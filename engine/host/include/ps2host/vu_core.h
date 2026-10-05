/*
 * The vector units' steps, for microprograms compiled ahead of time.
 *
 * engine/tools/vurecomp.py turns the microprograms a game runs into C++: each
 * instruction pair one block of straight-line code, its registers, fields and
 * operations constants. That code works the Vu's state through VuCore, whose
 * steps are the interpreter's (vu.cpp: Run, Lower, Upper) taken apart - the
 * stalls and the pipelines included, so that a compiled program and the
 * interpreter end in the same state, to the cycle. The interpreter stays the
 * reference: VuCompiledIsTheInterpreter (host tests) runs random programs
 * both ways and compares everything.
 */

#ifndef PS2HOST_VU_CORE_H
#define PS2HOST_VU_CORE_H

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

#include "ps2host/vu.h"
#include "ps2rt/vu_math.h"

#if defined(_MSC_VER)
#define PS2HOST_VU_INLINE __forceinline
#else
#define PS2HOST_VU_INLINE inline __attribute__((always_inline))
#endif

namespace ps2host {

/* What the lower instruction of a pair leaves for the end of the pair. */
struct Vu::Pending {
    int vf = -1;                    // a vector register the lower writes
    u32 lanes = 0, value[4] = {};
    bool vc = false;                // the lower changed control registers
    u32 vc_after[32];               // ...to these (only when vc: a pair is not worth zeroing 128 bytes)
    bool p = false;
    u32 p_value = 0;
    int p_latency = 0;              // the EFU op's, in cycles
    int q_latency = 0;              // DIV, SQRT, RSQRT's
    bool q_set = false;             // compiled code: Q's next value (q_value), not through vc_after
    u32 q_value = 0;
};

inline void Vu::LandNow() {
    if (q_busy_ && q_ready_ <= cycle_) { regs.vc[lifted::kRegQ] = q_next_; q_busy_ = false; }
    if (p_busy_ && p_ready_ <= cycle_) { p = p_next_; p_busy_ = false; }
    while (flag_count_ && flag_queue_[flag_head_].ready <= cycle_) {
        const FlagWrite& f = flag_queue_[flag_head_];
        if (f.sets_mac) { mac = f.mac; status = (status & ~0x3u) | (f.status & 0x3u); status |= f.status & 0xC0u; }
        if (f.sets_clip) clip = f.clip;
        flag_head_ = (flag_head_ + 1) & 7;
        --flag_count_;
    }
}

PS2HOST_VU_INLINE void Vu::PushFlags(const FlagWrite& write) {
    if (flag_count_ < 8) flag_queue_[(flag_head_ + flag_count_++) & 7] = write;
}

PS2HOST_VU_INLINE void Vu::Land() {
    // The flags land inline: nearly every pair has an FMAC's flags arriving,
    // and a call for each was 5% of a race. (Q, P and the flags are apart:
    // the order they land in within a cycle changes nothing.)
    while (flag_count_ && flag_queue_[flag_head_].ready <= cycle_) {
        const FlagWrite& f = flag_queue_[flag_head_];
        if (f.sets_mac) { mac = f.mac; status = (status & ~0x3u) | (f.status & 0x3u); status |= f.status & 0xC0u; }
        if (f.sets_clip) clip = f.clip;
        flag_head_ = (flag_head_ + 1) & 7;
        --flag_count_;
    }
    if ((q_busy_ && q_ready_ <= cycle_) || (p_busy_ && p_ready_ <= cycle_)) LandNow();
}

/* Where a run stands between pairs. */
struct VuRunState {
    std::uint32_t pc = 0;
    std::uint32_t delayed = ~0u;    // a branch the pair before took: where the next pair goes after it
    bool stopping = false;          // the pair before had E: this one is the last
    std::uint64_t count = 0;        // pairs run
    bool done = false;              // the program has ended
};

/* A block of a compiled program: pairs run in a line from its first, the
   run's state then left where it goes next (s.pc), or done. */
using VuBlock = void (*)(Vu& vu, VuRunState& state, std::uint64_t limit);

/* A microprogram compiled ahead of time. `code` is what it was compiled
   from, as runs: a count of runs, then for each its byte address in
   instruction memory, its length in words and the words. Its blocks start at
   `starts` (byte addresses, ascending); a run goes from block to block while
   the next starts one of them, and to the interpreter otherwise. */
struct VuCompiled {
    int unit;                       // 0: VU0, 1: VU1
    std::uint32_t entry;            // where a run it was recorded from started
    const std::uint32_t* code;
    const std::uint32_t* starts;
    const VuBlock* blocks;
    std::uint32_t block_count;
};
/* Adds one to those Vu::Run looks for (the generated file does, as it starts). */
void RegisterVuProgram(const VuCompiled* program);

struct VuCore {
    using u32 = Vu::u32;
    using u64 = Vu::u64;
    using Pending = Vu::Pending;    // what the generated code holds a pair's lower writes in
    static constexpr u32 kNone = ~0u;
    static constexpr int kFmacLatency = 4;

