/*
 * Device registers with hardware meaning, stage one: just enough for code that
 * starts a transfer and waits for it, or waits for a vertical blank, to go on.
 *
 * DMA: setting a channel's STR bit starts a transfer; here it completes at once
 *      - the bit clears and the channel's interrupt status is raised. VIF0, VIF1
 *      and GIF transfers are walked (dma.cpp), chains included: VIF1's data
 *      goes to its VIFcode decoder, the rest is counted. Nothing reaches a GS
 *      yet.
 * GIF: while PATH3 is masked, a PATH3 transfer's data waits in the GIF FIFO
 *      (16 quadwords deep), and GIF_STAT's FQC counts it - code handing PATH3
 *      over to VIF1 waits for the GIF to have taken its data before letting VIF1
 *      go. Unmasking drains it.
 * GS:  CSR reports a vertical blank on every read, its field bit alternating, so
 *      a wait for the next frame ends.
 * Timers: in host time, with their interrupts (timers.cpp).
 * VIF1: quadwords stored to its FIFO are decoded as VIFcodes (vif.cpp);
 *      MSKPATH3 shows in GIF_STAT's M3P bit, which code that hands PATH3 over
 *      waits on.
 *
 * Every register the game touches is counted for the report, so the next device
 * to implement is the one the game leans on.
 */

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <deque>
#include <string>

#include "ps2host/dma.h"
#include "ps2host/gs.h"
#include "ps2host/memory.h"
#include "ps2host/path3.h"
#include "ps2host/run.h"
#include "ps2host/vif.h"
#include "ps2host/vu.h"
#include "ps2host/profile.h"

