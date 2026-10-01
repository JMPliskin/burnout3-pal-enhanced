"""Disassembler for PlayStation 2 Vector Unit microcode.

The VUs are not MIPS cores and share nothing with the EE instruction set, so the
R5900 decoder cannot read this code at all - and worse, VU words decode as
*plausible* MIPS about 96% of the time, so nothing short of a dedicated decoder
will do.

Microcode is stored as 8-byte bundles holding two instructions issued together:

    bytes 0-3   lower instruction - loads, stores, integer ALU, branches
    bytes 4-7   upper instruction - the floating-point FMAC pipeline

The upper word also carries the bundle's control flags in its top bits:

    I  bit 31   the lower word is a 32-bit float immediate, not an instruction
    E  bit 30   end of microprogram (two more bundles execute after it)
    M  bit 29   start of a MAC-flag-dependent sequence
    D  bit 28   data-dependency stall hint
    T  bit 27   T-bit stop

Bits 24..21 are the destination field mask, naming which of the x/y/z/w lanes a
result is written to; it is rendered as a suffix, so `addx.xy` writes only x and y.

Coverage is reported rather than assumed: `decode_bundle` marks anything it cannot
identify, and `engine/tools/vudis.py --stats` measures how much of a real microcode blob
decodes.  Guessing at an encoding would be worse than admitting the gap.
"""

from __future__ import annotations

import dataclasses
import struct

VF = ["vf%02d" % i for i in range(32)]
VI = ["vi%02d" % i for i in range(32)]

# Distinctive padding encodings, which is what makes microcode findable in a
# stripped image at all.
LOWER_NOP = 0x8000033C
UPPER_NOP_BASE = 0x000002FF

# Bundle control flags, in the upper word.
FLAG_I = 1 << 31
FLAG_E = 1 << 30
FLAG_M = 1 << 29
FLAG_D = 1 << 28
FLAG_T = 1 << 27

_BC = "xyzw"


def _dest(word: int) -> str:
    """Render the destination field mask as a lane suffix."""
    mask = (word >> 21) & 0xF
    # Bit 24 is x, descending to bit 21 for w.
    lanes = "".join(c for c, bit in zip("xyzw", (8, 4, 2, 1)) if mask & bit)
    return lanes or "none"


# -- upper (FMAC) opcodes ------------------------------------------------
#
# 0x00..0x1B come in broadcast groups of four: the low two opcode bits select
# which lane of ft is broadcast across the operation.
_UPPER_BC = {
    0x00: "add", 0x04: "sub", 0x08: "madd", 0x0C: "msub",
    0x10: "max", 0x14: "mini", 0x18: "mul",
}

# Single-operand-source forms using the Q or I special registers.
_UPPER_QI = {
    0x1C: ("mulq", "q"), 0x1D: ("maxi", "i"), 0x1E: ("muli", "i"), 0x1F: ("minii", "i"),
    0x20: ("addq", "q"), 0x21: ("maddq", "q"), 0x22: ("addi", "i"), 0x23: ("maddi", "i"),
    0x24: ("subq", "q"), 0x25: ("msubq", "q"), 0x26: ("subi", "i"), 0x27: ("msubi", "i"),
}

# Plain three-register forms.
_UPPER_REG = {
    0x28: "add", 0x29: "madd", 0x2A: "mul", 0x2B: "max",
    0x2C: "sub", 0x2D: "msub", 0x2E: "opmsub", 0x2F: "mini",
}

# Opcodes 0x3C..0x3F extend into bits 10..6. Only the forms below are treated as
# known; anything else is reported as unknown rather than invented.
_UPPER_SPECIAL = {
    # (opcode, ext): mnemonic
    (0x3C, 0x04): "itof0", (0x3C, 0x05): "itof4", (0x3C, 0x06): "itof12",
    (0x3C, 0x07): "itof15",
    (0x3D, 0x04): "ftoi0", (0x3D, 0x05): "ftoi4", (0x3D, 0x06): "ftoi12",
    (0x3D, 0x07): "ftoi15",
    (0x3E, 0x04): "abs",
    (0x3F, 0x0B): "nop",
    (0x3F, 0x0F): "clip",
}

