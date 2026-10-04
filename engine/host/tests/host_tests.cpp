/*
 * The host's own tests, independent of any game: built with every port and run
 * by engine/tools/port.py before the port itself.
 */

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "ps2host/disc.h"
#include "ps2host/dma.h"
#include "ps2host/gs.h"
#include "iop_vm_internal.h"
#include "spu2.h"
#include "ps2host/memory.h"
#include "ps2host/vif.h"
#include "ps2host/path3.h"
#include "ps2host/vu.h"
#include "ps2host/vu_core.h"
#include "ps2rt/generated.h"

#if PS2HOST_VULKAN
#include "ps2gpu/gs_renderer.h"
#endif

/* vu_corpus.cpp (vurecomp.py --corpus, as the build goes): the random programs, compiled. */
const ps2host::VuCompiled* VuGeneratedPrograms(std::size_t* count);

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
    // The precomputed pages give what the manual's tables give, anywhere.
    const u32 psms[] = {PSMCT32, PSMCT24, PSMCT16, PSMCT16S, PSMT8, PSMT4, PSMT8H, PSMT4HL, PSMT4HH,
                        PSMZ32, PSMZ24, PSMZ16, PSMZ16S};
    u32 seed = 99, wrong = 0;
    for (u32 psm : psms)
        for (int i = 0; i < 20000; ++i) {
            seed = seed * 1664525u + 1013904223u;
            const u32 bp = seed >> 18, bw = 1 + ((seed >> 8) & 15);
            seed = seed * 1664525u + 1013904223u;
            const u32 x = (seed >> 5) & 2047, y = (seed >> 17) & 2047;
            u32 n1 = 0, n2 = 0;
            if (Memory::Address(bp, bw, x, y, psm, &n1) != Memory::AddressReference(bp, bw, x, y, psm, &n2) ||
                n1 != n2)
                ++wrong;
        }
    Check(wrong == 0, "GS addresses from precomputed pages match the step-by-step layout");
    // A row's run (image transfers) stores what one pixel at a time stores.
    auto one = std::make_unique<Memory>(), run = std::make_unique<Memory>();
    for (u32 psm : psms)
        for (int i = 0; i < 300; ++i) {
            seed = seed * 1664525u + 1013904223u;
            const u32 bp = seed >> 18, bw = 1 + ((seed >> 8) & 15);
            seed = seed * 1664525u + 1013904223u;
            const u32 x = (seed >> 5) & 2047, y = (seed >> 17) & 2047, count = std::min(1 + (seed & 255), 2048 - x);
            std::vector<u32> values(count);
            for (u32& v : values) v = seed = seed * 1664525u + 1013904223u;
            for (u32 k = 0; k < count; ++k) one->Write(bp, bw, x + k, y, psm, values[k]);
            run->WriteRun(bp, bw, x, y, psm, values.data(), count);
        }
    Check(std::memcmp(one->data(), run->data(), 4u << 20) == 0, "GS row runs store as single pixels do");
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
    gs.Finish();                                                                    // the queued draws
    Check(gs.memory.Read(0, 1, 5, 5, PSMCT32) == 0x80402010u, "a sprite's pixels take its colour");
    Check(gs.memory.Read(0, 1, 8, 5, PSMCT32) == 0 && gs.memory.Read(0, 1, 5, 6, PSMCT32) == 0,
          "a sprite stops short of its far edges");
    Check(gs.primitives() == 1 && gs.pixels_drawn() == 8, "one sprite of 4x2 pixels");
}

// PATH3 held by MSKPATH3: an unmask lets one EOP-ended group of packets go,
// as Burnout 3's texture uploads need (one group per window, five a picture).
void Path3GoesOneGroupPerWindow() {
    ps2host::Path3Queue queue;
    auto tag = [&](u32 nloop, bool eop, u32 flg, u32 nreg) {
        const u64 low = u64(nloop) | u64(eop) << 15 | u64(flg) << 58 | u64(nreg) << 60;
        queue.Push(u32(low));
        queue.Push(u32(low >> 32));
        queue.Push(0);
        queue.Push(0);
    };
    auto data = [&](u32 qwords) { for (u32 i = 0; i < 4 * qwords; ++i) queue.Push(0xD0D0D0D0u); };
    tag(1, false, 0, 4); data(4);                   // PACKED: A+D setup, no EOP
    tag(3, true, 2, 0); data(3);                    // IMAGE, EOP: the first group ends here
    tag(2, true, 1, 3); data(3);                    // REGLIST 2 x 3 registers: 3 quadwords
    std::vector<u32> sent;
    queue.Release(false, [&](u32 word) { sent.push_back(word); });
    Check(sent.size() == 4 * (1 + 4 + 1 + 3), "an unmask lets the packets up to the first EOP go");
    sent.clear();
    queue.Release(false, [&](u32 word) { sent.push_back(word); });
    Check(sent.size() == 4 * (1 + 3) && queue.empty(), "the next unmask the next group (REGLIST sized by halves)");
    tag(1, false, 2, 0); data(1);
    queue.Release(false, [&](u32 word) { sent.push_back(word); });
    Check(queue.empty(), "a group without its EOP yet goes whole");
}

