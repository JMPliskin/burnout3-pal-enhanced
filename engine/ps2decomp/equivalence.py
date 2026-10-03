"""Checking structured code against the plain lifted code, natively.

The plain lifted form of a function is a statement per instruction, verified
against the interpreter; the structured forms (structure.py) are rewrites of it -
loops, folded temporaries, elided saves. Verifying the rewrites against the
interpreter on random inputs proved too weak: two rewrite bugs survived it,
because the paths they broke were never taken. This compares each structured
form with its plain form instead, both compiled into one native harness, over
thousands of random inputs each - a hundred times more cases than the
interpreter can afford.

Both forms run on the same random register state and the same memory: every
byte reads as a hash of its address until written. A call reaches a stub whose
effect - the result registers and caller-saved registers it leaves - depends on
the call's target and its position in the sequence only: a folded form may
leave a dead temporary unset in an argument register the callee never reads,
so the stub must not read them either. What is compared is what a caller can
see: the memory written outside the function's own stack frame, the sequence
of call targets, and the registers the calling convention preserves or returns
(all of them for the forms that keep every register).
"""

from __future__ import annotations

import ctypes
import os
import re

from . import cppcheck

CASES = 2000
# A function whose cases take longer than this stops early with what it has run.
SECONDS_PER_FUNCTION = 1.0
BATCH = 500

