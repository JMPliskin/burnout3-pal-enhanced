"""Render derived functions against recovered struct types.

The emitted code stays address-based - that is what lets it be compiled and run
against the interpreter - but a value whose type was recovered becomes a *view*:
a thin object over a guest address with one accessor per field. A pointer field's
accessor returns the view of the type it points to, so a chain of loads reads as
a chain of field accesses:

    ReadS32(ReadS32(arg0 + 4) + 8)      becomes      arg0.f_04().f_08()

A load is shown as a field access only when it matches the field exactly - same
offset, same width and signedness as the field's recovered read. Anything else
stays a plain Read* call on the view's `.address`, so a type that is wrong or
incomplete can make the source less tidy but never different in meaning; and the
compiled-code check runs every function in this form.
"""

from __future__ import annotations

from .paths import _split_address
from .structs import Catalog, _offset_name
from .symexec import Arg, Load, children

READERS = {
    (1, True): "ReadS8", (1, False): "ReadU8",
    (2, True): "ReadS16", (2, False): "ReadU16",
    (4, True): "ReadS32", (4, False): "ReadU32",
    (8, False): "ReadU64",
}
WRITERS = {1: "Write8", 2: "Write16", 4: "Write32", 8: "Write64"}
VALUE_TYPES = {
    (1, True): "s8", (1, False): "u8", (2, True): "s16", (2, False): "u16",
    (4, True): "s32", (4, False): "u32", (8, False): "u64",
}


def _hex(value: int, signed: bool = False) -> str:
    text = "0x%X" % abs(value)
    if value < 0:
        return "-" + text
    return "+" + text if signed else text


def embedded_accessor(offset: int) -> str:
    return "e_%s" % _offset_name(offset)


def accessor(offset: int) -> str:
    return "f_%s" % _offset_name(offset)


def _signed_offset(offset: int) -> int:
    offset &= 0xFFFFFFFF
    return offset - (1 << 32) if offset & 0x80000000 else offset


class Typing:
    """Type information for one function's expressions."""

    def __init__(self, catalog: Catalog, function: int) -> None:
        self.catalog = catalog
        self.function = function
        self.used = set()

    def type_of(self, expr):
        """The recovered type of a value, as a catalog root, or None."""
        if isinstance(expr, Arg):
            return self.catalog.slot(self.function, expr.index)
        if isinstance(expr, Load):
            access = self.access(expr)
            if access is not None:
                _base, _path, owner, local = access
                return self.catalog.pointee(owner, local)
        return None

    def _locate(self, address):
        """(base, path, owner, local, field) for a typed address, else None."""
        try:
            base, offset = _split_address(address)
        except Exception:
            return None
        if base is None:
            return None
        root = self.type_of(base)
        if root is None:
            return None
        found = self.catalog.locate(root, _signed_offset(offset))
        if found is None:
            return None
        path, owner, local, field = found
        return base, path, owner, local, field

    def access(self, load):
        """(base, embed path, owner, offset) when a load is exactly a field read."""
        found = self._locate(load.address)
        if found is None:
            return None
        base, path, owner, local, field = found
        if self.catalog.read_flavour(field) != (load.size, load.signed):
            return None
        return base, path, owner, local

    def store_target(self, store):
        """(base, embed path, owner, offset) when a store is exactly a field write."""
        found = self._locate(store.address)
        if found is None:
            return None
        base, path, owner, local, field = found
        if self.catalog.write_width(field) != store.size:
            return None
        return base, path, owner, local

    def object(self, base, path) -> str:
        """The view a field belongs to: the base's view, then any embedded parts."""
        text = self.view(base)
        for at, inner in path:
            self.used.add(inner)
            text += ".%s()" % embedded_accessor(at)
        return text

    def view(self, expr) -> str:
        """Render a typed value as its view object."""
        root = self.type_of(expr)
        assert root is not None
        self.used.add(root)
        if isinstance(expr, Arg):
            return "arg%d" % expr.index
        base, path, owner, local = self.access(expr)
        self.used.add(owner)
        return "%s.%s()" % (self.object(base, path), accessor(local))

    def type_name(self, root) -> str:
        return self.catalog.names[root]

    # -- hooks for the symexec renderer ------------------------------------

    def render_arg(self, node) -> str:
        if self.type_of(node) is not None:
            self.used.add(self.type_of(node))
            return "arg%d.address" % node.index
        return "arg%d" % node.index

    def render_load(self, node, render_child) -> str:
        access = self.access(node)
        if access is not None:
            base, path, owner, offset = access
            self.used.add(owner)
            text = "%s.%s()" % (self.object(base, path), accessor(offset))
            if self.type_of(node) is not None:
                self.used.add(self.type_of(node))
                return text + ".address"
            return text
        reader = READERS.get((node.size, node.signed))
        if reader is None:
            from .symexec import Unsupported

            raise Unsupported("no reader for %d-byte load" % node.size)
        return "%s(%s)" % (reader, render_child(node.address))


