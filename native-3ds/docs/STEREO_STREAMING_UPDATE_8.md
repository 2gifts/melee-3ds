# Stereo and streaming update 8

This cumulative update adds slider-controlled stereoscopic gameplay, moves uncached asynchronous disc reads off the game thread, expands the bounded menu-source cache and reduces framebuffer/texture CPU work. It retains update 7's geometry optimizations, turnip materials, offline tournament defaults, UCF and audited optional scenery. Physical version 8 performance and stereo presentation still need console validation. Stable 60 rendered FPS has not been established.

## Stereo display

Raise the console's 3D slider to enable depth during gameplay; lower it fully for the original 2D rendering path. Menus and HUD text stay flat. The existing default remains centered 4:3, and ZL + ZR + SELECT still toggles expanded 5:3 gameplay without stretching.

The renderer uses the original world camera's eye-to-interest distance for convergence. Near and far geometry receive opposite horizontal disparities; geometry on that plane has zero disparity. Orthographic and HUD cameras receive no displacement. The engine updates once, geometry is decoded/uploaded once, and native draw calls target both eye viewports. Conservative mesh rejection considers both eyes. A shared stereo color/depth allocation is retained after first use; frame completion presents both eye images together, including when command, vertex or texture pressure forces submission partway through a frame.

The zero-slider path retains the existing mono target and vertex shaders. Stereo adds GPU drawing work, so 2D remains the appropriate mode for measuring progress toward the highest frame rate. This implementation does not establish the comfort or correctness of physical autostereoscopic presentation from emulator screenshots alone.

