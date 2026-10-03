/*
 * The VIF's command stream (stage one): VIFcodes are decoded so that the words
 * of data after them are skipped rather than read as commands, and the commands
 * with an effect the EE can observe without a VU or GS behind them are applied.
 *
 * So far that is MSKPATH3, which masks GIF PATH3 and shows in GIF_STAT's M3P
 * bit - code that hands PATH3 over waits on it. UNPACK, MPG and DIRECT carry
 * data for VU memory and the GS; it is counted, not yet delivered.
 */

#ifndef PS2HOST_VIF_H
#define PS2HOST_VIF_H

#include <cstdint>
#include <functional>

namespace ps2host {

class VifStream {
public:
    /* Called when MSKPATH3 masks (true) or unmasks (false) PATH3. */
    std::function<void(bool masked)> on_mask_path3;

    /* The next 32-bit word of the stream, from the FIFO or a DMA transfer. */
    void Word(std::uint32_t word);

    std::uint64_t commands() const { return commands_; }
    std::uint64_t data_words() const { return data_words_; }

private:
    std::uint32_t pending_ = 0;     /* data words still owed to the last command */
    std::uint32_t cycle_length_ = 0, write_length_ = 0;     /* STCYCL's CL and WL */
    std::uint64_t commands_ = 0, data_words_ = 0;

    /* Words of data an UNPACK of `num` elements in format `format` reads. */
    std::uint32_t UnpackWords(std::uint32_t format, std::uint32_t num) const;
};

/* The VIF1 stream fed by stores to its FIFO, 0x10005000. */
VifStream& Vif1();

}  // namespace ps2host

#endif  /* PS2HOST_VIF_H */
