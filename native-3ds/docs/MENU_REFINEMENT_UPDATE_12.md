# Menu refinement — update 12

Update 12 fixes menu stack corruption, adds guidance specific to each menu,
extends the expanded projection to menu models and enables Start-to-pause
by default. It retains the update 11 bottom-screen dashboard.

## Controls and presentation

- Start pauses and resumes a regular Versus match. This is an offline-port
  default; **VS Mode → Rules → More Rules → Pause** can still change it.
- **VIEW** on the bottom screen switches between centered 4:3 and expanded
  5:3, both in gameplay and in perspective menus. The ZL + ZR + SELECT
  shortcut also works. 4:3 remains the startup default.
- Expanded view reveals additional scene area at the sides. It does not
  stretch portraits, menu buttons or text, move cursor hit areas, or alter
  the game camera's tracking. Flat overlays retain their original centered
  placement. Small offscreen cameras retain their original projection.
- Menu guidance now describes the actual screen: solo modes, Stadium,
  Special Melee, rules, items, stage pools, sound, names and records each
  have their own advice. Versus settings are shown only where relevant.
  Training stage selection no longer displays the unrelated Versus stock
  and timer settings. Character selection says **START: NEXT** because it
  leads to another setup screen.
- FPS still starts hidden. The touch controls guide does not itself pause
  a running match. Settings still reset when the app closes.

The companion screen continues to redraw only when its visible state
changes. These changes add no portrait decoding during play, no additional
per-frame GPU passes, and no new gameplay cache allocations.

## Crash diagnosis

The saved ARM11 dump was verified against the exact installed update 11
release ELF: all 96 captured code bytes matched. It stopped at
`HSD_GObj_80390CFC+0x15c`, reading `proc->next` through a zeroed saved `r4`.

`fn_8022AFEC` declared a four-pointer `sp20` scratch array but wrote one
entry for every menu option. Main, 1-P and VS menus have five entries,
Options has six, and Special Melee has ten. In the release ARM code,
`sp20` began at `sp+4`; the fifth write landed at `sp+20`, directly on the
saved `r4`. Padding in the original PowerPC layout had masked the error.

A related seven-pointer array in `mn_8022A5D0` also received all ten
Special Melee entries. Both arrays now derive their capacity from the
original ten-entry option-joint table. No callbacks are skipped and no
menu entries are disabled. The changes are source overlays; the pinned
upstream checkout remains untouched.

## Validation

`tools/menu_refinement_test.py` drives three rounds of real menu input
through Main, 1-P, Regular Match, Stadium, VS, Special Melee, Options and
Data, including hovering every Special Melee entry. It also captures both
projection widths. `tools/menu_display_pause_test.py` checks character
and stage selection with expanded stereo, then pauses and resumes a live
Versus match three times. The paused match timer and match-frame counter
must remain fixed while native display frames continue.

Portable compositor tests render all 34 menu IDs and the existing match,
CSS and Training fixtures with framebuffer guards. Original font/menu
table bytes are checked against the locally owned US 1.02 DOL. Stereo and
shield GPU fixture tests and a protected-image comparison cover existing
renderer behavior. Release packaging verifies BE8 relocation, matches the
3DSX to its exact ELF and rejects development input/capture hooks.

These are host and emulator checks. Update 12 still needs physical New 3DS
validation; emulator speed does not establish a hardware frame rate.
Full single-player campaign compatibility and stable physical 60 FPS
remain unfinished work.
