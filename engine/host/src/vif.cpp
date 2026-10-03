/*
 * VIFcode decoding (EE User's Manual, chapter 6). A VIFcode is
 *   bits  0-15 IMMEDIATE, 16-23 NUM, 24-30 CMD, 31 interrupt.
 * Data lengths, in 32-bit words:
 *   STMASK 1; STROW, STCOL 4; MPG NUM*2 (NUM 0 = 256);
 *   DIRECT, DIRECTHL IMMEDIATE*4 (0 = 65536 quadwords);
 *   UNPACK (0x60-0x7F) by its element format, NUM and STCYCL's CL and WL;
 *   every other command none.
 */

#include "ps2host/vif.h"

namespace ps2host {

std::uint32_t VifStream::UnpackWords(std::uint32_t format, std::uint32_t num) const {
    const std::uint32_t vl = format & 3, vn = (format >> 2) & 3;
    // Bits per element: components (vn + 1) of 32 >> vl bits; V4-5 packs 16.
    const std::uint32_t bits = (vn == 3 && vl == 3) ? 16 : (vn + 1) * (32u >> vl);
    // With WL > CL (filling writes) only CL of every WL elements come from data.
    std::uint32_t elements = num;
    if (write_length_ > cycle_length_ && write_length_ != 0) {
        const std::uint32_t partial = num % write_length_;
        elements = cycle_length_ * (num / write_length_) +
                   (partial < cycle_length_ ? partial : cycle_length_);
    }
    return (elements * bits + 31) / 32;
}

void VifStream::Word(std::uint32_t word) {
    if (pending_) {
        --pending_;
        ++data_words_;
        if (direct_ && on_direct) on_direct(word);
        return;
    }
    direct_ = false;
    ++commands_;
    const std::uint32_t command = (word >> 24) & 0x7F;
    ++by_command_[command];
    const std::uint32_t num = (word >> 16) & 0xFF;
    const std::uint32_t immediate = word & 0xFFFF;
    if (command >= 0x60) {
        pending_ = UnpackWords(command & 0xF, num ? num : 256);
        return;
    }
    switch (command) {
        case 0x01:                                          // STCYCL
            cycle_length_ = immediate & 0xFF;
            write_length_ = immediate >> 8;
            break;
        case 0x06:                                          // MSKPATH3
            if (on_mask_path3) on_mask_path3((immediate & 0x8000) != 0);
            break;
        case 0x20: pending_ = 1; break;                     // STMASK
        case 0x30: case 0x31: pending_ = 4; break;          // STROW, STCOL
        case 0x4A: pending_ = (num ? num : 256) * 2; break; // MPG
        case 0x50: case 0x51:                               // DIRECT, DIRECTHL
            pending_ = (immediate ? immediate : 0x10000) * 4;
            direct_ = true;
            break;
        default: break;
    }
}

std::string VifStream::Summary() const {
    static const std::pair<std::uint32_t, const char*> kNames[] = {
        {0x00, "NOP"}, {0x01, "STCYCL"}, {0x02, "OFFSET"}, {0x03, "BASE"}, {0x04, "ITOP"},
        {0x05, "STMOD"}, {0x06, "MSKPATH3"}, {0x07, "MARK"}, {0x10, "FLUSHE"}, {0x11, "FLUSH"},
        {0x13, "FLUSHA"}, {0x14, "MSCAL"}, {0x15, "MSCALF"}, {0x17, "MSCNT"}, {0x20, "STMASK"},
        {0x30, "STROW"}, {0x31, "STCOL"}, {0x4A, "MPG"}, {0x50, "DIRECT"}, {0x51, "DIRECTHL"}};
    std::string out;
    std::uint64_t unpacks = 0;
    for (std::uint32_t c = 0x60; c < 0x80; ++c) unpacks += by_command_[c];
    for (const auto& [code, name] : kNames)
        if (by_command_[code]) out += std::string(" ") + name + " " + std::to_string(by_command_[code]);
    if (unpacks) out += " UNPACK " + std::to_string(unpacks);
    return out;
}

VifStream& Vif1() {
    static VifStream stream;
    return stream;
}

}  // namespace ps2host
