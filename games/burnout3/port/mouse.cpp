/*
 * The mouse in the frontend's menus: the game's own pointer over them (blue,
 * in the menus' style: port/cursor_art.h, from port/tools/make_cursor.py),
 * the row under it highlighted as Up and Down would, a left click choosing
 * it (Cross), a right click going back (Triangle), the wheel moving as Up
 * and Down. Only while a menu shows: elsewhere the pointer is hidden and the
 * mouse does nothing.
 *
 * Where the rows are (FINDINGS: "The mouse in the menus"): the frontend's
 * words are objects with their place - x, y, width, height at +0 to +0xC, in
 * its 640 x 480, the top left of the words as drawn - and a menu keeps its
 * rows' objects, and a selector: the row highlighted, then the count (two
 * u16s). A list (0x003210D0 sets one up, every list of the frontend's) has
 * its rows' objects at +0x14 + 4 * row and its selector's address at +0xC;
 * it ends when its rows' objects go back to the frontend's pool (0x001D7560).
 * A message box - two kinds, the frontend's (0x0032B2C0 its input each frame
 * it shows) and the driver details screens' (0x00449150) - has its selector
 * at +0xC and its buttons' objects at +0x20 or +0x38 + 4 * button.
 *
 * The mouse moves the highlight as the player would: Up or Down, a press at
 * a time, until the row under it is the menu's - the screen's own input
 * moves it, with its sounds, past the rows it skips. A click presses Cross
 * once it is there.
 */

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <vector>

#include "cursor_art.h"
#include "ps2host/host.h"
#include "ps2host/run.h"

// The frontend's lists set up; an object back to its pool.
void F_003210D0(lifted::Context& c);
void F_001D7560(lifted::Context& c);
// The message boxes' input each frame.
void F_0032B2C0(lifted::Context& c);
void F_00449150(lifted::Context& c);

// widescreen.cpp: a frontend element's x where the picture shows it.
float FrontendPlace(u32 object, float x, float cx, float cy);
// prompts.cpp: the button prompts for the device used last.
void UpdatePrompts(bool mouse_used);

