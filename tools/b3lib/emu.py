"""An R5900 interpreter, used to verify reconstructed functions behave correctly.

The project cannot prove byte-equality, because the original Metrowerks compiler
is unavailable.  Functional reconstruction needs a different kind of proof, and
this supplies it: run the *original* machine code in an interpreter, run the
reconstruction on the same inputs, and compare results.  That turns "I think this
C++ is right" into something a test can decide.

Scope is deliberately the integer core plus the common loads and stores - enough
to execute leaf and near-leaf functions, which is where reconstruction starts.
Anything outside that subset raises `UnsupportedInstruction` rather than being
quietly approximated, because an interpreter that silently guesses would produce
exactly the false confidence this is meant to remove.

Registers are modelled as full 64-bit values with MIPS III semantics: 32-bit
operations sign-extend their result into the destination register.
"""

from __future__ import annotations

import struct

from .r5900 import DISASM, GPR

MASK64 = (1 << 64) - 1
MASK32 = 0xFFFFFFFF

# Where the interpreter puts the stack. Chosen above the loaded image and its BSS
# so it cannot collide with real data.
DEFAULT_STACK_TOP = 0x01F00000
DEFAULT_STACK_SIZE = 64 * 1024

# Writing this to $ra before a call lets the interpreter spot the return.
RETURN_SENTINEL = 0xDEAD0000

# The global pointer, as the startup code establishes it. Recovered from the entry
# function: `lui $a0, 0x4F` then `addiu $a0, $a0, -0x7990` gives 0x004E8670, which
# is then copied into $gp at 0x001001C0. It lands just past the loaded image, in
# the BSS, which is where a small-data pointer belongs.
#
# This matters for execution: a great many functions reach globals as an offset
# from $gp, and leaving the register zero makes every one of them fault.
GP_VALUE = 0x004E8670


class EmulationError(Exception):
    """Base class for interpreter failures."""


class UnsupportedInstruction(EmulationError):
    def __init__(self, addr: int, word: int, mnemonic: str) -> None:
        super().__init__(
            "unsupported instruction at 0x%08X: %08x (%s)" % (addr, word, mnemonic)
        )
        self.addr = addr
        self.word = word
        self.mnemonic = mnemonic


class MemoryFault(EmulationError):
    def __init__(self, addr: int, size: int, store: bool) -> None:
        super().__init__(
            "%s of %d byte(s) at unmapped 0x%08X"
            % ("store" if store else "load", size, addr)
        )
        self.addr = addr


class ExecutionLimit(EmulationError):
    """Raised when a function runs longer than the instruction budget allows."""


def _sign32(value: int) -> int:
    value &= MASK32
    return value - (1 << 32) if value & 0x80000000 else value


def _sign64(value: int) -> int:
    value &= MASK64
    return value - (1 << 64) if value & (1 << 63) else value


class Memory:
    """A flat mapped span, backed by the loaded image plus a stack."""

    def __init__(self, base: int, data: bytes, total_size: int) -> None:
        if total_size < len(data):
            total_size = len(data)
        self.base = base
        self.size = total_size
        self.bytes = bytearray(total_size)
        self.bytes[: len(data)] = data
        self._journal = None

    def _offset(self, addr: int, size: int, store: bool) -> int:
        off = addr - self.base
        if off < 0 or off + size > self.size:
            raise MemoryFault(addr, size, store)
        return off

    # -- journalling ---------------------------------------------------
    #
    # Differential testing has to undo whatever a function wrote before the next
    # case runs. Snapshotting a chosen region is not enough: a function that writes
    # to a global outside that region leaves the change in place, which both
    # corrupts later cases and makes the derivation appear to disagree when it was
    # the harness that moved. Journalling every write makes the undo exact and
    # cheap, whatever the function touches.

    def begin_journal(self) -> None:
        self._journal = []

    def rollback(self) -> None:
        """Undo every write since `begin_journal`, most recent first."""
        journal = getattr(self, "_journal", None)
        if journal is None:
            return
        for offset, previous in reversed(journal):
            self.bytes[offset : offset + len(previous)] = previous
        self._journal = []

    def end_journal(self) -> None:
        self._journal = None

    def read(self, addr: int, size: int) -> bytes:
        off = self._offset(addr, size, False)
        return bytes(self.bytes[off : off + size])

    def write(self, addr: int, payload: bytes) -> None:
        off = self._offset(addr, len(payload), True)
        if self._journal is not None:
            self._journal.append((off, bytes(self.bytes[off : off + len(payload)])))
        self.bytes[off : off + len(payload)] = payload

    def read_int(self, addr: int, size: int, signed: bool) -> int:
        raw = self.read(addr, size)
        return int.from_bytes(raw, "little", signed=signed)

    def write_int(self, addr: int, value: int, size: int) -> None:
        self.write(addr, (value & ((1 << (size * 8)) - 1)).to_bytes(size, "little"))

    def read_cstring(self, addr: int, limit: int = 4096) -> bytes:
        out = bytearray()
        for i in range(limit):
            byte = self.read_int(addr + i, 1, False)
            if byte == 0:
                break
            out.append(byte)
        return bytes(out)


