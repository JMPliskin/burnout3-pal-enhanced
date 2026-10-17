// PAL bootstrap: the shared host supplies the PS2 platform services.
// Regional address hooks require PAL evidence before registration.
#include "ps2host/host.h"
#include "ps2host/run.h"
#include "ps2host/iop_vm.h"
#include <cstring>

namespace {
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
}