// Fog (PRIM.FGE): C = (F * C + (255 - F) * FOGCOL) >> 8, F from XYZF2 or the
// FOG register for XYZ2.
void GsFogsTowardsFogcol() {
    using namespace ps2host::gs;
    Gs gs;
    gs.WriteRegister(0x4C, (u64(0) << 0) | (u64(1) << 16) | (u64(PSMCT32) << 24));   // FRAME: fbp 0, 64 wide
    gs.WriteRegister(0x40, u64(63) << 16 | u64(63) << 48);                          // SCISSOR 0..63
    gs.WriteRegister(0x3D, 0x0000FFull);                                            // FOGCOL: red
    gs.WriteRegister(0x00, 6 | 1 << 5);                                             // PRIM: sprite, FGE
    gs.WriteRegister(0x01, 0x80FFFFFFull);                                          // RGBAQ: white
    gs.WriteRegister(0x04, (u64(0) << 4) | (u64(0) << 4) << 16 | u64(0x80) << 56);  // XYZF2 (0,0) F 0x80
    gs.WriteRegister(0x04, (u64(4) << 4) | (u64(4) << 4) << 16 | u64(0x80) << 56);  // XYZF2 (4,4)
    gs.Finish();
    Check(gs.memory.Read(0, 1, 1, 1, PSMCT32) == 0x807F7FFEu, "fog halfway to red: (128 * 255 + 127 * FOGCOL) >> 8");
    gs.WriteRegister(0x0A, u64(0) << 56);                                           // FOG: F 0 for XYZ2
    gs.WriteRegister(0x05, (u64(8) << 4) | (u64(0) << 4) << 16);                    // XYZ2 (8,0)
    gs.WriteRegister(0x05, (u64(12) << 4) | (u64(4) << 4) << 16);                   // XYZ2 (12,4)
    gs.Finish();
    Check(gs.memory.Read(0, 1, 9, 1, PSMCT32) == 0x800000FEu, "F 0 from the FOG register: FOGCOL (* 255 / 256)");
}

// A flat depth stays exact across a triangle: Burnout 3's loading bar is drawn at
// z 0xFFFFFF with GEQUAL over a buffer cleared to 0xFFFFFF, and float
// interpolation once lost the last bit and carved it into stripes.
void GsKeepsAFlatDepthExact() {
    using namespace ps2host::gs;
    Gs gs;
    gs.WriteRegister(0x4C, (u64(0) << 0) | (u64(1) << 16) | (u64(PSMCT32) << 24));   // FRAME: fbp 0, 64 wide
    gs.WriteRegister(0x4E, u64(8) | (u64(PSMZ24 & 0xF) << 24));                      // ZBUF: zbp 8 (blocks 256)
    gs.WriteRegister(0x40, u64(63) << 16 | u64(63) << 48);                          // SCISSOR 0..63
    for (u32 y = 0; y < 64; ++y)
        for (u32 x = 0; x < 64; ++x) gs.memory.Write(256, 1, x, y, PSMZ24, 0xFFFFFF);
    gs.WriteRegister(0x47, 1ull << 16 | 2ull << 17);                                // TEST: Z GEQUAL
    gs.WriteRegister(0x00, 3);                                                      // PRIM: triangle
    gs.WriteRegister(0x01, 0x80FFFFFFull);
    const u64 z = u64(0xFFFFFF) << 32;
    gs.WriteRegister(0x05, (u64(3) << 4) | (u64(1) << 4) << 16 | z);
    gs.WriteRegister(0x05, (u64(61) << 4) | (u64(1) << 4) << 16 | z);
    gs.WriteRegister(0x05, (u64(3) << 4) | (u64(37) << 4) << 16 | z);
    gs.Finish();
    u32 missing = 0;
    for (u32 y = 2; y < 18; ++y)
        for (u32 x = 4; x < 20; ++x)
            if (gs.memory.Read(0, 1, x, y, PSMCT32) != 0x80FFFFFFu) ++missing;
    Check(missing == 0, "a flat depth passes GEQUAL against itself everywhere");
}

