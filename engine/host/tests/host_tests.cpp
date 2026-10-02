/*
 * The host's own tests, independent of any game: built with every port and run
 * by engine/tools/port.py before the port itself.
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "ps2host/disc.h"
#include "ps2host/memory.h"
#include "ps2rt/generated.h"

namespace {

int g_failures = 0;

void Check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++g_failures;
    }
}

void MirrorsReachTheSameRam() {
    ps2host::Memory& m = ps2host::GuestMemory();
    m.Write(0x00100000u, 0x11223344u, 4);
    Check(m.Read(0x20100000u, 4) == 0x11223344u, "uncached mirror");
    Check(m.Read(0x30100000u, 4) == 0x11223344u, "uncached-accelerated mirror");
    Check(m.Read(0x80100000u, 4) == 0x11223344u, "kseg0");
    Check(m.Read(0xA0100000u, 4) == 0x11223344u, "kseg1");
}

void LoadsExtendAsTheEeDoes() {
    generated::Write32(0x00100010u, 0x80000001u);
    Check(generated::ReadS32(0x00100010u) == 0xFFFFFFFF80000001ull, "lw sign-extends");
    Check(generated::ReadU32(0x00100010u) == 0x80000001ull, "lwu zero-extends");
    generated::Write8(0x00100014u, 0xF0);
    Check(generated::ReadS8(0x00100014u) == 0xFFFFFFFFFFFFFFF0ull, "lb sign-extends");
}

void OutsideMemoryFaults() {
    bool faulted = false;
    try {
        ps2host::GuestMemory().Read(0x40000000u, 4);
    } catch (const ps2host::MemoryFault& fault) {
        faulted = fault.address == 0x40000000u && !fault.write;
    }
    Check(faulted, "an unmapped read faults");
}

void ScratchpadAndHooks() {
    ps2host::Memory& m = ps2host::GuestMemory();
    m.Write(ps2host::kScratchpadBase + 8, 0x55u, 1);
    Check(m.Read(ps2host::kScratchpadBase + 8, 1) == 0x55u, "scratchpad");
    int writes = 0;
    m.OnWrite(0x10003000u, [&](u32, u64 value) { writes += value == 7; });
    m.OnRead(0x10003000u, [](u32, u64 stored) { return stored + 1; });
    m.Write(0x10003000u, 7, 4);
    Check(writes == 1, "a write hook sees the value");
    Check(m.Read(0x10003000u, 4) == 8, "a read hook shapes the value");
}

void Kseg1ReachesDeviceRegisters() {
    ps2host::Memory& m = ps2host::GuestMemory();
    m.Write(0xB0001010u, 0x1234u, 4);                 // T1_MODE through kseg1
    Check(m.Read(0x10001010u, 4) == 0x1234u, "kseg1 reaches the EE registers");
    Check(m.Read(0x80100000u, 4) == m.Read(0x00100000u, 4), "kseg0 reaches RDRAM");
}

// A directory record: its length, extent, size, flags and name.
void Record(std::vector<u8>& image, u32 at, u32 lsn, u32 size, u8 flags, const std::string& name) {
    const u8 length = u8(33 + name.size() + (name.size() % 2 == 0 ? 1 : 0));
    image[at] = length;
    for (int i = 0; i < 4; ++i) {
        image[at + 2 + i] = u8(lsn >> (8 * i));
        image[at + 10 + i] = u8(size >> (8 * i));
    }
    image[at + 25] = flags;
    image[at + 32] = u8(name.size());
    std::memcpy(&image[at + 33], name.data(), name.size());
}

void DiscFindsFilesByPath() {
    using ps2host::kSectorSize;
    std::vector<u8> image(24 * kSectorSize, 0);
    u8* pvd = &image[16 * kSectorSize];
    pvd[0] = 1;
    std::memcpy(pvd + 1, "CD001", 5);
    Record(image, 16 * kSectorSize + 156, 20, kSectorSize, 2, std::string(1, '\0'));
    u32 at = 20 * kSectorSize;
    Record(image, at, 20, kSectorSize, 2, std::string(1, '\0')); at += 34;
    Record(image, at, 20, kSectorSize, 2, std::string(1, '\1')); at += 34;
    Record(image, at, 21, 5, 0, "FILE.BIN;1"); at += image[at];
    Record(image, at, 22, kSectorSize, 2, "DIR");
    Record(image, 22 * kSectorSize, 23, 3, 0, "INNER.DAT;1");
    std::memcpy(&image[21 * kSectorSize], "hello", 5);
    ps2host::Disc disc;
    disc.Use(image);
    ps2host::Disc::File file;
    Check(disc.Find("\\FILE.BIN;1", &file) && file.lsn == 21 && file.size == 5, "a file in the root");
    Check(disc.Find("/dir/inner.dat", &file) && file.lsn == 23 && file.size == 3,
          "a file in a directory, any case, no version");
    Check(!disc.Find("\\MISSING.BIN", &file), "a missing file");
    u8 sector[ps2host::kSectorSize];
    Check(disc.Read(21, 1, sector) && std::memcmp(sector, "hello", 5) == 0, "reading a sector");
    Check(!disc.Read(30, 1, sector), "reading past the end");
}

void BiosIsReadOnly() {
    ps2host::Memory& m = ps2host::GuestMemory();
    m.Write(ps2host::kBiosBase, 0xFFu, 1);
    Check(m.Read(ps2host::kBiosBase, 1) == 0, "the BIOS window ignores writes");
}

}  // namespace

int main() {
    MirrorsReachTheSameRam();
    LoadsExtendAsTheEeDoes();
    OutsideMemoryFaults();
    ScratchpadAndHooks();
    BiosIsReadOnly();
    Kseg1ReachesDeviceRegisters();
    DiscFindsFilesByPath();
    std::printf(g_failures ? "host tests: %d failed\n" : "host tests: all passed\n", g_failures);
    return g_failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
