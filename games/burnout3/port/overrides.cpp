/*
 * Burnout 3's host implementations of guest functions.
 *
 * An override replaces the translated function at its address: the way a port
 * redirects code that drives hardware the host provides differently (the GS, the
 * IOP's services) and the way enhancements hook in. Register them here with
 * ps2host::Override(address, function, name); see engine/docs/PORTING.md.
 */

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "ps2host/host.h"
#include "ps2host/mc2.h"
#include "ps2host/run.h"
#include "ps2host/settings.h"

// libmc2's handlers as the game has them, for when no card is given.
void F_00244218(lifted::Context& c);
void F_002445D0(lifted::Context& c);
void F_00246588(lifted::Context& c);
void F_00246788(lifted::Context& c);
void F_00245FC0(lifted::Context& c);
void F_002470D8(lifted::Context& c);
void F_00245398(lifted::Context& c);
// A screen with a movie: its update each frame (a method; see SkipMovie).
void F_001313C0(lifted::Context& c);
// The race's loading, and the game's (boot, after a race), a step each
// frame (see FastLoading).
void F_0019A950(lifted::Context& c);
// RenderWare's wait for its packet buffers to flip (see AddIdleLoop below).
void F_001F2408(lifted::Context& c);
// The 2D drawing: a batch of GS packets flushed, the frontend's sprites (see Flush2D, Sprites2D).
void F_0021A4F0(lifted::Context& c);
void F_00364920(lifted::Context& c);
// RenderWare's RwCameraSetViewWindow (see SetViewWindow).
void F_001E2110(lifted::Context& c);
// The frame's post effects: glow and speed blur (see PostEffects).
void F_002FA9A0(lifted::Context& c);
void F_001322B0(lifted::Context& c);
// The frontend: its string lookup, the main menu's input, a message box.
void F_00271A00(lifted::Context& c);
void F_00318100(lifted::Context& c);
void F_003181E0(lifted::Context& c);
void F_00321470(lifted::Context& c);
void F_0031C350(lifted::Context& c);

namespace {

/* 0x001156C0, the SDK's kernel-mode store: it disables interrupts, enters kernel
   mode, stores $a1 at $a0 and returns through `eret` with interrupts restored. The
   timer-register setters (0x00115730 and its two neighbours) tail-call it. The
   host has no modes, so the store is all there is to it. */
void KernelStore(lifted::Context& c) {
    generated::Write32(u32(c.r[4]), u32(c.r[5]));
}

/* 0x0010D3E8, the SDK's TTY write over DECI2 (buffer $a0, length $a1): what the
   game prints with printf. A retail console has no debugger to send it to; the
   port logs it, since it is the game telling what it is doing. */
void TtyWrite(lifted::Context& c) {
    const u32 buffer = u32(c.r[4]), length = u32(c.r[5]);
    std::string text;
    for (u32 i = 0; i < length && i < 4096; ++i) text += char(generated::ReadU8(buffer + i));
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) text.pop_back();
    if (!text.empty()) std::fprintf(stderr, "[tty] %s\n", text.c_str());
    c.r[2] = length;
}

/* libmc2's command handlers (FINDINGS: "Memory cards"): with --memcard the
   host's card answers them (ps2host/mc2.h); without one the game's own run,
   and find no card, as a console with an empty slot. The worker dispatches
   commands through 0x004BB320 (indexed by the command less 2); the game
   issues 2 GetInfo, 3 Format, 5 ReadFile, 6 WriteFile, 7 and 11 (Create, for
   files and directories), 10 GetDir and 14 SearchFile, which the worker
   makes of the lookup 0x00245398 itself. */
template <void (*Host)(lifted::Context&), void (*Game)(lifted::Context&)>
void CardOr(lifted::Context& c) {
    if (ps2host::mc2::CardDirectory().empty()) Game(c);
    else Host(c);
}

/* 0x001313C0, the update of a screen that plays a movie (FINDINGS:
   "Skipping movies"): in state 0x17 (+0x2F8) it drives the movie at +0x1C8
   (0x00325030 decodes and shows the next picture). The movie's +0x48 is set
   while it plays, +0x49 once it has finished, and +0x4A asks it to stop -
   0x00324ED0, the game's own request, which the update then honours: it
   cancels the stream's read in flight, marks the movie finished, and the
   screen goes on as at the movie's end. A press of Start (a press, not a
   hold) asks it here, for every movie the game sits through - not one that
   loops (+0x4B: the stream starts again at its end), which plays behind a
   screen waiting for input. */
