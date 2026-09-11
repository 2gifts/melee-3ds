# New 3DS hardware testing

The first console is a New Nintendo 3DS, non-XL, running Luma3DS 13.4.

## Latest installation: menu stereo and collision update 9

Installed and read-back verified at **2026-09-11 03:14:08 UTC** (September 10 locally) under `SD:/3ds/melee`. Executable: 3,898,128 bytes; SHA-256 `9bb0ae9749d81810e96a45dfb498de7881759ad7dfcfbcfec67aca0113107b84`. Version 8's executable, metadata, log and visual archives are preserved under `build/hardware/2026-09-10-update9/sd-before-update/`. The five visual archives were verified; original disc assets are unchanged.

The user confirmed physical stereo works in version 8, requested stronger depth and stereo menus, and reported closed-slot portraits and unexpected Jungle Japes hits. Update 9 increases maximum slider depth by 50%, enables perspective menu depth with a forward CSS ribbon, fixes closed-slot portrait visibility, corrects stage-animation completion after `longjmp`, and repairs invalid cached bone-transform chains before collision queries. It also removes redundant stereo GPU state commands. See [update 9 implementation and evidence](MENU_STEREO_COLLISION_UPDATE_9.md).

The final emulator regression covered all 29 stage selections and 25 character icons, sustained 120-simulation-second Marth/DK combat on both Poké Floats and Jungle Japes, zero failed matrix recoveries or invalid contacts, forced GPU memory pressure, original menu/slot image checks in both eyes, Peach turnips, audio and protected-image integrity. GPU-command CPU time improved about 10.5% in alternating emulator samples; physical performance and stable 60 rendered FPS remain unverified. The exact operation first producing the invalid transform cache remains under investigation, although the captured cases now recover successfully.

Launch through the existing Homebrew Launcher entry. The slider controls gameplay and menu depth; fully down selects 2D. HUD/orthographic text stays flat, and the existing 4:3 default and expanded-view shortcut remain. The package is `dist/menu-stereo-collision-update-9.zip`. Installation evidence is `build/hardware/2026-09-10-update9/sd-update.json`; exact symbols, sources and QA are archived in `release-update-9/` beside it. HOME Menu packaging remains planned.

## Previous installation: stereo and streaming update 8

Installed and read-back verified at **2026-09-11 01:00:22 UTC** (September 10 locally) under `SD:/3ds/melee`. Executable: 3,896,228 bytes; SHA-256 `656d94b4cb0b7bf05617559aa836962a38497a5d7511d798542f1fb3094fe5ff`. The old executable/log/metadata/visuals are backed up under `build/hardware/2026-09-10-update8/sd-before-update/`; original disc assets are unchanged.

The user reports that version 7 runs well overall, with slow title/Versus/character transitions and intermittent freezes, including Marth/DK and Poké Floats. The returned executable matches version 7 exactly. The log's watchdog reports a GPU wait at frame 16,253; no new exception dump was present. The older dump is preserved but is not evidence for this freeze. Returned files and hashes are under `build/hardware/2026-09-10-update8/sd-returned-v7/`.

Version 8 adds slider-controlled native stereo, asynchronous disc reads, expanded bounded menu caching, reduced framebuffer/texture CPU work and corrected unused/null GPU texture bindings. It also records a bounded GPU draw trail for a future watchdog stall. See [implementation and test evidence](STEREO_STREAMING_UPDATE_8.md). The package is `dist/stereo-streaming-update-8.zip`; installation and read-back status are recorded separately in `build/hardware/2026-09-10-update8/sd-update.json`.

In version 8, the slider controls gameplay depth, with fully down selecting 2D; menus and HUD stay flat. The existing 4:3 default and expanded-view shortcut remain. The user subsequently confirmed that its physical stereo works. Loading improvement, freeze resolution and stable 60 FPS were not established by that report. HOME Menu packaging is still planned.

