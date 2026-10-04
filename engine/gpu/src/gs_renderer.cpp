/*
 * GsRenderer on Vulkan (gs_renderer.h): triangles batched by state and drawn
 * with dynamic rendering into R8G8B8A8 targets, one per (FBP, FBW, PSM).
 *
 * One vertex and one fragment shader (shaders/gs.vert, gs.frag) do the GS's
 * per-pixel work: the texture function, the alpha test. Blending is the
 * fixed-function blender's, with the GS's factor - source alpha or FIX, where
 * 0x80 is 1.0 - as the fragment's second output (dual-source blending): the
 * GS's Cv = (A - B) * C + D becomes a source and a destination factor and an
 * operation, one pipeline per ALPHA setting, made when first used.
 *
 * Everything is synchronous for now - a flush records, submits and waits - which
 * keeps the first steps simple to check; batching across frames and keeping the
 * GPU busy come once the output matches the software GS.
 */

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>
#include <tuple>

#include "gpu_internal.h"
#include "gs_frag.h"
#include "gs_frag_single.h"
#include "gs_vert.h"
#include "date_frag.h"
#include "date_vert.h"
#include "swizzle_comp.h"
#include "decode_comp.h"

namespace ps2gpu {

namespace {

struct PackedVertex {
    float x, y;
    std::uint8_t r, g, b, a;
    float s, t, q;
    std::uint32_t z;
    std::uint8_t fog, pad[3];
};

/* What the fragment shader is told, after the vertex shader's 16 bytes. */
struct FragmentParams {
    std::uint32_t textured, tfx, tcc, atst, aref, fix_factor, fix, afail, fge, fogcol, pabe;
    std::uint32_t bilinear, wms, wmt, minu, maxu, minv, maxv;
    std::uint32_t tw, th, tex_format, ta0, ta1, aem;
    std::uint32_t logic, keep;
};
constexpr std::uint32_t kFragmentOffset = 16;

struct Image {
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    std::uint32_t width = 0, height = 0;
    VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT;
    VkDescriptorSet sampled = VK_NULL_HANDLE;   /* a target read by the DATE setup pass */
};

// Depth with stencil: the stencil carries the destination alpha test.
constexpr VkFormat kDepthFormat = VK_FORMAT_D32_SFLOAT_S8_UINT;

struct Texture {
    Image image;
    VkDescriptorSet sets[8] = {};       /* by sampler */
};

void Transition(VkCommandBuffer cmd, Image& t, VkImageLayout to, VkAccessFlags dst_access,
                VkPipelineStageFlags dst_stage) {
    if (t.layout == to) return;
    VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    b.oldLayout = t.layout;
    b.newLayout = to;
    b.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
    b.dstAccessMask = dst_access;
    b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = t.image;
    b.subresourceRange = {t.aspect, 0, 1, 0, 1};
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, dst_stage, 0, 0, nullptr, 0, nullptr, 1, &b);
    t.layout = to;
}

/* Everything written so far, visible to everything after. */
void MemoryBarrier(VkCommandBuffer cmd) {
    VkMemoryBarrier b{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    b.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
    b.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 1, &b, 0,
                         nullptr, 0, nullptr);
}

/* decode.comp's push constants: GsTextureDecode without the CLUT. */
struct DecodeParams {
    std::uint32_t tbp, tbw, psm, tw, th, wms, wmt, minu, maxu, minv, maxv, texa_lo, ta1, csa;
};

/* swizzle.comp's push constants. */
struct SwizzleParams {
    std::uint32_t bp, bw, psm, cols, rows, stride, to_memory, depth, row0;
};

constexpr VkDeviceSize kLocalMemory = 4u << 20;
constexpr VkDeviceSize kLinear = VkDeviceSize(2048) * 1024 * 4;    // the largest image, a value a pixel

/* FBMSK as the bits of the target's RGBA bytes it keeps. A 16-bit format's
   five bits a channel are the top five of the byte (the rest stay 0 either
   way, so count as kept), its alpha bit the byte's top bit; a 24-bit format
   keeps the alpha it does not store. */
std::uint32_t KeptBits(std::uint32_t fbmsk, std::uint32_t psm) {
    if (psm == 0x02 || psm == 0x0A || psm == 0x32 || psm == 0x3A) {
        const std::uint32_t r = ((fbmsk & 31) << 3) | 7, g = (((fbmsk >> 5) & 31) << 3) | 7;
        const std::uint32_t b = (((fbmsk >> 10) & 31) << 3) | 7, a = (fbmsk & 0x8000) ? 0xFF : 0x7F;
        return r | g << 8 | b << 16 | a << 24;
    }
    if (psm == 0x01 || psm == 0x31) return fbmsk | 0xFF000000u;
    return fbmsk;
}

/* The channels written whole or in part (Vulkan's write mask: R, G, B, A in
   bits 0-3). */
std::uint32_t WriteMask(std::uint32_t kept) {
    std::uint32_t mask = 0;
    for (int channel = 0; channel < 4; ++channel)
        if (((kept >> (8 * channel)) & 0xFF) != 0xFF) mask |= 1u << channel;
    return mask;
}

/* Whether some channel is written only in part. */
bool PartlyMasked(std::uint32_t kept) {
    for (int channel = 0; channel < 4; ++channel) {
        const std::uint32_t byte = (kept >> (8 * channel)) & 0xFF;
        if (byte != 0 && byte != 0xFF) return true;
    }
    return false;
}

/* The pipeline a state needs: blending and its operands, and the write mask
   (bits 9-12, inverted so that 0 is the plain pipeline). */
std::uint32_t PipelineKey(const GsDrawState& s) {
    const std::uint32_t masked = (~WriteMask(KeptBits(s.fbmsk, s.psm)) & 0xF) << 9;
    if (!s.blend) return masked;
    return masked | 1u | std::uint32_t(s.a) << 1 | std::uint32_t(s.b) << 3 | std::uint32_t(s.c) << 5 |
           std::uint32_t(s.d) << 7;
}

/* Cv = (A - B) * C + D as the blender's src * sf (op) dst * df. Each of Cs and
   Cd ends up with a coefficient (A is it) - (B is it), times C, plus (D is it):
   0, 1, C, -C, 1 - C, or 1 + C (more than the blender's factors reach; taken
   as 1). */
VkPipelineColorBlendAttachmentState Blending(std::uint32_t key, bool dual_source) {
    VkPipelineColorBlendAttachmentState b{};
    b.colorWriteMask = ~(key >> 9) & 0xF;           // R, G, B, A are bits 0-3, as Vulkan has them
    if (!(key & 1) || (key >> 13) & 3) return b;    // (a logic op replaces blending)
    const auto a = BlendColour((key >> 1) & 3), bb = BlendColour((key >> 3) & 3), d = BlendColour((key >> 7) & 3);
    const auto c = BlendAlpha((key >> 5) & 3);
    const bool dest_alpha = c == BlendAlpha::Dest;
    const VkBlendFactor kC = dest_alpha ? VK_BLEND_FACTOR_DST_ALPHA
                             : dual_source ? VK_BLEND_FACTOR_SRC1_ALPHA : VK_BLEND_FACTOR_SRC_ALPHA;
    const VkBlendFactor kOneMinusC = dest_alpha ? VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA
                                     : dual_source ? VK_BLEND_FACTOR_ONE_MINUS_SRC1_ALPHA
                                                   : VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    auto coefficient = [&](BlendColour who, bool* negative) {
        const int m = int(a == who) - int(bb == who);
        const bool plus_one = d == who;
        *negative = false;
        if (m == 0) return plus_one ? VK_BLEND_FACTOR_ONE : VK_BLEND_FACTOR_ZERO;
        if (m == 1) return plus_one ? VK_BLEND_FACTOR_ONE : kC;
        if (plus_one) return kOneMinusC;
        *negative = true;
        return kC;
    };
    bool src_negative, dst_negative;
    b.srcColorBlendFactor = coefficient(BlendColour::Source, &src_negative);
    b.dstColorBlendFactor = coefficient(BlendColour::Dest, &dst_negative);
    b.colorBlendOp = VK_BLEND_OP_ADD;
    if (!src_negative && dst_negative) b.colorBlendOp = VK_BLEND_OP_SUBTRACT;
    else if (src_negative && !dst_negative) b.colorBlendOp = VK_BLEND_OP_REVERSE_SUBTRACT;
    else if (src_negative && dst_negative) b.srcColorBlendFactor = b.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
    // The GS writes the source alpha as it is; blending is for colour only.
    b.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    b.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    b.alphaBlendOp = VK_BLEND_OP_ADD;
    b.blendEnable = VK_TRUE;
    return b;
}

bool SameBatch(const GsDrawState& x, const GsDrawState& y) {
    return x.fbp == y.fbp && x.fbw == y.fbw && x.psm == y.psm && x.fbmsk == y.fbmsk && x.scissor_x0 == y.scissor_x0 &&
           x.scissor_y0 == y.scissor_y0 && x.scissor_x1 == y.scissor_x1 && x.scissor_y1 == y.scissor_y1 &&
           x.textured == y.textured && (!x.textured || (x.texture == y.texture && x.tfx == y.tfx &&
           x.tcc == y.tcc && x.bilinear == y.bilinear && x.wms == y.wms && x.wmt == y.wmt && x.minu == y.minu &&
           x.maxu == y.maxu && x.minv == y.minv && x.maxv == y.maxv && x.tw == y.tw && x.th == y.th &&
           x.texture_target == y.texture_target && x.tex_fbp == y.tex_fbp && x.tex_fbw == y.tex_fbw &&
           x.tex_target_psm == y.tex_target_psm && x.tex_format == y.tex_format && x.ta0 == y.ta0 &&
           x.ta1 == y.ta1 && x.aem == y.aem)) &&
           x.alpha_test == y.alpha_test && (!x.alpha_test || (x.atst == y.atst && x.aref == y.aref && x.afail == y.afail)) &&
           PipelineKey(x) == PipelineKey(y) && (!x.blend || (x.fix == y.fix && x.pabe == y.pabe)) && x.depth == y.depth &&
           (!x.depth || (x.zbp == y.zbp && x.ztst == y.ztst && x.depth_write == y.depth_write)) &&
           x.fog == y.fog && (!x.fog || x.fogcol == y.fogcol) && x.date == y.date && (!x.date || x.datm == y.datm);
}

class VulkanGsRenderer final : public GsRenderer {
public:
    explicit VulkanGsRenderer(VulkanGpu& gpu) : g_(gpu) {}
    ~VulkanGsRenderer() override;
    bool Init(std::string* error);
    void UploadTexture(std::uint64_t key, const std::uint32_t* pixels, std::uint32_t width,
                       std::uint32_t height) override;
    void UploadDepth(std::uint32_t zbp, std::uint32_t width, std::uint32_t height, const std::uint32_t* z) override;
    void UploadTarget(std::uint32_t fbp, std::uint32_t fbw, std::uint32_t psm, const std::uint32_t* pixels,
                      std::uint32_t width, std::uint32_t height) override;
    void ReleaseTexture(std::uint64_t key) override;
    bool HasTarget(std::uint32_t fbp, std::uint32_t fbw, std::uint32_t psm) const override {
        return targets_.count(Key{fbp, fbw, psm}) != 0;
    }
    void DrawTriangles(const GsDrawState& state, const GsVertex* vertices, std::size_t count) override;
    void Flush() override;
    void Kick() override {
        Flush();
        g_.Flush();
    }
    bool ReadDepth(std::uint32_t zbp, std::uint32_t width, std::uint32_t height, std::vector<std::uint32_t>* z) override;
    void MemoryWrite(std::uint32_t begin, std::uint32_t end, const std::uint8_t* bytes) override;
    void MemoryRead(std::uint32_t begin, std::uint32_t end, std::uint8_t* bytes) override;
    bool TargetToMemory(std::uint32_t fbp, std::uint32_t fbw, std::uint32_t psm, std::uint32_t cols,
                        std::uint32_t rows, std::uint32_t first_row) override;
    void TargetFromMemory(std::uint32_t fbp, std::uint32_t fbw, std::uint32_t psm, std::uint32_t width,
                          std::uint32_t height) override;
    bool DepthToMemory(std::uint32_t zbp, std::uint32_t width, std::uint32_t height, std::uint32_t fbw,
                       std::uint32_t zpsm, std::uint32_t cols, std::uint32_t rows, std::uint32_t first_row) override;
    void DepthFromMemory(std::uint32_t zbp, std::uint32_t width, std::uint32_t height, std::uint32_t fbw,
                         std::uint32_t zpsm) override;
    void TextureFromMemory(std::uint64_t key, const GsTextureDecode& decode) override;
    bool ReadTarget(std::uint32_t fbp, std::uint32_t fbw, std::uint32_t psm, std::vector<std::uint32_t>* pixels,
                    std::uint32_t* width, std::uint32_t* height) override;
    bool ReadTexture(std::uint64_t key, std::vector<std::uint32_t>* pixels, std::uint32_t* width,
                     std::uint32_t* height) override;
    bool ReadImage(Image& image, std::vector<std::uint32_t>* pixels);

private:
    using Key = std::tuple<std::uint32_t, std::uint32_t, std::uint32_t>;
    Image* TargetFor(const GsDrawState& state);
    /* The depth buffer a draw uses: ZBP's, or the target's own when it tests
       nothing (dynamic rendering wants one attachment layout throughout). */
    Image* DepthFor(std::uint32_t key, std::uint32_t width, std::uint32_t height);
    bool MakeImage(Image& image, std::uint32_t width, std::uint32_t height, VkImageUsageFlags usage,
                   VkFormat format = VK_FORMAT_R8G8B8A8_UNORM);
    void DestroyImage(Image& image);
    void Fill(Texture& texture, const std::uint32_t* pixels);
    VkPipeline PipelineFor(std::uint32_t key);
    /* The destination alpha test's setup pass: stencil 1 where the target's
       alpha bit equals DATM. */
    bool MakeDatePipeline();
    void DateSetup(VkCommandBuffer cmd, Image& target, Image& depth, std::uint32_t datm);
    /* A target's descriptor for sampling it. */
    VkDescriptorSet SampledSet(Image& image);
    Image feedback_;                /* a copy of the target being drawn, when it is also the texture */
    /* The rendering pass the last batch left open, so the next one into the
       same target and depth buffer goes on in it (thousands of batches a
       frame, each beginning and ending its own, cost the GPU most of its
       time); anything else recorded ends it (VulkanGpu::end_pass). */
    Image* open_target_ = nullptr;
    Image* open_depth_ = nullptr;
    void EndPass(VkCommandBuffer cmd) {
        if (!open_target_) return;
        vkCmdEndRendering(cmd);
        open_target_ = open_depth_ = nullptr;
    }
    VkPipeline date_pipeline_ = VK_NULL_HANDLE;
    VkDescriptorSet SetFor(Texture& texture, int sampler);
    bool Buffer(VkDeviceSize bytes, VkBufferUsageFlags usage, VkBuffer* buffer, VkDeviceMemory* memory, void** mapped);

