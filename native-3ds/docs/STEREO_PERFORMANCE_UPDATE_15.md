# Pipelined stereo performance update 15

Update 15 targets shared rendering costs across gameplay, including casual
stages, items and attract battles. The targets remain 60 rendered FPS for
two-fighter diet-stage matches and at least 30 for crowded casual matches
with stereoscopic 3D. These are targets, not verified performance guarantees.

## Overlap simulation with GPU rendering

Previously, each frame submitted GPU commands and immediately waited for
completion before beginning the next game update. The renderer now defers
that wait until the next frame first needs GPU resources. Original game
simulation and animation can execute while the previous frame finishes.

The GPU still completes before command buffers, streaming vertices, native
geometry caches or texture allocations are reused. Framebuffer readbacks,
texture-copy ordering, memory-pressure barriers and empty-frame handling
retain their synchronization. The change does not skip simulation updates
or duplicate a rendered frame to increase the displayed FPS.

## Use shorter material programs

Constant-color materials take a short transform/color/texture-coordinate
path. Unlit vertex-color materials retain their affine material calculation
and skip the normal transform and four-light evaluation. Lit materials use
the existing lighting program. All paths share the same program binding and
uniform layout, and all stereo exits apply the same eye offset.

The CPU clip-space marker is tested before taking a shortcut, including in
batches that also contain vertices transformed by the GPU. This preserves
text, particles and other fallback geometry. Uniform state continues to be
updated across material changes; skipping those state updates was rejected
by the GPU comparison tests.

## Upload only referenced bone matrices

Decoded native geometry records the palette rows referenced by its vertices.
A draw now uploads changed rows only when that mesh uses them. Normal rows
are also omitted when lighting is disabled. The mask is cached alongside
immutable native vertices; dynamic geometry derives it from the actual
converted vertices. No bone, animation frame or gameplay calculation is
removed.

## Share growing geometry snapshots

Meshes commonly reference successively larger prefixes of the same vertex
array. Previously, each prefix could retain its own snapshot, causing the
same bytes to be compared repeatedly every rendered frame. Snapshots now
grow behind a stable, shared descriptor. Existing owners retain their
references, and the complete source still receives its normal byte check.

Growth pins the descriptor before allocating memory, so budget-driven cache
eviction cannot free it underneath the allocation. Allocation failure keeps
the existing uncached fallback. Snapshot storage stays within the same
12 MiB decoded-geometry budget. Changing bytes invalidate their cached
geometry normally; no sampled or partial content check replaces validation.

## Measurement and limits

The game log now separates time spent waiting for GPU queues from the duration
of each frame's final completed GPU queue. It also records material-path usage and palette upload
counts. These values will help distinguish CPU and GPU limits on physical
hardware. Existing render FPS and game-update-rate measurements remain.

Validation uses the real ARM build under Azahar, reference-path comparisons,
stereo GPU fixtures, buffer-pressure tests and original controller input.
Azahar CPU timings and accelerated GPU timings are not physical New 3DS
frame rates. Update 15 still needs console measurements against both targets.

In a fixed, paused four-fighter Temple encounter, three alternating reference
and optimized runs reduced median source bytes checked per rendered frame
from 1,007,079 to 258,873 (74.3%). Estimated geometry-lookup CPU time fell from
1.436 ms to 0.688 ms. Decoded geometry occupied 2,415,618 versus 1,665,102 bytes.
Both eye images were identical. These measurements isolate cache overhead;
they do not measure a continuous four-player match's console frame rate.

The host stress test extracted the production snapshot functions and passed
20,038 ownership/accounting checks, including 648 injected allocation
failures. Live ARM validation also compares cached geometry with fresh GX
decoding. Eight shader fixtures across both eyes compared 768,000 channels
with zero differences. Stereo, shield, layered-material, command-pressure,
pause/resume and four-fighter all-items checks cover their adjacent paths.

## Copied-model visibility compatibility

The attract-mode stress run exposed a freeze after Kirby copied Mr. Game &
Watch. The copied flat-model visibility table contains one group, while
Kirby's body contains two. The adjacent archive word is a relocated pointer.
On GameCube its high bit made that overread a negative, empty choice count;
on ARM the positive pointer became an enormous count and eventually an
unmapped read. The failing encounter was reproduced independently.

The overlay now copies the valid visibility group into a small per-player
table and explicitly zeroes unused body groups. The original hat table and
archive stay intact. There is no per-frame allocation or additional lookup
validation loop. The helper passes 864 combinations of player slot and group
count; the live reproduction observed the copied form in 63 snapshots and
continued to the next preview without the stall. The separate debugger
reproduction configures the next demo before asset loading; ordinary gameplay
regressions continue to use controller input.

Original fighter logic, collision, item behavior, stage hazards, RNG and
simulation cadence are unchanged by the performance changes and visibility
adaptation. Existing UCF settings and
audited optional Diet scenery remain. The update replaces the executable;
it does not replace disc assets or save files.

The queue ordering and shader implementation follow the pinned
[Citro3D renderer](https://github.com/devkitPro/citro3d/blob/9f21cf7b380ce6f9e01a0420f19f0763e5443ca7/source/renderqueue.c)
and the [Picasso assembler documentation](https://github.com/devkitPro/picasso/blob/master/Manual.md).
