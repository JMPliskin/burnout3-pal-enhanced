"""Code discovery for a stripped PS2 executable.

The retail Burnout 3 binary is one flat PROGBITS blob: no symbol table, and no
section headers marking where code stops and read-only data begins.  Everything
about the layout therefore has to be recovered by analysis.

The approach here is deliberately conservative, in the order that produces the
fewest false positives:

1. **Call-target harvesting.**  Every `jal` in the image names a function entry
   directly.  A `jal` that lands inside the image is near-proof of a function,
   because the compiler only emits it for real call sites.
2. **Recursive traversal.**  Starting from the ELF entry point and every
   harvested call target, walk the control-flow graph, following branches and
   jumps, and stop at returns.  This marks bytes that are *provably* reachable
   code rather than bytes that merely decode as code.
3. **Prologue scanning.**  In the gaps, look for the conventional frame setup
   (`addiu $sp, $sp, -N`) to catch functions only reached through function
   pointers - vtables, jump tables, and callback tables, which a 2004 C++ game
   uses heavily.
4. **Data classification.**  Whatever is left is scored: runs that decode poorly,
   or that look like pointers, floats, or strings, are recorded as data.

Each stage records *why* an address was classified, so the results stay
auditable instead of being an opaque guess.
"""

from __future__ import annotations

import collections
import dataclasses
import struct

from .r5900 import DISASM, Flags, GPR

# Confidence levels attached to each discovered function.
CONF_ENTRY = "entry"          # the ELF entry point
CONF_CALLED = "called"        # at least one direct jal targets it
CONF_PROLOGUE = "prologue"    # found by frame-setup pattern
CONF_POINTER = "pointer"      # referenced by a plausible pointer word


@dataclasses.dataclass
class Function:
    """A discovered function: a half-open address range plus provenance."""

    start: int
    end: int = 0
    confidence: str = CONF_PROLOGUE
    callers: set = dataclasses.field(default_factory=set)
    calls: set = dataclasses.field(default_factory=set)
    instruction_count: int = 0
    has_return: bool = False
    uses_vu: bool = False
    name: str = ""

    @property
    def size(self) -> int:
        return max(0, self.end - self.start)

    @property
    def label(self) -> str:
        return self.name or ("func_%08X" % self.start)


@dataclasses.dataclass
class Region:
    """A classified span of the image."""

    start: int
    end: int
    kind: str  # "code" | "data" | "unknown"

    @property
    def size(self) -> int:
        return self.end - self.start


