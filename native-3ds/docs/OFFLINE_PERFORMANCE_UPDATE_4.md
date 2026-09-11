# Offline and performance update 4

This update targets the entire renderer, adds offline tournament defaults and native UCF input fixes, and expands the optional Diet scenery. Console speed must still be measured on the New 3DS. The user's latest hardware result was approximately 30 rendered FPS on Diet Fountain and 12 FPS on Yoshi's Story with update 3; the SD log confirms 804 MHz and L2 enabled.

## Changes across all stages

- Cached meshes with one transform now have conservative local bounds. A homogeneous clip-space test rejects meshes entirely outside the visible left/right/top/bottom planes before texture lookup, native vertex upload and GPU submission. Mixed-matrix meshes retain the normal path. Bounds follow the live camera and expanded-view projection and include a rounding margin; intersecting geometry stays visible.
- Projected fighter shadows are disabled by default. This removes the additional fighter render/copy pass and per-surface projection work on every stage. Fighter models, ordinary lighting, collision, animation and game updates continue normally.
- Existing low-detail fighter selection, GPU command guards, bounded texture/geometry caches and New 3DS CPU/cache settings remain enabled globally.

Controlled emulator comparisons on the same original Yoshi's Story scene reduced estimated submitted draws from about 402 to 239 with bounds rejection, then to about 184 with projected shadows disabled. The prepared Diet Yoshi scene submitted approximately 118 draws per rendered frame. These are sampled draw-call measurements, not a claim of console FPS. Evidence: `build/profile-mp_geometry_cull.json`, `build/profile-mp_performance_no_shadows.json`, `build/update4-yoshi-detail.json`.

## Optional scenery

The package contains Fountain of Dreams, Yoshi's Story, Battlefield, Final Destination and Dream Land. The shared renderer changes also apply to all remaining stages, which retain their original archives.

The preparation tools compare original collision arrays, stage settings, gameplay parameters, joint bindings, interactive skeletons and relevant joint animations. Yoshi retains Randall's original path and timing and the interactive Shy Guy skeleton; its original stage settings are restored. Final Destination's original joint animation data is restored alongside Diet geometry, including all 66 timing sources queried by its animation-wait logic. Decorative material animation remains simplified. High-detail meshes in the appended original data are not attached for rendering.

The official Diet Classic 1.0.3 patch was downloaded, verified and applied locally to the user's original ISO. Its six stage archives are byte-identical to 1.0.2; its executable removes two Stadium callback no-ops that caused a rare gameplay desync. This native port retains those original callbacks and does not load Diet's executable. Stadium's archive also has linked transformation assets and removed display resources; this release keeps the original Stadium archive pending a dedicated conversion of those dependencies.

Prepared files live separately under `/3ds/melee/visuals/`. Original `/3ds/melee/files/` archives are unchanged. Renaming an optional file while the game is closed restores that stage's original scenery. Size/header validation rejects absent or malformed replacements and uses the original file.

## Offline defaults and UCF

On launch, all characters/stages and More Rules are unlocked. Versus defaults to 4 stocks, 8 minutes, items off, normal damage, handicap off, team attack on and pause off. Random stage selection uses the six singles stages. Normal menu changes work for the current session. No-card prompts are skipped. This is an in-memory unlocked configuration, not memory-card save support; settings reset on restart.

Native equivalents of the UCF 0.84 algorithms cover raw pad history, cardinal normalization, dashback and Nana correction, SDI, shield SDI, tumble input, shield drops and the crouch fix. They operate on simulation-frame input and the original fighter state. No rollback, matchmaking or online service is included. The 3DS still has digital shoulder buttons and its own stick hardware.

## Additional freeze fixed

A controller-driven Yoshi-to-Final Destination transition exposed a repeatable empty busy-wait in `HSD_Synth_8038B5AC`: an outstanding music transfer could only complete through a callback that the cooperative port was not servicing. CPU samples consistently identified this wait. It now pumps the pending DVD/ARAM callbacks while preventing nested mixer polling. It does not force the transfer lock open or skip game state. The same transition subsequently completed.

This is a reproduced and fixed loading freeze. Without a matching hardware dump it cannot establish the cause of the older user-reported Fountain freeze.

The full-stage screenshot review also exposed Captain Falcon stock icons appearing for other fighters. The recovered `gm_80168BF8` omitted its float return, so ARM optimized away the character-frame lookup. Its overlay now returns the lookup result, matching the verified original PPC call and epilogue. All 396 character/form/costume cases pass through both the mapping and the actual wrapper.

