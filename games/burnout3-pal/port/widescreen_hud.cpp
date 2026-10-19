// PAL 2D adaptation of Daniel Gomes Vieira's USA widescreen.cpp (MIT).
// The separately validated camera hook is unchanged. No host viewport/shader change.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>
#include "ps2host/host.h"
#include "ps2host/run.h"
#include "ps2host/settings.h"
// The 2D batch: its builders (each appends GS packets to a scratchpad
// buffer), its flush (see Flush2D); the frontend's sprites (see Sprites2D).
void F_002178D0(lifted::Context& c);
void F_00217A10(lifted::Context& c);
void F_00217C30(lifted::Context& c);
void F_00217D30(lifted::Context& c);
void F_00217E60(lifted::Context& c);
void F_00217FB0(lifted::Context& c);
void F_002183F0(lifted::Context& c);
void F_002187A0(lifted::Context& c);
void F_00218CF0(lifted::Context& c);
void F_00219140(lifted::Context& c);
void F_00219520(lifted::Context& c);
void F_00219820(lifted::Context& c);
void F_00219A10(lifted::Context& c);
void F_00219C90(lifted::Context& c);
void F_00219D40(lifted::Context& c);
void F_00219F10(lifted::Context& c);
void F_0021A4E0(lifted::Context& c);
void F_00364B00(lifted::Context& c);

