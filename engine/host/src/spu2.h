/*
 * The SPU2, the PS2's sound processor, as libsd drives it (iop_libs.cpp).
 *
 * Voices play through SPU RAM in real time, a sample at a time at 48 kHz:
 * their ADPCM is decoded, shaped by the ADSR envelope and mixed by their
 * volumes into the stereo output (an Output sink - the host's audio device -
 * takes it). Games synchronise with the same machinery: loop flags, ENDX, and
 * the SPU2 interrupt raised when a voice's playback (or a transfer) reaches the
 * interrupt address IRQA - the clock sound drivers stream by.
 *
 * Voice data is the SPU's ADPCM: 16-byte blocks of 28 samples. Byte 0 holds
 * the shift (bits 0-3) and the prediction filter (bits 4-6); byte 1 the flags:
 * bit 0 loop end, bit 1 repeat (go back to the loop start rather than stop),
 * bit 2 loop start. A voice plays at pitch/4096 of 48 kHz.
 *
 * Not modelled yet: reverb, volume sweeps (a sweeping volume plays at its
 * maximum), noise, pitch modulation, and the cores' external and sound-data
 * inputs.
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
    using s16 = std::int16_t;
    using u32 = std::uint32_t;
    using s32 = std::int32_t;
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

    /* Where the mixed output goes: interleaved 16-bit stereo at 48 kHz. */
    using Output = std::function<void(const s16* frames, u32 count)>;
    void SetOutput(Output output) { output_ = std::move(output); }

    /* Exposed for tests: one ADPCM block decoded, with the filter history. */
    static void DecodeBlock(const u8* block, s32 out[28], s32* older, s32* old);

private:
    enum class Phase : u8 { Off, Attack, Decay, Sustain, Release };
    struct Voice {
        u32 pitch = 0, ssa = 0, lsax = 0, nax = 0;
        u32 adsr1 = 0, adsr2 = 0, voll = 0, volr = 0;
        bool playing = false;           // sounding (a stopped voice still runs on)
        u32 counter = 0;                // 4.12 fixed point: samples into the block
        s32 samples[28] = {};           // the current block, decoded
        s32 older = 0, old = 0;         // the decoder's history
        s32 previous = 0;               // the sample before the current one
        Phase phase = Phase::Off;
        s32 level = 0;                  // the envelope, 0-0x7FFF
        u32 envelope_counter = 0;
    };
    struct Core {
        Voice voices[24];
        u32 endx = 0, irqa = 0, attr = 0, esa = 0, eea = 0, tsa = 0;
        u32 switches[16] = {};          // by switch number - 0x13
        u32 mvoll = 0x3FFF, mvolr = 0x3FFF;
        bool irq_enabled = false;
    };
    Core cores_[2];
    std::vector<u8> ram_;
    u64 last_us_ = 0;
    double clock_ = 0;               // samples since the SPU2 started
    double owed_ = 0;                // a fraction of a sample not yet produced
    Output output_;
    std::vector<s16> mixed_;

    static u32 CoreOf(u32 entry) { return entry & 1; }
    static u32 VoiceOf(u32 entry) { return ((entry >> 1) & 0x1F) % 24; }
    void CheckIrq(u32 core_hint, u32 from, u32 to, const std::function<void(u32)>& irq);
    void Start(Voice& v);
    void Decode(Voice& v);
    void NextBlock(Core& c, u32 index, const std::function<void(u32)>& irq);
    void Envelope(Voice& v);
    std::vector<u32> pending_irqs_;
};

}  // namespace iop
}  // namespace ps2host

#endif  /* PS2HOST_SPU2_H */
