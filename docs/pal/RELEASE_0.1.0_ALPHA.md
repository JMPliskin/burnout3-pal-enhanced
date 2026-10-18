# v0.1.0-alpha

Functional acceptance is complete, as confirmed by the maintainer: the rebuilt
player supports racing with GPU Thread On; an isolated installation starts with
no old profiles, creates and saves a new profile, remembers its ISO and restores
that profile after restart. No errors were observed. This is not a claim of full
MIPS equivalence or compatibility with every game mode and hardware configuration.

## Windows Pre-release draft

**Burnout 3: Takedown — PAL Enhanced 0.1.0 Alpha**

An unofficial Windows adaptation of the European PS2 version, SLES_525.84,
including Spanish and the original 50/60 Hz selection screens.

When binary publication is cleared, download the Windows x64 ZIP, extract it and
open Burnout3-PAL-Enhanced.exe. Select an ISO from your own compatible PAL disc.
The ISO path is remembered; moved or renamed images prompt for selection again.
Windows 10 1903+ x64, an AVX2 CPU and Vulkan-capable graphics are required.
No Python or BAT is needed to play. Settings and saves persist under
%LOCALAPPDATA%/Burnout3-PAL-Enhanced/; back up memcard/ before importing saves.

Features: original regional startup, persistent profiles, controller and keyboard
input, Vulkan rendering, adjustable internal resolution, 16:9, compiled PAL VU
programs, audio and Start-to-skip non-looping FMV.

Known limitations: early alpha, variable 3D performance, incomplete mode/hardware
testing, retained diagnostic instrumentation and an unexplained freeze in an older
GPU Thread build. Extended PC SETTINGS, additional USA graphics options, mods
and LAN are not validated for PAL.

Developed and maintained by JMPliskin with programming, reverse-engineering,
testing and debugging assistance from OpenAI ChatGPT and Codex. Based on Daniel
Gomes Vieira's original Burnout 3 PC port and ps2-port-engine, whose development
also uses AI-assisted tools. PAL decisions and gameplay validation are directed
and reviewed by the maintainer. Thanks to upstream contributors and third-party
projects; retain all bundled license notices.

Unofficial; not affiliated with Electronic Arts or Criterion Games. The EXE
contains translated game code. Port/runtime licenses do not grant rights to that
original code. No authorization from the original rights holders is claimed;
requiring your ISO does not itself establish redistribution permission.

Keep the Windows Release as a **draft Pre-release**, not Latest Stable, until
authorization to distribute the original game code in the EXE is established.
The public source and tag do not constitute approval to publish that binary.

## Source and artifact identity

- Source base: a076ff78b9d92ddeab595e88a247c586440b88a0.
- Final reviewed source commit: resolve the annotated tag v0.1.0-alpha using
  `git rev-parse v0.1.0-alpha^{commit}`. The tag must point to the reviewed final
  commit, not merely the base above.
- Engine base: d7e6e023f261d9f95ad097fcd063ec95e40ca465, plus the public diagnostic
  patch documented in the reconstruction guide. No dirty engine gitlink included.
- EXE SHA-256:
  `7e9661259050d10cb3c0b1c9b9cf9cb2190690917c5b7307fa5e103bdd96cb49`.
- Windows ZIP: Burnout3-PAL-Enhanced-0.1.0-alpha-windows-x64.zip, 12,281,806 bytes.
- ZIP SHA-256:
  `517263af78f5fbbb34c63c9fb41fe87e718542063624f862ca438d56b9926e3f`.

The ZIP is preserved byte-for-byte as requested: four files (EXE, README.txt,
LICENSE, THIRD-PARTY-NOTICES). No ISO, ELF, extracted resources, personal settings,
profiles, logs, PDB or generated source files are bundled. Its README.txt predates
the final manual confirmation and still describes that acceptance as pending.
This document and the Release draft supersede that historical validation wording.
The prepared ZIP is not a publicly approved download while distribution remains
unaccredited.

## Reconstruction and validation

Fresh Release reconstruction passed ten adapter tests, eleven synthetic startup
tests and all host/renderer tests. It rebuilt 9383 preserved EE functions and 97
VU programs from verified original inputs, with no old objects/libraries reused.
The bounded ISO startup reached 2200 VBLANK / 1898 functions with clean process
containment. A later manual log recorded normal exit, 3798 functions and
7,281,221,644 compiled VU pairs out of 7,281,550,960 (99.99548%). Maintainer manual
acceptance now confirms gameplay and isolated new-profile save/reload as above.
The historical GPU Thread freeze is still unexplained; instrumentation remains
compiled in even with PS2HOST_GPU_TRACE absent.

Reconstruction instructions: [diagnostic engine and clean build](../../games/burnout3-pal/port/diagnostics/README.md).
Preserved private generation artifacts and dependency source caches are required;
a fresh clone alone is not a turnkey build. Original game inputs and generated
code remain excluded from Git.

## Distribution review


The repository and engine carry MIT licenses for their original software. Those
notices do not document permission from the owners of Burnout 3 to distribute the
translated EE/VU program compiled into a player executable. Requiring an ISO at
runtime does not establish that missing permission. No such permission or other
reviewed distribution basis has been found in the examined project materials.
The upstream publication precedent does not establish that basis; this is not a
legal conclusion about every jurisdiction or an assertion that MIT is invalid.

General background: [US Copyright Office, computer programs](https://www.copyright.gov/register/tx-programs.html)
and [copyright in derivative works](https://www.copyright.gov/circs/circ14.pdf).
These sources do not decide the project's particular distribution rights.

| Component | Local license material | Packaging requirement |
| --- | --- | --- |
| PC port / engine | MIT | Retain copyright and permission notices; engine patch remains covered by upstream notices |
| SDL3 | zlib-style license | Retain license; accurately describe any alterations |
| volk | MIT | Retain copyright and license |
| Vulkan-Headers | Apache-2.0 / MIT per file; exceptions documented upstream | Select and document applicable license; include full required texts and notices, not only the top-level summary/links |
| glslang | Multiple licenses in LICENSE.txt | Retain the complete applicable notices; compiler tool and linked runtime are distinct |
| xxHash | BSD-2-Clause material in header | Retain notice and disclaimer |
| bcdec / stb_image | Dual-license header material | Retain or document the selected license |
| SMAA | Permissive notice in engine third_party | Retain full notice |
| Original game EE/VU | No redistribution grant established here | Separate rights review required before binary distribution |

The packaging helper's dependency/import audit is useful but is not a legal
clearance. Check its inventory against the final linked binary. The source-only
alternative must also exclude game-derived files, saves, private configuration
and reports, while preserving upstream attribution and the engine gitlink.

The packaging helper now includes Vulkan-Headers' LICENSE.md overview and the
full LICENSES/Apache-2.0.txt and LICENSES/MIT.txt alternatives, failing if either
full text is missing. Other dependency notices remain preserved. This resolves
the identified missing-text packaging issue, not the separate game-code rights
question. The source-only notes remain an alternative, not an authorization to
publish the Windows ZIP before the maintainer's validation conditions pass.


## Source-only alternative

The reviewed source, tests and documentation can be released under their
applicable licenses without attaching the game-derived Windows binary. A
source-only release must clearly say it is not a playable download and requires
the user's own PAL inputs and the documented generation/reconstruction workflow.
GitHub source archives do not include the engine submodule contents; initialize
the pinned submodule and apply the documented source patch in an isolated build.
