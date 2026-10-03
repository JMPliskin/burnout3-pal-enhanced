/*
 * The SPU2 (spu2.h): voices decoded, enveloped and mixed a sample at a time,
 * loop flags, ENDX, and the IRQA interrupt.
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
constexpr Spu2::u32 kBlockSpan = kSamplesPerBlock << 12;    // a block in counter units
constexpr Spu2::u32 kVmixl = 0x18 - 0x13, kVmixr = 0x1A - 0x13;

Spu2::s32 Clamp16(Spu2::s32 v) { return std::min(32767, std::max(-32768, v)); }

// A volume register: bit 15 clear is a fixed 15-bit signed volume; set, a sweep,
// played at its maximum for now.
Spu2::s32 Volume(Spu2::u32 v) {
    if (v & 0x8000) return 0x7FFF;
    return Spu2::s32(Spu2::s16(Spu2::u16(v << 1)));
}

struct Rate {
    Spu2::u32 shift, step;
    bool decreasing, exponential;
};

}  // namespace

void Spu2::DecodeBlock(const u8* block, s32 out[28], s32* older, s32* old) {
    static const s32 kF0[5] = {0, 60, 115, 98, 122}, kF1[5] = {0, 0, -52, -55, -60};
    u32 shift = block[0] & 15;
    if (shift > 12) shift = 9;                      // as the hardware treats 13-15
    const u32 filter = std::min<u32>((block[0] >> 4) & 7, 4);
    for (u32 i = 0; i < kSamplesPerBlock; ++i) {
        const u32 nibble = (block[2 + i / 2] >> ((i & 1) * 4)) & 15;
        s32 s = s32(s16(u16(nibble << 12))) >> shift;
        s += (*old * kF0[filter] + *older * kF1[filter] + 32) >> 6;
        s = Clamp16(s);
        *older = *old;
        *old = s;
        out[i] = s;
    }
}

void Spu2::SetParam(u32 entry, u32 value) {
    Core& c = cores_[CoreOf(entry)];
    Voice& v = c.voices[VoiceOf(entry)];
    switch (entry >> 8) {
        case 0x00: v.voll = value; break;
        case 0x01: v.volr = value; break;
        case 0x02: v.pitch = value & 0x3FFF; break;
        case 0x03: v.adsr1 = value; break;
        case 0x04: v.adsr2 = value; break;
        case 0x09: c.mvoll = value; break;             // SD_P_MVOLL
        case 0x0A: c.mvolr = value; break;             // SD_P_MVOLR
        default: break;                         // effects, external input: not yet
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
        case 0x05: return u32(v.level);             // ENVX
        case 0x06: return u32(Volume(v.voll) & 0xFFFF);  // VOLXL, VOLXR: the current volume
        case 0x07: return u32(Volume(v.volr) & 0xFFFF);
        case 0x09: return c.mvoll;
        case 0x0A: return c.mvolr;
        default: return 0;
    }
}

void Spu2::Start(Voice& v) {
    v.nax = v.ssa & ~15u;
    v.counter = 0;
    v.older = v.old = v.previous = 0;
    v.playing = true;
    v.phase = Phase::Attack;
    v.level = 0;
    v.envelope_counter = 0;
    Decode(v);
}

void Spu2::Decode(Voice& v) {
    const u32 block = v.nax & kRamMask & ~15u;
    u8 bytes[kBlockBytes];
    for (u32 i = 0; i < kBlockBytes; ++i) bytes[i] = ram_[(block + i) & kRamMask];
    if (bytes[1] & 4) v.lsax = block;               // loop start: where a repeat returns
    DecodeBlock(bytes, v.samples, &v.older, &v.old);
}

void Spu2::SetSwitch(u32 entry, u32 value) {
    Core& c = cores_[CoreOf(entry)];
    const u32 which = (entry >> 8) - 0x13;
    if (which < 16) c.switches[which] = value & 0xFFFFFF;
    switch (entry >> 8) {
        case 0x15:                              // KON: start from SSA
            for (u32 i = 0; i < 24; ++i)
                if (value & (1u << i)) {
                    Start(c.voices[i]);
                    c.endx &= ~(1u << i);
                }
            break;
        case 0x16:                              // KOFF: release
            for (u32 i = 0; i < 24; ++i)
                if ((value & (1u << i)) && c.voices[i].phase != Phase::Off)
                    c.voices[i].phase = Phase::Release;
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

// The end of a voice's block: its flags decide where it goes next. A voice never
// stops fetching: key-off and an unrepeated end only silence it, and its
// address runs on through the loop - drivers time things by silent looping
// voices.
void Spu2::NextBlock(Core& c, u32 index, const std::function<void(u32)>& irq) {
    Voice& v = c.voices[index];
    const u32 block = v.nax & kRamMask & ~15u;
    const u8 flags = ram_[(block + 1) & kRamMask];
    CheckIrq(0, block, block + kBlockBytes, irq);
    if (flags & 1) {
        c.endx |= 1u << index;
        if (!(flags & 2)) {                         // an end without repeat: silence
            v.playing = false;
            v.phase = Phase::Off;
            v.level = 0;
        }
        v.nax = v.lsax;
    } else {
        v.nax = (block + kBlockBytes) & kRamMask;
    }
    Decode(v);
}

// One step of the ADSR envelope (the PS1/PS2 SPU's: a rate is a shift and a
// step; exponential attack slows above 0x6000, exponential decrease is
// proportional to the level).
void Spu2::Envelope(Voice& v) {
    Rate r{};
    switch (v.phase) {
        case Phase::Off: return;
        case Phase::Attack: {
            const u32 rate = (v.adsr1 >> 8) & 0x7F;
            r = {rate >> 2, rate & 3, false, (v.adsr1 & 0x8000) != 0};
            break;
        }
        case Phase::Decay:
            if (v.level <= std::min(0x7FFF, s32(((v.adsr1 & 15) + 1) * 0x800))) {
                v.phase = Phase::Sustain;           // already at the sustain level
                return;
            }
            r = {(v.adsr1 >> 4) & 15, 0, true, true};
            break;
        case Phase::Sustain: {
            const u32 rate = (v.adsr2 >> 6) & 0x7F;
            r = {rate >> 2, rate & 3, (v.adsr2 & 0x4000) != 0, (v.adsr2 & 0x8000) != 0};
            break;
        }
        case Phase::Release:
            r = {v.adsr2 & 0x1F, 0, true, (v.adsr2 & 0x20) != 0};
            break;
    }
    s32 step = r.decreasing ? -8 + s32(r.step) : 7 - s32(r.step);
    step = r.shift < 11 ? step << (11 - r.shift) : step;
    u32 increment = r.shift > 11 ? (r.shift - 11 >= 16 ? 0 : 0x8000u >> (r.shift - 11)) : 0x8000u;
    if (r.exponential) {
        if (r.decreasing) {
            step = s32((std::int64_t(step) * v.level) >> 15);
        } else if (v.level >= 0x6000) {
            if (r.shift < 10) step >>= 2;
            else if (r.shift >= 11) increment >>= 2;
            else { step >>= 1; increment >>= 1; }
        }
    }
    v.envelope_counter += increment;
    if (v.envelope_counter < 0x8000) return;
    v.envelope_counter = 0;
    v.level = std::min(0x7FFF, std::max(0, v.level + step));
    switch (v.phase) {
        case Phase::Attack:
            if (v.level >= 0x7FFF) v.phase = Phase::Decay;
            break;
        case Phase::Decay: {
            const s32 sustain = std::min(0x7FFF, s32(((v.adsr1 & 15) + 1) * 0x800));
            if (v.level <= sustain) v.phase = Phase::Sustain;
            break;
        }
        case Phase::Release:
            if (v.level == 0) { v.phase = Phase::Off; v.playing = false; }
            break;
        default: break;
    }
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
    // The voices, a sample at a time; at most a second's worth at once (a host
    // that stalled longer than that catches up the clock, not the sound).
    owed_ += samples;
    u32 count = u32(owed_);
    owed_ -= count;
    count = std::min<u32>(count, u32(kRate));
    mixed_.assign(std::size_t(count) * 2, 0);
    for (u32 n = 0; n < count; ++n) {
        s32 left = 0, right = 0;
        for (u32 core = 0; core < 2; ++core) {
            Core& c = cores_[core];
            s32 core_left = 0, core_right = 0;
            for (u32 i = 0; i < 24; ++i) {
                Voice& v = c.voices[i];
                if (!v.pitch) continue;
                if (v.phase != Phase::Off) {
                    const u32 index = v.counter >> 12;
                    const s32 current = v.samples[index];
                    const s32 sample = v.previous + ((current - v.previous) * s32(v.counter & 0xFFF) >> 12);
                    const s32 shaped = (sample * v.level) >> 15;
                    if (c.switches[kVmixl] & (1u << i)) core_left += (shaped * Volume(v.voll)) >> 15;
                    if (c.switches[kVmixr] & (1u << i)) core_right += (shaped * Volume(v.volr)) >> 15;
                    Envelope(v);
                }
                const u32 before_index = v.counter >> 12;
                s32 last = v.samples[before_index];
                bool crossed = false;
                v.counter += v.pitch;
                while (v.counter >= kBlockSpan) {
                    v.counter -= kBlockSpan;
                    last = v.samples[kSamplesPerBlock - 1];
                    NextBlock(c, i, irq);
                    crossed = true;
                }
                const u32 index = v.counter >> 12;
                if (crossed || index != before_index)
                    v.previous = index ? v.samples[index - 1] : last;
            }
            left += (core_left * Volume(c.mvoll)) >> 15;
            right += (core_right * Volume(c.mvolr)) >> 15;
        }
        mixed_[2 * n] = s16(Clamp16(left));
        mixed_[2 * n + 1] = s16(Clamp16(right));
    }
    if (output_ && count) output_(mixed_.data(), count);
}

}  // namespace iop
}  // namespace ps2host
