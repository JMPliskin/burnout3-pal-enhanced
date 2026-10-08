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
#include <set>
#include <string>
#include <tuple>

#include "ps2host/host.h"
#include "ps2host/mc2.h"
#include "ps2host/run.h"

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
// The race's loading, a step each frame (see FastLoading).
void F_0019A950(lifted::Context& c);

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

/* 0x0019A950, a race's loading (FINDINGS: "Loading a race"): called by the
   main loop (0x00132600, its state 4) once a frame, it takes the load a
   phase further (world+0x143844, 0 to 0x17) as far as the parts it waits
   for are ready, and returns 1 when the race may start. Most phases wait on
   frames going by and on the IOP's streams; neither CPU is busy. While it
   returns 0 the vertical blanks come faster (SetFastForward): the loading
   screen, a still picture and a spinner, goes by sooner.
   PS2HOST_LOAD_SPEED sets how much faster (1: as the console). */
void FastLoading(lifted::Context& c) {
    static const unsigned speed = [] {
        const char* text = std::getenv("PS2HOST_LOAD_SPEED");
        return text ? unsigned(std::max(1, std::atoi(text))) : 8u;
    }();
    F_0019A950(c);
    ps2host::SetFastForward(u32(c.r[2]) ? 1 : speed);
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
    ps2host::Override(0x0019A950u, FastLoading, "a race's loading: fast-forwarded while it waits");
}
