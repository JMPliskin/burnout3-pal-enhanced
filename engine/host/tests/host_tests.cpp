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
#include "ps2host/dma.h"
#include "ps2host/gs.h"
#include "ps2host/memory.h"
#include "ps2host/vif.h"
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

void WideWritesReachEveryWordsHook() {
    ps2host::Memory& m = ps2host::GuestMemory();
    u32 seen[2] = {0, 0};
    m.OnWrite(0x10006000u, [&seen](u32, u64 value) { seen[0] = u32(value); });
    m.OnWrite(0x10006004u, [&seen](u32, u64 value) { seen[1] = u32(value); });
    m.Write(0x10006000u, 0x2222222211111111ull, 8);
    Check(seen[0] == 0x11111111u && seen[1] == 0x22222222u, "a 64-bit write reaches both words");
    m.OnWrite(0x10006000u, nullptr);
    m.OnWrite(0x10006004u, nullptr);
}

void VifSkipsDataAndAppliesMaskPath3() {
    ps2host::VifStream vif;
    int masks = 0;
    bool masked = false;
    vif.on_mask_path3 = [&](bool on) { ++masks; masked = on; };
    // DIRECT of one quadword whose data looks like MSKPATH3: it must be skipped.
    vif.Word(0x50000001u);
    for (int i = 0; i < 4; ++i) vif.Word(0x06008000u);
    Check(masks == 0, "DIRECT data is not decoded");
    vif.Word(0x06008000u);
    Check(masks == 1 && masked, "MSKPATH3 masks");
    vif.Word(0x06000000u);
    Check(masks == 2 && !masked, "MSKPATH3 unmasks");
    // UNPACK V4-32 of 2 elements: 8 words. V2-16 of 3: 3 words.
    vif.Word(0x6C020000u);
    for (int i = 0; i < 8; ++i) vif.Word(0x06008000u);
    vif.Word(0x65030000u);
    for (int i = 0; i < 3; ++i) vif.Word(0x06008000u);
    Check(masks == 2 && vif.data_words() == 4 + 8 + 3, "UNPACK lengths");
    // STCYCL CL=1 WL=2: an UNPACK V4-32 of 4 reads 2 elements (filling writes).
    vif.Word(0x01000201u);
    vif.Word(0x6C040000u);
    for (int i = 0; i < 8; ++i) vif.Word(0);
    vif.Word(0x06008000u);
    Check(masks == 3, "UNPACK with WL > CL reads CL of every WL");
    // MPG of 1 instruction pair: 2 words.
    vif.Word(0x4A010000u);
    vif.Word(0x06000000u);
    vif.Word(0x06000000u);
    Check(masks == 3, "MPG data is not decoded");
}

void PutTag(u32 at, u32 qwc, u32 id, u32 address, u32 upper0, u32 upper1) {
    ps2host::Memory& m = ps2host::GuestMemory();
    m.Write(at, qwc | (id << 28), 4);
    m.Write(at + 4, address, 4);
    m.Write(at + 8, upper0, 4);
    m.Write(at + 12, upper1, 4);
}

void DmaChainsAreWalked() {
    ps2host::Memory& m = ps2host::GuestMemory();
    const u32 chain = 0x00200000u, table = 0x00210000u, sub = 0x00220000u;
    // cnt (1 qword after it) -> ref (1 qword at table) -> call sub -> end
    PutTag(chain, 1, 1, 0, 0xA0000001u, 0xA0000002u);
    for (u32 i = 0; i < 4; ++i) m.Write(chain + 16 + 4 * i, 0xD0 + i, 4);
    PutTag(chain + 32, 1, 3, table, 0, 0);
    for (u32 i = 0; i < 4; ++i) m.Write(table + 4 * i, 0xE0 + i, 4);
    PutTag(chain + 48, 0, 5, sub, 0, 0);
    PutTag(sub, 0, 6, 0, 0, 0);                     // ret: back to chain + 64
    PutTag(chain + 64, 0, 7, 0, 0, 0);              // end
    ps2host::DmaChannelState ch{0x145u, 0, 0, chain, 0, 0};    // chain, TTE, STR
    std::vector<u32> words;
    const ps2host::DmaOutcome out = ps2host::SendDma(ch, [&words](u32 w) { words.push_back(w); });
    Check(out.ended && out.tags == 5 && out.quadwords == 2, "a chain is walked to its end tag");
    Check(words.size() == 2 * 5 + 8 && words[0] == 0xA0000001u && words[2] == 0xD0u &&
          words[8] == 0xE0u, "tags' upper halves, then each tag's data, in order");
    Check(((ch.chcr >> 28) & 7) == 7 && ((ch.chcr >> 4) & 3) == 0,
          "the TAG field and the call depth are left as at the end");
    // A chain that never ends is stopped, not followed forever.
    PutTag(chain, 0, 2, chain, 0, 0);               // next -> itself
    ps2host::DmaChannelState loop{0x105u, 0, 0, chain, 0, 0};
    Check(!ps2host::SendDma(loop, [](u32) {}, 100).ended, "a looping chain is cut off");
}