namespace {

// DualShock 2 inputs (libpad's order).
constexpr u16 kUp = 1u << 4, kDown = 1u << 6, kTriangle = 1u << 12, kCross = 1u << 14;

constexpr u32 kMaxRows = 32;

const bool kLog = std::getenv("PS2HOST_MOUSE_LOG") != nullptr;     // what the mouse does

float F(u32 at) {
    const u32 bits = generated::ReadU32(at);
    float value;
    std::memcpy(&value, &bits, 4);
    return value;
}

u64 Tick();

/* Whether the picture's point (x, y) - 640 x 448, as the 4:3 picture - is
   on a frontend object: its place (in the frontend's 640 x 480) where the
   picture shows it (in a wider picture, by its anchor), a little more
   around (the rows' gaps are small). */
bool Over(u32 object, float x, float y) {
    if (!object) return false;
    const float ox = F(object + 0), oy = F(object + 4), w = F(object + 8), h = F(object + 0xC);
    if (!(w > 0.0f && h > 0.0f && w < 1000.0f && h < 500.0f)) return false;
    constexpr float kRows = 448.0f / 480.0f;
    const float y0 = oy * kRows, y1 = (oy + h) * kRows, cx = ox + w * 0.5f, cy = (y0 + y1) * 0.5f;
    const u32 drawn_by = generated::ReadU32(object + 0x3C);
    const float x0 = FrontendPlace(drawn_by, ox, cx, cy), x1 = FrontendPlace(drawn_by, ox + w, cx, cy);
    return x >= x0 - 8.0f && x <= x1 + 8.0f && y >= y0 - 2.0f && y <= y1 + 2.0f;
}

// ------------------------------------------------------------ the legend

/* The buttons along the bottom of the frontend's screens (0x01E89800:
   0x0031ECD0 sets which): +0x0A set while the frontend shows (none in a
   race). A button's words are an object at +0x1C + 4 * slot, its icon at
   +0x48 + 4 * slot - the slot the button's: 0 Triangle (Back), 1 Cross
   (Select, Confirm), 9 Circle (Driver Details, Color); the D-pad's Change
   (+0x94, +0x98) is only a hint. A click on one presses it. */
constexpr u32 kLegend = 0x01E89800u;

bool FrontendUp() { return generated::ReadU8(kLegend + 0xA) != 0; }

u16 SlotButton(u32 slot) {
    switch (slot) {
        case 0: return 1u << 12;        // Triangle
        case 1: return 1u << 14;        // Cross
        case 9: return 1u << 13;        // Circle
        default: return 0;
    }
}

/* The button of the legend under (x, y) in the picture: 0 none. */
u16 LegendAt(float x, float y, u32* slot_out) {
    for (u32 slot = 0; slot < 11; ++slot) {
        if (Over(generated::ReadU32(kLegend + 0x1C + 4 * slot), x, y) ||
            Over(generated::ReadU32(kLegend + 0x48 + 4 * slot), x, y)) {
            *slot_out = slot;
            return SlotButton(slot);
        }
    }
    return 0;
}

// ------------------------------------------------------------ the lists

struct List {
    u32 at = 0;                     // the list
    bool up = false;                // set up, its rows not yet given back
};

std::vector<List> g_lists;

List& ListAt(u32 at) {
    for (List& l : g_lists)
        if (l.at == at) return l;
    g_lists.push_back(List{at});
    return g_lists.back();
}

/* 0x003210D0(list, ...): a list set up - it is up from now. */
void ListSetUp(lifted::Context& c) {
    ListAt(u32(c.r[4])).up = true;
    F_003210D0(c);
}

/* 0x001D7560(pool, layer, object): an object given back - a list whose row
   it was is gone. */
void ObjectFreed(lifted::Context& c) {
    const u32 object = u32(c.r[6]);
    for (List& l : g_lists)
        if (l.up && object && generated::ReadU32(l.at + 0x14) == object) {
            l.up = false;
            if (kLog) std::fprintf(stderr, "[mouse] list 0x%08X gone\n", unsigned(l.at));
        }
    F_001D7560(c);
}

/* The row of list `l` under (x, y), in the frontend's terms; -1 none. Where
   the row's words are now (they slide in); not a disabled row (its entry's
   byte at +4, list+0x10 the entries, 8 bytes each). */
int RowAt(const List& l, float x, float y) {
    const u32 selector = generated::ReadU32(l.at + 0xC);
    const u32 entries = generated::ReadU32(l.at + 0x10);
    if (!selector || !entries) return -1;
    const u16 count = std::min<u16>(generated::ReadU16(selector + 2), kMaxRows);
    for (u32 row = 0; row < count; ++row)
        if (!generated::ReadU8(entries + 8 * row + 4) && Over(generated::ReadU32(l.at + 0x14 + 4 * row), x, y))
            return int(row);
    return -1;
}

// ------------------------------------------------------------ message boxes

struct Box {
    u32 at = 0;
    u32 buttons = 0;            // where its buttons' objects start
    u64 seen = 0;               // the tick it last took input in
};
Box g_box;

template <void (*Original)(lifted::Context&), u32 kButtons>
void BoxInput(lifted::Context& c) {
    const u32 at = u32(c.r[4]);
    if (generated::ReadU8(at + 0xA)) {             // showing
        g_box.at = at;
        g_box.buttons = kButtons;
        g_box.seen = Tick();
    }
    Original(c);
}

bool BoxUp() { return g_box.at && g_box.seen + 2 >= Tick(); }

/* The button of the box under (x, y), in the frontend's terms; -1 none. */
int ButtonAt(float x, float y) {
    const u16 count = std::min<u16>(generated::ReadU16(g_box.at + 0xE), 8);
    for (u32 i = 0; i < count; ++i)
        if (Over(generated::ReadU32(g_box.at + g_box.buttons + 4 * i), x, y)) return int(i);
    return -1;
}

// ------------------------------------------------------------ the pointer

struct Pointer {
    u64 tick = 0;               // the pad's reads: a frame each
    bool shown = false;
    u64 used = 0;               // the tick the mouse last moved, clicked or turned in
    // What the mouse presses, a button a few reads at a time.
    std::deque<std::pair<u16, int>> presses;
    u16 holding = 0;
    int hold_for = 0;
    // The row the highlight is being brought to, by its selector (the index,
    // then the count: a list's or a box's); Cross on it, for a click.
    u32 selector = 0;
    int row = -1;
    bool confirm = false;
    int steps = 0;              // presses made towards it
};

Pointer g_pointer;

u64 Tick() { return g_pointer.tick; }

void Queue(u16 buttons, int reads = 2) {
    g_pointer.presses.emplace_back(buttons, reads);     // held...
    g_pointer.presses.emplace_back(u16(0), reads);      // ...and let go
}

/* Once a frame, as the pad is read: the pointer shown or hidden, what it is
   over, and the buttons the mouse holds. */
u16 MouseTick() {
    Pointer& p = g_pointer;
    ++p.tick;
    ps2host::HostMouseState m;
    const bool mouse = ps2host::TakeHostMouse(&m);
    const bool inside = mouse && m.over && m.x >= 0.0f && m.x <= 1.0f && m.y >= 0.0f && m.y <= 1.0f;
    if (mouse && (m.moved || m.pressed || m.wheel != 0.0f)) p.used = p.tick;
    // The prompts the mouse's once it is really used: a click, the wheel, or
    // a move of 2% of the picture from where it was - not the move a window
    // opening or taking the focus under the pointer reports.
    static float anchor_x = 0.0f, anchor_y = 0.0f;
    static bool anchored = false;
    bool mouse_used = mouse && (m.pressed || m.wheel != 0.0f);
    if (mouse && m.moved) {
        if (!anchored || std::abs(m.x - anchor_x) + std::abs(m.y - anchor_y) > 0.02f) {
            mouse_used = mouse_used || anchored;
            anchor_x = m.x;
            anchor_y = m.y;
            anchored = true;
        }
    }
    UpdatePrompts(mouse_used);
    // The controller or the keyboard: the pointer goes, and what it was doing.
    if ((ps2host::PadButtonsHeld() & ~p.holding) != 0) {
        p.used = 0;
        p.row = -1;
        p.confirm = false;
    }
    // The frontend shows (its legend is up), or a message box.
    const bool box = BoxUp();
    const bool menu = box || FrontendUp();
    const bool show = menu && inside && p.used && p.used + 240 >= p.tick;      // four seconds after its last use
    if (show != p.shown) {
        ps2host::ShowHostCursor(show);
        p.shown = show;
    }
    if (!menu || !mouse) {
        p.presses.clear();
        p.row = -1;
    } else if (inside) {
        // The pointer in the picture's terms, 640 x 448 as the 4:3 picture's
        // (a wider picture is the same 640, its 2D placed in it).
        const float x = m.x * 640.0f, y = m.y * 448.0f;
        // A message box has the input while it shows: the lists behind it wait.
        u32 over_selector = 0;
        int over_row = -1;
        u32 slot = 0;
        const u16 legend = LegendAt(x, y, &slot);
        if (box) {
            over_row = ButtonAt(x, y);
            over_selector = g_box.at + 0xC;
        } else if (FrontendUp()) {
            for (const List& l : g_lists) {
                if (!l.up) continue;
                const int row = RowAt(l, x, y);
                if (row >= 0) {
                    over_selector = generated::ReadU32(l.at + 0xC);
                    over_row = row;
                }
            }
        }
        if (over_row >= 0 && over_selector && (m.moved || (m.pressed & 1))) {
            if (p.selector != over_selector || p.row != over_row) p.steps = 0;
            p.selector = over_selector;
            p.row = over_row;
        }
        if ((m.pressed & 1) && over_row >= 0) p.confirm = true;
        // A button of the legend: pressed.
        if ((m.pressed & 1) && over_row < 0 && legend) Queue(legend);
        if (kLog && (m.pressed & 1) && over_row < 0)
            std::fprintf(stderr, "[mouse] legend: %s slot %u\n", legend ? "button" : "no button", unsigned(slot));
        if (m.pressed & 4) Queue(kTriangle);
        // The wheel: a notch is Up or Down.
        static float wheel = 0.0f;
        wheel += m.wheel;
        while (wheel >= 1.0f) { Queue(kUp); wheel -= 1.0f; }
        while (wheel <= -1.0f) { Queue(kDown); wheel += 1.0f; }
        if (kLog && (m.moved || m.pressed))
            std::fprintf(stderr, "[mouse] %.0f, %.0f: %s %d of 0x%08X%s\n", x, y, over_row >= 0 ? "over" : "not over",
                         over_row, unsigned(over_selector), m.pressed ? " (clicked)" : "");
    }
    // The highlight brought to the row a press at a time; Cross there for a
    // click. A list that will not come there (in twice its rows' presses)
    // is left as it is.
    if (p.row >= 0 && p.presses.empty() && p.hold_for == 0) {
        const int index = generated::ReadU16(p.selector);
        if (index == p.row) {
            if (p.confirm) Queue(kCross);
            p.row = -1;
            p.confirm = false;
        } else if (p.steps++ > 2 * int(generated::ReadU16(p.selector + 2)) + 2) {
            p.row = -1;
            p.confirm = false;
        } else {
            Queue(p.row > index ? kDown : kUp);
            if (kLog) std::fprintf(stderr, "[mouse] %s: row %d of 0x%08X, at %d\n", p.row > index ? "down" : "up",
                                   p.row, unsigned(p.selector), index);
        }
    }
    if (p.hold_for == 0 && !p.presses.empty()) {
        p.holding = p.presses.front().first;
        p.hold_for = p.presses.front().second;
        p.presses.pop_front();
    }
    if (p.hold_for > 0) {
        --p.hold_for;
        return p.holding;
    }
    p.holding = 0;
    return 0;
}

}  // namespace

