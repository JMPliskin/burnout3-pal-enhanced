# HD textures: PAL research status

This is a research summary, not an HD feature or pack release. The public
v0.1.0-alpha and the clean widescreen source must not be confused with private
AA/HD builds. No third-party DDS/PNG, generated catalog, converter or HD engine
patch is included in this integration.

## Existing engine and separate PAL experiments

The pinned engine already contains the PNG/DDS replacement loader in
`engine/host/src/texture_pack.cpp`, native lookup/association in `gpu_bridge.cpp`,
F9 texture-pack control when entries are indexed, and the Tab shortcut. The
shared setting is `texture_pack` in pc_settings.ini. Count/index success does
not establish a visible replacement.

A separate PAL experiment added executable-relative `texture pack` discovery,
explicit environment/INI precedence and bounded audit/catalog diagnostics.
Its F9/Tab controls and persistence were manually tested. Those changes are
**not integrated here**. Do not assume the clean player has that directory
precedence, the experimental label, catalog variable or applied-name counters.
The existing loader checks PS2HOST_TEXTURE_PACK, then texture_pack_dir in the
INI, then a texture pack folder beside that INI. Normal PAL native startup keeps
the INI under LocalAppData. This is settings-relative, not executable-relative.
Check the effective path before a trial; do not copy private configuration or
saves into a distribution.

## Results from the private HD333 trial

The filename study selected 333 DDS accepted by the real loader's full-key
rules, including 41 files under the source pack's UI folder. Original names and
subdirectories were retained; partial-key/image-only candidates were excluded.
Folder membership is not a certification of visual content or regional safety.

| Observation from the completed native trial | Count |
| --- | ---: |
| Indexed DDS | 333 |
| DDS loaded | 167 |
| Replacement associations | 16,918 |
| Reported replacement operations | 24,432 |
| Read/decode/upload errors reported | 0 |
| Catalog limit, unique full keys | 65,536 |
| Requests dropped after the limit | 9,209 |

These are session counters, not unique visible textures or FPS measurements.
An association may enqueue work; even a reported replacement does not certify
correct pixels. The catalog is incomplete. Its `disabled` count applies only
to lookups that reach the disabled branch: zero does not prove Tab was unused,
because the bridge can skip replacement lookups while HD is disabled.

The maintainer saw HD changes in menus and some scenery, but reported worse
in-race image quality and performance with HD On. **This is not a successful
validation of HD rendering.** A USA 0.4.0 test with the same 333 DDS looked and
ran better; engine, projection and execution conditions differed, so this is
not an isolated regional or GPU Thread benchmark.

## Known shared limitations and open questions

All 333 sampled files are DXT5/BC3 with one mip level. The compressed upload
path preserves supplied mips but does not generate a missing chain; the shader
clamps its manual LOD to the levels present. This is inherited engine behavior,
not a demonstrated PAL-only defect. Missing mips can contribute to shimmering
and sampling cost, but cannot alone explain a Thread-On-only slowdown.

The audited renderer/shader match the pinned USA reference. Alpha, UV/clamp,
cache invalidation, repeated associations and worker synchronization still
require equivalent-scene evidence. No speculative renderer fix is included.
See [identifier comparison](TEXTURE_COMPARISON.md) and
[graphics/performance evidence](GRAPHICS_PERFORMANCE.md).

## Next private validation

Keep 2x, AA Off, original blur, pacing On, vsync Off and one car/track/camera.
Compare HD Off/On at a fixed point, then separate UI and non-UI subsets using
the private manifest. Preserve original PAL artwork and keep ambiguous regional
content out of an approved pack. Only measure GPU Thread separately with the
same executable and scene. Do not install the full pack or convert candidate
keys as if they were verified replacements.
