# USA -> PAL executable address mapping

This reference records the address audit completed on 19 October 2026. It helps
locate candidates for regional adaptation; it does not authorize applying an
offset to an arbitrary function or data address. No hooks or binaries were
changed by this documentation task.

## Compared inputs

Both inputs are original PS2 ELF32 little-endian MIPS executables, identified by
the following serials and SHA-256 digests. No more specific mastering revision
has been established. These identities do not refer to Windows player builds or
engine revisions.

| Region | Executable | PCSX2 serial | ELF SHA-256 |
|---|---|---|---|
| USA | SLUS_210.50 | SLUS-21050 | `e118dba952bf9fde6a04ee284bb1f06fa0bb49c9e04bbe762af1039dcc443c66` |
| PAL | SLES_525.84 | SLES-52584 | `913a3bcbcd735a435aac676ef1055737a86f9bbac26611344788c5e5ef953c6f` |

The USA function index contains 9,277 file-backed executable entries. The PAL
analysis database contains 9,384 entries; this is distinct from the 9,383
translated PAL functions. A complete USA size database was unavailable, so its
next indexed entry supplies an upper bound, not an independently certified end.
Users must supply their own compatible game files. No game bytes, extracted
instructions, generated translations or private input paths accompany this page.

## Evidence classes

- **Strict static certificate:** a reciprocal unique full instruction signature,
  closed bounded control flow and resolved ordered call relationships under the
  normalization below. This does not prove identical data layouts or runtime
  effects in arbitrary states.
- **Documented identity or contract:** targeted binary/ABI inspection and the
  previously recorded tests or gameplay evidence. These mappings retain their
  individual scope; they are not full-function static certificates.
- **Candidate:** incomplete, ambiguous or structural evidence. A shared delta,
  size, prologue or partial fingerprint alone cannot certify identity.
- **Dynamic equivalence:** requires paired executions with equivalent initial
  state and comparisons of the relevant registers, memory and side effects.
  Neither static matching nor synthetic hook tests establish this by themselves.

### Reproduction method

1. Verify the ELF digests and use virtual addresses from file-backed executable
   sections. Preserve the original indexes and record input/tool versions.
2. Bound USA searches by the next indexed entry and a maximum 64 KiB span;
   use the PAL recorded end. Traverse reachable branches and delay slots.
   Treat calls as calls, returns as returns, and reject unresolved computed
   control-flow edges or open boundaries.
3. Eligible bodies are 32-32,768 bytes, have a return or calls, and have no PAL
   chunks. Retain instruction positions and holes inside the reachable envelope.
4. Canonicalize only direct external J/JAL destination operands. Express internal
   J destinations relative to the entry. Retain branch displacements, registers,
   stack/member offsets, constants, LUI operands, load/store and GP immediates.
   Broad immediate masking is useful only for candidate discovery.
5. Require reciprocal uniqueness and one-to-one assignment of full signatures.
   Require ordered callee edges at corresponding instruction offsets to resolve
   to independently established pairs (including mapped self-calls). Do not
   silently accept unknown indirect targets.
6. Bootstrap from unique bodies without calls, then propagate resolved calls
   (the audit allowed up to 20 passes). A second pass uses independently mapped
   parent callsites to disambiguate, still requiring resolved callee graphs
   (up to 10 passes). Neither stage is seeded by a guessed regional offset.
7. Record failures and multiple candidates separately. Independently review
   function boundaries, data references and contracts before adopting a hook.

These steps specify the audit predicate, not a shipped one-command analyzer.
The private audit scripts and generated bulk reports are not repository inputs.
A reproduction must retain its own input indexes, tool revisions, candidate
sets and certificates; differences in boundary discovery can change the counts.

## Accounting of the snapshot

| Quantity | Definition | Count |
|---|---|---:|
| Eligible USA / PAL bodies | Closed bounded bodies under the above predicate | 7,082 / 6,989 |
| Strict static pairs | Unique pairs passing signature and resolved-call checks | 1,233 |
| Distinct strict deltas | Unique values of PAL entry minus USA entry | 20 |
| Contiguous groups | Same-delta runs defined below, with at least two functions | 249 |
| Sparse anchor runs | Same-delta runs in the sorted verified-anchor list | 24 |
| Transition intervals | Adjacent sparse runs with different deltas | 23 |
| Previously documented function pairs | Reviewed separately from automatic certificates | 51 |
| Prior identities/contracts retained | Table below | 44 |
| Prior pairs still pending | Two ambiguous builders, four callers, one historic candidate | 7 |
| Combined evidence-labelled pairs | 1,233 strict + 44 prior; no overlapping pairs | 1,277 |

None of the 51 prior pairs passes the narrower automatic full-signature and
fully resolved graph predicate in this snapshot. This does not invalidate a
previously verified hook contract. Conversely, functional gameplay acceptance
does not certify the entire body or call graph of its containing function.