HARNESS = r'''
#include <chrono>
#include <cstring>
#include <unordered_map>
#include <vector>
#include "ps2rt/lifted.h"

namespace {
std::unordered_map<u32, u8> g_written;
std::vector<u64> g_calls;           // each call: its target, then its argument registers
u64 g_seed = 0;

u8 Byte(u32 a) {
    auto f = g_written.find(a);
    if (f != g_written.end()) return f->second;
    u64 x = (u64(a) * 0x9E3779B97F4A7C15ull) ^ g_seed;
    x ^= x >> 29; x *= 0xBF58476D1CE4E5B9ull; x ^= x >> 32;
    return u8(x);
}
// A case that runs away - random registers can make a loop sweep megabytes, or
// call without end - stops, as both forms then do at the same point.
struct Runaway {};
constexpr std::size_t kMaxWritten = 1 << 16, kMaxCalls = 4096 * 17;

u64 Load(u32 a, u32 n) { u64 v = 0; for (u32 i = 0; i < n; ++i) v |= u64(Byte(a + i)) << (8 * i); return v; }
void Store(u32 a, u64 v, u32 n) {
    for (u32 i = 0; i < n; ++i) g_written[a + i] = u8(v >> (8 * i));
    if (g_written.size() > kMaxWritten) throw Runaway{};
}
u64 Mix(u64 x) { x ^= x >> 31; x *= 0x94D049BB133111EBull; x ^= x >> 29; return x; }
}

namespace generated {
u64 ReadS8(u32 a) { return u64(s64(s8(Load(a, 1)))); }
u64 ReadU8(u32 a) { return Load(a, 1); }
u64 ReadS16(u32 a) { return u64(s64(s16(Load(a, 2)))); }
u64 ReadU16(u32 a) { return Load(a, 2); }
u64 ReadS32(u32 a) { return u64(s64(s32(Load(a, 4)))); }
u64 ReadU32(u32 a) { return Load(a, 4); }
u64 ReadU64(u32 a) { return Load(a, 8); }
void Write8(u32 a, u32 v) { Store(a, v, 1); }
void Write16(u32 a, u32 v) { Store(a, v, 2); }
void Write32(u32 a, u32 v) { Store(a, v, 4); }
void Write64(u32 a, u64 v) { Store(a, v, 8); }
u64 StackEnter(u32) { return 0; }
void StackLeave(u32) {}
u64 CallPointer(u32, u64, u64, u64, u64, u64, u64, u64, u64, u64, u64, u64, u64, u64, u64, u64, u64) { return 0; }
}

namespace lifted {
// A callee: what it leaves depends on its target, its place in the sequence and
// its arguments. A register live at a call is never folded away (a call reads
// every register), so the arguments must match exactly between the forms.
void Call(Context& c, u32 target) {
    if (g_calls.size() > kMaxCalls) throw Runaway{};
    u64 h = Mix(g_seed ^ (u64(target) << 20) ^ g_calls.size());
    g_calls.push_back(target);
    for (int r : {4, 5, 6, 7, 8, 9, 10, 11}) { g_calls.push_back(c.r[r]); h = Mix(h ^ c.r[r]); }
    for (int f = 12; f < 20; ++f) { g_calls.push_back(c.f[f]); h = Mix(h ^ c.f[f]); }
    for (int r : {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 24, 25})
        c.r[r] = Mix(h + r);
    c.hi = Mix(h + 40); c.lo = Mix(h + 41);
    for (int f = 0; f < 20; ++f) c.f[f] = u32(Mix(h + 60 + f));
    c.fcc = u32(h & 1);
}
void TailCall(Context& c, u32 target, u32, u32) { Call(c, target); }
void Syscall(Context& c) { c.r[2] = Mix(g_seed ^ c.r[3]); }
}

using Fn = void (*)(lifted::Context&);
struct Pair { u32 address; Fn plain, structured; int abi; };
%(declarations)s
static const Pair kPairs[] = {
%(pairs)s
};

namespace {
struct Outcome {
    int status;                      // 0 ran, 1 aborted (trap, budget, fault)
    lifted::Context c;
    std::unordered_map<u32, u8> written;
    std::vector<u64> calls;
};

Outcome Run(Fn f, const lifted::Context& start, u64 seed) {
    Outcome o{0, start, {}, {}};
    g_written.clear(); g_calls.clear(); g_seed = seed;
    o.c.budget = 4000;
    try { f(o.c); } catch (...) { o.status = 1; }
    o.written = g_written; o.calls = g_calls;
    return o;
}

// Memory written outside the function's own frame: below the stack pointer it
// entered with is its scratch space - spills, saved registers - which the
// structured forms keep in locals instead, and no caller can see.
std::unordered_map<u32, u8> Visible(const std::unordered_map<u32, u8>& written, u32 sp) {
    std::unordered_map<u32, u8> out;
    for (const auto& [address, value] : written)
        if (!(address < sp && address >= sp - 0x40000u)) out[address] = value;
    return out;
}

// 0 when a caller could not tell them apart; else what differs: 1 one stopped,
// 2 memory, 3 the calls, 100 + n register n, 200 + n float register n, 300 the rest.
int Differ(const Outcome& a, const Outcome& b, int abi, u32 sp) {
    if (a.status != b.status) return 1;
    if (a.status) return 0;                     // both stopped: nothing to compare
    if (Visible(a.written, sp) != Visible(b.written, sp)) return 2;
    if (a.calls != b.calls) return 3;
    static const int returned[] = {2, 3, 16, 17, 18, 19, 20, 21, 22, 23, 28, 29, 30};
    for (int r : returned) if (a.c.r[r] != b.c.r[r]) return 100 + r;
    if (abi) {
        for (int f : {0, 1}) if (a.c.f[f] != b.c.f[f]) return 200 + f;
        for (int f = 20; f < 32; ++f) if (a.c.f[f] != b.c.f[f]) return 200 + f;
        return 0;
    }
    for (int r = 0; r < 32; ++r) if (a.c.r[r] != b.c.r[r]) return 100 + r;
    for (int f = 0; f < 32; ++f) if (a.c.f[f] != b.c.f[f]) return 200 + f;
    return (a.c.hi == b.c.hi && a.c.lo == b.c.lo && a.c.fcc == b.c.fcc) ? 0 : 300;
}
}

/* For each pair, the first case that differs and what differs (case * 1000 +
   Differ's code), or -1 if none did; and how many cases ran - fewer than asked
   when a pair's cases took longer than `seconds`. */
extern "C" __declspec(dllexport) int equiv_run(int cases, double seconds, long long* first_difference,
                                               int* ran, int first, int last) {
    const int count = int(sizeof kPairs / sizeof kPairs[0]);
    for (int i = first; i < last && i < count; ++i) {
        first_difference[i] = -1;
        ran[i] = 0;
        const auto started = std::chrono::steady_clock::now();
        for (int k = 0; k < cases; ++k) {
            if ((k & 63) == 0 && std::chrono::duration<double>(
                    std::chrono::steady_clock::now() - started).count() > seconds)
                break;
            ran[i] = k + 1;
            const u64 seed = Mix(u64(kPairs[i].address) * 7919 + u64(k));
            lifted::Context start{};
            for (int r = 1; r < 32; ++r) start.r[r] = Mix(seed + r);
            start.r[29] = 0x01F00000u - (u32(seed) & 0xFF0);   // a plausible stack
            // A return address and global pointer as the hardware has them:
            // 32-bit addresses, sign-extended (a structured form restores $ra
            // as the 32-bit address it entered with).
            start.r[31] = 0x00100000u + (u32(seed >> 8) & 0x3FFFFCu);
            start.r[28] = 0x00480000u + (u32(seed >> 16) & 0xFFF0u);
            for (int f = 0; f < 32; ++f) start.f[f] = u32(Mix(seed + 100 + f));
            start.hi = Mix(seed + 200); start.lo = Mix(seed + 201);
            const Outcome a = Run(kPairs[i].plain, start, seed);
            const Outcome b = Run(kPairs[i].structured, start, seed);
            if (const int what = Differ(a, b, kPairs[i].abi, u32(start.r[29]))) {
                first_difference[i] = (long long)k * 1000 + what;
                break;
            }
        }
    }
    return count;
}
'''

_NAME = re.compile(r"\bvoid F_([0-9A-F]{8})\(lifted::Context& c\)")
CHUNK = 60


