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

namespace ps2gpu {
class Display;
}

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
/* Whether a Note under `key` would be logged (so its text is worth making). */
bool Noting(const std::string& key);

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
/* A channel a device moves at its own pace (the IPU's) has finished: its
   D_STAT bit, and its interrupt as PostDmac (devices.cpp). */
void DmaChannelEnded(u32 channel);
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
void SetFrameDumps(const std::string& dir, int every, int limit, u64 from = 0);
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
/* --frame-times N: every N frames, a line with the frame rate over them. */
void SetFrameTimes(unsigned every);
/* The GS on a thread of its own (gs_thread.cpp; --gs-inline keeps it on the
   EE's): what feeds the GS goes through these, in order; SyncGs waits until
   the GS has taken all of it, before anything reads the GS's state. */
void StartGsThread();
void StopGsThread();
void SyncGs();
void GsQword(int path, u64 low, u64 high);
void GsWord(int path, u32 word);
void GsWords(int path, const u32* words, u32 count, u32 from = ~0u);
void GsPrivileged(u32 address, u64 value);
/* The end of frame N, in the GS's stream: the handler (the GPU bridge's, when
   the GPU draws: hand the frame to the GPU, show its picture) runs on the GS
   thread once the frame's packets are taken, so the EE need not wait for
   them. False when there is no handler: the EE syncs and does it itself. */
void SetGsFrameHandler(std::function<void(u64)> handler);
/* VIF1, VU1 and what feeds the GS on a thread of their own, in real time
   (vif1_worker.cpp): Vif1Run queues a command the worker runs in order (at
   once without the worker, or on it); JoinVif1 waits until all have run. */
void StartVif1Worker();
/* Whether real time runs VIF1 on its worker (default yes; --vif1-sync: no). */
void SetVif1Worker(bool on);
/* The calling thread runs the game in real time (the EE's, the GS thread,
   VIF1's worker): Windows is asked not to slow it for power (on a hybrid
   processor, not to park it on an efficiency core) and to run it above
   normal priority. */
void FastThread();
bool Vif1Async();
bool OnVif1Worker();
bool Vif1Busy();
void Vif1Run(std::function<void()> command);
/* Vif1Run for a frame's end: the EE waits while the worker is two frames
   behind. */
void Vif1RunFrame(std::function<void()> command);
/* `why` and `address` name the wait for PS2HOST_JOIN_LOG. */
void JoinVif1(const char* why = nullptr, u32 address = 0);
/* GIF_STAT read while the worker has work (memory.cpp): what it will hold
   once the worker has run what is queued, as the EE can tell (devices.cpp),
   checked on the worker in turn; false when it cannot tell, and the read
   waits. Vif1Settled: a read with the queue run (the EE's). */
bool Vif1Guessable(u32 address, u32 size);
bool Vif1Guess(u32 address, u32* value);
void Vif1Settled(u32 address, u32 value);
/* Guesses made, and those found wrong. */
void Vif1Guesses(u64* guesses, u64* wrong);
/* --check-guesses (virtual time): every status read compared with the guess
   real time would have made, as if the worker were always behind. */
void SetCheckGuesses(bool on);
/* How long the worker has worked, in all (microseconds). */
u64 Vif1BusyMicroseconds();
/* How long the EE has waited for it, in all (microseconds): for its queue to
   empty, for it to catch up a frame, for room in the queue. */
void Vif1WaitMicroseconds(u64* join, u64* frame, u64* full);
bool GsFrame(u64 frame);
/* How long the GS thread has worked, in all (microseconds). */
u64 GsBusyMicroseconds();
/* How long the GPU has worked on what --gpu handed it, in all (microseconds;
   0 without it, or where the GPU keeps no time). */
u64 GpuBusyMicroseconds();
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
/* The window's events alone (PresentFrame without the picture, which the GS
   thread presents): false once it has been closed. */
bool PumpWindow();
/* The window's display, null when there is none (or no Vulkan one). */
ps2gpu::Display* WindowDisplay();
/* What the window shows instead of the software GS's picture (BGRA, as
   Gs::Picture gives it); false falls back to the software picture. */
void SetPictureSource(std::function<bool(std::vector<u32>*, u32*, u32*)> source);
/* Whether the window has the keyboard (input is read only then). */
bool WindowFocused();
/* Whether a key (a Win32 virtual key) is down, on the window's keyboard. */
bool HostKeyDown(int key);
/* The window's game controller, as a DualShock 2 reads (window.cpp): false
   when none is plugged in. `buttons` bit n is input n held; the sticks are
   right X, Y, left X, Y (0x80 centred); l2, r2 the triggers' travel. */
struct HostPadState {
    u16 buttons = 0;
    u8 sticks[4] = {0x80, 0x80, 0x80, 0x80};
    u8 l2 = 0, r2 = 0;
};
bool HostPad(HostPadState* state);
/* Controllers (dbc.cpp): each frame, the devices write their state. */
void DbcFrame();
/* The DualShock 2's buttons as the game last read them (bit n: input n held,
   libpad's order - 3 is Start): the keyboard, the game controller and
   --press together. For hooks that act on a button as the game would. */
u16 PadButtonsHeld();
/* --press: hold `button` (cross, start, up, ...) for 6 frames from `frame`. */
bool ScriptPress(const std::string& button, u64 from, u64 to);
/* Real time: vertical blanks that came while the last was still undelivered (threads.cpp). */
u64 LostVblanks();
/* Real time: how long the EE has slept with every thread waiting - its
   headroom (threads.cpp). */
u64 IdleNanoseconds();
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