// Triangles fill top-left: a pixel on an edge is drawn only when the edge is a
// left or top one, so two triangles sharing an edge draw it once, and a right
// or bottom edge on a pixel leaves it alone. Burnout 3's text-entry keys are
// An IMAGE packet taken a run of words at a time (Gs::Words: its quadwords
// to the transfer together) stores what it stores a word at a time, for
// every format, 24-bit pixels across quadwords and a transfer that ends
// inside the packet included.
void GsTakesImagesInBlocks() {
    using namespace ps2host::gs;
    const u32 psms[] = {PSMCT32, PSMCT24, PSMCT16, PSMCT16S, PSMT8, PSMT4, PSMT8H, PSMT4HL, PSMT4HH,
                        PSMZ32, PSMZ24, PSMZ16, PSMZ16S};
    u32 seed = 7, wrong = 0;
    for (u32 psm : psms) {
        std::vector<u32> stream;
        auto qword = [&](u64 low, u64 high) {
            stream.insert(stream.end(), {u32(low), u32(low >> 32), u32(high), u32(high >> 32)});
        };
        const u32 w = 37, h = 9, qwords = 60;   // 60 quadwords: more than 37x9 pixels of most formats
        qword(4 | u64(1) << 60, 0xE);           // A+D, four registers
        qword(u64(0x40) << 32 | u64(2) << 48 | u64(psm) << 56, 0x50);  // BITBLTBUF: DBP 0x40, DBW 2
        qword(u64(5) << 32 | u64(3) << 48, 0x51);                      // TRXPOS: 5, 3
        qword(w | u64(h) << 32, 0x52);                                 // TRXREG
        qword(0, 0x53);                                                // TRXDIR: host to local
        qword(qwords | u64(2) << 58, 0);        // IMAGE
        for (u32 i = 0; i < 4 * qwords; ++i) stream.push_back(seed = seed * 1664525u + 1013904223u);
        auto one = std::make_unique<Gs>(), blocks = std::make_unique<Gs>();
        for (u32 word : stream) one->Word(3, word);
        for (std::size_t at = 0, step = 1; at < stream.size(); at += step, step = step * 3 % 23 + 1)
            blocks->Words(3, stream.data() + at, u32(std::min(step, stream.size() - at)));
        if (std::memcmp(one->memory.data(), blocks->memory.data(), 4u << 20) != 0 || !one->uploads() ||
            !blocks->uploads())
            ++wrong;
    }
    Check(wrong == 0, "GS image packets taken in blocks store as a word at a time");
}

// cells whose right edge lies on a whole pixel, translucent: drawn on both
// sides they doubled a column.
void GsFillsTopLeft() {
    using namespace ps2host::gs;
    Gs gs;
    gs.WriteRegister(0x4C, (u64(0) << 0) | (u64(1) << 16) | (u64(PSMCT32) << 24));   // FRAME: fbp 0, 64 wide
    gs.WriteRegister(0x40, u64(63) << 16 | u64(63) << 48);                          // SCISSOR 0..63
    gs.WriteRegister(0x42, 0ull | 2ull << 2 | 2ull << 4 | 1ull << 6 | 0x80ull << 32); // ALPHA: Cs * FIX 1.0 + Cd
    gs.WriteRegister(0x00, 3 | 1 << 6);                                             // PRIM: triangle, ABE
    gs.WriteRegister(0x01, 0x80101010ull);
    const auto xy = [](u32 x, u32 y) { return (u64(x) << 4) | (u64(y) << 4) << 16; };
    gs.WriteRegister(0x05, xy(2, 2));                                               // the square (2,2)-(10,10)
    gs.WriteRegister(0x05, xy(10, 2));                                              // in two halves on its diagonal
    gs.WriteRegister(0x05, xy(10, 10));
    gs.WriteRegister(0x05, xy(2, 2));
    gs.WriteRegister(0x05, xy(10, 10));
    gs.WriteRegister(0x05, xy(2, 10));
    gs.Finish();
    u32 once = 0, twice = 0, outside = 0;
    for (u32 y = 0; y < 12; ++y)
        for (u32 x = 0; x < 12; ++x) {
            const u32 rgb = gs.memory.Read(0, 1, x, y, PSMCT32) & 0xFFFFFF;
            const bool inside = x >= 2 && x < 10 && y >= 2 && y < 10;
            if (inside && rgb == 0x101010) ++once;
            if (rgb == 0x202020) ++twice;
            if (!inside && rgb != 0) ++outside;
        }
    Check(once == 64, "every pixel of the square drawn once");
    Check(twice == 0, "the shared diagonal drawn once, not by both halves");
    Check(outside == 0, "the right and bottom edges, on whole pixels, not drawn");
}

