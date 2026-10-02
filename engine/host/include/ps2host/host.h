/*
 * The native host: runs a game's translated code on a PC.
 *
 * The lifted functions (engine/runtime/include/ps2rt/lifted.h) talk to their
 * world through a handful of hooks - memory reads and writes, calls, syscalls.
 * During verification those hooks lead back to the Python interpreter; here they
 * are served natively: guest memory is a set of flat arrays, a call is a lookup
 * in a table of translated functions, a syscall is a host implementation of the
 * EE kernel service.
 *
 * A game port links three things: the engine (runtime + this host), the game's
 * translated functions with the dispatch table engine/tools/port.py generates
 * for them, and the game's own overrides - host implementations that replace a
 * guest function at its address, which is how hardware access is redirected and
 * how a port's enhancements hook in.
 */

#ifndef PS2HOST_HOST_H
#define PS2HOST_HOST_H

#include <cstddef>
#include <string>

#include "ps2rt/lifted.h"

namespace ps2host {

using GuestFunction = void (*)(lifted::Context&);

/* One translated function: its guest address, its code, when known its name, and
   its size in bytes - which identifies a copy of it made at run time. */
struct FunctionEntry {
    u32 address;
    GuestFunction function;
    const char* name;
    u32 size;
};

/* The game's translated functions, sorted by address (port.py generates this). */
void RegisterFunctions(const FunctionEntry* table, std::size_t count);

/* A host implementation that runs instead of the guest function at `address`.
   Overrides take precedence over translated functions; registering twice
   replaces the earlier one. */
void Override(u32 address, GuestFunction function, const char* name);

/* The name of the function at `address`, or null. */
const char* FunctionName(u32 address);

struct Options {
    std::string elf_path;          /* the game's executable, from the user's own disc */
    s64 loop_budget = 2000000000;  /* loop iterations before a run is called stuck */
    bool trace_calls = false;      /* log every call, not just the first of each */
    int report_limit = 40;         /* how many entries each report section lists */
    int timeout_seconds = 30;      /* stop and report a run that goes on longer */
    std::string discover_path;     /* where to record code found copied at run time */
};

/* Load the executable, run it from its entry point, and report how far it got.
   Returns the process exit code: 0 when the guest exited, 2 when it stopped on
   something the host does not provide yet. */
int Run(const Options& options);

/* Command-line front end shared by every game port: parses the options and runs. */
int Main(int argc, char** argv);

}  // namespace ps2host

#endif  /* PS2HOST_HOST_H */