# Accumulator-writing variants share the 0x3C..0x3F space via the broadcast idiom.
_UPPER_SPECIAL_BC = {
    (0x3C, 0x00): "adda", (0x3C, 0x01): "suba", (0x3C, 0x02): "madda",
    (0x3C, 0x03): "msuba", (0x3D, 0x00): "mula",
    (0x3E, 0x00): "opmula",
}


# -- lower opcodes -------------------------------------------------------
#
# The lower slot is a mix of a few encoding shapes. Branches and the integer ALU
# use the top six bits; the rest live in a special group.
_LOWER_MAIN = {
    0x10: "lq", 0x11: "sq",
    0x14: "ilw", 0x15: "isw",
    0x18: "iaddiu", 0x19: "isubiu",
    0x20: "fceq", 0x21: "fcset", 0x22: "fcand", 0x23: "fcor",
    0x24: "fseq", 0x25: "fsset", 0x26: "fsand", 0x27: "fsor",
    0x28: "fmeq", 0x2A: "fmand", 0x2B: "fmor",
    0x2C: "fcget",
    0x30: "b", 0x31: "bal",
    0x32: "jr", 0x33: "jalr",
    0x34: "ibeq", 0x35: "ibne",
    0x36: "ibltz", 0x37: "ibgtz", 0x38: "iblez", 0x39: "ibgez",
}

# The lower opcode is seven bits wide, not six. The value 0x40 - that is, bit 31
# set with the rest clear - selects a whole special group whose instruction is
# named by bits 5..0 instead, which is where the integer ALU and the elementary
# function unit live.
LOWER_SPECIAL_GROUP = 0x40

# Special group, selected by bits 5..0.
_LOWER_SPECIAL = {
    0x30: "iadd", 0x31: "isub", 0x32: "iaddi",
    0x34: "iand", 0x35: "ior",
}

# Within the special group, 0x3C..0x3F extend again into bits 10..6.
_LOWER_SPECIAL_EXT = {
    (0x3C, 0x04): "itof0", (0x3C, 0x05): "itof4",
    (0x3C, 0x00): "move", (0x3C, 0x01): "mr32",
    (0x3C, 0x06): "lqi", (0x3C, 0x07): "sqi",
    (0x3C, 0x0A): "lqd", (0x3C, 0x0B): "sqd",
    (0x3D, 0x00): "div", (0x3D, 0x01): "sqrt", (0x3D, 0x02): "rsqrt",
    (0x3D, 0x03): "waitq",
    (0x3D, 0x04): "mtir", (0x3D, 0x05): "mfir",
    (0x3D, 0x06): "ilwr", (0x3D, 0x07): "iswr",
    (0x3D, 0x08): "rnext", (0x3D, 0x09): "rget",
    (0x3D, 0x0A): "rinit", (0x3D, 0x0B): "rxor",
    (0x3E, 0x00): "mfp", (0x3E, 0x01): "xtop", (0x3E, 0x02): "xitop",
    (0x3F, 0x00): "xgkick",
    (0x3F, 0x06): "esadd", (0x3F, 0x07): "ersadd",
    (0x3F, 0x0A): "eleng", (0x3F, 0x0B): "erleng",
    (0x3F, 0x0E): "esum", (0x3F, 0x0F): "ercpr",
}


@dataclasses.dataclass(frozen=True)
class VuInstruction:
    """One half of a bundle."""

    raw: int
    mnemonic: str
    operands: tuple = ()
    known: bool = True

    def format(self) -> str:
        if not self.known:
            return ".word 0x%08x" % self.raw
        if not self.operands:
            return self.mnemonic
        return "%-12s %s" % (self.mnemonic, ", ".join(str(o) for o in self.operands))

    def __str__(self) -> str:
        return self.format()


