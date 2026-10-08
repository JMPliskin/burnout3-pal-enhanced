/*
 * Burnout 3's host implementations of guest functions.
 *
 * An override replaces the translated function at its address: the way a port
 * redirects code that drives hardware the host provides differently (the GS, the
 * IOP's services) and the way enhancements hook in. Register them here with
 * ps2host::Override(address, function, name); see engine/docs/PORTING.md.
 */

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <set>
#include <string>
#include <tuple>

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
void F_001322B0(lifted::Context& c);
// The frontend: its string lookup, the main menu's input, a message box.
void F_00271A00(lifted::Context& c);
void F_00318100(lifted::Context& c);
void F_003181E0(lifted::Context& c);

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
   by sooner. PS2HOST_LOAD_SPEED sets how much faster (1: as the console). */
template <void (*Load)(lifted::Context&)>
void FastLoading(lifted::Context& c) {
    static const unsigned speed = [] {
        const char* text = std::getenv("PS2HOST_LOAD_SPEED");
        return text ? unsigned(std::max(1, std::atoi(text))) : 8u;
    }();
    Load(c);
    ps2host::SetFastForward(u32(c.r[2]) ? 1 : speed);
}

// ------------------------------------------------------------ PC SETTINGS
//
// The main menu's ONLINE (EA's servers are long gone) becomes PC SETTINGS
// (FINDINGS: "PC settings"): its label, its description and what Cross does
// there. The game draws every word of it - its own strings (0x00271A00, the
// lookup), its own message box (0x0032D080) - so it looks the game's.

// Text the game has no string for, written over strings nothing can show
// now (the online flow's television-standard messages: each has room for
// 85 characters or more), and handed out for these ids.
struct Text { u32 id, donor; const char* text; };
constexpr u32 kMainMenuOnline = 0xB8;       // "ONLINE", as the main menu builds it (0x00318340)
constexpr u32 kOnlineHelp = 0xEC;           // "PLAY BURNOUT 3 ONLINE", its description
constexpr u32 kFullscreenQuestion = 0x7F00; // ours, past the table's 4,006
const Text kTexts[] = {
    {kMainMenuOnline, 0xA1F, "PC SETTINGS"},
    {kOnlineHelp, 0xA20, "SET UP THE PC VERSION"},
    {kFullscreenQuestion, 0xF9D, "Play in fullscreen?"},
};

/* 0x00271A00(holder, id): the string with that id, from the language file
   (DATA/GLOBALUS.BIN, its table at 0x007A4880). The main menu's ONLINE is
   PC SETTINGS when the main menu asks for it (it alone calls from
   0x003183D0; other screens keep their ONLINE). */
void GetString(lifted::Context& c) {
    const u32 id = u32(c.r[5]), caller = u32(c.r[31]);
    for (const Text& t : kTexts) {
        if (t.id != id || (id == kMainMenuOnline && caller != 0x003183D0u)) continue;
        c.r[5] = t.donor;
        F_00271A00(c);
        const u32 at = u32(c.r[2]);
        if (!at) return;
        u32 i = 0;
        for (; t.text[i]; ++i) generated::Write16(at + 2 * i, u8(t.text[i]));
        generated::Write16(at + 2 * i, 0);
        return;
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

/* The frontend's message box (0x0032D080 shows one: the box, its type -
   7 is YES/NO - and the message's string id). Its +0x0A is set while it
   shows, +0x0C the button highlighted (0 the first); 0x0032B2C0(box, pad,
   1) takes a frame's input and returns the button pressed (its id from the
   type's table at 0x004A4A10: YES 4, NO 5) or 0xF; its method +0x0C
   closes it. */
constexpr u32 kMessageBox = 0x01E898E8;
constexpr u32 kYes = 4, kNo = 5;
bool g_settings_open = false;

/* 0x003181E0(menu), the main menu's input each frame: Cross (0x0022CFE0) on
   an entry opens it - entry 3, ONLINE, the online flow (a message box first).
   On entry 3 it opens PC SETTINGS instead: a YES/NO box asking for
   fullscreen, the setting as it is highlighted (MainMenu takes it from
   there). */
void MainMenuInput(lifted::Context& c) {
    const u32 menu = u32(c.r[4]);
    if (generated::ReadU16(menu + 0x14) == 3 && GuestCall(c, 0x0022CFE0u, {0x004F50C0u, 0})) {
        GuestCall(c, 0x0032D080u, {kMessageBox, 7, kFullscreenQuestion, 0xF, 0});
        generated::Write16(kMessageBox + 0x0C, ps2host::WindowFullscreen() ? 0 : 1);
        g_settings_open = true;
        c.r[2] = 1;
        return;
    }
    F_003181E0(c);
}

/* 0x00318100(menu), the main menu each frame: by its +0x10, the menu's
   input (1), or a message box's (0 and 2: the online flow's) until a button
   closes it. While PC SETTINGS' box shows it has the input: YES or NO sets
   fullscreen (and saves it, ps2host/settings.h), Triangle leaves it as it
   is; either closes the box. */
void MainMenu(lifted::Context& c) {
    if (!g_settings_open) {
        F_00318100(c);
        return;
    }
    const u32 pressed = GuestCall(c, 0x0032B2C0u, {kMessageBox, 0, 1});
    const bool back = (ps2host::PadButtonsHeld() & (1u << 12)) != 0;    // Triangle
    if (pressed == kYes || pressed == kNo || back) {
        if (pressed == kYes || pressed == kNo) ps2host::SetWindowFullscreen(pressed == kYes);
        GuestCall(c, generated::ReadU32(generated::ReadU32(kMessageBox) + 0x0C), {kMessageBox});
        g_settings_open = false;
    }
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
    // went to spinning there (--sample).
    ps2host::AddIdleLoop(0x001F24E0u);
    ps2host::Override(0x001156C0u, KernelStore, "kernel-mode store");
    ps2host::Override(0x0010D3E8u, TtyWrite, "TTY write (printf)");
    ps2host::Override(0x001313C0u, SkipMovie, "a movie screen's update: Start skips the movie");
    ps2host::Override(0x0019A950u, FastLoading<F_0019A950>, "a race's loading: fast-forwarded while it waits");
    ps2host::Override(0x001322B0u, FastLoading<F_001322B0>, "the game's loading: fast-forwarded while it waits");
    ps2host::Override(0x00271A00u, GetString, "the string lookup: PC SETTINGS' words");
    ps2host::Override(0x003181E0u, MainMenuInput, "the main menu's input: ONLINE is PC SETTINGS");
    ps2host::Override(0x00318100u, MainMenu, "the main menu: PC SETTINGS' message box");
}
