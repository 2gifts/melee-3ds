# Gameplay performance update 2 — September 10, 2026

This iteration targets the user's approximately 12 FPS Fox-versus-Link match on Fountain of Dreams and its subsequent hard freeze. **Stable 30 FPS on the console remains unverified; 60 FPS remains the target.** The reported freeze has not been reproduced or assigned a proven cause.

## Hardware evidence

The SD executable matched performance/aspect update 1, SHA-256 `4e07761dc23af9e3113a4845e6d5fdf074baaec73b24c92c0e93d9fe5e0652d2`. Its log and binary are preserved in `build/hardware/2026-09-10-fox-link-freeze/`. There was no new Luma exception dump.

The retained gameplay samples show approximately 55,000 submitted vertices, 791–834 draw calls and 114–123 ms per rendered frame. Both geometry caches were at their budgets; about 13.6 MiB of linear memory remained. The log ends after frame 2100 and cannot identify the exact freezing instruction. A fresh emulator run of this earlier build completed the two-minute Fox/Link match without freezing.

## Changes

- Normal fighter rendering selects Melee's existing low-detail model group when present. Missing groups fall back to the regular model; metal and special visibility paths retain their original handling. Animation, hitboxes and simulation update frequency are unchanged by this selection.
- Fountain's decorative star draw and second scene render for the water reflection are disabled. The reflection resource starts with a valid matrix and static water texture. Platform movement and stage logic continue running.
- A separate `3ds/melee/visuals/GrIz.dat` supplies simplified Fountain scenery from **Diet Melee Classic, Linux patcher 1.0.2**, applied locally to the user's US 1.02 ISO. The optional file is 492,294 bytes versus the original 1,118,546. The original `files/GrIz.dat` is retained. Other Diet stages, executable modifications, mode removals and music changes are not imported.
- File IDs retain the selected path and its matching size across reader eviction/reopening. Missing or truncated optional visuals fall back to the original archive.
- The log worker flushes pending text at least every two seconds while it can run. It can preempt a busy game and records a frame/phase checkpoint after ten seconds without progress. It does not reset or skip simulation. This should improve evidence from a game-thread freeze; it cannot guarantee logging through an OS or SD-service lockup.
- Default centered 4:3 and the ZL + ZR + SELECT expanded-world toggle are retained.

## Diet source and gameplay audit

