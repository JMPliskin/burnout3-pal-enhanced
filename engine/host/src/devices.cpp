/*
 * Device registers with hardware meaning, stage one: just enough for code that
 * starts a transfer and waits for it, or waits for a vertical blank, to go on.
 *
 * DMA: setting a channel's STR bit starts a transfer; here it completes at once
 *      - the bit clears and the channel's interrupt status is raised. What the
 *      transfer carried is noted, not yet acted on (no GS or IOP behind it).
 * GS:  CSR reports a vertical blank on every read, its field bit alternating, so
 *      a wait for the next frame ends.
 * Timers: a counter advances whenever it is read, so a busy-wait on it ends.
 *
 * Every register the game touches is counted for the report, so the next device
 * to implement is the one the game leans on.
 */

#include <cstdio>
#include <string>

#include "ps2host/memory.h"
#include "ps2host/run.h"

namespace ps2host {

namespace {

struct Channel {
    u32 base;
    const char* name;
    u32 status_bit;                 // its bit in D_STAT
};

const Channel kChannels[] = {
    {0x10008000u, "VIF0", 0}, {0x10009000u, "VIF1", 1}, {0x1000A000u, "GIF", 2},
    {0x1000B000u, "IPU_FROM", 3}, {0x1000B400u, "IPU_TO", 4}, {0x1000C000u, "SIF0", 5},
    {0x1000C400u, "SIF1", 6}, {0x1000C800u, "SIF2", 7}, {0x1000D000u, "SPR_FROM", 8},
    {0x1000D400u, "SPR_TO", 9},
};

constexpr u32 kDStat = 0x1000E010u;
constexpr u32 kGsCsr = 0x12001000u;
constexpr u32 kTimerCounts[] = {0x10000000u, 0x10000800u, 0x10001000u, 0x10001800u};

}  // namespace

void InstallDevices() {
    Memory& m = GuestMemory();
    for (const Channel& ch : kChannels) {
        const u32 chcr = ch.base, madr = ch.base + 0x10, qwc = ch.base + 0x20;
        m.OnWrite(chcr, [ch, madr, qwc, chcr](u32, u64 value) {
            if (!(value & 0x100)) return;                       // STR not set
            Memory& memory = GuestMemory();
            char text[128];
            std::snprintf(text, sizeof text,
                          "DMA %s started: CHCR=0x%08X MADR=0x%08X QWC=%u (completed at once)",
                          ch.name, unsigned(value), unsigned(memory.Read(madr, 4)),
                          unsigned(memory.Read(qwc, 4)));
            Note(std::string("dma ") + ch.name, text);
            memory.Write(chcr, value & ~u64(0x100), 4);         // done
            const u32 stat = u32(memory.Read(kDStat, 4));
            // D_STAT's status bits clear when written as 1, so set it directly.
            *reinterpret_cast<u32*>(memory.Translate(kDStat, 4)) = stat | (1u << ch.status_bit);
        });
    }
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
