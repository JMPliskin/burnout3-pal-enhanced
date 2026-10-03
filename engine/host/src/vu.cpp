/*
 * The vector units in micro mode (vu.h).
 *
 * Instruction fields (both halves): dest x y z w in bits 24-21, ft/it 20-16,
 * fs/is 15-11, fd/id 10-6. The upper's flags: I 31 (the lower word is a float
 * for the I register), E 30 (end), M 29, D 28, T 27. Instruction addresses are
 * in bytes (pairs of 8); data addresses in quadwords.
 */

#include "ps2host/vu.h"

#include <cmath>
#include <cstring>

#include "ps2rt/generated.h"

namespace lifted {
// The arithmetic model (runtime/src/vu0.cpp).
u32 Clean(u32 bits);
double Value(u32 bits);
u32 Result(double number);
}  // namespace lifted

namespace ps2host {

namespace {

constexpr Vu::u32 kNone = ~0u;
constexpr int kRegR = 20, kRegI = 21, kRegQ = 22;
constexpr Vu::u32 kCop2 = 0x4A000000u;       // a macro-mode COP2 operation

Vu::u32 Field(Vu::u32 word, int low, int count) { return (word >> low) & ((1u << count) - 1); }
std::int32_t Imm11(Vu::u32 word) { return std::int32_t(word << 21) >> 21; }

}  // namespace

struct Vu::Pending {
    int vf = -1;                    // a vector register the lower writes
    u32 lanes = 0, value[4] = {};
    bool vc = false;                // the lower changed control registers
    u32 vc_after[32] = {};
    bool p = false;
    u32 p_value = 0;
};

Vu::Vu(int index)
    : micro(index ? 16384 : 4096), data(index ? 16384 : 4096), data_mask_(index ? 0x3FF : 0xFF) {
    regs.vf[0][3] = 0x3F800000u;    // vf0 = (0, 0, 0, 1)
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
void Vu::Upper(u32 word, int* written_reg, u32* written_lanes) {
    *written_reg = -1;
    *written_lanes = 0;
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
            clip = ((clip << 6) | judgement) & 0xFFFFFF;
            return;
        }
        if (index == 0x2F) return;                          // NOP
        if ((index >= 0x10 && index <= 0x17) || index == 0x1D) target = int(ft);   // ITOF, FTOI, ABS
        else flags = true;                                  // the accumulator forms
    }
    lifted::Cop2(regs, kCop2 | (word & 0x01FFFFFF));
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
        mac = m;
        status = (status & ~0x3u) | ((m & 0xF) ? 1u : 0u) | ((m & 0xF0) ? 2u : 0u);
        status |= (status & 3u) << 6;                       // the sticky copies
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
    };
    const u32 target = (pc + 8 + u32(Imm11(word) * 8)) & u32(micro.size() - 1);
    const std::int16_t a = std::int16_t(vi[is]), b = std::int16_t(vi[it]);
    switch (op) {
        case 0x40: {
            const u32 funct = word & 0x3F;
            if (funct < 0x3C) { through_cop2(); return; }   // IADD, ISUB, IADDI, IAND, IOR
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
                case 0x7B: return;                                                            // WAITP
                case 0x7C: efu(std::sin(lifted::Value(v[fsf]))); return;                      // ESIN
                case 0x7D: efu(std::atan(lifted::Value(v[fsf]))); return;                     // EATAN
                case 0x7E: efu(std::exp(-lifted::Value(v[fsf]))); return;                     // EEXP
                default: through_cop2(); return;    // MOVE MR32 DIV SQRT RSQRT WAITQ MTIR MFIR R*
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
        case 0x11: clip = word & 0xFFFFFF; return;                                                  // FCSET
        case 0x12: setvi(1, (clip & word & 0xFFFFFF) != 0); return;                                 // FCAND
        case 0x13: setvi(1, ((clip | word) & 0xFFFFFF) == 0xFFFFFF); return;                        // FCOR
        case 0x14: case 0x15: case 0x16: case 0x17: {                           // FSEQ FSSET FSAND FSOR
            const u32 imm = (Field(word, 21, 1) << 11) | Field(word, 0, 11);
            if (op == 0x14) setvi(it, (status & 0xFFF) == imm);
            else if (op == 0x15) status = (status & 0x3F) | (imm & 0xFC0);
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

Vu::u64 Vu::Run(u32 pc, u64 limit) {
    u64 count = 0;
    u32 delayed = kNone;
    bool stopping = false;
    pc &= u32(micro.size() - 1) & ~7u;
    while (count < limit) {
        u32 lower, upper;
        std::memcpy(&lower, micro.data() + pc, 4);
        std::memcpy(&upper, micro.data() + pc + 4, 4);
        Pending pending;
        u32 branch = kNone;
        if (upper & 0x80000000u) regs.vc[kRegI] = lower;    // I bit: the lower is I's value
        else Lower(lower, pc, pending, &branch);
        int written = -1;
        u32 lanes = 0;
        Upper(upper, &written, &lanes);
        if (pending.vf > 0) {
            for (int lane = 0; lane < 4; ++lane) {
                const u32 bit = 8u >> lane;
                if ((pending.lanes & bit) && !(written == pending.vf && (lanes & bit)))
                    regs.vf[pending.vf][lane] = pending.value[lane];
            }
        }
        if (pending.vc) std::memcpy(regs.vc, pending.vc_after, sizeof pending.vc_after);
        if (pending.p) p = pending.p_value;
        regs.vc[0] = 0;
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
    resume = pc;
    pairs_ += count;
    return count;
}

Vu& Vu1() {
    static Vu vu(1);
    return vu;
}

}  // namespace ps2host
