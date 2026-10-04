#include "ps2host/host.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <mutex>
#include <set>
#include <thread>
#include <unordered_map>

#include "ps2host/disc.h"
#include "ps2host/gs.h"
#include "ps2host/iop_vm.h"
#include "ps2host/vif.h"
#include "ps2host/vu.h"
#include "ps2host/memory.h"
#include "ps2host/run.h"

namespace ps2host {

namespace {

const FunctionEntry* g_table = nullptr;
std::size_t g_count = 0;
u32 g_image_start = 0, g_image_end = 0;       // the executable's bytes, as loaded
std::unordered_map<u32, FunctionEntry> g_overrides;
bool g_trace = false;
bool g_all_notes = false;
std::vector<u32> g_watch;
std::set<std::string> g_noted;

const FunctionEntry* Find(u32 address) {
    auto found = g_overrides.find(address);
    if (found != g_overrides.end()) return &found->second;
    const FunctionEntry* end = g_table + g_count;
    const FunctionEntry* at = std::lower_bound(
        g_table, end, address, [](const FunctionEntry& e, u32 a) { return e.address < a; });
    return (at != end && at->address == address) ? at : nullptr;
}

std::string Hex(u32 value) {
    char text[16];
    std::snprintf(text, sizeof text, "0x%08X", value);
    return text;
}

std::string Describe(u32 address) {
    const char* name = FunctionName(address);
    return name ? Hex(address) + " " + name : Hex(address);
}

// The running thread's guest call stack, kept as the host unwinds through
// translated code.
struct Frame {
    explicit Frame(u32 address) : calls(CurrentCalls()) { calls.push_back(address); }
    ~Frame() { calls.pop_back(); }
    std::vector<u32>& calls;
};

}  // namespace

namespace {

// Code the program copied elsewhere and runs there - start-up code installs
// routines in kernel RAM as syscall handlers. A call to an address with no
// translation is matched against the program's functions by content: a copy of
// one, byte for byte, runs that function's translation. The copies are
// position-independent (their branches are relative and their calls absolute, so
// they reach the same targets), so the translation behaves the same there.
const FunctionEntry* FindCopy(u32 address) {
    static std::unordered_map<u32, const FunctionEntry*> copies;
    auto known = copies.find(address);
    if (known != copies.end()) return known->second;
    Memory& memory = GuestMemory();
    const FunctionEntry* found = nullptr;
    const u8* here = memory.Translate(address, 8);
    for (std::size_t i = 0; here && i < g_count && !found; ++i) {
        const FunctionEntry& e = g_table[i];
        if (!e.function || e.size < 8) continue;
        const u8* there = memory.Translate(address, e.size);
        const u8* original = memory.Translate(e.address, e.size);
        if (there && original && std::memcmp(there, original, e.size) == 0) found = &e;
    }
    copies[address] = found;
    if (found) {
        Note("copy " + Hex(address), "code at " + Hex(address) + " is a copy of " +
             Describe(found->address) + "; running its translation");
    }
    return found;
}

// Where in the loaded executable the code at `address` came from, or 0: the
// executable is searched for the bytes there. Code kept as data and copied out at
// run time (a kernel extension, an overlay) has no translation yet; finding its
// source lets engine/tools/port.py translate it for the next build.
u32 FindSource(u32 address) {
    // Code in the executable itself that no function starts at - a tail call's
    // target the analysis missed - is its own source.
    if (address >= g_image_start && address < g_image_end) return address;
    constexpr u32 kPattern = 32;
    Memory& memory = GuestMemory();
    const u8* here = memory.Translate(address, kPattern);
    const u8* image = memory.Translate(g_image_start, g_image_end - g_image_start);
    if (!here || !image || g_image_end - g_image_start < kPattern) return 0;
    const u8* end = image + (g_image_end - g_image_start) - kPattern;
    for (const u8* at = image; at <= end; at += 4) {
        const u32 source = g_image_start + u32(at - image);
        if (source != address && std::memcmp(at, here, kPattern) == 0) return source;
    }
    return 0;
}

}  // namespace

void RegisterFunctions(const FunctionEntry* table, std::size_t count) {
    g_table = table;
    g_count = count;
    // Before the game's own overrides, which may replace these in turn.
    ApplySdkOverrides(table, count);
}

void Override(u32 address, GuestFunction function, const char* name) {
    g_overrides[address] = FunctionEntry{address, function, name, 0};
}

const char* FunctionName(u32 address) {
    const FunctionEntry* entry = Find(address);
    return entry ? entry->name : nullptr;
}

bool Tracing() { return g_trace; }

Stats& RunStats() {
    static Stats stats;
    return stats;
}

void Note(const std::string& key, const std::string& text) {
    if (g_noted.insert(key).second) {
        RunStats().notes.push_back(text);
        std::fprintf(stderr, "[host] %s\n", text.c_str());
    } else if (g_all_notes) {
        std::fprintf(stderr, "[host] %s (again)\n", text.c_str());
    }
}

void Dispatch(lifted::Context& c, u32 address) {
    const FunctionEntry* entry = Find(address);
    if (!entry || !entry->function) {
        if (const FunctionEntry* copy = FindCopy(address)) entry = copy;
    }
    if (!entry || !entry->function) {
        if (const u32 source = FindSource(address)) {
            RunStats().discovered.emplace_back(address, source);
            throw Unimplemented{"a call to " + Hex(address) + ", code " +
                                (source == address ? std::string("no function covers")
                                                   : "copied from " + Hex(source) + " at run time") +
                                ", not translated yet (port.py translates it for the next build)"};
        }
        throw Unimplemented{"a call to " + Hex(address) + ", which has no translated function"};
    }
    u64& count = RunStats().calls[address];
    if (++count == 1 || g_trace)
        std::fprintf(stderr, "[call] %s\n", Describe(address).c_str());
    if (std::find(g_watch.begin(), g_watch.end(), address) != g_watch.end()) {
        std::fprintf(stderr, "[watch] %s a0=%016llX a1=%016llX a2=%016llX a3=%016llX "
                     "f12=%08X f13=%08X ra=%08X s0=%08X s1=%08X sp=%08X gp=%08X t0=%08X\n",
                     Describe(address).c_str(),
                     (unsigned long long)c.r[4], (unsigned long long)c.r[5],
                     (unsigned long long)c.r[6], (unsigned long long)c.r[7], c.f[12], c.f[13],
                     unsigned(c.r[31]), unsigned(c.r[16]), unsigned(c.r[17]), unsigned(c.r[29]),
                     unsigned(c.r[28]), unsigned(c.r[8]));
    }
    Frame frame(address);
    try {
        entry->function(c);
    } catch (...) {
        // The innermost frame keeps the stack as it was where the run stopped;
        // unwinding empties it on the way out.
        if (RunStats().stopped_in.empty()) {
            RunStats().stopped_in = CurrentCalls();
            RunStats().interrupted_in = InterruptedCalls();
        }
        throw;
    }
    if (std::find(g_watch.begin(), g_watch.end(), address) != g_watch.end())
        std::fprintf(stderr, "[watch] %s returned v0=%016llX f0=%08X s0=%08X s1=%08X sp=%08X\n",
                     Describe(address).c_str(), (unsigned long long)c.r[2], c.f[0],
                     unsigned(c.r[16]), unsigned(c.r[17]), unsigned(c.r[29]));
}

namespace {

// ELF32 little-endian, the PS2's executable format: load every PT_LOAD segment.
u32 LoadElf(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) throw Unimplemented{"cannot open the executable " + path};
    std::vector<u8> data((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    auto u16at = [&](std::size_t at) { u16 v; std::memcpy(&v, &data.at(at + 1) - 1, 2); return v; };
    auto u32at = [&](std::size_t at) { u32 v; std::memcpy(&v, &data.at(at + 3) - 3, 4); return v; };
    if (data.size() < 52 || std::memcmp(data.data(), "\x7F" "ELF", 4) != 0)
        throw Unimplemented{path + " is not an ELF file"};
    const u32 entry = u32at(24), phoff = u32at(28);
    const u16 phentsize = u16at(42), phnum = u16at(44);
    Memory& memory = GuestMemory();
    for (u16 i = 0; i < phnum; ++i) {
        const std::size_t ph = phoff + std::size_t(i) * phentsize;
        if (u32at(ph) != 1) continue;                       // PT_LOAD
        const u32 offset = u32at(ph + 4), vaddr = u32at(ph + 8);
        const u32 filesz = u32at(ph + 16), memsz = u32at(ph + 20);
        if (!memsz) continue;
        u8* target = memory.Translate(vaddr, memsz);
        if (!target || std::size_t(offset) + filesz > data.size())
            throw Unimplemented{"a segment of " + path + " lies outside guest memory"};
        std::memcpy(target, data.data() + offset, filesz);
        std::memset(target + filesz, 0, memsz - filesz);
        if (filesz && !g_image_start) {
            g_image_start = vaddr;
            g_image_end = vaddr + filesz;
        }
    }
    return entry;
}

void Report(const Options& options, const std::string& outcome) {
    const Stats& stats = RunStats();
    std::printf("\n=== host report ===\n%s\n", outcome.c_str());
    std::printf("functions executed: %zu distinct\n", stats.calls.size());
    const std::vector<u32>& stack = stats.stopped_in;
    if (!stack.empty()) {
        std::printf("guest call stack where it stopped (innermost last):\n");
        const std::size_t from = stack.size() > std::size_t(options.report_limit)
                                     ? stack.size() - options.report_limit : 0;
        for (std::size_t i = from; i < stack.size(); ++i)
            std::printf("  %s\n", Describe(stack[i]).c_str());
    }
    if (!stats.interrupted_in.empty()) {
        std::printf("in an interrupt handler; the thread it interrupted was in:\n");
        const std::vector<u32>& thread = stats.interrupted_in;
        const std::size_t from = thread.size() > std::size_t(options.report_limit)
                                     ? thread.size() - options.report_limit : 0;
        for (std::size_t i = from; i < thread.size(); ++i)
            std::printf("  %s\n", Describe(thread[i]).c_str());
    }
    std::printf("threads:\n%s%s", ThreadReport().c_str(), iop::Report().c_str());
    for (u32 address : options.peek) {
        std::printf("memory at 0x%08X:", unsigned(address));
        for (u32 i = 0; i < 64; i += 4) {
            if (i % 16 == 0) std::printf("\n  +%02X", unsigned(i));
            const u8* bytes = GuestMemory().Translate(address + i, 4);
            u32 word = 0;
            if (bytes) std::memcpy(&word, bytes, 4);
            std::printf(bytes ? "  %08X" : "  --------", unsigned(word));
        }
        std::printf("\n");
    }
    std::printf("VIF1 commands:%s\n", Vif1().Summary().c_str());
    std::printf("GS: %llu primitives, %llu pixels written\n",
                static_cast<unsigned long long>(gs::TheGs().primitives()),
                static_cast<unsigned long long>(gs::TheGs().pixels_drawn()));
    std::printf("%s\n", gs::TheGs().Summary().c_str());
    if (!GpuShadowSummary().empty()) std::printf("%s\n", GpuShadowSummary().c_str());
    std::printf("kernel services used:\n");
    for (const auto& [service, count] : stats.syscalls)
        std::printf("  %4d  %-24s x%llu\n", service, SyscallName(service),
                    (unsigned long long)count);
    auto registers = [&](const char* title, const std::map<u32, u64>& seen) {
        std::printf("%s: %zu\n", title, seen.size());
        int shown = 0;
        for (const auto& [address, count] : seen) {
            if (shown++ == options.report_limit) break;
            std::printf("  0x%08X x%llu\n", address, (unsigned long long)count);
        }
    };
    {
        // Where the time goes: the functions called most - a loop that never
        // ends shows here as the functions it keeps calling.
        std::vector<std::pair<u64, u32>> hot;
        for (const auto& [address, count] : stats.calls) hot.push_back({count, address});
        std::sort(hot.rbegin(), hot.rend());
        std::printf("most called:\n");
        for (std::size_t i = 0; i < hot.size() && i < std::size_t(options.report_limit); ++i)
            std::printf("  %-40s x%llu\n", Describe(hot[i].second).c_str(), (unsigned long long)hot[i].first);
    }
    registers("device registers read", stats.register_reads);
    registers("device registers written", stats.register_writes);
    for (const auto& [run, source] : stats.discovered)
        std::printf("discovered: code at %s is a copy of %s\n", Hex(run).c_str(), Hex(source).c_str());
    if (!options.discover_path.empty()) {
        // Merged with what earlier runs found: each run stops at the first one.
        std::FILE* out = std::fopen(options.discover_path.c_str(), "a");
        if (out) {
            for (const auto& [run, source] : stats.discovered)
                std::fprintf(out, "0x%08X 0x%08X\n", run, source);
            std::fclose(out);
        }
    }
}

}  // namespace

namespace {

// Stops a run that goes on too long, so it ends with a report rather than being
// killed without one: the running context's loop budget is exhausted, and the
// translated code stops at its next loop head (lifted::Tick) with the guest call
// stack intact.
class Watchdog {
public:
    explicit Watchdog(int seconds) : thread_([this, seconds] {
        std::unique_lock<std::mutex> lock(mutex_);
        if (!done_.wait_for(lock, std::chrono::seconds(seconds), [this] { return finished_; })) {
            fired = true;
            StopRequested() = true;
            ForEachContext([](lifted::Context& each) {
                reinterpret_cast<std::atomic<s64>*>(&each.budget)->store(-1);
            });
        }
    }) {}
    ~Watchdog() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            finished_ = true;
        }
        done_.notify_all();
        thread_.join();
    }
    std::atomic<bool> fired{false};

private:
    std::mutex mutex_;
    std::condition_variable done_;
    bool finished_ = false;
    std::thread thread_;
};

}  // namespace

