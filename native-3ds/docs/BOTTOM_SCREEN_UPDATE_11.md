# Bottom-screen companion display — update 11

The touch screen now complements Melee instead of displaying a running console.
It uses the original SIS font, original character-select portraits, player-color
accents, dark blue panels, gold dividers and Melee's circle/cross motif.

## Using it

- Matches: portraits, character names, human/CPU indicators, damage and stock
  counts for up to four player slots. Two fighters use wide rows; three or four
  use a 2-by-2 grid. Eliminated fighters stay in place with an **OUT** indicator.
- Character selection: the live roster, costumes and closed slots, plus the
  selection/Start controls. Training has its own **Free Play** display. Stamina
  uses HP; time matches do not label zero stocks as elimination.
- Title, menus and stage selection: matching artwork, current rules and context
  hints. Results keep the last match snapshot after fighter objects are freed.
- Tap **FPS OFF** at the lower left to show rendered FPS; tap again to hide it.
  It starts hidden each launch. This is rendered FPS, not simulation frequency.
- Tap **VIEW: 4:3** to switch between original and expanded gameplay projection.
  The existing ZL + ZR + SELECT shortcut still works.
- Tap **CONTROLS** for the control guide, then **CLOSE GUIDE** to return. The
  guide does not pause gameplay and closes automatically when scenes change.

The original top-screen HUD remains available. The 3D slider still controls
top-screen depth. Physical control mapping is still player one; the display's
four-player support includes CPU opponents and does not add multiplayer input.

## Implementation

`port/engine/bottom.c` takes a read-only snapshot through a fixed 144-byte,
word-only endian bridge. It uses the original player/rules/timer APIs. The CSS
overlay exports persistent door fields without retaining animated object
pointers. The completed snapshot is frozen when the original match-result flag is set, before asynchronous teardown can clear player slots or rules. Scene tracking uses the original scene-info assignment, not a guess
based on local state-machine indices.

At startup the native side decodes all 118 costume portraits from the user's
`MnSlChr.usd` into a 1,546,272-byte cache. Sheik uses the original five costume
head portraits from `IfAll.usd`, since the CSS has only a Zelda portrait. These
are read once; changing characters causes no bottom-screen file reads, texture
uploads or per-frame allocation. Original disc archives remain unchanged.

The portable RGB565 CPU compositor reuses its background and precomputes sample
coordinates outside pixel loops. It draws only when visible values change and
presents at most once per bottom-screen VBlank, without a blocking VBlank wait
or extra GX/Citro3D draw calls. The guide ignores invisible damage changes.
Short touch presses are latched across both controller and frame HID scans.
Engine logs remain in `game.log`; an actual engine error can still show a
readable error screen.

## Validation

- `python tools/test_bottom_screen.py`: original-art snapshots at 320x240,
  guarded framebuffer bounds, exact touch-target boundaries, two/four fighters,
  eliminations, CSS, title, menus, stage selection, guide, Training, Stamina
  and Sheik. Requires the user's extracted disc, as does the normal build.
- `python tools/bottom_screen_test.py`: development-only Azahar test using
  controller input to reach four-player CSS and a real Versus match. Checks
  slot close/reopen, live stocks/damage, a real KO and all three touch controls.
  The touch request hook changes only native UI input, not game state.
- The complete four-player run reached Results with one survivor, retaining all four slots and the final rules through teardown. Training then passed Zelda-to-Sheik-to-Zelda transformation, both climbers as one player slot, and 1,200 CPU combat updates with observed damage.
  A sampled combat window took about 1.49 ms per redraw in Azahar, with 51
  redraws across 1,190 rendered frames: about 0.06 ms amortized per game frame.
  Emulator timings are not physical New 3DS performance measurements.
- Stereo GPU checks passed all 20 depth/HUD/ribbon comparisons and 768,000 exact eye-order pixels. Shield tests retained the previous colors. Live executable/constant-data verification found no changes to protected image bytes.

The private update contains an executable only. Existing SD assets suffice.
Development touch/capture/inspection hooks are rejected by the release packager.
Hardware validation of this new display remains pending; this update does not
establish stable 60 FPS gameplay.
