"""The Windows release: the built port, its player README and the licences
of what it is built with, in one zip - checked to carry nothing of the game's
but its translated program, and nothing of the machine it was built on.

    python games/burnout3/port/tools/package.py

Build first (engine/tools/port.py --game games/burnout3 build). Written to
build/release/Burnout3-PC-<version>-windows-x64.zip (build/ is never
committed). Refuses:
  - a build carrying the disc-made logo icon (build/port/icon.ico: EA's
    artwork, for a player's own build only) - delete it and build again;
  - an executable that needs the Visual C++ runtime installed (it must be
    linked in), or that names a folder of the machine it was built on.
"""

import json
import os
import re
import subprocess
import sys
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
GAME = os.path.normpath(os.path.join(HERE, "..", ".."))
ROOT = os.path.normpath(os.path.join(GAME, "..", ".."))
BUILD = os.path.join(GAME, "build")
EXE = os.path.join(BUILD, "port", "cmake", "burnout3.exe")
DEPS = os.path.join(BUILD, "port", "cmake", "_deps")
sys.path.insert(0, os.path.join(ROOT, "engine"))

# The libraries built into the port, and where their licence is in their source.
LICENCES = [
    ("SDL 3", "sdl3-src", ["LICENSE.txt"]),
    ("volk", "volk-src", ["LICENSE.md"]),
    ("glslang", "glslang-src", ["LICENSE.txt"]),
    ("Vulkan-Headers", "vulkan_headers-src", ["LICENSE.md", "LICENSE.txt"]),
]


def fail(why):
    raise SystemExit("package: " + why)


def imports(exe):
    """The DLLs the executable imports (dumpbin, with the compiler's environment)."""
    from ps2decomp import cppcheck
    script = os.path.join(BUILD, "release_dumpbin.bat")
    with open(script, "w") as f:
        f.write('@echo off\ncall "%s" >nul\ndumpbin /nologo /dependents "%s"\n' % (cppcheck.find_vcvars(), exe))
    out = subprocess.run(["cmd", "/c", script], capture_output=True, text=True).stdout
    return sorted(set(m.lower() for m in re.findall(r"^\s+(\S+\.dll)\s*$", out, re.M)))


def main():
    with open(os.path.join(GAME, "game.json"), encoding="utf-8") as f:
        version = json.load(f).get("version", "0.0.0")
    if not os.path.exists(EXE):
        fail("no build at %s: build first" % EXE)
    if os.path.exists(os.path.join(BUILD, "port", "icon.ico")):
        fail("build/port/icon.ico is the disc-made logo (EA's artwork): delete it, build again")
    dlls = imports(EXE)
    runtime = [d for d in dlls if d.startswith(("msvcp", "vcruntime", "ucrtbase", "api-ms-win-crt"))]
    if runtime:
        fail("the executable needs the Visual C++ runtime installed (%s): it must be linked in" % ", ".join(runtime))
    data = open(EXE, "rb").read()
    for folder in {os.path.expanduser("~"), ROOT}:
        for form in (folder, folder.replace("\\", "/")):
            if form.encode() in data or form.lower().encode() in data:
                fail("the executable names a folder of this machine: %s" % form)
    if re.search(rb"[A-Za-z]:\\Users\\[^\\\x00]{1,64}\\", data):
        fail("the executable names a user's folder")
    notices = ["Third-party software in the Burnout 3: Takedown PC port", "=" * 56, ""]
    for name, folder, files in LICENCES:
        path = next((os.path.join(DEPS, folder, f) for f in files if os.path.exists(os.path.join(DEPS, folder, f))), None)
        if not path:
            fail("no licence for %s in %s" % (name, os.path.join(DEPS, folder)))
        with open(path, encoding="utf-8", errors="replace") as f:
            notices += ["-" * 72, name, "-" * 72, f.read().strip(), ""]
    with open(os.path.join(GAME, "port", "release", "README.txt"), encoding="utf-8") as f:
        readme = f.read().replace("{version}", version)
    name = "Burnout3-PC-%s" % version
    out_dir = os.path.join(BUILD, "release")
    os.makedirs(out_dir, exist_ok=True)
    out = os.path.join(out_dir, "%s-windows-x64.zip" % name)
    with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        z.write(EXE, "%s/burnout3.exe" % name)
        z.writestr("%s/README.txt" % name, readme.replace("\n", "\r\n"))
        z.writestr("%s/THIRD-PARTY-NOTICES.txt" % name, "\r\n".join(notices))
    print("package: %s (%.1f MB; imports %s)" % (out, os.path.getsize(out) / 1e6, ", ".join(dlls)))


if __name__ == "__main__":
    main()