void SkipMovie(lifted::Context& c) {
    const u32 screen = u32(c.r[4]), movie = screen + 0x1C8;
    static u16 before = 0;
    const u16 held = ps2host::PadButtonsHeld();
    const bool start = (held & 8u) && !(before & 8u);
    before = held;
    const bool playing = generated::ReadU32(screen + 0x2F8) == 0x17 && generated::ReadU8(movie + 0x48) &&
                         !generated::ReadU8(movie + 0x49) && !generated::ReadU8(movie + 0x4A);
    // +0x4B: it loops (the stream goes back to its start at the end) - the
    // title screen's background, which Start is for, not to be skipped.
    const bool loops = generated::ReadU8(movie + 0x4B) != 0;
    static const bool log = std::getenv("PS2HOST_MOVIE_LOG") != nullptr;
    if (log && playing) {
        static std::set<std::tuple<u32, u32, u32>> seen;
        const u32 vtable = generated::ReadU32(screen), which = generated::ReadU32(screen + 0x2FC);
        if (seen.insert({screen, vtable, which}).second)
            std::fprintf(stderr, "[movie] screen %08X class %08X movie %u: +48..4F %02X %02X %02X %02X %02X %02X %02X %02X\n",
                         screen, vtable, which, unsigned(generated::ReadU8(movie + 0x48)),
                         unsigned(generated::ReadU8(movie + 0x49)), unsigned(generated::ReadU8(movie + 0x4A)),
                         unsigned(generated::ReadU8(movie + 0x4B)), unsigned(generated::ReadU8(movie + 0x4C)),
                         unsigned(generated::ReadU8(movie + 0x4D)), unsigned(generated::ReadU8(movie + 0x4E)),
                         unsigned(generated::ReadU8(movie + 0x4F)));
    }
    if (playing && start && !loops) {
        if (log) std::fprintf(stderr, "[movie] skipped: screen %08X movie %u\n", screen, generated::ReadU32(screen + 0x2FC));
        generated::Write8(movie + 0x4A, 1);
    }
    F_001313C0(c);
}

/* The main loop's (0x00132600) loading states, each a function it calls
   once a frame until it returns non-zero (FINDINGS: "Loading a race"):
   0x0019A950 in state 4, a race's world, a phase a frame; 0x001322B0 in
   states 7 and 9, the game's own - the long load at boot, and after a race.
   Most of their time goes to frames going by and the IOP's streams, neither
   CPU busy. While one returns 0 the vertical blanks come faster
   (SetFastForward): the loading screen, a still picture and a spinner, goes
   by sooner. PS2HOST_LOAD_SPEED says how much faster (1: as the console). */
/* The frame's post effects (0x002FA9A0, from the race's and the frontend's
   3D drawing): the frame copied to a half-size buffer, its bright parts
   (glow) and a radial speed blur gathered there over frames, the buffer laid
   over the whole frame (0x002F9F50). At 320x224 that layer softens the
   picture at any internal resolution. BLUR OFF leaves the frame as drawn. */
void PostEffects(lifted::Context& c) {
    if (ps2host::Setting("blur", "original") == "off") return;
    F_002FA9A0(c);
}

/* A function that may wait in an idle loop: in real time the wait sleeps
   (ps2host::InterruptWait). */
template <void (*Wait)(lifted::Context&)>
void WaitsForInterrupt(lifted::Context& c) {
    ps2host::InterruptWait waiting;
    Wait(c);
}

template <void (*Load)(lifted::Context&)>
void FastLoading(lifted::Context& c) {
    static const char* forced = std::getenv("PS2HOST_LOAD_SPEED");
    const unsigned speed = forced ? unsigned(std::max(1, std::atoi(forced))) : 8u;
    Load(c);
    ps2host::SetFastForward(u32(c.r[2]) ? 1 : speed);
}

// ------------------------------------------------------------ PC SETTINGS
//
// The main menu (FINDINGS: "PC settings") gets two entries of the port's:
// its ONLINE (EA's servers are long gone) is PC SETTINGS, a list of the
// port's settings shown in the main menu's own list, and QUIT GAME is added
// after DRIVER DETAILS, asking first in the game's own YES/NO box. The game
// draws every word of it - its strings (0x00271A00, the lookup), its list
// (0x00321470) and ticker (0x0031C350), its message box (0x0032D080).

/* 0x00271A00(holder, id): the string with that id, from the language file
   (DATA/GLOBALUS.BIN, its table at 0x007A4880). Strings of the online flow
   - which nothing can show now - give the port the guest memory its own
   words and lists live in. */
struct Donor { u32 id, chars; };
const Donor kDonors[] = {{108, 122}, {109, 135}, {110, 110}, {182, 99}, {1187, 143}, {1108, 75}, {3161, 82},
                         {2868, 68}, {2870, 68}, {1206, 68}, {112, 71}, {113, 67}, {116, 88}};

/* Hands out guest memory from the donors (they are never shown), in order:
   texts and arrays the menu points at. Made once, when the language file is
   in (the first main menu); the same every time after. */
