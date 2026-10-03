/*
 * GsRenderer on Vulkan (gs_renderer.h): triangles batched by state and drawn
 * with dynamic rendering into R8G8B8A8 targets, one per (FBP, FBW, PSM).
 *
 * Everything is synchronous for now - a flush records, submits and waits - which
 * keeps the first steps simple to check; batching across frames and keeping the
 * GPU busy come once the output matches the software GS.
 */

#include <cstring>
#include <map>
#include <tuple>

#include "gpu_internal.h"
#include "gs_frag.h"
#include "gs_vert.h"

namespace ps2gpu {

namespace {

struct PackedVertex {
    float x, y;
    std::uint8_t r, g, b, a;
};

struct Target {
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    std::uint32_t width = 0, height = 0;
    VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
};

void Transition(VkCommandBuffer cmd, Target& t, VkImageLayout to, VkAccessFlags dst_access,
                VkPipelineStageFlags dst_stage) {
    if (t.layout == to) return;
    VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    b.oldLayout = t.layout;
    b.newLayout = to;
    b.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
    b.dstAccessMask = dst_access;
    b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = t.image;
    b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, dst_stage, 0, 0, nullptr, 0, nullptr, 1, &b);
    t.layout = to;
}

class VulkanGsRenderer final : public GsRenderer {
public:
    explicit VulkanGsRenderer(VulkanGpu& gpu) : g_(gpu) {}
    ~VulkanGsRenderer() override;
    bool Init(std::string* error);
    void DrawTriangles(const GsDrawState& state, const GsVertex* vertices, std::size_t count) override;
    void Flush() override;
    bool ReadTarget(std::uint32_t fbp, std::uint32_t fbw, std::uint32_t psm, std::vector<std::uint32_t>* pixels,
                    std::uint32_t* width, std::uint32_t* height) override;

private:
    using Key = std::tuple<std::uint32_t, std::uint32_t, std::uint32_t>;
    Target* TargetFor(const GsDrawState& state);
    bool Buffer(VkDeviceSize bytes, VkBufferUsageFlags usage, VkBuffer* buffer, VkDeviceMemory* memory, void** mapped);

