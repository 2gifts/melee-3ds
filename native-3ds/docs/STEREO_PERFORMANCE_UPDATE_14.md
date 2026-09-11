# Stereo rendering and attract stability update 14

Update 14 fixes the reproduced title/attract stop and reduces shared
rendering and joint-transform work. The changes apply across stages,
fighters, items, menus and both stereo eyes. It retains the original
fighter logic, item rules, match timing and simulation cadence while
correcting native-port math and collision-data compatibility bugs.
The existing UCF settings and optional scenery choices remain in place.

## Attract-mode stop

The reported screen is the port's explicit unsupported-THP-decoder stop,
not an ARM exception. The saved hardware log and emulator reproduction
both reach an optional movie after the simulated battles.

Movie playback now reports completion before opening the file, allocating
decode buffers or starting playback alarms. Original scene code performs
the transition and cleanup. The opening and ending handlers also bypass
their movie-specific waiting periods when no decoder is available.
The How to Play sequence returns to the title and later battles continue.

This is a graceful skip of optional prerecorded movies, **not an implemented
THP decoder**. Gameplay demonstrations still run in the original engine.

## Shared rendering work

The pinned Citro3D library marks a texture unit dirty on every bind, even
when the texture is unchanged or a unit is already disabled. Every such
update also clears PICA's texture cache. The port previously disabled the
second texture unit redundantly on many draws.

Bindings now compare the complete texture descriptor and current unit
pointer. Existing dirty bits are preserved. Frames, framebuffer copies
and texture uploads explicitly invalidate the binding snapshots, including
uploads into reused allocations. This avoids unnecessary cache clears
without treating a mutable texture pointer as immutable content.

Triangle draws also construct one batch containing the same 15 register
writes as the SDK. Primitive restart, draw-mode transitions and vertex-cache
flushes retain their original order. Context updates still run normally;
the packet is capacity-checked before submission. Other primitive types
keep the SDK path. A development-only switch generates both command
sequences and checks every word before submitting just one draw.

Joint SRT matrices reuse the existing libm sine/cosine results for identical
32-bit angles and floating-point control settings. The bounded cache uses
10 KiB, preserves signed zero and bypasses non-finite inputs. There is no
angle quantization, approximate trigonometry, matrix reassociation or
animation-rate reduction. Matrix construction retains its original formulas.

## Original PowerPC math

The non-PowerPC compatibility header mapped `__frsqrte(x)` to `sqrt(x)`.
Those operations are different: the original instruction estimates
`1 / sqrt(x)`. Melee refines that estimate with Newton iterations in many
fighter, collision, stage and particle routines. Starting those iterations
with a square root can make them diverge. An attract battle captured an Ice
Climbers recovery with a non-finite velocity and runaway rendering work.

The port now implements the PowerPC estimate using the integer algorithm
and 32-segment table from pinned Dolphin source, including subnormals,
signed zero, infinities and NaNs. Melee's existing refinement calculations
remain in place. Integer classification is kept separate from floating-point
optimization so ARM flush-to-zero mode cannot misclassify subnormal inputs.
See [Dolphin attribution](../port/engine/vendor/DOLPHIN-README.md).

The host comparison passed 1,065,593 exact comparisons with Dolphin and six
Newton-refinement cases. A separate ARM check passed all 57 pinned vectors
through the actual big-endian game ABI. To reproduce the host check, run
`python tools/test_ppc_math.py --fetch-reference`; downloads are pinned by
commit and verified against recorded SHA-256 hashes.

## Brinstar collision stability

Stress testing also caught invalid platform coordinates on Brinstar. The
recovered source treats two endpoint arrays and a bubble pool as one memory
block. ARM's section order separated them, so the stage read the wrong
bubble records and sometimes copied untouched stack values into its platform
vertices. The port now gives all views their original shared allocation,
with compile-time and linked-image checks for every offset. Columns that
have no active bubble receive the same uniform x-grid used by active bubbles;
the original endpoint updates, heights, stage animations and hazards remain.

Test reports distinguish selected-stage IDs (`StKind`) from archive/stage
module IDs (`GrKind`). For example, selected-stage 6 is Brinstar, 7 is
Corneria, and 14 is Temple.

## Validation and limits

The final attract regression passed 11 battles and repeated movie transitions,
including Brinstar and Ice Climbers encounters. More than 6.5 million batched
draw packets matched the linked SDK's command words exactly, and over 15
million live joint rotations matched the original libm results.

The ARM rotation self-test passed 16,960 comparisons against the original
libm, covering all rounding modes, flush-to-zero/default-NaN combinations,
signed zero, subnormals, extreme angles, infinities and NaNs. Development
builds can additionally compare every cached rotation used in actual scenes.

Stereo fixtures passed four modes, 20 depth/HUD/ribbon comparisons and
768,000 exact eye-order pixels. Original menu navigation, expanded CSS and
three default Start pause/resume cycles passed. Release packaging checks
the exact linked ELF, BE8 relocations and absence of native test hooks;
the Classic/trophy layout regression remains passing.

A controller-driven Temple scenario enabled all 31 item switches at Very
High frequency, selected four fighters and ran full stereo for 1,728
simulation frames. It observed seven item kinds and up to nine simultaneous
items without a stop. Stage observations verify both selected-stage ID 14
and archive/module ID 7, identifying Temple unambiguously.

Three alternating A/B pairs in the same paused Temple camera view produced
identical complete top-screen pixels. Actual texture-state updates fell
from about 259 to 69 per render (73%). Median sampled CPU time preparing
GPU commands fell from 1.327 ms to 1.117 ms (16%). This is the measured
command-preparation phase in that view, not a 16% overall FPS increase.

The buffer stress test forced 260 command-buffer barriers and 85 extra
vertex-stream barriers with 128 KiB/512-vertex limits. Point, culling,
blending, raster-state, I4/RGB565 framebuffer-copy, shield and layered-material
pixel checks passed. A final protected-image scan checked 3,591,116 bytes
with no modified code or read-only data.

These checks do not establish physical console frame rates. The reported
update 13 hardware log already confirms 804 MHz and L2 enabled; this update
targets remaining engine and GPU overhead. Stable 30 or 60 FPS in every
casual scenario, especially full stereo, remains unverified. Emulator CPU
phase comparisons must not be presented as New 3DS FPS measurements.

Only the executable and build metadata need updating. Existing game assets,
optional scenery, bottom-screen UI, centered 4:3 and expanded view controls
are retained.