class Machine:
    """An R5900 interpreter over a loaded image."""

    def __init__(self, base: int, data: bytes, total_size: int = None) -> None:
        if total_size is None:
            total_size = max(len(data), DEFAULT_STACK_TOP + DEFAULT_STACK_SIZE - base)
        self.memory = Memory(base, data, total_size)
        self.regs = [0] * 32
        # The EE's registers are 128 bits wide. Full SIMD arithmetic is out of
        # scope, but `lq`/`sq` must preserve the upper half, because Metrowerks
        # uses them to spill callee-saved registers in nearly every prologue -
        # losing those bits would corrupt restores.
        self.regs_hi = [0] * 32
        self.hi = 0
        self.lo = 0
        # Coprocessor 1: 32 single-precision registers held as raw 32-bit patterns,
        # plus the condition flag that `c.<cond>.s` sets and `bc1t`/`bc1f` test.
        #
        # Storing bits rather than Python floats matters. `lwc1`/`swc1`/`mtc1`/`mfc1`
        # are bit moves - the hardware copies all 32 bits verbatim - so keeping
        # floats here round-trips through a double and canonicalises NaN payloads,
        # silently altering values a copy must preserve. That showed up as a
        # float-copying function disagreeing with a derivation that was correct.
        self.fpr = [0] * 32
        self.fp_cond = False
        self.pc = 0
        self.instructions_executed = 0
        self.call_log = []

    # -- register access ----------------------------------------------

    def get(self, index: int) -> int:
        return 0 if index == 0 else self.regs[index]

    def set(self, index: int, value: int) -> None:
        if index:
            self.regs[index] = value & MASK64

    def get32(self, index: int) -> int:
        return self.get(index) & MASK32

    def set32(self, index: int, value: int) -> None:
        """Write a 32-bit result, sign-extended into the 64-bit register."""
        self.set(index, _sign32(value) & MASK64)

    # -- loading a classmethod convenience -----------------------------

    @classmethod
    def from_elf(cls, elf) -> "Machine":
        section = elf.exec_sections[0]
        data = elf.section_data(section)
        # Cover the BSS the program headers declare, so globals are addressable.
        memsz = max(
            (p.memsz for p in elf.segments if p.vaddr == section.addr), default=len(data)
        )
        total = max(len(data), memsz, DEFAULT_STACK_TOP + DEFAULT_STACK_SIZE - section.addr)
        return cls(section.addr, data, total)

    # -- execution -----------------------------------------------------

    def call(self, addr: int, args=(), max_instructions: int = 2_000_000,
             stack_top: int = DEFAULT_STACK_TOP) -> int:
        """Run the function at `addr` and return its $v0 as a signed 32-bit value.

        Arguments are placed in $a0..$a3; passing more than four is rejected
        rather than silently mishandled, since stack arguments need the caller's
        frame layout.
        """
        if len(args) > 4:
            raise EmulationError("only register arguments ($a0..$a3) are supported")
        for i, value in enumerate(args):
            self.set(4 + i, value & MASK64)
        self.set(28, GP_VALUE)            # $gp
        self.set(29, stack_top)           # $sp
        self.set(31, RETURN_SENTINEL)     # $ra
        self.pc = addr
        self.instructions_executed = 0

        budget = max_instructions
        while True:
            if self.pc == RETURN_SENTINEL:
                break
            self.step()
            self.instructions_executed += 1
            if self.instructions_executed > budget:
                raise ExecutionLimit(
                    "exceeded %d instructions; likely an infinite loop" % budget
                )
        return _sign32(self.get32(2))

    def step(self) -> None:
        """Execute one instruction, honouring the delay slot."""
        word = self.memory.read_int(self.pc, 4, False)
        ins = DISASM.decode(word, self.pc)
        if not ins.is_valid:
            raise UnsupportedInstruction(self.pc, word, ".word")

        if ins.has_delay_slot:
            target, link, nullify = self._resolve_transfer(ins)
            slot_pc = self.pc + 4
            saved = self.pc
            # A "likely" branch that is NOT taken nullifies its delay slot: the
            # instruction is skipped entirely rather than executed. Running it
            # anyway silently corrupts state - it is what made the SIMD strlen
            # return wrong lengths, by clobbering a mask register and advancing a
            # cursor that should have stood still.
            if not nullify:
                slot_word = self.memory.read_int(slot_pc, 4, False)
                slot_ins = DISASM.decode(slot_word, slot_pc)
                if slot_ins.has_delay_slot:
                    raise UnsupportedInstruction(
                        slot_pc, slot_word, "branch in delay slot"
                    )
                self.pc = slot_pc
                self._execute(slot_ins)
            if link is not None:
                self.set(31, saved + 8)
            self.pc = target if target is not None else saved + 8
            return

        self._execute(ins)
        self.pc += 4

    def _resolve_transfer(self, ins):
        """Decide the branch outcome.

        Returns (target or None, link or None, nullify_delay_slot).  The third
        value is True only for a "likely" branch that was not taken, which is the
        one case where the delay-slot instruction must be skipped.
        """
        m = ins.mnemonic
        rs = _sign64(self.get(ins.rs))
        rt = _sign64(self.get(ins.rt))
        link = True if ins.is_call else None

        if m in ("j", "b"):
            return ins.target, None, False
        if m == "jal":
            return ins.target, True, False
        if m == "jr":
            return self.get32(ins.rs), None, False
        if m == "jalr":
            return self.get32(ins.rs), True, False
        if m in ("bc1t", "bc1tl"):
            taken = self.fp_cond
            if taken:
                return ins.target, None, False
            return (self.pc + 8, None, True) if m.endswith("l") else (None, None, False)
        if m in ("bc1f", "bc1fl"):
            taken = not self.fp_cond
            if taken:
                return ins.target, None, False
            return (self.pc + 8, None, True) if m.endswith("l") else (None, None, False)

        taken = {
            "beq": rs == rt,
            "bne": rs != rt,
            "beql": rs == rt,
            "bnel": rs != rt,
            "blez": rs <= 0,
            "bgtz": rs > 0,
            "blezl": rs <= 0,
            "bgtzl": rs > 0,
            "bltz": rs < 0,
            "bgez": rs >= 0,
            "bltzl": rs < 0,
            "bgezl": rs >= 0,
            "bltzal": rs < 0,
            "bgezal": rs >= 0,
        }.get(m)
        if taken is None:
            raise UnsupportedInstruction(self.pc, ins.raw, m)
        if taken:
            return ins.target, link, False
        # Not taken: a "likely" branch nullifies its delay slot, so the caller is
        # told to skip it and resume past the pair.
        if ins.is_likely:
            return self.pc + 8, link, True
        return None, link, False

    def _execute(self, ins) -> None:
        m = ins.mnemonic
        if m == "nop" or m == "sync":
            return

        handler = _HANDLERS.get(m)
        if handler is None:
            raise UnsupportedInstruction(ins.addr, ins.raw, m)
        handler(self, ins)