class GuestPool {
public:
    void Begin(lifted::Context& c) {
        if (ready_) return;
        // The lookup's holder is the main loop's (0x0051BA44): whoever calls
        // here (a list, a ticker) has its own a0.
        u64 saved[32];
        std::memcpy(saved, c.r, sizeof saved);
        for (const Donor& d : kDonors) {
            c.r[4] = 0x0051BA44u;
            c.r[5] = d.id;
            F_00271A00(c);
            if (u32(c.r[2])) regions_.push_back({u32(c.r[2]), 2 * d.chars});
        }
        std::memcpy(c.r, saved, sizeof saved);
        ready_ = true;
    }
    u32 Take(u32 bytes, u32 align = 2) {
        for (auto& [at, left] : regions_) {
            const u32 start = (at + align - 1) & ~(align - 1);
            if (start + bytes <= at + left) {
                left -= start + bytes - at;
                at = start + bytes;
                return start;
            }
        }
        return 0;
    }
private:
    bool ready_ = false;
    std::vector<std::pair<u32, u32>> regions_;      // (next free, bytes left)
};
GuestPool g_pool;

/* A guest UTF-16 string the port writes (room for `chars`). */
struct GuestText {
    u32 at = 0, chars = 0;
    void Make(u32 room) { chars = room; at = g_pool.Take(2 * (room + 1)); }
    void Set(const std::string& text) {
        if (!at) return;
        u32 i = 0;
        for (; i < text.size() && i < chars; ++i) generated::Write16(at + 2 * i, u8(text[i]));
        generated::Write16(at + 2 * i, 0);
    }
};

// The main menu's entries, by its own string ids (0x00318340 builds them).
constexpr u32 kMainMenuOnline = 0xB8;       // "ONLINE"
constexpr u32 kOnlineHelp = 0xEC;           // "PLAY BURNOUT 3 ONLINE", its description
constexpr u32 kEntries = 6;                 // the game's five, and QUIT GAME
constexpr u32 kPcSettingsEntry = 3, kQuitEntry = 5;

/* The settings PC SETTINGS lists: each a few values, Cross going to the
   next (ON/OFF, or the internal resolution's three). Triangle goes back, as
   on the game's own screens (the legend says so): no BACK row - the list's
   six rows are the settings'. */
struct Option {
    const char* name;
    const char* help;
    int (*now)();                           // the value's index
    void (*set)(int);
    const char* values[3];
    int count;
};
int Fullscreen() { return ps2host::WindowFullscreen() ? 1 : 0; }
void SetFullscreen(int on) { ps2host::SetWindowFullscreen(on != 0); }
int Vsync() { return ps2host::WindowVsync() ? 1 : 0; }
void SetVsync(int on) { ps2host::SetWindowVsync(on != 0); }
int FramePacing() { return ps2host::SettingOn("frame_pacing", true) ? 1 : 0; }
void SetFramePacing(int on) { ps2host::SetSettingOn("frame_pacing", on != 0); }
const char* const kResolutions[] = {"original", "2x", "display"};
int Resolution() {
    const std::string now = ps2host::Setting("resolution", "original");
    for (int i = 0; i < 3; ++i)
        if (now == kResolutions[i]) return i;
    return 0;
}
void SetResolution(int which) { ps2host::SetSetting("resolution", kResolutions[which]); }
int Blur() { return ps2host::Setting("blur", "original") == "off" ? 1 : 0; }
void SetBlur(int off) { ps2host::SetSetting("blur", off ? "off" : "original"); }
const char* const kAspects[] = {"4:3", "16:9", "auto"};
int Aspect() {
    const std::string now = ps2host::Setting("aspect", "4:3");
    for (int i = 0; i < 3; ++i)
        if (now == kAspects[i]) return i;
    return 0;
}
void SetAspect(int which) { ps2host::SetWindowAspect(kAspects[which]); }
const Option kOptions[] = {
    {"FULLSCREEN", "THE WHOLE SCREEN OR A WINDOW (ALT+ENTER)", Fullscreen, SetFullscreen, {"OFF", "ON"}, 2},
    {"VERTICAL SYNC", "NO TEARING, OR THE LEAST DELAY", Vsync, SetVsync, {"OFF", "ON"}, 2},
    {"FRAME PACING", "PICTURES EVENLY SPACED, A LITTLE MORE DELAY", FramePacing, SetFramePacing, {"OFF", "ON"}, 2},
    {"RESOLUTION", "DRAWN AS THE PS2 DID, TWICE AS SHARP, OR AS SHARP AS THE SCREEN", Resolution, SetResolution,
     {"ORIGINAL", "2X", "DISPLAY"}, 3},
    {"BLUR", "THE SPEED BLUR AND GLOW, OR A CLEAR PICTURE", Blur, SetBlur, {"ORIGINAL", "OFF"}, 2},
    {"ASPECT RATIO", "THE PICTURE'S SHAPE: A TELEVISION'S, WIDESCREEN, OR THE SCREEN'S OWN", Aspect, SetAspect,
     {"ORIGINAL (4:3)", "WIDESCREEN (16:9)", "AUTO"}, 3},
};
constexpr u32 kOptionCount = sizeof kOptions / sizeof kOptions[0];
constexpr u32 kSettingsRows = kOptionCount;
static_assert(kSettingsRows <= kEntries, "the settings fit the menu's rows");

