/*
 * The host's run state, shared by its parts: dispatch, the kernel, devices and
 * the report. Not part of the interface a game port uses (host.h).
 */

#ifndef PS2HOST_RUN_H
#define PS2HOST_RUN_H

#include <atomic>
#include <map>
#include <string>
#include <functional>
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

/* A fresh EE register context. VU0's vf0 reads (0, 0, 0, 1) in hardware: macro
   code multiplies by vf0w for a 1.0 (Burnout 3's menu fades do), so it starts
   so, and nothing writes it (lifted::Cop2 drops writes to vf0). */
inline lifted::Context NewContext() {
    lifted::Context c{};
    c.vf[0][3] = 0x3F800000u;
    return c;
}

/* What the run touched, for the report at the end. */
struct Stats {
    std::map<u32, u64> calls;                  /* function -> times called */
    std::map<s32, u64> syscalls;               /* service -> times called */
    std::map<u32, u64> register_reads, register_writes;   /* device register -> count */
    std::vector<u32> stopped_in;               /* the stack when the run stopped */
    std::vector<u32> interrupted_in;           /* ...and, if that was a handler, the thread's */
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
/* In an interrupt handler: the call stack of the thread it interrupted. */
std::vector<u32> InterruptedCalls();
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
/* A packet from the IOP to the EE (a module's sceSifSendCmd): queued, and
   delivered as the SIF0 interrupt at the next safe point. */
void QueueIopPacket(std::vector<u8> packet);
/* Ask for interrupts to be delivered at the running thread's next loop head. */
void RequestDelivery();
/* The RPC-end packet answering an RPC request packet, for a server at
   `server` with receive buffers `buffer`/`cbuffer` (IOP addresses). */
void QueueRpcEnd(const std::vector<u8>& request, u32 server, u32 buffer, u32 cbuffer);
/* The IOP services the engine implements: CD/DVD (cdvd.cpp), ... */
void InstallIopServers();
void InstallFileIo();
void InstallLoadFile();
void InstallDbc();
void InstallIopHeap();
/* Signal an EE semaphore from the host (the IOP finishing a request); the
   running thread is not preempted. */
void SignalSemaphore(u32 id);
/* Run the DMA-completion handlers registered for a channel, as its interrupt. */
void RaiseDmac(u32 channel);
/* A channel's transfer has completed: its interrupt is delivered at the running
   thread's next loop head, or between threads - never inside the store that
   started the transfer, since code goes on to set up for the interrupt. */
void PostDmac(u32 channel);
/* Set (before budgets are spent) when the run must end, so the loop-head hook
   does not take a spent budget for its own request. */
std::atomic<bool>& StopRequested();

/* SDK library routines the host implements, applied by name (sdk.cpp). */
void ApplySdkOverrides(const FunctionEntry* table, std::size_t count);

/* The EE kernel: lifted::Syscall's implementation (kernel.cpp). */
void KernelSyscall(lifted::Context& c);
const char* SyscallName(s32 service);

/* Device registers with hardware meaning (devices.cpp). */
void InstallDevices();
/* Each vertical blank: write the displayed picture to `dir` every `every`
   frames, at most `limit` of them (--frames). */
void SetFrameDumps(const std::string& dir, int every, int limit);
/* The GPU renderer (gpu_bridge.cpp). Shadow: it draws beside the software GS;
   each frame its targets are compared with local memory (and written as BMP
   files into `dump_dir` when that is not empty); ShadowRtt also samples GPU
   targets as textures. Primary (--gpu): it draws instead of the software GS. */
enum class GpuMode { Shadow, ShadowRtt, Primary };
bool StartGpuRenderer(std::string* error, GpuMode mode, bool show);
void GpuShadowFrame(u64 frame, const std::string& dump_dir);
std::string GpuShadowSummary();
/* --until-frame: stop the run when frame N is done (with --virtual-time, the
   same point every time). */
void SetUntilFrame(u64 frame);
bool UntilFrameReached();
/* --gs-trace-frame: trace the GS during one frame. */
void SetGsTraceFrame(long long frame);
/* --gs-dump-frame N: GS local memory, all 4 MB, written to
   <frames dir or .>/gsmem_<N>.bin when frame N is done (with --gpu, as the
   GPU has it) - to set beside another run's (engine/tools/gsmem.py). */
void SetGsDumpFrame(long long frame);
void FrameDone(u64 frame);
/* The window (window.cpp): opened with --window, presented each frame. */
bool OpenWindow(const wchar_t* title, int width, int height);
bool WindowOpen();
bool WindowClosed();
void PresentFrame();
/* What the window shows instead of the software GS's picture (BGRA, as
   Gs::Picture gives it); false falls back to the software picture. */
void SetPictureSource(std::function<bool(std::vector<u32>*, u32*, u32*)> source);
/* Whether the window has the keyboard (input is read only then). */
bool WindowFocused();
/* Whether a key (a Win32 virtual key) is down, on the window's keyboard. */
bool HostKeyDown(int key);
/* Controllers (dbc.cpp): each frame, the devices write their state. */
void DbcFrame();
/* --press: hold `button` (cross, start, up, ...) for 6 frames from `frame`. */
bool ScriptPress(const std::string& button, u64 frame);
/* The EE timers (timers.cpp): their registers, the host time to their next
   interrupt (< 0: none), and raising the interrupts that are due. */
void InstallTimers();
s64 NanosToNextTimerEvent();
void DeliverTimers();
/* The host's clock, in nanoseconds (timers.cpp), which the EE timers, alarms
   and the IOP's time follow. Real time by default. With --virtual-time it is
   the scheduler's: each spent loop budget advances it by a fixed slice and
   waiting skips it to the next event, so a scripted run is the same run on any
   machine, under any load. Chosen before anything reads it. */
u64 ClockNs();
void SetVirtualClock(bool on);
bool VirtualClock();
void AdvanceClock(u64 ns);
/* Run the INTC handlers for `cause`, if it is enabled (threads.cpp). */
void RaiseIntc(u32 cause);
/* Sound (audio.cpp): opened with --audio; the SPU2 hands it 16-bit stereo
   frames at 48 kHz. */
bool OpenAudio();
/* ...and/or recorded to a WAV file (--audio-wav). */
bool OpenWavDump(const std::string& path);
void AudioFrames(const short* frames, unsigned count);

}  // namespace ps2host

#endif  /* PS2HOST_RUN_H */