# -- instruction handlers ------------------------------------------------
#
# Kept as a dispatch table rather than a long if-chain so the supported subset is
# explicit and easy to audit.

def _alu_rrr(fn):
    def run(m: Machine, ins) -> None:
        m.set32(ins.rd, fn(m.get32(ins.rs), m.get32(ins.rt)))
    return run


def _alu_d64(fn):
    def run(m: Machine, ins) -> None:
        m.set(ins.rd, fn(m.get(ins.rs), m.get(ins.rt)) & MASK64)
    return run


def _load(size: int, signed: bool):
    def run(m: Machine, ins) -> None:
        addr = (m.get32(ins.rs) + ins.imm) & MASK32
        value = m.memory.read_int(addr, size, signed)
        if size == 8:
            m.set(ins.rt, value & MASK64)
        else:
            m.set32(ins.rt, value)
    return run


def _store(size: int):
    def run(m: Machine, ins) -> None:
        addr = (m.get32(ins.rs) + ins.imm) & MASK32
        m.memory.write_int(addr, m.get(ins.rt), size)
    return run


def _shift(fn, use_reg: bool, width: int = 32):
    def run(m: Machine, ins) -> None:
        amount = (m.get32(ins.rs) & 0x1F) if use_reg else ins.imm
        if width == 32:
            m.set32(ins.rd, fn(m.get32(ins.rt), amount))
        else:
            m.set(ins.rd, fn(m.get(ins.rt), amount) & MASK64)
    return run