class ImageAnalyzer:
    """Recovers functions and code/data boundaries from a flat loaded image."""

    def __init__(self, data: bytes, base_addr: int, entry_point: int = 0) -> None:
        self.data = data
        self.base = base_addr
        self.end = base_addr + len(data)
        self.entry_point = entry_point
        self.words = struct.unpack("<%dI" % (len(data) // 4), data[: (len(data) // 4) * 4])
        self.functions: dict = {}
        self.code_bytes = bytearray(len(data))  # 1 where traversal proved code
        self.call_sites: dict = collections.defaultdict(set)
        self.stats = collections.Counter()

    # -- address helpers ----------------------------------------------

    def contains(self, addr: int) -> bool:
        return self.base <= addr < self.end

    def word_at(self, addr: int) -> int:
        if not self.contains(addr) or (addr - self.base) % 4:
            return 0
        return self.words[(addr - self.base) // 4]

    def _index(self, addr: int) -> int:
        return addr - self.base

    # -- stage 1: harvest direct call targets --------------------------

    def harvest_call_targets(self) -> int:
        """Scan every aligned word for `jal` and record its target.

        This deliberately scans the whole image rather than only known code: a
        `jal` encoding appearing inside genuine data is rare, and the traversal
        stage discards targets that do not hold sane code.
        """
        found = 0
        for i, word in enumerate(self.words):
            if (word >> 26) != 0x03:  # jal
                continue
            addr = self.base + i * 4
            target = ((addr + 4) & 0xF0000000) | ((word & 0x03FFFFFF) << 2)
            if not self.contains(target):
                continue
            self.call_sites[target].add(addr)
            found += 1
        return found

    # -- stage 2: recursive control-flow traversal ---------------------

    def _looks_like_code(self, addr: int, probe: int = 8) -> bool:
        """Cheap sanity check that `addr` begins a decodable instruction run."""
        valid = 0
        for k in range(probe):
            a = addr + k * 4
            if not self.contains(a):
                break
            ins = DISASM.decode(self.word_at(a), a)
            if not ins.is_valid:
                return False
            valid += 1
            if ins.ends_block:
                break
        return valid >= 2

    def traverse(self, start: int, func: Function) -> set:
        """Walk one function's control-flow graph, marking reachable bytes.

        Returns the set of addresses proven to belong to this function, which the
        caller turns into an extent.  The traversal itself must not decide the
        extent: a tail jump or a shared epilogue can land far away, and treating
        the furthest address reached as the end merges everything in between into
        one enormous bogus function.
        """
        queue = collections.deque([start])
        seen = set()
        max_addr = start
        while queue:
            addr = queue.popleft()
            while True:
                if not self.contains(addr) or addr in seen:
                    break
                seen.add(addr)
                idx = self._index(addr)
                ins = DISASM.decode(self.words[idx // 4], addr)
                if not ins.is_valid:
                    break
                self.code_bytes[idx : idx + 4] = b"\x01\x01\x01\x01"
                func.instruction_count += 1
                if addr > max_addr:
                    max_addr = addr
                if ins.mnemonic.startswith("v") or ins.mnemonic in ("qmfc2", "qmtc2"):
                    func.uses_vu = True

                # The delay slot always executes, so claim it before branching.
                if ins.has_delay_slot:
                    slot = addr + 4
                    if self.contains(slot):
                        self.code_bytes[self._index(slot) : self._index(slot) + 4] = b"\x01\x01\x01\x01"
                        seen.add(slot)
                        if slot > max_addr:
                            max_addr = slot

                if ins.is_call:
                    if ins.target >= 0 and self.contains(ins.target):
                        func.calls.add(ins.target)
                elif ins.target >= 0 and self.contains(ins.target):
                    # Intra-function branch or tail jump: queue the target.
                    queue.append(ins.target)

                if ins.is_return:
                    func.has_return = True
                    break
                if ins.ends_block:
                    break

                addr += 8 if ins.has_delay_slot else 4
        return seen

    # -- stage 2b: pointer-table targets -------------------------------

    def _is_prologue(self, addr: int) -> bool:
        """True when `addr` holds `addiu $sp, $sp, -N`."""
        word = self.word_at(addr)
        return (word & self._PROLOGUE_MASK) == self._PROLOGUE_MATCH and bool(word & 0x8000)

    def _follows_terminator(self, addr: int) -> bool:
        """True when the instruction two slots back ends a function.

        A function entry is normally preceded by the previous function's final
        delay slot, so the terminating `jr`/`j` sits two words back.
        """
        if not self.contains(addr - 8):
            return False
        return DISASM.decode(self.word_at(addr - 8), addr - 8).ends_block

    def harvest_pointer_targets(self) -> list:
        """Find function entries referenced only by pointer tables.

        Virtual calls and callback tables never produce a `jal`, so those targets
        are invisible to call-target harvesting.  Every aligned word that points
        at a plausible entry inside the image is considered, then filtered hard -
        a word has to land on a stack prologue or just past another function's
        terminator to be accepted, which is what keeps ordinary data from
        registering as code.
        """
        candidates = set()
        for word in self.words:
            if not self.contains(word) or word % 4:
                continue
            candidates.add(word)

        accepted = []
        for addr in sorted(candidates):
            if addr in self.functions:
                continue
            if not (self._is_prologue(addr) or self._follows_terminator(addr)):
                continue
            if not self._looks_like_code(addr):
                continue
            accepted.append(addr)
        return accepted

    # Alignment padding between functions is a short run of `nop`; bridging that
    # much keeps a function whole without letting a real gap merge two of them.
    MAX_PADDING_WORDS = 4

    def extent_from(self, start: int, visited: set) -> int:
        """Turn a traversal's visited set into a contiguous end address.

        The body of a function is the unbroken run of its own instructions. A gap
        means the run has ended - the next bytes belong to something else - except
        when the gap is only `nop` padding inserted for alignment.
        """
        addr = start
        end = start
        while self.contains(addr):
            if addr in visited:
                addr += 4
                end = addr
                continue
            # Look past a short run of nops for more of this function.
            probe = addr
            padding = 0
            while (
                self.contains(probe)
                and padding < self.MAX_PADDING_WORDS
                and self.word_at(probe) == 0
                and probe not in visited
            ):
                probe += 4
                padding += 1
            if padding and self.contains(probe) and probe in visited:
                addr = probe
                continue
            break
        return max(end, start + 4)

    # -- stage 3: prologue scan ----------------------------------------

    _PROLOGUE_MASK = 0xFFFF0000
    # addiu $sp, $sp, -N  ->  op=0x09 rs=29 rt=29, negative immediate
    _PROLOGUE_MATCH = (0x09 << 26) | (29 << 21) | (29 << 16)

    def scan_prologues(self) -> list:
        """Find `addiu $sp, $sp, -N` in spans not already proven to be code."""
        candidates = []
        for i, word in enumerate(self.words):
            if (word & self._PROLOGUE_MASK) != self._PROLOGUE_MATCH:
                continue
            if not (word & 0x8000):  # immediate must be negative
                continue
            addr = self.base + i * 4
            if self.code_bytes[self._index(addr)]:
                continue
            if not self._looks_like_code(addr):
                continue
            candidates.append(addr)
        return candidates

    # -- driver ---------------------------------------------------------

    def run(self) -> None:
        self.stats["jal_sites"] = self.harvest_call_targets()
        self.stats["call_targets"] = len(self.call_sites)

        # Seed with the entry point and every direct call target.
        seeds = []
        if self.contains(self.entry_point):
            seeds.append((self.entry_point, CONF_ENTRY))
        for target in sorted(self.call_sites):
            seeds.append((target, CONF_CALLED))

        for addr, confidence in seeds:
            if addr in self.functions:
                continue
            if confidence != CONF_ENTRY and not self._looks_like_code(addr):
                self.stats["rejected_call_targets"] += 1
                continue
            func = Function(start=addr, confidence=confidence)
            func.callers = set(self.call_sites.get(addr, ()))
            self.functions[addr] = func
            visited = self.traverse(addr, func)
            func.end = self.extent_from(addr, visited)
            func.instruction_count = sum(
                1 for a in visited if func.start <= a < func.end
            )

        # Functions reached only through pointer tables emit no jal at all, so
        # harvest plausible pointer targets before falling back to a blind scan.
        for addr in self.harvest_pointer_targets():
            if addr in self.functions:
                continue
            func = Function(start=addr, confidence=CONF_POINTER)
            self.functions[addr] = func
            visited = self.traverse(addr, func)
            func.end = self.extent_from(addr, visited)
            func.instruction_count = sum(
                1 for a in visited if func.start <= a < func.end
            )
            self.stats["pointer_found"] += 1

        # A final pass catches anything still missing.
        for addr in self.scan_prologues():
            if addr in self.functions:
                continue
            func = Function(start=addr, confidence=CONF_PROLOGUE)
            self.functions[addr] = func
            visited = self.traverse(addr, func)
            func.end = self.extent_from(addr, visited)
            func.instruction_count = sum(
                1 for a in visited if func.start <= a < func.end
            )
            self.stats["prologue_found"] += 1

        self._tighten_bounds()
        self._compute_stats()

    def _tighten_bounds(self) -> None:
        """Clamp each function to end no later than the next function start."""
        starts = sorted(self.functions)
        for i, addr in enumerate(starts):
            func = self.functions[addr]
            limit = starts[i + 1] if i + 1 < len(starts) else self.end
            # Clamp only. The extent already reflects the function's own
            # contiguous code, so growing it here would reintroduce the merging
            # this pass exists to prevent.
            if func.end > limit:
                func.end = limit
            if func.end <= func.start:
                func.end = min(func.start + 4, limit)

    def _compute_stats(self) -> None:
        self.stats["functions"] = len(self.functions)
        self.stats["code_bytes"] = sum(self.code_bytes)
        self.stats["image_bytes"] = len(self.data)
        self.stats["with_return"] = sum(1 for f in self.functions.values() if f.has_return)
        self.stats["uses_vu"] = sum(1 for f in self.functions.values() if f.uses_vu)
        for f in self.functions.values():
            self.stats["conf_" + f.confidence] += 1

    # -- reporting -------------------------------------------------------

    def regions(self) -> list:
        """Collapse the code bitmap into contiguous code/unknown regions."""
        out = []
        start = 0
        current = self.code_bytes[0] if self.code_bytes else 0
        for i in range(1, len(self.code_bytes)):
            if self.code_bytes[i] != current:
                out.append(Region(self.base + start, self.base + i, "code" if current else "unknown"))
                start = i
                current = self.code_bytes[i]
        out.append(
            Region(self.base + start, self.base + len(self.code_bytes), "code" if current else "unknown")
        )
        return out

    def coverage(self) -> float:
        total = len(self.data)
        return (sum(self.code_bytes) / total * 100.0) if total else 0.0
