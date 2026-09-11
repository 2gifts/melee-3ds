# Geometry capacity update 6

This cumulative update includes update 5, increases the shared decoded-geometry cache, and fixes Peach's missing turnip body/faces. A New 3DS launch with at least 80 MiB of ordinary heap uses a 12 MiB cache ceiling; smaller heaps retain 6 MiB. The cache change applies across stages and fighters without reducing visual detail or changing collision, animation or simulation rules.

The menu-source reservation increases by the same 6 MiB, preserving the previous amount reserved for other allocations. All 6,879,780 bytes of known title/menu sources still preload. The GPU-accessible geometry cache stays at 4 MiB and the texture cache at 16 MiB. Decoded-cache allocation failure continues through the existing uncached renderer.

## Diagnosis and controlled comparison

The update-5-era Venom/Mr. Game & Watch/Link profile showed roughly 463 decoded-cache misses and 82,740 streamed vertices per rendered frame. The 6 MiB cache filled before it could retain the scene's working set, forcing repeated decoding. The native cache also performed capacity scans, but that was a secondary cost; no eviction-policy rewrite is included here.

The same live match was tested at 6 → 8 → 12 → 6 → 12 MiB. Both 12 MiB observations used approximately 10 MiB of decoded-cache data and sharply reduced misses/streaming; returning to 6 MiB reproduced the slowdown. All observations stayed within the selected memory limit.

| Decoded cache limit | Cache misses/render | Streamed vertices/render | Emulated renders/sec | Emulated game updates/sec |
|---|---:|---:|---:|---:|
| 6 MiB | 463.8 | 82,740 | 7.51 | 37.54 |
| 8 MiB | 461.3 | 82,113 | 7.49 | 37.46 |
| 12 MiB | 32.8 | 4,513 | 42.93 | 58.95 |
| 6 MiB, repeat | 463.3 | 82,571 | 7.50 | 37.50 |
| 12 MiB, repeat | 31.4 | 4,497 | 43.97 | 58.83 |

These are observations from Azahar with its existing 300% CPU-clock setting, not New 3DS measurements. Camera animation changes visible draw counts somewhat between windows. The repeated capacity reversal and cache-work counters support the identified cache-thrashing problem; they do not prove physical 30 or 60 FPS. Evidence: `build/geometry-work-profile.json` and `build/geometry-capacity-profile.json`.

## Turnip rendering

The defect was reproduced using Peach's actual down-B input. The original body material blends a base texture, subtracts a separately mapped animated face texture, then applies lighting. The former single-texture approximation replaced both texture samples with white during material preparation and produced black. The live GX program and original archive bindings are saved in `build/turnip-live-material.json` and `build/turnip-assets.json`.

The renderer now recognizes that complete material program and evaluates it with two native texture units and independent coordinates. The recognition checks the operations and register dependencies rather than a turnip ID or image address. Ordinary materials retain their existing shader, vertex layout and caches. The additional coordinate stream costs 128 KiB of linear memory; matching layered meshes use uncached vertices so neither coordinate set can be lost through reuse. Texture slots are pinned by key during dual uploads and then looked up again after possible slot compaction. Separate sampler objects also preserve distinct wrap settings when two maps share image storage.

No character archives, face probabilities, damage values, or face images were changed. Eight final controller-driven pull/throw cycles produced original turnip faces 0, 2, 3, 5 and 6; earlier captures also exercised face 1. Visible body and facial details were inspected at the native output resolution.

## Validation

The final smoke build selected the 12 MiB ceiling automatically at boot, with no debugger budget override. Controller-driven movement, jumping, attacking, special and shield input passed 192,864 geometry comparisons against fresh decoding and 141,231 material comparisons. The cache remained below its limit while retaining newly generated animation geometry.

The cache-capacity rotation covered all 29 selectable stages and all 25 character slots. Minimum sampled ordinary-heap availability was 26,631,272 bytes, and every decoded-cache observation stayed below 12 MiB. These are total available bytes, not a largest-contiguous-allocation guarantee. The test cursor missed one stage-selection input; a held-input retry in the test tool completed the remaining five stages. The game kept advancing during that test interruption. Combined evidence: `build/update6-all-stage-complete.json`.

The turnip material passed 800,000 component comparisons against the original TEV evaluator, with zero error, and rejected 80 altered programs. Eight actual GPU comparisons against independently baked reference textures matched all pixels exactly, including different secondary UVs and eight forced texture-cache barriers. The ordinary renderer's 160-case state-change comparison also retained exact pixels. The development fixtures and their controls are excluded from the release.

The combined cache/turnip build completed 7,200 simulation updates of Fox/Link Fountain CPU combat with observed damage. Display mode toggles and uninterrupted audio capture passed, with zero capture-window underruns. Final smoke regression evidence is collected in `build/update6-qa/` and `build/update6-final-qa.log`; the full cache-capacity rotation preceded the turnip renderer change.

Release packaging passes BE8 image verification, menu/font table checks, exact ELF-to-3DSX repacking, debug-fixture exclusion and all five optional-stage asset audits. Hardware speed and long-session stability of update 6 remain unverified.

Release binary: 3,882,484 bytes; SHA-256 `2493d250fdc0b09d774f37b107a7401daec1c2e613dd4a9b4c14df545e3c5692`.

## Hardware feedback and installation

The user reported that update 5 runs well with substantially better frame rate, and reported the turnip defect. The returned log ended with a normal application exit after 28,140 rendered frames. Its final samples were about 49 render FPS and 60 game Hz, but the log does not establish which stage those samples represent or stable performance across stages. That log and metadata are preserved in `build/hardware/2026-09-10-update5-feedback/`.

The scoped SD installer is `tools/update_sd_cache6.ps1`. Its read-only preflight confirms the installed update-5 hash and unchanged original disc archives. Installation status is recorded separately in `build/hardware/2026-09-10-cache-turnips/sd-update.json`; do not infer installation from the presence of the package alone.

Installed to `SD:/3ds/melee` and read-back verified at **2026-09-10 22:20:39 UTC**. The installed binary matches the release hash above. The prior executable, metadata, log and five optional visual archives are preserved in `build/hardware/2026-09-10-cache-turnips/sd-before-update/`. Original disc archives were unchanged; the SD's Peach archive also matched the local original. Matching release/smoke ELF files and maps are archived under `release-update-6/`.

Final regression completed successfully: 251,756 total geometry and 188,237 material comparisons, eight pixel-exact layered GPU cases, 160 pixel-exact ordinary raster-state cases, both display modes, zero audio capture-window underruns, and 3,530,092 protected image bytes with no changes. The final Venom sample retained its cache below 12 MiB and about 26.8 MB of ordinary-heap availability. All speed figures remain emulator observations until the user tests this installed version.
