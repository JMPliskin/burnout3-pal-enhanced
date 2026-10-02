"""Structured C++ from machine code: loops and ifs instead of gotos, locals for registers.

Lifted code (lift.py) is one statement per instruction over the register context,
with gotos for control flow: correct and complete, and hard to read. This renders
the same statements in a form a person can follow:

* Control flow is structured. Natural loops become `for (;;)` with `continue` and
  `break`; a two-way branch whose arms meet again at its immediate post-dominator
  becomes `if`/`else`. Whatever does not fit - irreducible flow, jumps into the
  middle of another construct - stays a `goto`, so the result is always faithful.
* Registers are named locals (`a0`, `v0`, `f12`, `hi`), copied from the context on
  entry and back to it around anything that takes the context: calls, syscalls,
  the moves to and from VU0, returns. `out()` and `in()` do the copying.

The output keeps lift.py's interface - `void F_xxxxxxxx(lifted::Context& c)` - so
it is verified exactly as lifted code is, comparing the whole register file and
memory against the original. Functions with jump tables or linking branches are
left to the plain lifted form.
"""

from __future__ import annotations

import re
import struct

from . import lift
from .r5900 import DISASM

GPR_NAMES = ("zero", "at", "v0", "v1", "a0", "a1", "a2", "a3",
             "t0", "t1", "t2", "t3", "t4", "t5", "t6", "t7",
             "s0", "s1", "s2", "s3", "s4", "s5", "s6", "s7",
             "t8", "t9", "k0", "k1", "gp", "sp", "fp", "ra")
SCALARS = ("hi", "lo", "hi1", "lo1", "acc", "fcc", "sa")


class Unstructurable(lift.Refused):
    """This function is left in the plain lifted form."""


class Block:
    def __init__(self, start):
        self.start = start
        self.code = []           # statements before the terminator
        self.kind = "fall"       # fall, jump, cond, return, tail
        self.condition = None
        self.slot = ""           # the delay slot's statement
        self.likely = False
        self.taken = None        # address
        self.next = None         # fallthrough or jump target address
        self.tail = None         # address of a tail transfer
        self.succs = []


def _blocks(code, start, end):
    """Basic blocks keyed by start address, with terminators decoded."""
    inside = lambda address: start <= address < end
    by_address = {ins.addr: ins for ins in code}
    leaders = {start}
    for ins in code:
        if ins.has_delay_slot:
            if ins.mnemonic == "jr" and ins.rs != 31:
                raise Unstructurable("jump table")
            if ins.mnemonic in ("bltzal", "bgezal", "bltzall", "bgezall"):
                raise Unstructurable("linking branch")
            if not ins.is_call and ins.mnemonic not in ("jr", "jalr") and inside(ins.target):
                leaders.add(ins.target)
            if not ins.is_call:
                leaders.add(ins.addr + 8)
    slots = {ins.addr + 4 for ins in code if ins.has_delay_slot}
    if leaders & slots:
        raise Unstructurable("a jump lands in a delay slot")
    leaders = sorted(a for a in leaders if inside(a))
    blocks = {}
    for i, first in enumerate(leaders):
        block = Block(first)
        limit = leaders[i + 1] if i + 1 < len(leaders) else end
        address = first
        while address < limit:
            ins = by_address[address]
            if not ins.has_delay_slot:
                block.code.append(lift.statement(ins))
                address += 4
                continue
            slot = lift.statement(by_address[address + 4])
            m = ins.mnemonic
            after = address + 8
            link = "c.r[31] = 0x%08Xull;" % after
            if m == "jal":
                block.code += [slot, link, "lifted::Call(c, 0x%08Xu);" % ins.target]
                address = after
                continue
            if m == "jalr":
                block.code += ["{ u32 target = (u32)%s;" % lift._r(ins.rs), slot, link,
                               "lifted::Call(c, target); }"]
                address = after
                continue
            block.slot = slot
            if m == "jr":
                block.kind = "return"
            elif m in ("j", "b"):
                if inside(ins.target):
                    block.kind, block.next = "jump", ins.target
                else:
                    block.kind, block.tail = "tail", ins.target
            else:
                block.kind = "cond"
                block.condition = lift._CONDITIONS[m] % {"a": lift._r(ins.rs), "b": lift._r(ins.rt)}
                block.likely = m.endswith("l")
                block.taken = ins.target
                block.next = after
            address = after
            break
        else:
            block.next = limit           # falls into the next block, or off the end
        blocks[first] = block
    return blocks