@dataclasses.dataclass(frozen=True)
class Bundle:
    """An 8-byte microcode bundle: one lower and one upper instruction."""

    addr: int
    lower_raw: int
    upper_raw: int
    lower: VuInstruction
    upper: VuInstruction
    immediate: float = None

    @property
    def flags(self) -> str:
        out = []
        for bit, name in ((FLAG_I, "I"), (FLAG_E, "E"), (FLAG_M, "M"),
                          (FLAG_D, "D"), (FLAG_T, "T")):
            if self.upper_raw & bit:
                out.append(name)
        return "".join(out)

    @property
    def is_end(self) -> bool:
        return bool(self.upper_raw & FLAG_E)

    @property
    def fully_known(self) -> bool:
        return self.upper.known and self.lower.known

    def format(self) -> str:
        flags = self.flags
        marker = "[%-5s]" % flags if flags else " " * 7
        if self.immediate is not None:
            lower_text = "loi          %g" % self.immediate
        else:
            lower_text = self.lower.format()
        return "%08x %s %-34s %s" % (self.addr, marker, self.upper.format(), lower_text)


def decode_upper(word: int) -> VuInstruction:
    """Decode the FMAC half of a bundle."""
    opcode = word & 0x3F
    fd, fs, ft = (word >> 6) & 0x1F, (word >> 11) & 0x1F, (word >> 16) & 0x1F
    dest = _dest(word)

    base = opcode & 0x3C
    if base in _UPPER_BC and opcode <= 0x1B:
        name = _UPPER_BC[base]
        lane = _BC[opcode & 0x3]
        return VuInstruction(
            word, "%s%s.%s" % (name, lane, dest),
            (VF[fd], VF[fs], "%s%s" % (VF[ft], lane)),
        )

    if opcode in _UPPER_QI:
        name, special = _UPPER_QI[opcode]
        return VuInstruction(
            word, "%s.%s" % (name, dest), (VF[fd], VF[fs], special.upper())
        )

    if opcode in _UPPER_REG:
        return VuInstruction(
            word, "%s.%s" % (_UPPER_REG[opcode], dest), (VF[fd], VF[fs], VF[ft])
        )

    if opcode >= 0x3C:
        ext = (word >> 6) & 0x1F
        key = (opcode, ext)
        if key in _UPPER_SPECIAL:
            name = _UPPER_SPECIAL[key]
            if name == "nop":
                return VuInstruction(word, "nop")
            if name == "clip":
                return VuInstruction(word, "clip", (VF[fs], "%sw" % VF[ft]))
            return VuInstruction(word, "%s.%s" % (name, dest), (VF[fd], VF[fs]))
        # The accumulator forms reuse the broadcast idiom in this opcode space.
        acc_key = (opcode, ext & 0x3)
        if acc_key in _UPPER_SPECIAL_BC and ext <= 0x03:
            name = _UPPER_SPECIAL_BC[acc_key]
            return VuInstruction(
                word, "%s.%s" % (name, dest), ("ACC", VF[fs], VF[ft])
            )
        return VuInstruction(word, "upper?", (), known=False)

    return VuInstruction(word, "upper?", (), known=False)


