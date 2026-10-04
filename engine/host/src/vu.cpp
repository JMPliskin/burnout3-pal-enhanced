/*
 * The vector units in micro mode (vu.h).
 *
 * Instruction fields (both halves): dest x y z w in bits 24-21, ft/it 20-16,
 * fs/is 15-11, fd/id 10-6. The upper's flags: I 31 (the lower word is a float
 * for the I register), E 30 (end), M 29, D 28, T 27. Instruction addresses are
 * in bytes (pairs of 8); data addresses in quadwords.
 */

#include "ps2host/vu.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

#include "ps2host/memory.h"
#include "ps2host/profile.h"
#include "ps2rt/generated.h"
#include "ps2rt/vu_math.h"


namespace ps2host {

namespace {

constexpr Vu::u32 kNone = ~0u;
constexpr int kRegR = 20, kRegI = 21, kRegQ = 22;
constexpr Vu::u32 kCop2 = 0x4A000000u;       // a macro-mode COP2 operation

Vu::u32 Field(Vu::u32 word, int low, int count) { return (word >> low) & ((1u << count) - 1); }
std::int32_t Imm11(Vu::u32 word) { return std::int32_t(word << 21) >> 21; }

// The vector fields a pair reads, for its stalls: (register, lanes as dest bits).
struct Reads {
    int n = 0;
    Vu::u32 reg[6], lanes[6];
    void Add(Vu::u32 r, Vu::u32 l) {
        if (r && l) { reg[n] = r; lanes[n] = l; ++n; }
    }
};

Vu::u32 LaneBit(Vu::u32 lane) { return 8u >> lane; }    // x 0 .. w 3, as dest bits

void UpperReads(Vu::u32 word, Reads& r) {
    const Vu::u32 funct = word & 0x3F, dest = Field(word, 21, 4);
    const Vu::u32 fs = Field(word, 11, 5), ft = Field(word, 16, 5);
    if (funct < 0x1C) { r.Add(fs, dest); r.Add(ft, LaneBit(funct & 3)); return; }   // the broadcasts
    if (funct < 0x28) { r.Add(fs, dest); return; }                                   // Q and I forms
    if (funct < 0x30) {
        const Vu::u32 lanes = funct == 0x2E ? 0xE : dest;                             // OPMSUB: xyz
        r.Add(fs, lanes);
        r.Add(ft, lanes);
        return;
    }
    if (funct < 0x3C) return;
    const Vu::u32 index = (word & 3) | ((word >> 4) & 0x7C);
    if (index == 0x2F) return;                                                       // NOP
    if (index == 0x1F) { r.Add(fs, 0xE); r.Add(ft, 0x1); return; }                   // CLIP
    if (index < 0x10 || (index >= 0x18 && index < 0x1C)) {                           // the broadcasts
        r.Add(fs, dest);
        r.Add(ft, LaneBit(index & 3));
        return;
    }
    if (index < 0x28) { r.Add(fs, dest); return; }      // ITOF FTOI ABS, Q and I forms
    const Vu::u32 lanes = index == 0x2E ? 0xE : dest;   // OPMULA: xyz
    r.Add(fs, lanes);
    r.Add(ft, lanes);
}

void LowerReads(Vu::u32 word, Reads& r) {
    const Vu::u32 op = word >> 25, dest = Field(word, 21, 4);
    const Vu::u32 is = Field(word, 11, 5), it = Field(word, 16, 5);
    const Vu::u32 fsf = LaneBit(Field(word, 21, 2)), ftf = LaneBit(Field(word, 23, 2));
    if (op == 0x01) { r.Add(is, dest); return; }        // SQ
    if (op != 0x40 || (word & 0x3F) < 0x3C) return;
    const Vu::u32 index = (word & 3) | ((word >> 4) & 0x7C);
    switch (index) {
        case 0x30: r.Add(is, dest); return;                                          // MOVE
        case 0x31: r.Add(is, ((dest << 3) | (dest >> 1)) & 0xF); return;             // MR32: x from y ..
        case 0x35: case 0x37: r.Add(is, dest); return;                               // SQI SQD
        case 0x38: case 0x3A: r.Add(is, fsf); r.Add(it, ftf); return;                // DIV RSQRT
        case 0x39: r.Add(it, ftf); return;                                           // SQRT
        case 0x3C: case 0x42: case 0x43: r.Add(is, fsf); return;                     // MTIR RINIT RXOR
        case 0x70: case 0x71: case 0x72: case 0x73: r.Add(is, 0xE); return;         // ESADD .. ERLENG
        case 0x74: r.Add(is, 0xC); return;                                           // EATANxy
        case 0x75: r.Add(is, 0xA); return;                                           // EATANxz
        case 0x76: r.Add(is, 0xF); return;                                           // ESUM
        case 0x78: case 0x79: case 0x7A: case 0x7C: case 0x7D: case 0x7E:
            r.Add(is, fsf);
            return;
        default: return;
    }
}

bool IsEfu(Vu::u32 word) {
    if ((word >> 25) != 0x40 || (word & 0x3F) < 0x3C) return false;
    const Vu::u32 index = (word & 3) | ((word >> 4) & 0x7C);
    return index >= 0x70 && index <= 0x7E && index != 0x77 && index != 0x7B;
}

bool IsDivide(Vu::u32 word) {
    if ((word >> 25) != 0x40 || (word & 0x3F) < 0x3C) return false;
    const Vu::u32 index = (word & 3) | ((word >> 4) & 0x7C);
    return index >= 0x38 && index <= 0x3A;
}

bool IsWait(Vu::u32 word, Vu::u32 which) {             // 0x3B WAITQ, 0x7B WAITP
    return (word >> 25) == 0x40 && (word & 0x3F) >= 0x3C && ((word & 3) | ((word >> 4) & 0x7C)) == which;
}

constexpr int kFmacLatency = 4;

// A pair, decoded for its stalls once: what it reads and whether it waits on
// the divider or the EFU. Kept per address and checked against the words
// there (an MPG may have replaced them).
struct DecodedPair {
    Vu::u32 lower = 0, upper = 0;
    bool valid = false;
    bool immediate = false, q_waits = false, p_waits = false;
    Reads reads;
    bool has_fmac = false;          // the upper is an FMAC operation, decoded
    lifted::FmacOp fmac;
};

DecodedPair& Decoded(std::vector<DecodedPair>& cache, Vu::u32 pc, Vu::u32 lower, Vu::u32 upper) {
    DecodedPair& d = cache[pc / 8];
    if (d.valid && d.lower == lower && d.upper == upper) return d;
    d = DecodedPair{};
    d.lower = lower;
    d.upper = upper;
    d.valid = true;
    d.immediate = (upper & 0x80000000u) != 0;          // I bit: the lower is I's value
    UpperReads(upper, d.reads);
    d.has_fmac = lifted::DecodeFmac(kCop2 | (upper & 0x01FFFFFF), &d.fmac);
    if (!d.immediate) {
        LowerReads(lower, d.reads);
        d.q_waits = IsDivide(lower) || IsWait(lower, 0x3B);
        d.p_waits = IsEfu(lower) || IsWait(lower, 0x7B);
    }
    return d;
}

}  // namespace

struct Vu::Pending {
    int vf = -1;                    // a vector register the lower writes
    u32 lanes = 0, value[4] = {};
    bool vc = false;                // the lower changed control registers
    u32 vc_after[32];               // ...to these (only when vc: a pair is not worth zeroing 128 bytes)
    bool p = false;
    u32 p_value = 0;
    int p_latency = 0;              // the EFU op's, in cycles
    int q_latency = 0;              // DIV, SQRT, RSQRT's
};

Vu::Vu(int index)
    : own_micro_(index ? 16384 : 4096), own_data_(index ? 16384 : 4096), data_mask_(index ? 0x3FF : 0xFF) {
    micro = {own_micro_.data(), own_micro_.size()};
    data = {own_data_.data(), own_data_.size()};
    regs.vf[0][3] = 0x3F800000u;    // vf0 = (0, 0, 0, 1)
}

void Vu::Attach(std::uint8_t* micro_memory, std::uint8_t* data_memory) {
    if (micro_memory) micro.p = micro_memory;
    if (data_memory) data.p = data_memory;
}

Vu::u32* Vu::Quad(u32 address) {
    return reinterpret_cast<u32*>(data.data() + std::size_t(address & data_mask_) * 16);
}

void Vu::Kick(u32 address) {
    ++kicks_;
    for (int packets = 0; packets < 4096; ++packets) {
        const u32* tag = Quad(address);
        const u64 low = u64(tag[0]) | u64(tag[1]) << 32, high = u64(tag[2]) | u64(tag[3]) << 32;
        const u32 nloop = u32(low & 0x7FFF), eop = u32(low >> 15) & 1, flg = u32(low >> 58) & 3;
        const u32 nreg = u32(low >> 60) ? u32(low >> 60) : 16;
        const u32 qwords = flg == 0 ? nloop * nreg : flg == 1 ? (nloop * nreg + 1) / 2 : nloop;
        if (on_kick) on_kick(low, high);
        address = (address + 1) & data_mask_;
        for (u32 i = 0; i < qwords; ++i) {
            const u32* q = Quad(address);
            if (on_kick) on_kick(u64(q[0]) | u64(q[1]) << 32, u64(q[2]) | u64(q[3]) << 32);
            address = (address + 1) & data_mask_;
        }
        if (eop) return;
    }
}

// The upper instruction, run on the registers; says which vector register and
// fields it wrote (if any), for the lower's writes to give way to.
void Vu::Upper(u32 word, int* written_reg, u32* written_lanes, bool* set_flags, const lifted::FmacOp* fmac) {
    *written_reg = -1;
    *written_lanes = 0;
    *set_flags = false;
    const u32 funct = word & 0x3F, dest = Field(word, 21, 4);
    const u32 ft = Field(word, 16, 5), fd = Field(word, 6, 5);
    bool flags = false;
    int target = -1;
    if (funct < 0x3C) {
        if (funct >= 0x30) return;                          // not upper operations
        target = int(fd);
        // MAX and MINI leave the flags alone.
        flags = !((funct >= 0x10 && funct <= 0x17) || funct == 0x1D || funct == 0x1F ||
                  funct == 0x2B || funct == 0x2F);
    } else {
        const u32 index = (word & 3) | ((word >> 4) & 0x7C);
        if (index == 0x1F) {                                // CLIP: fs.xyz against |ft.w|
            const u32* v = regs.vf[Field(word, 11, 5)];
            const double w = std::fabs(lifted::Value(regs.vf[ft][3]));
            u32 judgement = 0;
            for (int lane = 0; lane < 3; ++lane) {
                const double x = lifted::Value(v[lane]);
                if (x > w) judgement |= 1u << (2 * lane);
                if (x < -w) judgement |= 2u << (2 * lane);
            }
            clip_latest_ = ((clip_latest_ << 6) | judgement) & 0xFFFFFF;
            *set_flags = true;
            return;
        }
        if (index == 0x2F) return;                          // NOP
        if ((index >= 0x10 && index <= 0x17) || index == 0x1D) target = int(ft);   // ITOF, FTOI, ABS
        else flags = true;                                  // the accumulator forms
    }
    if (fmac) lifted::RunFmac(regs, *fmac);
    else lifted::Cop2(regs, kCop2 | (word & 0x01FFFFFF));
    if (target > 0) {
        *written_reg = target;
        *written_lanes = dest;
    }
    if (flags) {
        // MAC: zero (bits 3-0) and sign (7-4) of each written field, x highest.
        const u32* out = target >= 0 ? regs.vf[target] : regs.vacc;
        u32 m = 0;
        for (int lane = 0; lane < 4; ++lane) {
            if (!(dest & (8u >> lane))) continue;
            const u32 bits = lifted::Clean(out[lane]);
            if ((bits & 0x7FFFFFFFu) == 0) m |= 8u >> lane;
            if (bits & 0x80000000u) m |= 0x80u >> lane;
        }
        mac_latest_ = m;
        status_latest_ = (status_latest_ & ~0x3u) | ((m & 0xF) ? 1u : 0u) | ((m & 0xF0) ? 2u : 0u);
        status_latest_ |= (status_latest_ & 3u) << 6;       // the sticky copies
        *set_flags = true;
    }
}

void Vu::Lower(u32 word, u32 pc, Pending& out, u32* branch) {
    const u32 op = word >> 25, dest = Field(word, 21, 4);
    const u32 it = Field(word, 16, 5), is = Field(word, 11, 5);
    u32* vi = regs.vc;
    auto setvi = [&](u32 r, u32 v) { if (r > 0 && r < 16) vi[r] = v & 0xFFFF; };
    auto load = [&](u32 address) {
        const u32* q = Quad(address);
        out.vf = int(it);
        out.lanes = dest;
        std::memcpy(out.value, q, 16);
    };
    auto store = [&](u32 address, const u32* v) {
        u32* q = Quad(address);
        for (int lane = 0; lane < 4; ++lane)
            if (dest & (8u >> lane)) q[lane] = v[lane];
    };
    auto through_cop2 = [&]() {
        // Run it as macro mode would, then hold its writes back until the upper
        // has read its operands.
        u32 before_vc[32], before_vf[4];
        std::memcpy(before_vc, regs.vc, sizeof before_vc);
        std::memcpy(before_vf, regs.vf[it], sizeof before_vf);
        lifted::Cop2(regs, kCop2 | (word & 0x01FFFFFF));
        if (std::memcmp(before_vf, regs.vf[it], sizeof before_vf) != 0) {
            out.vf = int(it);
            out.lanes = dest;
            std::memcpy(out.value, regs.vf[it], 16);
            std::memcpy(regs.vf[it], before_vf, sizeof before_vf);
        }
        if (std::memcmp(before_vc, regs.vc, sizeof before_vc) != 0) {
            out.vc = true;
            std::memcpy(out.vc_after, regs.vc, sizeof out.vc_after);
            std::memcpy(regs.vc, before_vc, sizeof before_vc);
        }
    };
    auto efu = [&](double value) {
        out.p = true;
        out.p_value = lifted::Result(value);
        static const int kLatency[16] = {11, 18, 18, 24, 54, 54, 12, 0, 12, 18, 12, 0, 29, 54, 44, 0};
        out.p_latency = kLatency[(((word & 3) | ((word >> 4) & 0x7C)) - 0x70) & 15];
    };
    const u32 target = (pc + 8 + u32(Imm11(word) * 8)) & u32(micro.size() - 1);
    const std::int16_t a = std::int16_t(vi[is]), b = std::int16_t(vi[it]);
    switch (op) {
        case 0x40: {
            const u32 funct = word & 0x3F;
            if (funct >= 0x30 && funct <= 0x35) {
                // IADD ISUB IADDI IAND IOR, as the macro-mode unit has them: they
                // write vi1-vi15, which no upper reads, so they need not wait.
                const u32 id = Field(word, 6, 5);
                if (funct == 0x32) {
                    const std::int32_t imm = (id & 0x10) ? std::int32_t(id) - 32 : std::int32_t(id);
                    setvi(it, u32(std::int32_t(vi[is]) + imm));
                    return;
                }
                const u32 x = vi[is], y = vi[it];
                setvi(id, funct == 0x30 ? x + y : funct == 0x31 ? x - y : funct == 0x34 ? (x & y) : (x | y));
                return;
            }
            if (funct < 0x3C) { through_cop2(); return; }
            const u32 index = (word & 3) | ((word >> 4) & 0x7C);
            const u32 fsf = Field(word, 21, 2);
            const u32* v = regs.vf[is];
            switch (index) {
                case 0x34: load(vi[is]); setvi(is, vi[is] + 1); return;                 // LQI
                case 0x35: store(vi[it], regs.vf[is]); setvi(it, vi[it] + 1); return;   // SQI
                case 0x36: setvi(is, vi[is] - 1); load(vi[is]); return;                 // LQD
                case 0x37: setvi(it, vi[it] - 1); store(vi[it], regs.vf[is]); return;   // SQD
                case 0x3E: {                                                            // ILWR
                    const u32* q = Quad(vi[is]);
                    for (int lane = 0; lane < 4; ++lane)
                        if (dest & (8u >> lane)) { setvi(it, q[lane]); break; }
                    return;
                }
                case 0x3F: {                                                            // ISWR
                    u32* q = Quad(vi[is]);
                    for (int lane = 0; lane < 4; ++lane)
                        if (dest & (8u >> lane)) q[lane] = vi[it] & 0xFFFF;
                    return;
                }
                case 0x64: {                                                            // MFP
                    out.vf = int(it);
                    out.lanes = dest;
                    for (u32& x : out.value) x = p;
                    return;
                }
                case 0x68: setvi(it, top); return;                                      // XTOP
                case 0x69: setvi(it, itop); return;                                     // XITOP
                case 0x6C: Kick(vi[is]); return;                                        // XGKICK
                case 0x70: case 0x71: case 0x72: case 0x73: {   // ESADD ERSADD ELENG ERLENG
                    const double x = lifted::Value(v[0]), y = lifted::Value(v[1]), z = lifted::Value(v[2]);
                    const double sum = x * x + y * y + z * z;
                    efu(index == 0x70 ? sum : index == 0x71 ? 1.0 / sum
                        : index == 0x72 ? std::sqrt(sum) : 1.0 / std::sqrt(sum));
                    return;
                }
                case 0x74: efu(std::atan2(lifted::Value(v[1]), lifted::Value(v[0]))); return;  // EATANxy
                case 0x75: efu(std::atan2(lifted::Value(v[2]), lifted::Value(v[0]))); return;  // EATANxz
                case 0x76: efu(lifted::Value(v[0]) + lifted::Value(v[1]) + lifted::Value(v[2]) +
                               lifted::Value(v[3])); return;                                   // ESUM
                case 0x78: efu(std::sqrt(std::fabs(lifted::Value(v[fsf])))); return;          // ESQRT
                case 0x79: efu(1.0 / std::sqrt(std::fabs(lifted::Value(v[fsf])))); return;    // ERSQRT
                case 0x7A: efu(1.0 / lifted::Value(v[fsf])); return;                          // ERCPR
                case 0x7B: return;                                                            // WAITP: a stall (Run)
                case 0x7C: efu(std::sin(lifted::Value(v[fsf]))); return;                      // ESIN
                case 0x7D: efu(std::atan(lifted::Value(v[fsf]))); return;                     // EATAN
                case 0x7E: efu(std::exp(-lifted::Value(v[fsf]))); return;                     // EEXP
                case 0x38: case 0x39: case 0x3A:                                            // DIV SQRT RSQRT
                    through_cop2();
                    out.q_latency = index == 0x3A ? 13 : 7;
                    return;
                case 0x3B: return;                                                            // WAITQ: a stall (Run)
                default: through_cop2(); return;    // MOVE MR32 MTIR MFIR R*
            }
        }
        case 0x00: load(vi[is] + u32(Imm11(word))); return;                     // LQ
        case 0x01: store(vi[it] + u32(Imm11(word)), regs.vf[is]); return;       // SQ
        case 0x04: {                                                             // ILW
            const u32* q = Quad(vi[is] + u32(Imm11(word)));
            for (int lane = 0; lane < 4; ++lane)
                if (dest & (8u >> lane)) { setvi(it, q[lane]); break; }
            return;
        }
        case 0x05: {                                                             // ISW
            u32* q = Quad(vi[is] + u32(Imm11(word)));
            for (int lane = 0; lane < 4; ++lane)
                if (dest & (8u >> lane)) q[lane] = vi[it] & 0xFFFF;
            return;
        }
        case 0x08: setvi(it, vi[is] + ((Field(word, 21, 4) << 11) | Field(word, 0, 11))); return;  // IADDIU
        case 0x09: setvi(it, vi[is] - ((Field(word, 21, 4) << 11) | Field(word, 0, 11))); return;  // ISUBIU
        case 0x10: setvi(1, (clip & 0xFFFFFF) == (word & 0xFFFFFF)); return;                        // FCEQ
        case 0x11: clip = clip_latest_ = word & 0xFFFFFF; return;                                   // FCSET
        case 0x12: setvi(1, (clip & word & 0xFFFFFF) != 0); return;                                 // FCAND
        case 0x13: setvi(1, ((clip | word) & 0xFFFFFF) == 0xFFFFFF); return;                        // FCOR
        case 0x14: case 0x15: case 0x16: case 0x17: {                           // FSEQ FSSET FSAND FSOR
            const u32 imm = (Field(word, 21, 1) << 11) | Field(word, 0, 11);
            if (op == 0x14) setvi(it, (status & 0xFFF) == imm);
            else if (op == 0x15) {
                status = (status & 0x3F) | (imm & 0xFC0);
                status_latest_ = (status_latest_ & 0x3F) | (imm & 0xFC0);
            }
            else if (op == 0x16) setvi(it, status & imm);
            else setvi(it, ((status | imm) & 0xFFF) == 0xFFF);
            return;
        }
        case 0x18: setvi(it, (vi[is] & 0xFFFF) == (mac & 0xFFFF)); return;     // FMEQ
        case 0x1A: setvi(it, vi[is] & mac); return;                              // FMAND
        case 0x1B: setvi(it, vi[is] | mac); return;                              // FMOR
        case 0x1C: setvi(it, clip & 0xFFF); return;                              // FCGET
        case 0x20: *branch = target; return;                                     // B
        case 0x21: setvi(it, (pc + 16) / 8); *branch = target; return;           // BAL
        case 0x24: *branch = (vi[is] * 8) & u32(micro.size() - 1); return;       // JR
        case 0x25: {                                                             // JALR
            const u32 to = (vi[is] * 8) & u32(micro.size() - 1);
            setvi(it, (pc + 16) / 8);
            *branch = to;
            return;
        }
        case 0x28: if (a == b) *branch = target; return;                         // IBEQ
        case 0x29: if (a != b) *branch = target; return;                         // IBNE
        case 0x2C: if (a < 0) *branch = target; return;                          // IBLTZ
        case 0x2D: if (a > 0) *branch = target; return;                          // IBGTZ
        case 0x2E: if (a <= 0) *branch = target; return;                         // IBLEZ
        case 0x2F: if (a >= 0) *branch = target; return;                         // IBGEZ
        default: return;
    }
}

void Vu::Land() {
    if (!q_busy_ && !p_busy_ && !flag_count_) return;
    if (q_busy_ && q_ready_ <= cycle_) { regs.vc[kRegQ] = q_next_; q_busy_ = false; }
    if (p_busy_ && p_ready_ <= cycle_) { p = p_next_; p_busy_ = false; }
    int kept = 0;
    for (int i = 0; i < flag_count_; ++i) {
        const FlagWrite& f = flag_queue_[i];
        if (f.ready <= cycle_) {
            if (f.sets_mac) { mac = f.mac; status = (status & ~0x3u) | (f.status & 0x3u); status |= f.status & 0xC0u; }
            if (f.sets_clip) clip = f.clip;
        } else {
            flag_queue_[kept++] = f;
        }
    }
    flag_count_ = kept;
}

void Vu::LandAll() {
    cycle_ += 64;                   // time passes after the program: what was in flight lands
    Land();
}

Vu::u64 Vu::Run(u32 pc, u64 limit) {
    Timed timed(data_mask_ == 0x3FF ? Section::Vu1 : Section::Vu0);
    u64 count = 0;
    u32 delayed = kNone;
    bool stopping = false;
    pc &= u32(micro.size() - 1) & ~7u;
    ++starts_[pc];
    // One cache a unit size; an entry stands only while its words are there.
    static std::vector<DecodedPair> caches[2];
    std::vector<DecodedPair>& decoded_ = caches[data_mask_ == 0x3FF ? 1 : 0];
    if (decoded_.size() != micro.size() / 8) decoded_.assign(micro.size() / 8, DecodedPair{});
    while (count < limit) {
        u32 lower, upper;
        std::memcpy(&lower, micro.data() + pc, 4);
        std::memcpy(&upper, micro.data() + pc + 4, 4);
        const DecodedPair& d = Decoded(decoded_, pc, lower, upper);
        const bool immediate = d.immediate;

        // Stalls: operands still in the pipeline, a busy divider or EFU, the waits.
        const Reads& reads = d.reads;
        u64 issue = cycle_;
        for (int i = 0; i < reads.n; ++i)
            for (int lane = 0; lane < 4; ++lane)
                if (reads.lanes[i] & (8u >> lane)) issue = std::max(issue, vf_ready_[reads.reg[i]][lane]);
        if (q_busy_ && d.q_waits) issue = std::max(issue, q_ready_);
        if (p_busy_ && d.p_waits) issue = std::max(issue, p_ready_);
        cycle_ = issue;
        Land();

        Pending pending;
        u32 branch = kNone;
        if (!immediate) Lower(lower, pc, pending, &branch);
        int written = -1;
        u32 lanes = 0;
        bool set_flags = false;
        Upper(upper, &written, &lanes, &set_flags, d.has_fmac ? &d.fmac : nullptr);
        // LOI: the upper beside it read the I from before.
        if (immediate) regs.vc[kRegI] = lower;
        if (pending.vf > 0) {
            for (int lane = 0; lane < 4; ++lane) {
                const u32 bit = 8u >> lane;
                if ((pending.lanes & bit) && !(written == pending.vf && (lanes & bit))) {
                    regs.vf[pending.vf][lane] = pending.value[lane];
                    vf_ready_[pending.vf][lane] = cycle_ + kFmacLatency;
                }
            }
        }
        if (written > 0)
            for (int lane = 0; lane < 4; ++lane)
                if (lanes & (8u >> lane)) vf_ready_[written][lane] = cycle_ + kFmacLatency;
        if (set_flags && flag_count_ < 8)
            flag_queue_[flag_count_++] = {cycle_ + kFmacLatency, mac_latest_, status_latest_, clip_latest_,
                                          (upper & 0x7FF) != 0x1FF, (upper & 0x7FF) == 0x1FF};
        if (pending.q_latency) {
            // (A quotient equal to the Q before changes no register: it is that Q.)
            q_next_ = pending.vc ? pending.vc_after[kRegQ] : regs.vc[kRegQ];
            q_ready_ = cycle_ + u64(pending.q_latency);
            q_busy_ = true;
            if (pending.vc) pending.vc_after[kRegQ] = regs.vc[kRegQ];
        }
        if (pending.vc) std::memcpy(regs.vc, pending.vc_after, sizeof pending.vc_after);
        if (pending.p) {
            p_next_ = pending.p_value;
            p_ready_ = cycle_ + u64(pending.p_latency ? pending.p_latency : 1);
            p_busy_ = true;
        }
        regs.vc[0] = 0;
        ++cycle_;
        ++count;
        if (stopping) {                      // the pair after an E bit runs, then the program ends
            pc = (pc + 8) & u32(micro.size() - 1);
            break;
        }
        if (upper & 0x40000000u) stopping = true;
        const u32 next = delayed != kNone ? delayed : (pc + 8) & u32(micro.size() - 1);
        delayed = branch;
        pc = next;
    }
    LandAll();
    resume = pc;
    pairs_ += count;
    return count;
}

Vu& Vu1() {
    static Vu vu(1);
    static const bool attached = [] {
        // The EE sees VU1's memories at 0x11008000 (instructions) and
        // 0x1100C000 (data).
        vu.Attach(GuestMemory().Translate(0x11008000u, 16384), GuestMemory().Translate(0x1100C000u, 16384));
        return true;
    }();
    (void)attached;
    return vu;
}

Vu& Vu0() {
    static Vu vu(0);
    static const bool attached = [] {
        // VU0's at 0x11000000 and 0x11004000.
        vu.Attach(GuestMemory().Translate(0x11000000u, 4096), GuestMemory().Translate(0x11004000u, 4096));
        return true;
    }();
    (void)attached;
    return vu;
}

}  // namespace ps2host
