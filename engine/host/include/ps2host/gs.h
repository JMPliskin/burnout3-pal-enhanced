/*
 * The Graphics Synthesizer, stage one: a software GS.
 *
 * GIF packets from the three paths (PATH1 from VU1, not yet; PATH2 from VIF1's
 * DIRECT; PATH3 from the GIF's own DMA) set GS registers, upload images into
 * the GS's 4 MB of local memory, and draw primitives into it; the display
 * registers say which part of local memory is the picture. Frames are written
 * out as BMP files on request (--frames), which is how the port's progress is
 * seen until there is a window.
 *
 * Local memory is laid out as the GS lays it out - pages, blocks and columns
 * in the order its manual gives for each pixel format (gs_memory.cpp) - since
 * games address it directly: a texture uploaded as one format and read as
 * another must come out as on the console.
 */

#ifndef PS2HOST_GS_H
#define PS2HOST_GS_H

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace ps2host {
namespace gs {

using u8 = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;
using s32 = std::int32_t;

// Pixel storage formats (PSM).
enum Psm : u32 {
    PSMCT32 = 0x00, PSMCT24 = 0x01, PSMCT16 = 0x02, PSMCT16S = 0x0A,
    PSMT8 = 0x13, PSMT4 = 0x14, PSMT8H = 0x1B, PSMT4HL = 0x24, PSMT4HH = 0x2C,
    PSMZ32 = 0x30, PSMZ24 = 0x31, PSMZ16 = 0x32, PSMZ16S = 0x3A,
};

/* Local memory: 4 MB, addressed by (base pointer in 256-byte blocks, buffer
   width in 64-pixel units, x, y, format). */
class Memory {
public:
    Memory() : bytes_(4u << 20) {}
    u32 Read(u32 bp, u32 bw, u32 x, u32 y, u32 psm) const;
    void Write(u32 bp, u32 bw, u32 x, u32 y, u32 psm, u32 value);
    u8* data() { return bytes_.data(); }

    /* The byte address of a pixel (for 4-bit formats, of its byte; *nibble
       says which half). Exposed for tests. */
    static u32 Address(u32 bp, u32 bw, u32 x, u32 y, u32 psm, u32* nibble);

private:
    std::vector<u8> bytes_;
};

struct Vertex {
    float x = 0, y = 0;         // pixels, after XYOFFSET
    u32 z = 0;
    u8 r = 0, g = 0, b = 0, a = 0;
    float s = 0, t = 0, q = 1;  // ST/Q
    u32 u = 0, v = 0;           // UV, 1/16 texel
    u8 fog = 0;
};

struct Context {                // one of the two drawing contexts
    u64 tex0 = 0, clamp = 0, tex1 = 0, xyoffset = 0, scissor = 0, alpha = 0;
    u64 test = 0, fba = 0, frame = 0, zbuf = 0;
};

class Gs {
public:
    Memory memory;
    /* --gs-trace: log image transfers and primitives (stderr). */
    bool trace = false;

    /* A GS register write by address (A+D, REGLIST, or the GIF's own). */
    void WriteRegister(u32 address, u64 value);

    /* A quadword of GIF data on a path (1, 2 or 3). */
    void Qword(int path, u64 low, u64 high);
    /* 32-bit words, gathered into quadwords. */
    void Word(int path, u32 word);

    /* Privileged registers (the 0x12000000 window): what is displayed. */
    void SetPrivileged(u32 address, u64 value);

    /* The displayed picture, 32-bit BGRA, and its size. False if nothing is. */
    bool Picture(std::vector<u32>* pixels, u32* width, u32* height) const;
    bool WriteBmp(const std::string& path) const;

    /* Every primitive as it is drawn, for a second renderer (the GPU's, which
       --gpu-shadow checks against this one): PRIM, the context it draws
       with, TEXA, and its vertices (3 for a triangle, 2 for a sprite). */
    struct DrawCall {
        u64 prim;
        const Context* context;
        u64 texa;
        const Vertex* vertices;
        u32 count;
    };
    std::function<void(const DrawCall&)> on_draw;
    /* The texture the primitive being drawn samples, decoded as the software
       rasteriser reads it (CLUT, TEXA): RGBA in memory, rows top-down. */
    void DecodeTexture(std::vector<u32>* pixels, u32* width, u32* height) const;
    /* Which frame buffer is displayed: FBP (as FRAME has it), FBW, PSM. */
    bool Displayed(u32* fbp, u32* fbw, u32* psm) const;
    /* A pixel of a frame buffer as RGBA, whatever its format (alpha 0x80 for
       24-bit formats). */
    u32 FramePixel(u32 fbp, u32 fbw, u32 psm, u32 x, u32 y) const;

    /* Changes whenever an image transfer or a local copy writes local memory. */
    u64 uploads() const { return uploads_; }

    u64 primitives() const { return primitives_; }
    u64 pixels_drawn() const { return pixels_; }
    /* For the report: GIF tags per path, register writes, image data. */
    std::string Summary() const;

private:
    struct Path {
        u64 tag_low = 0, tag_high = 0;
        u32 loops = 0, reg = 0, nregs = 0, format = 0;
        bool in_packet = false;
        u32 pending[4] = {};
        u32 pending_count = 0;
    };
    Path paths_[4];
    /* The drawing state a primitive's pixels all use, decoded from the
       registers once per primitive (Prepare) rather than once per pixel. */
    struct Setup {
        int sx0, sx1, sy0, sy1;                     // SCISSOR, inclusive
        u32 tfx;
        bool tcc, linear;
        u32 tbp, tbw, tpsm, tw, th, csa;            // TEX0
        u32 wms, wmt, minu, maxu, minv, maxv;       // CLAMP
        bool ate, date, zte, zmsk, abe, colclamp, fba, pabe;
        u32 atst, aref, afail, datm;                // TEST
        u32 ztst, zbp, zpsm;                        // TEST, ZBUF
        u32 fbp, fbw, fpsm, fbmsk;                  // FRAME
        u32 a, b, c, d, fix;                        // ALPHA
    };
    Setup setup_{};
    void Prepare();
    void Packed(u32 descriptor, u64 low, u64 high);
    void Kick(bool draw);
    void Draw();
    void DrawSprite(const Vertex& a, const Vertex& b);
    void DrawTriangle(const Vertex& a, const Vertex& b, const Vertex& c);
    void DrawLine(const Vertex& a, const Vertex& b);
    void Plot(int x, int y, u32 z, u8 r, u8 g, u8 b, u8 a, float s, float t, float q,
              float u, float v, bool textured);
    u32 Texel(float u, float v) const;          /* filtered as TEX1 says */
    u32 TexelAt(int u, int v) const;            /* one texel, wrapped or clamped */
    void Transfer(u64 low, u64 high);           // HWREG data: host to local
    void LocalToLocal();

    Context ctx_[2];
    u64 prim_ = 0, prmode_ = 0, prmodecont_ = 1, rgbaq_ = 0, st_ = 0, uv_ = 0;
    u64 texclut_ = 0, texa_ = 0, fogcol_ = 0, dthe_ = 0, colclamp_ = 1, pabe_ = 0;
    u64 bitbltbuf_ = 0, trxpos_ = 0, trxreg_ = 0, trxdir_ = 3;
    float q_ = 1;
    u32 rgba_source_ = 0;                       // where the last RGBAQ came from (trace)
    Vertex queue_[3];
    u32 queued_ = 0;
    u32 strip_ = 0;                             // vertices since the primitive started
    // Image transfer progress.
    u32 trx_x_ = 0, trx_y_ = 0;
    bool trx_active_ = false;
    u32 trx_partial_ = 0, trx_partial_bytes_ = 0;    // 24-bit pixels across quadwords
    u32 cbp0_ = 0, cbp1_ = 0;                       // CLUT bases CLD compares with
    // Privileged.
    u64 pmode_ = 0, dispfb_[2] = {}, display_[2] = {}, bgcolor_ = 0;
    u64 primitives_ = 0, pixels_ = 0, uploads_ = 0;
    u64 tags_[4] = {}, image_qwords_ = 0;
    std::array<u64, 256> register_writes_{};
    // CLUT, loaded per TEX0's CLD.
    std::array<u32, 256> clut_{};
    void LoadClut(u64 tex0);
    u64 Prim() const { return (prmodecont_ & 1) ? prim_ : ((prim_ & 7) | (prmode_ & ~7ull)); }
    const Context& Ctx() const { return ctx_[(Prim() >> 9) & 1]; }
};

Gs& TheGs();

}  // namespace gs
}  // namespace ps2host

#endif  /* PS2HOST_GS_H */
