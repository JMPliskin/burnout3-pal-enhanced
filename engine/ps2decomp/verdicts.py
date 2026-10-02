"""Verification verdicts remembered between runs.

A lifted or structured function is verified by compiling its C++ and running it
against the original on test cases. Its callees run in the interpreter, from the
original machine code, so the verdict depends on nothing but

* the function's own C++ text, and whether it is checked whole or at the
  calling-convention level;
* the environment: the interpreter, the test memory and its seeding, the
  runtime it is compiled with, the verification code, and the game's image
  and analysis.

The text goes into each key; the environment is one fingerprint over the files
that make it up. Change any of them and every verdict is recomputed. Change only
code that produces text - structuring, folding, naming - and only the functions
whose text changed are verified again.

What the key cannot see: the compiler's version, and the order functions run in
within a process (a function needing more fresh pages than
emu.Machine.FAULT_RETRIES can pass after others mapped them). A run with --no-cache recomputes everything
and is the check on both.
"""

from __future__ import annotations

import glob
import hashlib
import json
import os

# Modules that only produce the text being verified: the text is in every key, so
# a change to them invalidates exactly the verdicts whose text changes.
TEXT_ONLY = {"structure.py", "liveness.py", "naming.py", "signatures.py"}


def _files(repository: str) -> list:
    engine = os.path.join(repository, "engine")
    files = [path for path in glob.glob(os.path.join(engine, "ps2decomp", "*.py"))
             if os.path.basename(path) not in TEXT_ONLY]
    # derive.py plans the test cases; tools/lift.py only orchestrates - what it
    # verifies with is liftverify.py, among the modules above.
    files.append(os.path.join(engine, "tools", "derive.py"))
    for folder, _dirs, names in os.walk(os.path.join(engine, "runtime")):
        files += [os.path.join(folder, name) for name in names]
    return sorted(files)


def fingerprint(repository: str, inputs=()) -> str:
    """The environment's fingerprint: the source files verification depends on,
    plus `inputs` - bytes or anything with a stable repr (image, analysis, seeds)."""
    digest = hashlib.sha256()
    for path in _files(repository):
        digest.update(os.path.relpath(path, repository).replace(os.sep, "/").encode())
        with open(path, "rb") as handle:
            # Line endings differ between checkouts; the code does not.
            digest.update(handle.read().replace(b"\r\n", b"\n"))
    for item in inputs:
        digest.update(item if isinstance(item, bytes) else repr(item).encode())
    return digest.hexdigest()


# Verdicts of this many environments are kept: the current one, and a few before
# it - a full run and a subset run seed test memory differently, so they are
# different environments, and switching between them should not lose either.
KEEP_ENVIRONMENTS = 4


class Verdicts:
    """{key: outcome} on disk, per environment; a key covers form and text."""

    def __init__(self, path: str, environment: str, use: bool = True) -> None:
        self.path = path
        self.environment = environment
        self.use = use
        self.hits = self.misses = 0
        self.stored = []                 # [[environment, {key: outcome}]], newest last
        if os.path.exists(path):
            try:
                with open(path, encoding="utf-8") as handle:
                    self.stored = [list(pair) for pair in json.load(handle)]
            except (OSError, ValueError, TypeError):
                self.stored = []
        current = [pair for pair in self.stored if pair[0] == environment]
        self.stored = [pair for pair in self.stored if pair[0] != environment]
        self.entries = current[0][1] if current else {}

    def key(self, text: str, abi: bool) -> str:
        digest = hashlib.sha256(self.environment.encode())
        digest.update(b"abi" if abi else b"whole")
        digest.update(text.encode("utf-8"))
        return digest.hexdigest()

    def get(self, text: str, abi: bool):
        """The remembered outcome, or None (also when not using remembered ones)."""
        outcome = self.entries.get(self.key(text, abi)) if self.use else None
        if outcome is None:
            self.misses += 1
        else:
            self.hits += 1
        return outcome

    def put(self, text: str, abi: bool, outcome: str) -> None:
        self.entries[self.key(text, abi)] = outcome

    def save(self) -> None:
        os.makedirs(os.path.dirname(self.path), exist_ok=True)
        kept = self.stored[-(KEEP_ENVIRONMENTS - 1):] + [[self.environment, self.entries]]
        temporary = self.path + ".tmp"
        with open(temporary, "w", encoding="utf-8") as handle:
            json.dump(kept, handle)
        os.replace(temporary, self.path)