namespace {
// ------------------------------------------------------------ elements and anchors
//
// An element is what one piece of the game's code draws in one part of the
// screen: the object (the function whose call drew it), what called that
// (its context: the race's HUD, a frontend screen), and the cell of a 3x3
// grid over the 4:3 picture its middle lies in (the race's POS and LAP are
// the same object, top left and top right). The same in every run.

enum class Anchor { kCentre, kLeft, kRight, kStretch };

struct ElementId {
    u32 context = 0, object = 0;
    char cell[3] = {'?', '?', 0};        // row T M B, column L C R
    bool operator<(const ElementId& o) const {
        // Rows top to bottom, each left to right.
        if (cell[0] != o.cell[0]) return std::string("TMB").find(cell[0]) < std::string("TMB").find(o.cell[0]);
        if (cell[1] != o.cell[1]) return std::string("LCR").find(cell[1]) < std::string("LCR").find(o.cell[1]);
        if (object != o.object) return object < o.object;
        return context < o.context;
    }
    bool operator==(const ElementId& o) const {
        return context == o.context && object == o.object && cell[0] == o.cell[0] && cell[1] == o.cell[1];
    }
    bool known() const { return object != 0; }
    std::string Key() const {
        char key[48];
        std::snprintf(key, sizeof key, "hud.%06X.%06X.%s", unsigned(context), unsigned(object), cell);
        return key;
    }
};

/* The port's own anchors (the HUD editor's settings take precedence). */
const std::pair<const char*, Anchor> kDefaultAnchors[] = {
    // The race (chosen in play with the HUD editor): position, the boost
    // bar and their panels on the left, the speed on the right; what the top
    // corners hold (the lap, the times) to their sides.
    {"hud.1A0780.1A5730.BL", Anchor::kLeft},
    {"hud.1A0780.1A6280.BC", Anchor::kLeft},
    {"hud.1A0780.1A6280.BL", Anchor::kLeft},
    {"hud.1A0780.1D5FA0.TL", Anchor::kLeft},
    {"hud.1A0780.1D5FA0.TR", Anchor::kRight},
    {"hud.1A0780.352C80.TR", Anchor::kStretch},
    {"hud.1A0780.381380.BR", Anchor::kRight},
    {"hud.1A0780.381E00.TL", Anchor::kLeft},
    {"hud.1A0780.381E00.TR", Anchor::kRight},
    {"hud.1A0780.3CDC80.BR", Anchor::kRight},
    {"hud.1A0780.3CDC80.TL", Anchor::kLeft},
    {"hud.1A0780.3CDC80.TR", Anchor::kRight},
    {"hud.1A0780.3D6F90.TR", Anchor::kRight},
    {"hud.1A0780.3D7EA0.BC", Anchor::kStretch},
    {"hud.1A0780.3D7EA0.BR", Anchor::kRight},
    {"hud.1D75B0.1A7AF0.BC", Anchor::kLeft},
    {"hud.1D75B0.1A7AF0.BL", Anchor::kLeft},
    {"hud.1D75B0.1A8170.BL", Anchor::kLeft},
    // The frontend.
    {"hud.30D740.31B1D0.TC", Anchor::kStretch},
    {"hud.30D740.31B1D0.TL", Anchor::kLeft},
    {"hud.30D740.3B3240.MC", Anchor::kStretch},
    {"hud.30D740.3B3240.MR", Anchor::kStretch},
    {"hud.30D740.3D7EA0.BC", Anchor::kStretch},
    {"hud.30D740.3D7EA0.BR", Anchor::kRight},
};

const char* AnchorName(Anchor a) {
    switch (a) {
        case Anchor::kLeft: return "left";
        case Anchor::kRight: return "right";
        case Anchor::kStretch: return "stretch";
        default: return "centre";
    }
}

/* An element's anchor: its setting, else the port's default, else centred;
   remembered until the editor changes one. */
std::map<std::string, Anchor> g_anchors;
Anchor AnchorOf(const ElementId& id) {
    if (!id.known()) return Anchor::kCentre;
    const std::string key = id.Key();
    auto found = g_anchors.find(key);
    if (found != g_anchors.end()) return found->second;
    Anchor anchor = Anchor::kCentre;
    for (const auto& [name, value] : kDefaultAnchors)
        if (key == name) anchor = value;
    const std::string set = ps2host::Setting(key, "");
    if (set == "left") anchor = Anchor::kLeft;
    else if (set == "right") anchor = Anchor::kRight;
    else if (set == "centre") anchor = Anchor::kCentre;
    else if (set == "stretch") anchor = Anchor::kStretch;
    g_anchors[key] = anchor;
    return anchor;
}

/* 4:3 over the picture's shape: 1 for a 4:3 picture. */
float Squeeze() { return std::min(1.0f, (4.0f / 3.0f) / ps2host::PictureAspect()); }

/* x (about `centre`, `half` its distance to the picture's edges) brought in
   by k towards the middle, or the edge it is anchored to; stretched, as the
   game drew it - across the wider picture. */
float Place(float x, float centre, float half, float k, Anchor anchor) {
    if (anchor == Anchor::kStretch) return x;
    const float a = anchor == Anchor::kLeft ? centre - half : anchor == Anchor::kRight ? centre + half : centre;
    return a + (x - a) * k;
}

/* The cell of a point of the 4:3 picture, (0, 0) its top left, 640 x 448. */
void CellOf(float x, float y, char cell[3]) {
    cell[0] = y < 448.0f / 3 ? 'T' : y < 896.0f / 3 ? 'M' : 'B';
    cell[1] = x < 640.0f / 3 ? 'L' : x < 1280.0f / 3 ? 'C' : 'R';
    cell[2] = 0;
}

/* Who drew: the object (the caller of the function drawing) and its
   context, from the guest call stack, the drawing function innermost. The
   context is two calls further out: the frontend's layers (0x001D75B0)
   call every object, and what calls them is the race (0x001A0780) or a
   frontend screen (0x0030D740). */
void Caller(ElementId* id) {
    const std::vector<u32>& calls = ps2host::CurrentCalls();
    const std::size_t n = calls.size();
    id->object = n >= 2 ? calls[n - 2] : 0;
    id->context = n >= 4 ? calls[n - 4] : 0;
}

// ------------------------------------------------------------ the 2D batch
//
// 0x0021A4E0 flushes a batch of finished GS packets that its builders (the
// HUD: 0x00219820, 0x00218CF0, 0x002187A0 and the like) appended in
// scratchpad, [gp-0x6D20] to [gp-0x5D98]. Each builder's call is a piece:
// the bytes it appended and who called it. At the flush every XYZ word is
// found (per primitive, PRIM tracked through the batch) and brought in -
// where it draws into the 640-wide picture (FRAME tracked; not the 64x32
// target the race's sky reads), by its element's anchor. A primitive across
// the whole picture keeps its vertices at the edges (backdrops, fades; a
// highlight's rule still reaches them, its ends brought in); a scissor
// narrower than the picture comes in too (the frontend's tickers).

/* What the batches' A+D writes leave set, from one flush to the next. */
struct Batch2D {
    u32 fbw = 10;                       // FRAME_1's FBW: 10 the 640-wide picture
    int centre = 0x8000;                // XYOFFSET_1's X + 320 pixels, in GS units (1/16)
    int middle = 0x7200 + 224 * 16;     // ...and its Y + 224
    u32 prim = 6;                       // PRIM's type: 6 a sprite
    u32 starts = 0;                     // PRIM writes so far: each begins a strip or fan afresh
};
Batch2D g_batch;

struct Piece {
    u32 begin, end;
    ElementId id;                       // its cell found at the flush
};
std::vector<Piece> g_pieces;            // appended since the last flush
std::vector<Piece> g_building;          // builders running: where each began, who called

/* A builder: its call is a piece. Each builder flushes first when the batch
   has no room for it (some again on the way): what it appended before a
   flush is a piece of that flush (Flush2D), and it goes on from the emptied
   batch's start. Taken from where it began before the flush, a builder's
   piece came out empty and its vertices belonged to no element - drawn
   centred, whichever builder found the batch full that frame (the race's
   BEST label flipping between its anchor and the middle, apart from its
   shadow). */
template <void (*Build)(lifted::Context&)>
void Builder(lifted::Context& c) {
    const u32 gp = u32(c.r[28]);
    Piece piece{u32(generated::ReadU32(gp - 0x5D98u)), 0, {}};
    Caller(&piece.id);
    g_building.push_back(piece);
    Build(c);
    piece = g_building.back();
    g_building.pop_back();
    const u32 end = generated::ReadU32(gp - 0x5D98u);
    if (end > piece.begin) g_pieces.push_back({piece.begin, end, piece.id});
}

bool IsXyz(u32 reg) { return reg == 0x4 || reg == 0x5 || reg == 0xC || reg == 0xD; }
u64 Read64(u32 at) { return generated::ReadU32(at) | u64(generated::ReadU32(at + 4)) << 32; }

/* An XYZ word: the primitive it is a vertex of, and where it draws - into
   the picture or not (another target), about which middle. */
struct Vertex2D {
    u32 at, prim, start;
    bool picture;
    int centre, middle;
};

/* SCISSOR_1 (an A+D write at `at`) narrower than the picture: brought in
   with what it clips, or that shows past its panel. The whole picture's
   stays. */
void SqueezeScissor(u32 at, float k) {
    const u64 scissor = Read64(at);
    const int x0 = int(scissor & 0x7FF), x1 = int((scissor >> 16) & 0x7FF);
    if (x0 <= 8 && x1 >= 631) return;
    const auto in = [k](int x) { return std::clamp(int(320.0f + float(x - 320) * k + 0.5f), 0, 2047); };
    const u64 squeezed = (scissor & ~0x07FF07FFull) | u64(in(x0)) | u64(in(x1)) << 16;
    generated::Write32(at, u32(squeezed));
}

/* One GIF tag's vertices (PACKED: its XYZ registers and A+D's; REGLIST)
   with their primitives, its A+D writes applied to g_batch (and a narrow
   scissor squeezed); where the next tag begins. */
u32 TagVertices(u32 at, u32 end, std::vector<Vertex2D>* vertices, float k) {
    const u64 tag = Read64(at), regs = Read64(at + 8);
    at += 16;
    const u32 nloop = u32(tag & 0x7FFF), flg = u32(tag >> 58) & 3;
    const auto set_prim = [](u32 value) {
        g_batch.prim = value & 7;
        ++g_batch.starts;
    };
    if ((tag >> 46) & 1) set_prim(u32(tag >> 47));                // PRE: the tag's PRIM
    u32 nreg = u32(tag >> 60) & 0xF;
    if (!nreg) nreg = 16;
    if (flg == 0) {
        for (u32 l = 0; l < nloop && at + 16 <= end; ++l)
            for (u32 r = 0; r < nreg && at + 16 <= end; ++r, at += 16) {
                const u32 reg = u32(regs >> (4 * r)) & 0xF;
                if (IsXyz(reg)) {
                    vertices->push_back({at, g_batch.prim, g_batch.starts, g_batch.fbw == 10, g_batch.centre, g_batch.middle});
                } else if (reg == 0x0) {
                    set_prim(generated::ReadU32(at));
                } else if (reg == 0xE) {
                    const u32 address = generated::ReadU8(at + 8);
                    if (IsXyz(address)) {
                        vertices->push_back({at, g_batch.prim, g_batch.starts, g_batch.fbw == 10, g_batch.centre, g_batch.middle});
                    } else if (address == 0x00) {
                        set_prim(generated::ReadU32(at));
                    } else if (address == 0x4C) {
                        g_batch.fbw = u32(Read64(at) >> 16) & 0x3F;
                    } else if (address == 0x18) {
                        g_batch.centre = int(Read64(at) & 0xFFFF) + 320 * 16;
                        g_batch.middle = int((Read64(at) >> 32) & 0xFFFF) + 224 * 16;
                    } else if (address == 0x40 && k < 1.0f && g_batch.fbw == 10) {
                        SqueezeScissor(at, k);
                    }
                }
            }
        return at;
    }
    if (flg == 1) {
        const u32 words = nloop * nreg;
        for (u32 i = 0; i < words; ++i) {
            const u32 reg = u32(regs >> (4 * (i % nreg))) & 0xF;
            if (IsXyz(reg)) vertices->push_back({at + 8 * i, g_batch.prim, g_batch.starts, g_batch.fbw == 10, g_batch.centre, g_batch.middle});
            else if (reg == 0x0) set_prim(generated::ReadU32(at + 8 * i));
        }
        return at + (words + 1) / 2 * 16;
    }
    return at + nloop * 16;
}

/* Vertices per primitive of a type: a point, a line, a triangle, a sprite;
   0 for a strip or fan (all of its run, one shape). */
u32 PrimitiveVertices(u32 prim) {
    switch (prim) {
        case 0: return 1;
        case 1: case 6: return 2;
        case 3: return 3;
        default: return 0;
    }
}

/* A batch's vertices into the picture, each placed by its piece's element. */
void PlaceVertices(const std::vector<Vertex2D>& vertices, float k) {
    const int half = 312 * 16;
    // Each vertex's piece (pieces are in order, as appended), and each
    // piece's element: the cell of the middle of what it drew.
    std::vector<int> owner(vertices.size(), -1);
    std::vector<float> lo_x(g_pieces.size(), 1e9f), hi_x(g_pieces.size(), -1e9f);
    std::vector<float> lo_y(g_pieces.size(), 1e9f), hi_y(g_pieces.size(), -1e9f);
    for (std::size_t v = 0; v < vertices.size(); ++v) {
        const u32 at = vertices[v].at;
        auto it = std::upper_bound(g_pieces.begin(), g_pieces.end(), at,
                                   [](u32 a, const Piece& p) { return a < p.begin; });
        if (it == g_pieces.begin()) continue;
        --it;
        if (at >= it->end) continue;
        const int p = int(it - g_pieces.begin());
        owner[v] = p;
        const u32 word = generated::ReadU32(at);
        const float x = float(int(word & 0xFFFF) - vertices[v].centre) / 16.0f + 320.0f;
        const float y = float(int(word >> 16) - vertices[v].middle) / 16.0f + 224.0f;
        lo_x[p] = std::min(lo_x[p], x);
        hi_x[p] = std::max(hi_x[p], x);
        lo_y[p] = std::min(lo_y[p], y);
        hi_y[p] = std::max(hi_y[p], y);
    }
    std::vector<Anchor> anchors(g_pieces.size(), Anchor::kCentre);
    std::vector<bool> hidden(g_pieces.size(), false);
    for (std::size_t p = 0; p < g_pieces.size(); ++p) {
        if (lo_x[p] > hi_x[p]) continue;
        CellOf((lo_x[p] + hi_x[p]) / 2, (lo_y[p] + hi_y[p]) / 2, g_pieces[p].id.cell);
        anchors[p] = AnchorOf(g_pieces[p].id);
        hidden[p] = false;
    }
    for (std::size_t i = 0; i < vertices.size();) {
        const u32 size = PrimitiveVertices(vertices[i].prim);
        std::size_t j = i + 1;
        if (size == 0) {
            while (j < vertices.size() && vertices[j].start == vertices[i].start) ++j;
        } else {
            j = std::min(vertices.size(), i + size);
        }
        int x0 = 0x10000, x1 = -1;
        for (std::size_t v = i; v < j; ++v) {
            const int x = int(generated::ReadU32(vertices[v].at) & 0xFFFF);
            x0 = std::min(x0, x);
            x1 = std::max(x1, x);
        }
        // One across the picture keeps its vertices at the edges there.
        const int centre = vertices[i].centre;
        const bool across = x0 <= centre - half && x1 >= centre + half;
        const int p = owner[i];
        const Anchor anchor = p >= 0 ? anchors[p] : Anchor::kCentre;
        const bool hide = p >= 0 && hidden[p];
        for (std::size_t v = i; v < j; ++v) {
            const u32 word = generated::ReadU32(vertices[v].at);
            if (hide) {
                // The chosen element, blinking: every vertex at the first's.
                generated::Write32(vertices[v].at, generated::ReadU32(vertices[i].at));
                continue;
            }
            const int x = int(word & 0xFFFF);
            if (across && (x <= centre - half || x >= centre + half)) continue;
            const float placed = Place(float(x), float(centre), 320.0f * 16, k, anchor);
            const int at_x = int(std::lround(placed));
            generated::Write32(vertices[v].at, (word & ~0xFFFFu) | (u32(at_x) & 0xFFFFu));
        }
        i = j;
    }
}

/* 0x0021A4E0: the batch placed, then flushed. */
void Flush2D(lifted::Context& c) {
    const u32 gp = u32(c.r[28]);
    u32 at = generated::ReadU32(gp - 0x6D20u);
    const u32 end = generated::ReadU32(gp - 0x5D98u);
    const float k = Squeeze();
    // A builder flushing on its way: what it appended so far is its piece.
    // PS2HOST_HUD_LOG=1: how often that happens.
    static const bool log = std::getenv("PS2HOST_HUD_LOG") != nullptr;
    static u32 flushes = 0, inside = 0;
    for (const Piece& b : g_building)
        if (end > b.begin) g_pieces.push_back({b.begin, end, b.id}), ++inside;
    if (log && ++flushes % 1000 == 0)
        std::fprintf(stderr, "[hud] %u batches flushed, %u of them inside a builder\n", flushes, inside);
    std::sort(g_pieces.begin(), g_pieces.end(), [](const Piece& x, const Piece& y) { return x.begin < y.begin; });
    std::vector<Vertex2D> all;
    while (at + 16 <= end && end - at < 0x10000) at = TagVertices(at, end, &all, k);
    if (k < 1.0f) {
        std::vector<Vertex2D> picture;
        for (const Vertex2D& v : all)
            if (v.picture) picture.push_back(v);
        PlaceVertices(picture, k);
    }
    g_pieces.clear();
    F_0021A4E0(c);
    // The builders still running go on from the emptied batch.
    for (Piece& b : g_building) b.begin = generated::ReadU32(gp - 0x5D98u);
}

// ------------------------------------------------------------ the frontend's sprites
//
// 0x00364B00(vertices, count) draws the frontend objects' sprites (and the
// rivals' labels, which the game places with its own 4:3 projection): 32-byte
// vertices, x and y floats about the middle of the screen. Placed for the
// draw, the objects' own put back after (they are kept from frame to frame).

void Sprites2D(lifted::Context& c) {
    const float k = Squeeze();
    const u32 vertices = u32(c.r[4]), count = u32(c.r[5]);
    if (k >= 1.0f || count == 0 || count > 4096) {
        F_00364B00(c);
        return;
    }
    std::vector<u32> saved(2 * count);
    float lo_x = 1e9f, hi_x = -1e9f, lo_y = 1e9f, hi_y = -1e9f;
    for (u32 i = 0; i < count; ++i) {
        saved[2 * i] = generated::ReadU32(vertices + 32 * i);
        saved[2 * i + 1] = generated::ReadU32(vertices + 32 * i + 4);
        float x, y;
        std::memcpy(&x, &saved[2 * i], 4);
        std::memcpy(&y, &saved[2 * i + 1], 4);
        lo_x = std::min(lo_x, x);
        hi_x = std::max(hi_x, x);
        lo_y = std::min(lo_y, y);
        hi_y = std::max(hi_y, y);
    }
    ElementId id;
    Caller(&id);
    CellOf((lo_x + hi_x) / 2 + 320.0f, (lo_y + hi_y) / 2 + 224.0f, id.cell);
    const Anchor anchor = AnchorOf(id);
    const bool hide = false;
    for (u32 i = 0; i < count; ++i) {
        float x;
        std::memcpy(&x, &saved[2 * i], 4);
        x = hide ? 0.0f : Place(x, 0.0f, 320.0f, k, anchor);
        u32 bits;
        std::memcpy(&bits, &x, 4);
        generated::Write32(vertices + 32 * i, bits);
        if (hide) generated::Write32(vertices + 32 * i + 4, saved[1]);
    }
    F_00364B00(c);
    for (u32 i = 0; i < count; ++i) {
        generated::Write32(vertices + 32 * i, saved[2 * i]);
        generated::Write32(vertices + 32 * i + 4, saved[2 * i + 1]);
    }
}


} // namespace
void RegisterPalWidescreenHud() {
    ps2host::Override(0x0021A4E0u, Flush2D, "a 2D batch's flush: the HUD placed in a wider picture");
    ps2host::Override(0x00364B00u, Sprites2D, "the frontend's sprites: placed in a wider picture");
    ps2host::Override(0x002178D0u, Builder<F_002178D0>, "a 2D builder: its piece");
    ps2host::Override(0x00217A10u, Builder<F_00217A10>, "a 2D builder: its piece");
    ps2host::Override(0x00217C30u, Builder<F_00217C30>, "a 2D builder: its piece");
    ps2host::Override(0x00217D30u, Builder<F_00217D30>, "a 2D builder: its piece");
    ps2host::Override(0x00217E60u, Builder<F_00217E60>, "a 2D builder: its piece");
    ps2host::Override(0x00217FB0u, Builder<F_00217FB0>, "a 2D builder: its piece");
    ps2host::Override(0x002183F0u, Builder<F_002183F0>, "a 2D builder: its piece");
    ps2host::Override(0x002187A0u, Builder<F_002187A0>, "a 2D builder: its piece");
    ps2host::Override(0x00218CF0u, Builder<F_00218CF0>, "a 2D builder: its piece");
    ps2host::Override(0x00219140u, Builder<F_00219140>, "a 2D builder: its piece");
    ps2host::Override(0x00219520u, Builder<F_00219520>, "a 2D builder: its piece");
    ps2host::Override(0x00219820u, Builder<F_00219820>, "a 2D builder: its piece");
    ps2host::Override(0x00219A10u, Builder<F_00219A10>, "a 2D builder: its piece");
    ps2host::Override(0x00219C90u, Builder<F_00219C90>, "a 2D builder: its piece");
    ps2host::Override(0x00219D40u, Builder<F_00219D40>, "a 2D builder: its piece");
    ps2host::Override(0x00219F10u, Builder<F_00219F10>, "a 2D builder: its piece");
}