struct MainMenuState {
    u32 menu = 0;                           // the main menu's object (0x00318340's a0)
    u32 entries = 0, helps = 0;             // our arrays: kEntries entries (8 bytes), help pointers
    u32 game_entries[kEntries][2] = {};     // the main menu's rows, kept while PC SETTINGS shows
    u32 game_helps[kEntries] = {};
    GuestText pc_settings, pc_settings_help, quit, quit_help, quit_question, blank;
    GuestText option_names[kOptionCount], option_helps[kOptionCount];
    bool made = false, settings = false, quitting = false;
};
MainMenuState g_main;

void MakeTexts(lifted::Context& c) {
    if (g_main.made) return;
    g_pool.Begin(c);
    g_main.entries = g_pool.Take(8 * kEntries, 8);
    g_main.helps = g_pool.Take(4 * kEntries, 4);
    g_main.pc_settings.Make(16);
    g_main.pc_settings.Set("PC SETTINGS");
    g_main.pc_settings_help.Make(32);
    g_main.pc_settings_help.Set("SET UP THE PC VERSION");
    g_main.quit.Make(16);
    g_main.quit.Set("QUIT GAME");
    g_main.quit_help.Make(32);
    g_main.quit_help.Set("CLOSE BURNOUT 3");
    g_main.quit_question.Make(32);
    g_main.quit_question.Set("Quit Burnout 3?");
    g_main.blank.Make(2);
    g_main.blank.Set(" ");               // (an empty string the list does not draw as empty)
    for (u32 i = 0; i < kOptionCount; ++i) {
        g_main.option_names[i].Make(36);
        g_main.option_helps[i].Make(64);
        g_main.option_helps[i].Set(kOptions[i].help);
    }
    g_main.made = g_main.entries && g_main.helps;
}

void ShowOption(u32 i) {
    const Option& o = kOptions[i];
    g_main.option_names[i].Set(std::string(o.name) + ": " + o.values[o.now()]);
}

constexpr u32 kQuitQuestion = 0x7F02;       // our string id (past the table's 4,006): the question

void GetString(lifted::Context& c) {
    const u32 id = u32(c.r[5]), caller = u32(c.r[31]);
    // The main menu's ONLINE as PC SETTINGS: asked from 0x003183D0 (it
    // alone; other screens keep their ONLINE), and its description.
    if ((id == kMainMenuOnline && caller == 0x003183D0u) || id == kOnlineHelp || id == kQuitQuestion) {
        MakeTexts(c);
        const GuestText& t = id == kMainMenuOnline ? g_main.pc_settings
                           : id == kOnlineHelp     ? g_main.pc_settings_help
                                                   : g_main.quit_question;
        if (t.at) {
            c.r[2] = t.at;
            return;
        }
    }
    F_00271A00(c);
}

/* Calls a guest function from a host one, its arguments in a0-t1, and gives
   back v0; the caller's registers as they were. */
u32 GuestCall(lifted::Context& c, u32 target, std::initializer_list<u32> arguments) {
    u64 saved[32];
    std::memcpy(saved, c.r, sizeof saved);
    int r = 4;
    for (u32 a : arguments) c.r[r++] = s64(s32(a));
    lifted::Call(c, target, u32(saved[31]));
    const u32 result = u32(c.r[2]);
    std::memcpy(c.r, saved, sizeof saved);
    return result;
}

/* 0x00321470(list, &index, entries): the main menu's list (0x00318340 sets
   it up from 0x00318460, the index and the count at menu+0x14, its five
   entries - a string pointer and a disabled byte each - at menu+0x18). It
   gets six: the game's five and QUIT GAME, in an array of the port's. */
void MenuList(lifted::Context& c) {
    if (u32(c.r[31]) == 0x00318464u) {
        MakeTexts(c);
        if (g_main.made) {
            const u32 from = u32(c.r[6]);
            g_main.menu = u32(c.r[5]) - 0x14;
            for (u32 i = 0; i < 8 * (kEntries - 1); i += 4)
                generated::Write32(g_main.entries + i, generated::ReadU32(from + i));
            generated::Write32(g_main.entries + 8 * kQuitEntry, g_main.quit.at);
            generated::Write32(g_main.entries + 8 * kQuitEntry + 4, 0);
            generated::Write16(u32(c.r[5]) + 2, kEntries);      // the count
            c.r[6] = g_main.entries;
            g_main.settings = g_main.quitting = false;
        }
    }
    F_00321470(c);
}