def sources(pairs: dict) -> tuple:
    """(driver C++, [body C++]) for {address: (plain, structured, abi)}: both forms
    of each function, renamed apart, and the driver that compares them."""
    declarations, table, bodies = [], [], []
    ordered = sorted(pairs.items())
    for address, (_plain, _structured, abi) in ordered:
        declarations.append("void P_%08X(lifted::Context& c);\nvoid S_%08X(lifted::Context& c);"
                            % (address, address))
        table.append("    {0x%08Xu, P_%08X, S_%08X, %d}," % (address, address, address, int(abi)))
    preamble = '#include "ps2rt/lifted.h"\nusing namespace generated;\nusing namespace lifted;\n'
    for start in range(0, len(ordered), CHUNK):
        text = [preamble]
        for _address, (plain, structured, _abi) in ordered[start:start + CHUNK]:
            text.append(_NAME.sub(r"void P_\1(lifted::Context& c)", plain))
            text.append(_NAME.sub(r"void S_\1(lifted::Context& c)", structured))
        bodies.append("\n".join(text))
    driver = HARNESS % {"declarations": "\n".join(declarations), "pairs": "\n".join(table)}
    return driver, bodies


def _build(paths, work: str) -> str:
    import subprocess

    runtime = os.path.join(cppcheck.RUNTIME, "src")
    all_sources = list(paths) + [os.path.join(runtime, name) for name in
                                 ("eefpu.cpp", "eeint.cpp", "lifted_support.cpp", "vu0.cpp")]
    cppcheck._BUILDS += 1
    dll = os.path.join(work, "equivalence_%d_%d.dll" % (os.getpid(), cppcheck._BUILDS))
    # The sources go in a response file: a hundred and more paths are longer than
    # the 8,191 characters a cmd command line may be.
    response = os.path.join(work, "sources.rsp")
    with open(response, "w", encoding="utf-8") as handle:
        handle.write("\n".join('"%s"' % p for p in all_sources) + "\n")
    script = os.path.join(work, "build.bat")
    with open(script, "w", encoding="utf-8") as handle:
        handle.write('@echo off\ncall "%s" >nul\ncd /d "%s"\n' % (cppcheck.find_vcvars(), work))
        handle.write('cl /nologo /std:c++17 /O2 /EHsc /bigobj /MP4 /W1 /LD /I"%s" @"%s" /Fe"%s" '
                     '>build.log 2>&1\nexit /b %%errorlevel%%\n'
                     % (os.path.join(cppcheck.RUNTIME, "include"), response, dll))
    if subprocess.run(["cmd", "/c", script], capture_output=True).returncode or \
            not os.path.exists(dll):
        log = os.path.join(work, "build.log")
        text = open(log, encoding="utf-8", errors="replace").read() if os.path.exists(log) else ""
        errors = [line for line in text.splitlines() if " error " in line or "fatal" in line]
        raise cppcheck.CompileError("\n".join(errors[:20]) or text[-3000:] or "the build did not run")
    return dll


def check(pairs: dict, work: str, cases: int = CASES) -> dict:
    """{address: None if the forms agreed on every case, else (the first case that
    differed, what differed: see Differ)} for {address: (plain, structured, abi)}."""
    os.makedirs(work, exist_ok=True)
    driver, bodies = sources(pairs)
    paths = []
    for number, text in enumerate([driver] + bodies):
        path = os.path.join(work, "equivalence_%03d.cpp" % number)
        with open(path, "w", encoding="utf-8", newline="\n") as handle:
            handle.write(text)
        paths.append(path)
    import time

    started = time.time()
    library = ctypes.CDLL(_build(paths, work))
    built = time.time()
    library.equiv_run.restype = ctypes.c_int
    library.equiv_run.argtypes = [ctypes.c_int, ctypes.c_double,
                                  ctypes.POINTER(ctypes.c_longlong), ctypes.POINTER(ctypes.c_int),
                                  ctypes.c_int, ctypes.c_int]
    ordered = sorted(pairs)
    results = (ctypes.c_longlong * len(ordered))()
    ran = (ctypes.c_int * len(ordered))()
    # In batches, each followed by a heartbeat (tools/watchdog.py takes long
    # silence for a hang).
    for first in range(0, len(ordered), BATCH):
        library.equiv_run(cases, SECONDS_PER_FUNCTION, results, ran, first, first + BATCH)
        print("    equivalence: %d of %d checked" % (min(first + BATCH, len(ordered)), len(ordered)),
              flush=True)
    short = sum(1 for i in range(len(ordered)) if results[i] < 0 and ran[i] < cases)
    print("  equivalence: %d functions, built in %.0fs, run in %.0fs%s"
          % (len(ordered), built - started, time.time() - built,
             (", %d stopped early on time" % short) if short else ""))
    return {address: (None if results[i] < 0 else divmod(int(results[i]), 1000))
            for i, address in enumerate(ordered)}