## Previous installation: geometry CPU update 7

Installed to `SD:/3ds/melee` and hash verified on 2026-09-10 at 23:06:24 UTC. The cumulative update includes the Peach turnip fix, larger decoded cache, faster exact ARM comparisons and ordered GPU cache eviction. All 29 stages/25 character slots, sustained Fox/Link combat, live cache checks and output references passed in Azahar. See [update 7 evidence](GEOMETRY_CPU_UPDATE_7.md). Physical speed and the previously reported hardware freeze remain unverified for this version.

Executable SHA-256: `c4cfaa61148dec0f2104b23d584d25711688fbf5167e8f34eb7d485347f01927`. The prior version 6 executable and SD log are preserved in `build/hardware/2026-09-10-geometry-cpu/sd-before-update/`; exact build symbols and evidence are archived alongside them.

## Previous installation: geometry capacity and turnip update 6

The user reports substantial frame-rate improvement with update 5, with missing textures on Peach's down-B turnips. The returned log ends normally and is preserved under `build/hardware/2026-09-10-update5-feedback/`.

Update 6 increases the bounded decoded-geometry cache to 12 MiB when a New 3DS launch supplies at least 80 MiB of ordinary heap. It also fixes the reproduced turnip defect using the original two-texture material and separate face coordinates. [Update 6 evidence](GEOMETRY_CAPACITY_UPDATE_6.md) covers the full stage/character-slot cache rotation, two-minute Fox/Link Fountain combat, repeated turnip pulls, GPU pixel references, memory pressure and final regression checks.

Installed and read-back verified on `SD:/3ds/melee` at **2026-09-10 22:20:39 UTC**: 3,882,484 bytes, SHA-256 `2493d250fdc0b09d774f37b107a7401daec1c2e613dd4a9b4c14df545e3c5692`. The five optional visual archives and original disc files remain intact. The prior executable/log/metadata and exact matching new symbols are under `build/hardware/2026-09-10-cache-turnips/`. Package: `dist/geometry-capacity-update-6.zip`.

Continue launching through the same Homebrew Launcher entry. Update 6 has not yet been measured on the console. Stable 30 FPS across stages and the 60 FPS target remain open; HOME Menu packaging remains planned.

## Startup crash, 2026-09-10

The SD card's executable SHA-256 was `cf9d4179dfa5bb0610f53b6268d0eb7c0e0cfcb167cdb76d072dc880d5594e11`, matching `renderer optimization 1`. Its log stopped after model detection. No ARM11 binary dump was found in the standard Luma dump directory; the supplied photo contains the fault registers.

The photo reports ARM11 data abort, write access, permission-section fault:

- PC `0x00101634`: `str r3, [r2]` in the native startup relocation loop.
- FAR / R2 `0x00371978`: an engine relocation word inside the RX code segment.
- The game engine had not yet started.

The exact crashed executable, corresponding ELF, log and package metadata are preserved in `build/hardware/2026-09-10-startup/`. Replaying its 3DSX loading and startup writes with RX/R/RW permissions reproduces `0x00371978` as the first forbidden store. The earlier emulator gameplay tests did not enforce this write restriction.

## Hardware startup fix 1

The build now resolves and byte-swaps BE8 relocation words before packaging. Their ELF relocation records are removed so the little-endian 3DSX loader cannot alter them. Native relocations remain intact. The now-unused startup fixup table is removed. Startup compares the actual segment addresses with expected addresses stored by the build, and stops with a readable message if a loader uses a different layout.

