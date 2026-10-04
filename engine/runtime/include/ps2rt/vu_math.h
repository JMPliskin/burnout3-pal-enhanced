/*
 * The VU's arithmetic model (engine/ps2decomp/vu0.py documents it, and where it
 * departs from the hardware): clean operands, results narrowed to single
 * precision and saturated. Inline, so that the macro-mode COP2 (vu0.cpp) and
 * the micro-mode interpreter (engine/host/src/vu.cpp) compute each lane
 * without a call per step.
 */

#ifndef PS2RT_VU_MATH_H
#define PS2RT_VU_MATH_H

#include <cstring>

#include "ps2rt/types.h"

namespace lifted {

inline constexpr u32 kSign = 0x80000000u;
inline constexpr u32 kMax = 0x7F7FFFFFu;
inline constexpr u32 kOne = 0x3F800000u;
inline constexpr int kRegR = 20, kRegI = 21, kRegQ = 22;

inline u32 Clean(u32 bits) {
    const u32 exponent = bits & 0x7F800000u;
    if (exponent == 0) return bits & kSign;
    if (exponent == 0x7F800000u) return (bits & kSign) | kMax;
    return bits;
}

inline double Value(u32 bits) {
    const u32 cleaned = Clean(bits);
    float value;
    std::memcpy(&value, &cleaned, sizeof value);
    return value;
}

inline u32 Result(double number) {
    if (number != number) return kMax;
    const float narrowed = static_cast<float>(number);
    u32 bits;
    std::memcpy(&bits, &narrowed, sizeof bits);
    if ((bits & 0x7F800000u) == 0x7F800000u) return (bits & kSign) | kMax;
    if ((bits & 0x7F800000u) == 0) return bits & kSign;
    return bits;
}

enum Op { kAdd, kSub, kMul, kMadd, kMsub, kMax_, kMini };

inline u32 Binary(int op, u32 a, u32 b, u32 acc) {
    const double x = Value(a), y = Value(b);
    switch (op) {
        case kAdd: return Result(x + y);
        case kSub: return Result(x - y);
        case kMul: return Result(x * y);
        case kMadd: return Result(Value(acc) + Value(Result(x * y)));
        case kMsub: return Result(Value(acc) - Value(Result(x * y)));
        case kMax_: return x < y ? Clean(b) : Clean(a);
        case kMini: return y < x ? Clean(b) : Clean(a);
    }
    return 0;
}

inline u32 FloatToInt(double number) {
    if (number >= 2147483647.0) return 0x7FFFFFFFu;
    if (number <= -2147483648.0) return 0x80000000u;
    return (u32)(s32)number;
}

}  // namespace lifted

#endif  /* PS2RT_VU_MATH_H */
