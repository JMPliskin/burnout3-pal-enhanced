/*
 * EE threads, semaphores and interrupts, as the EE kernel schedules them.
 *
 * Each guest thread is a host fiber with its own register context and native
 * stack; translated code takes its context as an argument, so it runs unchanged
 * on any of them. Scheduling follows the kernel: the ready thread with the best
 * (lowest) priority runs, first-come among equals, and keeps running until it
 * blocks (WaitSema on zero, SleepThread), yields (RotateThreadReadyQueue), exits,
 * or makes a better thread ready - there is no time slicing.
 *
 * Interrupts run the handlers the game registered, in interrupt context, at a
 * safe point: the running thread's next loop head (lifted::loop_head_hook, which
 * a spent budget reaches - the host spends it to get control there), or between
 * threads. Two sources:
 *   - DMA completion (PostDmac), posted by the transfer and delivered after the
 *     store that started it, since code goes on to set up for its interrupt;
 *   - the vertical blank, start and end, 60 times a second in real time, as the
 *     display raises it whatever the processor is doing - code busy-waiting on
 *     work a vblank handler does depends on it. When every thread waits, frames
 *     are delivered at once instead, so idle time costs nothing. Runs with an
 *     explicit --budget have no real-time vblank, so they are deterministic.
 * If no thread becomes ready even after many frames, the program is deadlocked
 * here, and the run stops with each thread's state in the report.
 *
 * A fault inside a fiber cannot unwind across fibers, so it is caught there and
 * rethrown by the scheduler, on the host's own stack.
 */

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <exception>
#include <thread>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "ps2host/host.h"
#include "ps2host/iop_vm.h"
#include "ps2host/memory.h"
#include "ps2host/run.h"

