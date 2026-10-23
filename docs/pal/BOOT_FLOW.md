# PAL boot flow: established contracts and remaining work

Updated 23 October 2026. Labels follow [development continuity](../../PAL_DEVELOPMENT.md).
This separates recovered boot blockers from proposed startup preferences.

## Established flow

| Stage | Evidence and contract |
| --- | --- |
| Original program | SOURCE: SLES-52584 1.01, ELF `SLES_525.84`, entry `0x00100008`; preserve original game logic |
| Runtime setup | SOURCE/AUTO: copied-code discovery, PAL relocation compatibility, kernel/devices and original PAL IOP modules |
| Resources | OBSERVED: GTFSCDVD RPC and original resource fixups progressed; do not correct resource pointers by guessing |
| Language | OBSERVED + MANUAL: original selection can activate Spanish, effective language 5 and GlobalSp.bin/HeadSp.bin in the recorded case |
| Memory card | SOURCE/AUTO + MANUAL: recovered worker switch and seven PAL CardOr wrappers; profile creation/save/reload subsequently accepted |
| PAL video | MANUAL: original 50/60 Hz screen remains; 60 Hz test requires explicit Yes confirmation, default No returns to 50 Hz |
| Frontend/game | MANUAL: menus, FMV, original input, profiles and races work within tested scope |
| Movie skipping | SOURCE/AUTO + MANUAL: PAL `0x00131410`, Start edge requests original stop; no fabricated completion |

### 0x00242AC8 is recovered, not the next unresolved boot task

The earlier JSON map covered entry `0x00242A50..0x00242AC8` and a separated chunk
`0x00242D40..0x00242D50`. The original worker's indirect jump legitimately reached
`0x00242AC8`. Completing `0x00242A50:0x00242D50` recovered the omitted switch cases.
It was not a separate function, a demonstrated bad pointer, or the language/video
initializer. Keep the original worker/queue/polling and table `0x004BB880`.
This adaptation remains NOT VERIFIED for full MIPS/native equivalence; recovering
execution and scoped synthetic/gameplay acceptance do not change that label.

PAL CardOr targets are documented in [architecture/evidence](ARCHITECTURE_AND_EVIDENCE.md).
Without a supplied virtual card they still call the original PAL functions;
with one they use host mc2. Do not bypass the worker or force success to skip a dialog.

### Historical selection observations, not implementation APIs

The recorded language screen used cursor `0x01E89EB0`, index 2 for Spanish and
language value 5 at `0x004E2E98`. In a later video screen the cursor value had
different meaning. Frame-input sequences from those runs depend on startup timing.
Do not reuse those frames/addresses as a production preference mechanism.

### Established language entry points

Reusing the prior SOURCE inspection: language initialization is `0x00437740`,
selection processing `0x004374C0`. The original guest `language=3` path selects
Spanish GlobalSp/HeadSp resources and internal ID 5; argument 3 and internal 5
are not interchangeable. En/Du/Sp/Sw resource branches cover English, Dutch,
Spanish and Swedish in the inspected SLES-52584 executable. Other PAL editions
and complete runtime acceptance for all languages are not established here.

The original startup parser recognizes `language=`; passing it through the host
and its effect on a visible selector remain unvalidated. It is not an established
PC CLI option. Preserve original loading, callbacks and state lifetime; do not
replace them with direct RAM writes or assume direct initialization shows a selector.

## Planned PAL-070: preferences and optional skipping

Not implemented: preferred Spanish, preferred 60 Hz, all-language preference
selection, optional selector skipping, or corresponding F9 controls.

First milestone: persistent preferred language, with Spanish preferred and the
original selection behavior available. Reuse the known language entry points;
resolve the remaining host/selector/lifetime contracts rather than rediscovering
them. Design defaults < INI < explicit CLI and shared F9 settings, with honest
restart/effective-state semantics where needed. Preferred 60 Hz, video confirmation
and optional startup-screen skipping are later milestones. Preserve:

- All original PAL languages and both original refresh modes.
- A user option to show the original startup selection screens.
- Video-test confirmation and safe fallback, not arbitrary VBLANK-clock changes.
- Existing Spanish profiles/cards and unforced configuration.
- Clean startup/new profile without depending on an unrelated memory-card save.
- The full-height 640x512 selector picture fallback and original button instructions.

Native-menu integration is a later independent roadmap section; existing F9 is
a host overlay. Adding a preference is not permission to port the USA SLUS menu.

## Historical evidence and limits

The [accepted chronology](https://github.com/JMPliskin/burnout3-pal-enhanced/blob/2aac9350af0a139771d8a5677d0863b216343efc/games/burnout3-pal/port/ADAPTATIONS.md)
contains the Spanish switch/card/video reports and their exact isolated paths.
Early software-backend slowness and disappearing text in captures did not by
themselves prove a boot deadlock. Later manual acceptance supersedes their old
"next action" statements without deleting the original observations.
All-language and exhaustive PAL50 feature validation remain incomplete.