// Every format's layout maps a page's pixels onto distinct bytes (nibbles).
void GsLayoutsArePermutations() {
    using namespace ps2host::gs;
    struct Case { u32 psm, w, h, bw; };
    const Case cases[] = {{PSMCT32, 64, 32, 1}, {PSMZ32, 64, 32, 1}, {PSMCT16, 64, 64, 1},
                          {PSMCT16S, 64, 64, 1}, {PSMZ16, 64, 64, 1}, {PSMZ16S, 64, 64, 1},
                          {PSMT8, 128, 64, 2}, {PSMT4, 128, 128, 2}};
    for (const Case& c : cases) {
        std::vector<int> seen(8192 * 2, 0);
        bool ok = true;
        const u32 bytes = c.psm == PSMT4 ? 0 : c.psm == PSMT8 ? 1 : (c.psm & 2) ? 2 : 4;
        for (u32 y = 0; y < c.h; ++y)
            for (u32 x = 0; x < c.w; ++x) {
                u32 nibble = 0;
                const u32 a = Memory::Address(0, c.bw, x, y, c.psm, &nibble);
                if (a >= 8192) { ok = false; continue; }
                const u32 slot = bytes ? a / bytes : a * 2 + nibble;
                if (seen[slot]++) ok = false;
            }
        char what[64];
        std::snprintf(what, sizeof what, "GS layout 0x%02X covers one page exactly", unsigned(c.psm));
        Check(ok, what);
    }
}

void GsDrawsASprite() {
    using namespace ps2host::gs;
    Gs gs;
    gs.WriteRegister(0x4C, (u64(0) << 0) | (u64(1) << 16) | (u64(PSMCT32) << 24));   // FRAME: fbp 0, 64 wide
    gs.WriteRegister(0x40, u64(63) << 16 | u64(63) << 48);                          // SCISSOR 0..63
    gs.WriteRegister(0x00, 6);                                                      // PRIM: sprite
    gs.WriteRegister(0x01, 0x80402010ull);                                          // RGBAQ
    gs.WriteRegister(0x05, (u64(4) << 4) | (u64(4) << 4) << 16);                    // XYZ2 (4,4)
    gs.WriteRegister(0x05, (u64(8) << 4) | (u64(6) << 4) << 16);                    // XYZ2 (8,6)
    Check(gs.memory.Read(0, 1, 5, 5, PSMCT32) == 0x80402010u, "a sprite's pixels take its colour");
    Check(gs.memory.Read(0, 1, 8, 5, PSMCT32) == 0 && gs.memory.Read(0, 1, 5, 6, PSMCT32) == 0,
          "a sprite stops short of its far edges");
    Check(gs.primitives() == 1 && gs.pixels_drawn() == 8, "one sprite of 4x2 pixels");
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
    WideWritesReachEveryWordsHook();
    VifSkipsDataAndAppliesMaskPath3();
    DmaChainsAreWalked();
    GsLayoutsArePermutations();
    GsDrawsASprite();
    std::printf(g_failures ? "host tests: %d failed\n" : "host tests: all passed\n", g_failures);
    return g_failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
