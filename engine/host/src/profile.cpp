/* Timed sections for the report (profile.h). */

#include "ps2host/profile.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <chrono>
#include <cstdio>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <dbghelp.h>
#include <intrin.h>
#include <timeapi.h>
#endif

namespace ps2host {

namespace {

using Clock = std::chrono::steady_clock;

// Sections are timed in the processor's timestamp counter - a clock read on
// every entry and exit, hundreds of thousands a frame, which the system clock
// made a tenth of the run - and turned into seconds against the steady clock
// when reported.
std::uint64_t Ticks() {
#if defined(_MSC_VER)
    return __rdtsc();
#else
    return std::uint64_t(Clock::now().time_since_epoch().count());
#endif
}

const Clock::time_point g_start = Clock::now();
const std::uint64_t g_start_ticks = Ticks();
std::uint64_t g_ticks[int(Section::Count)] = {};
// The sections open now, innermost last, and when the innermost resumed.
Section g_stack[16];
int g_depth = 0;
std::uint64_t g_since = 0;

double Elapsed(Clock::time_point from, Clock::time_point to) {
    return std::chrono::duration<double>(to - from).count();
}

}  // namespace

Timed::Timed(Section section) : section_(section) {
    const std::uint64_t now = Ticks();
    if (g_depth > 0) g_ticks[int(g_stack[g_depth - 1])] += now - g_since;
    if (g_depth < 16) g_stack[g_depth] = section;
    ++g_depth;
    g_since = now;
}

Timed::~Timed() {
    const std::uint64_t now = Ticks();
    g_ticks[int(section_)] += now - g_since;
    --g_depth;
    g_since = now;
}

std::string ProfileSummary() {
    static const char* const kNames[] = {"GS drawing", "GS image transfers", "VU1", "VU0",
                                         "GPU bookkeeping", "GPU drawing", "GPU and memory in step",
                                         "DMA and VIF", "IOP", "SPU2"};
    const double total = Elapsed(g_start, Clock::now());
    const double seconds_per_tick = total / double(std::max<std::uint64_t>(1, Ticks() - g_start_ticks));
    std::string out = "time:";
    char text[64];
    std::snprintf(text, sizeof text, " %.1f s;", total);
    out += text;
    for (int i = 0; i < int(Section::Count); ++i) {
        if (i >= int(Section::GpuBridge) && g_ticks[i] == 0) continue;   // the GPU's, when it ran
        const double seconds = double(g_ticks[i]) * seconds_per_tick;
        std::snprintf(text, sizeof text, "%s %s %.1f%%", i ? "," : "", kNames[i],
                      total > 0 ? 100.0 * seconds / total : 0.0);
        out += text;
    }
    return out;
}

// ------------------------------------------------------------------ --sample

namespace {

constexpr int kFrames = 8;              // the function running, and seven callers

struct Sampler {
    std::atomic<bool> stop{false};
    std::thread thread;
    std::mutex mutex;
    std::vector<std::uint64_t> stacks;  // kFrames addresses a sample
};

Sampler& TheSampler() {
    static Sampler sampler;
    return sampler;
}

}  // namespace

#ifdef _WIN32

void StartSampler() {
    Sampler& s = TheSampler();
    HANDLE target = nullptr;
    DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &target,
                    THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT, FALSE, 0);
    if (!target) return;
    timeBeginPeriod(1);
    s.thread = std::thread([&s, target] {
        std::vector<std::uint64_t> local;
        local.reserve(1 << 20);
        while (!s.stop.load()) {
            Sleep(1);
            // While the thread is suspended nothing here may allocate or take a
            // lock it could hold: the walk goes into a fixed array, through
            // the executable's unwind tables.
            std::uint64_t frames[kFrames] = {};
            CONTEXT ctx{};
            ctx.ContextFlags = CONTEXT_FULL;
            if (SuspendThread(target) == DWORD(-1)) break;
            if (GetThreadContext(target, &ctx)) {
                frames[0] = ctx.Rip;
                for (int n = 1; n < kFrames; ++n) {
                    DWORD64 base = 0;
                    PRUNTIME_FUNCTION entry = RtlLookupFunctionEntry(ctx.Rip, &base, nullptr);
                    if (entry) {
                        void* handler = nullptr;
                        DWORD64 frame = 0;
                        RtlVirtualUnwind(UNW_FLAG_NHANDLER, base, ctx.Rip, entry, &ctx, &handler, &frame, nullptr);
                    } else {                // a leaf: its return address is on top of the stack
                        ctx.Rip = *reinterpret_cast<const DWORD64*>(ctx.Rsp);
                        ctx.Rsp += 8;
                    }
                    if (!ctx.Rip) break;
                    frames[n] = ctx.Rip;
                }
            }
            ResumeThread(target);
            local.insert(local.end(), frames, frames + kFrames);
            if (local.size() >= (1u << 16)) {
                std::lock_guard<std::mutex> lock(s.mutex);
                s.stacks.insert(s.stacks.end(), local.begin(), local.end());
                local.clear();
            }
        }
        std::lock_guard<std::mutex> lock(s.mutex);
        s.stacks.insert(s.stacks.end(), local.begin(), local.end());
        CloseHandle(target);
    });
}