#if PS2HOST_VULKAN
// GS local memory on the GPU (--gpu): targets and depth buffers swizzled into it
// and out of it by a compute shader must land where the GS model puts them,
// byte for byte, in every format a target or depth buffer takes.
void GpuMemorySwizzlesAsTheGs() {
    using namespace ps2host::gs;
    std::string error;
    auto gpu = ps2gpu::CreateGpu(&error);
    if (!gpu) {
        std::printf("  (no Vulkan device, GPU local memory unchecked: %s)\n", error.c_str());
        return;
    }
    auto renderer = ps2gpu::CreateGsRenderer(*gpu, &error);
    Check(renderer != nullptr, "the GPU renderer starts");
    if (!renderer) return;
    const u32 w = 128, h = 64, fbw = 2;
    u32 seed = 12345;
    auto next = [&] { seed = seed * 1664525u + 1013904223u; return seed; };
    auto to16 = [](u32 c) {
        return ((c >> 3) & 31) | (((c >> 11) & 31) << 5) | (((c >> 19) & 31) << 10) | ((c >> 31) << 15);
    };
    auto from16 = [](u32 c) {
        return ((c & 31) << 3) | (((c >> 5) & 31) << 11) | (((c >> 10) & 31) << 19) | ((c & 0x8000) ? 0x80000000u : 0);
    };
    Memory reference;                               // what the GPU's copy must hold, all along
    std::vector<u8> gpu_bytes(4u << 20);
    auto same_memory = [&] {
        renderer->MemoryRead(0, 16384, gpu_bytes.data());
        return std::memcmp(gpu_bytes.data(), reference.data(), gpu_bytes.size()) == 0;
    };
    struct Format { u32 psm; bool depth; const char* name; };
    const Format formats[] = {{PSMCT32, false, "CT32"}, {PSMCT24, false, "CT24"}, {PSMCT16, false, "CT16"},
                              {PSMCT16S, false, "CT16S"}, {PSMZ32, true, "Z32"}, {PSMZ24, true, "Z24"},
                              {PSMZ16, true, "Z16"}, {PSMZ16S, true, "Z16S"}};
    u32 base = 0x40;                                // FBP, in 2048-word units; a new place each time
    for (const Format& f : formats) {
        const bool sixteen = f.psm == PSMCT16 || f.psm == PSMCT16S || f.psm == PSMZ16 || f.psm == PSMZ16S;
        // Image to memory.
        std::vector<u32> pixels(w * h);
        for (u32& p : pixels) p = f.depth ? next() & (sixteen ? 0xFFFFu : 0xFFFFFFu) : next();
        if (f.depth) {
            renderer->UploadDepth(base, w, h, pixels.data());
            Check(renderer->DepthToMemory(base, w, h, fbw, f.psm, w, h), "a depth buffer goes to GPU memory");
        } else {
            renderer->UploadTarget(base, fbw, f.psm, pixels.data(), w, h);
            Check(renderer->TargetToMemory(base, fbw, f.psm, w, h), "a target goes to GPU memory");
        }
        for (u32 y = 0; y < h; ++y)
            for (u32 x = 0; x < w; ++x) {
                const u32 c = pixels[y * w + x];
                reference.Write(base * 32, fbw, x, y, f.psm, !f.depth && sixteen ? to16(c) : c);
            }
        std::string what = std::string(f.name) + ": an image swizzled on the GPU as the GS lays it out";
        Check(same_memory(), what.c_str());
        // Memory to image: other bytes there, from the CPU.
        const u32 blocks = fbw * (sixteen ? 1 : 2) * 32;   // the 64 rows' pages
        for (u32 i = base * 32 * 256; i < (base * 32 + blocks) * 256; ++i) reference.data()[i] = u8(next() >> 24);
        renderer->MemoryWrite(base * 32, base * 32 + blocks, reference.data() + base * 32 * 256);
        std::vector<u32> back;
        u32 bw_ = 0, bh = 0;
        if (f.depth) {
            renderer->DepthFromMemory(base, w, h, fbw, f.psm);
            Check(renderer->ReadDepth(base, w, h, &back), "a depth buffer reads back");
        } else {
            renderer->TargetFromMemory(base, fbw, f.psm, w, h);
            Check(renderer->ReadTarget(base, fbw, f.psm, &back, &bw_, &bh) && bw_ == w, "a target reads back");
        }
        u32 wrong = 0;
        for (u32 y = 0; y < h && back.size() == w * h; ++y)
            for (u32 x = 0; x < w; ++x) {
                const u32 raw = reference.Read(base * 32, fbw, x, y, f.psm);
                // Depth goes through a float (Z / 2^32): exact up to 24 bits.
                const u32 want = f.depth ? (raw >= 0xFFFFFF80u ? 0xFFFFFFFFu : u32(float(raw)))
                                         : sixteen ? from16(raw) : raw;
                if (back[y * w + x] != want) ++wrong;
            }
        what = std::string(f.name) + ": memory unswizzled on the GPU as the GS reads it";
        Check(back.size() == w * h && wrong == 0, what.c_str());
        base += 8;
    }
}