def decode_lower(word: int) -> VuInstruction:
    """Decode the load/store/integer/branch half of a bundle."""
    if word == LOWER_NOP:
        return VuInstruction(word, "nop")

    # Seven bits, not six: bit 31 distinguishes the special group from everything
    # else, and masking it away collapses the two spaces together.
    opcode = (word >> 25) & 0x7F
    it, is_ = (word >> 16) & 0x1F, (word >> 11) & 0x1F
    imm11 = word & 0x7FF
    if imm11 & 0x400:
        imm11 -= 0x800

    if opcode == LOWER_SPECIAL_GROUP:
        sub = word & 0x3F
        if sub in _LOWER_SPECIAL:
            name = _LOWER_SPECIAL[sub]
            if name == "iaddi":
                imm5 = (word >> 6) & 0x1F
                if imm5 & 0x10:
                    imm5 -= 0x20
                return VuInstruction(word, name, (VI[it], VI[is_], "%+d" % imm5))
            return VuInstruction(word, name, (VI[(word >> 6) & 0x1F], VI[is_], VI[it]))
        if sub >= 0x3C:
            ext = (word >> 6) & 0x1F
            ext_name = _LOWER_SPECIAL_EXT.get((sub, ext))
            if ext_name is None:
                return VuInstruction(word, "lower?", (), known=False)
            fs, ft = (word >> 11) & 0x1F, (word >> 16) & 0x1F
            if ext_name in ("div", "rsqrt"):
                return VuInstruction(
                    word, ext_name, ("Q", "%s%s" % (VF[fs], _BC[(word >> 21) & 3]),
                                     "%s%s" % (VF[ft], _BC[(word >> 23) & 3])),
                )
            if ext_name == "sqrt":
                return VuInstruction(word, ext_name, ("Q", "%s%s" % (VF[ft], _BC[(word >> 23) & 3])))
            if ext_name in ("waitq", "xgkick", "xtop", "xitop", "mfp"):
                return VuInstruction(word, ext_name, (VI[it],) if ext_name != "waitq" else ())
            if ext_name in ("move", "mr32", "lqi", "sqi", "lqd", "sqd"):
                return VuInstruction(word, "%s.%s" % (ext_name, _dest(word)), (VF[ft], VF[fs]))
            if ext_name in ("mtir", "mfir", "ilwr", "iswr"):
                return VuInstruction(word, ext_name, (VI[it], VF[fs]))
            return VuInstruction(word, ext_name, (VF[ft], VF[fs]))
        return VuInstruction(word, "lower?", (), known=False)

    name = _LOWER_MAIN.get(opcode)
    if name is None:
        return VuInstruction(word, "lower?", (), known=False)

    if name in ("b", "bal"):
        ops = ("%+d" % imm11,) if name == "b" else (VI[it], "%+d" % imm11)
        return VuInstruction(word, name, ops)
    if name in ("jr", "jalr"):
        return VuInstruction(word, name, (VI[is_],))
    if name in ("ibeq", "ibne"):
        return VuInstruction(word, name, (VI[it], VI[is_], "%+d" % imm11))
    if name in ("ibltz", "ibgtz", "iblez", "ibgez"):
        return VuInstruction(word, name, (VI[is_], "%+d" % imm11))
    if name in ("iaddiu", "isubiu"):
        # The immediate is split, with its high bits above the register fields.
        imm15 = (word & 0x7FF) | (((word >> 21) & 0xF) << 11)
        return VuInstruction(word, name, (VI[it], VI[is_], "0x%x" % imm15))
    if name in ("lq", "sq", "ilw", "isw"):
        return VuInstruction(
            word, "%s.%s" % (name, _dest(word)), (VI[it], "0x%x(%s)" % (imm11 & 0x7FF, VI[is_]))
        )
    return VuInstruction(word, name, (VI[it], VI[is_]))


def decode_bundle(lower_word: int, upper_word: int, addr: int = 0) -> Bundle:
    """Decode one 8-byte bundle.

    When the I flag is set the lower word is not an instruction at all but a
    32-bit float constant, so it is reported as an immediate instead.
    """
    upper = decode_upper(upper_word)
    if upper_word & FLAG_I:
        immediate = struct.unpack("<f", struct.pack("<I", lower_word))[0]
        lower = VuInstruction(lower_word, "loi", ("%g" % immediate,))
        return Bundle(addr, lower_word, upper_word, lower, upper, immediate)
    return Bundle(addr, lower_word, upper_word, decode_lower(lower_word), upper)


def decode_blob(data: bytes, base_addr: int = 0):
    """Decode a whole microcode blob into bundles."""
    count = len(data) // 8
    out = []
    for i in range(count):
        lower, upper = struct.unpack_from("<II", data, i * 8)
        out.append(decode_bundle(lower, upper, base_addr + i * 8))
    return out


def coverage(bundles) -> dict:
    """Summarise how much of a blob decoded.

    All-zero bundles are counted separately rather than as successes.  A zero
    upper word technically decodes - opcode 0 is `addx` - but a run of zeroes is
    padding, and letting it inflate the figure would misrepresent how much real
    microcode the decoder actually understands.
    """
    total = len(bundles)
    real = [b for b in bundles if b.lower_raw or b.upper_raw]
    padding = total - len(real)
    denom = len(real)
    upper_ok = sum(1 for b in real if b.upper.known)
    lower_ok = sum(1 for b in real if b.lower.known)
    return {
        "bundles": total,
        "padding": padding,
        "real": denom,
        "upper_known": upper_ok,
        "lower_known": lower_ok,
        "both_known": sum(1 for b in real if b.fully_known),
        "programs": sum(1 for b in bundles if b.is_end),
        "upper_pct": upper_ok / denom * 100.0 if denom else 0.0,
        "lower_pct": lower_ok / denom * 100.0 if denom else 0.0,
    }