def header(catalog: Catalog, roots, source_name: str) -> str:
    """The view classes for the given types, as a C++ header."""
    roots = sorted(roots, key=lambda root: catalog.names[root])
    included = set(roots)
    lines = [
        "/*",
        " * Recovered struct types - GENERATED, do not edit by hand.",
        " *",
        " * Produced by tools/derive.py from %s, using the type recovery in" % source_name,
        " * tools/b3lib/structs.py. Each type was inferred from how the original code",
        " * uses pointers: the offsets and widths it accesses, and the arguments, return",
        " * values and pointer fields that carry the same object between functions.",
        " *",
        " * A type is a *view* over a guest address rather than a C++ struct with the",
        " * original layout, because the derived code runs against an emulated address",
        " * space. Each accessor reads or writes one field; a pointer field returns the",
        " * view of the type it points to. Only types the derived functions use are",
        " * listed, each with every field the recovery found, including fields no",
        " * derived function touches yet.",
        " *",
        " * The names are placeholders derived from the evidence: S_0035F0A0_a0 is the",
        " * type of argument 0 of func_0035F0A0, and S_..._p1C is what its field 0x1C",
        " * points to.",
        " */",
        "",
        "#ifndef B3_GENERATED_TYPES_H",
        "#define B3_GENERATED_TYPES_H",
        "",
        '#include "b3/generated.h"',
        "",
        "namespace generated {",
        "",
        "/* Every view is a guest address; this is the shared base. */",
        "struct View {",
        "    u64 address;",
        "    View(u64 value = 0) : address(value) {}",
        "};",
        "",
    ]
    for root in roots:
        lines.append("struct %s;" % catalog.names[root])
    lines.append("")

    definitions = []
    for root in roots:
        name = catalog.names[root]
        fields = catalog.roots[root]
        offsets = sorted(fields)
        lines.append("/* %d function(s); %d field(s) at %s..%s */"
                     % (len(catalog.functions(root)), len(fields),
                        _hex(offsets[0]), _hex(offsets[-1])))
        lines.append("struct %s : View {" % name)
        lines.append("    using View::View;")
        for offset in offsets:
            field = fields[offset]
            flavour = catalog.read_flavour(field)
            width = catalog.write_width(field)
            target = catalog.pointee(root, offset)
            kind = catalog.kind(field)
            note = "%d function(s)" % len(field.functions)
            displacement = ("address + 0x%X" % offset if offset > 0 else
                            "address - 0x%X" % -offset if offset < 0 else "address")
            if flavour is not None and flavour in READERS:
                if target is not None and target in included:
                    returns = catalog.names[target]
                    describe = "-> %s" % returns
                else:
                    returns = "u64"
                    describe = "f32 bits" if kind == "float" else VALUE_TYPES[flavour]
                    if target is not None:
                        describe = "pointer to %s" % catalog.names[target]
                lines.append("    %s %s() const;  /* %s  %s, %s */"
                             % (returns, accessor(offset), _hex(offset, signed=True),
                                describe, note))
                definitions.append("inline %s %s::%s() const { return %s(%s); }"
                                   % (returns, name, accessor(offset), READERS[flavour],
                                      displacement))
            if width is not None and width in WRITERS:
                lines.append("    void set_%s(u64 value) const;" % accessor(offset))
                definitions.append(
                    "inline void %s::set_%s(u64 value) const { %s(%s, value); }"
                    % (name, accessor(offset), WRITERS[width], displacement)
                )
        for at, inner in sorted(catalog.embedded(root).items()):
            if inner not in included:
                continue
            lines.append("    %s %s() const;  /* %s  embedded %s */"
                         % (catalog.names[inner], embedded_accessor(at), _hex(at, signed=True),
                            catalog.names[inner]))
            definitions.append("inline %s %s::%s() const { return %s(address + 0x%X); }"
                               % (catalog.names[inner], name, embedded_accessor(at),
                                  catalog.names[inner], at))
        lines.append("};")
        lines.append("")
    lines.append("/* Accessor definitions, after every type is complete. */")
    lines.extend(definitions)
    lines += ["", "}  // namespace generated", "", "#endif  /* B3_GENERATED_TYPES_H */", ""]
    return "\n".join(lines)


def _walk(expr):
    yield expr
    for child in children(expr):
        yield from _walk(child)


def _accesses(derivation):
    """Every (address, size, signed, is_store) a derivation performs."""
    seen = set()
    for path in derivation.paths:
        roots = list(path.conditions)
        for store in path.stores:
            roots += [store.address, store.value]
        if path.expression is not None:
            roots.append(path.expression)
        for root in roots:
            for node in _walk(root):
                if isinstance(node, Load) and id(node) not in seen:
                    seen.add(id(node))
                    yield node.address, node.size, node.signed, False
        for store in path.stores:
            yield store.address, store.size, None, True


def add_derived_evidence(graph, verified, rounds: int = 4) -> int:
    """Add the derived functions' own accesses to the type graph as evidence.

    The scan that builds the graph cannot follow a variable index, so an array of
    records walked by a loop contributes nothing. The derivations can: an unrolled
    loop appears as every concrete access it makes, and each one was verified
    against the original. So any access through a typed base at an offset the type
    does not yet have becomes a field, and an argument that has no type at all but
    is dereferenced - typically only inside an inlined callee - gets one.

    Repeated because each new field can type a pointer loaded from it.
    """
    added = 0
    for _round in range(rounds):
        catalog = Catalog(graph)
        before = added
        for func, derivation, _tested, _strong in verified:
            typing = Typing(catalog, func["addr"])
            for address, size, signed, is_store in _accesses(derivation):
                if size not in (1, 2, 4, 8):
                    continue
                try:
                    base, offset = _split_address(address)
                except Exception:
                    continue
                if base is None:
                    continue
                offset = _signed_offset(offset)
                root = typing.type_of(base)
                if root is None:
                    if not isinstance(base, Arg):
                        continue
                    node = graph.node(("arg", func["addr"], base.index))
                elif catalog.locate(root, offset) is not None:
                    continue
                else:
                    node = root
                field = graph.field(node, offset)
                if not field.uses:
                    added += 1
                # A derived load cannot tell `lwc1` from `lwu`, so it says nothing
                # about whether the field is a float.
                field.uses[(size, "derived", bool(signed) if not is_store else None,
                            is_store)] += 1
                field.functions.add(func["addr"])
        if added == before:
            break
    return added
