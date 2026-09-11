# Performance and aspect update 1

The user completed a full two-minute match on a smaller New 3DS using match/menu fix 3. That establishes playable hardware gameplay, with remaining slowdown. This update targets gameplay CPU cost, GPU submission and correct display proportions. **Stable 60 rendered FPS on a physical New 3DS remains unverified and has not been established.**

## Display and controls

Each launch starts in 4:3: 320×240 centered on the 400×240 top display, with 40-pixel black bars. Hold ZL and ZR, then press SELECT to toggle expanded gameplay. SELECT alone still exits to Homebrew Launcher. The choice is session-only.

The top display is 5:3. Expanded mode multiplies the world camera's horizontal projection by 1.25, revealing 25% more horizontal world at the same vertical scale. It does not alter stored FOV, camera tracking or game rules. HUD, menus and shadow cameras retain their 4:3 proportions. Perspective projection, camera visibility queries, viewport/scissor conversion, point/line width and framebuffer-copy coordinates agree on the active width. Lower-screen render FPS and game update Hz are separate measurements; 60 game updates do not imply 60 rendered frames.

The approach follows the game-camera adjustment in [Dolphin's GALE01r2 proper widescreen code by Dan Salvato and mirrorbender](https://github.com/dolphin-emu/dolphin/blob/master/Data/Sys/GameSettings/GALE01r2.ini), adapted to 5:3 and implemented in the native camera code. There is no emulator or runtime Gecko-code dependency.

## Performance changes

- Melee already paces simulation through its original VI/pad queue. Removing the additional native VBlank wait avoids quantizing a late render to another display interval. The renderer still waits for the previous GPU queue before recycling buffers.
- Frame submission cleans each GPU command segment and changed resource instead of flushing the whole linear heap. A link wrapper also covers Citro3D's internal command splits during clears and texture copies. Vertex, index and texture writes retain explicit cache maintenance.
- Geometry entries share snapshots of identical source ranges. A range is compared once per GX/cache visibility generation; changed snapshots invalidate their dependent entries. GX invalidation, CPU cache visibility calls and preload transitions advance that generation. This does not assume animation data stays constant between frames.
- Animated material-register colors now reach the vertex shader as a fixed vertex attribute. They no longer force otherwise identical vertices to be decoded and uploaded again. This fits the PICA vertex uniform limit without reducing the matrix palette.
- The shade compiler can retain an intermediate clamp through an exact identity operation, allowing eligible two-channel materials to use the GPU. Nonidentity operations that would change clamp semantics still use the existing fallback.
- Residual BE8 `memcmp` calls use aligned ARM multiword loads, falling back to byte comparison for mismatches and incompatible alignment, without reading past the supplied length.
- A lower-priority native worker owns routine SD logging. A 32 KiB queue keeps slow file writes off the game thread; handled errors and exit wait for persistence. Log errors and dropped entries are counted. Worker creation failure retains synchronous logging.

An additional loading hang appeared during cold-boot Fox/Pikachu Training on Yoshi's Story. The original ARAM busy wait expected asynchronous DMA interrupts. The cooperative adapter might complete an earlier queued request when interrupts were restored, leaving this request pending; ARM compilation reduced the empty wait to an infinite branch. The source overlay now services queued ARAM transfers inside that wait. The affected cold-boot path then loaded, rendered, toggled display modes and completed controller-driven particle cleanup. The prior hang is recorded in `build/performance-arq-hang.json` and `.log`.

## Measurements and validation

Azahar 2126.1 uses an experimental 300% CPU setting here because it does not implement the New 3DS speed request. These figures are relative emulator evidence, not predictions of console speed.

| Check | Result | Evidence |
|---|---|---|
| Same Fountain Training session, old/new VBlank wait, repeated | 28.52 / 36.91 / 28.76 / 36.62 rendered FPS; approximately 59–60 simulation Hz | `build/profile-gpu_vblank_wait.json` |
| Yoshi's Story idle Training, expanded/4:3/expanded/4:3 | 53.94 / 54.80 / 55.11 / 53.54 rendered FPS; approximately 59–60 simulation Hz | `build/profile-mp_native_expanded.json` |
| Shared geometry-source comparison | Geometry lookup phase about 6.1–6.6 ms → 4.87–4.93 ms per render | `build/profile-geometry_source_compare_all.json` |
| ARM comparison fixture | 65,600 cases passed; 16,384-call microbenchmark 7.76 → 6.08 ms | `build/native-memory-test.json` |
| Fresh geometry/material evaluation during animated combat | 106,142 geometry and 84,754 material comparisons passed | `build/performance-geometry-cache-test.json` |
| Display toggle on Yoshi's Story | 4:3 → expanded → 4:3; zero pixels outside the bars in 4:3, 19,200 nonblack side pixels expanded; stored FOV/aspect and view matrix unchanged | `build/display-mode-test.json`, `build/performance-4-3.png`, `build/performance-expanded.png` |
| GPU stress with only 512 transient vertices | 707 additional safe buffer reuses; 64 point, 32 culling, 148 blending cases and both EFB-copy references exact at destination precision | `build/performance-streaming-buffer-test.json` |
| Retained raster state | 160 cases, three draws per case; cached and uncached results pixel-identical | `build/performance-raster-state-test.json` |
| Uninterrupted NDSP capture | Zero capture-window underruns; maximum mixer gap 6.912 ms; longest all-zero run one sample | `build/performance-audio-capture-test.json` |
| Async logging over two timed matches and results | 175 flushes, no dropped entries or file errors by frame 48,505 | `build/performance-log-worker-test.json` |
| Timed Versus | Fox/Pikachu completed Fountain and Yoshi's Story matches and reached Results; Fountain returned to character selection | `build/performance-two-matches.log`, `build/performance-yoshi-versus-flow-test.json`, `build/performance-fountain-versus-return-test.json` |
| Protected image after those matches | All 3,502,044 checked code/read-only bytes unchanged | `build/performance-live-image-verification.json` |
| After the ARAM wait fix, Yoshi's Story controller input | Fox effects called cleanup three times and deleted 19 particles; a subsequent attack dealt 9% to Pikachu | `build/performance-final-effect-cleanup.json`, `build/performance-final-combat.json` |

Those two timed runs, GPU stress and audio checks preceded the narrowly scoped ARAM wait fix and atomic diagnostic-counter reads. The geometry/material validation preceded the final ARM comparison implementation; the latter has its own on-ARM fixture. The affected ARAM path and final package are checked separately. The shader host test covers 66,049 deferred-clamp combinations, and scissor tests cover 60,000 rectangles across both widths.

The old hardware log is preserved at `build/hardware/2026-09-10-performance/game.log`. Its gameplay render samples commonly cost roughly 85–110 ms, with no idle calls; log flushes also caused stalls. The emulator's whole-heap-flush A/B did not show a measurable FPS gain, so that change particularly needs real-device timing. A short isolated phase improvement or lighter-stage sample is not evidence of sustained 60 FPS across matches.

## Delivery and next measurements

The executable-only package is `dist/performance-aspect-update.zip`. Reuse the installed `/3ds/melee/files` assets and launch the same Homebrew entry. `build.json` records its hash and validation. Exact deployed ELFs, executable and image metadata are retained under `build/hardware/2026-09-10-performance/deployed-performance-1/` for future crash decoding.

Release executable: 3,860,668 bytes; SHA-256 `4e07761dc23af9e3113a4845e6d5fdf074baaec73b24c92c0e93d9fe5e0652d2`. Packaging verified all 46,373 BE8 words and full loaded-segment equality against the finalized release ELF, and rejected unexpected loader placement. Development controls, memory fixtures and GPU A/B flags are absent. Hardware uses normal DSP startup.

The on-screen FPS display and `/3ds/melee/game.log` provide actual-console measurements for the next optimization pass. Prioritize sustained gameplay, including attacks, effects and stage animation. Full-roster/stage coverage, exact complex materials, saving and multiplayer remain unfinished. A standalone HOME Menu app with its own icon remains planned after these `.3dsx` iterations.