int Run(const Options& options) {
    g_trace = options.trace_calls;
    g_all_notes = options.all_notes;
    GuestMemory().WatchWrites(options.watch_writes);
    if (options.window) OpenWindow(L"PS2 port", 1280, 896);
    iop::SetTrace(options.iop_trace);
    gs::TheGs().trace = options.gs_trace;
    SetGsTraceFrame(options.gs_trace_frame);
    if (options.gpu_shadow) {
        std::string error;
        if (!StartGpuShadow(&error)) std::fprintf(stderr, "--gpu-shadow: %s\n", error.c_str());
    }
    bool sound = false;
    if (options.audio) {
        sound = OpenAudio();
        if (!sound) std::fprintf(stderr, "no sound device: running silent\n");
    }
    if (!options.audio_wav.empty()) {
        if (OpenWavDump(options.audio_wav)) sound = true;
        else std::fprintf(stderr, "cannot write %s\n", options.audio_wav.c_str());
    }
    if (sound) iop::EnableSound();
    for (const auto& [button, frame] : options.presses)
        if (!ScriptPress(button, frame)) std::fprintf(stderr, "--press: no button %s\n", button.c_str());
    if (!options.frames_dir.empty()) SetFrameDumps(options.frames_dir, options.frames_every, 200);
    g_watch = options.watch;
    InstallDevices();
    InstallIopServers();
    lifted::Context c = NewContext();
    Watchdog watchdog(options.timeout_seconds);
    std::string outcome;
    int code = 2;
    try {
        if (!options.disc_path.empty() && !GuestDisc().Open(options.disc_path))
            throw Unimplemented{"cannot open the disc image " + options.disc_path};
        const u32 entry = LoadElf(options.elf_path);
        // The kernel enters a program with a stack at the top of RAM; the program's
        // own start-up code then asks for its real one (SetupThread).
        c.r[29] = kRdramSize - 0x10;
        c.r[31] = 0;
        std::fprintf(stderr, "[host] entry %s\n", Describe(entry).c_str());
        RunThreads(c, entry, options.loop_budget);
    } catch (const GuestExit& exit) {
        outcome = "the guest exited with code " + std::to_string(exit.code);
        code = 0;
    } catch (const Unimplemented& missing) {
        outcome = "stopped: " + missing.what;
    } catch (const MemoryFault& fault) {
        outcome = std::string("stopped: a ") + (fault.write ? "write" : "read") +
                  " at " + Hex(fault.address) + ", outside guest memory";
    } catch (const lifted::Abort& abort) {
        static const char* reasons[] = {"", "a trap", "a loop that did not finish within the budget",
                                        "a callee that failed", "a memory fault", "a COP2 operation"};
        const char* reason = abort.reason < 6 ? reasons[abort.reason] : "an abort";
        if (abort.reason == 2 && watchdog.fired)
            reason = "the time limit, in the loop";
        outcome = std::string("stopped: ") + reason + " at " + Hex(abort.address);
        if (abort.reason == 2 && watchdog.fired && abort.address == 0)
            outcome = "stopped: the time limit, between threads (no thread was in a loop)";
    }
    Report(options, outcome);
    return code;
}