def _op_addiu(m: Machine, ins) -> None:
    m.set32(ins.rt, m.get32(ins.rs) + ins.imm)


def _op_lui(m: Machine, ins) -> None:
    m.set32(ins.rt, (ins.imm & 0xFFFF) << 16)


def _op_andi(m: Machine, ins) -> None:
    m.set(ins.rt, m.get(ins.rs) & (ins.imm & 0xFFFF))


def _op_ori(m: Machine, ins) -> None:
    m.set(ins.rt, m.get(ins.rs) | (ins.imm & 0xFFFF))


def _op_xori(m: Machine, ins) -> None:
    m.set(ins.rt, m.get(ins.rs) ^ (ins.imm & 0xFFFF))


def _op_slti(m: Machine, ins) -> None:
    m.set32(ins.rt, 1 if _sign64(m.get(ins.rs)) < ins.imm else 0)


def _op_sltiu(m: Machine, ins) -> None:
    m.set32(ins.rt, 1 if m.get(ins.rs) < (ins.imm & MASK64) else 0)


def _op_slt(m: Machine, ins) -> None:
    m.set32(ins.rd, 1 if _sign64(m.get(ins.rs)) < _sign64(m.get(ins.rt)) else 0)


def _op_sltu(m: Machine, ins) -> None:
    m.set32(ins.rd, 1 if m.get(ins.rs) < m.get(ins.rt) else 0)


def _op_mult(m: Machine, ins) -> None:
    product = _sign32(m.get32(ins.rs)) * _sign32(m.get32(ins.rt))
    m.lo = _sign32(product & MASK32) & MASK64
    m.hi = _sign32((product >> 32) & MASK32) & MASK64
    if ins.rd:
        m.set32(ins.rd, product & MASK32)


def _op_multu(m: Machine, ins) -> None:
    product = m.get32(ins.rs) * m.get32(ins.rt)
    m.lo = _sign32(product & MASK32) & MASK64
    m.hi = _sign32((product >> 32) & MASK32) & MASK64
    if ins.rd:
        m.set32(ins.rd, product & MASK32)


def _op_div(m: Machine, ins) -> None:
    a, b = _sign32(m.get32(ins.rs)), _sign32(m.get32(ins.rt))
    if b == 0:
        # The hardware leaves HI/LO undefined; keep going rather than fail, since
        # real code guards these and a fault here would be misleading.
        return
    q = abs(a) // abs(b)
    if (a < 0) != (b < 0):
        q = -q
    m.lo = _sign32(q) & MASK64
    m.hi = _sign32(a - b * q) & MASK64


