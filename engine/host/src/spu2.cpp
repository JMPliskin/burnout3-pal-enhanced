/*
 * The SPU2 (spu2.h): voices advancing through SPU RAM in real time, loop
 * flags, ENDX, and the IRQA interrupt.
 */

#include "spu2.h"

#include <algorithm>
#include <cstring>

namespace ps2host {
namespace iop {

namespace {

constexpr Spu2::u32 kRamMask = (2u << 20) - 1;
constexpr double kRate = 48000.0;
constexpr Spu2::u32 kSamplesPerBlock = 28, kBlockBytes = 16;

}  // namespace

void Spu2::SetParam(u32 entry, u32 value) {
    Core& c = cores_[CoreOf(entry)];
    Voice& v = c.voices[VoiceOf(entry)];
    switch (entry >> 8) {
        case 0x00: v.voll = value; break;
        case 0x01: v.volr = value; break;
        case 0x02: v.pitch = value & 0x3FFF; break;
        case 0x03: v.adsr1 = value; break;
        case 0x04: v.adsr2 = value; break;
        default: break;                         // core volumes, mixing: not yet
    }
}

Spu2::u32 Spu2::GetParam(u32 entry) const {
    const Core& c = cores_[CoreOf(entry)];
    const Voice& v = c.voices[VoiceOf(entry)];
    switch (entry >> 8) {
        case 0x00: return v.voll;
        case 0x01: return v.volr;
        case 0x02: return v.pitch;
        case 0x03: return v.adsr1;
        case 0x04: return v.adsr2;
        case 0x05: return v.playing ? 0x7FFF : 0;   // ENVX: no envelope yet
        default: return 0;
    }
}

void Spu2::SetSwitch(u32 entry, u32 value) {
    Core& c = cores_[CoreOf(entry)];
    const u32 which = (entry >> 8) - 0x13;
    if (which < 16) c.switches[which] = value & 0xFFFFFF;
    switch (entry >> 8) {
        case 0x15:                              // KON: start from SSA
            for (u32 i = 0; i < 24; ++i)
                if (value & (1u << i)) {
                    Voice& v = c.voices[i];
                    v.nax = v.ssa & ~15u;
                    v.position = 0;
                    v.playing = true;
                    c.endx &= ~(1u << i);
                }
            break;
        case 0x16:                              // KOFF: release - stopped here
            for (u32 i = 0; i < 24; ++i)
                if (value & (1u << i)) c.voices[i].playing = false;
            break;
        case 0x17: c.endx = value & 0xFFFFFF; break;
        default: break;
    }
}

Spu2::u32 Spu2::GetSwitch(u32 entry) const {
    const Core& c = cores_[CoreOf(entry)];
    if ((entry >> 8) == 0x17) return c.endx;
    const u32 which = (entry >> 8) - 0x13;
    return which < 16 ? c.switches[which] : 0;
}

void Spu2::SetAddr(u32 entry, u32 value) {
    Core& c = cores_[CoreOf(entry)];
    Voice& v = c.voices[VoiceOf(entry)];
    value &= kRamMask;
    switch (entry >> 8) {
        case 0x1C: c.esa = value; break;
        case 0x1D: c.eea = value; break;
        case 0x1E: c.tsa = value; break;
        case 0x1F: c.irqa = value; break;
        case 0x20: v.ssa = value; break;
        case 0x21: v.lsax = value; break;
        case 0x22: v.nax = value; break;
        default: break;
    }
}

Spu2::u32 Spu2::GetAddr(u32 entry) const {
    const Core& c = cores_[CoreOf(entry)];
    const Voice& v = c.voices[VoiceOf(entry)];
    switch (entry >> 8) {
        case 0x1C: return c.esa;
        case 0x1D: return c.eea;
        case 0x1E: return c.tsa;
        case 0x1F: return c.irqa;
        case 0x20: return v.ssa;
        case 0x21: return v.lsax;
        case 0x22: return v.nax;
        default: return 0;
    }
}

void Spu2::SetCoreAttr(u32 entry, u32 value) {
    Core& c = cores_[CoreOf(entry)];
    switch (entry & ~1u) {
        case 0x4: c.irq_enabled = value != 0; break;    // SD_CORE_IRQ_ENABLE
        default: break;
    }
    c.attr = (c.attr & ~(1u << (entry & ~1u))) | ((value ? 1u : 0u) << (entry & ~1u));
}

Spu2::u32 Spu2::GetCoreAttr(u32 entry) const {
    const Core& c = cores_[CoreOf(entry)];
    if ((entry & ~1u) == 0x4) return c.irq_enabled ? 1 : 0;
    return (c.attr >> (entry & ~1u)) & 1;
}

void Spu2::CheckIrq(u32 core_hint, u32 from, u32 to, const std::function<void(u32)>& irq) {
    // IRQA is shared memory: either core with interrupts on sees it.
    (void)core_hint;
    for (u32 core = 0; core < 2; ++core) {
        Core& c = cores_[core];
        if (!c.irq_enabled) continue;
        if (c.irqa >= from && c.irqa < to) {
            c.irq_enabled = false;              // fires once; the driver re-arms it
            irq(core);
        }
    }
}

void Spu2::Write(u32 address, const u8* data, u32 size) {
    for (u32 i = 0; i < size; ++i) ram_[(address + i) & kRamMask] = data[i];
    pending_irqs_.push_back(address & kRamMask);
    pending_irqs_.push_back((address + size) & kRamMask);
}

void Spu2::Read(u32 address, u8* data, u32 size) const {
    for (u32 i = 0; i < size; ++i) data[i] = ram_[(address + i) & kRamMask];
}

void Spu2::Advance(u64 now_us, const std::function<void(u32 core)>& irq) {
    // Transfers that touched IRQA.
    for (std::size_t i = 0; i + 1 < pending_irqs_.size(); i += 2)
        CheckIrq(0, pending_irqs_[i], pending_irqs_[i + 1] > pending_irqs_[i] ? pending_irqs_[i + 1] : pending_irqs_[i] + 1, irq);
    pending_irqs_.clear();
    if (!last_us_) last_us_ = now_us;
    const double samples = double(now_us - last_us_) * kRate / 1e6;
    last_us_ = now_us;
    if (samples <= 0) return;
    // The SPU2's memory streams: SPU RAM 0x0000-0x27FF is rings of 0x200 bytes
    // (256 16-bit samples) that the hardware writes (outputs) and reads (block
    // transfer input) one sample per 48 kHz tick, all at the same position. An
    // IRQA inside them fires each time that position passes it - every 256
    // samples, the clock drivers stream by.
    const u64 before = u64(clock_), after = u64(clock_ + samples);
    clock_ += samples;
    for (u32 core = 0; core < 2; ++core) {
        Core& c = cores_[core];
        if (!c.irq_enabled || c.irqa >= 0x2800) continue;
        const u64 offset = (c.irqa & 0x1FF) >> 1;
        // Is there a t in (before, after] with t % 256 == offset?
        const u64 next = before - before % 256 + offset + (before % 256 >= offset ? 256 : 0);
        if (next <= after) {
            c.irq_enabled = false;
            irq(core);
        }
    }
    for (u32 core = 0; core < 2; ++core) {
        Core& c = cores_[core];
        for (u32 i = 0; i < 24; ++i) {
            Voice& v = c.voices[i];
            if (!v.playing || !v.pitch) continue;
            v.position += samples * double(v.pitch) / 4096.0;
            // Cross block boundaries, at most a second's worth at a time.
            for (int guard = 0; v.position >= kSamplesPerBlock && guard < 4096; ++guard) {
                v.position -= kSamplesPerBlock;
                const u32 block = v.nax & kRamMask & ~15u;
                const u8 flags = ram_[(block + 1) & kRamMask];
                if (flags & 4) v.lsax = block;
                CheckIrq(core, block, block + kBlockBytes, irq);
                if (flags & 1) {
                    c.endx |= 1u << i;
                    if (flags & 2) {
                        v.nax = v.lsax;
                    } else {
                        v.playing = false;
                        v.position = 0;
                        break;
                    }
                } else {
                    v.nax = (block + kBlockBytes) & kRamMask;
                }
            }
        }
    }
}

}  // namespace iop
}  // namespace ps2host
