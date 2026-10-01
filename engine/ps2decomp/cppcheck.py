"""Compile the generated C++ and run it against the original machine code.

The derivations are verified as expression trees, evaluated in Python. The thing
the project actually ships is the C++ those trees are rendered to, and nothing
checked that the two mean the same. They did not: the renderer printed a signed
and an unsigned comparison identically, and C++'s promotion rules quietly
narrowed 64-bit register arithmetic to `int` - 419 shifts in the emitted file were
undefined behaviour, and the file compiled without a single error.

So this closes the loop. The emitted source is compiled into a shared library
together with a dispatch table and memory helpers that call back into Python,
and every function is run on the same cases the derivation was verified on,
against the interpreter. A function only counts if its *compiled* form agrees.

The compiler is MSVC, found through vswhere. Where none is installed the check
reports itself unavailable rather than passing.
"""

from __future__ import annotations

import ctypes
import os
import re
import subprocess

MASK64 = (1 << 64) - 1

# The C++ runtime the generated code is compiled against: engine/runtime.
RUNTIME = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "runtime")

VSWHERE = r"C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe"

# Functions the emitted code calls but which were not derived: declared, not defined.
EXTERN = re.compile(r"^u64 (func_([0-9A-F]{8}))\(([^)]*)\);  /\* not yet derived \*/$", re.M)

SIGNATURE = re.compile(r"^(s32|void) (func_([0-9A-F]{8}))\(([^)]*)\) \{$", re.M)

READERS = [
    ("ReadS8", 1, 1), ("ReadU8", 1, 0), ("ReadS16", 2, 1), ("ReadU16", 2, 0),
    ("ReadS32", 4, 1), ("ReadU32", 4, 0), ("ReadU64", 8, 0),
]
WRITERS = [("Write8", 1, "u32"), ("Write16", 2, "u32"), ("Write32", 4, "u32"),
           ("Write64", 8, "u64")]

READ_CALLBACK = ctypes.CFUNCTYPE(ctypes.c_uint64, ctypes.c_uint32, ctypes.c_uint32,
                                 ctypes.c_uint32)
WRITE_CALLBACK = ctypes.CFUNCTYPE(None, ctypes.c_uint32, ctypes.c_uint64, ctypes.c_uint32)
CALL_CALLBACK = ctypes.CFUNCTYPE(ctypes.c_uint64, ctypes.c_uint32,
                                 ctypes.POINTER(ctypes.c_uint64), ctypes.c_uint32)


_BUILDS = 0

from . import layout
# Room left between the derived function's guest stack and a callee's.
CALLEE_GAP = 0x100
from .paths import callee_stack_top


class CompilerUnavailable(Exception):
    pass


class CompileError(Exception):
    pass


def find_vcvars() -> str:
    if not os.path.exists(VSWHERE):
        raise CompilerUnavailable("vswhere not found; no Visual Studio installed")
    result = subprocess.run(
        [VSWHERE, "-latest", "-products", "*", "-property", "installationPath"],
        capture_output=True, text=True,
    )
    root = result.stdout.strip()
    vcvars = os.path.join(root, "VC", "Auxiliary", "Build", "vcvars64.bat")
    if not root or not os.path.exists(vcvars):
        raise CompilerUnavailable("no MSVC x64 toolset found")
    return vcvars


def functions_in(source: str) -> list:
    """(address, name, return type, parameter indices) for every emitted function."""
    found = []
    for match in SIGNATURE.finditer(source):
        return_type, name, address, params = match.groups()
        # arg0..arg7 are vector entries 0..7, farg0..farg7 entries 8..15.
        indices = [int(n) + (8 if f else 0)
                   for f, n in re.findall(r"\b(f?)arg(\d+)\b", params)]
        found.append((int(address, 16), name, return_type, indices, params))
    return found


def externs_in(source: str) -> list:
    """(address, name, parameter indices) for every not-yet-derived callee."""
    found = []
    for match in EXTERN.finditer(source):
        name, address, params = match.groups()
        indices = [int(n) + (8 if f else 0)
                   for f, n in re.findall(r"\b(f?)arg(\d+)\b", params)]
        found.append((int(address, 16), name, indices))
    return found


