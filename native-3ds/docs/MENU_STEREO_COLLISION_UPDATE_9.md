# Menu stereo and collision update 9

This cumulative update increases slider-controlled stereo strength by 50%, adds depth to the original perspective menu models, places the character-select Ready to Fight ribbon in front of the screen, fixes portraits showing through closed character slots, and addresses reproduced distant hazard hits on Jungle Japes. It also reduces redundant GPU state work during stereo drawing. The user confirmed that version 8's physical 3D presentation works; version 9 still requires console validation. Stable 60 rendered FPS remains an open target.

## Display and character selection

The renderer and conservative mesh rejection share the same stereo-strength constant, increased from 0.008 to 0.012 pixels per slider unit. Fully lowering the slider retains the mono path. Both 4:3 and expanded 5:3 gameplay retain their existing proportions and controls.

Full-size perspective menu cameras now use their original eye-to-interest distance as the convergence plane. Orthographic text and HUD cameras remain flat. The CSS ribbon receives a separate convergence distance 1.5 times its camera distance during its original immediate draw callbacks. This moves it forward without changing its size, animation, hit areas, or game camera. Menu backgrounds retain their original modeled depth.

The closed-slot defect was reproduced with DK: the slot changed to closed, but the costume and emblem joints no longer had their hidden flags after the next animation update. The source overlay now enforces those flags after the original slot animation. The original reopening path restores the portrait. Controller-driven captures cover selected, closed, reopened and token-lifted states in both eye views.

## Jungle Japes and collision correctness

The returned SD executable matched version 8 exactly. Its log ended with normal application exit and did not identify individual hits. A live Marth/DK reproduction on Jungle Japes traced unexpected 30-damage hits to the original Klaptrap hazard while it was far away. The contact coordinates were nonfinite. This is stronger evidence than inferring a cause from the matchup or removing stage hazards.

Two problems were reproduced:

- The shared stage animation search uses a callback and `longjmp`. Its callback changed a non-volatile automatic pointer; optimized ARM code returned the saved initial null pointer after the jump. This prevented the stage from observing animation completion. The overlay makes that automatic pointer and callback access volatile. ARM disassembly verifies a load of the updated pointer. Jungle Japes now cycles its hazard state instead of remaining indefinitely in its active animation state. The shared correction applies to all stages using that helper.
- A fighter bone's cached matrix contained twelve NaNs despite finite current rotation, scale, translation and parent matrix. Collision position queries now detect the observed invalid cached matrix and rebuild it through the joint's actual class and hierarchy using its current pose. The capsule solver also rejects nonfinite results instead of accepting an unordered distance comparison as a hit. A bounded diagnostic records any remaining rejected contacts. This does not disable ordinary damage or stage hazards.

The exact operation that first creates the invalid cached matrix has not been conclusively identified. Recomputing the captured quaternion pose 1,000 times with poisoned output storage produced finite values matching an independent quaternion-matrix reference. The initial combat observation recorded 866 successful matrix repairs, zero failed repairs and zero invalid capsule contacts reaching the fallback.

The first full roster/stage sweep then exposed a broader case: 2,156 rebuild attempts failed, although the capsule guard accepted no invalid contacts. A captured Kirby pose had a chain of invalid parent matrices with finite SRT. `HSD_JObjSetupMatrix` rebuilds only dirty joints; rebuilding a child alone reused those stale parents. Recovery now walks the invalid ancestor chain and rebuilds it from the highest invalid parent downward through the original class methods, marking affected descendants dirty. The walk is bounded at 64 joints. The reproduced Kirby check subsequently recorded 51,384 ancestor rebuilds with zero failed recovery calls and zero rejected capsule contacts. The original failed trace is retained, and the final regression repeats the complete roster with per-stage checks. This is evidence for recovery of the observed failures, not proof that every possible transform defect is resolved.

The host regression executes the actual adapted collision routines: the captured distant/NaN hazard is rejected; 50,000 point/sphere cases agree with an independent double-precision distance oracle, including 15,723 hits; coincident endpoints produce finite output. The test intentionally uses an identity collision matrix and does not claim exhaustive capsule or inverse-matrix coverage.

The production ancestor-recovery walk also passes 20,000 poisoned transform hierarchies against clean cached matrices, byte-for-byte, with 378,928 ancestor rebuilds. Valid parents remain untouched; a nonfinite source and a chain beyond the 64-joint bound report failure safely. This host test models HSD's lazy class dispatch and dirty propagation; the live Kirby reproduction verifies the actual engine classes and matrices.

