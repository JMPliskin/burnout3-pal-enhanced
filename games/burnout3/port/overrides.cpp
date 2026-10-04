/*
 * Burnout 3's host implementations of guest functions.
 *
 * An override replaces the translated function at its address: the way a port
 * redirects code that drives hardware the host provides differently (the GS, the
 * IOP's services) and the way enhancements hook in. Register them here with
 * ps2host::Override(address, function, name); see engine/docs/PORTING.md.
 */

#include <cstdio>
#include <map>
#include <string>

#include "ps2host/host.h"
#include "ps2host/mc2.h"

// libmc2's handlers as the game has them, for when no card is given.
void F_00244218(lifted::Context& c);
void F_002445D0(lifted::Context& c);
void F_00246588(lifted::Context& c);
void F_00246788(lifted::Context& c);
void F_00245FC0(lifted::Context& c);
void F_002470D8(lifted::Context& c);
void F_00245398(lifted::Context& c);

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

/* 0x00109F00, libmpeg's sceMpegGetPicture(mp, buffer, macroblocks): decodes the
   next picture on the IPU. The host has no IPU yet (FINDINGS: movies), so a
   movie plays as kMoviePictures blank pictures and then ends - sceMpegIsEnd
   (0x0010A028) returns the first word of mp->sys, at mp+0x40. Ending at the
   first picture left the frontend waiting on its background movie. */
constexpr int kMoviePictures = 120;

void MpegGetPicture(lifted::Context& c) {
    static std::map<u32, int> pictures;          // by stream
    const u32 mp = u32(c.r[4]);
    if (++pictures[mp] >= kMoviePictures) {
        pictures[mp] = 0;
        generated::Write32(u32(generated::ReadU32(mp + 0x40)), 1);
    }
    c.r[2] = 0;
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
    ps2host::Override(0x00109F00u, MpegGetPicture, "sceMpegGetPicture: blank movies (no IPU yet)");
}
