/*
 * Every game port's entry point. The game supplies its translated functions (the
 * table engine/tools/port.py generates) and its overrides; the rest is shared.
 */

#include <cstdio>

#include "ps2host/host.h"

#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
#include <intrin.h>
#endif

extern const ps2host::FunctionEntry kGuestFunctions[];
extern const std::size_t kGuestFunctionCount;

/* The game's host implementations of guest functions (its port's overrides.cpp). */
void RegisterGameOverrides();

namespace {

/* The port is built for AVX2 (engine/host/CMakeLists.txt; the VU's lanes and
   the rest take its three-operand forms): a processor without it is told so
   rather than stopped by an illegal instruction somewhere. */
bool HasAvx2() {
#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86)) && defined(__AVX2__)
    int info[4];
    __cpuid(info, 0);
    if (info[0] < 7) return false;
    __cpuid(info, 1);
    const bool osxsave = (info[2] >> 27) & 1, avx = (info[2] >> 28) & 1;
    if (!osxsave || !avx) return false;
    if ((_xgetbv(0) & 6) != 6) return false;        // the OS saves the YMM registers
    __cpuidex(info, 7, 0);
    return (info[1] >> 5) & 1;
#else
    return true;
#endif
}

}  // namespace

int main(int argc, char** argv) {
    if (!HasAvx2()) {
        std::fprintf(stderr, "This build needs a processor with AVX2 (Intel Haswell, AMD Excavator or later).\n");
        return 70;
    }
    ps2host::RegisterFunctions(kGuestFunctions, kGuestFunctionCount);
    RegisterGameOverrides();
    return ps2host::Main(argc, argv);
}
