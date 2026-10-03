/*
 * GS local memory layout (GS User's Manual, chapter 8: "Data format in local
 * memory").
 *
 * Memory is 4 MB of 8 KB pages, a page 32 blocks of 256 bytes, a block four
 * columns of 64 bytes. A buffer is a base block (bp) and a width in 64-pixel
 * units (bw); its pages run left to right, then down. Each format has a page
 * size in pixels and orders the blocks within a page, and the pixels within a
 * block, its own way - the tables below. The 8-bit formats PSMT8H, PSMT4HL and
 * PSMT4HH live in the top byte (or nibble) of a PSMCT32 pixel.
 *
 *   format          page       block    pixels per block
 *   PSMCT32/24, Z32 64 x 32    8 x 8    64 words
 *   PSMCT16/16S, Z  64 x 64   16 x 8    128 halfwords
 *   PSMT8           128 x 64  16 x 16   256 bytes
 *   PSMT4           128 x 128 32 x 16   512 nibbles
 */

#include "ps2host/gs.h"

namespace ps2host {
namespace gs {

namespace {

constexpr u32 kMask = (4u << 20) - 1;

const u8 kBlock32[4][8] = {
    {0, 1, 4, 5, 16, 17, 20, 21}, {2, 3, 6, 7, 18, 19, 22, 23},
    {8, 9, 12, 13, 24, 25, 28, 29}, {10, 11, 14, 15, 26, 27, 30, 31}};
const u8 kBlockZ32[4][8] = {
    {24, 25, 28, 29, 8, 9, 12, 13}, {26, 27, 30, 31, 10, 11, 14, 15},
    {16, 17, 20, 21, 0, 1, 4, 5}, {18, 19, 22, 23, 2, 3, 6, 7}};
const u8 kBlock16[8][4] = {
    {0, 2, 8, 10}, {1, 3, 9, 11}, {4, 6, 12, 14}, {5, 7, 13, 15},
    {16, 18, 24, 26}, {17, 19, 25, 27}, {20, 22, 28, 30}, {21, 23, 29, 31}};
const u8 kBlock16S[8][4] = {
    {0, 2, 16, 18}, {1, 3, 17, 19}, {8, 10, 24, 26}, {9, 11, 25, 27},
    {4, 6, 20, 22}, {5, 7, 21, 23}, {12, 14, 28, 30}, {13, 15, 29, 31}};
const u8 kBlockZ16[8][4] = {
    {24, 26, 16, 18}, {25, 27, 17, 19}, {28, 30, 20, 22}, {29, 31, 21, 23},
    {8, 10, 0, 2}, {9, 11, 1, 3}, {12, 14, 4, 6}, {13, 15, 5, 7}};
const u8 kBlockZ16S[8][4] = {
    {24, 26, 8, 10}, {25, 27, 9, 11}, {16, 18, 0, 2}, {17, 19, 1, 3},
    {28, 30, 12, 14}, {29, 31, 13, 15}, {20, 22, 4, 6}, {21, 23, 5, 7}};

// Within a block: the word (32-bit), halfword (16-bit), byte (8-bit) or
// nibble (4-bit) holding pixel (x, y).
const u8 kColumn32[8][8] = {
    {0, 1, 4, 5, 8, 9, 12, 13}, {2, 3, 6, 7, 10, 11, 14, 15},
    {16, 17, 20, 21, 24, 25, 28, 29}, {18, 19, 22, 23, 26, 27, 30, 31},
    {32, 33, 36, 37, 40, 41, 44, 45}, {34, 35, 38, 39, 42, 43, 46, 47},
    {48, 49, 52, 53, 56, 57, 60, 61}, {50, 51, 54, 55, 58, 59, 62, 63}};
const u8 kColumn16[8][16] = {
    {0, 2, 8, 10, 16, 18, 24, 26, 1, 3, 9, 11, 17, 19, 25, 27},
    {4, 6, 12, 14, 20, 22, 28, 30, 5, 7, 13, 15, 21, 23, 29, 31},
    {32, 34, 40, 42, 48, 50, 56, 58, 33, 35, 41, 43, 49, 51, 57, 59},
    {36, 38, 44, 46, 52, 54, 60, 62, 37, 39, 45, 47, 53, 55, 61, 63},
    {64, 66, 72, 74, 80, 82, 88, 90, 65, 67, 73, 75, 81, 83, 89, 91},
    {68, 70, 76, 78, 84, 86, 92, 94, 69, 71, 77, 79, 85, 87, 93, 95},
    {96, 98, 104, 106, 112, 114, 120, 122, 97, 99, 105, 107, 113, 115, 121, 123},
    {100, 102, 108, 110, 116, 118, 124, 126, 101, 103, 109, 111, 117, 119, 125, 127}};
// The first 8 rows; rows 8-15 are these plus 128.
const u8 kColumn8[8][16] = {
    {0, 4, 16, 20, 32, 36, 48, 52, 2, 6, 18, 22, 34, 38, 50, 54},
    {8, 12, 24, 28, 40, 44, 56, 60, 10, 14, 26, 30, 42, 46, 58, 62},
    {33, 37, 49, 53, 1, 5, 17, 21, 35, 39, 51, 55, 3, 7, 19, 23},
    {41, 45, 57, 61, 9, 13, 25, 29, 43, 47, 59, 63, 11, 15, 27, 31},
    {96, 100, 112, 116, 64, 68, 80, 84, 98, 102, 114, 118, 66, 70, 82, 86},
    {104, 108, 120, 124, 72, 76, 88, 92, 106, 110, 122, 126, 74, 78, 90, 94},
    {65, 69, 81, 85, 97, 101, 113, 117, 67, 71, 83, 87, 99, 103, 115, 119},
    {73, 77, 89, 93, 105, 109, 121, 125, 75, 79, 91, 95, 107, 111, 123, 127}};
// The first 8 rows; rows 8-15 are these plus 256.
const u16 kColumn4[8][32] = {
    {0, 8, 32, 40, 64, 72, 96, 104, 2, 10, 34, 42, 66, 74, 98, 106,
     4, 12, 36, 44, 68, 76, 100, 108, 6, 14, 38, 46, 70, 78, 102, 110},
    {16, 24, 48, 56, 80, 88, 112, 120, 18, 26, 50, 58, 82, 90, 114, 122,
     20, 28, 52, 60, 84, 92, 116, 124, 22, 30, 54, 62, 86, 94, 118, 126},
    {65, 73, 97, 105, 1, 9, 33, 41, 67, 75, 99, 107, 3, 11, 35, 43,
     69, 77, 101, 109, 5, 13, 37, 45, 71, 79, 103, 111, 7, 15, 39, 47},
    {81, 89, 113, 121, 17, 25, 49, 57, 83, 91, 115, 123, 19, 27, 51, 59,
     85, 93, 117, 125, 21, 29, 53, 61, 87, 95, 119, 127, 23, 31, 55, 63},
    {192, 200, 224, 232, 128, 136, 160, 168, 194, 202, 226, 234, 130, 138, 162, 170,
     196, 204, 228, 236, 132, 140, 164, 172, 198, 206, 230, 238, 134, 142, 166, 174},
    {208, 216, 240, 248, 144, 152, 176, 184, 210, 218, 242, 250, 146, 154, 178, 186,
     212, 220, 244, 252, 148, 156, 180, 188, 214, 222, 246, 254, 150, 158, 182, 190},
    {129, 137, 161, 169, 193, 201, 225, 233, 131, 139, 163, 171, 195, 203, 227, 235,
     133, 141, 165, 173, 197, 205, 229, 237, 135, 143, 167, 175, 199, 207, 231, 239},
    {145, 153, 177, 185, 209, 217, 241, 249, 147, 155, 179, 187, 211, 219, 243, 251,
     149, 157, 181, 189, 213, 221, 245, 253, 151, 159, 183, 191, 215, 223, 247, 255}};

u32 Page(u32 bp, u32 pages_per_row, u32 page_x, u32 page_y) {
    return bp + 32 * (page_y * (pages_per_row ? pages_per_row : 1) + page_x);
}

}  // namespace

u32 Memory::Address(u32 bp, u32 bw, u32 x, u32 y, u32 psm, u32* nibble) {
    *nibble = 0;
    switch (psm) {
        case PSMCT32: case PSMCT24: case PSMT8H: case PSMT4HL: case PSMT4HH: {
            const u32 block = Page(bp, bw, x / 64, y / 32) + kBlock32[(y % 32) / 8][(x % 64) / 8];
            return (block * 256 + kColumn32[y % 8][x % 8] * 4) & kMask;
        }
        case PSMZ32: case PSMZ24: {
            const u32 block = Page(bp, bw, x / 64, y / 32) + kBlockZ32[(y % 32) / 8][(x % 64) / 8];
            return (block * 256 + kColumn32[y % 8][x % 8] * 4) & kMask;
        }
        case PSMCT16: case PSMCT16S: case PSMZ16: case PSMZ16S: {
            const u8 (*table)[4] = psm == PSMCT16 ? kBlock16 : psm == PSMCT16S ? kBlock16S
                                 : psm == PSMZ16 ? kBlockZ16 : kBlockZ16S;
            const u32 block = Page(bp, bw, x / 64, y / 64) + table[(y % 64) / 8][(x % 64) / 16];
            return (block * 256 + kColumn16[y % 8][x % 16] * 2) & kMask;
        }
        case PSMT8: {
            const u32 block = Page(bp, bw / 2, x / 128, y / 64) + kBlock32[(y % 64) / 16][(x % 128) / 16];
            const u32 row = y % 16;
            return (block * 256 + kColumn8[row % 8][x % 16] + (row >= 8 ? 128 : 0)) & kMask;
        }
        case PSMT4: {
            const u32 block = Page(bp, bw / 2, x / 128, y / 128) + kBlock16[(y % 128) / 16][(x % 128) / 32];
            const u32 row = y % 16;
            const u32 index = kColumn4[row % 8][x % 32] + (row >= 8 ? 256 : 0);
            *nibble = index & 1;
            return (block * 256 + index / 2) & kMask;
        }
        default:
            return (bp * 256 + (y * bw * 64 + x) * 4) & kMask;
    }
}

u32 Memory::Read(u32 bp, u32 bw, u32 x, u32 y, u32 psm) const {
    u32 nibble = 0;
    const u32 a = Address(bp, bw, x, y, psm, &nibble);
    const u8* p = &bytes_[a];
    switch (psm) {
        case PSMCT32: case PSMZ32: return u32(p[0]) | u32(p[1]) << 8 | u32(p[2]) << 16 | u32(p[3]) << 24;
        case PSMCT24: case PSMZ24: return u32(p[0]) | u32(p[1]) << 8 | u32(p[2]) << 16;
        case PSMCT16: case PSMCT16S: case PSMZ16: case PSMZ16S: return u32(p[0]) | u32(p[1]) << 8;
        case PSMT8: return p[0];
        case PSMT4: return nibble ? p[0] >> 4 : p[0] & 15;
        case PSMT8H: return p[3];
        case PSMT4HL: return p[3] & 15;
        case PSMT4HH: return p[3] >> 4;
        default: return 0;
    }
}

void Memory::Write(u32 bp, u32 bw, u32 x, u32 y, u32 psm, u32 value) {
    u32 nibble = 0;
    const u32 a = Address(bp, bw, x, y, psm, &nibble);
    u8* p = &bytes_[a];
    switch (psm) {
        case PSMCT32: case PSMZ32:
            p[0] = u8(value); p[1] = u8(value >> 8); p[2] = u8(value >> 16); p[3] = u8(value >> 24); break;
        case PSMCT24: case PSMZ24:
            p[0] = u8(value); p[1] = u8(value >> 8); p[2] = u8(value >> 16); break;
        case PSMCT16: case PSMCT16S: case PSMZ16: case PSMZ16S:
            p[0] = u8(value); p[1] = u8(value >> 8); break;
        case PSMT8: p[0] = u8(value); break;
        case PSMT4: p[0] = nibble ? u8((p[0] & 0x0F) | (value << 4)) : u8((p[0] & 0xF0) | (value & 15)); break;
        case PSMT8H: p[3] = u8(value); break;
        case PSMT4HL: p[3] = u8((p[3] & 0xF0) | (value & 15)); break;
        case PSMT4HH: p[3] = u8((p[3] & 0x0F) | (value << 4)); break;
        default: break;
    }
}

}  // namespace gs
}  // namespace ps2host