int Main(int argc, char** argv) {
    Options options;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--trace") options.trace_calls = true;
        else if (arg == "--notes") options.all_notes = true;
        else if (arg == "--iop-trace") options.iop_trace = true;
        else if (arg == "--window") options.window = true;
        else if (arg == "--gs-trace") options.gs_trace = true;
        else if (arg == "--gpu-shadow") options.gpu_shadow = true;
        else if (arg == "--gs-trace-frame" && i + 1 < argc) options.gs_trace_frame = std::stoll(argv[++i]);
        else if (arg == "--audio") options.audio = true;
        else if (arg == "--audio-wav" && i + 1 < argc) options.audio_wav = argv[++i];
        else if (arg == "--press" && i + 1 < argc) {
            const std::string spec = argv[++i];
            const std::size_t at = spec.find('@');
            if (at != std::string::npos)
                options.presses.push_back({spec.substr(0, at), std::stoull(spec.substr(at + 1))});
        }
        else if (arg == "--frames" && i + 1 < argc) options.frames_dir = argv[++i];
        else if (arg == "--frames-every" && i + 1 < argc) options.frames_every = std::stoi(argv[++i]);
        else if (arg == "--budget" && i + 1 < argc) options.loop_budget = std::stoll(argv[++i]);
        else if (arg == "--limit" && i + 1 < argc) options.report_limit = std::stoi(argv[++i]);
        else if (arg == "--timeout" && i + 1 < argc) options.timeout_seconds = std::stoi(argv[++i]);
        else if (arg == "--discover" && i + 1 < argc) options.discover_path = argv[++i];
        else if (arg == "--disc" && i + 1 < argc) options.disc_path = argv[++i];
        else if (arg == "--watch" && i + 1 < argc)
            options.watch.push_back(u32(std::stoul(argv[++i], nullptr, 16)));
        else if (arg == "--watch-write" && i + 1 < argc)
            options.watch_writes.push_back(u32(std::stoul(argv[++i], nullptr, 16)));
        else if (arg == "--peek" && i + 1 < argc)
            options.peek.push_back(u32(std::stoul(argv[++i], nullptr, 16)));
        else if (arg.rfind("--", 0) != 0) options.elf_path = arg;
        else {
            std::fprintf(stderr, "unknown option %s\n", arg.c_str());
            return 64;
        }
    }
    if (options.elf_path.empty()) {
        std::fprintf(stderr, "usage: %s <executable from the game disc> [--trace] [--notes] [--watch addr] [--peek addr] "
                             "[--budget N] [--limit N] [--timeout seconds]\n", argv[0]);
        return 64;
    }
    return Run(options);
}

}  // namespace ps2host