Venom's initialization also relied on the original GameCube linker placing several independent globals next to each other. The ARM build could call the callback table itself as executable code. Its callback, Arwing state, spawn-vector and animation lookups now use named allocations. Six tables and all 12 spawn vectors are checked against the user's original executable. The same stage now loads through normal controller input. Debug evidence is saved in `build/venom-sanitized-exception.log`; the distributed executable has no null-sanitizer instrumentation.

A return-value audit covered 984 upstream translation units. Two additional used results now match the original PPC behavior: Icicle Mountain's segment mover returns its scrolling displacement, and Birdo's egg animation callback returns the common item-cleanup result. Host checks cover all segment-presence combinations with positive, negative and zero displacement, plus both item-cleanup outcomes. Other audit diagnostics include recovered functions with deliberately inaccurate return types and unused results; the audit is not a claim that all source-level undefined behavior has been removed.

## Validation and limits

- 100,000 transformed-bound cases checked against an independent eight-corner reference; invalid and mixed-transform bounds stay on the normal path.
- 39,361 live cached-geometry comparisons against fresh decoding and 33,718 material checks passed with animated Yoshi gameplay.
- UCF tests cover 65,536 raw-stick pairs plus production helper cases for thresholds, history, cardinal normalization, Nana and action gates.
- Actual ARM compilation checks the UCF controller, stick-history, timer, animation-frame and Nana-buffer field offsets against the original ABI.
- Live original-engine save/rule fields confirm both unlock masks, all More Rules flags, 4-stock/8-minute settings, no items and the six-stage random mask. Input hooks execute during controller-driven play; the scripted sequence did not establish every UCF correction on physical hardware.
- 2,048 deferred music-transfer chains verify completion with nested mixer attempts suppressed and correct restoration of polling state.
- Optional-file tests cover replacement selection, fallback, reader eviction and mixed reads. Release checks verify the BE8 image, repackaged executable and absence of smoke input/capture fixtures.

The controller-driven sweep passed all 29 selectable stages and all 25 character-selection slots (27 observed fighter kinds, including Nana and Sheik). It recorded 22,324 uninterrupted simulation updates during its measurement windows, bounded caches and zero GPU command-buffer barriers while exercising 2,943 texture evictions across scene changes. Records and captures are indexed by `build/all-stage-combined.json`. The final Icicle Mountain and Birdo-return changes receive separate stage retests after this sweep.

These are bounded compatibility checks, not exhaustive testing of every move, item or stage transformation. New console FPS, stability during long sessions and analog feel remain hardware validation items. Stable 30 FPS is the immediate target; 60 FPS remains the longer-term goal.

Final-build follow-up checks passed: one simulated minute each on Icicle Mountain and Mushroom Kingdom II; 60 seconds of Fox/Link CPU combat on Venom; 30 seconds on Diet Yoshi's Story; live unlocked/default rule fields; 4:3 → expanded → 4:3 framebuffer checks; and a regular 4-stock/8-minute Versus match through results and back to character selection. The match ended by stocks with 6:19 remaining. The uninterrupted NDSP capture had zero underruns within its measurement window. All 3,512,212 inspected protected code/constant bytes matched the final smoke ELF. The console release excludes the smoke controls and sanitizer handler.

Release executable SHA-256: `13fbd9a398d59931b1d5c0ea1bac3e0c9658d94590d357693abebbbb742b7496`. Exact release ELF/map and metadata are retained under `build/hardware/2026-09-10-offline-qol/deployed-update-4` for future console fault decoding.

Installed to `SD:/3ds/melee` on September 10, 2026 at 21:02 UTC. The executable, five optional scenery files and build metadata were flushed and verified by readback hashes. Existing files were backed up locally under `build/hardware/2026-09-10-offline-qol/sd-before-update`; original disc archives were preserved. The installation record is `build/hardware/2026-09-10-offline-qol/sd-update.json`.

## Sources

- [Diet Melee download](https://diet.melee.tv/download/) and [1.0.3 changelog](https://diet.melee.tv/changelog/).
- [UCF source](https://github.com/AltimorTASDK/ucf), commit `5634468e00c2b43e8c9c402970caf562c15a0d0d`.
- [Slippi assembly integration](https://github.com/project-slippi/slippi-ssbm-asm), commit `fcf47f10dc244152c2ebaa3a9dec142ea42243b7`.
- [Dolphin's CPU-side culling work](https://us.dolphin-emu.org/blog/2023/02/12/dolphin-progress-report-december-2022-january-2023/) informed the shared draw-rejection approach; this port uses its own PICA200 implementation.