The official [Diet Melee site](https://diet.melee.tv/) describes its lower-detail models. Its [changelog](https://diet.melee.tv/changelog/) specifically documents removed Fountain stars and other stage simplifications. The linked Linux patcher's script identifies itself as 1.0.2, even though the website advertises a later release. Only the independently checked Fountain archive is used here.

The [official downloads page](https://diet.melee.tv/download/) supplied `DietMeleeLinuxPatcher.tar.gz`, SHA-256 `3f27e034447aa9cdf93b401d78dc5c8f802cb504d416370e55dfb600b2af0a31`. Its bundled scripts and executable were not run. The Classic xdelta patch was applied with xdelta3 3.2.0 from the [upstream xdelta release](https://github.com/jmacd/xdelta/releases/tag/v3.2.0). All inputs and patched output remain local.

`tools/audit_diet_fountain.py` compares relocation-independent contents against the original archive: collision fields and all vertex/line/joint arrays, ground and stage parameters, moving-platform parameters, collision bindings, and interactive skeleton transforms/animation curves match. The explicit visual exceptions are the removed model-1 decorative animation and model-3 decorative siblings after index 11. The stage's collision bindings and interactive joint lookups use indices at most 6. This is a data and source audit, not a multiplayer determinism certification.

The accepted visual SHA-256 is `913134d58c804f44b9fe3dc41b981e50a076e6b27c3c58583fa74095ce22f61d`. `tools/prepare_diet_fountain.py` extracts only that revision from a locally patched ISO and reruns the audit. The ordinary asset extractor still requires the original validated Melee DOL.

## Emulator measurements

Azahar 2126.1, New 3DS mode, experimental **300% emulated CPU clock**. These are guest timing estimates under that configuration, not console benchmarks or host-wall-clock FPS. Rendering and simulation rates are recorded separately, and profiling requires the same gameplay scene at both endpoints.

The first three comparisons used the same live Fox/Link Training session with the original stage archive:

| Rendering configuration | Render FPS |
|---|---:|
| Previous visuals, repeated baseline | 34.91 / 34.64 |
| Low-detail fighters | 41.19 |
| Low-detail fighters, no stars | 44.36 |
| Above plus static reflection, repeated | 49.75 / 51.91 |
| Final build with simplified Fountain, separate fresh session | 59.59 |

The original baseline simulation ran near 59–60 Hz in this emulator configuration. The final sample rendered 1,465 frames over 24.58 guest seconds, with 1,469 simulation updates (59.76 Hz). This average does not establish a minimum frame rate. The final stage typically submits roughly 4,700–4,900 vertices and 283–318 draws in idle Training samples; active combat varies. Native cached geometry is around 1 MiB and free linear memory around 20 MiB. Scenery is visibly simpler, including a mostly empty background.

Evidence: `build/gameplay-quality-profile.json`, `build/fox-link-baseline-profile.json`, `build/diet-fountain-profile.json`, `build/gameplay-final-profile.json`, `build/gameplay-final-training-game.log`, and `build/diet-fountain.png`.

The final full Fox/Link rematch supplies 119 one-second rate samples from active gameplay: median 60.0 render FPS, range 48.2–60.9, median 60.0 simulation Hz. Its 60-frame geometry samples have a median of 4,737 vertices and 269 draws. The rate readout can briefly exceed 60 because its windows are not perfectly aligned with the simulation clock. This is still the 300% emulator configuration and is not stable 60 FPS. See `build/gameplay-rematch-performance.json`.

## Validation

- An original-stage two-minute Fox/Link Versus match completed; a separate CPU Attack test demonstrated damage. Twelve side-special/shine/laser input cycles observed Fox's actual special states without a freeze.
- The simplified stage sustained 3,600 original CPU Attack simulation updates, with observed damage.
- The final build compared 48,086 cached geometry draws and 44,937 material evaluations with fresh decoding, without mismatch. Fox effect cleanup removed 19 particles without a crash.
- A deliberately induced 12-second development-only CPU stall produced a persisted checkpoint (`frame=10447 phase=1`), and rendering recovered. That fault-injection hook is absent from the release.
- The uninterrupted audio capture had zero capture-window underruns, a 12.64 ms maximum mixer gap, and audible nonzero stereo output. Earlier cumulative underruns include intentional debugger pauses and the injected stall.
- 4:3 → expanded → 4:3 passed actual framebuffer checks; the raw camera FOV/aspect remained unchanged. All 3,504,324 protected code/read-only bytes checked after gameplay were unchanged.
- The final release's menu strings/tables match the original DOL. All 46,376 BE8 fixup words and the packaged loaded segments passed validation. Emulator controls, capture buffers and the stall injection are absent.
- Two Fox/Link Fountain matches completed and returned to CSS in one final-build session. The second was observed from timer 120 through expiration and Results. Peach/Pikachu then completed a match on the unmodified Yoshi's Story archive; both simpler fighter models were visually inspected. Its active profile averaged 58.08 rendered FPS and 59.69 simulation Hz in the same emulator configuration. After all three matches and returns, 3,504,324 protected bytes were still unchanged.

Live test records use the `build/gameplay-final-*` prefix; CPU and special-cycle records also include `build/diet-fountain-cpu-attack.log` and `build/fox-link-side-special-stress.json`. Match/rematch results and SD installation are recorded in `docs/HARDWARE_TESTS.md` after completion.

## Package and rollback

`dist/gameplay-performance-update.zip` contains the new executable, the optional local Fountain archive, build metadata and instructions. Executable SHA-256: `e5dafac9bf114b1908503107987abe8ca4ce87f71c26b0a5bbdd6de8a8a9f59a` (3,862,508 bytes).

`tools/update_sd_gameplay.ps1` verifies the previous executable and original Fountain archive, backs up the old files, stages each new file with a durable flush, and verifies read-back hashes. To restore original Fountain scenery later, remove or rename only `/3ds/melee/visuals/GrIz.dat` with the game closed. The default low-detail fighters and reduced reflection remain in the executable.

Installed and read-back verified on `SD:/3ds/melee` at 18:14:44 UTC. `build/hardware/2026-09-10-fox-link-freeze/sd-update.json` records the executable and visual hashes; `sd-before-update/` preserves the previous files and `deployed-gameplay-2/` preserves the exact release ELFs, map, binary and metadata. Original disc assets were unchanged.

HOME Menu icon/CIA packaging remains planned after these `.3dsx` gameplay iterations stabilize. Saving and multiplayer remain unfinished.