namespace ps2host {

namespace {

struct Channel {
    u32 base;
    const char* name;
    u32 status_bit;                 // its bit in D_STAT
    bool walked;                    // its transfers are followed (dma.cpp)
};

const Channel kChannels[] = {
    {0x10008000u, "VIF0", 0, true}, {0x10009000u, "VIF1", 1, true},
    {0x1000A000u, "GIF", 2, true}, {0x1000B000u, "IPU_FROM", 3, false},
    {0x1000B400u, "IPU_TO", 4, false}, {0x1000C000u, "SIF0", 5, false},
    {0x1000C400u, "SIF1", 6, false}, {0x1000C800u, "SIF2", 7, false},
    {0x1000D000u, "SPR_FROM", 8, true}, {0x1000D400u, "SPR_TO", 9, true},
};

// Register offsets within a channel.
constexpr u32 kMadr = 0x10, kQwc = 0x20, kTadr = 0x30, kAsr0 = 0x40, kAsr1 = 0x50;
constexpr u32 kGifFifoDepth = 16;

constexpr u32 kDStat = 0x1000E010u;

// D_STAT: writing 1 clears a status bit (0-15) and flips a mask bit (16-31), so
// what a store leaves is computed from what was there, kept here.
u32 d_stat = 0;

void SetDStat(u32 value) {
    d_stat = value;
    *reinterpret_cast<u32*>(GuestMemory().Translate(kDStat, 4)) = value;
}
constexpr u32 kGsCsr = 0x12001000u;
constexpr u32 kGifStat = 0x10003020u;
constexpr u32 kGifStatM3P = 1u << 1;
constexpr u32 kGifStatFqcShift = 24, kGifStatFqc = 0x1Fu << kGifStatFqcShift;

// PATH3: GIF DMA data to the GS, held while VIF1 has PATH3 masked - it goes
// after the PATH2 data VIF1 sends meanwhile, as the GIF arbitrates.
//
// A mask takes effect only at the end of the GIF packet PATH3 is in - the one
// whose tag has EOP set - so unmasking and masking again at once lets exactly
// one EOP-ended group through. Burnout 3 queues a picture's texture uploads
// (161,280 words at the car select) on PATH3 while it is masked and opens a
// window like that before each group of draws: five groups, five windows,
// each texture slot refilled between the draws that use its contents.
// Releasing everything at the first window refilled every slot early, and the
// garage was drawn with whatever was uploaded last into its slots (the font).
Path3Queue g_path3_held;
bool g_path3_masked = false;

void Path3(const u32* words, u32 count, u32 from) {
    // Behind what is still held, even unmasked: the GIF keeps PATH3 in order.
    if (g_path3_masked || !g_path3_held.empty()) g_path3_held.Push(words, count);
    else gs::TheGs().Words(3, words, count, from);
}

void ReleasePath3(bool all) {
    g_path3_held.ReleaseBlock(all, [](const u32* words, std::size_t count) {
        gs::TheGs().Words(3, words, u32(count));
    });
}

u32* Register(u32 address) { return reinterpret_cast<u32*>(GuestMemory().Translate(address, 4)); }

// The scratchpad channels: SADR (base + 0x80) is the scratchpad address.
// SPR_FROM copies QWC quadwords from it to MADR; SPR_TO copies from memory
// (normal or a source chain) into it.
constexpr u32 kSadr = 0x80;

// Main RAM or the scratchpad, with nothing watching stores: no hooks to run,
// so a copy may go through pointers.
bool Plain(const Memory& m, u32 address) {
    return !m.watching() && ((address & 0x1FFFFFFFu) < 0x10000000u || (address >> 28) == 0x7);
}

std::string Scratchpad(const Channel& ch, u32 chcr) {
    Memory& m = GuestMemory();
    u32 sadr = u32(m.Read(ch.base + kSadr, 4)) & 0x3FF0u;
    const u32 madr = u32(m.Read(ch.base + kMadr, 4)), qwc = u32(m.Read(ch.base + kQwc, 4));
    char text[160];
    if (ch.base == 0x1000D000u) {                       // SPR_FROM
        if ((chcr >> 2) & 3) return "chain mode is not modelled (nothing copied)";
        for (u32 i = 0; i < 16 * qwc; i += 16) {           // a quadword at a time (SADR is aligned)
            const u32 from = kScratchpadBase + ((sadr + i) & 0x3FF0u);
            u8* to = Plain(m, madr + i) ? m.Translate(madr + i, 16) : nullptr;
            if (const u8* source = to ? m.Translate(from, 16) : nullptr) {
                std::memcpy(to, source, 16);
                continue;
            }
            for (u32 k = 0; k < 16; k += 4) m.Write(madr + i + k, m.Read(from + k, 4), 4);
        }
        *Register(ch.base + kMadr) = madr + 16 * qwc;
        *Register(ch.base + kQwc) = 0;
        *Register(ch.base + kSadr) = (sadr + 16 * qwc) & 0x3FF0u;
        std::snprintf(text, sizeof text, "%u quadwords from scratchpad 0x%04X", unsigned(qwc), unsigned(sadr));
        return text;
    }
    DmaChannelState state{chcr, madr, qwc, u32(m.Read(ch.base + kTadr, 4)),
                          u32(m.Read(ch.base + kAsr0, 4)), u32(m.Read(ch.base + kAsr1, 4))};
    u8* scratchpad = Plain(m, kScratchpadBase) ? m.Translate(kScratchpadBase, kScratchpadSize) : nullptr;
    const DmaOutcome outcome = SendDmaBlocks(state, [&sadr, scratchpad](const u32* words, u32 count, u32 from) {
        for (u32 i = 0; i < count; ++i) {
            g_dma_source = from + 4 * i;
            if (scratchpad) std::memcpy(scratchpad + (sadr & 0x3FFCu), &words[i], 4);
            else GuestMemory().Write(kScratchpadBase + (sadr & 0x3FFCu), words[i], 4);
            sadr = (sadr + 4) & 0x3FFFu;
        }
    });
    *Register(ch.base + kMadr) = state.madr;
    *Register(ch.base + kQwc) = state.qwc;
    *Register(ch.base + kTadr) = state.tadr;
    *Register(ch.base + kSadr) = sadr & 0x3FF0u;
    *Register(ch.base) = state.chcr;
    std::snprintf(text, sizeof text, "%llu quadwords to scratchpad", static_cast<unsigned long long>(outcome.quadwords));
    return text;
}

// Walks a started transfer and leaves the registers as it would; returns a note.
std::string Walk(const Channel& ch, u32 chcr) {
    Memory& m = GuestMemory();
    if (ch.base == 0x1000D000u || ch.base == 0x1000D400u) return Scratchpad(ch, chcr);
    DmaChannelState state{chcr, u32(m.Read(ch.base + kMadr, 4)), u32(m.Read(ch.base + kQwc, 4)),
                          u32(m.Read(ch.base + kTadr, 4)), u32(m.Read(ch.base + kAsr0, 4)),
                          u32(m.Read(ch.base + kAsr1, 4))};
    // A run of words at a time, each handed on directly.
    auto to_vif = [](VifStream& vif) {
        return [&vif](const u32* words, u32 count, u32 from) { vif.Words(words, count, from); };
    };
    DmaBlockSink sink = [](const u32*, u32, u32) {};
    if (ch.base == 0x10008000u) sink = to_vif(Vif0());
    if (ch.base == 0x10009000u) sink = to_vif(Vif1());
    if (ch.base == 0x1000A000u) sink = Path3;
    char text[160];
    try {
        Timed timed(Section::Dma);      // (VU1 and the GS, which it drives, timed apart)
        const DmaOutcome outcome = SendDmaBlocks(state, sink);
        std::snprintf(text, sizeof text, "%llu quadwords, %u tags%s",
                      static_cast<unsigned long long>(outcome.quadwords), outcome.tags,
                      outcome.ended ? "" : ", stopped: the chain does not end");
        if (ch.base == 0x1000A000u && (*Register(kGifStat) & kGifStatM3P)) {
            const u32 held = outcome.quadwords < kGifFifoDepth ? u32(outcome.quadwords) : kGifFifoDepth;
            *Register(kGifStat) = (*Register(kGifStat) & ~kGifStatFqc) | (held << kGifStatFqcShift);
        }
    } catch (const MemoryFault& fault) {
        std::snprintf(text, sizeof text, "stopped: read outside memory at 0x%08X", unsigned(fault.address));
    }
    *Register(ch.base + kMadr) = state.madr;
    *Register(ch.base + kQwc) = state.qwc;
    *Register(ch.base + kTadr) = state.tadr;
    *Register(ch.base + kAsr0) = state.asr0;
    *Register(ch.base + kAsr1) = state.asr1;
    *Register(ch.base) = state.chcr;
    return text;
}
constexpr u32 kVif1Fifo = 0x10005000u;

}  // namespace

namespace {
std::string g_frames_dir;
int g_frames_every = 30, g_frames_limit = 100, g_frames_written = 0;
}  // namespace

void SetFrameDumps(const std::string& dir, int every, int limit) {
    g_frames_dir = dir;
    g_frames_every = every > 0 ? every : 1;
    g_frames_limit = limit;
}

namespace {
long long g_trace_frame = -1;
}  // namespace

void SetGsTraceFrame(long long frame) { g_trace_frame = frame; }

namespace {
long long g_dump_frame = -1;
}  // namespace

void SetGsDumpFrame(long long frame) { g_dump_frame = frame; }

namespace {
u64 g_until_frame = 0;
bool g_until_reached = false;
}  // namespace

void SetUntilFrame(u64 frame) { g_until_frame = frame; }

namespace {
unsigned g_frame_times = 0;
}  // namespace

void SetFrameTimes(unsigned every) { g_frame_times = every; }

/* --frame-times: the frames since the last line, and how fast they went. */
void FrameTimes(u64 frame) {
    using Clock = std::chrono::steady_clock;
    static Clock::time_point last = Clock::now();
    static u64 last_frame = 0;
    if (!g_frame_times || frame < last_frame + g_frame_times) return;
    const Clock::time_point now = Clock::now();
    const double seconds = std::chrono::duration<double>(now - last).count();
    std::fprintf(stderr, "[frames] %llu-%llu: %.2f s, %.1f frames a second\n",
                 static_cast<unsigned long long>(last_frame), static_cast<unsigned long long>(frame), seconds,
                 seconds > 0 ? double(frame - last_frame) / seconds : 0.0);
    last = now;
    last_frame = frame;
}
bool UntilFrameReached() { return g_until_reached; }

void FrameDone(u64 frame) {
    FrameTimes(frame);
    // Time passes: unmasked, PATH3 sends what it still holds.
    if (!g_path3_masked && !g_path3_held.empty()) ReleasePath3(true);
    if (g_dump_frame >= 0 && (long long)frame == g_dump_frame) {
        gs::Gs& g = gs::TheGs();
        g.Finish();
        if (g.on_memory) g.on_memory(0, 16384, false);      // the GPU's drawing, back in memory
        const std::string path = (g_frames_dir.empty() ? std::string(".") : g_frames_dir) + "/gsmem_" +
                                 std::to_string(frame) + ".bin";
        if (std::FILE* f = std::fopen(path.c_str(), "wb")) {
            std::fwrite(g.memory.data(), 1, 4u << 20, f);
            std::fclose(f);
            std::fprintf(stderr, "[gs] local memory after frame %llu written to %s\n",
                         static_cast<unsigned long long>(frame), path.c_str());
        }
    }
    if (g_until_frame && frame >= g_until_frame && !g_until_reached) {
        g_until_reached = true;
        StopRequested() = true;
        ForEachContext([](lifted::Context& each) { each.budget = -1; });
    }
    if (g_trace_frame >= 0) {
        // From the end of the frame before to the end of six frames on: the
        // game draws a picture across several.
        if ((long long)frame + 1 == g_trace_frame) gs::TheGs().trace = true;
        else if ((long long)frame == g_trace_frame + 5) gs::TheGs().trace = false;
        if (gs::TheGs().trace) std::fprintf(stderr, "[gs] ---- frame %llu\n", static_cast<unsigned long long>(frame + 1));
    }
    DbcFrame();
    PresentFrame();
    if (WindowClosed()) throw GuestExit{0};             // the user closed the window
    const bool dump = !g_frames_dir.empty() && g_frames_written < g_frames_limit && frame % u64(g_frames_every) == 0;
    GpuShadowFrame(frame, dump ? g_frames_dir : std::string());
    if (!dump) return;
    char name[64];
    std::snprintf(name, sizeof name, "/frame_%06llu.bmp", static_cast<unsigned long long>(frame));
    if (gs::TheGs().WriteBmp(g_frames_dir + name)) ++g_frames_written;
    // VU1's memories beside the picture, for engine/tools/vudis.py.
    for (const auto& [file, span] : {std::make_pair("/vu1_micro.bin", Vu1().micro), std::make_pair("/vu1_data.bin", Vu1().data)})
        if (FILE* out = std::fopen((g_frames_dir + file).c_str(), "wb")) {
            std::fwrite(span.data(), 1, span.size(), out);
            std::fclose(out);
        }
}

void InstallDevices() {
    Memory& m = GuestMemory();
    for (const Channel& ch : kChannels) {
        const u32 chcr = ch.base, madr = ch.base + kMadr, qwc = ch.base + kQwc;
        m.OnWrite(chcr, [ch, madr, qwc, chcr](u32, u64 value) {
            if (!(value & 0x100)) return;                       // STR not set
            Memory& memory = GuestMemory();
            char text[256];
            std::snprintf(text, sizeof text,
                          "DMA %s started: CHCR=0x%08X MADR=0x%08X QWC=%u TADR=0x%08X (completed at once)",
                          ch.name, unsigned(value), unsigned(memory.Read(madr, 4)),
                          unsigned(memory.Read(qwc, 4)), unsigned(memory.Read(ch.base + kTadr, 4)));
            std::string note = text;
            if (gs::TheGs().trace && (ch.base == 0x10009000u || ch.base == 0x1000A000u))
                std::fprintf(stderr, "[dma] %s\n", text);
            if (ch.walked) note += ": " + Walk(ch, u32(value));
            Note(std::string("dma ") + ch.name, note);
            *Register(chcr) &= ~0x100u;                         // done
            SetDStat(d_stat | (1u << ch.status_bit));
            PostDmac(ch.status_bit);
        });
    }
    m.OnWrite(kDStat, [](u32, u64 value) {
        const u32 written = u32(value);
        SetDStat((d_stat & ~(written & 0xFFFFu)) ^ (written & 0xFFFF0000u));
    });
    Vif1().on_mask_path3 = [](bool masked) {
        if (gs::TheGs().trace)
            std::fprintf(stderr, "[vif1] MSKPATH3 %d, %zu PATH3 words held\n", int(masked), g_path3_held.size());
        u32* stat = reinterpret_cast<u32*>(GuestMemory().Translate(kGifStat, 4));
        // Unmasking lets PATH3 take its turn: the packet group it is in goes
        // through before a mask can stop it again.
        *stat = masked ? (*stat | kGifStatM3P) : (*stat & ~(kGifStatM3P | kGifStatFqc));
        g_path3_masked = masked;
        if (!masked) ReleasePath3(false);
    };
    Vif1().on_direct = [](u32 word) { gs::TheGs().Word(2, word); };
    Vif1().on_direct_block = [](const u32* words, u32 count, u32 from) { gs::TheGs().Words(2, words, count, from); };
    Vif1().on_trace = [](const char* what) {
        if (gs::TheGs().trace) std::fprintf(stderr, "[vif1] %s, %zu PATH3 words held\n", what, g_path3_held.size());
        // FLUSHA waits for PATH3 to finish: unmasked, all it holds goes.
        if (!g_path3_masked) ReleasePath3(true);
    };
    // VU1 behind VIF1; its XGKICKs are GIF PATH1.
    Vif1().vu = &Vu1();
    Vif1().double_buffered = true;
    Vif0().vu = &Vu0();
    Vu1().on_kick = [](u64 low, u64 high) { gs::TheGs().Qword(1, low, high); };
    // The GS's privileged registers: what is displayed. A 64-bit store reaches
    // both words; each passes the whole register on.
    for (u32 reg : {0x12000000u, 0x12000070u, 0x12000080u, 0x12000090u, 0x120000A0u, 0x120000E0u}) {
        for (u32 half : {0u, 4u}) {
            GuestMemory().OnWrite(reg + half, [reg](u32, u64) {
                gs::TheGs().SetPrivileged(reg, GuestMemory().Read(reg, 8));
            });
        }
    }
    // The FIFO takes whole quadwords; the store's last word hands all four over.
    m.OnWrite(kVif1Fifo + 12, [](u32, u64) {
        Memory& memory = GuestMemory();
        for (u32 i = 0; i < 4; ++i) Vif1().Word(u32(memory.Read(kVif1Fifo + 4 * i, 4)));
    });
    m.OnRead(kGsCsr, [](u32, u64 stored) {
        static u64 field = 0;
        field ^= 1u << 13;                          // FIELD alternates each frame
        return (stored & ~u64(1u << 13)) | field | (1u << 3);   // VSINT pending
    });
    InstallTimers();
}

}  // namespace ps2host