    // ------------------------------------------------------------ stalls
    static PS2HOST_VU_INLINE u64 Now(const Vu& v) { return v.cycle_; }
    static PS2HOST_VU_INLINE void Wait(u64& t, const Vu& v, int reg, int lane) {
        t = std::max(t, v.vf_ready_[reg][lane]);
    }
    static PS2HOST_VU_INLINE void WaitQ(u64& t, const Vu& v) { if (v.q_busy_) t = std::max(t, v.q_ready_); }
    static PS2HOST_VU_INLINE void WaitP(u64& t, const Vu& v) { if (v.p_busy_) t = std::max(t, v.p_ready_); }
    static PS2HOST_VU_INLINE void Issue(Vu& v, u64 t) {
        v.cycle_ = t;
        v.Land();
    }

    // ------------------------------------------------------------ lower
    static PS2HOST_VU_INLINE u32 Vi(const Vu& v, u32 r) { return v.regs.vc[r]; }
    /* setvi: only VI1-VI15 (the generator leaves out the others). */
    static PS2HOST_VU_INLINE void SetVi(Vu& v, u32 r, u32 value) { v.regs.vc[r] = value & 0xFFFF; }
    static PS2HOST_VU_INLINE u32* Quad(Vu& v, u32 address) {
        return reinterpret_cast<u32*>(v.data.data() + std::size_t(address & v.data_mask_) * 16);
    }
    static PS2HOST_VU_INLINE void Load(Vu& v, Vu::Pending& out, u32 address, int it, u32 dest) {
        const u32* q = Quad(v, address);
        out.vf = it;
        out.lanes = dest;
        std::memcpy(out.value, q, 16);
    }
    template <u32 Dest>
    static PS2HOST_VU_INLINE void Store(Vu& v, u32 address, int fs) {
        u32* q = Quad(v, address);
        const u32* from = v.regs.vf[fs];
        if constexpr ((Dest & 8) != 0) q[0] = from[0];
        if constexpr ((Dest & 4) != 0) q[1] = from[1];
        if constexpr ((Dest & 2) != 0) q[2] = from[2];
        if constexpr ((Dest & 1) != 0) q[3] = from[3];
    }
    /* ILW, ILWR: the first lane dest names (the generator gives it). */
    static PS2HOST_VU_INLINE u32 Word(Vu& v, u32 address, int lane) { return Quad(v, address)[lane]; }
    template <u32 Dest>
    static PS2HOST_VU_INLINE void StoreVi(Vu& v, u32 address, u32 it) {     // ISW, ISWR
        u32* q = Quad(v, address);
        const u32 value = v.regs.vc[it] & 0xFFFF;
        if constexpr ((Dest & 8) != 0) q[0] = value;
        if constexpr ((Dest & 4) != 0) q[1] = value;
        if constexpr ((Dest & 2) != 0) q[2] = value;
        if constexpr ((Dest & 1) != 0) q[3] = value;
    }
    static PS2HOST_VU_INLINE void Mfp(Vu& v, Vu::Pending& out, int it, u32 dest) {
        out.vf = it;
        out.lanes = dest;
        for (u32& x : out.value) x = v.p;
    }
    static PS2HOST_VU_INLINE u32 Top(const Vu& v) { return v.top; }
    static PS2HOST_VU_INLINE u32 Itop(const Vu& v) { return v.itop; }
    static void Kick(Vu& v, u32 address) { v.Kick(address); }
    /* Through macro mode's COP2 (MOVE, MR32, MTIR, MFIR, the R register, DIV,
       SQRT, RSQRT), its writes held back to the pair's end. */
    static void ThroughCop2(Vu& v, Vu::Pending& out, u32 word, int it, u32 dest) {
        u32 before_vc[32], before_vf[4];
        std::memcpy(before_vc, v.regs.vc, sizeof before_vc);
        std::memcpy(before_vf, v.regs.vf[it], sizeof before_vf);
        lifted::Cop2(v.regs, 0x4A000000u | (word & 0x01FFFFFF));
        if (std::memcmp(before_vf, v.regs.vf[it], sizeof before_vf) != 0) {
            out.vf = it;
            out.lanes = dest;
            std::memcpy(out.value, v.regs.vf[it], 16);
            std::memcpy(v.regs.vf[it], before_vf, sizeof before_vf);
        }
        if (std::memcmp(before_vc, v.regs.vc, sizeof before_vc) != 0) {
            out.vc = true;
            std::memcpy(out.vc_after, v.regs.vc, sizeof out.vc_after);
            std::memcpy(v.regs.vc, before_vc, sizeof before_vc);
        }
    }
    /* The lower operations macro mode shares, as ThroughCop2 would leave
       them: a vector write held back only if it changes the register, a VI
       write only if it changes the VI, Q's next value set. */
    static PS2HOST_VU_INLINE void HoldVf(Vu& v, Vu::Pending& out, int it, u32 dest, const u32 value[4]) {
        if (it == 0) return;
        u32 after[4];
        std::memcpy(after, v.regs.vf[it], 16);
        for (int lane = 0; lane < 4; ++lane)
            if (dest & (8u >> lane)) after[lane] = value[lane];
        if (std::memcmp(after, v.regs.vf[it], 16) == 0) return;
        out.vf = it;
        out.lanes = dest;
        std::memcpy(out.value, after, 16);
    }
    static PS2HOST_VU_INLINE void Move(Vu& v, Vu::Pending& out, int it, int fs, u32 dest) {
        const u32 value[4] = {v.regs.vf[fs][0], v.regs.vf[fs][1], v.regs.vf[fs][2], v.regs.vf[fs][3]};
        HoldVf(v, out, it, dest, value);
    }
    static PS2HOST_VU_INLINE void Mr32(Vu& v, Vu::Pending& out, int it, int fs, u32 dest) {
        const u32* a = v.regs.vf[fs];
        const u32 value[4] = {a[1], a[2], a[3], a[0]};
        HoldVf(v, out, it, dest, value);
    }
    static PS2HOST_VU_INLINE void Mfir(Vu& v, Vu::Pending& out, int it, int is, u32 dest) {
        u32 x = v.regs.vc[is] & 0xFFFF;
        if (x & 0x8000) x |= 0xFFFF0000u;
        const u32 value[4] = {x, x, x, x};
        HoldVf(v, out, it, dest, value);
    }
    /* MTIR: VI1-VI15 only (the generator leaves out the others). */
    static PS2HOST_VU_INLINE void Mtir(Vu& v, Vu::Pending& out, int it, int fs, int fsf) {
        const u32 value = v.regs.vf[fs][fsf] & 0xFFFF;
        if (value == v.regs.vc[it]) return;
        out.vc = true;
        std::memcpy(out.vc_after, v.regs.vc, sizeof out.vc_after);
        out.vc_after[it] = value;
    }
    static PS2HOST_VU_INLINE void Divide(Vu::Pending& out, u32 q, int latency) {
        out.q_set = true;
        out.q_value = q;
        out.q_latency = latency;
    }
    static PS2HOST_VU_INLINE u32 Div(const Vu& v, int fs, int fsf, int ft, int ftf) {
        const u32 num = v.regs.vf[fs][fsf], den = v.regs.vf[ft][ftf];
        return lifted::Value(den) == 0.0 ? ((num ^ den) & lifted::kSign) | lifted::kMax
                                         : lifted::Result(lifted::Value(num) / lifted::Value(den));
    }
    static PS2HOST_VU_INLINE u32 Sqrt(const Vu& v, int ft, int ftf) {
        return lifted::Result(std::sqrt(std::fabs(lifted::Value(v.regs.vf[ft][ftf]))));
    }
    static PS2HOST_VU_INLINE u32 Rsqrt(const Vu& v, int fs, int fsf, int ft, int ftf) {
        const u32 num = v.regs.vf[fs][fsf];
        const double root = std::sqrt(std::fabs(lifted::Value(v.regs.vf[ft][ftf])));
        return root == 0.0 ? (num & lifted::kSign) | lifted::kMax : lifted::Result(lifted::Value(num) / root);
    }
    static PS2HOST_VU_INLINE void Efu(Vu::Pending& out, double value, int latency) {
        out.p = true;
        out.p_value = lifted::Result(value);
        out.p_latency = latency;
    }
    static PS2HOST_VU_INLINE double Value(const Vu& v, int reg, int lane) {
        return lifted::Value(v.regs.vf[reg][lane]);
    }
    // The flag instructions read and set these.
    static PS2HOST_VU_INLINE u32& Clip(Vu& v) { return v.clip; }
    static PS2HOST_VU_INLINE u32& ClipLatest(Vu& v) { return v.clip_latest_; }
    static PS2HOST_VU_INLINE u32& Status(Vu& v) { return v.status; }
    static PS2HOST_VU_INLINE u32& StatusLatest(Vu& v) { return v.status_latest_; }
    static PS2HOST_VU_INLINE u32 Mac(const Vu& v) { return v.mac; }