/* 0x0031C350(ticker, &index, helps, 0): the main menu's descriptions, a
   string pointer each (0x00318340, from 0x003184A4): six, as the list. */
void MenuHelps(lifted::Context& c) {
    if (u32(c.r[31]) == 0x003184A8u && g_main.made) {
        const u32 from = u32(c.r[6]);
        for (u32 i = 0; i < kEntries - 1; ++i) generated::Write32(g_main.helps + 4 * i, generated::ReadU32(from + 4 * i));
        generated::Write32(g_main.helps + 4 * kQuitEntry, g_main.quit_help.at);
        c.r[6] = g_main.helps;
    }
    F_0031C350(c);
}

/* The list (0x01E89544, its entries at +0x10) draws each row's words once,
   and again only when the row is highlighted or left: 0x0031FE40(list, row,
   enabled), which the game calls to enable or disable a row, draws it again
   (0x00320EB0) - every row, its words changed. */
void RefreshList(lifted::Context& c) {
    for (u32 row = 0; row < kEntries; ++row) GuestCall(c, 0x0031FE40u, {0x01E89544u, row, 1});
}

/* PC SETTINGS: the list's rows become the settings, the main menu's kept to
   come back to. */
void ShowSettings(lifted::Context& c, bool on) {
    const u32 menu = g_main.menu;
    if (on) {
        for (u32 i = 0; i < kEntries; ++i) {
            g_main.game_entries[i][0] = generated::ReadU32(g_main.entries + 8 * i);
            g_main.game_entries[i][1] = generated::ReadU32(g_main.entries + 8 * i + 4);
            g_main.game_helps[i] = generated::ReadU32(g_main.helps + 4 * i);
        }
        for (u32 i = 0; i < kOptionCount; ++i) {
            ShowOption(i);
            generated::Write32(g_main.entries + 8 * i, g_main.option_names[i].at);
            generated::Write32(g_main.entries + 8 * i + 4, 0);
            generated::Write32(g_main.helps + 4 * i, g_main.option_helps[i].at);
        }
        // The list keeps its six rows (it is set up with them): any past the
        // settings are blank, and the count Up and Down keep to is theirs.
        for (u32 i = kSettingsRows; i < kEntries; ++i) {
            generated::Write32(g_main.entries + 8 * i, g_main.blank.at);
            generated::Write32(g_main.entries + 8 * i + 4, 0);
            generated::Write32(g_main.helps + 4 * i, g_main.blank.at);
        }
        generated::Write16(menu + 0x16, kSettingsRows);
        generated::Write16(menu + 0x14, 0);
    } else {
        for (u32 i = 0; i < kEntries; ++i) {
            generated::Write32(g_main.entries + 8 * i, g_main.game_entries[i][0]);
            generated::Write32(g_main.entries + 8 * i + 4, g_main.game_entries[i][1]);
            generated::Write32(g_main.helps + 4 * i, g_main.game_helps[i]);
        }
        generated::Write16(menu + 0x16, kEntries);
        generated::Write16(menu + 0x14, kPcSettingsEntry);
    }
    g_main.settings = on;
    RefreshList(c);
    // The button legend along the bottom (0x0031ECD0 on 0x01E89800, its
    // buttons as flags): the main menu's 0x104 (Change, Select); a
    // screen within a menu adds 0x1, (Triangle) Back. A button's sprite
    // and text (the legend's +0x1C and +0x48, a word per slot) stay once
    // made, so Back's (slot 0) go back to the frontend's pool (0x001D7560
    // on 0x01E903E0, by the legend's layer) as the main menu returns.
    constexpr u32 kLegend = 0x01E89800u;
    if (!on) {
        const u32 layer = u32(s32(generated::ReadS16(kLegend + 0x8u)));
        for (const u32 at : {kLegend + 0x1Cu, kLegend + 0x48u}) {
            if (const u32 object = generated::ReadU32(at)) {
                GuestCall(c, 0x001D7560u, {0x01E903E0u, layer, object});
                generated::Write32(at, 0);
            }
        }
    }
    GuestCall(c, 0x0031ECD0u, {kLegend, on ? 0x105u : 0x104u, 3u});
}

/* The frontend's message box (0x0032D080 shows one: the box, its type -
   7 is YES/NO - and the message's string id). Its +0x0A is set while it
   shows, +0x0C the button highlighted (0 the first); 0x0032B2C0(box, pad,
   1) takes a frame's input and returns the button pressed (its id from the
   type's table at 0x004A4A10: YES 4, NO 5) or 0xF; its method +0x0C
   closes it. */
