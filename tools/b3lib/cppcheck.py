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

VSWHERE = r"C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe"

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


_BUILDS = 0


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
        indices = [int(i) for i in re.findall(r"arg(\d)", params)]
        found.append((int(address, 16), name, return_type, indices))
    return found


def harness_source(functions) -> str:
    """A dispatch table plus memory helpers that forward to Python callbacks."""
    lines = [
        '#include "b3/generated.h"',
        "",
        "namespace generated {",
    ]
    for _address, name, return_type, indices in functions:
        params = ", ".join("u64" for _ in indices) or "void"
        lines.append("%s %s(%s);" % (return_type, name, params))
    lines += [
        "}",
        "",
        "typedef u64 (*ReadFn)(u32 address, u32 size, u32 is_signed);",
        "typedef void (*WriteFn)(u32 address, u64 value, u32 size);",
        "static ReadFn g_read;",
        "static WriteFn g_write;",
        "",
        "namespace generated {",
    ]
    for name, size, signed in READERS:
        lines.append("u64 %s(u32 a) { return g_read(a, %d, %d); }" % (name, size, signed))
    for name, size, value_type in WRITERS:
        lines.append("void %s(u32 a, %s v) { g_write(a, v, %d); }" % (name, value_type, size))
    lines += [
        "}",
        "",
        'extern "C" __declspec(dllexport) void b3_set_memory(ReadFn r, WriteFn w) {',
        "    g_read = r;",
        "    g_write = w;",
        "}",
        "",
        "/* Returns 1 with *result set, 2 for a void function, 0 if unknown. */",
        'extern "C" __declspec(dllexport) int b3_call(u32 address, const u64* args, s32* result) {',
        "    switch (address) {",
    ]
    for address, name, return_type, indices in functions:
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
    with open(harness, "w", encoding="utf-8") as handle:
        handle.write(harness_source(functions))

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
        handle.write('cl /nologo /std:c++17 /O2 /W3 /LD /I"%s" %s /Fe"%s" >build.log 2>&1\n'
                     % (os.path.abspath(include_dir),
                        " ".join('"%s"' % path for path in sources), dll))
        handle.write("exit /b %errorlevel%\n")
    result = subprocess.run(["cmd", "/c", script], capture_output=True, text=True)
    if result.returncode != 0:
        with open(os.path.join(work_dir, "build.log"), encoding="utf-8",
                  errors="replace") as handle:
            log = handle.read()
        errors = [line for line in log.splitlines() if " error " in line]
        raise CompileError("\n".join(errors[:20]) or log[-2000:])
    return dll, functions


class CompiledFunctions:
    """The compiled library, with memory routed to an interpreter's memory."""

    def __init__(self, dll_path: str, memory) -> None:
        self.memory = memory
        self.writes = []
        # Keep the callback objects alive for as long as the library can call them.
        self._read = READ_CALLBACK(self._on_read)
        self._write = WRITE_CALLBACK(self._on_write)
        self.library = ctypes.CDLL(dll_path)
        self.library.b3_set_memory(self._read, self._write)
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
        self.writes.append((address, value & ((1 << (8 * size)) - 1), size))

    def evaluator(self, address: int):
        """An `evaluate(args)` for derive.run_cases, backed by compiled code."""
        def evaluate(args):
            self.writes = []
            self.error = None
            values = (ctypes.c_uint64 * 4)(*[(a & MASK64) for a in (list(args) + [0] * 4)[:4]])
            result = ctypes.c_int32(0)
            kind = self.library.b3_call(address, values, ctypes.byref(result))
            if self.error is not None:
                raise self.error
            if kind == 0:
                raise KeyError("0x%08X is not in the compiled library" % address)
            return (None if kind == 2 else result.value), list(self.writes)
        return evaluate
