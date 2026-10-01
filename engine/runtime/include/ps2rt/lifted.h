/*
 * Support for lifted functions: machine code translated instruction by instruction.
 *
 * A derived function (generated.h) is an expression over its arguments and reads
 * like source. A lifted function is a mechanical translation over an explicit
 * register context: every instruction becomes one statement that does exactly
 * what the R5900 does, branches become gotos, and calls pass the whole context.
 * It is less readable, but every function the interpreter can run can be lifted,
 * whatever its size or control flow, and each one is verified the same way: run
 * against the original machine code, comparing the entire register file and every
 * byte of memory written.
 *
 * The semantics follow engine/ps2decomp/emu.py exactly - the interpreter is the
 * reference both are tested against.
 */

#ifndef PS2RT_LIFTED_H
#define PS2RT_LIFTED_H

#include "ps2rt/generated.h"
#include "ps2rt/types.h"

namespace lifted {

/* The register state a function runs on, and the only state it has. */
struct Context {
    u64 r[32];      /* general registers, low 64 bits */
    u64 rh[32];     /* their upper 64 bits, for lq/sq and the 128-bit MMI ops */
    u64 hi, lo;     /* multiply/divide results */
    u64 hi1, lo1;   /* the second pipeline's, as mult1/div1 use */
    u32 f[32];      /* FPU registers, as raw bit patterns */
    u32 acc;        /* the FPU accumulator, as a bit pattern */
    u32 fcc;        /* the FPU condition flag */
    s64 budget;     /* loop iterations left before the run is abandoned */
};

/* Thrown, and caught at the entry point, when a run cannot go on: a trap
   instruction, an exhausted loop budget, or a callee that failed. */
struct Abort {
    u32 address;
    u32 reason;     /* 1 trap, 2 budget, 3 callee failed */
};

/* Calls `target` with the whole context, as a jal or jalr does; returns when the
   callee returns. Defined by whatever hosts the lifted code. */
void Call(Context& c, u32 target);

/* A syscall: the host decides what the service does. */
void Syscall(Context& c);

/* A word result, sign-extended into its 64-bit register as the EE does. */
inline u64 Sx32(u64 value) { return (u64)(s64)(s32)(u32)value; }

inline u64 Read64(u32 address) { return generated::ReadU64(address); }

/* At every loop head, so a loop that never ends cannot hang the host. */
inline void Tick(Context& c, u32 address) {
    if (--c.budget < 0) throw Abort{address, 2};
}

inline void Trap(u32 address) { throw Abort{address, 1}; }

/* 32x32 multiply into HI/LO, as mult/multu/madd do; the caller copies LO to rd. */
void Multiply(u64& hi, u64& lo, u64 a, u64 b, bool is_signed, bool accumulate);
void Divide(u64& hi, u64& lo, u64 a, u64 b, bool is_signed);

/* Single-precision arithmetic is generated.h's (FloatAdd, IntFromFloat, ...),
   shared with the derived code; comparisons use FloatValue. This is the one the
   accumulator needs besides: acc + a * b (or acc - a * b), rounded once. */
u32 FloatMulAdd(u32 acc, u32 a, u32 b, bool subtract);

/* Per-byte 128-bit add and subtract (paddb, psubb). */
void AddBytes(u64& lo, u64& hi, u64 alo, u64 ahi, u64 blo, u64 bhi, bool subtract);

/* Lane-wise 128-bit MMI operations: each lane of rs (a) and rt (b). */
enum LaneOp { kPmaxw = 1, kPminw, kPsubw, kPadduw, kPaddub, kPabsw };
void Lanes(int op, u64& lo, u64& hi, u64 alo, u64 ahi, u64 blo, u64 bhi);
void ShiftHalfwordsRight(u64& lo, u64& hi, u64 blo, u64 bhi, u32 amount);   /* psrlh */

/* Unaligned loads (they merge into the register's old value) and stores. */
u64 LoadWordLeft(u32 address, u64 old);
u64 LoadWordRight(u32 address, u64 old);
u64 LoadDoubleLeft(u32 address, u64 old);
u64 LoadDoubleRight(u32 address, u64 old);
void StoreLeft(u32 address, u64 value, u32 size);
void StoreRight(u32 address, u64 value, u32 size);

}  // namespace lifted

#endif  /* PS2RT_LIFTED_H */