The pending report has 2,261 candidate rows involving 1,752 unique USA entries:
1,559 unique-signature rows with unresolved graphs, 695 ambiguous candidate rows,
and the seven prior pairs. Rows are not counts of certified functions.

### Delta histogram

All values are PAL minus USA; counts cover only the 1,233 strict pairs.

| Delta | Bytes | Pairs |
|---|---:|---:|
| -0x10 | -16 | 293 |
| +0x0 | 0 | 180 |
| +0x10 | 16 | 46 |
| +0x50 | 80 | 6 |
| +0x60 | 96 | 71 |
| +0x70 | 112 | 241 |
| +0xA0 | 160 | 5 |
| +0x190 | 400 | 8 |
| +0x1D0 | 464 | 13 |
| +0x1E0 | 480 | 11 |
| +0x1F0 | 496 | 36 |
| +0x260 | 608 | 12 |
| +0x350 | 848 | 18 |
| +0x360 | 864 | 96 |
| +0x410 | 1040 | 145 |
| +0x460 | 1120 | 2 |
| +0x480 | 1152 | 2 |
| +0x4A0 | 1184 | 12 |
| +0x4B0 | 1200 | 5 |
| +0x4D0 | 1232 | 31 |

### Groups and transitions

A contiguous group requires consecutive entries in **both original indexes**,
identical delta, matched bounded bodies and equal intervening word gaps smaller
than 64 words (256 bytes). Its half-open extent ends at the last matched body's
end. It is not permission to translate every interior address by that delta.

Examples of confirmed groups (USA half-open intervals):

| USA interval | Delta | Functions |
|---|---|---:|
| [0x001053A8, 0x00106180) | 0 | 19 |
| [0x001EB258, 0x001EDB58) | -0x10 | 15 |
| [0x0022CBE0, 0x0022D09C) | -0x10 | 10 |
| [0x00298170, 0x0029841C) | +0x70 | 11 |
| [0x0035CA00, 0x0035ED9C) | +0x1F0 | 9 |
| [0x00468CA0, 0x00469300) | +0x4D0 | 13 |

Sparse runs ignore gaps between verified anchors. For example, 293 anchors from
USA 0x0019E9E0 through 0x00257080 share -0x10, but include a maximum gap of
34,312 bytes. This is not a proven continuous block. The 23 transitions bracket
changes between the 24 sparse runs; they do not identify exact insertion PCs.
Examples include 0x0019D440 -> 0x0019E9E0 (0 -> -0x10),
0x003642C0 -> 0x00364600 (+0x1F0 -> +0x1E0), and
0x00386CF0 -> 0x00386EE0 (+0x350 -> +0x360).

Insertions, deletions and alignment changes are plausible explanations for these
piecewise, non-monotonic shifts. Without source/link maps, their individual
causes remain hypotheses. They are not evidence of one global relocation.

## Previously documented identities and contracts

The following **44** pairs are published as address/size/evidence metadata.
Sizes are bounded inspected envelopes in bytes, not indisputable source-level
function extents. Evidence codes preserve the scope of previous validation:

- **D:** documentary role plus targeted static review; full equivalence pending.
- **K:** host contract and synthetic routing; privileged CP0 equivalence excluded.
- **M:** movie offsets/instructions and eight synthetic cases; Start skip manually confirmed.
- **C:** projection instruction audit, 14 camera cases with notification spy and manual Hor+ acceptance.
- **H:** static HUD map, 27 synthetic 2D cases and manual HUD acceptance; not proof of every caller body.
- **L:** libmc2 ABI/delay-slot checks and host routing; profile save/load manually confirmed.
- **R:** RPC argument/version contract, synthetic tests and startup evidence.

These are existing results, not tests rerun for this document. See
[adaptation history](../../games/burnout3-pal/port/ADAPTATIONS.md),
[camera implementation](WIDESCREEN_3D.md) and [HUD implementation](WIDESCREEN_HUD.md).