def harness_source(functions, typed: bool, externs=()) -> str:
    """A dispatch table plus memory helpers that forward to Python callbacks."""
    # The dispatch switch has a case per function - over two thousand - and
    # optimising it ran the compiler out of heap. Only the generated code has to be
    # checked at /O2; the harness that calls it does not.
    lines = ['#pragma optimize("", off)', '#include "ps2rt/generated.h"']
    if typed:
        lines.append('#include "types.h"')
    lines += ["", "namespace generated {"]
    for _address, name, return_type, _indices, params in functions:
        # The declaration repeats the definition's own parameter list, typed views
        # included; the u64 arguments below convert to them implicitly.
        lines.append("%s %s(%s);" % (return_type, name, params))
    lines += [
        "}",
        "",
        "typedef u64 (*ReadFn)(u32 address, u32 size, u32 is_signed);",
        "typedef void (*WriteFn)(u32 address, u64 value, u32 size);",
        "typedef u64 (*CallFn)(u32 address, const u64* arguments, u32 stack_top);",
        "static ReadFn g_read;",
        "static WriteFn g_write;",
        "static CallFn g_call;",
        "static u32 g_sp = 0x%08X;  /* the interpreter's default stack top */"
        % layout.CURRENT.stack_top,
        "",
        "namespace generated {",
        "u64 StackEnter(u32 size) { g_sp -= size; return g_sp; }",
        "u64 CallPointer(u32 address, u64 a0, u64 a1, u64 a2, u64 a3, u64 a4, u64 a5,",
        "                u64 a6, u64 a7, u64 f0, u64 f1, u64 f2, u64 f3, u64 f4, u64 f5,",
        "                u64 f6, u64 f7) {",
        "    u64 vector[16] = {a0, a1, a2, a3, a4, a5, a6, a7,",
        "                      f0, f1, f2, f3, f4, f5, f6, f7};",
        "    u32 top = g_sp - 0x%X < 0x%08X ? g_sp - 0x%X : 0x%08X;"
        % (CALLEE_GAP, callee_stack_top(), CALLEE_GAP, callee_stack_top()),
        "    return g_call(address, vector, top);",
        "}",
        "void StackLeave(u32 size) { g_sp += size; }",
    ]
    # A callee that was not derived runs as the original machine code.
    for address, name, indices in externs:
        params = ", ".join("u64 p%d" % index for index in indices) or "void"
        lines.append("u64 %s(%s) {" % (name, params))
        lines.append("    u64 vector[16] = {0};")
        for index in indices:
            lines.append("    vector[%d] = p%d;" % (index, index))
        # Callees run where the model runs them, or lower if nested derived
        # functions have already used more guest stack than that.
        lines.append("    u32 top = g_sp - 0x%X < 0x%08X ? g_sp - 0x%X : 0x%08X;"
                     % (CALLEE_GAP, callee_stack_top(), CALLEE_GAP, callee_stack_top()))
        lines.append("    return g_call(0x%08X, vector, top);" % address)
        lines.append("}")
    for name, size, signed in READERS:
        lines.append("u64 %s(u32 a) { return g_read(a, %d, %d); }" % (name, size, signed))
    for name, size, value_type in WRITERS:
        lines.append("void %s(u32 a, %s v) { g_write(a, v, %d); }" % (name, value_type, size))
    lines += [
        "}",
        "",
        'extern "C" __declspec(dllexport) void b3_set_memory(ReadFn r, WriteFn w, CallFn c) {',
        "    g_read = r;",
        "    g_write = w;",
        "    g_call = c;",
        "}",
        "",
        "/* Returns 1 with *result set, 2 for a void function, 0 if unknown. */",
        'extern "C" __declspec(dllexport) int b3_call(u32 address, const u64* args, s32* result) {',
        "    switch (address) {",
    ]
    for address, name, return_type, indices, _params in functions:
        call = "generated::%s(%s)" % (name, ", ".join("args[%d]" % i for i in indices))
        if return_type == "void":
            lines.append("    case 0x%08X: %s; return 2;" % (address, call))
        else:
            lines.append("    case 0x%08X: *result = %s; return 1;" % (address, call))
    lines += ["    }", "    return 0;", "}", ""]
    return "\n".join(lines)