constexpr u32 kMessageBox = 0x01E898E8;
constexpr u32 kYes = 4, kNo = 5;

bool TrianglePressed() {
    static bool before = false;
    const bool now = (ps2host::PadButtonsHeld() & (1u << 12)) != 0;
    const bool pressed = now && !before;
    before = now;
    return pressed;
}

/* 0x003181E0(menu), the main menu's input each frame: Cross (0x0022CFE0) on
   an entry opens it (0x0030D560), Up and Down move. Cross on PC SETTINGS
   lists the settings; on one of them, it switches it (and saves it,
   ps2host/settings.h); Triangle goes back. Cross on QUIT GAME asks
   first. */
void MainMenuInput(lifted::Context& c) {
    const u32 menu = u32(c.r[4]);
    const bool ours = g_main.made && menu == g_main.menu;
    const bool back = TrianglePressed();
    if (ours && g_main.settings && back) {
        ShowSettings(c, false);
        c.r[2] = 1;
        return;
    }
    if (ours && GuestCall(c, 0x0022CFE0u, {0x004F50C0u, 0})) {
        const u32 entry = generated::ReadU16(menu + 0x14);
        if (g_main.settings) {
            if (entry < kOptionCount) {
                const Option& o = kOptions[entry];
                o.set((o.now() + 1) % o.count);
                ShowOption(entry);
                RefreshList(c);
            } else {
                ShowSettings(c, false);
            }
            c.r[2] = 1;
            return;
        }
        if (entry == kPcSettingsEntry) {
            ShowSettings(c, true);
            c.r[2] = 1;
            return;
        }
        if (entry == kQuitEntry) {
            GuestCall(c, 0x0032D080u, {kMessageBox, 7, kQuitQuestion, 0xF, 0});
            generated::Write16(kMessageBox + 0x0C, 1);          // NO highlighted
            g_main.quitting = true;
            c.r[2] = 1;
            return;
        }
    }
    F_003181E0(c);
}

/* 0x00318100(menu), the main menu each frame: by its +0x10, the menu's
   input (1), or a message box's (0 and 2: the online flow's) until a button
   closes it. While QUIT GAME's box shows it has the input: YES ends the
   game (as closing the window does), NO or Triangle closes the box. */
void MainMenu(lifted::Context& c) {
    if (!g_main.quitting) {
        F_00318100(c);
        return;
    }
    const u32 pressed = GuestCall(c, 0x0032B2C0u, {kMessageBox, 0, 1});
    if (pressed == kYes) throw ps2host::GuestExit{0};
    if (pressed == kNo || TrianglePressed()) {
        GuestCall(c, generated::ReadU32(generated::ReadU32(kMessageBox) + 0x0C), {kMessageBox});
        g_main.quitting = false;
    }
}

// ------------------------------------------------------------ ASPECT RATIO
//
// The game draws for a 4:3 television: its cameras (0x00211CC0 sets one's
// field of view) give RenderWare a view window of tan(fov / 2) across and
// that over 4/3 up, each frame (the field of view follows the speed). For a
// wider picture (ASPECT RATIO, PC SETTINGS) a 4:3 window is made wider by
// as much - the same view up and down, more at the sides - and the display
// shows the picture that shape. RenderWare culls to the window it is given.

/* 0x001E2110(camera, &window): RenderWare's RwCameraSetViewWindow. */
void SetViewWindow(lifted::Context& c) {
    const float wider = ps2host::PictureAspect() / (4.0f / 3.0f);
    const u32 window = u32(c.r[5]);
    const u32 x_bits = generated::ReadU32(window), y_bits = generated::ReadU32(window + 4);
    float x, y;
    std::memcpy(&x, &x_bits, 4);
    std::memcpy(&y, &y_bits, 4);
    // A 4:3 window alone (a camera drawing for the screen), the game's own
    // copy put back after.
    if (std::fabs(wider - 1.0f) < 0.001f || !(x > 0.0f) || std::fabs(y / x - 0.75f) > 0.002f) {
        F_001E2110(c);
        return;
    }
    const float wide = x * wider;
    u32 wide_bits;
    std::memcpy(&wide_bits, &wide, 4);
    generated::Write32(window, wide_bits);
    F_001E2110(c);
    generated::Write32(window, x_bits);
}

// The 2D drawing - the HUD, the frontend's words and sprites, the labels
// over the rivals (placed by the game's own 4:3 projection) - is laid out
// for 4:3 too. In a wider picture it keeps its shape, centred: each 2D
// vertex's x is brought towards the middle of the screen by 4:3 over the
// picture's shape. Two ways in:
//   0x0021A4F0 flushes a batch of finished GS packets (the HUD: 0x00218D00,
//     0x002187B0, 0x00219D50 and the like build them in scratchpad): its
//     XYZ words, in GS pixels with XYOFFSET, where it draws into the
//     640-wide picture (FRAME's FBW 10) and not across all of it (a
//     backdrop, a fade); not into the small targets it also draws (a 64x32
//     the race's sky reads).
//   0x00364920(vertices, count) draws the frontend objects' sprites (and the
//     rivals' labels): 32-byte vertices, x a float about the screen's middle.