    VulkanGpu& g_;
    VkShaderModule vert_ = VK_NULL_HANDLE, frag_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout set_layout_ = VK_NULL_HANDLE;
    VkDescriptorPool pool_ = VK_NULL_HANDLE;
    VkSampler samplers_[8] = {};    /* bilinear << 2 | clamp_s << 1 | clamp_t */
    VkPipelineLayout layout_ = VK_NULL_HANDLE;
    std::map<std::uint32_t, VkPipeline> pipelines_;
    std::map<Key, Image> targets_;
    std::map<Key, Image> depths_;   /* by (ZBP or a target's own, width, height) */
    std::map<std::uint64_t, Texture> textures_;
    Texture blank_;                 /* bound when nothing is textured */
    GsDrawState state_;
    std::vector<PackedVertex> pending_;
    /* Vertex data for what is recorded: appended to chunks of host-visible
       memory, all reusable once the submission that reads them has run. */
    struct VertexChunk {
        VkBuffer buffer;
        VkDeviceMemory memory;
        void* mapped;
        VkDeviceSize size;
    };
    // One ring a command buffer slot: a slot's vertices are free again once
    // its submission has run, whatever the others are doing.
    struct VertexRing {
        std::vector<VertexChunk> chunks;
        std::size_t chunk = 0;
        VkDeviceSize used = 0;
        bool reset_pending = false;
    };
    VertexRing vertex_rings_[VulkanGpu::kSlots];
    void* Vertices(VkDeviceSize bytes, VkBuffer* buffer, VkDeviceSize* offset);
    /* A staging buffer, freed once the submission that reads it has run. */
    void FreeAfterSubmit(VkBuffer buffer, VkDeviceMemory memory);