    // ------------------------------------------------------------ upper
#if PS2RT_VU_SSE
    /* An FMAC operation: Op (vu_math.h's), Operand (0-3 a lane of ft, 4 Q,
       5 I, 6 ft), into To (-1 ACC; 0 nowhere: VF0 stays). */
    template <int Op, int Operand, int To, u32 Dest>
    static PS2HOST_VU_INLINE void Fmac(Vu& v, int fs, int ft) {
        lifted::Context& c = v.regs;
        const __m128i a = _mm_loadu_si128(reinterpret_cast<const __m128i*>(c.vf[fs]));
        __m128i b;
        if constexpr (Operand == 6) b = _mm_loadu_si128(reinterpret_cast<const __m128i*>(c.vf[ft]));
        else if constexpr (Operand == 4) b = _mm_set1_epi32(int(c.vc[lifted::kRegQ]));
        else if constexpr (Operand == 5) b = _mm_set1_epi32(int(c.vc[lifted::kRegI]));
        else b = _mm_set1_epi32(int(c.vf[ft][Operand]));
        const __m128i acc = _mm_loadu_si128(reinterpret_cast<const __m128i*>(c.vacc));
        const __m128i result = lifted::LanesSse<Op>(a, b, acc);
        if constexpr (To == 0 || Dest == 0) {
            return;
        } else {
            u32* to = To < 0 ? c.vacc : c.vf[To];
            const __m128i mask = _mm_set_epi32((Dest & 1) ? -1 : 0, (Dest & 2) ? -1 : 0, (Dest & 4) ? -1 : 0,
                                               (Dest & 8) ? -1 : 0);
            const __m128i old = _mm_loadu_si128(reinterpret_cast<const __m128i*>(to));
            _mm_storeu_si128(reinterpret_cast<__m128i*>(to),
                             _mm_or_si128(_mm_and_si128(mask, result), _mm_andnot_si128(mask, old)));
        }
    }
#else
    template <int Op, int Operand, int To, u32 Dest>
    static void Fmac(Vu& v, int fs, int ft) {
        lifted::FmacOp f;
        f.op = Op;
        f.operand = Operand;
        f.to_acc = To < 0;
        f.fd = To > 0 ? u32(To) : 0;
        f.fs = u32(fs);
        f.ft = u32(ft);
        f.dest = Dest;
        lifted::RunFmac(v.regs, f);
    }
#endif
    /* Macro mode's COP2 operations, as it runs them (vu0.cpp's Cop2), without
       finding which each time. */
    template <u32 Dest>
    static PS2HOST_VU_INLINE void Opmula(Vu& v, int fs, int ft) {
        const u32 *a = v.regs.vf[fs], *b = v.regs.vf[ft];
        const u32 out[3] = {lifted::Result(lifted::Value(a[1]) * lifted::Value(b[2])),
                            lifted::Result(lifted::Value(a[2]) * lifted::Value(b[0])),
                            lifted::Result(lifted::Value(a[0]) * lifted::Value(b[1]))};
        if constexpr ((Dest & 8) != 0) v.regs.vacc[0] = out[0];
        if constexpr ((Dest & 4) != 0) v.regs.vacc[1] = out[1];
        if constexpr ((Dest & 2) != 0) v.regs.vacc[2] = out[2];
    }
    template <u32 Dest>
    static PS2HOST_VU_INLINE void Opmsub(Vu& v, int fd, int fs, int ft) {
        const u32 *a = v.regs.vf[fs], *b = v.regs.vf[ft];
        const u32* acc = v.regs.vacc;
        const u32 out[3] = {lifted::Binary(lifted::kMsub, a[1], b[2], acc[0]),
                            lifted::Binary(lifted::kMsub, a[2], b[0], acc[1]),
                            lifted::Binary(lifted::kMsub, a[0], b[1], acc[2])};
        if (fd == 0) return;
        if constexpr ((Dest & 8) != 0) v.regs.vf[fd][0] = out[0];
        if constexpr ((Dest & 4) != 0) v.regs.vf[fd][1] = out[1];
        if constexpr ((Dest & 2) != 0) v.regs.vf[fd][2] = out[2];
    }
    /* ITOF0/4/12/15 (Shift 0-3), FTOI the same, ABS: into ft (VF0 stays). */
    template <u32 Dest, int Shift>
    static PS2HOST_VU_INLINE void Itof(Vu& v, int ft, int fs) {
        static constexpr double kScale[4] = {1.0, 16.0, 4096.0, 32768.0};
        const u32* a = v.regs.vf[fs];
        u32 out[4];
        for (int lane = 0; lane < 4; ++lane) out[lane] = lifted::Result(double(std::int32_t(a[lane])) / kScale[Shift]);
        WriteVf<Dest>(v, ft, out);
    }
    template <u32 Dest, int Shift>
    static PS2HOST_VU_INLINE void Ftoi(Vu& v, int ft, int fs) {
        static constexpr double kScale[4] = {1.0, 16.0, 4096.0, 32768.0};
        const u32* a = v.regs.vf[fs];
        u32 out[4];
        for (int lane = 0; lane < 4; ++lane) out[lane] = lifted::FloatToInt(lifted::Value(a[lane]) * kScale[Shift]);
        WriteVf<Dest>(v, ft, out);
    }
    template <u32 Dest>
    static PS2HOST_VU_INLINE void Abs(Vu& v, int ft, int fs) {
        const u32* a = v.regs.vf[fs];
        u32 out[4];
        for (int lane = 0; lane < 4; ++lane) out[lane] = lifted::Clean(a[lane]) & ~lifted::kSign;
        WriteVf<Dest>(v, ft, out);
    }
    template <u32 Dest>
    static PS2HOST_VU_INLINE void WriteVf(Vu& v, int reg, const u32 out[4]) {
        if (reg == 0) return;
        if constexpr ((Dest & 8) != 0) v.regs.vf[reg][0] = out[0];
        if constexpr ((Dest & 4) != 0) v.regs.vf[reg][1] = out[1];
        if constexpr ((Dest & 2) != 0) v.regs.vf[reg][2] = out[2];
        if constexpr ((Dest & 1) != 0) v.regs.vf[reg][3] = out[3];
    }
    /* Any other upper operation, as macro mode's COP2 runs it. */
    static void Cop2(Vu& v, u32 word) { lifted::Cop2(v.regs, 0x4A000000u | (word & 0x01FFFFFF)); }
    /* CLIP: fs.xyz against |ft.w|. */
    static void ClipJudge(Vu& v, int fs, int ft) {
        const u32* f = v.regs.vf[fs];
        const double w = std::fabs(lifted::Value(v.regs.vf[ft][3]));
        u32 judgement = 0;
        for (int lane = 0; lane < 3; ++lane) {
            const double x = lifted::Value(f[lane]);
            if (x > w) judgement |= 1u << (2 * lane);
            if (x < -w) judgement |= 2u << (2 * lane);
        }
        v.clip_latest_ = ((v.clip_latest_ << 6) | judgement) & 0xFFFFFF;
    }
    /* MAC: zero (bits 3-0) and sign (7-4) of each written field, x highest;
       `out` the register the upper wrote (Target: -1 ACC). */
    template <int Target, u32 Dest>
    static PS2HOST_VU_INLINE void Flags(Vu& v) {
        const u32* out = Target >= 0 ? v.regs.vf[Target] : v.regs.vacc;
#if PS2RT_VU_SSE
        // The four lanes at once: which are zero and which negative (as Clean
        // leaves them), lane x the highest bit as the MAC flag has it.
        static constexpr u32 kReversed[16] = {0, 8, 4, 12, 2, 10, 6, 14, 1, 9, 5, 13, 3, 11, 7, 15};
        const __m128i bits = lifted::CleanLanes(_mm_loadu_si128(reinterpret_cast<const __m128i*>(out)));
        const __m128i magnitude = _mm_and_si128(bits, _mm_set1_epi32(0x7FFFFFFF));
        const u32 zero = u32(_mm_movemask_ps(_mm_castsi128_ps(_mm_cmpeq_epi32(magnitude, _mm_setzero_si128()))));
        const u32 sign = u32(_mm_movemask_ps(_mm_castsi128_ps(bits)));
        const u32 m = (kReversed[zero] & Dest) | (kReversed[sign] & Dest) << 4;
#else
        u32 m = 0;
        for (int lane = 0; lane < 4; ++lane) {
            if (!(Dest & (8u >> lane))) continue;
            const u32 bits = lifted::Clean(out[lane]);
            if ((bits & 0x7FFFFFFFu) == 0) m |= 8u >> lane;
            if (bits & 0x80000000u) m |= 0x80u >> lane;
        }
#endif
        v.mac_latest_ = m;
        v.status_latest_ = (v.status_latest_ & ~0x3u) | ((m & 0xF) ? 1u : 0u) | ((m & 0xF0) ? 2u : 0u);
        v.status_latest_ |= (v.status_latest_ & 3u) << 6;
    }