/* 4:3 over the picture's shape: 1 for a 4:3 picture. */
float HudSqueeze() { return std::min(1.0f, (4.0f / 3.0f) / ps2host::PictureAspect()); }

bool IsXyz(u32 reg) { return reg == 0x4 || reg == 0x5 || reg == 0xC || reg == 0xD; }
u64 Read64(u32 at) { return generated::ReadU32(at) | u64(generated::ReadU32(at + 4)) << 32; }

/* What the batches' A+D writes leave set, from one flush to the next. */
struct Batch2D {
    u32 fbw = 10;                       // FRAME_1's FBW: 10 the 640-wide picture
    int centre = 0x8000;                // XYOFFSET_1's X + 320 pixels, in GS units (1/16)
    u32 prim = 6;                       // PRIM's type: 6 a sprite
    u32 starts = 0;                     // PRIM writes so far: each begins a strip or fan afresh
};
Batch2D g_batch;

/* An XYZ word, and the primitive type it is a vertex of. */
struct Vertex2D {
    u32 at, prim, start;
};

/* SCISSOR_1 (an A+D write at `at`) narrower than the picture: brought in
   with what it clips (the frontend's tickers), or it shows past the
   panels. The whole picture's stays. */
void SqueezeScissor(u32 at, float k) {
    const u64 scissor = Read64(at);
    const int x0 = int(scissor & 0x7FF), x1 = int((scissor >> 16) & 0x7FF);
    if (x0 <= 8 && x1 >= 631) return;
    const auto in = [k](int x) { return std::clamp(int(320.0f + float(x - 320) * k + 0.5f), 0, 2047); };
    const u64 squeezed = (scissor & ~0x07FF07FFull) | u64(in(x0)) | u64(in(x1)) << 16;
    generated::Write32(at, u32(squeezed));
}