    /* GS local memory on the GPU (swizzle.comp): the memory itself, and the
       linear rows images are copied through on their way in and out. */
    bool InitMemory(std::string* error);
    bool DeviceBuffer(VkDeviceSize bytes, VkBufferUsageFlags usage, VkBuffer* buffer, VkDeviceMemory* memory);
    void Swizzle(VkCommandBuffer cmd, const SwizzleParams& params);
    /* An image's first rows into the linear buffer, or back. */
    void ImageToLinear(VkCommandBuffer cmd, Image& image, VkImageAspectFlags aspect, std::uint32_t rows,
                       std::uint32_t first_row = 0);
    void LinearToImage(VkCommandBuffer cmd, Image& image, VkImageAspectFlags aspect);
    /* A texture image of this size under `key`, made (or made again) if need be. */
    Texture* TextureImage(std::uint64_t key, std::uint32_t width, std::uint32_t height);
    VkBuffer gsmem_ = VK_NULL_HANDLE, linear_ = VK_NULL_HANDLE, clut_ = VK_NULL_HANDLE;
    VkDeviceMemory gsmem_memory_ = VK_NULL_HANDLE, linear_memory_ = VK_NULL_HANDLE, clut_memory_ = VK_NULL_HANDLE;
    VkShaderModule decode_ = VK_NULL_HANDLE;
    VkPipeline decode_pipeline_ = VK_NULL_HANDLE;
    VkShaderModule swizzle_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout swizzle_set_layout_ = VK_NULL_HANDLE;
    VkDescriptorPool swizzle_pool_ = VK_NULL_HANDLE;
    VkDescriptorSet swizzle_set_ = VK_NULL_HANDLE;
    VkPipelineLayout swizzle_layout_ = VK_NULL_HANDLE;
    VkPipeline swizzle_pipeline_ = VK_NULL_HANDLE;
};

void* VulkanGsRenderer::Vertices(VkDeviceSize bytes, VkBuffer* buffer, VkDeviceSize* offset) {
    // The slot the draws using these are recorded into.
    VertexRing& ring = vertex_rings_[g_.slot()];
    if (!ring.reset_pending) {
        ring.reset_pending = true;
        g_.AfterSubmit([&ring] {
            ring.chunk = 0;
            ring.used = 0;
            ring.reset_pending = false;
        });
    }
    const VkDeviceSize aligned = (bytes + 255) & ~VkDeviceSize(255);
    while (ring.chunk < ring.chunks.size() && ring.used + aligned > ring.chunks[ring.chunk].size) {
        ++ring.chunk;
        ring.used = 0;
    }
    if (ring.chunk == ring.chunks.size()) {
        VertexChunk chunk{};
        chunk.size = std::max<VkDeviceSize>(VkDeviceSize(8) << 20, aligned);
        if (!Buffer(chunk.size, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, &chunk.buffer, &chunk.memory, &chunk.mapped))
            return nullptr;
        ring.chunks.push_back(chunk);
        ring.used = 0;
    }
    VertexChunk& chunk = ring.chunks[ring.chunk];
    *buffer = chunk.buffer;
    *offset = ring.used;
    void* at = static_cast<std::uint8_t*>(chunk.mapped) + ring.used;
    ring.used += aligned;
    return at;
}

void VulkanGsRenderer::FreeAfterSubmit(VkBuffer buffer, VkDeviceMemory memory) {
    VkDevice device = g_.device;
    g_.AfterSubmit([device, buffer, memory] {
        vkUnmapMemory(device, memory);
        vkDestroyBuffer(device, buffer, nullptr);
        vkFreeMemory(device, memory, nullptr);
    });
}

bool VulkanGsRenderer::Buffer(VkDeviceSize bytes, VkBufferUsageFlags usage, VkBuffer* buffer,
                              VkDeviceMemory* memory, void** mapped) {
    VkBufferCreateInfo bci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bci.size = bytes;
    bci.usage = usage;
    if (vkCreateBuffer(g_.device, &bci, nullptr, buffer) != VK_SUCCESS) return false;
    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(g_.device, *buffer, &req);
    VkMemoryAllocateInfo mai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    mai.allocationSize = req.size;
    mai.memoryTypeIndex = g_.FindMemory(req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                                              VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (vkAllocateMemory(g_.device, &mai, nullptr, memory) != VK_SUCCESS) return false;
    vkBindBufferMemory(g_.device, *buffer, *memory, 0);
    return vkMapMemory(g_.device, *memory, 0, bytes, 0, mapped) == VK_SUCCESS;
}

bool VulkanGsRenderer::MakeImage(Image& t, std::uint32_t width, std::uint32_t height, VkImageUsageFlags usage,
                                 VkFormat format) {
    t.width = width;
    t.height = height;
    t.aspect = format == kDepthFormat ? VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT
                                      : VK_IMAGE_ASPECT_COLOR_BIT;
    VkImageCreateInfo ici{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ici.imageType = VK_IMAGE_TYPE_2D;
    ici.format = format;
    ici.extent = {width, height, 1};
    ici.mipLevels = 1;
    ici.arrayLayers = 1;
    ici.samples = VK_SAMPLE_COUNT_1_BIT;
    ici.tiling = VK_IMAGE_TILING_OPTIMAL;
    ici.usage = usage;
    if (vkCreateImage(g_.device, &ici, nullptr, &t.image) != VK_SUCCESS) return false;
    VkMemoryRequirements req;
    vkGetImageMemoryRequirements(g_.device, t.image, &req);
    VkMemoryAllocateInfo mai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    mai.allocationSize = req.size;
    mai.memoryTypeIndex = g_.FindMemory(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (vkAllocateMemory(g_.device, &mai, nullptr, &t.memory) != VK_SUCCESS) return false;
    vkBindImageMemory(g_.device, t.image, t.memory, 0);
    VkImageViewCreateInfo vci{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    vci.image = t.image;
    vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vci.format = format;
    vci.subresourceRange = {t.aspect, 0, 1, 0, 1};
    return vkCreateImageView(g_.device, &vci, nullptr, &t.view) == VK_SUCCESS;
}

void VulkanGsRenderer::DestroyImage(Image& t) {
    if (t.view) vkDestroyImageView(g_.device, t.view, nullptr);
    if (t.image) vkDestroyImage(g_.device, t.image, nullptr);
    if (t.memory) vkFreeMemory(g_.device, t.memory, nullptr);
    t = Image{};
}

bool VulkanGsRenderer::Init(std::string* error) {
    auto module = [&](const std::uint32_t* code, std::size_t bytes) {
        VkShaderModuleCreateInfo ci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        ci.codeSize = bytes;
        ci.pCode = code;
        VkShaderModule m = VK_NULL_HANDLE;
        vkCreateShaderModule(g_.device, &ci, nullptr, &m);
        return m;
    };
    vert_ = module(spirv_gs_vert, sizeof spirv_gs_vert);
    frag_ = g_.dual_source ? module(spirv_gs_frag, sizeof spirv_gs_frag)
                           : module(spirv_gs_frag_single, sizeof spirv_gs_frag_single);
    if (!vert_ || !frag_) {
        if (error) *error = "the GS shaders did not load";
        return false;
    }
    VkDescriptorSetLayoutBinding binding{0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
                                         VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
    VkDescriptorSetLayoutCreateInfo dli{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    dli.bindingCount = 1;
    dli.pBindings = &binding;
    vkCreateDescriptorSetLayout(g_.device, &dli, nullptr, &set_layout_);
    VkDescriptorPoolSize size{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 8192};
    VkDescriptorPoolCreateInfo dpi{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    dpi.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    dpi.maxSets = 8192;
    dpi.poolSizeCount = 1;
    dpi.pPoolSizes = &size;
    vkCreateDescriptorPool(g_.device, &dpi, nullptr, &pool_);
    for (int i = 0; i < 8; ++i) {
        VkSamplerCreateInfo sci{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        sci.magFilter = sci.minFilter = (i & 4) ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
        sci.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        sci.addressModeU = (i & 2) ? VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE : VK_SAMPLER_ADDRESS_MODE_REPEAT;
        sci.addressModeV = (i & 1) ? VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE : VK_SAMPLER_ADDRESS_MODE_REPEAT;
        sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sci.maxLod = 0.0f;
        vkCreateSampler(g_.device, &sci, nullptr, &samplers_[i]);
    }
    VkPushConstantRange push[2] = {{VK_SHADER_STAGE_VERTEX_BIT, 0, 8},
                                   {VK_SHADER_STAGE_FRAGMENT_BIT, kFragmentOffset, sizeof(FragmentParams)}};
    VkPipelineLayoutCreateInfo lci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    lci.setLayoutCount = 1;
    lci.pSetLayouts = &set_layout_;
    lci.pushConstantRangeCount = 2;
    lci.pPushConstantRanges = push;
    vkCreatePipelineLayout(g_.device, &lci, nullptr, &layout_);
    if (!PipelineFor(0) || !MakeDatePipeline()) {
        if (error) *error = "vkCreateGraphicsPipelines failed";
        return false;
    }
    const std::uint32_t white = 0xFFFFFFFFu;
    if (!MakeImage(blank_.image, 1, 1, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT)) {
        if (error) *error = "no image for the blank texture";
        return false;
    }
    Fill(blank_, &white);
    g_.end_pass = [this](VkCommandBuffer cmd) { EndPass(cmd); };
    return InitMemory(error);
}

bool VulkanGsRenderer::DeviceBuffer(VkDeviceSize bytes, VkBufferUsageFlags usage, VkBuffer* buffer,
                                    VkDeviceMemory* memory) {
    VkBufferCreateInfo bci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bci.size = bytes;
    bci.usage = usage;
    if (vkCreateBuffer(g_.device, &bci, nullptr, buffer) != VK_SUCCESS) return false;
    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(g_.device, *buffer, &req);
    VkMemoryAllocateInfo mai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    mai.allocationSize = req.size;
    mai.memoryTypeIndex = g_.FindMemory(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (vkAllocateMemory(g_.device, &mai, nullptr, memory) != VK_SUCCESS) return false;
    return vkBindBufferMemory(g_.device, *buffer, *memory, 0) == VK_SUCCESS;
}

bool VulkanGsRenderer::InitMemory(std::string* error) {
    const VkBufferUsageFlags usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
                                     VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    if (!DeviceBuffer(kLocalMemory, usage, &gsmem_, &gsmem_memory_) ||
        !DeviceBuffer(kLinear, usage, &linear_, &linear_memory_) ||
        !DeviceBuffer(1024, usage, &clut_, &clut_memory_)) {
        if (error) *error = "no GPU memory for GS local memory";
        return false;
    }
    VkShaderModuleCreateInfo ci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    ci.codeSize = sizeof spirv_swizzle_comp;
    ci.pCode = spirv_swizzle_comp;
    vkCreateShaderModule(g_.device, &ci, nullptr, &swizzle_);
    VkDescriptorSetLayoutBinding bindings[3] = {
        {0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr}};
    VkDescriptorSetLayoutCreateInfo dli{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    dli.bindingCount = 3;
    dli.pBindings = bindings;
    vkCreateDescriptorSetLayout(g_.device, &dli, nullptr, &swizzle_set_layout_);
    VkDescriptorPoolSize size{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 3};
    VkDescriptorPoolCreateInfo dpi{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    dpi.maxSets = 1;
    dpi.poolSizeCount = 1;
    dpi.pPoolSizes = &size;
    vkCreateDescriptorPool(g_.device, &dpi, nullptr, &swizzle_pool_);
    VkDescriptorSetAllocateInfo dai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    dai.descriptorPool = swizzle_pool_;
    dai.descriptorSetCount = 1;
    dai.pSetLayouts = &swizzle_set_layout_;
    if (vkAllocateDescriptorSets(g_.device, &dai, &swizzle_set_) != VK_SUCCESS) {
        if (error) *error = "no descriptor set for GS local memory";
        return false;
    }
    VkDescriptorBufferInfo infos[3] = {{gsmem_, 0, VK_WHOLE_SIZE}, {linear_, 0, VK_WHOLE_SIZE},
                                       {clut_, 0, VK_WHOLE_SIZE}};
    VkWriteDescriptorSet writes[3] = {};
    for (int i = 0; i < 3; ++i) {
        writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstSet = swizzle_set_;
        writes[i].dstBinding = std::uint32_t(i);
        writes[i].descriptorCount = 1;
        writes[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[i].pBufferInfo = &infos[i];
    }
    vkUpdateDescriptorSets(g_.device, 3, writes, 0, nullptr);
    VkPushConstantRange push{VK_SHADER_STAGE_COMPUTE_BIT, 0,
                             std::uint32_t(std::max(sizeof(SwizzleParams), sizeof(DecodeParams)))};
    VkPipelineLayoutCreateInfo lci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    lci.setLayoutCount = 1;
    lci.pSetLayouts = &swizzle_set_layout_;
    lci.pushConstantRangeCount = 1;
    lci.pPushConstantRanges = &push;
    vkCreatePipelineLayout(g_.device, &lci, nullptr, &swizzle_layout_);
    VkComputePipelineCreateInfo cpi{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
    cpi.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    cpi.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    cpi.stage.module = swizzle_;
    cpi.stage.pName = "main";
    cpi.layout = swizzle_layout_;
    if (!swizzle_ ||
        vkCreateComputePipelines(g_.device, VK_NULL_HANDLE, 1, &cpi, nullptr, &swizzle_pipeline_) != VK_SUCCESS) {
        if (error) *error = "the swizzle shader did not load";
        return false;
    }
    ci.codeSize = sizeof spirv_decode_comp;
    ci.pCode = spirv_decode_comp;
    vkCreateShaderModule(g_.device, &ci, nullptr, &decode_);
    cpi.stage.module = decode_;
    if (!decode_ ||
        vkCreateComputePipelines(g_.device, VK_NULL_HANDLE, 1, &cpi, nullptr, &decode_pipeline_) != VK_SUCCESS) {
        if (error) *error = "the texture decode shader did not load";
        return false;
    }
    // Local memory starts as zeros, the CPU's copy too.
    g_.Record([&](VkCommandBuffer cmd) {
        vkCmdFillBuffer(cmd, gsmem_, 0, kLocalMemory, 0);
        MemoryBarrier(cmd);
    });
    return true;
}

void VulkanGsRenderer::Swizzle(VkCommandBuffer cmd, const SwizzleParams& params) {
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, swizzle_pipeline_);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, swizzle_layout_, 0, 1, &swizzle_set_, 0, nullptr);
    vkCmdPushConstants(cmd, swizzle_layout_, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof params, &params);
    vkCmdDispatch(cmd, (params.cols + 7) / 8, (params.rows + 7) / 8, 1);
}

void VulkanGsRenderer::ImageToLinear(VkCommandBuffer cmd, Image& image, VkImageAspectFlags aspect,
                                     std::uint32_t rows, std::uint32_t first_row) {
    Transition(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_ACCESS_TRANSFER_READ_BIT,
               VK_PIPELINE_STAGE_TRANSFER_BIT);
    VkBufferImageCopy copy{};
    copy.bufferRowLength = image.width;
    copy.imageSubresource = {aspect, 0, 0, 1};
    copy.imageOffset = {0, std::int32_t(first_row), 0};
    copy.imageExtent = {image.width, rows, 1};
    vkCmdCopyImageToBuffer(cmd, image.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, linear_, 1, &copy);
    MemoryBarrier(cmd);
}

void VulkanGsRenderer::LinearToImage(VkCommandBuffer cmd, Image& image, VkImageAspectFlags aspect) {
    MemoryBarrier(cmd);
    Transition(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT,
               VK_PIPELINE_STAGE_TRANSFER_BIT);
    VkBufferImageCopy copy{};
    copy.bufferRowLength = image.width;
    copy.imageSubresource = {aspect, 0, 0, 1};
    copy.imageExtent = {image.width, image.height, 1};
    vkCmdCopyBufferToImage(cmd, linear_, image.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
    MemoryBarrier(cmd);
}

void VulkanGsRenderer::MemoryWrite(std::uint32_t begin, std::uint32_t end, const std::uint8_t* bytes) {
    if (end <= begin) return;
    Flush();
    const VkDeviceSize size = VkDeviceSize(end - begin) * 256;
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    void* mapped = nullptr;
    if (!Buffer(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, &buffer, &memory, &mapped)) return;
    std::memcpy(mapped, bytes, std::size_t(size));
    g_.Record([&](VkCommandBuffer cmd) {
        MemoryBarrier(cmd);
        const VkBufferCopy copy{0, VkDeviceSize(begin) * 256, size};
        vkCmdCopyBuffer(cmd, buffer, gsmem_, 1, &copy);
        MemoryBarrier(cmd);
    });
    FreeAfterSubmit(buffer, memory);
}

void VulkanGsRenderer::MemoryRead(std::uint32_t begin, std::uint32_t end, std::uint8_t* bytes) {
    if (end <= begin) return;
    Flush();
    const VkDeviceSize size = VkDeviceSize(end - begin) * 256;
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    void* mapped = nullptr;
    if (!Buffer(size, VK_BUFFER_USAGE_TRANSFER_DST_BIT, &buffer, &memory, &mapped)) return;
    g_.Run([&](VkCommandBuffer cmd) {
        MemoryBarrier(cmd);
        const VkBufferCopy copy{VkDeviceSize(begin) * 256, 0, size};
        vkCmdCopyBuffer(cmd, gsmem_, buffer, 1, &copy);
        MemoryBarrier(cmd);
    });
    std::memcpy(bytes, mapped, std::size_t(size));
    vkUnmapMemory(g_.device, memory);
    vkDestroyBuffer(g_.device, buffer, nullptr);
    vkFreeMemory(g_.device, memory, nullptr);
}

bool VulkanGsRenderer::TargetToMemory(std::uint32_t fbp, std::uint32_t fbw, std::uint32_t psm, std::uint32_t cols,
                                      std::uint32_t rows, std::uint32_t first_row) {
    auto found = targets_.find(Key{fbp, fbw, psm});
    if (found == targets_.end()) return false;
    Flush();
    Image& t = found->second;
    cols = std::min(cols, t.width);
    rows = std::min(rows, t.height);
    if (first_row >= rows) return true;
    rows -= first_row;
    if (!cols) return true;
    g_.Record([&](VkCommandBuffer cmd) {
        ImageToLinear(cmd, t, VK_IMAGE_ASPECT_COLOR_BIT, rows, first_row);
        Swizzle(cmd, SwizzleParams{fbp * 32, fbw, psm, cols, rows, t.width, 1, 0, first_row});
        MemoryBarrier(cmd);
    });
    return true;
}

void VulkanGsRenderer::TargetFromMemory(std::uint32_t fbp, std::uint32_t fbw, std::uint32_t psm,
                                        std::uint32_t width, std::uint32_t height) {
    Flush();
    GsDrawState state;
    state.fbp = fbp;
    state.fbw = fbw;
    state.psm = psm;
    state.width = width;
    state.height = height;
    Image* t = TargetFor(state);
    g_.Record([&](VkCommandBuffer cmd) {
        MemoryBarrier(cmd);
        Swizzle(cmd, SwizzleParams{fbp * 32, fbw, psm, t->width, t->height, t->width, 0, 0, 0});
        LinearToImage(cmd, *t, VK_IMAGE_ASPECT_COLOR_BIT);
    });
}

bool VulkanGsRenderer::DepthToMemory(std::uint32_t zbp, std::uint32_t width, std::uint32_t height, std::uint32_t fbw,
                                     std::uint32_t zpsm, std::uint32_t cols, std::uint32_t rows,
                                     std::uint32_t first_row) {
    auto found = depths_.find(Key{zbp, width, height});
    if (found == depths_.end()) return false;
    Flush();
    Image& d = found->second;
    cols = std::min(cols, d.width);
    rows = std::min(rows, d.height);
    if (first_row >= rows) return true;
    rows -= first_row;
    if (!cols) return true;
    g_.Record([&](VkCommandBuffer cmd) {
        ImageToLinear(cmd, d, VK_IMAGE_ASPECT_DEPTH_BIT, rows, first_row);
        Swizzle(cmd, SwizzleParams{zbp * 32, fbw, zpsm, cols, rows, d.width, 1, 1, first_row});
        MemoryBarrier(cmd);
    });
    return true;
}

void VulkanGsRenderer::DepthFromMemory(std::uint32_t zbp, std::uint32_t width, std::uint32_t height,
                                       std::uint32_t fbw, std::uint32_t zpsm) {
    Flush();
    Image* d = DepthFor(zbp, width, height);
    g_.Record([&](VkCommandBuffer cmd) {
        MemoryBarrier(cmd);
        Swizzle(cmd, SwizzleParams{zbp * 32, fbw, zpsm, d->width, d->height, d->width, 0, 1, 0});
        LinearToImage(cmd, *d, VK_IMAGE_ASPECT_DEPTH_BIT);
    });
}

VkPipeline VulkanGsRenderer::PipelineFor(std::uint32_t key) {
    auto found = pipelines_.find(key);
    if (found != pipelines_.end()) return found->second;
    VkPipelineShaderStageCreateInfo stages[2] = {{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO},
                                                 {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO}};
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vert_;
    stages[0].pName = "main";
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = frag_;
    stages[1].pName = "main";
    VkVertexInputBindingDescription binding{0, sizeof(PackedVertex), VK_VERTEX_INPUT_RATE_VERTEX};
    VkVertexInputAttributeDescription attributes[5] = {
        {0, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(PackedVertex, x)},
        {1, 0, VK_FORMAT_R8G8B8A8_UNORM, offsetof(PackedVertex, r)},
        {2, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(PackedVertex, s)},
        {3, 0, VK_FORMAT_R32_UINT, offsetof(PackedVertex, z)},
        {4, 0, VK_FORMAT_R8_UNORM, offsetof(PackedVertex, fog)}};
    VkPipelineVertexInputStateCreateInfo input{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    input.vertexBindingDescriptionCount = 1;
    input.pVertexBindingDescriptions = &binding;
    input.vertexAttributeDescriptionCount = 5;
    input.pVertexAttributeDescriptions = attributes;
    VkPipelineInputAssemblyStateCreateInfo assembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo viewport{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    viewport.viewportCount = 1;
    viewport.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode = VK_CULL_MODE_NONE;
    raster.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo multisample{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    const VkPipelineColorBlendAttachmentState blend = Blending(key, g_.dual_source);
    VkPipelineColorBlendStateCreateInfo blending{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    if ((key >> 13) & 3) {                          // bits 13-14: the logic op of a partly masked draw
        blending.logicOpEnable = VK_TRUE;
        blending.logicOp = ((key >> 13) & 3) == 1 ? VK_LOGIC_OP_AND : VK_LOGIC_OP_OR;
    }
    blending.attachmentCount = 1;
    blending.pAttachments = &blend;
    // Depth's test, write and comparison are set per batch (Vulkan 1.3).
    // The destination alpha test: drawn only where the setup pass set stencil 1.
    VkPipelineDepthStencilStateCreateInfo depth{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    depth.front = {VK_STENCIL_OP_KEEP, VK_STENCIL_OP_KEEP, VK_STENCIL_OP_KEEP, VK_COMPARE_OP_EQUAL, 1, 0, 1};
    depth.back = depth.front;
    VkDynamicState dynamic_states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR,
                                       VK_DYNAMIC_STATE_DEPTH_TEST_ENABLE, VK_DYNAMIC_STATE_DEPTH_WRITE_ENABLE,
                                       VK_DYNAMIC_STATE_DEPTH_COMPARE_OP, VK_DYNAMIC_STATE_STENCIL_TEST_ENABLE};
    VkPipelineDynamicStateCreateInfo dynamic{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynamic.dynamicStateCount = 6;
    dynamic.pDynamicStates = dynamic_states;
    const VkFormat format = VK_FORMAT_R8G8B8A8_UNORM;
    VkPipelineRenderingCreateInfo rendering{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
    rendering.colorAttachmentCount = 1;
    rendering.pColorAttachmentFormats = &format;
    rendering.depthAttachmentFormat = kDepthFormat;
    rendering.stencilAttachmentFormat = kDepthFormat;
    VkGraphicsPipelineCreateInfo pci{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    pci.pNext = &rendering;
    pci.stageCount = 2;
    pci.pStages = stages;
    pci.pVertexInputState = &input;
    pci.pInputAssemblyState = &assembly;
    pci.pViewportState = &viewport;
    pci.pRasterizationState = &raster;
    pci.pMultisampleState = &multisample;
    pci.pColorBlendState = &blending;
    pci.pDepthStencilState = &depth;
    pci.pDynamicState = &dynamic;
    pci.layout = layout_;
    VkPipeline pipeline = VK_NULL_HANDLE;
    if (vkCreateGraphicsPipelines(g_.device, VK_NULL_HANDLE, 1, &pci, nullptr, &pipeline) != VK_SUCCESS)
        return VK_NULL_HANDLE;
    pipelines_[key] = pipeline;
    return pipeline;
}

bool VulkanGsRenderer::MakeDatePipeline() {
    auto module = [&](const std::uint32_t* code, std::size_t bytes) {
        VkShaderModuleCreateInfo ci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        ci.codeSize = bytes;
        ci.pCode = code;
        VkShaderModule m = VK_NULL_HANDLE;
        vkCreateShaderModule(g_.device, &ci, nullptr, &m);
        return m;
    };
    VkShaderModule vert = module(spirv_date_vert, sizeof spirv_date_vert);
    VkShaderModule frag = module(spirv_date_frag, sizeof spirv_date_frag);
    VkPipelineShaderStageCreateInfo stages[2] = {{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO},
                                                 {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO}};
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vert;
    stages[0].pName = "main";
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = frag;
    stages[1].pName = "main";
    VkPipelineVertexInputStateCreateInfo input{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkPipelineInputAssemblyStateCreateInfo assembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo viewport{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    viewport.viewportCount = 1;
    viewport.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode = VK_CULL_MODE_NONE;
    raster.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo multisample{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo depth{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    depth.stencilTestEnable = VK_TRUE;
    depth.front = {VK_STENCIL_OP_KEEP, VK_STENCIL_OP_REPLACE, VK_STENCIL_OP_KEEP, VK_COMPARE_OP_ALWAYS, 1, 1, 1};
    depth.back = depth.front;
    VkPipelineColorBlendStateCreateInfo blending{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    VkDynamicState dynamic_states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynamic.dynamicStateCount = 2;
    dynamic.pDynamicStates = dynamic_states;
    VkPipelineRenderingCreateInfo rendering{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
    rendering.depthAttachmentFormat = kDepthFormat;
    rendering.stencilAttachmentFormat = kDepthFormat;
    VkGraphicsPipelineCreateInfo pci{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    pci.pNext = &rendering;
    pci.stageCount = 2;
    pci.pStages = stages;
    pci.pVertexInputState = &input;
    pci.pInputAssemblyState = &assembly;
    pci.pViewportState = &viewport;
    pci.pRasterizationState = &raster;
    pci.pMultisampleState = &multisample;
    pci.pDepthStencilState = &depth;
    pci.pColorBlendState = &blending;
    pci.pDynamicState = &dynamic;
    pci.layout = layout_;
    const VkResult r = vkCreateGraphicsPipelines(g_.device, VK_NULL_HANDLE, 1, &pci, nullptr, &date_pipeline_);
    vkDestroyShaderModule(g_.device, vert, nullptr);
    vkDestroyShaderModule(g_.device, frag, nullptr);
    return r == VK_SUCCESS;
}

VkDescriptorSet VulkanGsRenderer::SampledSet(Image& image) {
    if (image.sampled) return image.sampled;
    VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    ai.descriptorPool = pool_;
    ai.descriptorSetCount = 1;
    ai.pSetLayouts = &set_layout_;
    if (vkAllocateDescriptorSets(g_.device, &ai, &image.sampled) != VK_SUCCESS) return VK_NULL_HANDLE;
    VkDescriptorImageInfo info{samplers_[3], image.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstSet = image.sampled;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo = &info;
    vkUpdateDescriptorSets(g_.device, 1, &write, 0, nullptr);
    return image.sampled;
}

void VulkanGsRenderer::DateSetup(VkCommandBuffer cmd, Image& target, Image& depth, std::uint32_t datm) {
    if (!SampledSet(target)) return;
    Transition(cmd, target, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_SHADER_READ_BIT,
               VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
    Transition(cmd, depth, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
               VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
               VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT);
    VkRenderingAttachmentInfo z{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    z.imageView = depth.view;
    z.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    z.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    z.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    VkRenderingAttachmentInfo stencil = z;
    stencil.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;   // 0 everywhere first
    stencil.clearValue.depthStencil = {0.0f, 0};
    VkRenderingInfo ri{VK_STRUCTURE_TYPE_RENDERING_INFO};
    ri.renderArea = {{0, 0}, {target.width, target.height}};
    ri.layerCount = 1;
    ri.pDepthAttachment = &z;
    ri.pStencilAttachment = &stencil;
    vkCmdBeginRendering(cmd, &ri);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, date_pipeline_);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout_, 0, 1, &target.sampled, 0, nullptr);
    VkViewport vp{0, 0, float(target.width), float(target.height), 0, 1};
    vkCmdSetViewport(cmd, 0, 1, &vp);
    VkRect2D all{{0, 0}, {target.width, target.height}};
    vkCmdSetScissor(cmd, 0, 1, &all);
    FragmentParams params{};
    params.textured = datm;                         // the setup shader's first word is DATM
    vkCmdPushConstants(cmd, layout_, VK_SHADER_STAGE_FRAGMENT_BIT, kFragmentOffset, sizeof params, &params);
    vkCmdDraw(cmd, 3, 1, 0, 0);
    vkCmdEndRendering(cmd);
}

VkDescriptorSet VulkanGsRenderer::SetFor(Texture& texture, int sampler) {
    VkDescriptorSet& set = texture.sets[sampler];
    if (set) return set;
    VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    ai.descriptorPool = pool_;
    ai.descriptorSetCount = 1;
    ai.pSetLayouts = &set_layout_;
    if (vkAllocateDescriptorSets(g_.device, &ai, &set) != VK_SUCCESS) return VK_NULL_HANDLE;
    VkDescriptorImageInfo info{samplers_[sampler], texture.image.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstSet = set;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo = &info;
    vkUpdateDescriptorSets(g_.device, 1, &write, 0, nullptr);
    return set;
}

void VulkanGsRenderer::Fill(Texture& texture, const std::uint32_t* pixels) {
    Image& t = texture.image;
    const VkDeviceSize bytes = VkDeviceSize(t.width) * t.height * 4;
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    void* mapped = nullptr;
    if (!Buffer(bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, &buffer, &memory, &mapped)) return;
    std::memcpy(mapped, pixels, bytes);
    g_.Record([&](VkCommandBuffer cmd) {
        Transition(cmd, t, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT,
                   VK_PIPELINE_STAGE_TRANSFER_BIT);
        VkBufferImageCopy copy{};
        copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        copy.imageExtent = {t.width, t.height, 1};
        vkCmdCopyBufferToImage(cmd, buffer, t.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
        Transition(cmd, t, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_SHADER_READ_BIT,
                   VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
    });
    FreeAfterSubmit(buffer, memory);
}

void VulkanGsRenderer::UploadTexture(std::uint64_t key, const std::uint32_t* pixels, std::uint32_t width,
                                     std::uint32_t height) {
    Flush();                        // what is pending may sample the old contents
    Texture* texture = TextureImage(key, width, height);
    if (texture) Fill(*texture, pixels);
}

Texture* VulkanGsRenderer::TextureImage(std::uint64_t key, std::uint32_t width, std::uint32_t height) {
    Texture& texture = textures_[key];
    if (texture.image.width != width || texture.image.height != height) {
        if (texture.image.image) g_.Submit();   // recorded draws may still sample it
        for (VkDescriptorSet& set : texture.sets)
            if (set) {
                vkFreeDescriptorSets(g_.device, pool_, 1, &set);
                set = VK_NULL_HANDLE;
            }
        DestroyImage(texture.image);
        if (!MakeImage(texture.image, width, height,
                       VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT))
            return nullptr;
    }
    return &texture;
}

void VulkanGsRenderer::TextureFromMemory(std::uint64_t key, const GsTextureDecode& d) {
    Flush();                        // what is pending may sample the old contents
    Texture* texture = TextureImage(key, d.tw, d.th);
    if (!texture) return;
    Image& t = texture->image;
    const DecodeParams params{d.tbp, d.tbw, d.psm, d.tw, d.th, d.wms, d.wmt, d.minu, d.maxu, d.minv, d.maxv,
                              d.texa_lo, d.ta1, d.csa};
    g_.Record([&](VkCommandBuffer cmd) {
        MemoryBarrier(cmd);
        if (d.clut) vkCmdUpdateBuffer(cmd, clut_, 0, 1024, d.clut);
        MemoryBarrier(cmd);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, decode_pipeline_);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, swizzle_layout_, 0, 1, &swizzle_set_, 0,
                                nullptr);
        vkCmdPushConstants(cmd, swizzle_layout_, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof params, &params);
        vkCmdDispatch(cmd, (d.tw + 7) / 8, (d.th + 7) / 8, 1);
        MemoryBarrier(cmd);
        Transition(cmd, t, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT,
                   VK_PIPELINE_STAGE_TRANSFER_BIT);
        VkBufferImageCopy copy{};
        copy.bufferRowLength = t.width;
        copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        copy.imageExtent = {t.width, t.height, 1};
        vkCmdCopyBufferToImage(cmd, linear_, t.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
        Transition(cmd, t, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_SHADER_READ_BIT,
                   VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
        MemoryBarrier(cmd);
    });
}

void VulkanGsRenderer::UploadTarget(std::uint32_t fbp, std::uint32_t fbw, std::uint32_t psm,
                                    const std::uint32_t* pixels, std::uint32_t width, std::uint32_t height) {
    Flush();                        // what is pending draws into the old contents first
    GsDrawState state;
    state.fbp = fbp;
    state.fbw = fbw;
    state.psm = psm;
    state.width = width;
    state.height = height;
    Image* t = TargetFor(state);
    const std::uint32_t w = std::min(width, t->width), h = std::min(height, t->height);
    const VkDeviceSize bytes = VkDeviceSize(w) * h * 4;
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    void* mapped = nullptr;
    if (!Buffer(bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, &buffer, &memory, &mapped)) return;
    for (std::uint32_t y = 0; y < h; ++y)
        std::memcpy(static_cast<std::uint8_t*>(mapped) + std::size_t(y) * w * 4, pixels + std::size_t(y) * width,
                    std::size_t(w) * 4);
    g_.Record([&](VkCommandBuffer cmd) {
        Transition(cmd, *t, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT,
                   VK_PIPELINE_STAGE_TRANSFER_BIT);
        VkBufferImageCopy copy{};
        copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        copy.imageExtent = {w, h, 1};
        vkCmdCopyBufferToImage(cmd, buffer, t->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
    });
    FreeAfterSubmit(buffer, memory);
}

void VulkanGsRenderer::ReleaseTexture(std::uint64_t key) {
    auto found = textures_.find(key);
    if (found == textures_.end()) return;
    Flush();
    Texture texture = found->second;
    textures_.erase(found);
    // Recorded draws may still sample it: it goes after they have run.
    VkDevice device = g_.device;
    VkDescriptorPool pool = pool_;
    g_.AfterSubmit([device, pool, texture]() mutable {
        for (VkDescriptorSet& set : texture.sets)
            if (set) vkFreeDescriptorSets(device, pool, 1, &set);
        if (texture.image.view) vkDestroyImageView(device, texture.image.view, nullptr);
        if (texture.image.image) vkDestroyImage(device, texture.image.image, nullptr);
        if (texture.image.memory) vkFreeMemory(device, texture.image.memory, nullptr);
    });
}

Image* VulkanGsRenderer::TargetFor(const GsDrawState& state) {
    Image& t = targets_[Key{state.fbp, state.fbw, state.psm}];
    if (t.image) return &t;
    MakeImage(t, state.width, state.height,
              VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                  VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
    // Local memory starts as zeros.
    g_.Record([&](VkCommandBuffer cmd) {
        Transition(cmd, t, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT,
                   VK_PIPELINE_STAGE_TRANSFER_BIT);
        const VkClearColorValue zero{};
        const VkImageSubresourceRange all{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        vkCmdClearColorImage(cmd, t.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &zero, 1, &all);
    });
    return &t;
}

Image* VulkanGsRenderer::DepthFor(std::uint32_t key, std::uint32_t width, std::uint32_t height) {
    Image& d = depths_[Key{key, width, height}];
    if (d.image) return &d;
    MakeImage(d, width, height,
              VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                  VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
              kDepthFormat);
    // Local memory starts as zeros: the farthest Z.
    g_.Record([&](VkCommandBuffer cmd) {
        Transition(cmd, d, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT,
                   VK_PIPELINE_STAGE_TRANSFER_BIT);
        const VkClearDepthStencilValue zero{0.0f, 0};
        const VkImageSubresourceRange all{VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT, 0, 1, 0, 1};
        vkCmdClearDepthStencilImage(cmd, d.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &zero, 1, &all);
    });
    return &d;
}

void VulkanGsRenderer::UploadDepth(std::uint32_t zbp, std::uint32_t width, std::uint32_t height,
                                   const std::uint32_t* z) {
    Flush();
    Image* d = DepthFor(zbp, width, height);
    const VkDeviceSize bytes = VkDeviceSize(width) * height * 4;
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    void* mapped = nullptr;
    if (!Buffer(bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, &buffer, &memory, &mapped)) return;
    float* out = static_cast<float*>(mapped);
    for (std::size_t i = 0; i < std::size_t(width) * height; ++i) out[i] = float(double(z[i]) / 4294967296.0);
    g_.Record([&](VkCommandBuffer cmd) {
        Transition(cmd, *d, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT,
                   VK_PIPELINE_STAGE_TRANSFER_BIT);
        VkBufferImageCopy copy{};
        copy.imageSubresource = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 0, 1};
        copy.imageExtent = {width, height, 1};
        vkCmdCopyBufferToImage(cmd, buffer, d->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
    });
    FreeAfterSubmit(buffer, memory);
}

void VulkanGsRenderer::DrawTriangles(const GsDrawState& state, const GsVertex* v, std::size_t count) {
    if (!SameBatch(state, state_)) Flush();
    state_ = state;
    for (std::size_t i = 0; i + 2 < count; i += 3) {
        for (int k = 0; k < 3; ++k) {
            // Flat shading takes the colour of the triangle's last vertex.
            const GsVertex& c = state.gouraud ? v[i + k] : v[i + 2];
            const GsVertex& p = v[i + k];
            pending_.push_back({p.x, p.y, c.r, c.g, c.b, c.a, p.s, p.t, p.q, p.z, p.fog, {0, 0, 0}});
        }
    }
}

void VulkanGsRenderer::Flush() {
    if (pending_.empty()) return;
    const VkDeviceSize bytes = pending_.size() * sizeof(PackedVertex);
    VkBuffer vertex_buffer = VK_NULL_HANDLE;
    VkDeviceSize vertex_offset = 0;
    void* vertex_data = Vertices(bytes, &vertex_buffer, &vertex_offset);
    if (!vertex_data) {
        pending_.clear();
        return;
    }
    std::memcpy(vertex_data, pending_.data(), bytes);
    Image* t = TargetFor(state_);
    // A target's own depth buffer is keyed past any ZBP (which has 9 bits).
    Image* depth = DepthFor(state_.depth ? state_.zbp : 0x10000u + state_.fbp, t->width, t->height);
    const VkPipeline pipeline = PipelineFor(PipelineKey(state_));
    Texture* texture = &blank_;
    if (state_.textured) {
        auto found = textures_.find(state_.texture);
        if (found != textures_.end()) texture = &found->second;
    }
    // The shader addresses and filters texels itself (texelFetch), as the GS does.
    const int sampler = 3;
    VkDescriptorSet set = SetFor(*texture, sampler);
    // Render to texture: the target itself, or a copy when it is also drawn into.
    Image* source = nullptr;
    bool copy_first = false;
    if (state_.textured && state_.texture_target) {
        auto found = targets_.find(Key{state_.tex_fbp, state_.tex_fbw, state_.tex_target_psm});
        if (found != targets_.end()) {
            source = &found->second;
            if (source == t) {
                if (feedback_.width != t->width || feedback_.height != t->height) {
                    g_.Submit();                        // what is recorded may still use the old copy
                    if (feedback_.sampled) vkFreeDescriptorSets(g_.device, pool_, 1, &feedback_.sampled);
                    feedback_.sampled = VK_NULL_HANDLE;
                    DestroyImage(feedback_);
                    MakeImage(feedback_, t->width, t->height, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);
                }
                copy_first = true;
                set = SampledSet(feedback_);
            } else {
                set = SampledSet(*source);
            }
        }
    }
    FragmentParams params{};
    params.textured = state_.textured && (texture != &blank_ || source);
    params.tfx = std::uint32_t(state_.tfx);
    params.tcc = state_.tcc;
    params.atst = state_.alpha_test ? state_.atst : 1;
    params.aref = state_.aref;
    // AFAIL: what a pixel failing the alpha test still writes. Drawn as up to
    // two passes - the passing pixels with everything, then the failing ones
    // with what AFAIL keeps (the shader's mode 0 drops failing pixels, 1
    // passing ones, 2 none).
    struct Pass { std::uint32_t mode; bool depth_write; std::uint32_t write_mask; };
    const bool depth_write = state_.depth && state_.depth_write;
    const std::uint32_t kept = KeptBits(state_.fbmsk, state_.psm);
    const std::uint32_t mask = WriteMask(kept);
    // A channel FBMSK masks only in part is drawn exactly with two logic ops -
    // which replace blending, so only where the blend leaves the source as it
    // is (Burnout 3's race writes two bits of each half of its frame's pixels
    // through a 16-bit view). Otherwise the channel is written whole.
    const bool source_only = !state_.blend || (state_.a == state_.b && state_.d == BlendColour::Source) ||
                             (state_.a == BlendColour::Source && state_.b == BlendColour::Zero &&
                              state_.c == BlendAlpha::Fix && state_.fix == 0x80 && state_.d == BlendColour::Zero);
    const bool logic = g_.logic_op && PartlyMasked(kept) && source_only;
    std::vector<Pass> passes;
    if (!state_.alpha_test || state_.atst == 1) {
        passes = {{2, depth_write, mask}};
    } else {
        switch (state_.afail) {
            case 0: passes = {{0, depth_write, mask}}; break;                                   // KEEP
            case 1:                                                                              // FB_ONLY
                if (depth_write) passes = {{0, true, mask}, {1, false, mask}};
                else passes = {{2, false, mask}};
                break;
            case 2:                                                                              // ZB_ONLY
                passes = {{0, depth_write, mask}};
                if (depth_write) passes.push_back({1, true, 0});
                break;
            default: passes = {{0, depth_write, mask}, {1, false, mask & 0x7u}}; break;          // RGB_ONLY
        }
    }
    params.fge = state_.fog;
    params.fogcol = state_.fogcol;
    params.fix_factor = state_.c == BlendAlpha::Fix;
    params.fix = state_.fix;
    params.bilinear = state_.bilinear;
    params.wms = state_.wms;
    params.wmt = state_.wmt;
    params.minu = state_.minu;
    params.maxu = state_.maxu;
    params.minv = state_.minv;
    params.maxv = state_.maxv;
    params.tw = state_.tw;
    params.th = state_.th;
    params.tex_format = source ? state_.tex_format : 0;
    params.ta0 = state_.ta0;
    params.ta1 = state_.ta1;
    params.aem = state_.aem;
    const std::uint32_t count = std::uint32_t(pending_.size());
    static int complaints = 0;
    if ((!pipeline || !set) && complaints < 5) {
        ++complaints;
        std::fprintf(stderr, "[ps2gpu] a batch was dropped: %s\n", pipeline ? "no descriptor set" : "no pipeline");
    }
    if (pipeline && set) {
        g_.RecordInPass([&](VkCommandBuffer cmd) {
            // Barriers and copies cannot go inside a rendering pass: a batch
            // needing one, or drawing elsewhere, ends the open pass first.
            const bool outside = state_.date || copy_first ||
                                 (source && source->layout != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            if (outside || open_target_ != t || open_depth_ != depth) EndPass(cmd);
            if (state_.date) DateSetup(cmd, *t, *depth, state_.datm);
            if (copy_first) {
                Transition(cmd, *t, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_ACCESS_TRANSFER_READ_BIT,
                           VK_PIPELINE_STAGE_TRANSFER_BIT);
                Transition(cmd, feedback_, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT,
                           VK_PIPELINE_STAGE_TRANSFER_BIT);
                VkImageCopy region{};
                region.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
                region.dstSubresource = region.srcSubresource;
                region.extent = {t->width, t->height, 1};
                vkCmdCopyImage(cmd, t->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, feedback_.image,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
                Transition(cmd, feedback_, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_SHADER_READ_BIT,
                           VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
            } else if (source) {
                Transition(cmd, *source, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_SHADER_READ_BIT,
                           VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
            }
            if (!open_target_) {
            Transition(cmd, *t, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                       VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT,
                       VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
            VkRenderingAttachmentInfo colour{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
            colour.imageView = t->view;
            colour.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            colour.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
            colour.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            Transition(cmd, *depth, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                       VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                       VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT);
            VkRenderingAttachmentInfo z{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
            z.imageView = depth->view;
            z.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            z.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
            z.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            VkRenderingInfo ri{VK_STRUCTURE_TYPE_RENDERING_INFO};
            ri.renderArea = {{0, 0}, {t->width, t->height}};
            ri.layerCount = 1;
            ri.colorAttachmentCount = 1;
            ri.pColorAttachments = &colour;
            ri.pDepthAttachment = &z;
            ri.pStencilAttachment = &z;
            vkCmdBeginRendering(cmd, &ri);
            open_target_ = t;
            open_depth_ = depth;
            }
            // ZTST: NEVER, ALWAYS, GEQUAL, GREATER - the larger Z is nearer.
            static const VkCompareOp kCompare[4] = {VK_COMPARE_OP_NEVER, VK_COMPARE_OP_ALWAYS,
                                                    VK_COMPARE_OP_GREATER_OR_EQUAL, VK_COMPARE_OP_GREATER};
            vkCmdSetDepthTestEnable(cmd, state_.depth ? VK_TRUE : VK_FALSE);
            vkCmdSetStencilTestEnable(cmd, state_.date ? VK_TRUE : VK_FALSE);
            vkCmdSetDepthCompareOp(cmd, kCompare[state_.ztst & 3]);
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout_, 0, 1, &set, 0, nullptr);
            VkViewport vp{0, 0, float(t->width), float(t->height), 0, 1};
            vkCmdSetViewport(cmd, 0, 1, &vp);
            const std::int32_t x0 = std::max(0, state_.scissor_x0), y0 = std::max(0, state_.scissor_y0);
            const std::int32_t x1 = std::min(std::int32_t(t->width) - 1, state_.scissor_x1);
            const std::int32_t y1 = std::min(std::int32_t(t->height) - 1, state_.scissor_y1);
            VkRect2D sc{{x0, y0}, {std::uint32_t(std::max(0, x1 - x0 + 1)), std::uint32_t(std::max(0, y1 - y0 + 1))}};
            vkCmdSetScissor(cmd, 0, 1, &sc);
            const float size[2] = {float(t->width), float(t->height)};
            vkCmdPushConstants(cmd, layout_, VK_SHADER_STAGE_VERTEX_BIT, 0, 8, size);
            vkCmdBindVertexBuffers(cmd, 0, 1, &vertex_buffer, &vertex_offset);
            // PABE splits each pass again: blended where As >= 0x80 (the shader's
            // pabe 1), not blended below (pabe 2, the pipeline without blending).
            const bool split = state_.blend && state_.pabe;
            for (const Pass& pass : passes) {
                for (std::uint32_t half = split ? 1 : 0; half <= (split ? 2u : 0u); ++half) {
                    std::uint32_t key = (PipelineKey(state_) & ~(0xFu << 9)) | ((~pass.write_mask & 0xFu) << 9);
                    if (half == 2) key &= 0xFu << 9;                // blending off
                    // Partly masked: AND with the kept bits, then OR the rest
                    // in (a channel this pass leaves alone keeps every bit).
                    std::uint32_t keep = kept;
                    for (int channel = 0; channel < 4; ++channel)
                        if (!(pass.write_mask >> channel & 1)) keep |= 0xFFu << (8 * channel);
                    for (std::uint32_t op = logic ? 1 : 0; op <= (logic ? 2u : 0u); ++op) {
                        const std::uint32_t op_key = op ? ((key & (0xFu << 9)) | op << 13) : key;
                        const VkPipeline chosen = op_key == PipelineKey(state_) ? pipeline : PipelineFor(op_key);
                        if (!chosen) continue;
                        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, chosen);
                        vkCmdSetDepthWriteEnable(cmd, pass.depth_write && op != 1 ? VK_TRUE : VK_FALSE);
                        params.afail = pass.mode;
                        params.pabe = half;
                        params.logic = op;
                        params.keep = keep;
                        vkCmdPushConstants(cmd, layout_, VK_SHADER_STAGE_FRAGMENT_BIT, kFragmentOffset,
                                           sizeof params, &params);
                        vkCmdDraw(cmd, count, 1, 0, 0);
                    }
                }
            }
        });
    }
    pending_.clear();
}

bool VulkanGsRenderer::ReadDepth(std::uint32_t zbp, std::uint32_t width, std::uint32_t height,
                                 std::vector<std::uint32_t>* z) {
    Flush();
    auto found = depths_.find(Key{zbp, width, height});
    if (found == depths_.end()) return false;
    Image& d = found->second;
    const VkDeviceSize bytes = VkDeviceSize(width) * height * 4;
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    void* mapped = nullptr;
    if (!Buffer(bytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT, &buffer, &memory, &mapped)) return false;
    g_.Run([&](VkCommandBuffer cmd) {
        Transition(cmd, d, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_ACCESS_TRANSFER_READ_BIT,
                   VK_PIPELINE_STAGE_TRANSFER_BIT);
        VkBufferImageCopy copy{};
        copy.imageSubresource = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 0, 1};
        copy.imageExtent = {width, height, 1};
        vkCmdCopyImageToBuffer(cmd, d.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buffer, 1, &copy);
    });
    // Z / 2^32 back to the GS's Z.
    const float* in = static_cast<const float*>(mapped);
    z->resize(std::size_t(width) * height);
    for (std::size_t i = 0; i < z->size(); ++i) {
        const double v = double(in[i]) * 4294967296.0 + 0.5;
        (*z)[i] = v <= 0.0 ? 0u : v >= 4294967295.0 ? 0xFFFFFFFFu : std::uint32_t(v);
    }
    vkUnmapMemory(g_.device, memory);
    vkDestroyBuffer(g_.device, buffer, nullptr);
    vkFreeMemory(g_.device, memory, nullptr);
    return true;
}

bool VulkanGsRenderer::ReadImage(Image& t, std::vector<std::uint32_t>* pixels) {
    Flush();
    const VkDeviceSize bytes = VkDeviceSize(t.width) * t.height * 4;
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    void* mapped = nullptr;
    if (!Buffer(bytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT, &buffer, &memory, &mapped)) return false;
    g_.Run([&](VkCommandBuffer cmd) {
        Transition(cmd, t, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_ACCESS_TRANSFER_READ_BIT,
                   VK_PIPELINE_STAGE_TRANSFER_BIT);
        VkBufferImageCopy copy{};
        copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        copy.imageExtent = {t.width, t.height, 1};
        vkCmdCopyImageToBuffer(cmd, t.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buffer, 1, &copy);
    });
    pixels->assign(std::size_t(t.width) * t.height, 0);
    std::memcpy(pixels->data(), mapped, bytes);
    vkUnmapMemory(g_.device, memory);
    vkDestroyBuffer(g_.device, buffer, nullptr);
    vkFreeMemory(g_.device, memory, nullptr);
    return true;
}

bool VulkanGsRenderer::ReadTexture(std::uint64_t key, std::vector<std::uint32_t>* pixels, std::uint32_t* width,
                                   std::uint32_t* height) {
    auto found = textures_.find(key);
    if (found == textures_.end() || !found->second.image.image) return false;
    *width = found->second.image.width;
    *height = found->second.image.height;
    return ReadImage(found->second.image, pixels);
}

bool VulkanGsRenderer::ReadTarget(std::uint32_t fbp, std::uint32_t fbw, std::uint32_t psm,
                                  std::vector<std::uint32_t>* pixels, std::uint32_t* width, std::uint32_t* height) {
    Flush();
    auto found = targets_.find(Key{fbp, fbw, psm});
    if (found == targets_.end()) return false;
    Image& t = found->second;
    const VkDeviceSize bytes = VkDeviceSize(t.width) * t.height * 4;
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    void* mapped = nullptr;
    if (!Buffer(bytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT, &buffer, &memory, &mapped)) return false;
    g_.Run([&](VkCommandBuffer cmd) {
        Transition(cmd, t, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_ACCESS_TRANSFER_READ_BIT,
                   VK_PIPELINE_STAGE_TRANSFER_BIT);
        VkBufferImageCopy copy{};
        copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        copy.imageExtent = {t.width, t.height, 1};
        vkCmdCopyImageToBuffer(cmd, t.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buffer, 1, &copy);
    });
    pixels->assign(std::size_t(t.width) * t.height, 0);
    std::memcpy(pixels->data(), mapped, bytes);
    *width = t.width;
    *height = t.height;
    vkUnmapMemory(g_.device, memory);
    vkDestroyBuffer(g_.device, buffer, nullptr);
    vkFreeMemory(g_.device, memory, nullptr);
    return true;
}

VulkanGsRenderer::~VulkanGsRenderer() {
    g_.Submit();
    vkDeviceWaitIdle(g_.device);
    for (auto& [key, t] : targets_) DestroyImage(t);
    for (auto& [key, d] : depths_) DestroyImage(d);
    for (auto& [key, texture] : textures_) DestroyImage(texture.image);
    DestroyImage(blank_.image);
    DestroyImage(feedback_);
    for (VertexRing& ring : vertex_rings_)
        for (VertexChunk& chunk : ring.chunks) {
            vkDestroyBuffer(g_.device, chunk.buffer, nullptr);
            vkFreeMemory(g_.device, chunk.memory, nullptr);
        }
    for (auto& [key, pipeline] : pipelines_) vkDestroyPipeline(g_.device, pipeline, nullptr);
    if (date_pipeline_) vkDestroyPipeline(g_.device, date_pipeline_, nullptr);
    for (VkSampler s : samplers_)
        if (s) vkDestroySampler(g_.device, s, nullptr);
    if (pool_) vkDestroyDescriptorPool(g_.device, pool_, nullptr);
    if (set_layout_) vkDestroyDescriptorSetLayout(g_.device, set_layout_, nullptr);
    if (layout_) vkDestroyPipelineLayout(g_.device, layout_, nullptr);
    if (vert_) vkDestroyShaderModule(g_.device, vert_, nullptr);
    if (frag_) vkDestroyShaderModule(g_.device, frag_, nullptr);
    if (swizzle_pipeline_) vkDestroyPipeline(g_.device, swizzle_pipeline_, nullptr);
    if (swizzle_layout_) vkDestroyPipelineLayout(g_.device, swizzle_layout_, nullptr);
    if (swizzle_pool_) vkDestroyDescriptorPool(g_.device, swizzle_pool_, nullptr);
    if (swizzle_set_layout_) vkDestroyDescriptorSetLayout(g_.device, swizzle_set_layout_, nullptr);
    if (swizzle_) vkDestroyShaderModule(g_.device, swizzle_, nullptr);
    if (decode_pipeline_) vkDestroyPipeline(g_.device, decode_pipeline_, nullptr);
    if (decode_) vkDestroyShaderModule(g_.device, decode_, nullptr);
    for (auto [buffer, memory] : {std::pair{gsmem_, gsmem_memory_}, std::pair{linear_, linear_memory_},
                                  std::pair{clut_, clut_memory_}}) {
        if (buffer) vkDestroyBuffer(g_.device, buffer, nullptr);
        if (memory) vkFreeMemory(g_.device, memory, nullptr);
    }
}

}  // namespace

std::string SubmitStats() {
    char text[96];
    std::snprintf(text, sizeof text, "%llu submissions, %.1f s waiting for them",
                  static_cast<unsigned long long>(g_submits), g_wait_seconds);
    return text;
}

std::unique_ptr<GsRenderer> CreateGsRenderer(Gpu& gpu, std::string* error) {
    auto renderer = std::make_unique<VulkanGsRenderer>(static_cast<VulkanGpu&>(gpu));
    if (!renderer->Init(error)) return nullptr;
    return renderer;
}

}  // namespace ps2gpu