    VulkanGpu& g_;
    VkPipelineLayout layout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
    std::map<Key, Target> targets_;
    GsDrawState state_;
    std::vector<PackedVertex> pending_;
    VkBuffer vertices_ = VK_NULL_HANDLE;
    VkDeviceMemory vertex_memory_ = VK_NULL_HANDLE;
    void* vertex_map_ = nullptr;
    VkDeviceSize vertex_capacity_ = 0;
};

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

bool VulkanGsRenderer::Init(std::string* error) {
    auto module = [&](const std::uint32_t* code, std::size_t bytes) {
        VkShaderModuleCreateInfo ci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        ci.codeSize = bytes;
        ci.pCode = code;
        VkShaderModule m = VK_NULL_HANDLE;
        vkCreateShaderModule(g_.device, &ci, nullptr, &m);
        return m;
    };
    VkShaderModule vert = module(spirv_gs_vert, sizeof spirv_gs_vert);
    VkShaderModule frag = module(spirv_gs_frag, sizeof spirv_gs_frag);
    if (!vert || !frag) {
        if (error) *error = "the GS shaders did not load";
        return false;
    }
    VkPushConstantRange push{VK_SHADER_STAGE_VERTEX_BIT, 0, 8};
    VkPipelineLayoutCreateInfo lci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    lci.pushConstantRangeCount = 1;
    lci.pPushConstantRanges = &push;
    vkCreatePipelineLayout(g_.device, &lci, nullptr, &layout_);

    VkPipelineShaderStageCreateInfo stages[2] = {{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO},
                                                 {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO}};
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vert;
    stages[0].pName = "main";
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = frag;
    stages[1].pName = "main";
    VkVertexInputBindingDescription binding{0, sizeof(PackedVertex), VK_VERTEX_INPUT_RATE_VERTEX};
    VkVertexInputAttributeDescription attributes[2] = {
        {0, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(PackedVertex, x)},
        {1, 0, VK_FORMAT_R8G8B8A8_UNORM, offsetof(PackedVertex, r)}};
    VkPipelineVertexInputStateCreateInfo input{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    input.vertexBindingDescriptionCount = 1;
    input.pVertexBindingDescriptions = &binding;
    input.vertexAttributeDescriptionCount = 2;
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
    VkPipelineColorBlendAttachmentState blend{};
    blend.colorWriteMask = 0xF;
    VkPipelineColorBlendStateCreateInfo blending{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    blending.attachmentCount = 1;
    blending.pAttachments = &blend;
    VkDynamicState dynamic_states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynamic.dynamicStateCount = 2;
    dynamic.pDynamicStates = dynamic_states;
    const VkFormat format = VK_FORMAT_R8G8B8A8_UNORM;
    VkPipelineRenderingCreateInfo rendering{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
    rendering.colorAttachmentCount = 1;
    rendering.pColorAttachmentFormats = &format;
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
    pci.pDynamicState = &dynamic;
    pci.layout = layout_;
    const VkResult r = vkCreateGraphicsPipelines(g_.device, VK_NULL_HANDLE, 1, &pci, nullptr, &pipeline_);
    vkDestroyShaderModule(g_.device, vert, nullptr);
    vkDestroyShaderModule(g_.device, frag, nullptr);
    if (r != VK_SUCCESS) {
        if (error) *error = "vkCreateGraphicsPipelines failed";
        return false;
    }
    return true;
}

Target* VulkanGsRenderer::TargetFor(const GsDrawState& state) {
    Target& t = targets_[Key{state.fbp, state.fbw, state.psm}];
    if (t.image) return &t;
    t.width = state.width;
    t.height = state.height;
    VkImageCreateInfo ici{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ici.imageType = VK_IMAGE_TYPE_2D;
    ici.format = VK_FORMAT_R8G8B8A8_UNORM;
    ici.extent = {t.width, t.height, 1};
    ici.mipLevels = 1;
    ici.arrayLayers = 1;
    ici.samples = VK_SAMPLE_COUNT_1_BIT;
    ici.tiling = VK_IMAGE_TILING_OPTIMAL;
    ici.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    vkCreateImage(g_.device, &ici, nullptr, &t.image);
    VkMemoryRequirements req;
    vkGetImageMemoryRequirements(g_.device, t.image, &req);
    VkMemoryAllocateInfo mai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    mai.allocationSize = req.size;
    mai.memoryTypeIndex = g_.FindMemory(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    vkAllocateMemory(g_.device, &mai, nullptr, &t.memory);
    vkBindImageMemory(g_.device, t.image, t.memory, 0);
    VkImageViewCreateInfo vci{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    vci.image = t.image;
    vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vci.format = VK_FORMAT_R8G8B8A8_UNORM;
    vci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCreateImageView(g_.device, &vci, nullptr, &t.view);
    // Local memory starts as zeros.
    g_.Run([&](VkCommandBuffer cmd) {
        Transition(cmd, t, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT,
                   VK_PIPELINE_STAGE_TRANSFER_BIT);
        const VkClearColorValue zero{};
        const VkImageSubresourceRange all{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        vkCmdClearColorImage(cmd, t.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &zero, 1, &all);
    });
    return &t;
}

void VulkanGsRenderer::DrawTriangles(const GsDrawState& state, const GsVertex* v, std::size_t count) {
    const bool same = state.fbp == state_.fbp && state.fbw == state_.fbw && state.psm == state_.psm &&
                      state.scissor_x0 == state_.scissor_x0 && state.scissor_y0 == state_.scissor_y0 &&
                      state.scissor_x1 == state_.scissor_x1 && state.scissor_y1 == state_.scissor_y1;
    if (!same) Flush();
    state_ = state;
    for (std::size_t i = 0; i + 2 < count; i += 3) {
        for (int k = 0; k < 3; ++k) {
            // Flat shading takes the colour of the triangle's last vertex.
            const GsVertex& c = state.gouraud ? v[i + k] : v[i + 2];
            pending_.push_back({v[i + k].x, v[i + k].y, c.r, c.g, c.b, c.a});
        }
    }
}

void VulkanGsRenderer::Flush() {
    if (pending_.empty()) return;
    const VkDeviceSize bytes = pending_.size() * sizeof(PackedVertex);
    if (bytes > vertex_capacity_) {
        if (vertices_) {
            vkDestroyBuffer(g_.device, vertices_, nullptr);
            vkFreeMemory(g_.device, vertex_memory_, nullptr);
        }
        vertex_capacity_ = bytes * 2;
        Buffer(vertex_capacity_, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, &vertices_, &vertex_memory_, &vertex_map_);
    }
    std::memcpy(vertex_map_, pending_.data(), bytes);
    Target* t = TargetFor(state_);
    const std::uint32_t count = std::uint32_t(pending_.size());
    g_.Run([&](VkCommandBuffer cmd) {
        Transition(cmd, *t, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                   VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
        VkRenderingAttachmentInfo colour{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
        colour.imageView = t->view;
        colour.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        colour.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        colour.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        VkRenderingInfo ri{VK_STRUCTURE_TYPE_RENDERING_INFO};
        ri.renderArea = {{0, 0}, {t->width, t->height}};
        ri.layerCount = 1;
        ri.colorAttachmentCount = 1;
        ri.pColorAttachments = &colour;
        vkCmdBeginRendering(cmd, &ri);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
        VkViewport vp{0, 0, float(t->width), float(t->height), 0, 1};
        vkCmdSetViewport(cmd, 0, 1, &vp);
        const std::int32_t x0 = std::max(0, state_.scissor_x0), y0 = std::max(0, state_.scissor_y0);
        const std::int32_t x1 = std::min(std::int32_t(t->width) - 1, state_.scissor_x1);
        const std::int32_t y1 = std::min(std::int32_t(t->height) - 1, state_.scissor_y1);
        VkRect2D sc{{x0, y0}, {std::uint32_t(std::max(0, x1 - x0 + 1)), std::uint32_t(std::max(0, y1 - y0 + 1))}};
        vkCmdSetScissor(cmd, 0, 1, &sc);
        const float size[2] = {float(t->width), float(t->height)};
        vkCmdPushConstants(cmd, layout_, VK_SHADER_STAGE_VERTEX_BIT, 0, 8, size);
        const VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(cmd, 0, 1, &vertices_, &offset);
        vkCmdDraw(cmd, count, 1, 0, 0);
        vkCmdEndRendering(cmd);
    });
    pending_.clear();
}

bool VulkanGsRenderer::ReadTarget(std::uint32_t fbp, std::uint32_t fbw, std::uint32_t psm,
                                  std::vector<std::uint32_t>* pixels, std::uint32_t* width, std::uint32_t* height) {
    Flush();
    auto found = targets_.find(Key{fbp, fbw, psm});
    if (found == targets_.end()) return false;
    Target& t = found->second;
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
    vkDeviceWaitIdle(g_.device);
    for (auto& [key, t] : targets_) {
        vkDestroyImageView(g_.device, t.view, nullptr);
        vkDestroyImage(g_.device, t.image, nullptr);
        vkFreeMemory(g_.device, t.memory, nullptr);
    }
    if (vertices_) {
        vkDestroyBuffer(g_.device, vertices_, nullptr);
        vkFreeMemory(g_.device, vertex_memory_, nullptr);
    }
    if (pipeline_) vkDestroyPipeline(g_.device, pipeline_, nullptr);
    if (layout_) vkDestroyPipelineLayout(g_.device, layout_, nullptr);
}

}  // namespace

std::unique_ptr<GsRenderer> CreateGsRenderer(Gpu& gpu, std::string* error) {
    auto renderer = std::make_unique<VulkanGsRenderer>(static_cast<VulkanGpu&>(gpu));
    if (!renderer->Init(error)) return nullptr;
    return renderer;
}

}  // namespace ps2gpu