namespace ps2host {

namespace {

enum class State { Dormant, Ready, Running, Waiting, Sleeping };

const char* StateName(State s) {
    switch (s) {
        case State::Dormant: return "dormant";
        case State::Ready: return "ready";
        case State::Running: return "running";
        case State::Waiting: return "waiting on a semaphore";
        case State::Sleeping: return "sleeping";
    }
    return "?";
}

struct Thread {
    u32 id = 0;
    u32 entry = 0, stack = 0, stack_size = 0, gp = 0;
    s32 priority = 0;
    State state = State::Dormant;
    u64 queued = 0;                 // order among ready threads of equal priority
    u32 waiting_on = 0;             // the semaphore, while Waiting
    s32 wakeups = 0;                // WakeupThread calls not yet consumed by a sleep
    lifted::Context context{};
    std::vector<u32> calls;         // the guest call stack, for the report
    LPVOID fiber = nullptr;
};

struct Semaphore {
    s32 count = 0, max = 0;
};

struct Handler {
    u32 cause, function, argument;      // cause: the INTC cause, or the DMA channel
};

// INTC causes the frame delivers: vertical blank start and end.
constexpr u32 kVblankStart = 2, kVblankEnd = 3;
// Frames that may pass with every thread waiting before the run is called stuck.
constexpr u64 kIdleFrames = 600;

struct Scheduler {
    std::map<u32, std::unique_ptr<Thread>> threads;
    u32 next_thread = 1;
    Thread* current = nullptr;
    LPVOID host_fiber = nullptr;
    u64 counter = 0;
    std::exception_ptr stop;
    std::map<u32, Semaphore> semaphores;
    u32 next_semaphore = 1;
    std::map<u32, Handler> handlers;        // INTC handlers, by id
    std::map<u32, Handler> dmac_handlers;   // DMA-completion handlers, by id
    u32 next_handler = 1;
    u32 intc_mask = 0;
    u32 dmac_mask = 0;
    s64 loop_budget = 0;
    u64 frames = 0;
    bool in_interrupt = false;
    lifted::Context interrupt_context{};
    u32 pending_dmac = 0;                   // channels whose interrupt waits
    bool realtime = false;                  // a vblank timer runs (unlimited budget)
    std::atomic<bool> vblank_due{false};    // set by the timer, taken by LoopHead
    std::atomic<lifted::Context*> running{nullptr};     // the thread on the processor
    bool delivery_requested = false;        // the current thread's budget was zeroed
    s64 saved_budget = 0;                   // ...and this is what it was
    std::vector<u32> interrupt_calls;
};

Scheduler& S() {
    static Scheduler scheduler;
    return scheduler;
}

void MakeReady(Thread& t) {
    t.state = State::Ready;
    t.queued = ++S().counter;
}

Thread* Find(u32 id) {
    auto found = S().threads.find(id);
    return found == S().threads.end() ? nullptr : found->second.get();
}

// Give the processor back to the scheduler; returns when this thread runs again.
void GiveWay() {
    Scheduler& s = S();
    if (s.in_interrupt || !s.current) return;     // handlers never block
    SwitchToFiber(s.host_fiber);
}

// Called after a thread became ready: run it now if it is better than this one.
void Preempt(Thread& woken) {
    Scheduler& s = S();
    if (s.in_interrupt || !s.current || woken.priority >= s.current->priority) return;
    MakeReady(*s.current);
    GiveWay();
}

void CALLBACK ThreadMain(void* parameter) {
    Thread& t = *static_cast<Thread*>(parameter);
    try {
        Dispatch(t.context, t.entry);
        t.state = State::Dormant;               // returning from its entry ends a thread
    } catch (...) {
        S().stop = std::current_exception();
    }
    SwitchToFiber(S().host_fiber);              // never resumed
}

Thread* PickReady() {
    Thread* best = nullptr;
    for (auto& [id, t] : S().threads) {
        if (t->state != State::Ready) continue;
        if (!best || t->priority < best->priority ||
            (t->priority == best->priority && t->queued < best->queued))
            best = t.get();
    }
    return best;
}

// Run the handlers registered for `cause` among `handlers`, in interrupt context.
bool RunHandlers(const std::map<u32, Handler>& handlers, u32 cause) {
    Scheduler& s = S();
    bool ran = false;
    for (const auto& [id, h] : handlers) {
        if (h.cause != cause) continue;
        lifted::Context& c = s.interrupt_context;
        c.r[4] = cause;
        c.r[5] = h.argument;
        c.r[29] = 0x01F80000u;                  // an interrupt stack clear of the threads'
        c.r[31] = 0;
        // The kernel leaves $gp as the interrupted code had it: the program's.
        if (s.current) c.r[28] = s.current->context.r[28];
        else if (!s.threads.empty()) c.r[28] = s.threads.begin()->second->context.r[28];
        c.budget = StopRequested() ? -1 : s.loop_budget;     // a handler stops too
        const bool nested = s.in_interrupt;
        s.in_interrupt = true;
        Dispatch(c, h.function);
        s.in_interrupt = nested;
        ran = true;
    }
    return ran;
}

// One frame's interrupts, delivered to the handlers registered for them.
bool DeliverFrame() {
    Scheduler& s = S();
    ++s.frames;
    iop::Run();                             // the IOP's timers and sound interrupts
    bool ran = false;
    for (u32 cause : {kVblankStart, kVblankEnd})
        if (s.intc_mask & (1u << cause)) ran |= RunHandlers(s.handlers, cause);
    return ran;
}

void DeliverPendingDmac() {
    Scheduler& s = S();
    while (s.pending_dmac) {
        u32 channel = 0;
        while (!((s.pending_dmac >> channel) & 1)) ++channel;
        s.pending_dmac &= s.pending_dmac - 1;
        RaiseDmac(channel);
    }
}

void SpendBudget(lifted::Context& c) {
    reinterpret_cast<std::atomic<s64>*>(&c.budget)->store(0);
}

// Raises the vertical blank in real time: marks a frame due and spends the
// running thread's budget, so its next loop head delivers it.
void VblankTimer() {
    using clock = std::chrono::steady_clock;
    const auto period = std::chrono::microseconds(16683);      // NTSC, 59.94 Hz
    auto next = clock::now() + period;
    while (!StopRequested()) {
        std::this_thread::sleep_until(next);
        next += period;
        Scheduler& s = S();
        s.vblank_due = true;
        if (lifted::Context* c = s.running.load()) SpendBudget(*c);
    }
}

// lifted::loop_head_hook: a spent budget is either this host's request to
// deliver interrupts, or the end of the run.
void LoopHead(lifted::Context& c, u32 address) {
    Scheduler& s = S();
    if (StopRequested() || s.in_interrupt || !s.current || &c != &s.current->context)
        throw lifted::Abort{address, 2};
    const bool frame = s.vblank_due.exchange(false);
    const bool requested = s.delivery_requested;
    // With a real-time timer a budget can be spent for a frame another loop head
    // already took; without one, a spent budget that nobody asked for is the end.
    if (!frame && !requested && !s.realtime) throw lifted::Abort{address, 2};
    s.delivery_requested = false;
    c.budget = requested ? s.saved_budget : s.loop_budget;
    if (StopRequested()) c.budget = -1;         // the watchdog fired meanwhile
    if (frame) DeliverFrame();
    while (DeliverIopReply()) {
    }
    DeliverPendingDmac();
    // Returning from the interrupt, a thread its handler woke may run first.
    Thread* best = PickReady();
    if (best && best->priority < s.current->priority) {
        MakeReady(*s.current);
        GiveWay();
    }
}

}  // namespace

std::atomic<bool>& StopRequested() {
    static std::atomic<bool> stop{false};
    return stop;
}

void RaiseDmac(u32 channel) {
    Scheduler& s = S();
    const std::string key = "dmac " + std::to_string(channel);
    if (!(s.dmac_mask & (1u << channel))) {
        Note(key + " masked", "DMAC: channel " + std::to_string(channel) +
                              " completed while its interrupt is disabled");
        return;
    }
    if (RunHandlers(s.dmac_handlers, channel))
        Note(key, "DMAC: channel " + std::to_string(channel) + " interrupt delivered");
    else
        Note(key + " none", "DMAC: channel " + std::to_string(channel) + " interrupt has no handler");
}

void PostDmac(u32 channel) {
    S().pending_dmac |= 1u << channel;
    RequestDelivery();
}

void RequestDelivery() {
    Scheduler& s = S();
    if (s.in_interrupt || !s.current || s.delivery_requested) return;
    lifted::Context& c = s.current->context;
    s.saved_budget = c.budget;
    s.delivery_requested = true;
    c.budget = 0;
}

std::vector<u32>& CurrentCalls() {
    Scheduler& s = S();
    if (s.in_interrupt || !s.current) return s.interrupt_calls;
    return s.current->calls;
}

std::vector<u32> InterruptedCalls() {
    Scheduler& s = S();
    return s.in_interrupt && s.current ? s.current->calls : std::vector<u32>();
}

void ForEachContext(void (*visit)(lifted::Context&)) {
    for (auto& [id, t] : S().threads) visit(t->context);
    visit(S().interrupt_context);
}

void RunThreads(const lifted::Context& initial, u32 entry, s64 loop_budget) {
    Scheduler& s = S();
    s.loop_budget = loop_budget;
    lifted::loop_head_hook = LoopHead;
    s.realtime = loop_budget == INT64_MAX;
    if (s.realtime) std::thread(VblankTimer).detach();
    s.host_fiber = ConvertThreadToFiber(nullptr);
    auto main = std::make_unique<Thread>();
    main->id = s.next_thread++;
    main->entry = entry;
    main->context = initial;
    main->context.budget = loop_budget;
    main->fiber = CreateFiber(1 << 20, ThreadMain, main.get());
    Thread& first = *main;
    s.threads[first.id] = std::move(main);
    MakeReady(first);
    u64 idle = 0;
    for (;;) {
        if (s.stop) std::rethrow_exception(s.stop);
        if (StopRequested()) throw lifted::Abort{0, 2};     // the time limit, between threads
        // Answers from the IOP arrive between threads, as interrupts.
        while (DeliverIopReply()) {
        }
        DeliverPendingDmac();
        Thread* next = PickReady();
        if (!next) {
            if (first.state == State::Dormant)
                throw GuestExit{0};                 // the program's first thread returned
            DeliverFrame();
            if (++idle > kIdleFrames) {
                std::string states;
                for (auto& [id, t] : s.threads)
                    states += "\n    thread " + std::to_string(id) + ": " + StateName(t->state);
                throw Unimplemented{"every thread is waiting, and " + std::to_string(kIdleFrames) +
                                    " frames of interrupts woke none:" + states};
            }
            continue;
        }
        idle = 0;
        s.current = next;
        next->state = State::Running;
        s.running = &next->context;
        SwitchToFiber(next->fiber);
        s.running = nullptr;
        s.current = nullptr;
        if (s.vblank_due.exchange(false)) DeliverFrame();
        if (s.stop) std::rethrow_exception(s.stop);
    }
}

std::string ThreadReport() {
    std::string out;
    for (auto& [id, t] : S().threads) {
        char line[160];
        std::snprintf(line, sizeof line, "  thread %u: %s, priority %d, entry 0x%08X\n", id,
                      StateName(t->state), t->priority, t->entry);
        out += line;
        // Where it is: the innermost calls of its guest stack.
        const std::size_t from = t->calls.size() > 10 ? t->calls.size() - 10 : 0;
        for (std::size_t i = from; i < t->calls.size(); ++i) {
            const char* name = FunctionName(t->calls[i]);
            std::snprintf(line, sizeof line, "      0x%08X %s\n", t->calls[i], name ? name : "");
            out += line;
        }
    }
    return out;
}

void SignalSemaphore(u32 id) {
    Scheduler& s = S();
    auto found = s.semaphores.find(id);
    if (found == s.semaphores.end()) return;
    Thread* taker = nullptr;
    for (auto& [tid, t] : s.threads) {
        if (t->state == State::Waiting && t->waiting_on == id &&
            (!taker || t->priority < taker->priority ||
             (t->priority == taker->priority && t->queued < taker->queued)))
            taker = t.get();
    }
    if (taker) MakeReady(*taker);
    else found->second.count++;
}

// The thread, semaphore and interrupt services; false for any other.
bool ThreadSyscall(lifted::Context& c, s32 number, u64& result) {
    Scheduler& s = S();
    Memory& m = GuestMemory();
    const u32 a0 = u32(c.r[4]), a1 = u32(c.r[5]);
    switch (number) {
        case 0x20: {                                 // CreateThread(param*)
            auto t = std::make_unique<Thread>();
            t->id = s.next_thread++;
            // struct ThreadParam { status, func, stack, stackSize, gpReg, initPriority }
            t->entry = u32(m.Read(a0 + 4, 4));
            t->stack = u32(m.Read(a0 + 8, 4));
            t->stack_size = u32(m.Read(a0 + 12, 4));
            t->gp = u32(m.Read(a0 + 16, 4));
            t->priority = s32(m.Read(a0 + 20, 4));
            result = t->id;
            s.threads[t->id] = std::move(t);
            return true;
        }
        case 0x22: {                                 // StartThread(id, arg)
            Thread* t = Find(a0);
            if (!t || t->state != State::Dormant) { result = u64(-1); return true; }
            t->context = lifted::Context{};
            t->context.r[4] = a1;
            t->context.r[28] = t->gp;
            t->context.r[29] = t->stack + t->stack_size - 0x2A0;
            t->context.budget = s.loop_budget;
            t->calls.clear();
            if (t->fiber) DeleteFiber(t->fiber);
            t->fiber = CreateFiber(1 << 20, ThreadMain, t);
            MakeReady(*t);
            result = a0;
            Preempt(*t);
            return true;
        }
        case 0x23: case 0x24:                        // ExitThread / ExitDeleteThread
            if (s.current) {
                s.current->state = State::Dormant;
                GiveWay();
            }
            result = 0;
            return true;
        case 0x2F:                                   // GetThreadId
            result = s.current ? s.current->id : 1;
            return true;
        case 0x29: case -0x29: {                     // ChangeThreadPriority(id, priority)
            Thread* t = a0 == 0 ? s.current : Find(a0);
            if (!t) { result = u64(-1); return true; }
            result = u32(t->priority);
            t->priority = s32(a1);
            // A thread that is no longer the best gives way.
            if (t == s.current) {
                if (Thread* better = PickReady(); better && better->priority < t->priority) {
                    MakeReady(*t);
                    GiveWay();
                }
            }
            return true;
        }
        case 0x2B: case -0x2B: {                     // RotateThreadReadyQueue(priority)
            if (s.current && s.current->priority == s32(a0) && !s.in_interrupt) {
                MakeReady(*s.current);
                GiveWay();
            }
            result = 0;
            return true;
        }
        case 0x32:                                   // SleepThread
            if (s.current) {
                if (s.current->wakeups > 0) {
                    s.current->wakeups--;
                } else {
                    s.current->state = State::Sleeping;
                    GiveWay();
                }
            }
            result = 0;
            return true;
        case 0x33: case 0x34: case -0x34: {          // WakeupThread / iWakeupThread
            Thread* t = Find(a0);
            if (!t) { result = u64(-1); return true; }
            if (t->state == State::Sleeping) {
                MakeReady(*t);
                Preempt(*t);
            } else {
                t->wakeups++;
            }
            result = 0;
            return true;
        }
        case 0x40: {                                 // CreateSema(param*): count, max, init
            Semaphore sema;
            sema.count = s32(m.Read(a0 + 8, 4));     // struct SemaParam { count, max_count, init_count, ... }
            sema.max = s32(m.Read(a0 + 4, 4));
            const u32 id = s.next_semaphore++;
            s.semaphores[id] = sema;
            result = id;
            return true;
        }
        case 0x41:                                   // DeleteSema
            s.semaphores.erase(a0);
            result = a0;
            return true;
        case 0x42: case 0x43: case -0x42: case -0x43: {   // SignalSema / iSignalSema
            auto found = s.semaphores.find(a0);
            if (found == s.semaphores.end()) { result = u64(-1); return true; }
            // The best thread waiting on it takes the signal; else the count rises.
            Thread* taker = nullptr;
            for (auto& [id, t] : s.threads) {
                if (t->state == State::Waiting && t->waiting_on == a0 &&
                    (!taker || t->priority < taker->priority ||
                     (t->priority == taker->priority && t->queued < taker->queued)))
                    taker = t.get();
            }
            if (taker) {
                MakeReady(*taker);
                Preempt(*taker);
            } else {
                found->second.count++;
            }
            result = a0;
            return true;
        }
        case 0x44: {                                 // WaitSema(id)
            auto found = s.semaphores.find(a0);
            if (found == s.semaphores.end()) { result = u64(-1); return true; }
            if (found->second.count > 0) {
                found->second.count--;
            } else if (s.current && !s.in_interrupt) {
                s.current->state = State::Waiting;
                s.current->waiting_on = a0;
                s.current->queued = ++s.counter;
                GiveWay();
            }
            result = a0;
            return true;
        }
        case 0x45: case -0x45: {                     // PollSema / iPollSema
            auto found = s.semaphores.find(a0);
            if (found == s.semaphores.end() || found->second.count <= 0) {
                result = u64(-1);
                return true;
            }
            found->second.count--;
            result = a0;
            return true;
        }
        case 0x10: case -0x10: {                     // AddIntcHandler(cause, handler, next, arg)
            const u32 id = s.next_handler++;
            s.handlers[id] = Handler{a0, a1, u32(c.r[7])};
            result = id;
            return true;
        }
        case 0x11: case -0x11:                       // RemoveIntcHandler(cause, id)
            s.handlers.erase(a1);
            result = 0;
            return true;
        case 0x12: case -0x12: {                     // AddDmacHandler(channel, handler, next, arg)
            const u32 id = s.next_handler++;
            s.dmac_handlers[id] = Handler{a0, a1, u32(c.r[7])};
            result = id;
            return true;
        }
        case 0x13: case -0x13:                       // RemoveDmacHandler(channel, id)
            s.dmac_handlers.erase(a1);
            result = 0;
            return true;
        case 0x16: case -0x16:                       // _EnableDmac(channel)
            s.dmac_mask |= 1u << (a0 & 31);
            result = 1;
            return true;
        case 0x17: case -0x17:                       // _DisableDmac(channel)
            s.dmac_mask &= ~(1u << (a0 & 31));
            result = 1;
            return true;
        case 0x14: case -0x14:                       // _EnableIntc(cause)
            s.intc_mask |= 1u << (a0 & 31);
            result = 1;
            return true;
        case 0x15: case -0x15:                       // _DisableIntc(cause)
            s.intc_mask &= ~(1u << (a0 & 31));
            result = 1;
            return true;
        default:
            return false;
    }
}

}  // namespace ps2host
