/*
 * The GPU GS renderer (engine/gpu, engine/docs/GPU.md) driven by the host's GS
 * model, in one of two modes.
 *
 * --gpu-shadow: beside the software GS, and checked against it. Every
 * primitive the software GS draws is also handed to ps2gpu with the state it was
 * drawn with. Textures are decoded from the software GS's local memory (with
 * --gpu-shadow-rtt, a texture that is a GPU target is sampled from the target
 * instead); depth buffers start each frame as local memory has them. At the end
 * of each frame every target drawn into is read back and compared with local
 * memory, and the agreement is reported.
 *
 * --gpu: instead of the software GS. The GS model still takes every GIF packet,
 * register and image transfer, but draws nothing (Gs::rasterize false); the GPU
 * draws, and its images stand for regions of local memory.
 *
 * Local memory is kept twice: the GS model's, which the CPU reads and writes,
 * and a copy on the GPU (GsRenderer::Memory*), which images are swizzled into
 * and out of by a compute shader; per 256-byte block the bridge knows which
 * copy is current. Each GPU image (a target or depth buffer, with the blocks
 * it covers) knows whether the GPU has drawn what memory lacks (gpu_newer) or
 * memory has changed under it (stale). Before the CPU reads or writes local
 * memory - a transfer, a local copy, a CLUT load, a texture decoded, the
 * picture shown - the GPU images over it are swizzled into the GPU's copy and
 * the blocks the CPU lacks come back to it; after it is written, the images
 * there are stale and reload, from the GPU's copy once the CPU's writes have
 * gone over. A target drawn under another width or format than one it
 * overlaps (Burnout 3's reflection buffers, the race's blur passes in its
 * depth buffer's memory), or a depth buffer drawn into as colour, is
 * reconciled the same way - all of it on the GPU, waiting for nothing. An image stands for the
 * rows drawn into so far, not all the scissor allows: Burnout 3's depth buffer
 * starts where its frame buffer's 448 rows end, and a 512-row reach would have
 * each depth write make the frame stale. A texture that is a target is sampled
 * from the target itself. Other textures are decoded into a cache keyed by
 * their registers, TEXA and their CLUT, dropped when memory under them is
 * written or the GPU draws over them.
 *
 * Known difference: the GPU's blender rounds where the GS truncates, so a
 * blended pixel can be 1 brighter (invisible; in shadow runs, Burnout 3's frame
 * copy doubles it into the 16-bit display buffer).
 *
 * Without PS2HOST_VULKAN the options report that they are unavailable.
 */

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <list>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

#include "ps2host/gs.h"
#include "ps2host/profile.h"
#include "ps2host/run.h"

#if PS2HOST_VULKAN
#include "ps2gpu/gs_renderer.h"
#endif

namespace ps2host {

#if PS2HOST_VULKAN

ps2gpu::Gpu* WindowGpu();       // window.cpp: the window's device, when there is one

namespace {

using gs::u32;
using gs::u64;

u32 Bits(u64 value, int low, int count) { return u32((value >> low) & ((1ull << count) - 1)); }

/* Bits a pixel of a format takes in local memory (the 8H/4HL/4HH formats live
   in 32-bit pixels). */
u32 PixelBits(u32 psm) {
    switch (psm) {
        case gs::PSMCT16: case gs::PSMCT16S: case gs::PSMZ16: case gs::PSMZ16S: return 16;
        case gs::PSMT8: return 8;
        case gs::PSMT4: return 4;
        default: return 32;
    }
}

bool Is16(u32 psm) { return psm == gs::PSMCT16 || psm == gs::PSMCT16S || psm == gs::PSMZ16 || psm == gs::PSMZ16S; }

/* The blocks a buffer's first `rows` rows take: whole rows of pages (32 blocks
   each), FBW pages wide. */
std::pair<u32, u32> Blocks(u32 fbp, u32 fbw, u32 psm, u32 rows) {
    const u32 page_rows = Is16(psm) ? 64 : 32;
    const u32 begin = fbp * 32;
    return {begin, begin + std::max(1u, (rows + page_rows - 1) / page_rows) * std::max(1u, fbw) * 32};
}

u32 To16(u32 c) {
    return ((c >> 3) & 31) | (((c >> 11) & 31) << 5) | (((c >> 19) & 31) << 10) | ((c >> 31) << 15);
}

u32 From16(u32 c) {
    return ((c & 31) << 3) | (((c >> 5) & 31) << 11) | (((c >> 10) & 31) << 19) | ((c & 0x8000) ? 0x80000000u : 0);
}

struct Extent {
    u32 x1 = 0, y1 = 0;             // the furthest pixel drawn, exclusive
};

/* A GPU image standing for a region of local memory (--gpu). */
struct Owned {
    u32 fbp = 0, fbw = 1, psm = 0;  // FBP (ZBP) in 2048-word units, FBW, PSM
    bool depth = false;
    u32 width = 0, height = 0;      // the image
    // The blocks it stands for: the rows used so far (drawn into, or sampled),
    // of all it could hold; of those, the ones the GPU has drawn into.
    u32 begin = 0, end = 0, full_end = 0;
    u32 drawn_end = 0;
    // The blocks drawn into since it last went to GPU memory: what another
    // image over them must wait for (not all it was ever drawn over - the
    // race's depth buffer is cleared to 512 rows once a frame, over the glow
    // buffers after its 448, and drawn into below 448 ten thousand times).
    u32 dirty_begin = ~0u, dirty_end = 0;
    u32 x1 = 0, y1 = 0;             // how much of it the GPU has drawn
    bool gpu_newer = false;         // drawn on the GPU, not yet in local memory
    bool stale = true;              // local memory changed since the image was loaded
    bool stale_beyond = false;      // ...in rows past `end`: stale when it grows into them
    int why = 0;                    // what made it stale: 0 new, 1 memory written, 2 a draw over it, 3 another's write-back
    u64 reloads[4] = {};            // reloads, by why
    u64 drawn_serial = 0;           // when the GPU last drew into it (Bridge::serial)
    u64 checked = 0;                // Bridge::newer_epoch when Use last looked at the others
};

/* A texture decoded into the renderer (--gpu). */
struct Cached {
    u64 renderer_key = 0;
    u32 begin = 0, end = 0;
    bool valid = false;
    u64 decoded_serial = 0;         // Bridge::serial when decoded
    u64 content = 0;                // a hash of its blocks when decoded
    bool decoded = false;
    // The images whose blocks it can share (all they could ever cover), as of
    // `owned_seen` images: images are only ever added, and live as long as
    // the bridge.
    std::vector<const Owned*> over;
    std::size_t owned_seen = 0;
    std::list<u64>::iterator lru;
};

struct Bridge {
    std::unique_ptr<ps2gpu::Gpu> gpu;
    std::unique_ptr<ps2gpu::GsRenderer> renderer;
    bool primary = false;
    // --gpu-shadow-rtt: a texture that is a GPU target is sampled from it, as
    // the GPU will have to on its own.
    bool targets_as_textures = false;

    // Shadow: the texture last decoded, by what decides its contents, and the
    // memory drawn into since.
    std::tuple<u64, u64, u64, u64> texture_state{~0ull, 0, 0, 0};
    u64 texture_key = 0;
    std::map<u32, u32> since_decode;  // start -> blocks
    bool drawn_into(u32 bp, u32 blocks) const {
        for (const auto& [start, count] : since_decode)
            if (start < bp + blocks && bp < start + count) return true;
        return false;
    }
    std::map<std::tuple<u32, u32, u32>, Extent> drawn;  // this frame's targets
    u64 frames = 0;
    double agreement_sum = 0;
    std::string dump_dir;           // a dump asked for, made at the next frame with drawing
    std::set<u32> depth_seeded;     // depth buffers loaded from local memory this frame

