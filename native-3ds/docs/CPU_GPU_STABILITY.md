# CPU speed verification and GPU command capacity

This follow-up retains gameplay performance update 2's low-detail fighters,
audited Diet Fountain scenery and 4:3 output. Physical-console 30/60 FPS and
the reported freeze remain unverified.

## CPU setting

The previous build requested New 3DS speedup through libctru, but that API
returns no success result. `port/3ds/cpu_speed.c` now reads the actual clock
and L2 state using Luma's system-information extension. The bottom screen
and game log show the result. Only if valid readback reports a slower clock
or disabled cache does it attempt the same clock/cache bitmask used by
Rosalina, once, and read back again. It does not report success based on the
request alone. Unsupported firmware/emulators say the request is unverified.

The libctru speedup request is retained for suspend/restore. An APT hook
queues a recheck after restore/wakeup; the main thread performs it outside
the callback. Original 3DS models bypass this path. Host tests cover fast,
slow, unavailable, invalid and failed queries, bounded retry and restoration.

Primary API sources:

- [libctru speedup implementation](https://github.com/devkitPro/libctru/blob/master/libctru/source/os.c)
- [Luma CPU status extension](https://github.com/LumaTeam/Luma3DS/blob/master/k11_extension/source/svc/GetSystemInfo.c)
- [Rosalina New 3DS CPU settings](https://github.com/LumaTeam/Luma3DS/blob/master/sysmodules/rosalina/source/menus/n3ds.c)

## GPU command capacity

The renderer allocates a 2 MiB command arena. libctru's raw command upload
uses an unchecked copy, while ordinary writes can panic on exhaustion.
Citro3D's FrameSplit advances within that arena instead of recycling it.
Each physical draw now reserves 64 KiB for shader/context uploads and list
finalization. Near capacity, it submits and completes the current queue,
preserves the color/depth buffers and all vertex/index allocations, and
continues in a fresh command arena without presenting a partial frame.

The guard covers expanded alpha-test passes, points, shadows and EFB-copy
draws. It records peak command bytes and rollover counts in the game log.
This prevents a concrete exhaustion hazard; there is no evidence yet that
the user's freeze was caused by command exhaustion.

`tools/command_buffer_test.py` lowers command capacity to 128 KiB in a
development build and checks point, culling, blending, EFB and raster-state
reference pixels. It also exercises simultaneous vertex-buffer recycling.
The capacity override is excluded from the hardware release.

## Rejected cache experiment

A range-aware geometry-source invalidation prototype was compared against
the existing cache in idle and CPU Attack Fox/Link Fountain scenes. Both
averaged about 59 FPS in Azahar's 300% CPU configuration, with roughly
1.0–1.06 ms of geometry lookup per rendered frame. The new skip counter
remained zero: relevant writes preceded the normal full-frame invalidation.
The prototype was removed from production code. Its independent boundary,
overflow and 20,000-overlap checks remain reproducible under
`references/experiments/source_dirty.h` and `tools/test_source_dirty.py`.
Evidence: `build/range-cache-combat-profile.json`.

## Completed validation and installation

At a forced 128 KiB command budget, point, culling, blending, EFB-copy and
raster-state pixel checks passed. The renderer completed 294 command-arena
rollovers and 85 vertex-arena rollovers, then restored the production limits.
The normal scene's observed command high-water mark was 151,424 bytes,
well below the real 2 MiB arena. This result does not implicate command
exhaustion in the reported freeze. See `build/command-buffer-test.json` and
its accompanying log.

Twelve Fox side-special/reflector/laser input cycles against an attacking
Link passed and observed the actual special states. A subsequent combat
sample averaged 59.58 rendered FPS and 59.79 simulation Hz across 23.6
guest seconds in Azahar at 300% CPU configuration. It is not a hardware
benchmark. All 3,507,116 checked protected image bytes remained unchanged.
Evidence: `build/command-guard-fox-link-stress.json`,
`build/command-guard-profile.json`, `build/command-guard-live-image.json`.

The release passed all 46,376 BE8 fixups, exact packaged loaded-segment
comparison, original menu text/table checks, and exclusion of development
controls and artificial command limits. The rejected range-invalidation
prototype is absent. The tested smoke build and release differ in their
normal development-only features and startup update label.

`dist/gameplay-performance-update-3.zip` contains the replacement executable
and unchanged optional Fountain archive. Executable: 3,864,708 bytes,
SHA-256 `c82446cc0fe1330550b9598911ebec8c5e459b2f59c9c6a3ff279b1ba0dbfa5a`.
Installed and read-back verified on `SD:/3ds/melee` at
**2026-09-10 18:41:55 UTC**. The old executable, metadata and log are backed
up under `build/hardware/2026-09-10-cpu-gpu-stability/sd-before-update/`;
matching release ELFs/map and metadata are in `deployed-gameplay-3/`.
`sd-update.json` records installation and unchanged original assets.

No new hardware result has arrived since the roughly 12 FPS report for
the much earlier performance/aspect update 1. The next console run should
establish both its actual clock setting and whether the major scenery/model
reduction reaches the 30 FPS minimum. The 60 FPS target is still open.
