/*
 * The host's run state, shared by its parts: dispatch, the kernel, devices and
 * the report. Not part of the interface a game port uses (host.h).
 */

#ifndef PS2HOST_RUN_H
#define PS2HOST_RUN_H

#include <map>
#include <string>
#include <vector>

#include "ps2host/host.h"
#include "ps2rt/lifted.h"

namespace ps2host {

/* Thrown when the guest asks for something the host does not provide yet: the
   run stops there and the report says what it was. */
struct Unimplemented {
    std::string what;
};

/* Thrown when the guest program exits through the kernel. */
struct GuestExit {
    s32 code;
};

/* Run a guest function: an override if there is one, else the translated code. */
void Dispatch(lifted::Context& c, u32 address);

/* What the run touched, for the report at the end. */
struct Stats {
    std::map<u32, u64> calls;                  /* function -> times called */
    std::map<s32, u64> syscalls;               /* service -> times called */
    std::map<u32, u64> register_reads, register_writes;   /* device register -> count */
    std::vector<u32> stopped_in;               /* the stack when the run stopped */
    std::vector<std::string> notes;            /* one line per first-time event */
    /* Code run where it was copied to: (run address, the executable's copy). */
    std::vector<std::pair<u32, u32>> discovered;
};
Stats& RunStats();

/* Whether --trace was given: log every call and kernel service, not only the first. */
bool Tracing();

/* A one-time note: logged the first time `key` is seen, then only counted. */
void Note(const std::string& key, const std::string& text);

/* Threads (threads.cpp): run the program's first thread from `entry` under the
   kernel's scheduler; returns only by throwing why the run ended. */
void RunThreads(const lifted::Context& initial, u32 entry, s64 loop_budget);
/* The running thread's guest call stack (or the interrupt handlers'). */
std::vector<u32>& CurrentCalls();
/* Every guest register context: the threads' and the interrupt handlers'. */
void ForEachContext(void (*visit)(lifted::Context&));
/* Each thread's state, for the report. */
std::string ThreadReport();
/* The thread, semaphore and interrupt services; false for any other service. */
bool ThreadSyscall(lifted::Context& c, s32 number, u64& result);

/* The IOP over SIF (iop.cpp): the transfers SifSetDma queues, and delivering
   one queued answer to the EE (false when none waits). */
void SifDma(u32 transfers, u32 count);
bool DeliverIopReply();
/* The IOP services the engine implements: CD/DVD (cdvd.cpp), ... */
void InstallIopServers();
void InstallFileIo();
/* Signal an EE semaphore from the host (the IOP finishing a request); the
   running thread is not preempted. */
void SignalSemaphore(u32 id);
/* Run the DMA-completion handlers registered for a channel, as its interrupt. */
void RaiseDmac(u32 channel);

/* SDK library routines the host implements, applied by name (sdk.cpp). */
void ApplySdkOverrides(const FunctionEntry* table, std::size_t count);

/* The EE kernel: lifted::Syscall's implementation (kernel.cpp). */
void KernelSyscall(lifted::Context& c);
const char* SyscallName(s32 service);

/* Device registers with hardware meaning (devices.cpp). */
void InstallDevices();

}  // namespace ps2host

#endif  /* PS2HOST_RUN_H */