// Textures decoded on the GPU from its copy of local memory (--gpu, when the
// GPU holds their blocks) must come out texel for texel as the GS model
// decodes them: every format, CLUTs (CSA for 4-bit ones), TEXA, region modes.
void GpuDecodesTexturesAsTheGs() {
    using namespace ps2host::gs;
    std::string error;
    auto gpu = ps2gpu::CreateGpu(&error);
    if (!gpu) return;                               // reported by GpuMemorySwizzlesAsTheGs
    auto renderer = ps2gpu::CreateGsRenderer(*gpu, &error);
    if (!renderer) return;
    Gs gs;
    gs.rasterize = false;
    u32 seed = 777;
    for (u32 i = 0; i < (4u << 20); ++i) {
        seed = seed * 1664525u + 1013904223u;
        gs.memory.data()[i] = u8(seed >> 24);
    }
    renderer->MemoryWrite(0, 16384, gs.memory.data());
    std::vector<u32> decoded;
    gs.on_draw = [&](const Gs::DrawCall&) {
        u32 w = 0, h = 0;
        gs.DecodeTexture(&decoded, &w, &h);
    };
    struct Case { u32 psm, cpsm, csa; u64 clamp; const char* name; };
    const u64 region_clamp = 2 | 2 << 2 | u64(5) << 4 | u64(40) << 14 | u64(3) << 24 | u64(20) << 34;
    const u64 region_repeat = 3 | 3 << 2 | u64(15) << 4 | u64(32) << 14 | u64(7) << 24 | u64(8) << 34;
    const Case cases[] = {
        {PSMCT32, 0, 0, 0, "CT32"}, {PSMCT24, 0, 0, 0, "CT24"}, {PSMCT16, 0, 0, 0, "CT16"},
        {PSMCT16S, 0, 0, region_clamp, "CT16S, region clamp"}, {PSMT8, 0, 0, 0, "T8, CT32 CLUT"},
        {PSMT8, PSMCT16, 0, region_repeat, "T8, CT16 CLUT, region repeat"}, {PSMT4, 0, 3, 0, "T4, CSA 3"},
        {PSMT8H, 0, 0, 0, "T8H"}, {PSMT4HL, 0, 5, 0, "T4HL"}, {PSMT4HH, 0, 1, 0, "T4HH"},
        {PSMZ32, 0, 0, 0, "Z32"}, {PSMZ24, 0, 0, 0, "Z24"}, {PSMZ16, 0, 0, 0, "Z16"}, {PSMZ16S, 0, 0, 0, "Z16S"}};
    u64 key = 1;
    for (const Case& k : cases) {
        const u32 tbp = 0x480, tbw = 2, lw = 6, lh = 6;             // 64 x 64
        const u64 texa = 0x40 | 1ull << 15 | 0x90ull << 32;           // TA0 0x40, AEM, TA1 0x90
        const u64 tex0 = u64(tbp) | u64(tbw) << 14 | u64(k.psm) << 20 | u64(lw) << 26 | u64(lh) << 30 |
                         u64(0x2400) << 37 | u64(k.cpsm) << 51 | u64(k.csa) << 56 | 1ull << 61;   // CLD 1
        gs.WriteRegister(0x3B, texa);
        gs.WriteRegister(0x08, k.clamp);
        gs.WriteRegister(0x06, tex0);
        gs.WriteRegister(0x00, 6 | 1 << 4);                           // a textured sprite
        gs.WriteRegister(0x05, 0);
        gs.WriteRegister(0x05, (u64(16) << 4) | (u64(16) << 4) << 16);
        ps2gpu::GsTextureDecode d;
        d.tbp = tbp;
        d.tbw = tbw;
        d.psm = k.psm;
        d.tw = 1u << lw;
        d.th = 1u << lh;
        d.wms = u32(k.clamp & 3);
        d.wmt = u32(k.clamp >> 2 & 3);
        d.minu = u32(k.clamp >> 4 & 1023);
        d.maxu = u32(k.clamp >> 14 & 1023);
        d.minv = u32(k.clamp >> 24 & 1023);
        d.maxv = u32(k.clamp >> 34 & 1023);
        d.texa_lo = u32(texa & 0x80FF);
        d.ta1 = u32(texa >> 32 & 0xFF);
        d.csa = k.csa;
        d.clut = gs.Clut().data();
        renderer->TextureFromMemory(key, d);
        std::vector<u32> back;
        u32 w = 0, h = 0;
        const bool read = renderer->ReadTexture(key, &back, &w, &h);
        u32 wrong = 0;
        for (std::size_t i = 0; i < back.size() && i < decoded.size(); ++i) wrong += back[i] != decoded[i];
        const std::string what = std::string(k.name) + ": decoded on the GPU as the GS model decodes it";
        Check(read && back.size() == decoded.size() && decoded.size() == 64 * 64 && wrong == 0, what.c_str());
        ++key;
    }
}
#endif

