# Shield color update 10

Update 10 restores the original player tint on shields. The user reported a
black Player 1 shield in otherwise playable update 9 matches. A controller-driven
Marth shield reproduced the issue: the original effect received RGB `F25959`,
but the renderer replaced the texture with white while evaluating its material,
then multiplied the result by that texture. This discarded the red tint and
left a grayscale shield.

The renderer now recognizes the original four-stage texture-interpolation
programs by their operations, registers and inputs. It does not override player
colors or change fighter, shield, collision or input behavior. A held shield
interpolates between its original color endpoints in one native GPU combiner
stage. The startup effect also blends its second image with independent UVs.
Animated opacity and crossfade values remain live. Unmatched materials retain
their existing path, including Peach's two-texture turnip material.

Held shields retain the decoded and native geometry caches. Their pixel work
uses one GPU stage, as before; the startup flash uses three stages within the
same draw. This fix does not establish a new hardware frame-rate result.

Validation for this change includes:

- 800,000 channel comparisons against the captured original GX programs,
  randomized colors/registers/texture samples and 168 rejected program mutations.
  Parameter quantization stays within two 8-bit channel units.
- 72 GPU/reference image pairs covering all six shield palette colors, held and
  startup materials, three crossfade weights, two opacities, RGBA/intensity alpha,
  independent UVs and texture eviction pressure. The GPU tolerance is three
  8-bit channel units.
- Real P1 shields captured through L input in 2D and both 3D eyes, including
  startup and held states. No fighter or asset memory is changed by the test.
- The existing turnip material, stereo pixel reference, actual turnip pulls,
  geometry cache comparisons, CPU combat and protected-image checks.
- Release BE8 image validation and exclusion of the development GPU fixtures
  and simulated-controller hooks.

The portable, asset-free host regression is `python tools/test_shield_material.py`.
It includes only the small captured GPU state programs under `tests/fixtures/`;
textures, game archives and screenshots are not source-test inputs. The running
development build supports `tools/shield_gpu_test.py` and `tools/update10_qa.py`.
Local results are preserved under `build/update10-qa/`.

The private update package is `dist/shield-color-update-10.zip`. Replace only
`3ds/melee/melee.3dsx`; existing disc files and optional Diet scenery remain in
place. The startup log identifies `shield color update 10`.

The user reports update 9 as very playable after several matches. Its returned
log ends normally, with some full-stereo gameplay windows near 35 rendered FPS
and 60 simulation updates per second. These are separate measurements; stable
60 rendered FPS remains an open goal. Update 10 still needs physical validation.