def _successors(block, blocks):
    out = []
    for target in (block.taken, block.next):
        if target is not None and target in blocks and target not in out:
            out.append(target)
    return out


def _dominators(order, preds, entry):
    """Immediate dominators (Cooper, Harvey and Kennedy)."""
    index = {node: i for i, node in enumerate(order)}
    idom = {entry: entry}

    def intersect(a, b):
        while a != b:
            while index[a] > index[b]:
                a = idom[a]
            while index[b] > index[a]:
                b = idom[b]
        return a

    changed = True
    while changed:
        changed = False
        for node in order[1:]:
            known = [p for p in preds[node] if p in idom]
            if not known:
                continue
            new = known[0]
            for p in known[1:]:
                new = intersect(p, new)
            if idom.get(node) != new:
                idom[node] = new
                changed = True
    return idom


def _reverse_postorder(entry, succs):
    seen, order = set(), []

    def visit(node):
        stack = [(node, iter(succs[node]))]
        seen.add(node)
        while stack:
            current, children = stack[-1]
            child = next(children, None)
            if child is None:
                order.append(current)
                stack.pop()
            elif child not in seen:
                seen.add(child)
                stack.append((child, iter(succs[child])))

    visit(entry)
    return list(reversed(order))


class _Emitter:
    def __init__(self, blocks, start, end):
        self.blocks = blocks
        self.end = end
        self.succs = {a: _successors(b, blocks) for a, b in blocks.items()}
        self.order = _reverse_postorder(start, self.succs)
        reachable = set(self.order)
        preds = {a: [] for a in reachable}
        for a in reachable:
            for s in self.succs[a]:
                preds[s].append(a)
        self.idom = _dominators(self.order, preds, start)
        # Post-dominators: dominators of the reversed graph from a virtual exit.
        exit_node = "exit"
        rsuccs = {exit_node: []}
        rpreds = {a: [] for a in reachable}
        rpreds[exit_node] = []
        for a in reachable:
            rsuccs.setdefault(a, [])
            outs = [s for s in self.succs[a]]
            if not outs or blocks[a].kind in ("return", "tail") or (
                    blocks[a].kind in ("fall", "cond") and blocks[a].next not in blocks):
                outs = outs + [exit_node]
            for s in outs:
                rsuccs.setdefault(s, []).append(a)
        rorder = _reverse_postorder(exit_node, rsuccs)
        rpred = {n: [] for n in rorder}
        for n in rorder:
            for s in rsuccs.get(n, []):
                if s in rpred:
                    rpred[s].append(n)
        self.ipdom = _dominators(rorder, rpred, exit_node)
        # Loops: a back edge goes to a block that dominates its source.
        self.headers = set()
        self.loops = {}
        for a in reachable:
            for s in self.succs[a]:
                if self._dominates(s, a):
                    self.headers.add(s)
                    self.loops.setdefault(s, set()).add(a)
        self.body = {h: self._natural_loop(h, sources, preds) for h, sources in self.loops.items()}
        self.emitted = set()
        self.labels = set()

    def _dominates(self, a, b):
        while True:
            if a == b:
                return True
            parent = self.idom.get(b)
            if parent is None or parent == b:
                return False
            b = parent

    def _natural_loop(self, header, sources, preds):
        body, work = {header}, list(sources)
        while work:
            node = work.pop()
            if node not in body:
                body.add(node)
                work.extend(preds[node])
        return body

    def _follow(self, header):
        """The single block a loop exits to, or None."""
        exits = {s for n in self.body[header] for s in self.succs[n] if s not in self.body[header]}
        return next(iter(exits)) if len(exits) == 1 else None

    def emit(self, node, stop, loops, depth):
        lines = []
        pad = "    " * depth
        while node is not None and node != stop:
            if node not in self.blocks:
                lines.append(pad + "out(); lifted::TailCall(c, 0x%08Xu, entry_ra, entry_sp); return;" % node)
                return lines
            if loops and node not in self.body[loops[-1][0]]:
                # Leaving the loop: break, or a goto to wherever it goes.
                lines.append(self._transfer(node, loops, depth) or (pad + "break;"))
                return lines
            if node in self.emitted:
                lines.append(pad + "goto L_%08X;" % node)
                self.labels.add(node)
                return lines
            if node in self.headers and (not loops or loops[-1][0] != node):
                follow = self._follow(node)
                lines.append(pad + "for (;;) {")
                lines.append(pad + "    lifted::Tick(c, 0x%08Xu);" % node)
                inner = self._block(node, None, loops + [(node, follow)], depth + 1)
                lines += inner
                lines.append(pad + "}")
                node = follow
                continue
            body, node = self._chain(node, stop, loops, depth)
            lines += body
        return lines

    def _block(self, node, stop, loops, depth):
        """The header's code then the rest of the loop body (`node` already marked)."""
        lines, nxt = self._chain(node, stop, loops, depth)
        if nxt is not None:
            lines += self.emit(nxt, stop, loops, depth)
        return lines

    def _transfer(self, target, loops, depth):
        """The statement for an edge to `target`, or None to fall into it."""
        pad = "    " * depth
        if target is not None and target not in self.blocks:
            return pad + "out(); lifted::TailCall(c, 0x%08Xu, entry_ra, entry_sp); return;" % target
        if loops:
            header, follow = loops[-1]
            if target == header:
                return pad + "continue;"
            if target == follow:
                return pad + "break;"
            if target not in self.body[header]:
                self.labels.add(target)
                return pad + "goto L_%08X;" % target
        if target in self.emitted:
            self.labels.add(target)
            return pad + "goto L_%08X;" % target
        return None

    def _chain(self, node, stop, loops, depth):
        """Emit one block; returns (lines, the block to continue with or None)."""
        pad = "    " * depth
        block = self.blocks[node]
        self.emitted.add(node)
        lines = ["@L_%08X" % node]          # a label placeholder, kept if targeted
        lines += [pad + s for s in block.code if s]
        if block.kind == "return":
            lines += [pad + s for s in (block.slot,) if s]
            lines.append(pad + "out(); return;")
            return lines, None
        if block.kind == "tail":
            lines += [pad + s for s in (block.slot,) if s]
            lines.append(pad + "out(); lifted::TailCall(c, 0x%08Xu, entry_ra, entry_sp); return;" % block.tail)
            return lines, None
        if block.kind in ("fall", "jump"):
            if block.slot:
                lines.append(pad + block.slot)
            target = block.next
            if target is not None and target >= self.end and target not in self.blocks:
                lines.append(pad + "out(); lifted::TailCall(c, 0x%08Xu, entry_ra, entry_sp); "
                             "return;  /* runs off the end */" % target)
                return lines, None
            jump = self._transfer(target, loops, depth)
            if jump is not None:
                lines.append(jump)
                return lines, None
            return lines, target
        # A two-way branch.
        taken, fall = block.taken, block.next
        if block.likely:
            lines.append(pad + "if (%s) {" % block.condition)
            if block.slot:
                lines.append(pad + "    " + block.slot)
        else:
            lines.append(pad + "{ const bool t = %s;" % block.condition)
            if block.slot:
                lines.append(pad + "  " + block.slot)
            lines.append(pad + "  if (t) {")
        join = self.ipdom.get(node)
        if join == "exit" or (loops and join not in self.body[loops[-1][0]]):
            join = None
        inner = depth + (1 if block.likely else 1)
        lines += self._arm(taken, join, loops, inner + (0 if block.likely else 0))
        lines.append(pad + ("} else {" if block.likely else "  } else {"))
        lines += self._arm(fall, join, loops, inner)
        lines.append(pad + ("}" if block.likely else "  } }"))
        if join is None:
            return lines, None
        jump = self._transfer(join, loops, depth)
        if jump is not None:
            lines.append(jump)
            return lines, None
        return lines, join

    def _arm(self, target, join, loops, depth):
        if target == join:
            return []
        jump = self._transfer(target, loops, depth + 1)
        if jump is not None:
            return [jump]
        return self.emit(target, join, loops, depth + 1)