// VIF1 into a VU: a microprogram loaded with MPG and run with MSCAL, UNPACK's
// formats, mask and skipping writes, and XGKICK.
void VifRunsMicroprogramsOnAVu() {
    ps2host::Vu vu(1);
    ps2host::VifStream vif;
    vif.vu = &vu;
    vif.double_buffered = true;
    std::vector<std::uint64_t> kicked;
    vu.on_kick = [&](std::uint64_t low, std::uint64_t) { kicked.push_back(low); };
    auto f = [](float x) { std::uint32_t u; std::memcpy(&u, &x, 4); return u; };
    const std::uint32_t kNopU = 0x000002FF, kNopL = 0x8000033C, kEnd = 0x40000000;
    const std::uint32_t program[] = {
        // 0: lq.xyzw vf1, 0(vi0)              1: add.xyzw vf2, vf1, vf1
        0x01E10000, kNopU, kNopL, 0x01E108A8,
        // 2: sq.xyzw vf2, 1(vi0) [E]          3: (the pair after the end)
        0x03E01001, kNopU | kEnd, kNopL, kNopU,
        // 4: iaddiu vi1, vi0, 2               5: xgkick vi1 [E]          6: -
        0x10010002, kNopU, 0x80000EFC, kNopU | kEnd, kNopL, kNopU};
    vif.Word(0x4A000000u | (7u << 16));                     // MPG 7 pairs at 0
    for (std::uint32_t w : program) vif.Word(w);
    vif.Word(0x01000101u);                                  // STCYCL CL 1 WL 1
    vif.Word(0x6C010000u);                                  // UNPACK V4-32, 1 at 0
    for (float x : {1.0f, 2.0f, 3.0f, 4.0f}) vif.Word(f(x));
    vif.Word(0x14000000u);                                  // MSCAL 0
    const std::uint32_t* q = vu.Quad(1);
    Check(q[0] == f(2) && q[1] == f(4) && q[2] == f(6) && q[3] == f(8), "VU: lq, add, sq");
    Check(vu.resume == 4 * 8, "VU: a program ends after the pair following its E bit");

    // V3-16 masked: w from the row register.
    vif.Word(0x20000000u);                                  // STMASK
    vif.Word(0x40u);                                        // cycle 0, w: row
    vif.Word(0x30000000u);                                  // STROW
    for (std::uint32_t w : {0u, 0u, 0u, f(1)}) vif.Word(w);
    vif.Word(0x79010003u);                                  // UNPACK V3-16 masked, 1 at 3
    vif.Word(0x0002FFFFu);
    vif.Word(0x00000003u);
    q = vu.Quad(3);
    Check(q[0] == 0xFFFFFFFFu && q[1] == 2 && q[2] == 3 && q[3] == f(1), "UNPACK V3-16, sign-extended, masked");

    // CL 2, WL 1: every other quadword.
    vif.Word(0x01000102u);                                  // STCYCL CL 2 WL 1
    vif.Word(0x6002000Au);                                  // UNPACK S-32, 2 at 10
    vif.Word(5);
    vif.Word(6);
    Check(vu.Quad(10)[3] == 5 && vu.Quad(11)[0] == 0 && vu.Quad(12)[1] == 6, "UNPACK skipping writes");

    // A GIF packet at quadword 2: one A+D register write, then EOP.
    std::uint32_t* tag = vu.Quad(2);
    tag[0] = 1u | 1u << 15;                                 // NLOOP 1, EOP
    tag[1] = 1u << 28;                                      // FLG PACKED, NREG 1
    tag[2] = 0xE;                                           // A+D
    tag[3] = 0;
    vif.Word(0x01000101u);
    vif.Word(0x14000004u);                                  // MSCAL 4 (byte 32)
    Check(kicked.size() == 2 && (kicked[0] & 0x7FFF) == 1, "XGKICK sends the tag and its data");
}

// The VU's visible timing: the flags reach FMAND four cycles after the FMAC
// operation, Q seven after DIV (WAITQ waits for it), and an upper beside a LOI
// reads the I from before - as Burnout 3's microcode depends on.
// The FMAC's four lanes at once (single precision, SSE) give what the
// arithmetic model gives a lane at a time in double: every operation, operand
// form, dest mask and destination, on bit patterns rich in zeros, denormals,
// infinities, NaNs and values near overflow and underflow.
void VuLanesAreTheModel() {
    auto context = std::make_unique<lifted::Context>(), scalar = std::make_unique<lifted::Context>();
    u32 seed = 2024, wrong = 0, runs = 0;
    auto next = [&] { return seed = seed * 1664525u + 1013904223u; };
    auto value = [&]() -> u32 {
        const u32 r = next(), sign = r & 0x80000000u;
        switch ((r >> 8) % 8) {
            case 0: return sign;                                            // zero
            case 1: return sign | (r & 0x007FFFFFu);                        // denormal
            case 2: return sign | 0x7F800000u | ((r >> 4) & 1);             // infinity, NaN
            case 3: return sign | 0x7F000000u | (next() & 0x007FFFFFu);     // near the top
            case 4: return sign | 0x00800000u | (next() & 0x00FFFFFFu);     // near the bottom
            case 5: return sign | 0x3F800000u | (next() & 0x007FFFFFu);     // about one
            default: return next();
        }
    };
    for (int i = 0; i < 200000; ++i) {
        for (auto& v : context->vf) for (u32& lane : v) lane = value();
        for (u32& lane : context->vacc) lane = value();
        context->vc[20 + 1] = value();                                       // I
        context->vc[20 + 2] = value();                                       // Q
        lifted::FmacOp f;
        f.op = int(next() % 7);
        f.operand = int(next() % 7);
        f.to_acc = (next() & 3) == 0;
        f.fd = next() % 32;
        f.fs = next() % 32;
        f.ft = next() % 32;
        f.dest = next() % 16;
        *scalar = *context;
        lifted::RunFmac(*context, f);
        lifted::RunFmacScalar(*scalar, f);
        ++runs;
        if (std::memcmp(context->vf, scalar->vf, sizeof context->vf) != 0 ||
            std::memcmp(context->vacc, scalar->vacc, sizeof context->vacc) != 0)
            ++wrong;
    }
    char what[96];
    std::snprintf(what, sizeof what, "VU FMAC lanes at once match the model (%u of %u differ)", wrong, runs);
    Check(wrong == 0, what);
}