## Stereo submission performance

Previously each material started in the left eye, drew right, then restored the left viewport/scissor and stereo attribute. The new path starts in whichever eye is already selected, switches once, draws the other eye, and leaves that selection for the next material. Each eye still receives all materials in the original order. Unchanged stereo attributes also avoid redundant writes. Copies and clears explicitly establish their required viewport and zero shift; resource submission barriers retain both eye surfaces.

An old-order development switch supports an exact image comparison. Four half/full-slider and 4:3/expanded fixtures compared 768,000 output pixels with identical results. Twenty checks cover near, convergence, far, flat HUD and forward ribbon disparities. The maximum fixture disparities are 12, 0, -6, 0 and 6 pixels respectively. Conservative bounds checks cover 100,000 mono/stereo transforms, verifying both eyes before rejecting any mesh.

In alternating old/new same-scene samples of the final emulator build, the GPU-command CPU phase fell from 11.268/11.247 microseconds per sampled call to 10.065/10.080 microseconds, about 10.5%. This measures command setup, not total rendering time or physical console FPS. The returned version 8 log still includes expanded/full-stereo windows around 14–19 rendered FPS despite about 60 simulation updates per second. Simulation rate must not be presented as 60 FPS.

## Evidence and delivery

Returned files are preserved under `build/hardware/2026-09-10-update9/sd-returned-v8/`. Original failing and corrected observations remain under `build/update9-*.json` and their logs. Final regression, package validation and SD read-back evidence are recorded separately; an emulator result or a packaged file alone does not establish a physical installation or console performance.

Package: `dist/menu-stereo-collision-update-9.zip`. Original disc assets, five optional audited Diet stage archives, offline unlocks, tournament defaults, UCF, asynchronous loading and the turnip material correction are retained. HOME Menu packaging remains planned while `.3dsx` testing continues.

### Final regression results

The final development image completed one continuous full-stereo sweep of all 29 stage selections and all 25 character icons. Per-stage assertions found zero failed matrix repairs and zero invalid capsule contacts. Minimum sampled free ordinary heap was 24,420,040 bytes; peak sampled decoded geometry, native geometry and texture occupancy was 12,581,352, 4,194,232 and 16,776,352 bytes, respectively, within their existing budgets.

The same session then passed stereo depth and exact reference-image fixtures, layered material references, eight Peach turnip pulls in each of mono and stereo, fresh geometry-cache comparisons, and forced command, vertex-stream and texture-memory pressure. There were 434,640 live texture-content comparisons and 47 framebuffer conversions covering 7,156,160 pixels. Marth/DK CPU combat ran for 120 simulation seconds on each of Poké Floats and Jungle Japes. After both combat runs, cumulative counters were 121,609 matrix recovery calls, 102,762 ancestor rebuilds, zero failed recoveries and zero invalid contacts. These are simulation-duration and correctness checks, not physical FPS measurements.

The uninterrupted audio capture had zero underruns in its capture window and a 6.132 ms maximum mixer gap. All 3,552,940 checked code/read-only bytes remained unchanged. The QA success marker and detailed results are in `build/update9-final-qa.log` and `build/update9-qa/`.

The final production executable is 3,898,128 bytes, SHA-256 `9bb0ae9749d81810e96a45dfb498de7881759ad7dfcfbcfec67aca0113107b84`. Packaging verified 46,498 BE8 relocation words and excludes injected controls, emulator audio support, GPU fixtures and temporary matrix-capture instrumentation. Exact development/release symbols, source snapshots and regression evidence are retained with the hardware archive.

A fresh final-image boot also passed main/Versus stereo-menu captures and all four CSS states. Both eye views were visually inspected for the ready ribbon and closed portrait; closing removes the costume and emblem, and reopening restores them. The menu and CSS results are recorded in `build/update9-menu-depth.json` and `build/update9-css-final.json`.

Version 9 was installed and read-back verified at **2026-09-11 03:14:08 UTC** in `SD:/3ds/melee`, including matching executable, metadata and all five optional visual archives. Version 8's files were backed up locally, and the original disc archives were preserved. The installation report is `build/hardware/2026-09-10-update9/sd-update.json`. This confirms SD installation, not execution or frame rate on the console.
