/*
 * Burnout 3's host implementations of guest functions.
 *
 * An override replaces the translated function at its address: the way a port
 * redirects code that drives hardware the host provides differently (the GS, the
 * IOP's services) and the way enhancements hook in. Register them here with
 * ps2host::Override(address, function, name); see engine/docs/PORTING.md.
 */

#include <cstdio>
#include <string>

#include "ps2host/host.h"

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
   movie ends at once: no picture, and the stream marked ended - sceMpegIsEnd
   (0x0010A028) returns the first word of mp->sys, at mp+0x40. The frontend's
   background movie and the intro then finish as soon as they start. */
void MpegGetPicture(lifted::Context& c) {
    const u32 sys = u32(generated::ReadU32(u32(c.r[4]) + 0x40));
    generated::Write32(sys, 1);
    c.r[2] = 0;
}

}  // namespace

void RegisterGameIopServers();     // iop_servers.cpp

void RegisterGameOverrides() {
    RegisterGameIopServers();
    ps2host::Override(0x001156C0u, KernelStore, "kernel-mode store");
    ps2host::Override(0x0010D3E8u, TtyWrite, "TTY write (printf)");
    ps2host::Override(0x00109F00u, MpegGetPicture, "sceMpegGetPicture: movies end at once (no IPU yet)");
}