| USA | PAL | Delta | USA/PAL bytes | Role | Evidence |
|---|---|---|---|---|---|
| `0x00102EA0` | `0x00102EA0` | +0x0 | 788/788 | sceGsSyncPath diagnostic name | D |
| `0x001156C0` | `0x001156C0` | +0x0 | 104/104 | KernelStore host contract | K |
| `0x001313C0` | `0x00131410` | +0x50 | 184/184 | SkipMovie screen update | M |
| `0x001A0790` | `0x001A0780` | -0x10 | 104/104 | HUD anchor identity / context | H |
| `0x001A5740` | `0x001A5730` | -0x10 | 2236/2236 | HUD anchor identity / context | H |
| `0x001A7B00` | `0x001A7AF0` | -0x10 | 1656/1656 | HUD anchor identity / context | H |
| `0x001A8180` | `0x001A8170` | -0x10 | 780/780 | HUD anchor identity / context | H |
| `0x001D5FB0` | `0x001D5FA0` | -0x10 | 180/180 | HUD anchor identity / context | H |
| `0x001D75C0` | `0x001D75B0` | -0x10 | 804/804 | HUD anchor identity / context | H |
| `0x001E2110` | `0x001E2100` | -0x10 | 100/100 | RwCameraSetViewWindow | C |
| `0x001E3FD0` | `0x001E3FC0` | -0x10 | 92/92 | frame notification | C |
| `0x00211CC0` | `0x00211CB0` | -0x10 | 136/136 | camera FOV | C |
| `0x00211DD0` | `0x00211DC0` | -0x10 | 248/248 | camera wrapper initializer | C |
| `0x002178E0` | `0x002178D0` | -0x10 | 308/308 | 2D packet builder | H |
| `0x00217A20` | `0x00217A10` | -0x10 | 536/536 | 2D packet builder | H |
| `0x00217C40` | `0x00217C30` | -0x10 | 252/252 | 2D packet builder | H |
| `0x00217D40` | `0x00217D30` | -0x10 | 300/300 | 2D packet builder | H |
| `0x00217E70` | `0x00217E60` | -0x10 | 328/328 | 2D packet builder | H |
| `0x00217FC0` | `0x00217FB0` | -0x10 | 1084/1084 | 2D packet builder | H |
| `0x00218400` | `0x002183F0` | -0x10 | 932/932 | 2D packet builder | H |
| `0x002187B0` | `0x002187A0` | -0x10 | 1356/1356 | 2D packet builder | H |
| `0x00218D00` | `0x00218CF0` | -0x10 | 1104/1104 | 2D packet builder | H |
| `0x00219150` | `0x00219140` | -0x10 | 992/992 | 2D packet builder | H |
| `0x00219530` | `0x00219520` | -0x10 | 764/764 | 2D packet builder | H |
| `0x00219830` | `0x00219820` | -0x10 | 496/496 | 2D packet builder | H |
| `0x00219A20` | `0x00219A10` | -0x10 | 616/616 | 2D packet builder | H |
| `0x00219CA0` | `0x00219C90` | -0x10 | 168/168 | 2D packet builder | H |
| `0x0021A4F0` | `0x0021A4E0` | -0x10 | 248/248 | 2D flush | H |
| `0x00242A60` | `0x00242A50` | -0x10 | 768/768 | libmc2 worker | L |
| `0x00244218` | `0x00244208` | -0x10 | 948/948 | libmc2 GetInfo | L |
| `0x002445D0` | `0x002445C0` | -0x10 | 2392/2392 | libmc2 Format | L |
| `0x00245398` | `0x00245388` | -0x10 | 1092/1092 | libmc2 Lookup | L |
| `0x00245FC0` | `0x00245FB0` | -0x10 | 1480/1480 | libmc2 Create/Mkdir | L |
| `0x00246588` | `0x00246578` | -0x10 | 512/512 | libmc2 ReadFile | L |
| `0x00246788` | `0x00246778` | -0x10 | 1600/1600 | libmc2 WriteFile | L |
| `0x002470D8` | `0x002470C8` | -0x10 | 1188/1188 | libmc2 GetDir | L |
| `0x0030D6E0` | `0x0030D740` | +0x60 | 224/224 | HUD anchor identity / context | H |
| `0x0031B130` | `0x0031B1D0` | +0xA0 | 1284/1284 | HUD anchor identity / context | H |
| `0x00364920` | `0x00364B00` | +0x1E0 | 580/580 | Frontend / world-label sprites | H |
| `0x00381030` | `0x00381380` | +0x350 | 1712/1712 | HUD anchor identity / context | H |
| `0x00381AB0` | `0x00381E00` | +0x350 | 1860/1860 | HUD anchor identity / context | H |
| `0x003CD920` | `0x003CDC80` | +0x360 | 484/484 | HUD anchor identity / context | H |
| `0x003D7B40` | `0x003D7EA0` | +0x360 | 876/876 | HUD anchor identity / context | H |
| `0x004438E0` | `0x00443D80` | +0x4A0 | 908/908 | lgDevInit RPC contract | R |

### Key code and data distinctions

Camera, FOV and Flush2D have -0x10 entry deltas. Their raw word matches are
24/25 (100 bytes), 33/34 (136 bytes) and 61/62 (248 bytes), respectively;
the differing word is a direct call destination. Sprites have +0x1E0:
0x00364920 -> 0x00364B00, 144/145 raw words over 580 bytes. A full body and
its references provide stronger evidence than a matching prologue.

The 16 documented HUD builder assignments all have -0x10. Fourteen appear in
the retained table; the other two remain ambiguous below. Shared displacement
does not resolve their identities.

