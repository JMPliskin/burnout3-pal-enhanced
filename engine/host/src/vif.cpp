/*
 * VIFcode decoding (EE User's Manual, chapter 6). A VIFcode is
 *   bits  0-15 IMMEDIATE, 16-23 NUM, 24-30 CMD, 31 interrupt.
 * Data lengths, in 32-bit words:
 *   STMASK 1; STROW, STCOL 4; MPG NUM*2 (NUM 0 = 256);
 *   DIRECT, DIRECTHL IMMEDIATE*4 (0 = 65536 quadwords);
 *   UNPACK (0x60-0x7F) by its element format, NUM and STCYCL's CL and WL;
 *   every other command none.
 *
 * UNPACK's CMD is 0x60 | m << 4 | vn << 2 | vl: m masks, vn + 1 components of
 * 32 >> vl bits (vn 3, vl 3 is V4-5, 16 bits of RGBA 5-5-5-1). IMMEDIATE holds
 * the address in quadwords (bits 0-9), USN (14: zero-extend, not sign-extend)
 * and FLG (15: relative to TOPS). With CL >= WL each CL quadwords take WL
 * elements and skip the rest; with WL > CL each WL take CL elements and the
 * rest are filled from the row and column registers by the mask.
 */

#include "ps2host/vif.h"

#include <algorithm>
#include <cstring>

#include "ps2host/vu.h"

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

void VifStream::WriteElement(const std::uint32_t value[4], bool from_data) {
    Unpack& u = unpack_;
    if (u.written >= u.num) return;
    std::uint32_t cl = cycle_length_, wl = write_length_;
    if (wl == 0) { wl = 1; cl = std::max(cl, 1u); }
    const std::uint32_t k = u.written++;
    const std::uint32_t address = wl <= cl ? u.address + (k / wl) * cl + (k % wl) : u.address + k;
    const std::uint32_t position = std::min(k % wl, 3u);
    if (!vu) return;
    std::uint32_t* q = vu->Quad(address);
    const bool three = ((u.format >> 2) & 3) == 2;
    for (std::uint32_t f = 0; f < 4; ++f) {
        std::uint32_t m = u.masked ? (mask >> (position * 8 + f * 2)) & 3 : 0;
        if (!from_data && m == 0) m = 1;            // a filling write: the row
        switch (m) {
            case 0: {
                if (three && f == 3) break;         // V3 leaves w as it was
                std::uint32_t v = value[f];
                if (mode == 1) v += row[f];
                else if (mode == 2) row[f] = v = v + row[f];
                q[f] = v;
                break;
            }
            case 1: q[f] = row[f]; break;
            case 2: q[f] = col[position]; break;
            default: break;                         // write-protected
        }
    }
}

void VifStream::UnpackFill() {
    Unpack& u = unpack_;
    if (write_length_ <= cycle_length_) return;
    const std::uint32_t zero[4] = {};
    while (u.written < u.num && (u.written % write_length_) >= cycle_length_) WriteElement(zero, false);
}

void VifStream::UnpackData(std::uint32_t word) {
    Unpack& u = unpack_;
    for (int i = 0; i < 4; ++i) u.bytes.push_back(std::uint8_t(word >> (8 * i)));
    const std::uint32_t vl = u.format & 3, vn = (u.format >> 2) & 3;
    const bool v45 = vn == 3 && vl == 3;
    const std::uint32_t width = 4u >> vl;           // bytes per component
    const std::uint32_t size = v45 ? 2 : (vn + 1) * width;
    std::size_t at = 0;
    while (u.bytes.size() - at >= size && u.written < u.num) {
        const std::uint8_t* b = u.bytes.data() + at;
        std::uint32_t c[4] = {};
        if (v45) {
            const std::uint32_t v = std::uint32_t(b[0]) | std::uint32_t(b[1]) << 8;
            c[0] = (v & 0x1F) << 3;
            c[1] = ((v >> 5) & 0x1F) << 3;
            c[2] = ((v >> 10) & 0x1F) << 3;
            c[3] = ((v >> 15) & 1) << 7;
        } else {
            for (std::uint32_t i = 0; i <= vn; ++i) {
                const std::uint8_t* e = b + i * width;
                if (width == 4) std::memcpy(&c[i], e, 4);
                else if (width == 2) {
                    const std::uint16_t h = std::uint16_t(e[0] | e[1] << 8);
                    c[i] = u.unsigned_ ? h : std::uint32_t(std::int32_t(std::int16_t(h)));
                } else {
                    c[i] = u.unsigned_ ? e[0] : std::uint32_t(std::int32_t(std::int8_t(e[0])));
                }
            }
            if (vn == 0) c[1] = c[2] = c[3] = c[0];             // S: every field
            else if (vn == 1) { c[2] = c[0]; c[3] = c[1]; }     // V2: x y x y
        }
        at += size;
        WriteElement(c, true);
        UnpackFill();
    }
    u.bytes.erase(u.bytes.begin(), u.bytes.begin() + std::ptrdiff_t(at));
}