/* PS2HOST_MOUSE_PROBE=1: every move of a frontend selector (0x00319360 Up,
   0x00319330 Down, which every screen's input calls) - the selector and the
   code moving it, to find a menu the mouse does not know yet. */
void F_00319360(lifted::Context& c);
void F_00319330(lifted::Context& c);
template <void (*Original)(lifted::Context&)>
void SelectorProbe(lifted::Context& c) {
    std::fprintf(stderr, "[probe] selector 0x%08X (%u of %u) moved from 0x%08X\n", unsigned(c.r[4]),
                 unsigned(generated::ReadU16(u32(c.r[4]))), unsigned(generated::ReadU16(u32(c.r[4]) + 2)),
                 unsigned(c.r[31]));
    Original(c);
}

void RegisterMouse() {
    if (std::getenv("PS2HOST_MOUSE_PROBE")) {
        ps2host::Override(0x00319360u, SelectorProbe<F_00319360>, "probe");
        ps2host::Override(0x00319330u, SelectorProbe<F_00319330>, "probe");
    }
    ps2host::SetHostCursor(cursor_art::kPixels, cursor_art::kWidth, cursor_art::kHeight, cursor_art::kHotX,
                           cursor_art::kHotY);
    ps2host::SetPadHook(MouseTick);
    ps2host::Override(0x003210D0u, ListSetUp, "a frontend list set up: up, for the mouse");
    ps2host::Override(0x001D7560u, ObjectFreed, "a frontend object given back: a list gone, for the mouse");
    ps2host::Override(0x0032B2C0u, BoxInput<F_0032B2C0, 0x20>, "a message box's input: its buttons, for the mouse");
    ps2host::Override(0x00449150u, BoxInput<F_00449150, 0x38>,
                      "a driver details message box's input: its buttons, for the mouse");
}