The libmc2 **command table**, a data object, moves from 0x004BB320 to
0x004BB880: **+0x560 (1,376 bytes)**, with a 396-byte table envelope.
Its 99 entries for commands 2-100 point into the worker; all 99 corresponding
destinations shift **-0x10**. The worker envelope is 768 bytes, PAL
[0x00242A50, 0x00242D50); an original 120-byte indexed prefix plus an end
chunk must not be mistaken for the whole worker. Computed dispatch requires
its table-aware boundary review, beyond the simple closed-CFG predicate.

ELF `.reginfo` gives GP 0x004E8670 -> 0x004E8BF0:
**+0x580 (1,408 bytes)**. GP is a register initialization value, not a
function entry. The 0x20 difference between these two data shifts can affect
GP-relative immediates; it does not establish a -0x20 code region.

## Unresolved mappings - excluded from the retained 44

| USA | Proposed PAL | Bytes USA/PAL | Evidence still missing |
|---|---|---|---|
| 0x00219D50 | 0x00219D40 | 464/464 | Individual builder identity |
| 0x00219F20 | 0x00219F10 | 464/464 | Individual builder identity |
| 0x001A6290 | 0x001A6280 | 6256/6256 | Strict full-body/callee certificate |
| 0x00352AB0 | 0x00352C80 | 1176/1176 | Strict full-body/callee certificate |
| 0x003B2EE0 | 0x003B3240 | 9532/9532 | Strict full-body/callee certificate |
| 0x003D6C30 | 0x003D6F90 | 932/932 | Strict full-body/callee certificate |
| 0x0010D3E8 | 0x0010D3E8 | 372/372 | Historic TtyWrite candidate, unconfirmed |

The two USA builders have identical bodies to each other; the two PAL builders
also have identical bodies to each other. Both cross assignments remain
possible. Ordering and -0x10 cannot select one. Existing wrappers invoke their
respective original PAL symbols; this ambiguity alone does not demonstrate a
runtime defect.

The four callers have structural evidence (entry signatures, aligned larger
bodies and ordered known 2D calls), with register-allocation/scheduling differences.
Their contextual use and manual acceptance do not certify every instruction.
A broad immediate-masked candidate fingerprint is not a strict certificate.

No -0x20 displacement was found among the 1,233 strict pairs or 51 documented
function pairs. This is an absence in this dataset, not a claim about every
function or every retail executable revision.

## HUD constants and hook audit

The audit checked **24** anchor tuples (context/object/cell/anchor) against the
explicit adopted map and **18** HUD hook registration entries. No original USA
identity keys or hook-entry addresses remained in those respective sets.
Four structurally identified caller keys still have the limitations above.
This check does not assert that the entire repository contains no USA constants.

Identity addresses are distinct from screen coordinates, packet strides, object
field offsets and GP-relative operands. Do not apply regional deltas to these
other values. Existing camera and HUD corrections remain unchanged; see the
linked implementation documents for transform behavior and validation scope.

## Proposed lookup policy - not implemented

A future USA -> PAL resolver should consult explicit, evidence-labelled pairs first,
including executable digests, entry/envelope, validation method and unresolved
limitations. It must distinguish code entries, interior entries and data objects.
Ambiguous entries must return all candidates or an unresolved result.

High-confidence groups may narrow a search window and suggest a candidate delta.
They must never automatically authorize an unknown address, constant or hook.
Crossing a gap, transition, section or executable identity invalidates any
range-based assumption. A lookup result should cite the supporting certificate
and require human acceptance before changing code. No automatic resolver is
provided by this documentation change.

## Mandatory verification before porting a hook

1. Confirm executable identities and the USA behavior being reused.
2. Locate the full PAL body and callers independently; inspect branches, delay
   slots, internal entries and jump tables. Resolve multiple candidates.
3. Compare full instructions and ordered calls, retaining non-address operands;
   justify every normalized address and inspect referenced data separately.
4. Verify ABI: registers, signedness, return value, object fields, GP references,
   side effects, ownership, asynchronous/polling behavior and original-call path.
5. Record the evidence class and limitations. An unresolved identity must not
   become an installed hook merely because an offset predicts its address.
6. Add focused independent regression cases and confirm routing without recursion.
   Compare original execution where feasible; clearly label synthetic-only tests.
7. Use an isolated build and data directory, preserve recovery artifacts, and
   validate relevant PAL gameplay paths before promoting the adaptation.

## Publication scope

Only reviewed address pairs, bounded sizes, aggregate statistics, input digests
and methodology are included here. Bulk generated certificates/candidate reports,
private audit paths, instruction bodies, executables, game resources and personal
data remain local. The histogram is represented as a small aggregate table;
the private generated SVG and CSV files are not copied into Git.