def _op_divu(m: Machine, ins) -> None:
    a, b = m.get32(ins.rs), m.get32(ins.rt)
    if b == 0:
        return
    m.lo = _sign32(a // b) & MASK64
    m.hi = _sign32(a % b) & MASK64


def _op_mfhi(m: Machine, ins) -> None:
    m.set(ins.rd, m.hi)


def _op_mflo(m: Machine, ins) -> None:
    m.set(ins.rd, m.lo)


def _op_mthi(m: Machine, ins) -> None:
    m.hi = m.get(ins.rs)


def _op_mtlo(m: Machine, ins) -> None:
    m.lo = m.get(ins.rs)


def _op_movz(m: Machine, ins) -> None:
    if m.get(ins.rt) == 0:
        m.set(ins.rd, m.get(ins.rs))


def _op_movn(m: Machine, ins) -> None:
    if m.get(ins.rt) != 0:
        m.set(ins.rd, m.get(ins.rs))



def _op_lq(m: Machine, ins) -> None:
    """128-bit load. The address is forced to quadword alignment by hardware."""
    addr = (m.get32(ins.rs) + ins.imm) & MASK32 & ~0xF
    m.set(ins.rt, m.memory.read_int(addr, 8, False))
    m.regs_hi[ins.rt] = m.memory.read_int(addr + 8, 8, False)


def _op_sq(m: Machine, ins) -> None:
    addr = (m.get32(ins.rs) + ins.imm) & MASK32 & ~0xF
    m.memory.write_int(addr, m.get(ins.rt), 8)
    m.memory.write_int(addr + 8, m.regs_hi[ins.rt], 8)


# The largest finite single-precision magnitude, bit pattern 0x7F7FFFFF.
FLOAT_MAX = struct.unpack("<f", struct.pack("<I", 0x7F7FFFFF))[0]


def _f32(value: float) -> float:
    """Round a Python double to single precision, as the EE's FPU would.

    The Emotion Engine's FPU is deliberately not IEEE-754: it has no infinities and
    no NaNs, and an operation that overflows saturates to the largest representable
    magnitude rather than becoming infinite.

    Modelling that is not optional here. Python computes in double precision, so an
    overflowing `mul.s` reaches `struct.pack` with a value no float32 can hold and
    raises OverflowError - which showed up as the interpreter crashing outright on
    real game code. Saturating is both the fix and the more faithful behaviour, and
    it is a quirk any port has to reproduce or physics will diverge.
    """
    if value != value:          # NaN has no representation on this FPU
        return 0.0
    if value > FLOAT_MAX:
        return FLOAT_MAX
    if value < -FLOAT_MAX:
        return -FLOAT_MAX
    return struct.unpack("<f", struct.pack("<f", value))[0]


def _fp_bits(value: float) -> int:
    return struct.unpack("<I", struct.pack("<f", value))[0]


def _fp_from_bits(bits: int) -> float:
    return struct.unpack("<f", struct.pack("<I", bits & MASK32))[0]


# COP1 R-format packs its registers differently from the integer unit, and the
# disassembler does not populate rs/rt/rd for these forms, so the fields are taken
# straight from the encoding.
def _fd(ins) -> int:
    return (ins.raw >> 6) & 0x1F


def _fs(ins) -> int:
    return (ins.raw >> 11) & 0x1F


def _ft(ins) -> int:
    return (ins.raw >> 16) & 0x1F


def _op_lwc1(m: Machine, ins) -> None:
    # A bit move: no interpretation, so a NaN payload survives intact. Converting
    # to a Python float here and back on the matching `swc1` sets the quiet-NaN bit
    # and silently alters values that a copy must preserve.
    addr = (m.get32(ins.rs) + ins.imm) & MASK32
    m.fpr[ins.rt] = m.memory.read_int(addr, 4, False)


def _op_swc1(m: Machine, ins) -> None:
    addr = (m.get32(ins.rs) + ins.imm) & MASK32
    m.memory.write_int(addr, m.fpr[ins.rt] & MASK32, 4)


def _op_mtc1(m: Machine, ins) -> None:
    m.fpr[ins.rd] = m.get32(ins.rt)


def _op_mfc1(m: Machine, ins) -> None:
    m.set32(ins.rt, m.fpr[ins.rd] & MASK32)


# Float registers hold raw bit patterns, so arithmetic converts in and out
# explicitly. Confining the conversion to these helpers is what keeps the bit moves
# above exact.
def _fp_arith(fn):
    def run(m: Machine, ins) -> None:
        a = _fp_from_bits(m.fpr[_fs(ins)])
        b = _fp_from_bits(m.fpr[_ft(ins)])
        m.fpr[_fd(ins)] = _fp_bits(_f32(fn(a, b)))
    return run


def _fp_unary(fn):
    def run(m: Machine, ins) -> None:
        m.fpr[_fd(ins)] = _fp_bits(_f32(fn(_fp_from_bits(m.fpr[_fs(ins)]))))
    return run


def _fp_compare(fn):
    def run(m: Machine, ins) -> None:
        m.fp_cond = fn(
            _fp_from_bits(m.fpr[_fs(ins)]), _fp_from_bits(m.fpr[_ft(ins)])
        )
    return run


def _op_cvt_w_s(m: Machine, ins) -> None:
    """Float to fixed, truncating toward zero as the EE does."""
    m.fpr[_fd(ins)] = int(_fp_from_bits(m.fpr[_fs(ins)])) & MASK32


def _op_cvt_s_w(m: Machine, ins) -> None:
    m.fpr[_fd(ins)] = _fp_bits(_f32(float(_sign32(m.fpr[_fs(ins)]))))


def _op_div_s(m: Machine, ins) -> None:
    divisor = _fp_from_bits(m.fpr[_ft(ins)])
    if divisor == 0.0:
        # The FPU flags this rather than trapping, so yield zero instead of raising.
        m.fpr[_fd(ins)] = _fp_bits(0.0)
        return
    m.fpr[_fd(ins)] = _fp_bits(_f32(_fp_from_bits(m.fpr[_fs(ins)]) / divisor))



# -- MMI: the 128-bit integer SIMD subset -------------------------------
#
# Only the operations the real code actually leans on are implemented. The
# optimised strlen alone needs psubb, pnor, pand and the pcpy family, and it sits
# under a great many string routines.

def _q_get(m: Machine, index: int) -> int:
    """Read a register as one 128-bit value."""
    return (m.regs_hi[index] << 64) | m.get(index)


def _q_set(m: Machine, index: int, value: int) -> None:
    if index == 0:
        return
    m.regs[index] = value & MASK64
    m.regs_hi[index] = (value >> 64) & MASK64


def _q_bitwise(fn):
    def run(m: Machine, ins) -> None:
        _q_set(m, ins.rd, fn(_q_get(m, ins.rs), _q_get(m, ins.rt)) & ((1 << 128) - 1))
    return run


def _q_perbyte(fn):
    """Apply `fn` to each of the sixteen byte lanes independently."""
    def run(m: Machine, ins) -> None:
        a, b = _q_get(m, ins.rs), _q_get(m, ins.rt)
        out = 0
        for lane in range(16):
            shift = lane * 8
            out |= (fn((a >> shift) & 0xFF, (b >> shift) & 0xFF) & 0xFF) << shift
        _q_set(m, ins.rd, out)
    return run


def _op_pcpyld(m: Machine, ins) -> None:
    """Low doubleword of rt into the low half, low doubleword of rs into the high."""
    if ins.rd:
        m.regs[ins.rd] = m.get(ins.rt)
        m.regs_hi[ins.rd] = m.get(ins.rs)


def _op_pcpyud(m: Machine, ins) -> None:
    """High doublewords of rs and rt, packed down."""
    if ins.rd:
        m.regs[ins.rd] = m.regs_hi[ins.rs]
        m.regs_hi[ins.rd] = m.regs_hi[ins.rt]


def _op_pcpyh(m: Machine, ins) -> None:
    """Broadcast the low halfword of each doubleword across it."""
    value = _q_get(m, ins.rt)
    lo_h = value & 0xFFFF
    hi_h = (value >> 64) & 0xFFFF
    out = 0
    for lane in range(4):
        out |= lo_h << (lane * 16)
        out |= hi_h << (64 + lane * 16)
    _q_set(m, ins.rd, out)


_HANDLERS = {
    # MMI (128-bit integer SIMD) subset
    "pand": _q_bitwise(lambda a, b: a & b),
    "por": _q_bitwise(lambda a, b: a | b),
    "pxor": _q_bitwise(lambda a, b: a ^ b),
    "pnor": _q_bitwise(lambda a, b: ~(a | b)),
    "paddb": _q_perbyte(lambda a, b: a + b),
    "psubb": _q_perbyte(lambda a, b: a - b),
    "pcpyld": _op_pcpyld,
    "pcpyud": _op_pcpyud,
    "pcpyh": _op_pcpyh,
    # 128-bit load/store, used for register spills
    "lq": _op_lq,
    "sq": _op_sq,
    # Coprocessor 1 (single precision)
    "lwc1": _op_lwc1,
    "swc1": _op_swc1,
    "mtc1": _op_mtc1,
    "mfc1": _op_mfc1,
    "add.s": _fp_arith(lambda a, b: a + b),
    "sub.s": _fp_arith(lambda a, b: a - b),
    "mul.s": _fp_arith(lambda a, b: a * b),
    "div.s": _op_div_s,
    "max.s": _fp_arith(max),
    "min.s": _fp_arith(min),
    "mov.s": _fp_unary(lambda a: a),
    "neg.s": _fp_unary(lambda a: -a),
    "abs.s": _fp_unary(abs),
    "sqrt.s": _fp_unary(lambda a: a ** 0.5 if a >= 0 else 0.0),
    "cvt.w.s": _op_cvt_w_s,
    "cvt.s.w": _op_cvt_s_w,
    "c.eq.s": _fp_compare(lambda a, b: a == b),
    "c.lt.s": _fp_compare(lambda a, b: a < b),
    "c.le.s": _fp_compare(lambda a, b: a <= b),
    "c.f.s": _fp_compare(lambda a, b: False),
    # I-type arithmetic and logic
    "addiu": _op_addiu,
    "addi": _op_addiu,
    "daddiu": lambda m, i: m.set(i.rt, (m.get(i.rs) + i.imm) & MASK64),
    "daddi": lambda m, i: m.set(i.rt, (m.get(i.rs) + i.imm) & MASK64),
    "lui": _op_lui,
    "andi": _op_andi,
    "ori": _op_ori,
    "xori": _op_xori,
    "slti": _op_slti,
    "sltiu": _op_sltiu,
    # Register arithmetic and logic
    "addu": _alu_rrr(lambda a, b: a + b),
    "add": _alu_rrr(lambda a, b: a + b),
    "subu": _alu_rrr(lambda a, b: a - b),
    "sub": _alu_rrr(lambda a, b: a - b),
    "and": _alu_d64(lambda a, b: a & b),
    "or": _alu_d64(lambda a, b: a | b),
    "xor": _alu_d64(lambda a, b: a ^ b),
    "nor": _alu_d64(lambda a, b: ~(a | b)),
    "daddu": _alu_d64(lambda a, b: a + b),
    "dadd": _alu_d64(lambda a, b: a + b),
    "dsubu": _alu_d64(lambda a, b: a - b),
    "dsub": _alu_d64(lambda a, b: a - b),
    "slt": _op_slt,
    "sltu": _op_sltu,
    "movz": _op_movz,
    "movn": _op_movn,
    # Shifts
    "sll": _shift(lambda v, n: v << n, False),
    "srl": _shift(lambda v, n: (v & MASK32) >> n, False),
    "sra": _shift(lambda v, n: _sign32(v) >> n, False),
    "sllv": _shift(lambda v, n: v << n, True),
    "srlv": _shift(lambda v, n: (v & MASK32) >> n, True),
    "srav": _shift(lambda v, n: _sign32(v) >> n, True),
    "dsll": _shift(lambda v, n: v << n, False, 64),
    "dsrl": _shift(lambda v, n: (v & MASK64) >> n, False, 64),
    "dsra": _shift(lambda v, n: _sign64(v) >> n, False, 64),
    "dsll32": _shift(lambda v, n: v << (n + 32), False, 64),
    "dsrl32": _shift(lambda v, n: (v & MASK64) >> (n + 32), False, 64),
    "dsra32": _shift(lambda v, n: _sign64(v) >> (n + 32), False, 64),
    # Multiply and divide
    "mult": _op_mult,
    "multu": _op_multu,
    "div": _op_div,
    "divu": _op_divu,
    "mfhi": _op_mfhi,
    "mflo": _op_mflo,
    "mthi": _op_mthi,
    "mtlo": _op_mtlo,
    # Loads
    "lb": _load(1, True),
    "lbu": _load(1, False),
    "lh": _load(2, True),
    "lhu": _load(2, False),
    "lw": _load(4, True),
    "lwu": _load(4, False),
    "ld": _load(8, False),
    # Stores
    "sb": _store(1),
    "sh": _store(2),
    "sw": _store(4),
    "sd": _store(8),
}

# Control transfers are handled in _resolve_transfer rather than the dispatch
# table, so they have to be listed explicitly for coverage reporting to be honest.
CONTROL_MNEMONICS = frozenset({
    "j", "b", "jal", "jr", "jalr",
    "beq", "bne", "beql", "bnel",
    "blez", "bgtz", "blezl", "bgtzl",
    "bltz", "bgez", "bltzl", "bgezl", "bltzal", "bgezal",
    "bc1t", "bc1f", "bc1tl", "bc1fl",
})

SUPPORTED_MNEMONICS = frozenset(_HANDLERS) | CONTROL_MNEMONICS | {"nop", "sync"}
