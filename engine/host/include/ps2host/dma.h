/*
 * The EE DMA controller's source transfers (EE User's Manual, chapter 5): what a
 * channel sends to its peripheral once its STR bit is set, walked at once.
 *
 * Normal mode sends QWC quadwords from MADR. Source chain mode follows DMAtags:
 *   tag bits 0-15 QWC, 28-30 ID, 31 IRQ, 32-62 ADDR, 63 SPR (ADDR in scratchpad)
 *   ID 0 refe  data at ADDR, then end      4 refs  as ref (stall control aside)
 *      1 cnt   data after the tag, the        5 call  data after the tag; push
 *              next tag after the data                the address after it; next
 *      2 next  data after the tag, next               tag at ADDR
 *              tag at ADDR                    6 ret   data after the tag; pop the
 *      3 ref   data at ADDR, next tag                 next tag (end when empty)
 *              after this one                 7 end   data after the tag, then end
 * With CHCR.TTE the tag's upper 64 bits go to the peripheral ahead of its data,
 * which is how VIF chains carry VIFcodes in their tags. With CHCR.TIE a tag with
 * IRQ set ends the transfer after its data.
 */

#ifndef PS2HOST_DMA_H
#define PS2HOST_DMA_H

#include <cstdint>
#include <functional>

namespace ps2host {

struct DmaChannelState {
    std::uint32_t chcr, madr, qwc, tadr, asr0, asr1;
};

struct DmaOutcome {
    std::uint64_t quadwords = 0;    /* data quadwords sent, tags excluded */
    std::uint32_t tags = 0;
    bool ended = false;             /* reached an end tag (or IRQ with TIE, or normal mode) */
};

/* Sends the channel's data to `word`, one 32-bit word at a time, reading guest
   memory, and leaves the registers as the hardware would at the end (MADR, QWC,
   TADR, ASR, CHCR's TAG field). Stops after `max_tags` tags, so a chain that
   loops is reported rather than followed forever. */
DmaOutcome SendDma(DmaChannelState& channel, const std::function<void(std::uint32_t)>& word,
                   std::uint32_t max_tags = 1u << 16);

}  // namespace ps2host

#endif  /* PS2HOST_DMA_H */