// The hooks lifted code calls through.
namespace lifted {

void Call(Context& c, u32 target) { ps2host::Dispatch(c, target); }

void TailCall(Context& c, u32 target, u32 /*return_address*/, u32 /*entry_sp*/) {
    // The guest jumps and never comes back here; natively, the callee runs and
    // the translated function returns straight after (it emits `return;`).
    ps2host::Dispatch(c, target);
}

void Syscall(Context& c) { ps2host::KernelSyscall(c); }

// vcallms: VU0's registers are the ones macro mode uses, so the microprogram
// runs on the calling thread's and leaves its results there.
void Vcallms(Context& c, u32 address) {
    ps2host::Vu& vu = ps2host::Vu0();
    std::memcpy(vu.regs.vf, c.vf, sizeof c.vf);
    std::memcpy(vu.regs.vc, c.vc, sizeof c.vc);
    std::memcpy(vu.regs.vacc, c.vacc, sizeof c.vacc);
    vu.Run(address);
    std::memcpy(c.vf, vu.regs.vf, sizeof c.vf);
    std::memcpy(c.vc, vu.regs.vc, sizeof c.vc);
    std::memcpy(c.vacc, vu.regs.vacc, sizeof c.vacc);
}

}  // namespace lifted

// Derived code's hooks: a guest stack for out-parameters, and pointer calls.
namespace generated {

namespace {
u32 g_stack = 0x01F00000u;      // below the guest stack the kernel hands out
}

u64 StackEnter(u32 size) { g_stack -= size; return g_stack; }
void StackLeave(u32 size) { g_stack += size; }

u64 CallPointer(u32 address, u64, u64, u64, u64, u64, u64, u64, u64,
                u64, u64, u64, u64, u64, u64, u64, u64) {
    throw ps2host::Unimplemented{"a derived pointer call to " + std::to_string(address)};
}

}  // namespace generated