void VifStream::Start(std::uint32_t pc) {
    if (double_buffered) {
        top = tops;
        dbf ^= 1;
        tops = base + (dbf ? offset : 0);
    }
    itop = itops;
    if (!vu) return;
    vu->top = top;
    vu->itop = itop;
    vu->Run(pc);
    end_pc_ = vu->resume;
}

void VifStream::Word(std::uint32_t word) {
    if (pending_) {
        --pending_;
        ++data_words_;
        const std::uint32_t index = data_index_++;
        switch (data_) {
            case Data::Direct: if (on_direct) on_direct(word); break;
            case Data::Mpg:
                if (vu) {
                    const std::size_t at = (mpg_address_ + 4 * index) & (vu->micro.size() - 1);
                    std::memcpy(vu->micro.data() + at, &word, 4);
                }
                break;
            case Data::Unpack: UnpackData(word); break;
            case Data::Mask: mask = word; break;
            case Data::Row: row[index & 3] = word; break;
            case Data::Col: col[index & 3] = word; break;
            default: break;
        }
        return;
    }
    data_ = Data::None;
    data_index_ = 0;
    ++commands_;
    const std::uint32_t command = (word >> 24) & 0x7F;
    ++by_command_[command];
    const std::uint32_t num = (word >> 16) & 0xFF;
    const std::uint32_t immediate = word & 0xFFFF;
    if (command >= 0x60) {
        Unpack& u = unpack_;
        u.format = command & 0xF;
        u.masked = (command & 0x10) != 0;
        u.unsigned_ = (immediate & 0x4000) != 0;
        u.address = immediate & 0x3FF;
        if (double_buffered && (immediate & 0x8000)) u.address += tops;
        u.num = num ? num : 256;
        u.written = 0;
        u.bytes.clear();
        pending_ = UnpackWords(command & 0xF, u.num);
        data_ = Data::Unpack;
        UnpackFill();
        return;
    }
    switch (command) {
        case 0x01:                                          // STCYCL
            cycle_length_ = immediate & 0xFF;
            write_length_ = immediate >> 8;
            break;
        case 0x02:                                          // OFFSET
            offset = immediate & 0x3FF;
            dbf = 0;
            tops = base;
            break;
        case 0x03: base = immediate & 0x3FF; break;         // BASE
        case 0x04: itops = immediate & 0x3FF; break;        // ITOP
        case 0x05: mode = immediate & 3; break;             // STMOD
        case 0x06:                                          // MSKPATH3
            if (on_mask_path3) on_mask_path3((immediate & 0x8000) != 0);
            break;
        case 0x14: case 0x15: Start(immediate * 8); break;  // MSCAL, MSCALF
        case 0x17: Start(end_pc_); break;                   // MSCNT
        case 0x20: pending_ = 1; data_ = Data::Mask; break; // STMASK
        case 0x30: pending_ = 4; data_ = Data::Row; break;  // STROW
        case 0x31: pending_ = 4; data_ = Data::Col; break;  // STCOL
        case 0x4A:                                          // MPG
            pending_ = (num ? num : 256) * 2;
            mpg_address_ = immediate * 8;
            data_ = Data::Mpg;
            break;
        case 0x50: case 0x51:                               // DIRECT, DIRECTHL
            pending_ = (immediate ? immediate : 0x10000) * 4;
            data_ = Data::Direct;
            break;
        default: break;                                     // NOP, MARK, FLUSH*: nothing waits
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
    if (vu) out += "; VU: " + std::to_string(vu->pairs()) + " instruction pairs, " +
                   std::to_string(vu->kicks()) + " XGKICKs";
    return out;
}

VifStream& Vif1() {
    static VifStream stream;
    return stream;
}

}  // namespace ps2host
