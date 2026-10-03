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
 * Timers: a counter advances whenever it is read, so a busy-wait on it ends.
 * VIF1: quadwords stored to its FIFO are decoded as VIFcodes (vif.cpp);
 *      MSKPATH3 shows in GIF_STAT's M3P bit, which code that hands PATH3 over
 *      waits on.
 *
 * Every register the game touches is counted for the report, so the next device
 * to implement is the one the game leans on.
 */

#include <cstdio>
#include <string>

#include "ps2host/dma.h"
#include "ps2host/memory.h"
#include "ps2host/run.h"
#include "ps2host/vif.h"

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
    {0x1000D000u, "SPR_FROM", 8, false}, {0x1000D400u, "SPR_TO", 9, false},
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

u32* Register(u32 address) { return reinterpret_cast<u32*>(GuestMemory().Translate(address, 4)); }

// Walks a started transfer and leaves the registers as it would; returns a note.
std::string Walk(const Channel& ch, u32 chcr) {
    Memory& m = GuestMemory();
    DmaChannelState state{chcr, u32(m.Read(ch.base + kMadr, 4)), u32(m.Read(ch.base + kQwc, 4)),
                          u32(m.Read(ch.base + kTadr, 4)), u32(m.Read(ch.base + kAsr0, 4)),
                          u32(m.Read(ch.base + kAsr1, 4))};
    std::function<void(u32)> sink = [](u32) {};
    if (ch.base == 0x10009000u) sink = [](u32 word) { Vif1().Word(word); };
    char text[160];
    try {
        const DmaOutcome outcome = SendDma(state, sink);
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
constexpr u32 kTimerCounts[] = {0x10000000u, 0x10000800u, 0x10001000u, 0x10001800u};

}  // namespace

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
        u32* stat = reinterpret_cast<u32*>(GuestMemory().Translate(kGifStat, 4));
        // Unmasking lets PATH3 take its turn, which drains what the FIFO held.
        *stat = masked ? (*stat | kGifStatM3P) : (*stat & ~(kGifStatM3P | kGifStatFqc));
    };
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
    for (u32 counter : kTimerCounts) {
        m.OnRead(counter, [](u32 address, u64 stored) {
            const u64 next = (stored + 0x100) & 0xFFFF;
            *reinterpret_cast<u32*>(GuestMemory().Translate(address, 4)) = u32(next);
            return next;
        });
    }
}

}  // namespace ps2host