std::string SampleReport(int top) {
    Sampler& s = TheSampler();
    if (!s.thread.joinable()) return "";
    s.stop = true;
    s.thread.join();
    timeEndPeriod(1);
    HANDLE process = GetCurrentProcess();
    SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
    if (!SymInitialize(process, nullptr, TRUE)) return "sample: no symbols";
    std::unordered_map<std::uint64_t, std::string> names;
    auto name = [&](std::uint64_t address) -> const std::string& {
        auto found = names.find(address);
        if (found != names.end()) return found->second;
        alignas(SYMBOL_INFO) char buffer[sizeof(SYMBOL_INFO) + 256];
        SYMBOL_INFO* symbol = reinterpret_cast<SYMBOL_INFO*>(buffer);
        symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
        symbol->MaxNameLen = 255;
        DWORD64 displacement = 0;
        std::string text;
        if (SymFromAddr(process, address, &displacement, symbol)) {
            text = symbol->Name;
        } else {
            char hex[32];
            std::snprintf(hex, sizeof hex, "0x%llX", static_cast<unsigned long long>(address));
            text = hex;
        }
        return names.emplace(address, text).first->second;
    };
    std::map<std::string, std::uint64_t> self, total;
    std::map<std::string, std::map<std::string, std::uint64_t>> callers;
    const std::size_t samples = s.stacks.size() / kFrames;
    for (std::size_t i = 0; i < samples; ++i) {
        const std::uint64_t* f = &s.stacks[i * kFrames];
        if (!f[0]) continue;
        const std::string& leaf = name(f[0]);
        ++self[leaf];
        std::string seen[kFrames];
        for (int n = 0; n < kFrames && f[n]; ++n) {
            const std::string& fn = name(f[n]);
            if (std::find(seen, seen + n, fn) == seen + n) ++total[fn];
            seen[n] = fn;
        }
        if (f[1]) ++callers[leaf][name(f[1])];
    }
    SymCleanup(process);
    auto ranked = [](const std::map<std::string, std::uint64_t>& counts) {
        std::vector<std::pair<std::uint64_t, std::string>> list;
        for (const auto& [fn, n] : counts) list.push_back({n, fn});
        std::sort(list.rbegin(), list.rend());
        return list;
    };
    std::string out;
    char line[512];
    std::snprintf(line, sizeof line, "sample: %zu samples (1 ms apart)\n  self:\n", samples);
    out += line;
    const auto by_self = ranked(self);
    for (int i = 0; i < top && i < int(by_self.size()); ++i) {
        const auto& [n, fn] = by_self[i];
        std::string from;
        const auto by_caller = ranked(callers[fn]);
        for (int k = 0; k < 2 && k < int(by_caller.size()); ++k)
            from += (k ? ", " : "  <- ") + by_caller[k].second;
        std::snprintf(line, sizeof line, "  %5.1f%%  %s%s\n", 100.0 * double(n) / double(samples ? samples : 1),
                      fn.substr(0, 90).c_str(), from.substr(0, 160).c_str());
        out += line;
    }
    out += "  total (anywhere in the eight frames):\n";
    const auto by_total = ranked(total);
    for (int i = 0; i < top && i < int(by_total.size()); ++i) {
        std::snprintf(line, sizeof line, "  %5.1f%%  %s\n",
                      100.0 * double(by_total[i].first) / double(samples ? samples : 1),
                      by_total[i].second.substr(0, 120).c_str());
        out += line;
    }
    return out;
}

#else

void StartSampler() {}
std::string SampleReport(int) { return ""; }

#endif

}  // namespace ps2host