The [Mario 64 Ultimate renderer](https://github.com/Epic0522/Super-Mario-64-3ds-port---Ultimate/blob/master/src/pc/gfx/gfx_citro3d.c) was used as a design reference. The Melee camera/target implementation is local to this port.

## Loading and CPU work

The New 3DS startup cache now includes nine title, menu and announcer archives, totaling 8,983,517 source bytes within a 9 MiB ceiling. Cached data remains immutable; the engine receives private copies. Allocation failure retains normal disc loading.

A measured Peach selection read approximately 3.6 MB, including the 2,102,080-byte animation archive, even on reselection. Texture uploads accounted for only a few milliseconds of the initial emulator trace. Keeping every fighter archive in memory would exceed the available budget, so a native worker now services uncached asynchronous DVD reads. Completion and original engine callbacks still run on the game thread. Cached menu reads complete directly, and synchronous reads retain a locked fallback. This removes individual blocking SD reads from the menu thread; archive parsing, relocation and necessary scene transitions can still take time.

The worker never enters the big-endian game engine or mutates GPU state. A 64-case live read-integrity test covers cached/uncached archives, alignment, varying lengths, EOF, invalid IDs, guards and synchronous reads interleaved with the worker. A separate test deliberately added 20 ms to uncached reads: menus continued advancing during Mewtwo, Marth and DK loading, recording 22, 21 and 28 frames while reads were in progress. This demonstrates nonblocking behavior under that injected delay, not a measured physical SD speedup.

Framebuffer-to-RGB565 conversion now reuses repeated source samples/rows and packs aligned pixel groups while preserving the original GameCube tiling and bytes. Host checks compare 58,081,277 pixels across 352 cases. The final candidate's paired live ARM run compared 80 copies and 11,993,600 pixels exactly: conversion took 196.676 ms versus 1,063.975 ms for the previous routine. This roughly 5.4-fold improvement applies only to that conversion routine, not overall gameplay. An earlier 187-copy run with different copy sizes measured a 4.3-fold routine improvement.

Texture visibility now tracks CPU write ranges, palette writes, disc-read destinations and framebuffer-copy destinations instead of rehashing every resident texture at each routine frame boundary. Explicit game texture invalidations remain supported. Old/new live validation rehashes reused source bytes independently; the host test covers 100,000 overlap cases and 87,120 tiled sizes, including address overflow. Native allocations already referenced by GPU commands remain protected.

In a final mono Venom/Mr. Game & Watch/Link comparison, alternating old/new invalidation modes reduced source hashing from about 73 checks and 0.55–0.56 MB per render to approximately zero. The sampled texture-lookup phase fell from 1.20–1.38 ms to 0.24–0.26 ms per render. Geometry source traffic stayed around 3.7 MB per render, but native draw requests varied between windows. The observed emulator rates of 55.7–58.6 FPS therefore do not establish a controlled whole-game speedup or physical 60 FPS. Evidence: `build/update8-qa/texture-visibility-performance.json` and its complete profile.

## Returned freeze and resource safety

The returned SD binary exactly matched version 7. Its log stopped at frame 16,253, and the watchdog recorded phase 2, waiting for the GPU. There was no newer Luma exception dump; the existing dump predates this build. The log does not identify the precise offending draw or prove a matchup-specific cause. The returned executable, metadata, log and dump are preserved under `build/hardware/2026-09-10-update8/sd-returned-v7/`.

Inspection found two concrete hazards. The pinned Citro3D implementation dereferences a null texture on units 1/2 before checking its type; a local adapter now records the intended null binding safely. Ordinary untextured draws could also retain an earlier texture binding after cache retirement. Each frame and draw now clears unused texture units before resources can be reclaimed. The adapter uses a pinned, licensed internal header with compile-time layout checks against the linked SDK.

A bounded 32-draw trail records submitted addresses/counts and the final command-buffer size in ordinary memory. A future watchdog GPU stall writes that trail without dereferencing retired resources. These fixes address actual invalid resource use, but emulator success does not conclusively attribute or close the reported hardware freezes.

## Validation and release evidence

Final candidate results are collected in `build/update8-qa/`, with original interrupted logs retained. Tests use the smoke build and Azahar 2126.1 at the existing 300% CPU setting; emulator FPS must not be treated as physical New 3DS FPS. Controller-driven tests do not patch fighter positions, damage or timers.

The four stereo GPU fixture modes cover half/full slider depth in both aspect modes, checking near/convergence/far/HUD disparities and the 4:3 bars. Peach pull/throw sequences and fresh geometry-cache comparisons run in both 2D and stereo. Command, stream and texture-pressure phases separately force GPU resource reuse. Pressure tests must actually create the relevant resource traffic: cached Final Destination cannot trigger upload-time texture retirement, so the texture phase uses Stadium's live screen copies.

The final all-stage sweep passed all 29 stages and all 25 character slots in one continuous session with stereo enabled. It completed 3,898,483 live source-content comparisons and observed 6,444 write-range invalidations without a stale-texture failure. Minimum sampled ordinary heap availability was 24,434,624 bytes. Sampled decoded geometry, native geometry and texture maxima were 12,579,189, 4,194,262 and 16,777,120 bytes, respectively, within the existing 12/4/16 MiB limits. These short per-stage checks cover loading, ordinary inputs and resource reuse; they are not exhaustive move/hazard testing. Their profiling includes deliberate validation overhead and is unsuitable as a speed benchmark.

Marth/DK on Poké Floats and Fox/Link on Fountain each completed a further 7,200 simulation updates with the original Training CPU Attack setting, stereo active and observed damage. Fox/Link also passed twelve special-attack cycles. Real-game stereo captures differ between eyes at half/full depth in both aspect modes; slider-zero captures are identical. Eight Peach pull/throw cycles passed in each of 2D and stereo, retaining the original turnip body/face materials. All eight layered-material GPU reference cases remained pixel-exact, and mono/stereo cache validation passed 6,281 fresh native vertex/index comparisons plus 187,890 original/optimized geometry-source comparisons.

The final uninterrupted audio capture had zero capture-window underruns, with a maximum mixer gap of 10,729 microseconds. Cumulative audio counters also include earlier debugger stops and forced resource pressure; they are not an uninterrupted-session audio measurement. After the complete regression, all 3,550,316 checked protected code/read-only bytes were unchanged. Completion is recorded in `build/update8-stages-qa.log`.

Package: `dist/stereo-streaming-update-8.zip`. The release excludes test controls, injected disc delays, emulator-only audio and validation fixtures. ELF-to-3DSX repacking and loaded-image checks pass, with 46,491 BE8 relocation words verified. The executable is **3,896,228 bytes**, SHA-256 `656d94b4cb0b7bf05617559aa836962a38497a5d7511d798542f1fb3094fe5ff`.

Installation and read-back verification are recorded separately in `build/hardware/2026-09-10-update8/sd-update.json`; a built package alone does not mean the SD has been updated. Original disc assets remain separate from the five optional visual archives. HOME Menu packaging remains planned while `.3dsx` iterations continue.

Installed and read-back verified at **2026-09-11 01:00:22 UTC** (September 10 locally) under `SD:/3ds/melee`. The previous executable, metadata, log and optional visuals are backed up in `build/hardware/2026-09-10-update8/sd-before-update/`. Original disc archives were unchanged. The full local `dist/native-alpha/` package also carries the same version 8 executable. Matching development/release ELFs and maps, source hashes and regression evidence are preserved under `release-update-8/`.