def _localize(text, gprs, fprs, scalars):
    text = re.sub(r"c\.r\[(\d+)\]", lambda m: _gpr(int(m.group(1)), gprs), text)
    text = re.sub(r"c\.f\[(\d+)\]", lambda m: _fpr(int(m.group(1)), fprs), text)
    for name in SCALARS:
        if re.search(r"\bc\.%s\b" % name, text):
            scalars.add(name)
            text = re.sub(r"\bc\.%s\b" % name, name, text)
    return text


def _gpr(index, used):
    used.add(index)
    return GPR_NAMES[index] if index else "0ull"


def _fpr(index, used):
    used.add(index)
    return "f%d" % index


# Statements that hand the context to something else; registers are synchronised
# around them.
_TAKES_CONTEXT = re.compile(r"lifted::(Call|TailCall|Syscall|Cop2Move)\(c")


def structure_function(image: bytes, base: int, start: int, end: int) -> str:
    """Structured C++ for one function; raises lift.Refused if it cannot be."""
    lift.lift_function(image, base, start, end)       # the same refusals as lifting
    count = (end - start) // 4
    words = struct.unpack_from("<%dI" % count, image, start - base)
    code = [DISASM.decode(word, start + 4 * i) for i, word in enumerate(words)]
    blocks = _blocks(code, start, end)
    emitter = _Emitter(blocks, start, end)
    body = emitter.emit(start, None, [], 1)
    # Blocks only reachable by a goto - from inside a loop, say - follow the main
    # body, each under its label.
    for node in emitter.order:
        if node not in emitter.emitted:
            body += emitter.emit(node, None, [], 1)
    gprs, fprs, scalars = set(), set(), set()
    out_lines = []
    for line in body:
        if line.startswith("@L_"):
            address = int(line[3:], 16)
            if address in emitter.labels:
                out_lines.append("L_%08X:;" % address)
            continue
        localized = _localize(line, gprs, fprs, scalars)
        if _TAKES_CONTEXT.search(localized) and "out();" not in localized:
            indent = localized[:len(localized) - len(localized.lstrip())]
            localized = "%sout(); %s in();" % (indent, localized.strip())
        out_lines.append(localized)
    gprs.discard(0)
    names = [(GPR_NAMES[i], "c.r[%d]" % i) for i in sorted(gprs)]
    names += [("f%d" % i, "c.f[%d]" % i) for i in sorted(fprs)]
    names += [(n, "c.%s" % n) for n in SCALARS if n in scalars]
    types = {"fcc": "u32", "acc": "u32", "sa": "u32"}
    lines = ["void %s(lifted::Context& c) {" % lift.name_of(start),
             "    const u32 entry_ra = (u32)c.r[31], entry_sp = (u32)c.r[29];",
             "    (void)entry_ra; (void)entry_sp;"]
    for name, source in names:
        kind = "u32" if name.startswith("f") and name[1:].isdigit() else types.get(name, "u64")
        lines.append("    %s %s = %s;" % (kind, name, source))
    lines.append("    auto out = [&] { %s };" % " ".join("%s = %s;" % (s, n) for n, s in names))
    lines.append("    auto in = [&] { %s };" % " ".join("%s = %s;" % (n, s) for n, s in names))
    lines.append("    (void)out; (void)in;")
    lines += out_lines
    lines.append("}")
    return "\n".join(lines) + "\n"
