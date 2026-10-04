/*
 * PATH3 held back by VIF1's MSKPATH3 (devices.cpp).
 *
 * GIF DMA data waits here while PATH3 is masked. A mask takes effect only at
 * the end of the GIF packet PATH3 is in - the one whose tag has EOP set - so
 * unmasking and masking again at once lets exactly one EOP-ended group of
 * packets through: Release(false). Release(true) lets all of it go (time
 * passing with PATH3 unmasked, or FLUSHA waiting for it).
 */

#ifndef PS2HOST_PATH3_H
#define PS2HOST_PATH3_H

#include <algorithm>
#include <cstdint>
#include <deque>

namespace ps2host {

class Path3Queue {
public:
    void Push(std::uint32_t word) { held_.push_back(word); }
    bool empty() const { return held_.empty(); }
    std::size_t size() const { return held_.size(); }

    /* Hands words to `sink`, up to the end of the first packet with EOP set
       (or all of them). */
    template <typename Sink>
    void Release(bool all, Sink&& sink) {
        std::size_t end = held_.size();
        if (!all) {
            std::size_t at = 0;
            while (at + 4 <= held_.size()) {
                const std::uint64_t low = std::uint64_t(held_[at]) | std::uint64_t(held_[at + 1]) << 32;
                const std::uint64_t nloop = low & 0x7FFF, eop = (low >> 15) & 1;
                const std::uint64_t flg = (low >> 58) & 3, nreg = (low >> 60) ? (low >> 60) : 16;
                const std::uint64_t qwords = flg == 0 ? nloop * nreg : flg == 1 ? (nloop * nreg + 1) / 2 : nloop;
                at += 4 + std::size_t(qwords) * 4;
                if (eop) break;
            }
            end = std::min(at, held_.size());
        }
        for (std::size_t i = 0; i < end; ++i) sink(held_[i]);
        held_.erase(held_.begin(), held_.begin() + std::ptrdiff_t(end));
    }

private:
    std::deque<std::uint32_t> held_;
};

}  // namespace ps2host

#endif  /* PS2HOST_PATH3_H */
