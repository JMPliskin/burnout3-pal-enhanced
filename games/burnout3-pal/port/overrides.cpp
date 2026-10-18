// PAL bootstrap: the shared host supplies the PS2 platform services.
// Regional address hooks require PAL evidence before registration.
#include "ps2host/host.h"
#include "ps2host/run.h"
#include "ps2host/iop_vm.h"
#include "ps2host/mc2.h"
#include <cstring>
#include <cstdio>
#include <cstdlib>

void F_00244208(lifted::Context&);
void F_002445C0(lifted::Context&);
void F_00246578(lifted::Context&);
void F_00246778(lifted::Context&);
void F_00245FB0(lifted::Context&);
void F_002470C8(lifted::Context&);
void F_00245388(lifted::Context&);
void F_00131410(lifted::Context&);

namespace {
// NTSC-U SkipMovie contract, confirmed against PAL 00131410/003251B0.
// Request the original stop (+4A), never synthesize completion (+49).
void SkipMovie(lifted::Context& c) {
    const u32 screen = u32(c.r[4]), movie = screen + 0x1C8;
    static u16 before = 0;
    const u16 held = ps2host::PadButtonsHeld();
    const bool start = (held & 8u) && !(before & 8u);
    before = held;
    const bool playing = generated::ReadU32(screen + 0x2F8) == 0x17 &&
        generated::ReadU8(movie + 0x48) && !generated::ReadU8(movie + 0x49) &&
        !generated::ReadU8(movie + 0x4A);
    if (playing && start && !generated::ReadU8(movie + 0x4B)) {
        generated::Write8(movie + 0x4A, 1);
        if (std::getenv("PS2HOST_MOVIE_LOG"))
            std::fprintf(stderr, "[PAL movie] stop requested: screen %08X movie %u\n",
                screen, unsigned(generated::ReadU32(screen + 0x2FC)));
    }
    F_00131410(c);
}

// Same CardOr contract as NTSC-U; the PAL worker/queue/polling stay original.
template <void (*Host)(lifted::Context&), void (*Game)(lifted::Context&), u32 Address>
void CardOr(lifted::Context& c) {
    if (ps2host::mc2::CardDirectory().empty()) { Game(c); return; }
    const bool log = std::getenv("PAL_MC2_LOG") != nullptr;
    if (log) std::fprintf(stderr, "[PAL mc2] handler %08X args=%08X,%08X,%08X,%08X,%08X,%08X\n",
        Address, u32(c.r[4]), u32(c.r[5]), u32(c.r[6]), u32(c.r[7]), u32(c.r[8]), u32(c.r[9]));
    Host(c);
    if (log) std::fprintf(stderr, "[PAL mc2] result=%016llX\n", static_cast<unsigned long long>(c.r[2]));
}
// PAL instruction audit: sw a1,0(a0), then ErrorEPC <- ra and eret.
// Its timer setters supply B0001000/1010/1020 in their jump delay slots.
// Reuse the NTSC-U host contract, not the privileged CPU implementation.
// Write32 retains the shared host's device hooks and memory fault handling.
void KernelStore(lifted::Context& c) {
    generated::Write32(u32(c.r[4]), u32(c.r[5]));
}

// Reuse games/burnout3/port/iop_servers.cpp's LgDev wire contract.
// PAL lgDevInit at 00443D80 sends procedure 12, 0x240 bytes, and reads +4.
// This is a host server for an absent USB device, not execution of LGDEVW.IRX.
std::vector<u8> LgDev(u32 procedure, const std::vector<u8>& data) {
    constexpr u32 kVersion = 0x010B1B00u;
    constexpr u32 kNoDevice = 0x80000001u;
    std::vector<u8> answer(data.size() < 8 ? 8 : data.size(), 0);
    if (procedure == 12) {
        std::memcpy(&answer[4], &kVersion, 4);
        return answer;
    }
    if (procedure == 1) {
        std::memcpy(&answer[0], &kNoDevice, 4);
        return answer;
    }
    // Unlike the NTSC-U fallback, do not silently answer unknown procedures.
    throw ps2host::Unimplemented{"IOP lgDev procedure " + std::to_string(procedure)};
}
}

#ifdef PAL_RELOCATION_AUDIT
void RegisterRelocationAudits();
#endif

void RegisterGameOverrides() {
    // Same opt-in as NTSC-U: the original IRX runs when loadfile loads it.
    // Keep the shared loader's missing-import traps unchanged.
    ps2host::iop::RunModule("RWA.IRX");
    // NTSC-U uses the original EA filesystem module for RPC 0x00475453.
    ps2host::iop::RunModule("GTFSCDVD.IRX");
#ifdef PAL_RELOCATION_AUDIT
    RegisterRelocationAudits();
#endif
    ps2host::Override(0x001156C0u, KernelStore, "PAL kernel-mode store");
    ps2host::RegisterRpcServer(0x046D046Du, LgDev);
    using namespace ps2host::mc2;
    ps2host::Override(0x00244208u, CardOr<GetInfo, F_00244208, 0x00244208u>, "PAL libmc2 GetInfo");
    ps2host::Override(0x002445C0u, CardOr<Format, F_002445C0, 0x002445C0u>, "PAL libmc2 Format");
    ps2host::Override(0x00246578u, CardOr<ReadFile, F_00246578, 0x00246578u>, "PAL libmc2 ReadFile");
    ps2host::Override(0x00246778u, CardOr<WriteFile, F_00246778, 0x00246778u>, "PAL libmc2 WriteFile");
    ps2host::Override(0x00245FB0u, CardOr<Create, F_00245FB0, 0x00245FB0u>, "PAL libmc2 Create/Mkdir");
    ps2host::Override(0x002470C8u, CardOr<GetDir, F_002470C8, 0x002470C8u>, "PAL libmc2 GetDir");
    ps2host::Override(0x00245388u, CardOr<Lookup, F_00245388, 0x00245388u>, "PAL libmc2 Lookup");
    ps2host::Override(0x00131410u, SkipMovie, "PAL movie update: Start requests original stop");
}