This uses the contiguous, page-aligned image layout of [Luma's homebrew loader](https://github.com/LumaTeam/Luma3DS/blob/master/sysmodules/loader/source/3dsx.c). It does not change console configuration or memory permissions. The same fixed-layout preparation is applicable to the later standalone app.

Validation completed:

- All 46,317 BE8 relocation words verified against the linked image, including 26,562 words in code and 1,626 in read-only data.
- Actual packaged 3DSX relocations replayed; every loaded code/rodata/data byte matched the finalized ELF. An alternate base address fails the layout check.
- Original startup and a complete timed Mario/Mario Versus match reached Results and returned to character selection in Azahar.
- All 3,493,964 checked code/read-only bytes remained unchanged after gameplay.
- Original results and character tables still matched the supported DOL.
- Hardware executable rebuilt without injected controls, emulator audio component, or GPU test fixtures.

Package: `dist/hardware-startup-fix.zip`.
Executable SHA-256: `464e0c081cadbae7373edbc8689fd3817e0bc8382ced30f69b1ce33661bc2fab`.
The user subsequently confirmed that this build launches on the New 3DS, with music and sound effects sounding correct. The next hardware report identified invisible menu elements and stuttering transitions.

The planned final delivery is a standalone HOME Menu app with its own icon. Continue using `.3dsx` builds for hardware iteration until the core match path is stable.

## Menu rendering fix 2

The SD executable matched startup fix 1. Its log and executable are preserved in `build/hardware/2026-09-10-menus/`. The user's two photos show background effects and text, but missing menu buttons and character portraits. The log progressed through 3,180 displayed frames with no engine error, texture barriers or menu memory shortage.

The same missing geometry was reproduced in Azahar. Disabling GPU vertex evaluation and the texture/raster caches did not restore it. Temporarily suppressing culling restored the complete CSS. Captured menu triangles used GX back-face culling with clockwise winding in the port's clip coordinates. The native GX-to-PICA front/back mapping was reversed for this pipeline. The corrected mapping retains culling, and the rebuilt app renders main-menu buttons, portraits, player panels and stage icons. The previous synthetic fixture assumed the same incorrect winding as the implementation; its old passing result did not establish original-menu correctness.

`tools/menu_visual_test.py` now navigates the original main menu, VS submenu, character selection, ready screen and stage selection with controller input and saves screenshots. It also requires visible pixels in the actual portrait region, rejecting the photographed blank-CSS regression. Initial fixed CSS has 7,711 bright portrait-region pixels; the ready screen has 10,114. See `build/menu-visual-test.json` and `build/menu-fix-*.png`.

Two bounded optimizations accompany the fix: unchanged textures survive menu visits until capacity/allocation pressure evicts them, replacing the previous 120-frame expiry; routine logs use an 8 KiB buffer and flush together every 300 rendered frames. Startup, preload boundaries, handled errors and exit retain immediate log persistence. An unhandled hardware crash can lose the latest buffered routine lines. Texture hashes still revalidate changed source addresses after GX invalidation, and the existing 16 MiB/256-entry limits remain.

The rebuilt smoke executable passed 64 point, 32 culling, 148 blend and two EFB-copy pixel-reference cases under a 512-vertex streaming limit, forcing 520 additional buffer reuses. Raster state caching matched the uncached path pixel-for-pixel in 160 cases. A separate 1 MiB texture pressure run forced 419 barriers and passed 53,785 indexed/linear lookup comparisons. Controller input inflicted 9% damage on the CPU. An uninterrupted HLE audio capture had zero capture-window underruns and a 14.91 ms maximum mixer gap. These are emulator validations, not physical frame-rate claims.

Package: `dist/menu-rendering-fix.zip`. Executable SHA-256: `73d3ddfc73c339b58a85c89b7e277f67190049b8a61ec455b38b8b1b2db02f8d`. It includes physical controls and normal DSP startup, with smoke controls and GPU fixtures excluded. All 46,317 BE8 relocation words and complete loaded segments passed package validation. This specific rendering update still needs console validation.

The original two-minute Mario/Mario CPU Versus match on Fountain of Dreams then finished, displayed Results and returned through START input to a visibly complete CSS (10,408 bright portrait-region pixels). The protected-image check found zero modified bytes across 3,494,428 code/read-only bytes. Evidence: `build/menu-fix-versus-flow-test.json`, `build/menu-fix-versus-return-test.json`, `build/menu-fix-returned-css.png`, and `build/menu-fix-live-image-verification.json`.

## Match and menu fix 3

The user confirmed that fix 2 restores most menus, with bad player-name labels, incorrect item-frequency rendering, transition stutter and a crash shortly after starting a match. The SD executable exactly matched fix 2. Its binary, matching ELFs, game log and 1,012-byte Luma dump are preserved in `build/hardware/2026-09-10-match-crash/`.

The Luma dump's captured 96 code bytes match that ELF. PC `003BE584` is `hsd_8039D0A0+0x50`; its `ldrh [r7,#30]` reads from `400AE166`. The routine's fake aggregate begins at the joint-reference array and assumes particle heads and the allocator follow at fixed offsets. On the original DOL, those offsets identify separate named globals; the ARM linker placed them elsewhere. Consequently, the first particle pointer was actually unrelated floating-point data (`400AE148`). The overlay now uses the actual particle-head array, joint array and particle allocator. This fixes the invalid source reference instead of suppressing the fault. The dump confirms generator ID `0x1AF`; stack candidates include generator/effect destruction and `Fighter_ChangeMotionState` (the raw stack scan is not a complete unwinder).

Item Switch and both name menus had similar assumptions about adjacent animation, glyph and item-order globals. Their views now refer to named objects. A previously uninitialized item-highlight animation value starts at frame zero when entering the item grid from the frequency row. The build also lacked the upstream Shift-JIS conversion step: even English fighter names use full-width SJIS characters. Preprocessed narrow C literals now use escaped CP932 bytes, preserving macros/header literals and leaving UTF-prefixed/wide literals alone.

The linked smoke and hardware images were compared against the owned DOL: 68 fighter-name strings, 150 keyboard strings, 292 glyph-variant strings, 287 font textures, kerning/glyph maps, animation tables and all six name-loop references match. Tests exercised every frequency (None through Very High), toggled an item, and verified the settings after closing/reopening. Controller input created the name A, returned to the name list and opened Fox/Pikachu CSS. Screenshots now show keyboard letters, saved A, and both fighter labels; the keyboard check found 882 letter-interior pixels, versus the previous empty cells. Evidence: `build/match-fix-menu-test.json`, `build/name-entry-test.json`, `build/menu-fix-fix3-*.png`, and `build/match-fix-menu-layout*.json`. Names/settings currently persist in the running session only; memory-card saving remains unavailable.

The hardware log showed 79 SD log flushes costing 1.65 seconds by rendered frame 300. Routine startup/mode lines now stay buffered; initialization checkpoints, completed preloads, handled errors, exit and 300-frame batches persist together. New 3DS startup optionally caches `GmTtAll.usd`, `MnMaAll.usd`, `MnSlChr.usd` and `MnSlMap.usd` (6,879,780 source bytes) before engine/audio startup, within an 8 MiB maximum and a 56 MiB ordinary-heap reserve. Reads still copy into private writable engine archives; allocation failure falls back to SD. The reader tests cover capacity, source immutability, EOF padding and cleanup. The texture cache now supports 1,024 entries and 1,024 hash buckets while retaining the 16 MiB byte limit; the hardware's old 256-entry limit filled at about 3 MiB. Host collision/eviction tests cover the expanded index. Repeated VS/CSS visits avoided about 53.7 MB of SD reads (`build/menu-cache-capacity-test.json`); that narrow cycle stayed at 253 textures under either slot limit and does not demonstrate a slot-capacity speedup. These changes target avoidable transition work; emulator timings do not establish physical console speed.

Controller-driven Fox reflector/jump/up-special input exercised the previously crashing cleanup four times and deleted 20 particles, followed by combat inflicting 9% on Pikachu. The default two-minute Fountain of Dreams match progressed through damage, KO/respawn, timer expiration, Results and return to CSS. All 3,495,348 checked protected-image bytes remained unchanged. A 1 MiB texture pressure test forced 544 completion barriers while 58,626 indexed lookups matched the reference scan. The uninterrupted audio capture had zero capture-window underruns and a 17.70 ms maximum mixer gap. Evidence is in `build/match-fix-effect-cleanup-live-test.json`, `build/match-fix-combat-test.json`, `build/match-fix-texture-index-live-test.json`, `build/match-fix-audio-capture-test.json`, `build/match-fix-versus-flow-test.json`, `build/match-fix-versus-return-test.json`, and `build/match-fix-live-image-verification.json`. These tests preceded the final addition of the 276,257-byte title archive to the same tested file-cache path; the engine and renderer are unchanged by that addition.

Package: `dist/match-menu-fix.zip`. Hardware executable: 3,855,700 bytes, SHA-256 `2432d1fcdadff420fd9a9623ddcf16681d2eb0186ccbc26ca4cb6a59e0f2bc4c`. All 46,340 BE8 fixups and loaded segments passed packaging verification. The release uses physical controls and normal DSP startup, with emulator controls/captures excluded. HOME Menu app/icon packaging remains planned after `.3dsx` hardware iteration stabilizes.

The final cold boot reached the main menu, Fox/Pikachu CSS and stage selection; the full 6,879,780 bytes of title/menu source data were served through the cache with no engine error (`build/final-menu-boot-test.json`, `build/final-menu-boot.log`). The verified release and metadata were then installed to `SD:/3ds/melee`. The prior executable/log were backed up, game assets were unchanged, and SD hashes match the package. See `build/hardware/2026-09-10-match-crash/sd-update.json`; exact deployed ELFs/executable/metadata are retained in `deployed-fix3/` beside it for decoding future hardware dumps.

## Performance and aspect update 1

The user confirmed that fix 3's text looks correct and a full two-minute match completed on the smaller New 3DS. The reinserted SD executable matches fix 3 exactly. Its gameplay log, preserved in `build/hardware/2026-09-10-performance/`, still shows roughly 85–110 ms render times in representative gameplay samples and substantial logging stalls.

The new package defaults to centered 4:3, with ZL + ZR + SELECT toggling expanded 5:3 world rendering without stretching fighters or HUD. It displays render FPS and simulation Hz separately. Gameplay optimizations reduce redundant VBlank waiting, geometry-source comparisons, material-driven vertex uploads and whole-heap cache flushes; a native worker handles routine SD logging. A cold-boot Yoshi's Story Training test also exposed an animation-transfer busy wait, now serviced explicitly by the cooperative ARAM queue.

[Performance and aspect notes](PERFORMANCE_ASPECT.md) list the exact emulator measurements, visual checks, two timed matches, GPU pixel references, audio capture and final affected-path validation. Emulator improvement is established for specific phases and the redundant VBlank wait, but stable 60 FPS on the console is not established. The release is `dist/performance-aspect-update.zip`, with its hash and BE8 image verification in `build.json`. HOME Menu icon packaging remains planned; continue using the same Homebrew Launcher entry.

Installed and read-back verified on `SD:/3ds/melee`: 3,860,668-byte executable, SHA-256 `4e07761dc23af9e3113a4845e6d5fdf074baaec73b24c92c0e93d9fe5e0652d2`. The prior executable, metadata and log are backed up in `build/hardware/2026-09-10-performance/sd-before-update/`; assets were unchanged. `sd-update.json` records the verified installation, and `deployed-performance-1/` contains the matching release ELFs and metadata. After the final ARAM wait change, 3,502,132 protected code/read-only bytes remained unchanged through Training, effects and controller combat (`build/performance-final-live-image.json`); the release's menu text/tables also match the original DOL (`build/performance-release-menu-layout.json`).

## Gameplay performance update 2

The user confirmed correct 4:3 output but reported approximately 12 FPS and a hard freeze during Fox versus Link on Fountain. The SD log remained readable and showed roughly 55,000 vertices and 800 draw calls per frame, with full geometry caches. There was no new Luma dump. The original freeze remains unreproduced; no specific instruction or proven root cause has been identified.

This update selects the original low-detail fighter models and removes Fountain's star rendering and live reflection. An optional, separately installed Diet Melee Classic Fountain archive simplifies the remaining scenery. Its collision arrays, stage/platform parameters, bindings, interactive transforms and animations were audited against the user's original assets. The original disc files remain intact. [Gameplay performance notes](GAMEPLAY_PERFORMANCE.md) document provenance, visual differences, tests and emulator limitations.

The final build completed two Fox/Link Fountain matches with returns to character selection, followed by a Peach/Pikachu match on the original Yoshi's Story archive. The observed Fountain rematch ran from timer 120 to Results; active gameplay samples had a 60 FPS median and 48.2–60.9 FPS range in Azahar at 300% emulated CPU clock. These are not hardware results and do not demonstrate stable 30/60 FPS on the console. Geometry/material reference checks, Fox particle cleanup, zero-underrun uninterrupted audio capture, both display modes, release text/image verification and post-match protected memory checks passed.

Automatic log preservation now runs on a sleeping native worker capable of preempting a busy game. A deliberately induced development-only stall produced a persisted ten-second frame/phase checkpoint and recovered. The release excludes that fault-injection hook. If another console freeze occurs, leaving the console powered for about 15 seconds gives the worker an opportunity to preserve evidence before a forced shutdown; it cannot guarantee recovery or logging through an OS/SD-service failure.

Installed to and read-back verified on `SD:/3ds/melee` at **2026-09-10 18:14:44 UTC**: executable **3,862,508 bytes**, SHA-256 `e5dafac9bf114b1908503107987abe8ca4ce87f71c26b0a5bbdd6de8a8a9f59a`; optional `visuals/GrIz.dat` **492,294 bytes**, SHA-256 `913134d58c804f44b9fe3dc41b981e50a076e6b27c3c58583fa74095ce22f61d`. The previous executable, metadata and game log are preserved in `build/hardware/2026-09-10-fox-link-freeze/sd-before-update/`. Exact deployed ELFs and metadata are in `deployed-gameplay-2/`; `sd-update.json` records the successful installation and unchanged original assets.

Use the same Homebrew Launcher entry. Physical frame rate and freeze behavior still require console validation. The HOME Menu app/icon remains planned after gameplay iteration stabilizes.

## Gameplay performance update 3

The follow-up adds New 3DS CPU clock/L2 readback, a bounded retry when a valid query confirms a slow state, and GPU command-arena capacity guards. Update 2's visual reductions and 4:3 behavior are retained. Forced 128 KiB command-buffer pixel tests passed through 294 command rollovers and 85 simultaneous vertex-buffer rollovers. Fox/Link special-effect stress, protected memory and release checks passed; [validation details](CPU_GPU_STABILITY.md) retain the evidence and limits. No console result has arrived for update 2 or 3, so stable hardware 30/60 FPS and the freeze's cause remain unresolved.

Installed and read-back verified on `SD:/3ds/melee` at **2026-09-10 18:41:55 UTC**: 3,864,708 bytes, SHA-256 `c82446cc0fe1330550b9598911ebec8c5e459b2f59c9c6a3ff279b1ba0dbfa5a`. Package: `dist/gameplay-performance-update-3.zip`. `build/hardware/2026-09-10-cpu-gpu-stability/` contains the prior SD files, matching deployed ELFs/map, metadata and installation report. Original disc files and the optional audited Fountain hash are unchanged. Launch through Homebrew Launcher as before; the bottom screen now also reports CPU status.
