# Texture conversion update 5

This is a cumulative update to the [offline and performance update 4](OFFLINE_PERFORMANCE_UPDATE_4.md). The shared renderer converts uncompressed and palette-indexed GameCube textures directly into native PICA200 tiled storage. It applies to fighters, effects, menus and all stages using those formats. It does not reduce texture resolution or alter stage mechanics.

The converter processes native 4-by-4 Morton subtiles, moving source tile addressing outside the inner pixel loop. Exact intensity, intensity/alpha and RGB565 formats avoid expanding to RGBA and immediately compressing again. Palette textures reuse a small preconverted palette. Partial tiles and power-of-two padding preserve the existing edge-clamping behavior. CMPR textures retain their existing specialized converter. Cache sizes and resource lifetimes are unchanged.

Update 4's global mesh rejection, reduced fighter detail, disabled projected shadows, five audited Diet archives, unlocked tournament defaults, native UCF 0.84, aspect controls and crash fixes remain included.

## Verification

The host comparison extracts the original decoder from the actual renderer source. All 960 cases and 12,720,000 output pixels match byte-for-byte across the ten affected formats, including palette limits, missing palettes, unaligned input pointers and padded edges. Destination canaries remain intact. The host RGB565 640-by-406 conversion benchmark measured 2.625 ms for the reference and 0.417 ms for the new converter; these are PC CPU measurements, not New 3DS FPS.

Live development builds additionally convert each selected texture using both implementations and compare the entire native texture allocation. Timing records pair both converters on identical source bytes. The original runs second, so it can benefit from source data already being cached. These checks and their timing counters are absent from the hardware release.

The final smoke build passed 362 paired live uploads across nine formats while traversing Stadium, Yoshi's Story, Fountain, Battlefield, Venom and their intervening menus. CI14X2 is covered by the host tests but was not observed in this live sequence. All nine observed formats converted faster in the paired ARM measurements:

| GX format | Paired uploads | Reference mean ms | New mean ms |
|---|---:|---:|---:|
| I4 | 160 | 0.960 | 0.171 |
| I8 | 13 | 1.321 | 0.065 |
| IA4 | 23 | 0.324 | 0.049 |
| IA8 | 3 | 1.730 | 0.211 |
| RGB565 | 117 | 89.884 | 16.102 |
| RGB5A3 | 9 | 0.994 | 0.343 |
| RGBA8 | 2 | 2.394 | 0.457 |
| CI4 | 5 | 1.820 | 0.358 |
| CI8 | 30 | 5.126 | 0.932 |

These are conversion-only timings from emulated ARM, with different source dimensions across formats. They are not console timings or a whole-game multiplier. Exact records and screenshots are under `build/update5-qa/texture-validation.json` and `build/update5-qa/stage-*.png`. The run exercised texture evictions within the existing 16 MiB limit and recorded zero GPU command-buffer barriers.

The final follow-up passed 30 simulated seconds of Fox/Link CPU combat on Venom and 60 on Fountain, with observed damage. The display test on Stadium measured 0 → 18,391 → 0 nonblack pixels in the side strips when switching 4:3 → expanded → 4:3, while the original camera aspect/FOV stayed unchanged. The first display check on Diet Fountain exposed only 30 side pixels after a KO and did not meet the test's 100-pixel scenery precondition; its image is preserved and the unchanged assertion was rerun on Stadium. The audio capture completed without an underrun in its uninterrupted measurement window. Cumulative audio counters include debugger pauses and are not a zero-underrun claim for the whole session. Live unlock/rule/UCF-hook and protected-image checks also passed.

Broad A/B frame timing on Stadium was not treated as a controlled speedup: its screen switches between live footage and static information, changing texture-upload work during the observation windows. Paired per-upload comparisons are the relevant conversion evidence. This optimization cannot imply the same multiplier for total gameplay speed; physics, animation, geometry, texture hashing and GPU execution remain other costs.

Release executable SHA-256: `994cce036cf4262d53a1ab182525bab7ccdfe1ae9e9498e156cd6887154f486e`.

Installed to `SD:/3ds/melee` on September 10, 2026 at 21:25 UTC. Update 4 and the existing SD log/metadata were backed up under `build/hardware/2026-09-10-texture-repack/sd-before-update`. The executable, five optional visuals and metadata were flushed and verified by readback hashes; original disc archives were checked unchanged. Installation evidence is `build/hardware/2026-09-10-texture-repack/sd-update.json`. Exact release/smoke symbols and validation records are retained under `build/hardware/2026-09-10-texture-repack/release-update-5` for future fault decoding.

## Reproduction

- `tools/test_texture_repack.py`: independent host comparison against the renderer's original decoder.
- `tools/update5_qa.py`: controller-driven scene changes, paired live texture checks, combat, aspect, audio, QoL and protected-image checks.
- `tools/package_update.py`: release repack comparison, BE8 image verification, development-fixture exclusion, original-asset audits and ZIP verification.
- `tools/update_sd_texture5.ps1`: pinned update-4-to-update-5 installer; validates without writing unless invoked with `-Install`.

Stable 30 rendered FPS across gameplay remains the immediate hardware target, with 60 FPS the longer-term goal. Emulator timing does not establish either target, and the older physical Fountain freeze has not been conclusively attributed to a specific fixed issue.