    // Primary.
    std::map<std::tuple<u32, u32, u32, bool>, Owned> owned;
    std::unordered_map<u64, Cached> textures;
    std::list<u64> texture_lru;     // most recent first
    // The textures over each page (32 blocks) of local memory, for what a
    // write there invalidates: scanning all 2,048 on every write was 3% of a
    // race. Elements of `textures` stay put until erased.
    std::vector<std::vector<Cached*>> texture_pages = std::vector<std::vector<Cached*>>(512);
    void IndexTexture(Cached* t, bool add) {
        for (u32 page = t->begin >> 5; page <= ((std::max(t->end, t->begin + 1) - 1) >> 5) && page < 512; ++page) {
            std::vector<Cached*>& list = texture_pages[page];
            if (add) {
                list.push_back(t);
            } else {
                auto found = std::find(list.begin(), list.end(), t);
                if (found != list.end()) {
                    *found = list.back();
                    list.pop_back();
                }
            }
        }
    }
    u64 next_texture = 1;
    u64 serial = 0;                 // counts GPU draws, so a texture knows if one went over it
    // Bumped when an image starts holding drawing memory lacks, or reaches
    // further: until then, an image Use has checked against the others needs
    // no second look (tens of thousands of draws a frame go to one target).
    u64 newer_epoch = 1;
    // Local memory is kept twice: the GS model's (the CPU's) and the
    // renderer's (the GPU's), which images are swizzled into and out of. Per
    // 256-byte block, which copy is current.
    enum : u8 { kSame = 0, kCpuNewer = 1, kGpuNewer = 2 };
    std::vector<u8> blocks = std::vector<u8>(16384, kSame);
    u64 downloads = 0, uploads = 0;  // blocks copied GPU -> CPU, CPU -> GPU
    // Why blocks came back to the CPU (PS2HOST_GPU_LOG): 0 a texture decoded,
    // 1 the GS model reading, 2 the GS model writing.
    int cause = 1;
    u64 downloads_by[3] = {};
    std::map<u32, u64> texture_downloads;   // by the texture's TEX0 PSM << 16 | TBW
    u64 write_backs = 0, reloads = 0, decodes = 0;
    u64 unchanged = 0;              // textures invalidated whose texels proved the same
    u64 gpu_decodes = 0;            // textures decoded on the GPU from its copy of local memory
    // The CLUT, in rows of 16 entries (bit r for row r): which the GS model
    // holds as loaded (a load from blocks the CPU has), which the GPU's copy
    // holds (loaded from GPU memory, where the GPU drew them, or sent over).
    // Every row is at least one of the two. A GPU load bumps its rows'
    // generations, which key the textures read through them.
    u32 clut_cpu = 0xFFFF, clut_gpu = 0;
    u64 clut_generation[16] = {};
    u64 gpu_clut_loads = 0;
    // PS2HOST_GPU_DUMP_FRAME=N (and PS2HOST_GPU_DUMP_DIR): during frame N, each
    // target as it stood when drawing moved on to another, as a BMP, with a
    // line per pass on stderr - to see which pass of a frame goes wrong.
    u64 frame = 0;
    long long dump_frame = -1;
    std::string dump_passes = ".";
    std::tuple<u32, u32, u32> last_target{~0u, 0, 0};
    u32 pass = 0;
};

Bridge* g_bridge = nullptr;

// ------------------------------------------------------------------ --gpu

/* Blocks [begin, end) whose copy on one side is behind the other's brought up
   to date, in runs: `to_gpu` the GPU's from the CPU's, else the CPU's from the
   GPU's (which waits for the GPU). */
void Coherent(Bridge& b, u32 begin, u32 end, bool to_gpu) {
    end = std::min(end, 16384u);
    const u8 behind = to_gpu ? Bridge::kCpuNewer : Bridge::kGpuNewer;
    u8* bytes = gs::TheGs().memory.data();
    for (u32 i = begin; i < end;) {
        if (b.blocks[i] != behind) {
            ++i;
            continue;
        }
        u32 j = i;
        while (j < end && b.blocks[j] == behind) b.blocks[j++] = Bridge::kSame;
        if (to_gpu) {
            b.renderer->MemoryWrite(i, j, bytes + std::size_t(i) * 256);
            b.uploads += j - i;
        } else {
            b.renderer->MemoryRead(i, j, bytes + std::size_t(i) * 256);
            b.downloads += j - i;
            b.downloads_by[b.cause] += j - i;
        }
        i = j;
    }
}

/* What the GPU drew into an image, swizzled into the GPU's local memory - on
   the GPU, nothing waiting. */
void WriteBack(Bridge& b, Owned& o) {
    Timed timed(Section::GpuSync);
    o.gpu_newer = false;
    if (o.dirty_end <= o.dirty_begin) return;
    // Only the rows drawn into since the last write-back: the image's other
    // rows may be stale - another image drew over them since, and memory has
    // that, not this.
    const u32 page_rows = Is16(o.psm) ? 64 : 32, row_blocks = std::max(1u, o.fbw) * 32;
    const u32 first = (o.dirty_begin - o.begin) / row_blocks * page_rows;
    const u32 last = std::min(o.y1, (o.dirty_end - o.begin) / row_blocks * page_rows);
    // Blocks the CPU wrote go over first: the image covers them only in part.
    Coherent(b, o.dirty_begin, o.dirty_end, true);
    const bool done = o.depth ? b.renderer->DepthToMemory(o.fbp, o.width, o.height, o.fbw, o.psm, o.x1, last, first)
                              : b.renderer->TargetToMemory(o.fbp, o.fbw, o.psm, o.x1, last, first);
    if (done) {
        ++b.write_backs;
        std::fill(b.blocks.begin() + std::min(o.dirty_begin, 16384u),
                  b.blocks.begin() + std::min(o.dirty_end, 16384u), Bridge::kGpuNewer);
    }
    o.dirty_begin = ~0u;
    o.dirty_end = 0;
}

/* Before the CPU reads or writes local memory [begin, end): what the GPU drew
   there comes back to it; written, the GPU images there must reload. */
void Sync(Bridge& b, u32 begin, u32 end, bool write, const Owned* except = nullptr) {
    for (auto& [key, o] : b.owned)
        if (&o != except && o.gpu_newer && o.dirty_begin < end && begin < o.dirty_end) WriteBack(b, o);
    {
        Timed timed(Section::GpuSync);
        Coherent(b, begin, end, false);
    }
    if (write) std::fill(b.blocks.begin() + begin, b.blocks.begin() + std::min(end, 16384u), Bridge::kCpuNewer);
    for (auto& [key, o] : b.owned) {
        if (&o == except) continue;
        if (!write || end <= o.begin || o.full_end <= begin) continue;
        if (begin < o.end) {
            if (!o.stale) { o.stale = true; o.why = 1; }
        } else {
            o.stale_beyond = true;
        }
    }
    if (write && begin < end)
        for (u32 page = begin >> 5; page <= ((end - 1) >> 5) && page < 512; ++page)
            for (Cached* t : b.texture_pages[page])
                if (t->begin < end && begin < t->end) t->valid = false;
}

/* A target or depth buffer about to be drawn into or sampled: up to date with
   memory and whatever else the GPU drew over its blocks. */
Owned& Use(Bridge& b, u32 fbp, u32 fbw, u32 psm, bool depth, u32 width, u32 height, u32 rows) {
    Owned& o = b.owned[std::make_tuple(fbp, fbw, psm, depth)];
    if (!o.width) {
        o.fbp = fbp;
        o.fbw = fbw;
        o.psm = psm;
        o.depth = depth;
        o.width = width;
        o.height = height;
        std::tie(o.begin, o.end) = Blocks(fbp, fbw, psm, rows);
        o.full_end = Blocks(fbp, fbw, psm, height).second;
        o.drawn_end = o.begin;
    } else {
        const u32 end = Blocks(fbp, fbw, psm, rows).second;
        if (end > o.end) {
            o.end = end;
            o.checked = 0;
            if (o.stale_beyond && !o.stale) { o.stale = true; o.why = 1; }
        }
    }
    if (!o.stale && o.checked == b.newer_epoch) return o;
    // Another image's drawing over these blocks reaches this one through memory.
    for (auto& [key, other] : b.owned)
        if (&other != &o && other.gpu_newer && other.dirty_begin < o.end && o.begin < other.dirty_end) {
            WriteBack(b, other);
            if (!o.stale) { o.stale = true; o.why = 3; }
        }
    o.checked = b.newer_epoch;
    if (o.stale) {
        // Loaded from the GPU's local memory, the CPU's writes there first -
        // and its own drawing not yet there: an image goes stale for rows
        // another drew over while newer than memory in rows of its own.
        if (o.gpu_newer) WriteBack(b, o);
        Timed timed(Section::GpuSync);
        ++b.reloads;
        Coherent(b, o.begin, o.full_end, true);
        if (depth) b.renderer->DepthFromMemory(o.fbp, o.width, o.height, o.fbw, o.psm);
        else b.renderer->TargetFromMemory(o.fbp, o.fbw, o.psm, o.width, o.height);
        ++o.reloads[o.why];
        o.stale = o.stale_beyond = false;
    }
    return o;
}

/* After the GPU drew into `o`, rows [y0, y1): memory and the other images
   over those blocks are behind. */
void Drew(Bridge& b, Owned& o, u32 x1, u32 y1, u32 y0) {
    const u32 page_rows = Is16(o.psm) ? 64 : 32, row_blocks = std::max(1u, o.fbw) * 32;
    y1 = std::min(std::max(y1, y0 + 1), o.height);
    y0 = std::min(y0, y1 - 1);
    const u32 db = o.begin + (y0 / page_rows) * row_blocks;
    const u32 de = o.begin + ((y1 + page_rows - 1) / page_rows) * row_blocks;
    // Already newer than memory over these blocks: the others over them were
    // made stale then (any reloaded since wrote this one back first).
    const bool inside = o.gpu_newer && db >= o.dirty_begin && de <= o.dirty_end;
    o.gpu_newer = true;
    o.x1 = std::max(o.x1, std::min(x1, o.width));
    o.y1 = std::max(o.y1, y1);
    o.drawn_end = std::max(o.drawn_end, Blocks(o.fbp, o.fbw, o.psm, o.y1).second);
    o.end = std::max(o.end, o.drawn_end);
    o.dirty_begin = std::min(o.dirty_begin, db);
    o.dirty_end = std::max(o.dirty_end, de);
    o.drawn_serial = ++b.serial;    // textures over it decoded before this are out of date
    if (inside) return;
    ++b.newer_epoch;
    for (auto& [key, other] : b.owned) {
        if (&other == &o || de <= other.begin || other.full_end <= db) continue;
        if (db < other.end) {
            if (!other.stale) { other.stale = true; other.why = 2; }
        } else {
            other.stale_beyond = true;
        }
    }
}

/* A 64-bit hash of `size` bytes (a multiple of 8). Four independent lanes,
   folded at the end: one chain of multiplies waits on each before it, and
   textures are hashed hundreds of thousands of times a run (only compared
   within a run, so the values themselves are free to change). */
u64 Hash(const unsigned char* data, std::size_t size) {
    constexpr u64 kMul = 0xFF51AFD7ED558CCDull;
    u64 h[4] = {0x9E3779B97F4A7C15ull ^ size, 0xC2B2AE3D27D4EB4Full, 0x165667B19E3779F9ull, 0x27D4EB2F165667C5ull};
    std::size_t i = 0;
    for (; i + 32 <= size; i += 32)
        for (int lane = 0; lane < 4; ++lane) {
            u64 word;
            std::memcpy(&word, data + i + lane * 8, 8);
            h[lane] = (h[lane] ^ word) * kMul;
            h[lane] ^= h[lane] >> 29;
        }
    for (int lane = 0; i + 8 <= size; i += 8, ++lane) {
        u64 word;
        std::memcpy(&word, data + i, 8);
        h[lane] = (h[lane] ^ word) * kMul;
        h[lane] ^= h[lane] >> 29;
    }
    u64 out = h[0];
    for (int lane = 1; lane < 4; ++lane) {
        out = (out ^ h[lane]) * kMul;
        out ^= out >> 29;
    }
    return out;
}

bool GpuHolds(const Bridge& b, u32 begin, u32 end);

bool Palettized(u32 psm) {
    return psm == gs::PSMT8 || psm == gs::PSMT4 || psm == gs::PSMT8H || psm == gs::PSMT4HL || psm == gs::PSMT4HH;
}

/* The CLUT rows a palettized texture reads: all 16 for 8-bit, CSA's for 4-bit. */
u32 ClutRowsOf(u32 psm, u32 csa) {
    if (!Palettized(psm)) return 0;
    return (psm == gs::PSMT8 || psm == gs::PSMT8H) ? 0xFFFFu : 1u << (csa % 16);
}

/* The CLUT rows a palettized texture reads, as a hash: their entries where
   the GS model holds them, their generation where only the GPU does. */
u64 ClutHash(const Bridge& b, u32 psm, u32 csa) {
    const u32 rows = ClutRowsOf(psm, csa);
    if (!rows) return 0;
    // Hashed once a load: hundreds of thousands of palettized draws a frame.
    static u64 loads = ~0ull, row_hash[16];
    if (gs::TheGs().clut_loads() != loads) {
        loads = gs::TheGs().clut_loads();
        const auto& clut = gs::TheGs().Clut();
        for (u32 r = 0; r < 16; ++r) {
            u64 hash = 1469598103934665603ull;
            for (u32 i = 0; i < 16; ++i) hash = (hash ^ clut[r * 16 + i]) * 1099511628211ull;
            row_hash[r] = hash;
        }
    }
    u64 hash = 0x9E3779B97F4A7C15ull;
    for (u32 r = 0; r < 16; ++r) {
        if (!(rows >> r & 1)) continue;
        const u64 row = (b.clut_cpu >> r & 1) ? row_hash[r] : ~(b.clut_generation[r] * 0xC2B2AE3D27D4EB4Full);
        hash = (hash ^ row ^ r) * 0xFF51AFD7ED558CCDull;
    }
    return hash;
}

/* A CLUT load (on_clut_load): from the GPU's copy of local memory when the
   GPU holds its blocks newer - the race's crash and glow scenes load CLUTs
   from images just drawn, and bringing those back waited ~8 ms each - else
   left to the GS model. */
bool LoadClut(Bridge& b, u64 tex0, u64 texa) {
    const u32 psm = Bits(tex0, 20, 6), cbp = Bits(tex0, 37, 14), cpsm = Bits(tex0, 51, 4), csa = Bits(tex0, 56, 5);
    const u32 rows = ClutRowsOf(psm, csa);
    if (!GpuHolds(b, cbp, cbp + 8)) {
        b.clut_cpu |= rows;
        b.clut_gpu &= ~rows;
        return false;
    }
    Timed timed(Section::GpuSync);
    for (auto& [key, o] : b.owned)
        if (o.gpu_newer && o.dirty_begin < cbp + 8 && cbp < o.dirty_end) WriteBack(b, o);
    Coherent(b, cbp, cbp + 8, true);
    ps2gpu::GsClutLoad load;
    load.cbp = cbp;
    load.cpsm = cpsm;
    load.csa = csa;
    load.eight = psm == gs::PSMT8 || psm == gs::PSMT8H;
    load.texa_lo = u32(texa & 0x80FF);
    load.ta1 = Bits(texa, 32, 8);
    b.renderer->ClutFromMemory(load);
    b.clut_gpu |= rows;
    b.clut_cpu &= ~rows;
    for (u32 r = 0; r < 16; ++r)
        if (rows >> r & 1) ++b.clut_generation[r];
    ++b.gpu_clut_loads;
    return true;
}

/* The renderer's key for the texture the current primitive samples, decoded if
   need be (--gpu). */
/* Whether the GPU holds blocks [begin, end) newer than the CPU: an image drawn
   over them and not yet swizzled into GPU memory, or GPU memory newer. */
bool GpuHolds(const Bridge& b, u32 begin, u32 end) {
    for (const auto& [key, o] : b.owned)
        if (o.gpu_newer && o.dirty_begin < end && begin < o.dirty_end) return true;
    end = std::min(end, 16384u);
    for (u32 i = begin; i < end; ++i)
        if (b.blocks[i] == Bridge::kGpuNewer) return true;
    return false;
}

/* The renderer's key for the texture the current primitive samples, decoded if
   need be: by the CPU from its copy of local memory, or - when the GPU holds
   the texture's blocks newer (the race reads its own targets' alpha as
   PSMT8H textures, its depth buffer as PSMZ16) - by the GPU from its own,
   rather than wait for them to come back. */
u64 CachedTexture(Bridge& b, const gs::Context& c, u64 texa, u32 tw, u32 th) {
    const u32 tbp = Bits(c.tex0, 0, 14), tbw = Bits(c.tex0, 14, 6), psm = Bits(c.tex0, 20, 6);
    // What decides the decoded texels: where and how it is stored (TBP0, TBW,
    // PSM, TW, TH), how a region mode wraps it (CLAMP), TEXA, the CLUT's
    // contents (wherever it was loaded from).
    u64 key = c.tex0 & 0x3FFFFFFFFull;
    if (Bits(c.clamp, 0, 2) >= 2 || Bits(c.clamp, 2, 2) >= 2) key ^= c.clamp * 0xC2B2AE3D27D4EB4Full;
    key ^= texa * 0x165667B19E3779F9ull;
    const u32 clut_rows = ClutRowsOf(psm, Bits(c.tex0, 56, 5));
    key ^= ClutHash(b, psm, Bits(c.tex0, 56, 5)) * 0x27D4EB2F165667C5ull;
    Cached& t = b.textures[key];
    if (!t.renderer_key) {
        t.renderer_key = b.next_texture++;
        b.texture_lru.push_front(key);
        t.lru = b.texture_lru.begin();
        t.begin = tbp;
        t.end = std::min(16384u, tbp + std::max(1u, tbw * 64 * th * PixelBits(psm) / 8 / 256 + 32));
        b.IndexTexture(&t, true);
    } else {
        b.texture_lru.splice(b.texture_lru.begin(), b.texture_lru, t.lru);
    }
    // The GPU drawing over its blocks since it was decoded makes it out of date
    // as surely as memory written there.
    if (t.owned_seen != b.owned.size()) {
        t.over.clear();
        for (const auto& [okey, o] : b.owned)
            if (o.begin < t.end && t.begin < o.full_end) t.over.push_back(&o);
        t.owned_seen = b.owned.size();
    }
    for (const Owned* o : t.over)
        if (o->drawn_serial > t.decoded_serial && o->begin < t.end && t.begin < o->drawn_end) t.valid = false;
    // Decoded on the GPU too when the CLUT rows it reads are only there.
    if (!t.valid && (GpuHolds(b, t.begin, t.end) || (clut_rows & ~b.clut_cpu))) {
        Timed timed(Section::GpuSync);
        for (auto& [okey, o] : b.owned)
            if (o.gpu_newer && o.dirty_begin < t.end && t.begin < o.dirty_end) WriteBack(b, o);
        Coherent(b, t.begin, t.end, true);
        ps2gpu::GsTextureDecode d;
        d.tbp = tbp;
        d.tbw = tbw;
        d.psm = psm;
        d.tw = tw;
        d.th = th;
        d.wms = Bits(c.clamp, 0, 2);
        d.wmt = Bits(c.clamp, 2, 2);
        d.minu = Bits(c.clamp, 4, 10);
        d.maxu = Bits(c.clamp, 14, 10);
        d.minv = Bits(c.clamp, 24, 10);
        d.maxv = Bits(c.clamp, 34, 10);
        d.texa_lo = u32(texa & 0x80FF);
        d.ta1 = Bits(texa, 32, 8);
        d.csa = Bits(c.tex0, 56, 5);
        // Through the GPU's CLUT, its rows the GPU lacks sent over first.
        d.clut = nullptr;
        if (clut_rows) {
            b.renderer->ClutRows(gs::TheGs().Clut().data(), b.clut_cpu & ~b.clut_gpu);
            b.clut_gpu |= b.clut_cpu;
        }
        b.renderer->TextureFromMemory(t.renderer_key, d);
        t.valid = true;
        t.decoded = false;              // the CPU's copy was not read: no hash to compare
        t.decoded_serial = b.serial;
        ++b.gpu_decodes;
    }
    if (!t.valid) {
        Timed timed(Section::GpuSync);
        b.cause = 0;
        const u64 before = b.downloads;
        Sync(b, t.begin, t.end, false);
        if (b.downloads != before) b.texture_downloads[psm << 16 | tbw] += b.downloads - before;
        // Games send the same texels again and again (Burnout 3 its textures
        // every frame, through PATH3): decoded again only if its blocks hold
        // something else than when it was last decoded.
        const u64 content = Hash(gs::TheGs().memory.data() + std::size_t(t.begin) * 256,
                                 std::size_t(t.end - t.begin) * 256);
        if (!t.decoded || content != t.content) {
            std::vector<u32> pixels;
            u32 w = 0, h = 0;
            gs::TheGs().DecodeTexture(&pixels, &w, &h);
            b.renderer->UploadTexture(t.renderer_key, pixels.data(), w, h);
            t.content = content;
            ++b.decodes;
        } else {
            ++b.unchanged;
        }
        t.valid = true;
        t.decoded = true;
        t.decoded_serial = b.serial;
    }
    const u64 renderer_key = t.renderer_key;
    // Keep the cache bounded: the least recently used go.
    while (b.textures.size() > 2048) {
        const u64 old = b.texture_lru.back();
        b.texture_lru.pop_back();
        auto found = b.textures.find(old);
        if (found != b.textures.end()) {
            b.renderer->ReleaseTexture(found->second.renderer_key);
            b.IndexTexture(&found->second, false);
            b.textures.erase(found);
        }
    }
    (void)tw;
    return renderer_key;
}

// ------------------------------------------------------------------ both

ps2gpu::GsVertex Convert(const gs::Vertex& v, bool fst, u32 tw, u32 th) {
    ps2gpu::GsVertex out{};
    out.x = v.x;
    out.y = v.y;
    out.z = v.z;                    // clamped to the depth format by the caller
    out.r = v.r;
    out.g = v.g;
    out.b = v.b;
    out.a = v.a;
    out.fog = v.fog;
    if (fst) {
        out.s = float(v.u) / 16.0f / float(tw);
        out.t = float(v.v) / 16.0f / float(th);
        out.q = 1.0f;
    } else {
        out.s = v.s;
        out.t = v.t;
        out.q = v.q;
    }
    return out;
}

/* A primitive as triangles: sprites as two (the second vertex's colour), lines
   and points as one pixel wide quads. */
std::size_t Triangles(const gs::Gs::DrawCall& call, bool fst, u32 tw, u32 th, ps2gpu::GsVertex* v, bool* gouraud) {
    const u32 type = u32(call.prim & 7);
    auto quad = [&](ps2gpu::GsVertex a, ps2gpu::GsVertex b) {
        ps2gpu::GsVertex ab = b, ba = b;
        ab.x = b.x; ab.y = a.y; ab.t = a.t;
        ba.x = a.x; ba.y = b.y; ba.s = a.s;
        a.r = b.r; a.g = b.g; a.b = b.b; a.a = b.a; a.fog = b.fog; a.z = b.z;   // flat: the second vertex's, Z too
        v[0] = a; v[1] = ab; v[2] = ba; v[3] = ab; v[4] = b; v[5] = ba;
        *gouraud = true;
        return std::size_t(6);
    };
    if (type == 6 && call.count >= 2)
        return quad(Convert(call.vertices[0], fst, tw, th), Convert(call.vertices[1], fst, tw, th));
    // Lines and points as the software GS draws them: a pixel per step along
    // the major axis, at the line's position rounded, both ends included. A
    // pixel n is sampled at n, so the band [p - 0.5, p + 0.5) across the line
    // holds exactly the rounded pixel, and the ends reach half a pixel past.
    if (type == 0 && call.count >= 1) {             // a point: its pixel
        ps2gpu::GsVertex a = Convert(call.vertices[call.count - 1], fst, tw, th), b = a;
        a.x -= 0.5f;
        a.y -= 0.5f;
        b.x += 0.5f;
        b.y += 0.5f;
        return quad(a, b);
    }
    if ((type == 1 || type == 2) && call.count >= 2) {   // a line: a band one pixel across
        ps2gpu::GsVertex a = Convert(call.vertices[0], fst, tw, th), b = Convert(call.vertices[1], fst, tw, th);
        const bool flat = std::fabs(b.x - a.x) >= std::fabs(b.y - a.y);
        float& a_major = flat ? a.x : a.y;
        float& b_major = flat ? b.x : b.y;
        const float reach = b_major >= a_major ? 0.5f : -0.5f;
        a_major -= reach;
        b_major += reach;
        ps2gpu::GsVertex a0 = a, a1 = a, b0 = b, b1 = b;
        if (flat) { a0.y -= 0.5f; b0.y -= 0.5f; a1.y += 0.5f; b1.y += 0.5f; }
        else { a0.x -= 0.5f; b0.x -= 0.5f; a1.x += 0.5f; b1.x += 0.5f; }
        v[0] = a0; v[1] = b0; v[2] = a1; v[3] = b0; v[4] = b1; v[5] = a1;
        return 6;
    }
    if (call.count >= 3) {
        for (int i = 0; i < 3; ++i) v[i] = Convert(call.vertices[i], fst, tw, th);
        return 3;
    }
    return 0;
}

void WriteBmp(const std::string& path, const std::vector<u32>& rgba, u32 stride, u32 w, u32 h);

/* PS2HOST_GPU_DUMP_FRAME: the target drawing just left, as it stands. */
void DumpPass(Bridge& s, const std::tuple<u32, u32, u32>& target) {
    const auto [fbp, fbw, psm] = target;
    std::vector<u32> pixels;
    u32 w = 0, h = 0;
    if (!s.renderer->ReadTarget(fbp, fbw, psm, &pixels, &w, &h)) return;
    char name[96];
    std::snprintf(name, sizeof name, "/pass_%03u_%X_%u_%02X.bmp", unsigned(s.pass), unsigned(fbp), unsigned(fbw),
                  unsigned(psm));
    WriteBmp(s.dump_passes + name, pixels, w, std::min(w, 640u), std::min(h, 512u));
}

void OnDraw(const gs::Gs::DrawCall& call) {
    Timed timed(Section::GpuBridge);
    Bridge& s = *g_bridge;
    const u32 type = u32(call.prim & 7);
    if (type == 7) return;
    if (!s.primary && type < 3) return;                 // the shadow compares what it has always drawn
    const gs::Context& c = *call.context;
    ps2gpu::GsDrawState state;
    state.fbp = Bits(c.frame, 0, 9);
    state.fbw = std::max(1u, Bits(c.frame, 16, 6));
    state.psm = Bits(c.frame, 24, 6);
    state.fbmsk = u32(c.frame >> 32);
    state.width = std::min(2048u, state.fbw * 64);
    state.height = 512;
    state.scissor_x0 = int(Bits(c.scissor, 0, 11));
    state.scissor_x1 = int(Bits(c.scissor, 16, 11));
    state.scissor_y0 = int(Bits(c.scissor, 32, 11));
    state.scissor_y1 = int(Bits(c.scissor, 48, 11));
    state.gouraud = (call.prim >> 3) & 1;
    state.textured = (call.prim >> 4) & 1;
    const bool fst = (call.prim >> 8) & 1;
    const u32 tw = 1u << std::min(Bits(c.tex0, 26, 4), 10u), th = 1u << std::min(Bits(c.tex0, 30, 4), 10u);
    // The primitive as triangles, and the pixels it can reach.
    ps2gpu::GsVertex v[6];
    bool gouraud = state.gouraud;
    const std::size_t n = Triangles(call, fst, tw, th, v, &gouraud);
    state.gouraud = gouraud;
    if (!n) return;
    u32 x1 = 0, y1 = 0, y0 = state.height;
    for (std::size_t i = 0; i < n; ++i) {
        // Exclusive: the GS draws the pixels whose centres lie before the far edge.
        x1 = std::max(x1, u32(std::clamp(std::ceil(v[i].x), 0.0f, float(state.width))));
        y1 = std::max(y1, u32(std::clamp(std::ceil(v[i].y), 0.0f, float(state.height))));
        y0 = std::min(y0, u32(std::clamp(std::floor(v[i].y), 0.0f, float(state.height))));
    }
    y0 = std::max(y0, u32(std::max(state.scissor_y0, 0)));
    x1 = std::min(x1, u32(state.scissor_x1 + 1));
    y1 = std::min(y1, u32(state.scissor_y1 + 1));
    if (s.primary) {
        // Wholly outside the scissor (or the buffer): nothing to draw, and no
        // image to bring up to date for it. Burnout 3's garage sends thousands
        // a frame to FRAME 0, FBW 1, whose colour and Z alias the same page.
        float lo_x = v[0].x, lo_y = v[0].y, hi_x = v[0].x, hi_y = v[0].y;
        for (std::size_t i = 1; i < n; ++i) {
            lo_x = std::min(lo_x, v[i].x);
            lo_y = std::min(lo_y, v[i].y);
            hi_x = std::max(hi_x, v[i].x);
            hi_y = std::max(hi_y, v[i].y);
        }
        const float left = float(std::max(state.scissor_x0, 0)), top = float(std::max(state.scissor_y0, 0));
        const float right = float(std::min(state.scissor_x1, int(state.width) - 1));
        const float bottom = float(std::min(state.scissor_y1, int(state.height) - 1));
        if (std::floor(hi_x) < left || std::ceil(lo_x) > right || std::floor(hi_y) < top || std::ceil(lo_y) > bottom)
            return;
        if (Bits(c.test, 16, 1) && Bits(c.test, 17, 2) == 0) return;    // ZTST NEVER: every pixel fails
    }
    const u32 rows = std::max(1u, y1);
    if (s.primary && (long long)s.frame == s.dump_frame) {
        const auto key = std::make_tuple(state.fbp, state.fbw, state.psm);
        if (key != s.last_target) {
            if (std::get<0>(s.last_target) != ~0u) DumpPass(s, s.last_target);
            ++s.pass;
            std::fprintf(stderr, "[gpu-pass] %03u: frame %X/%u/%02X fbmsk %08X; prim %llX tex0 %llX clamp %llX; "
                                 "alpha %llX test %llX zbuf %llX\n",
                         unsigned(s.pass), unsigned(state.fbp), unsigned(state.fbw), unsigned(state.psm),
                         unsigned(state.fbmsk), static_cast<unsigned long long>(call.prim),
                         static_cast<unsigned long long>(c.tex0), static_cast<unsigned long long>(c.clamp),
                         static_cast<unsigned long long>(c.alpha), static_cast<unsigned long long>(c.test),
                         static_cast<unsigned long long>(c.zbuf));
            s.last_target = key;
        }
    }
    Owned* target = nullptr;
    if (s.primary) target = &Use(s, state.fbp, state.fbw, state.psm, false, state.width, state.height, rows);
    if (state.textured) {
        const u32 tex_psm = Bits(c.tex0, 20, 6), tbp_blocks = Bits(c.tex0, 0, 14);
        const u32 tex_format = tex_psm == gs::PSMCT32 ? 0 : tex_psm == gs::PSMCT24 ? 1
                             : (tex_psm == gs::PSMCT16 || tex_psm == gs::PSMCT16S) ? 2 : 3;
        state.tw = tw;
        state.th = th;
        // A texture that is a target the GPU has drawn is sampled from it
        // (render to texture).
        if ((s.primary || s.targets_as_textures) && tex_format < 3 && tbp_blocks % 32 == 0) {
            const u32 candidates[2] = {tex_format == 2 ? tex_psm : gs::PSMCT32, tex_format == 2 ? tex_psm : gs::PSMCT24};
            const u32 tex_fbp = tbp_blocks / 32, tex_fbw = Bits(c.tex0, 14, 6);
            for (u32 psm : candidates) {
                const bool known = s.primary ? s.owned.count(std::make_tuple(tex_fbp, tex_fbw, psm, false)) != 0
                                             : s.renderer->HasTarget(tex_fbp, tex_fbw, psm);
                if (!known) continue;
                if (s.primary) {
                    Owned& source = s.owned[std::make_tuple(tex_fbp, tex_fbw, psm, false)];
                    Use(s, tex_fbp, tex_fbw, psm, false, source.width, source.height, 1);   // sampling grows nothing
                }
                state.texture_target = true;
                state.tex_fbp = tex_fbp;
                state.tex_fbw = tex_fbw;
                state.tex_target_psm = psm;
                break;
            }
            state.tex_format = tex_format;
            state.ta0 = Bits(call.texa, 0, 8);
            state.ta1 = Bits(call.texa, 32, 8);
            state.aem = Bits(call.texa, 15, 1) != 0;
        }
        if (!state.texture_target) {
            if (s.primary) {
                state.texture = CachedTexture(s, c, call.texa, tw, th);
            } else {
                // Decoded again when the texture's registers change, local memory
                // is uploaded into, or a draw since went into the texture's blocks.
                const auto key = std::make_tuple(c.tex0, c.clamp, call.texa, gs::TheGs().uploads());
                const u32 tbp = Bits(c.tex0, 0, 14);
                const u32 tex_blocks = std::max(1u, Bits(c.tex0, 14, 6) * 64 * th * PixelBits(tex_psm) / 8 / 256);
                if (key != s.texture_state || s.drawn_into(tbp, tex_blocks)) {
                    std::vector<u32> pixels;
                    u32 w = 0, h = 0;
                    gs::TheGs().DecodeTexture(&pixels, &w, &h);
                    // Named by the registers that place it: decoding it again
                    // replaces it rather than adding another.
                    s.texture_key = c.tex0 & 0x1FFFFFFFFFFFFFFFull;
                    s.renderer->UploadTexture(s.texture_key, pixels.data(), w, h);
                    s.texture_state = key;
                    s.since_decode.clear();
                }
                state.texture = s.texture_key;
            }
        }
        state.tfx = ps2gpu::TextureFunction(Bits(c.tex0, 35, 2));
        state.tcc = Bits(c.tex0, 34, 1) != 0;
        const int k = int(std::int32_t(Bits(c.tex1, 32, 12) << 20) >> 20);
        const u32 mmin = Bits(c.tex1, 6, 3);
        state.bilinear = k <= 0 ? Bits(c.tex1, 5, 1) != 0 : (mmin == 1 || mmin == 4 || mmin == 5);
        state.wms = Bits(c.clamp, 0, 2);
        state.wmt = Bits(c.clamp, 2, 2);
        state.minu = Bits(c.clamp, 4, 10);
        state.maxu = Bits(c.clamp, 14, 10);
        state.minv = Bits(c.clamp, 24, 10);
        state.maxv = Bits(c.clamp, 34, 10);
    }
    state.alpha_test = Bits(c.test, 0, 1) != 0;
    state.atst = Bits(c.test, 1, 3);
    state.aref = Bits(c.test, 4, 8);
    state.afail = Bits(c.test, 12, 2);
    state.date = Bits(c.test, 14, 1) != 0 && state.psm != gs::PSMCT24;
    state.datm = Bits(c.test, 15, 1);
    state.blend = (call.prim >> 6) & 1;
    state.a = ps2gpu::BlendColour(std::min(2u, Bits(c.alpha, 0, 2)));
    state.b = ps2gpu::BlendColour(std::min(2u, Bits(c.alpha, 2, 2)));
    state.c = ps2gpu::BlendAlpha(std::min(2u, Bits(c.alpha, 4, 2)));
    state.d = ps2gpu::BlendColour(std::min(2u, Bits(c.alpha, 6, 2)));
    state.fix = Bits(c.alpha, 32, 8);
    state.pabe = call.pabe;
    state.fog = (call.prim >> 5) & 1;
    state.fogcol = u32(call.fogcol & 0xFFFFFF);
    // Depth: ZTE with ZBUF.
    const u32 zpsm = 0x30 | Bits(c.zbuf, 24, 4);
    const u32 zmax = zpsm == gs::PSMZ32 ? 0xFFFFFFFFu : zpsm == gs::PSMZ24 ? 0xFFFFFFu : 0xFFFFu;
    state.depth = Bits(c.test, 16, 1) != 0;
    state.ztst = Bits(c.test, 17, 2);
    state.zbp = Bits(c.zbuf, 0, 9);
    state.depth_write = Bits(c.zbuf, 32, 1) == 0;
    // ZTST ALWAYS with ZMSK: the depth buffer plays no part. Burnout 3's race
    // blurs into scratch targets at its depth buffer's address with ZBUF left
    // there; treating that as a depth buffer would bring it up to date - a
    // write-back and a reload - for every one of their draws.
    if (state.depth && state.ztst == 1 && !state.depth_write) state.depth = false;
    // ZTST ALWAYS into a depth buffer over the colour target's own memory:
    // the GS writes each pixel's colour and its Z into the same memory, the Z
    // in its own layout - other words of it. As a depth buffer that is no
    // use; as memory it is: the race's glow passes lay their threshold grey
    // down this way, as colour and again as Z (the Z = the grey). So the Z
    // is drawn as colour into a view of that memory in the Z format, which
    // GPU memory reconciles with the colour like any two views.
    bool z_as_colour = false;
    if (s.primary && state.depth && state.ztst == 1) {
        const auto colour = Blocks(state.fbp, state.fbw, state.psm, rows);
        const auto z = Blocks(state.zbp, state.fbw, zpsm, rows);
        if (colour.first < z.second && z.first < colour.second) {
            state.depth = false;
            // An alpha test that fails every pixel writes Z only for AFAIL
            // ZB_ONLY (the race's glow passes: ATST NEVER, FB_ONLY - no Z).
            const bool none_pass = state.alpha_test && state.atst == 0;
            z_as_colour = state.depth_write && (!none_pass || state.afail == 2);
        }
    }
    Owned* depth = nullptr;
    if (state.depth) {
        if (s.primary) {
            depth = &Use(s, state.zbp, state.fbw, zpsm, true, state.width, state.height, rows);
        } else if (s.depth_seeded.insert(state.zbp).second) {
            // Each depth buffer starts the frame as local memory has it, so the
            // GPU tests against what the software GS drew.
            std::vector<u32> z(std::size_t(state.width) * state.height);
            const gs::Gs& g = gs::TheGs();
            for (u32 y = 0; y < state.height; ++y)
                for (u32 x = 0; x < state.width; ++x)
                    z[std::size_t(y) * state.width + x] = g.memory.Read(state.zbp * 32, state.fbw, x, y, zpsm);
            s.renderer->UploadDepth(state.zbp, state.width, state.height, z.data());
        }
    }

    for (std::size_t i = 0; i < n; ++i) v[i].z = std::min(v[i].z, zmax);
    // Not timed apart: tens of millions of primitives a race, and a timer's
    // two clock reads each cost as much as recording one.
    s.renderer->DrawTriangles(state, v, n);
    if (s.primary) {
        // FBMSK over every bit the format stores: the colour is not written.
        const u32 stored = state.psm == gs::PSMCT24 ? 0xFFFFFFu : Is16(state.psm) ? 0x80F8F8F8u : 0xFFFFFFFFu;
        if ((state.fbmsk & stored) != stored) Drew(s, *target, x1, y1, y0);
        if (depth && state.depth_write) Drew(s, *depth, x1, y1, y0);
        if (z_as_colour) {
            // The Z, as the bytes the Z format stores, into the Z-format view.
            ps2gpu::GsDrawState zs;
            zs.fbp = state.zbp;
            zs.fbw = state.fbw;
            zs.psm = zpsm;
            zs.width = state.width;
            zs.height = state.height;
            zs.scissor_x0 = state.scissor_x0;
            zs.scissor_y0 = state.scissor_y0;
            zs.scissor_x1 = state.scissor_x1;
            zs.scissor_y1 = state.scissor_y1;
            zs.gouraud = state.gouraud;
            if (zpsm == gs::PSMZ24) zs.fbmsk = 0xFF000000u;
            ps2gpu::GsVertex zv[6];
            for (std::size_t i = 0; i < n; ++i) {
                zv[i] = v[i];
                const u32 z = v[i].z;
                const u32 c = Is16(zpsm) ? From16(z & 0xFFFF) : z;
                zv[i].r = u8(c);
                zv[i].g = u8(c >> 8);
                zv[i].b = u8(c >> 16);
                zv[i].a = u8(c >> 24);
            }
            Owned& view = Use(s, zs.fbp, zs.fbw, zs.psm, false, zs.width, zs.height, rows);
            s.renderer->DrawTriangles(zs, zv, n);
            Drew(s, view, x1, y1, y0);
        }
        return;
    }
    Extent& e = s.drawn[std::make_tuple(state.fbp, state.fbw, state.psm)];
    e.x1 = std::max(e.x1, x1);
    e.y1 = std::max(e.y1, y1);
    u32& blocks = s.since_decode[state.fbp * 32];
    blocks = std::max(blocks, std::max(1u, state.fbw * 64 * e.y1 * PixelBits(state.psm) / 8 / 256));
}

void WriteBmp(const std::string& path, const std::vector<u32>& rgba, u32 stride, u32 w, u32 h) {
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return;
    const u32 data = w * h * 4, file = 54 + data;
    unsigned char header[54] = {'B', 'M'};
    auto put32 = [&](int at, u32 v) { for (int i = 0; i < 4; ++i) header[at + i] = (v >> (8 * i)) & 0xFF; };
    put32(2, file);
    put32(10, 54);
    put32(14, 40);
    put32(18, w);
    put32(22, u32(-std::int32_t(h)));                   // top-down
    header[26] = 1;
    header[28] = 32;
    put32(34, data);
    std::fwrite(header, 1, 54, f);
    for (u32 y = 0; y < h; ++y)
        for (u32 x = 0; x < w; ++x) {
            const u32 c = rgba[std::size_t(y) * stride + x];
            const unsigned char px[4] = {(unsigned char)(c >> 16), (unsigned char)(c >> 8), (unsigned char)c, 0xFF};
            std::fwrite(px, 1, 4, f);
        }
    std::fclose(f);
}

}  // namespace

bool StartGpuRenderer(std::string* error, GpuMode mode, bool show) {
    static Bridge bridge;
    // On the window's device when there is a window: one Vulkan device for both.
    ps2gpu::Gpu* device = WindowGpu();
    if (!device) {
        bridge.gpu = ps2gpu::CreateGpu(error);
        if (!bridge.gpu) return false;
        device = bridge.gpu.get();
    }
    bridge.renderer = ps2gpu::CreateGsRenderer(*device, error);
    if (!bridge.renderer) return false;
    g_bridge = &bridge;
    bridge.primary = mode == GpuMode::Primary;
    if (const char* dump = std::getenv("PS2HOST_GPU_DUMP_FRAME")) bridge.dump_frame = std::atoll(dump);
    if (const char* dir = std::getenv("PS2HOST_GPU_DUMP_DIR")) bridge.dump_passes = dir;
    bridge.targets_as_textures = mode == GpuMode::ShadowRtt;
    gs::Gs& g = gs::TheGs();
    g.on_draw = OnDraw;
    if (bridge.primary) {
        g.rasterize = false;
        g.on_memory = [](u32 begin, u32 end, bool write) {
            g_bridge->cause = write ? 2 : 1;
            Sync(*g_bridge, begin, end, write);
        };
        g.on_clut_load = [](u64 tex0, u64 texa) { return LoadClut(*g_bridge, tex0, texa); };
    }
    if (show || bridge.primary) {
        // The window shows the GPU's picture: the displayed buffer read back
        // from its target (the software picture where the GPU has none).
        SetPictureSource([](std::vector<u32>* pixels, u32* width, u32* height) {
            u32 fbp = 0, fbw = 0, psm = 0;
            if (!g_bridge || !gs::TheGs().Displayed(&fbp, &fbw, &psm)) return false;
            if (g_bridge->primary) {
                auto found = g_bridge->owned.find(std::make_tuple(fbp, fbw, psm, false));
                if (found == g_bridge->owned.end() || found->second.stale) return false;
            }
            std::vector<u32> rgba;
            u32 w = 0, h = 0;
            if (!g_bridge->renderer->ReadTarget(fbp, fbw, psm, &rgba, &w, &h)) return false;
            const u32 shown_w = std::min(w, 640u), shown_h = std::min(h, 448u);
            pixels->resize(std::size_t(shown_w) * shown_h);
            for (u32 y = 0; y < shown_h; ++y)
                for (u32 x = 0; x < shown_w; ++x) {
                    const u32 c = rgba[std::size_t(y) * w + x];
                    (*pixels)[std::size_t(y) * shown_w + x] = (c & 0xFF00FF00u) | ((c & 0xFF) << 16) | ((c >> 16) & 0xFF);
                }
            *width = shown_w;
            *height = shown_h;
            return true;
        });
    }
    std::fprintf(stderr, "gpu: drawing on %s %s\n", device->Describe().c_str(),
                 bridge.primary ? "instead of the software GS" : "beside the software GS");
    return true;
}

void GpuShadowFrame(u64 frame, const std::string& dump_dir) {
    if (!g_bridge) return;
    Bridge& s = *g_bridge;
    if (s.primary) {
        if ((long long)s.frame == s.dump_frame && std::get<0>(s.last_target) != ~0u) DumpPass(s, s.last_target);
        s.frame = frame + 1;
        // The frame's drawing to the GPU now, to run while the next is made,
        // rather than when something next waits on the GPU.
        {
            Timed timed(Section::GpuRender);
            s.renderer->Kick();
        }
        if (frame % 100 == 0)
            std::fprintf(stderr, "[gpu] frame %llu: %llu draws, %llu write-backs, %llu reloads, %llu decodes, %zu images, %zu textures\n",
                         static_cast<unsigned long long>(frame), static_cast<unsigned long long>(s.serial),
                         static_cast<unsigned long long>(s.write_backs), static_cast<unsigned long long>(s.reloads),
                         static_cast<unsigned long long>(s.decodes), s.owned.size(), s.textures.size());
        if (std::getenv("PS2HOST_GPU_LOG")) {
            std::fprintf(stderr, "[gpu]   KB to the CPU: %llu for textures, %llu for reads, %llu for writes;",
                         static_cast<unsigned long long>(s.downloads_by[0] / 4),
                         static_cast<unsigned long long>(s.downloads_by[1] / 4),
                         static_cast<unsigned long long>(s.downloads_by[2] / 4));
            for (const auto& [kind, blocks] : s.texture_downloads)
                std::fprintf(stderr, " psm %02X tbw %u: %llu KB", unsigned(kind >> 16), unsigned(kind & 0xFFFF),
                             static_cast<unsigned long long>(blocks / 4));
            std::fprintf(stderr, "\n");
        }
        if (std::getenv("PS2HOST_GPU_LOG"))
            for (const auto& [key, o] : s.owned)
                std::fprintf(stderr, "[gpu]   %s %X/%u/%02X blocks %X-%X drawn to %X, %ux%u; reloads new %llu memory %llu draw %llu write-back %llu\n",
                             o.depth ? "Z" : "C", unsigned(o.fbp), unsigned(o.fbw), unsigned(o.psm), unsigned(o.begin),
                             unsigned(o.end), unsigned(o.drawn_end), unsigned(o.x1), unsigned(o.y1),
                             static_cast<unsigned long long>(o.reloads[0]), static_cast<unsigned long long>(o.reloads[1]),
                             static_cast<unsigned long long>(o.reloads[2]), static_cast<unsigned long long>(o.reloads[3]));
        return;
    }
    if (!dump_dir.empty()) s.dump_dir = dump_dir;
    s.renderer->Flush();
    const gs::Gs& g = gs::TheGs();
    u32 shown_fbp = 0, shown_fbw = 0, shown_psm = 0;
    g.Displayed(&shown_fbp, &shown_fbw, &shown_psm);
    std::string line;
    u64 total = 0, close = 0;
    for (const auto& [key, extent] : s.drawn) {
        const auto [fbp, fbw, psm] = key;
        std::vector<u32> pixels;
        u32 w = 0, h = 0;
        if (!s.renderer->ReadTarget(fbp, fbw, psm, &pixels, &w, &h)) continue;
        const u32 x1 = std::min(extent.x1, w), y1 = std::min(extent.y1, h);
        const bool sixteen = psm == gs::PSMCT16 || psm == gs::PSMCT16S;
        const int slack = sixteen ? 8 : 2;
        u64 n = 0, ok = 0;
        for (u32 y = 0; y < y1; ++y)
            for (u32 x = 0; x < x1; ++x) {
                const u32 want = g.FramePixel(fbp, fbw, psm, x, y), got = pixels[std::size_t(y) * w + x];
                bool same = true;
                for (int shift = 0; shift < (psm == gs::PSMCT32 ? 32 : 24); shift += 8) {   // alpha too where it is stored
                    const int a = int((want >> shift) & 0xFF), b = int((got >> shift) & 0xFF);
                    if (std::abs(a - b) > slack) same = false;
                }
                ++n;
                ok += same;
            }
        total += n;
        close += ok;
        char text[96];
        std::snprintf(text, sizeof text, " %X/%u/%02X %ux%u %.1f%%", unsigned(fbp), unsigned(fbw), unsigned(psm),
                      unsigned(x1), unsigned(y1), n ? 100.0 * double(ok) / double(n) : 100.0);
        line += text;
        if (!s.dump_dir.empty() && x1 && y1) {
            char name[96];
            std::snprintf(name, sizeof name, "/gpu_%06llu_%X.bmp", static_cast<unsigned long long>(frame),
                          unsigned(fbp));
            WriteBmp(s.dump_dir + name, pixels, w, x1, y1);
            // The software GS's, beside it.
            std::vector<u32> reference(std::size_t(x1) * y1);
            for (u32 y = 0; y < y1; ++y)
                for (u32 x = 0; x < x1; ++x) reference[std::size_t(y) * x1 + x] = g.FramePixel(fbp, fbw, psm, x, y);
            std::snprintf(name, sizeof name, "/soft_%06llu_%X.bmp", static_cast<unsigned long long>(frame),
                          unsigned(fbp));
            WriteBmp(s.dump_dir + name, reference, x1, x1, y1);
        }
    }
    if (total) {
        ++s.frames;
        s.agreement_sum += double(close) / double(total);
        std::fprintf(stderr, "[gpu-shadow] frame %llu:%s (shown %X)\n", static_cast<unsigned long long>(frame),
                     line.c_str(), unsigned(shown_fbp));
    }
    if (!s.drawn.empty()) s.dump_dir.clear();
    s.drawn.clear();
    s.depth_seeded.clear();
}

std::string GpuShadowSummary() {
    if (!g_bridge) return "";
    char text[320];
    if (g_bridge->primary) {
        std::snprintf(text, sizeof text,
                      "gpu: %llu images into GPU local memory, %llu out of it; %llu KB to the CPU, %llu KB"
                      " from it; %llu textures decoded (%llu more found unchanged), %llu on the GPU;"
                      " %llu CLUTs loaded on the GPU",
                      static_cast<unsigned long long>(g_bridge->write_backs),
                      static_cast<unsigned long long>(g_bridge->reloads),
                      static_cast<unsigned long long>(g_bridge->downloads / 4),
                      static_cast<unsigned long long>(g_bridge->uploads / 4),
                      static_cast<unsigned long long>(g_bridge->decodes),
                      static_cast<unsigned long long>(g_bridge->unchanged),
                      static_cast<unsigned long long>(g_bridge->gpu_decodes),
                      static_cast<unsigned long long>(g_bridge->gpu_clut_loads));
        return std::string(text) + "\ngpu: " + ps2gpu::SubmitStats();
    }
    if (!g_bridge->frames) return "";
    std::snprintf(text, sizeof text, "gpu-shadow: %llu frames compared, %.1f%% of pixels agree on average",
                  static_cast<unsigned long long>(g_bridge->frames), 100.0 * g_bridge->agreement_sum / double(g_bridge->frames));
    return text;
}

#else

bool StartGpuRenderer(std::string* error, GpuMode, bool) {
    if (error) *error = "built without the GPU renderer (PS2HOST_VULKAN)";
    return false;
}
void GpuShadowFrame(gs::u64, const std::string&) {}
std::string GpuShadowSummary() { return ""; }

#endif

}  // namespace ps2host