// Random microprograms (vu_corpus.cpp, from engine/tools/vurecomp.py
// --corpus), compiled ahead of time, run as the interpreter runs them: from
// random registers and memory, every register, flag, pipeline, cycle and
// byte of data memory the same at the end - and the pairs counted.
void VuCompiledIsTheInterpreter() {
    std::size_t count = 0;
    const ps2host::VuCompiled* programs = VuGeneratedPrograms(&count);
    u32 seed = 77;
    auto next = [&] { return seed = seed * 1664525u + 1013904223u; };
    auto number = [&]() -> u32 {
        const u32 r = next();
        switch (r % 6) {
            case 0: return 0;
            case 1: return 0x3F800000u ^ (r & 0x80000000u);                    // +-1
            case 2: return (r & 0x807FFFFFu) | 0x3F000000u;                     // about one
            case 3: return (r & 0x807FFFFFu) | 0x43000000u;                     // about 128-256
            default: return next();
        }
    };
    u32 wrong = 0, runs = 0, entered = 0;
    for (std::size_t i = 0; i < count; ++i) {
        const ps2host::VuCompiled& program = programs[i];
        if (!program.block_count) continue;
        for (int trial = 0; trial < 6; ++trial) {
            auto interpreted = std::make_unique<ps2host::Vu>(program.unit);
            auto ahead = std::make_unique<ps2host::Vu>(program.unit);
            for (ps2host::Vu* vu : {interpreted.get(), ahead.get()}) {
                for (std::size_t at = 0; at < vu->micro.size(); at += 8) {     // NOPs around the program
                    const u32 nop[2] = {0x8000033Cu, 0x000002FFu};
                    std::memcpy(vu->micro.data() + at, nop, 8);
                }
                const u32* code = program.code;
                const u32 runs_in = *code++;
                for (u32 r = 0; r < runs_in; ++r) {
                    std::memcpy(vu->micro.data() + code[0], code + 2, 4 * code[1]);
                    code += 2 + code[1];
                }
            }
            const u32 start = seed;
            for (ps2host::Vu* vu : {interpreted.get(), ahead.get()}) {
                seed = start;
                for (int r = 1; r < 32; ++r)
                    for (u32& lane : vu->regs.vf[r]) lane = number();
                for (u32& lane : vu->regs.vacc) lane = number();
                for (int r = 1; r < 16; ++r) vu->regs.vc[r] = next() % ((next() & 1) ? 16 : 0x10000);
                vu->regs.vc[20] = 0x3F800000u | (next() & 0x7FFFFF);
                vu->regs.vc[21] = number();
                vu->regs.vc[22] = number();
                for (std::size_t b = 0; b < vu->data.size(); b += 4) {
                    const u32 value = number();
                    std::memcpy(vu->data.data() + b, &value, 4);
                }
                vu->mac = next() & 0xFFFF;
                vu->status = next() & 0xFFF;
                vu->clip = next() & 0xFFFFFF;
                vu->p = number();
            }
            interpreted->use_compiled = false;
            const u64 limit = 3000;
            const u64 a = interpreted->Run(program.entry, limit), b = ahead->Run(program.entry, limit);
            ++runs;
            if (ahead->compiled_pairs()) ++entered;
            if (a != b || !interpreted->SameState(*ahead)) ++wrong;
        }
    }
    char what[128];
    std::snprintf(what, sizeof what, "VU programs compiled ahead run as interpreted (%u of %u runs differ)", wrong, runs);
    Check(wrong == 0 && runs > 0, what);
    // (A jump to a register's address mostly leaves for the NOPs around: the
    // interpreter goes on there.)
    std::snprintf(what, sizeof what, "VU compiled programs taken where they are (%u of %u runs)", entered, runs);
    Check(entered == runs, what);
}

void VuTimingIsVisible() {
    ps2host::Vu vu(0);
    auto f = [](float x) { std::uint32_t u; std::memcpy(&u, &x, 4); return u; };
    const std::uint32_t kNopU = 0x000002FF, kNopL = 0x8000033C, kI = 0x80000000, kEnd = 0x40000000;
    const std::uint32_t program[] = {
        0x100200FF, 0x0100006C,         // iaddiu vi2, vi0, 0xFF    sub.x vf1, vf0, vf0 (zero: MAC Zx)
        0x34011000, kNopU,              // fmand vi1, vi2: the flags from before
        0x81E003BC, kNopU,              // div Q, vf0w, vf0w
        kNopL, 0x002000DC,              // mulq.w vf3, vf0, Q: the Q from before
        0x34031000, kNopU,              // fmand vi3, vi2: four cycles on, the sub's
        0x800003BF, kNopU,              // waitq
        kNopL, 0x0020011C,              // mulq.w vf4, vf0, Q
        f(3), kI | kNopU,               // loi 3
        f(5), kI | 0x0020015E,          // loi 5                    muli.w vf5, vf0, I
        kNopL, kEnd | 0x0020019E,       //                          muli.w vf6, vf0, I [E]
        kNopL, kNopU};
    std::memcpy(vu.micro.data(), program, sizeof program);
    vu.Run(0);
    Check(vu.regs.vc[1] == 0 && vu.regs.vc[3] == 8, "VU: FMAND sees the MAC flags four cycles late");
    Check(vu.regs.vf[3][3] == 0 && vu.regs.vf[4][3] == f(1), "VU: Q arrives after DIV's latency; WAITQ waits");
    Check(vu.regs.vf[5][3] == f(3) && vu.regs.vf[6][3] == f(5), "VU: an upper beside LOI reads the old I");
}