/* One GIF tag's vertices (PACKED: its XYZ registers and A+D's; REGLIST)
   with their primitives' types, its A+D writes applied to g_batch (and a
   narrow scissor squeezed); where the next tag begins. */
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
                    vertices->push_back({at, g_batch.prim, g_batch.starts});
                } else if (reg == 0x0) {
                    set_prim(generated::ReadU32(at));
                } else if (reg == 0xE) {
                    const u32 address = generated::ReadU8(at + 8);
                    if (IsXyz(address)) vertices->push_back({at, g_batch.prim, g_batch.starts});
                    else if (address == 0x00) set_prim(generated::ReadU32(at));
                    else if (address == 0x4C) g_batch.fbw = u32(Read64(at) >> 16) & 0x3F;
                    else if (address == 0x18) g_batch.centre = int(Read64(at) & 0xFFFF) + 320 * 16;
                    else if (address == 0x40 && k < 1.0f && g_batch.fbw == 10) SqueezeScissor(at, k);
                }
            }
        return at;
    }
    if (flg == 1) {
        const u32 words = nloop * nreg;
        for (u32 i = 0; i < words; ++i) {
            const u32 reg = u32(regs >> (4 * (i % nreg))) & 0xF;
            if (IsXyz(reg)) vertices->push_back({at + 8 * i, g_batch.prim, g_batch.starts});
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

/* Each primitive squeezed towards the middle - but for one that spans the
   picture, its vertices at the edges. */
void SqueezePrimitives(const std::vector<Vertex2D>& vertices, float k) {
    const int centre = g_batch.centre, half = 312 * 16;
    for (std::size_t i = 0; i < vertices.size();) {
        const u32 prim = vertices[i].prim, size = PrimitiveVertices(prim);
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
        // One across the picture keeps its vertices at the edges there (a
        // backdrop, a fade stay whole; a highlight's rule still reaches the
        // edges, its ends brought in with the words).
        const bool across = x0 <= centre - half && x1 >= centre + half;
        {
            for (std::size_t v = i; v < j; ++v) {
                const u32 word = generated::ReadU32(vertices[v].at);
                const int at_x = int(word & 0xFFFF);
                if (across && (at_x <= centre - half || at_x >= centre + half)) continue;
                const float x = float(at_x - centre) * k;
                const int squeezed = centre + int(x < 0.0f ? x - 0.5f : x + 0.5f);
                generated::Write32(vertices[v].at, (word & ~0xFFFFu) | (u32(squeezed) & 0xFFFFu));
            }
        }
        i = j;
    }
}

/* 0x0021A4F0: the batch in [gp-0x6D20], to [gp-0x5D98], squeezed as it goes. */
void Flush2D(lifted::Context& c) {
    const u32 gp = u32(c.r[28]);
    u32 at = generated::ReadU32(gp - 0x6D20u);
    const u32 end = generated::ReadU32(gp - 0x5D98u);
    const float k = HudSqueeze();
    std::vector<Vertex2D> vertices;
    while (at + 16 <= end && end - at < 0x10000) {
        vertices.clear();
        at = TagVertices(at, end, &vertices, k);
        if (!vertices.empty() && k < 1.0f && g_batch.fbw == 10) SqueezePrimitives(vertices, k);
    }
    F_0021A4F0(c);
}

/* 0x00364920(vertices, count): the vertices squeezed for the draw, the
   objects' own put back after (they are kept from frame to frame). */
void Sprites2D(lifted::Context& c) {
    const float k = HudSqueeze();
    const u32 vertices = u32(c.r[4]), count = u32(c.r[5]);
    if (k >= 1.0f || count > 4096) {
        F_00364920(c);
        return;
    }
    std::vector<u32> saved(count);
    for (u32 i = 0; i < count; ++i) {
        saved[i] = generated::ReadU32(vertices + 32 * i);
        float x;
        std::memcpy(&x, &saved[i], 4);
        x *= k;
        u32 bits;
        std::memcpy(&bits, &x, 4);
        generated::Write32(vertices + 32 * i, bits);
    }
    F_00364920(c);
    for (u32 i = 0; i < count; ++i) generated::Write32(vertices + 32 * i, saved[i]);
}

}  // namespace

void RegisterGameIopServers();     // iop_servers.cpp

void RegisterGameOverrides() {
    RegisterGameIopServers();
    using namespace ps2host::mc2;
    ps2host::Override(0x00244218, CardOr<GetInfo, F_00244218>, "libmc2 GetInfo (host card)");
    ps2host::Override(0x002445D0, CardOr<Format, F_002445D0>, "libmc2 Format (host card)");
    ps2host::Override(0x00246588, CardOr<ReadFile, F_00246588>, "libmc2 ReadFile (host card)");
    ps2host::Override(0x00246788, CardOr<WriteFile, F_00246788>, "libmc2 WriteFile (host card)");
    ps2host::Override(0x00245FC0, CardOr<Create, F_00245FC0>, "libmc2 Create/Mkdir (host card)");
    ps2host::Override(0x002470D8, CardOr<GetDir, F_002470D8>, "libmc2 GetDir (host card)");
    ps2host::Override(0x00245398, CardOr<Lookup, F_00245398>, "libmc2 lookup (host card)");
    // RenderWare's wait for its DMA packet buffers to flip (0x001F2408, the
    // loop at 0x001F24E0: two globals the DMA-end interrupt updates, and a
    // callback each turn): a third of the race's time under --virtual-time
    // went to spinning there (--sample); in real time, 60% of the EE's in the
    // menus, a core spun while it waited for its next picture's blank.
    ps2host::AddIdleLoop(0x001F24E0u);
    ps2host::Override(0x001F2408u, WaitsForInterrupt<F_001F2408>, "RenderWare's packet wait: asleep in real time");
    ps2host::Override(0x002FA9A0u, PostEffects, "the frame's speed blur and glow: BLUR (PC SETTINGS)");
    ps2host::Override(0x0021A4F0u, Flush2D, "a 2D batch's flush: the HUD centred in a wider picture");
    ps2host::Override(0x00364920u, Sprites2D, "the frontend's sprites: centred in a wider picture");
    ps2host::Override(0x001E2110u, SetViewWindow, "RwCameraSetViewWindow: wider for ASPECT RATIO (PC SETTINGS)");
    ps2host::Override(0x001156C0u, KernelStore, "kernel-mode store");
    ps2host::Override(0x0010D3E8u, TtyWrite, "TTY write (printf)");
    ps2host::Override(0x001313C0u, SkipMovie, "a movie screen's update: Start skips the movie");
    ps2host::Override(0x0019A950u, FastLoading<F_0019A950>, "a race's loading: fast-forwarded while it waits");
    ps2host::Override(0x001322B0u, FastLoading<F_001322B0>, "the game's loading: fast-forwarded while it waits");
    ps2host::Override(0x00271A00u, GetString, "the string lookup: PC SETTINGS' words");
    ps2host::Override(0x003181E0u, MainMenuInput, "the main menu's input: PC SETTINGS, QUIT GAME");
    ps2host::Override(0x00318100u, MainMenu, "the main menu: QUIT GAME's message box");
    ps2host::Override(0x00321470u, MenuList, "a list: the main menu's with QUIT GAME");
    ps2host::Override(0x0031C350u, MenuHelps, "a ticker: the main menu's descriptions with QUIT GAME's");
}