    // ------------------------------------------------------------ the pair's end
    /* As Run after the two halves: the lower's held writes (giving way to the
       upper's fields), when the fields land, the flags on their way, Q and P. */
    static PS2HOST_VU_INLINE void Finish(Vu& v, Vu::Pending& pending, int written, u32 lanes, bool set_flags,
                                         bool sets_mac, bool sets_clip) {
        if (pending.vf > 0) {
            for (int lane = 0; lane < 4; ++lane) {
                const u32 bit = 8u >> lane;
                if ((pending.lanes & bit) && !(written == pending.vf && (lanes & bit))) {
                    v.regs.vf[pending.vf][lane] = pending.value[lane];
                    v.vf_ready_[pending.vf][lane] = v.cycle_ + kFmacLatency;
                }
            }
        }
        FinishUpper(v, written, lanes, set_flags, sets_mac, sets_clip);
        if (pending.q_latency) {
            v.q_next_ = pending.q_set ? pending.q_value
                      : pending.vc    ? pending.vc_after[lifted::kRegQ] : v.regs.vc[lifted::kRegQ];
            v.q_ready_ = v.cycle_ + u64(pending.q_latency);
            v.q_busy_ = true;
            if (pending.vc) pending.vc_after[lifted::kRegQ] = v.regs.vc[lifted::kRegQ];
        }
        if (pending.vc) std::memcpy(v.regs.vc, pending.vc_after, sizeof pending.vc_after);
        if (pending.p) {
            v.p_next_ = pending.p_value;
            v.p_ready_ = v.cycle_ + u64(pending.p_latency ? pending.p_latency : 1);
            v.p_busy_ = true;
        }
        End(v);
    }
    /* The same for a pair whose lower left nothing (FinishUpper, then End). */
    static PS2HOST_VU_INLINE void FinishUpper(Vu& v, int written, u32 lanes, bool set_flags, bool sets_mac,
                                              bool sets_clip) {
        if (written > 0)
            for (int lane = 0; lane < 4; ++lane)
                if (lanes & (8u >> lane)) v.vf_ready_[written][lane] = v.cycle_ + kFmacLatency;
        if (set_flags)
            v.PushFlags({v.cycle_ + kFmacLatency, v.mac_latest_, v.status_latest_, v.clip_latest_, sets_mac,
                         sets_clip});
    }
    static PS2HOST_VU_INLINE void End(Vu& v) {
        v.regs.vc[0] = 0;
        ++v.cycle_;
    }
};

}  // namespace ps2host

#endif  /* PS2HOST_VU_CORE_H */
