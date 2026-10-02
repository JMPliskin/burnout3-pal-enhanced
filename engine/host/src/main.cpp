/*
 * Every game port's entry point. The game supplies its translated functions (the
 * table engine/tools/port.py generates) and its overrides; the rest is shared.
 */

#include "ps2host/host.h"

extern const ps2host::FunctionEntry kGuestFunctions[];
extern const std::size_t kGuestFunctionCount;

/* The game's host implementations of guest functions (its port's overrides.cpp). */
void RegisterGameOverrides();

int main(int argc, char** argv) {
    ps2host::RegisterFunctions(kGuestFunctions, kGuestFunctionCount);
    RegisterGameOverrides();
    return ps2host::Main(argc, argv);
}
