/*
 * The vector units in micro mode: a VU running a microprogram out of its own
 * instruction memory over its own data memory, as VIF1 starts VU1 (MSCAL) and
 * the EE starts VU0 (vcallms).
 *
 * A microinstruction is a pair: an upper instruction for the FMAC units and a
 * lower one for everything else, issued together. The upper encodes exactly as
 * VU0 macro mode's COP2 operations do, so it runs through the runtime's
 * lifted::Cop2 - the arithmetic verified against vu0.py - as do the lower
 * operations macro mode shares (MOVE, MR32, DIV, SQRT, RSQRT, MTIR, MFIR, the
 * R register, and the integer ALU). Loads, stores, branches, the flags, the
 * elementary functions and XGKICK are here.
 *
 * Within a pair the lower instruction reads its registers before the upper
 * writes them, and where both write the same field the upper's result stands.
 * Results are otherwise available at once: the hardware's pipeline interlocks
 * make that the same for code that waits (WAITQ, WAITP, and the stalls), which
 * is what compiled microcode does. An E bit ends the program after the pair that
 * follows it.
 */

#ifndef PS2HOST_VU_H
#define PS2HOST_VU_H

#include <cstdint>
#include <functional>
#include <vector>

#include "ps2rt/lifted.h"

namespace ps2host {

class Vu {
public:
    using u32 = std::uint32_t;
    using u64 = std::uint64_t;

    /* VU0 (4 KB of each memory) or VU1 (16 KB). */
    explicit Vu(int index);
    Vu(const Vu&) = delete;             /* its memory spans point into itself */
    Vu& operator=(const Vu&) = delete;

    /* Instruction and data memory: the VU's own, or (Attach) the EE's view of
       it at 0x11000000, which code also reads and writes directly. */
    struct Span {
        std::uint8_t* p;
        std::size_t n;
        std::uint8_t* data() const { return p; }
        std::size_t size() const { return n; }
    };
    Span micro, data;
    void Attach(std::uint8_t* micro_memory, std::uint8_t* data_memory);
    lifted::Context regs{};         /* vf, vc (VI0-15, R 20, I 21, Q 22), vacc */
    u32 mac = 0, status = 0, clip = 0, p = 0;
    u32 top = 0, itop = 0;          /* what XTOP and XITOP read (VU1) */
    u32 resume = 0;                 /* where the last run ended: MSCNT continues there */

    /* XGKICK: each quadword of the GIF packet, as PATH1 delivers it. */
    std::function<void(u64 low, u64 high)> on_kick;

    /* Runs from instruction address `pc` (bytes) to the end of the program;
       returns the instruction pairs executed. Stops after `limit` regardless. */
    u64 Run(u32 pc, u64 limit = u64(1) << 24);

    u64 pairs() const { return pairs_; }
    u64 kicks() const { return kicks_; }

    /* For tests: one 128-bit quadword of data memory. */
    u32* Quad(u32 address);

private:
    std::vector<std::uint8_t> own_micro_, own_data_;
    u32 data_mask_;                 /* in quadwords */
    u64 pairs_ = 0, kicks_ = 0;
    struct Pending;
    void Lower(u32 word, u32 pc, Pending& out, u32* branch);
    void Upper(u32 word, int* written_reg, u32* written_lanes);
    void Kick(u32 address);
};

/* VU1, which VIF1 loads and starts. */
Vu& Vu1();
/* VU0, which VIF0 loads and the EE starts (vcallms) or VIF0 does. */
Vu& Vu0();

}  // namespace ps2host

#endif  /* PS2HOST_VU_H */