// The IOP interpreter against emu.py, the verified R5900 interpreter: one MIPS I
// instruction per vector (engine/tools/iop_vectors.py).
struct CpuVector {
    const char* name;
    u32 word;
    u32 regs[32];
    u8 data[64];
    u32 out[32];
    u32 hi, lo;
    u8 data_out[64];
};

const CpuVector kCpuVectors[] = {
#include "iop_cpu_vectors.inc"
};

void IopInstructionsMatchTheR5900() {
    using namespace ps2host::iop;
    Vm& vm = TheVm();
    int failures = 0;
    for (const CpuVector& v : kCpuVectors) {
        vm.Write(0x00100000u, v.word, 4);
        for (u32 i = 0; i < 64; ++i) vm.Write(0x00100100u + i, v.data[i], 1);
        Context c;
        for (u32 i = 1; i < 32; ++i) c.r[i] = v.regs[i];
        c.hi = v.regs[0];
        c.lo = v.regs[0] ^ 0x5A5A5A5Au;
        c.pc = 0x00100000u;
        c.npc = 0x00100004u;
        vm.Step(c, 1);
        bool ok = c.hi == v.hi && c.lo == v.lo;
        for (u32 i = 1; i < 32; ++i) ok = ok && c.r[i] == v.out[i];
        for (u32 i = 0; i < 64; ++i) ok = ok && vm.Read(0x00100100u + i, 1) == v.data_out[i];
        if (!ok && failures++ < 5) {
            char what[96];
            std::snprintf(what, sizeof what, "IOP %s (0x%08X) matches the R5900", v.name, unsigned(v.word));
            Check(false, what);
        }
    }
    Check(failures == 0, "every IOP instruction vector matches");
}

}  // namespace

void SpuDecodesAdpcmAndPlaysAVoice() {
    using ps2host::iop::Spu2;
    // Shift 0, filter 0: a nibble is the sample's top four bits.
    Spu2::u8 block[16] = {0x00, 0x00, 0x81};
    Spu2::s32 out[28], older = 0, old = 0;
    Spu2::DecodeBlock(block, out, &older, &old);
    Check(out[0] == 4096 && out[1] == -32768 && out[2] == 0, "ADPCM: shift 0, filter 0");
    // Filter 1 predicts 60/64 of the previous sample.
    Spu2::u8 predicted[16] = {0x1C};
    older = 0;
    old = 64;
    Spu2::DecodeBlock(predicted, out, &older, &old);
    Check(out[0] == 60, "ADPCM: filter 1 predicts from the previous sample");

    // A voice looping one block of 4096s, at full volume once its attack is done.
    Spu2 spu;
    Spu2::u8 loop[16] = {0x00, 0x07};
    for (int i = 2; i < 16; ++i) loop[i] = 0x11;
    spu.Write(0x5000, loop, 16);
    std::vector<Spu2::s16> heard;
    spu.SetOutput([&](const Spu2::s16* frames, Spu2::u32 count) { heard.assign(frames, frames + 2 * count); });
    spu.SetAddr(0x2000, 0x5000);                    // SSA, voice 0 of core 0
    spu.SetParam(0x0000, 0x3FFF);                   // VOLL
    spu.SetParam(0x0100, 0x3FFF);                   // VOLR
    spu.SetParam(0x0200, 0x1000);                   // pitch: 48 kHz
    spu.SetParam(0x0300, 0x000F);                   // ADSR1: fastest attack, full sustain
    spu.SetParam(0x0400, 0x1FC0);                   // ADSR2: sustain holds
    spu.SetSwitch(0x1800, 1);                       // VMIXL
    spu.SetSwitch(0x1A00, 1);                       // VMIXR
    spu.SetSwitch(0x1500, 1);                       // KON
    auto none = [](Spu2::u32) {};
    spu.Advance(1000, none);
    spu.Advance(2000, none);                        // 48 samples
    Check(heard.size() == 96, "SPU2: a millisecond is 48 stereo frames");
    Check(heard.size() == 96 && heard[90] > 3500 && heard[90] < 4200 && heard[91] == heard[90],
          "SPU2: a voice sounds at its volume");
    Check(spu.GetSwitch(0x1700) & 1, "SPU2: ENDX after the loop's end");
    Check(spu.GetParam(0x0500) == 0x7FFF, "SPU2: ENVX after the attack");
}

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
    GsFogsTowardsFogcol();
    Path3GoesOneGroupPerWindow();
    GsKeepsAFlatDepthExact();
    GsFillsTopLeft();
    GsTakesImagesInBlocks();
#if PS2HOST_VULKAN
    GpuMemorySwizzlesAsTheGs();
    GpuDecodesTexturesAsTheGs();
#endif
    VifRunsMicroprogramsOnAVu();
    VuLanesAreTheModel();
    VuCompiledIsTheInterpreter();
    VuTimingIsVisible();
    IopInstructionsMatchTheR5900();
    SpuDecodesAdpcmAndPlaysAVoice();
    std::printf(g_failures ? "host tests: %d failed\n" : "host tests: all passed\n", g_failures);
    return g_failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
