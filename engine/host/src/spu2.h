/*
 * The SPU2, the PS2's sound processor, as libsd drives it (iop_libs.cpp).
 *
 * Stage one models what games synchronise with: voices playing through SPU
 * RAM in real time, their loop flags, ENDX, and the SPU2 interrupt raised when
 * a voice's playback (or a transfer) reaches the interrupt address IRQA - the
 * clock sound drivers stream by. Samples are not yet mixed into sound.
 *
 * Voice data is the SPU's ADPCM: 16-byte blocks of 28 samples. A block's
 * second byte holds its flags: bit 0 loop end, bit 1 repeat (go back to the
 * loop start rather than stop), bit 2 loop start. A voice plays at pitch/4096
 * of 48 kHz.
 *
 * libsd's entries (ps2sdk libsd-common.h): a voice parameter or address is
 * (param << 8) | core | voice << 1; a switch or core address (n << 8) | core.
 */

#ifndef PS2HOST_SPU2_H
#define PS2HOST_SPU2_H

#include <cstdint>
#include <functional>
#include <vector>

namespace ps2host {
namespace iop {

class Spu2 {
public:
    using u8 = std::uint8_t;
    using u16 = std::uint16_t;
    using u32 = std::uint32_t;
    using u64 = std::uint64_t;

    Spu2() : ram_(2u << 20) {}

    void SetParam(u32 entry, u32 value);
    u32 GetParam(u32 entry) const;
    void SetSwitch(u32 entry, u32 value);
    u32 GetSwitch(u32 entry) const;
    void SetAddr(u32 entry, u32 value);
    u32 GetAddr(u32 entry) const;
    void SetCoreAttr(u32 entry, u32 value);
    u32 GetCoreAttr(u32 entry) const;

    /* Data into (or out of) SPU RAM at a byte address, as a transfer would. */
    void Write(u32 address, const u8* data, u32 size);
    void Read(u32 address, u8* data, u32 size) const;

    /* Advance playback to `now_us`; `irq(core)` for each interrupt raised. */
    void Advance(u64 now_us, const std::function<void(u32 core)>& irq);

private:
    struct Voice {
        u32 pitch = 0, ssa = 0, lsax = 0, nax = 0;
        u32 adsr1 = 0, adsr2 = 0, voll = 0, volr = 0;
        bool playing = false;
        double position = 0;            // samples into the current block
    };
    struct Core {
        Voice voices[24];
        u32 endx = 0, irqa = 0, attr = 0, esa = 0, eea = 0, tsa = 0;
        u32 switches[16] = {};          // by switch number - 0x13
        bool irq_enabled = false;
    };
    Core cores_[2];
    std::vector<u8> ram_;
    u64 last_us_ = 0;
    double clock_ = 0;               // samples since the SPU2 started

    static u32 CoreOf(u32 entry) { return entry & 1; }
    static u32 VoiceOf(u32 entry) { return ((entry >> 1) & 0x1F) % 24; }
    void CheckIrq(u32 core_hint, u32 from, u32 to, const std::function<void(u32)>& irq);
    std::vector<u32> pending_irqs_;
};

}  // namespace iop
}  // namespace ps2host

#endif  /* PS2HOST_SPU2_H */