def build(generated_path: str, runtime_sources, include_dir: str, work_dir: str) -> tuple:
    """Compile the generated file into a DLL; returns (dll path, functions)."""
    vcvars = find_vcvars()
    os.makedirs(work_dir, exist_ok=True)
    with open(generated_path, encoding="utf-8") as handle:
        functions = functions_in(handle.read())
    harness = os.path.join(work_dir, "harness.cpp")
    generated_dir = os.path.dirname(os.path.abspath(generated_path))
    typed = os.path.exists(os.path.join(generated_dir, "types.h"))
    with open(generated_path, encoding="utf-8") as handle:
        externs = externs_in(handle.read())
    with open(harness, "w", encoding="utf-8") as handle:
        handle.write(harness_source(functions, typed, externs))

    # A fresh name per build: Windows keeps a loaded DLL locked, so rebuilding in
    # the same process under the same name would fail.
    global _BUILDS
    _BUILDS += 1
    dll = os.path.join(work_dir, "generated_check_%d_%d.dll" % (os.getpid(), _BUILDS))
    sources = [os.path.abspath(generated_path), harness] + [
        os.path.abspath(path) for path in runtime_sources
    ]
    # /O2 on purpose: an optimiser is what turns undefined behaviour into wrong
    # answers, so checking an unoptimised build would hide exactly the problems
    # this exists to find.
    script = os.path.join(work_dir, "build.bat")
    with open(script, "w", encoding="utf-8") as handle:
        handle.write("@echo off\n")
        handle.write('call "%s" >nul\n' % vcvars)
        handle.write('cd /d "%s"\n' % work_dir)
        handle.write('cl /nologo /std:c++17 /O2 /W3 /LD /I"%s" /I"%s" %s /Fe"%s" >build.log 2>&1\n'
                     % (os.path.abspath(include_dir), generated_dir,
                        " ".join('"%s"' % path for path in sources), dll))
        handle.write("exit /b %errorlevel%\n")
    result = subprocess.run(["cmd", "/c", script], capture_output=True, text=True)
    if result.returncode != 0:
        with open(os.path.join(work_dir, "build.log"), encoding="utf-8",
                  errors="replace") as handle:
            log = handle.read()
        errors = [line for line in log.splitlines() if " error " in line]
        # Every error, not a sample: the caller drops each function named here.
        raise CompileError("\n".join(errors) or log[-2000:])
    return dll, functions


class CompiledFunctions:
    """The compiled library, with memory routed to an interpreter's memory."""

    def __init__(self, dll_path: str, machine, budget: int = 100_000) -> None:
        self.machine = machine
        self.budget = budget
        self.memory = machine.memory
        # Keep the callback objects alive for as long as the library can call them.
        self._read = READ_CALLBACK(self._on_read)
        self._write = WRITE_CALLBACK(self._on_write)
        self._call = CALL_CALLBACK(self._on_call)
        self.library = ctypes.CDLL(dll_path)
        self.library.b3_set_memory(self._read, self._write, self._call)
        self.library.b3_call.restype = ctypes.c_int
        self.library.b3_call.argtypes = [
            ctypes.c_uint32, ctypes.POINTER(ctypes.c_uint64), ctypes.POINTER(ctypes.c_int32)
        ]
        self.error = None

    def _on_read(self, address, size, signed):
        try:
            return self.memory.read_int(address, size, bool(signed)) & MASK64
        except Exception as exc:     # an exception cannot cross the C boundary
            self.error = exc
            return 0

    def _on_write(self, address, value, size):
        # Written straight to memory, inside the verifier's journal: a callee run
        # later in the same function must see it, as it would on the console.
        try:
            self.memory.write_int(address, value & ((1 << (8 * size)) - 1), size)
        except Exception as exc:
            self.error = exc

    def _on_call(self, address, arguments, stack_top):
        try:
            self.machine.call(address, [arguments[i] for i in range(16)],
                              max_instructions=self.budget, stack_top=stack_top)
            return self.machine.get(2) & MASK64
        except Exception as exc:
            self.error = exc
            return 0

    def evaluator(self, address: int):
        """An `evaluate(args)` for derive.run_cases, backed by compiled code."""
        def evaluate(args):
            self.error = None
            values = (ctypes.c_uint64 * 16)(
                *[(a & MASK64) for a in (list(args) + [0] * 16)[:16]])
            result = ctypes.c_int32(0)
            kind = self.library.b3_call(address, values, ctypes.byref(result))
            if self.error is not None:
                raise self.error
            if kind == 0:
                raise KeyError("0x%08X is not in the compiled library" % address)
            return (None if kind == 2 else result.value), []
        return evaluate
