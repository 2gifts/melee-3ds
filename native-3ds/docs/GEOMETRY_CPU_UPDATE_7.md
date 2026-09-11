# Geometry CPU update 7

This cumulative update retains update 6's Peach turnip rendering and larger decoded-geometry cache. It reduces two shared CPU costs across stages and fighters while retaining the existing geometry checks, memory ceilings and GPU lifetime rules.

## Geometry source comparisons

The ARM comparator used for geometry snapshots now processes four bounded 16-byte groups per loop. It compares every requested byte, preserves unsigned byte ordering on a mismatch, and handles unaligned inputs and tails without reading beyond the requested length. Original game calls continue using the previous general-purpose comparator.

The ARM/BE8 test exercises 262,208 combinations of alignment, length (0–1024 bytes), and mismatch location. All match the independent byte-order reference. On distinct equal buffers, the emulator microbenchmark measured 6.096 ms for the previous ARM routine and 5.526 ms for the unrolled routine, about 9% less time. This is a routine benchmark, not a whole-game FPS increase.

A live Venom/Mr. Game & Watch/Link comparison alternated the two implementations. In the settled windows the old routine's geometry-lookup phase measured 5.085 and 5.305 ms per render; the new routine measured 4.586 ms. Source traffic stayed close to 3.72 MB per render. The earlier window had different draw work and is excluded from this comparison. Enabling a second comparison on actual live geometry also passed 639,760 old/new result checks.

Evidence: `build/cache-lru-native-memory.log` (initial expanded fixture), `build/update7-qa/native-memory-test.json` (final fixture), `build/profile-geometry_block_compare.json`, `build/block-compare-live-validation.json`.

## GPU geometry eviction

The 4 MiB native geometry cache now maintains an ordered list of occupied slots. On the first use of an entry in each frame, that entry moves to the newest end. The oldest end supplies eviction candidates directly, avoiding a full scan of all 2,048 slots. Entries referenced by the current frame remain protected; when none can safely be released, rendering retains the existing uncached fallback. The list costs 8,200 bytes of ordinary memory and does not enlarge GPU allocations.

The host test compares 100,000 mixed operations across all slots against an independent ordered-array reference, checking both link directions and removed entries. A development-only live validator compares each eviction candidate's frame with the previous full scan, verifies chain membership and allocation accounting, and can compare every reused native vertex/index against fresh conversion of the current bridge inputs.

In settled Venom windows, the full-scan mode visited about 43,638 slots per render and spent 2.582 ms in native geometry handling. The ordered-list windows visited about 21 slots per render and spent 2.089–2.169 ms in that phase. Render rates were 43.16 versus 45.01–45.09 in the emulator; animation and same-age eviction choices vary between windows, so these are observations rather than a deterministic benchmark. Both modes kept the same cache limit and similar miss counts. The earlier full-scan window showed different scene work and is excluded from the comparison.

Evidence: `build/cache-lru-host.log`, `build/profile-native_geometry_linear_scan.json`, `build/cache-lru-live-validation.json`.

All timing measurements use Azahar 2126.1 with the existing 300% CPU setting. They establish reduced work in this environment, not physical New 3DS speed. Stable 30 rendered FPS across gameplay remains unverified; 60 FPS remains the target. The original hardware Fountain freeze is not conclusively attributed.

## Release validation and installation

Candidate evidence is collected under `build/update7-qa/` with progress in `build/update7-qa.log`. Installation is recorded separately under `build/hardware/2026-09-10-geometry-cpu/sd-update.json`; the existence of this document or a package does not establish that the SD was updated.

The final candidate passed all 29 stages and all 25 character slots in one session. Minimum sampled ordinary-heap availability was 26,596,664 bytes. All observed decoded-geometry, native-geometry and texture allocations stayed below their 12/4/16 MiB limits. Every profiled native eviction used a single candidate lookup. Evidence: `build/update7-all-stage.json` and `build/update7-qa/summary.json`.

Actual-input cache checks passed 47,956 native vertex/index reuse comparisons, 551,305 old/new geometry-source comparisons, 155,674 fresh geometry decodes, 111,819 material comparisons and 921 LRU candidate/chain checks. A separate native-only pass retained ordinary decoded-cache behavior while validating reuse under Venom cache pressure.

Eight Peach pull/throw cycles retained visible original turnip bodies/faces, including face variants 0, 4 and 6 in this run. All eight layered GPU reference cases remained pixel-exact. Fox/Link Fountain completed twelve special-attack cycles and another 7,200 simulation updates with CPU Attack enabled and observed damage.

GPU point, cull, blend, raster-state and EFB-copy references passed with a 128 KiB command limit; combined command/vertex rollover also passed with a 512-vertex stream. The initial EFB test timed out because it required shadow copies while the performance setting disabled projected shadows. The test now temporarily enables those passes and restores the original setting in its cleanup. The game kept advancing during that test precondition failure; no production code was changed to resolve it. Both output modes passed, the uninterrupted audio capture had zero capture-window underruns, and 3,533,188 protected image bytes were unchanged. Completion evidence is in `build/update7-finish-qa.log`; the original interrupted test log is preserved.

Release packaging passed exact ELF-to-3DSX repacking, BE8 image checks, debug-fixture exclusion and the five optional visual-archive audits. Binary size: **3,883,988 bytes**. SHA-256: `c4cfaa61148dec0f2104b23d584d25711688fbf5167e8f34eb7d485347f01927`.

Installed and read-back verified at **2026-09-10 23:06:24 UTC**, under `SD:/3ds/melee`. Version 6, metadata, the returned log and existing optional visuals are backed up in `build/hardware/2026-09-10-geometry-cpu/sd-before-update/`. Original disc assets were unchanged. Matching release and smoke ELFs/maps, source snapshots, and QA evidence are archived under `release-update-7/`. The full local `dist/native-alpha/` package now carries the same version 7 executable.
