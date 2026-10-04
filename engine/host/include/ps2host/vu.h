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
 * An E bit ends the program after the pair that follows it.
 *
 * Timing is modelled where code can see it, in cycles: a pair reading a vector
 * field still being computed stalls until it lands (4 cycles after the FMAC or
 * load issued); Q arrives 7 cycles after DIV and SQRT, 13 after RSQRT, and P
 * after the EFU operation's latency, the old value readable meanwhile; and the
 * MAC, status and clip flags reach the lower instructions (FMAND, FCAND...) 4
 * cycles after the upper operation that set them. Hand-pipelined microcode -
 * Burnout 3's clip tests, which read the flags of the vertex before - depends
 * on all of these (engine/tools/vuhazards.py lists where).
 */

#ifndef PS2HOST_VU_H
#define PS2HOST_VU_H

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "ps2rt/lifted.h"

namespace lifted { struct FmacOp; }

namespace ps2host {

struct VuRunState;

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
    /* The flags as the lower instructions read them, and P. */
    u32 mac = 0, status = 0, clip = 0, p = 0;
    u32 top = 0, itop = 0;          /* what XTOP and XITOP read (VU1) */
    u32 resume = 0;                 /* where the last run ended: MSCNT continues there */

    /* XGKICK: each quadword of the GIF packet, as PATH1 delivers it. */
    std::function<void(u64 low, u64 high)> on_kick;

    /* Runs from instruction address `pc` (bytes) to the end of the program;
       returns the instruction pairs executed. Stops after `limit` regardless. */
    u64 Run(u32 pc, u64 limit = u64(1) << 24);

    u64 pairs() const { return pairs_; }
    /* Of those, the pairs run compiled ahead of time (vu_core.h). */
    u64 compiled_pairs() const { return compiled_pairs_; }
    /* Compiled programs entered, found with other code in memory than they
       were compiled from, and the bytes compared to tell. */
    u64 compiled_entries() const { return compiled_entries_; }
    u64 compiled_stale() const { return compiled_stale_; }
    u64 compiled_checked() const { return compiled_checked_; }
    u64 kicks() const { return kicks_; }
    /* Whether Run takes compiled programs where it has them (the tests run
       the interpreter alone to compare). */
    bool use_compiled = true;
    /* For tests: everything a run can change, the same in both. */
    bool SameState(const Vu& other) const;
    /* --vu-record: each program a run starts, and the code reachable from
       where it starts, to `path` (engine/tools/vurecomp.py compiles them). */
    static void RecordPrograms(const std::string& path);
    /* How often each program address was started, for reports. */
    const std::map<u32, u64>& starts() const { return starts_; }

    /* For tests: one 128-bit quadword of data memory. */
    u32* Quad(u32 address);

private:
    friend struct VuCore;
    std::vector<std::uint8_t> own_micro_, own_data_;
    u32 data_mask_;                 /* in quadwords */
    u64 pairs_ = 0, kicks_ = 0, compiled_pairs_ = 0, compiled_entries_ = 0, compiled_stale_ = 0,
        compiled_checked_ = 0;
    std::map<u32, u64> starts_;

    /* Timing, in cycles (one per pair issued, plus stalls). */
    u64 cycle_ = 0;
    u64 vf_ready_[32][4] = {};      /* when each vector field's pending write lands */
    u32 q_next_ = 0, p_next_ = 0;   /* DIV's and the EFU's results in flight */
    u64 q_ready_ = 0, p_ready_ = 0;
    bool q_busy_ = false, p_busy_ = false;
    /* The FMAC sets the flags as it finishes: what upper instructions have
       produced so far, and what is still on its way to the lower's view. */
    u32 mac_latest_ = 0, status_latest_ = 0, clip_latest_ = 0;
    struct FlagWrite { u64 ready; u32 mac, status, clip; bool sets_mac, sets_clip; };
    /* In the order they were set, which is the order they land in (each
       lands 4 cycles after it was set, and time only goes forward): a ring,
       flag_head_ the oldest. */
    FlagWrite flag_queue_[8] = {};
    int flag_head_ = 0, flag_count_ = 0;
    void PushFlags(const FlagWrite& write) {
        if (flag_count_ < 8) flag_queue_[(flag_head_ + flag_count_++) & 7] = write;
    }
    void Land();                    /* results whose time has come (a look, inline) */
    void LandNow();                 /* ...landing them */
    void LandAll();

    struct Pending;
    /* The interpreter, from where `state` stands. */
    void Interpret(VuRunState& state, u64 limit);
    /* Compiled programs from where `state` stands, as far as they go
       (vu_core.h); whether any ran. */
    bool RunCompiled(VuRunState& state, u64 limit);
    void Lower(u32 word, u32 pc, Pending& out, u32* branch);
    void Upper(u32 word, int* written_reg, u32* written_lanes, bool* set_flags,
               const lifted::FmacOp* fmac = nullptr);
    void Kick(u32 address);
};

/* VU1, which VIF1 loads and starts. */
Vu& Vu1();
/* VU0, which VIF0 loads and the EE starts (vcallms) or VIF0 does. */
Vu& Vu0();

}  // namespace ps2host

#endif  /* PS2HOST_VU_H */
