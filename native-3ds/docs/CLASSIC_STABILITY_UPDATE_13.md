# Classic stability and cache update 13

This update fixes the saved Event Match crash, the reproduced Collect the
Trophies loading loop, and related Classic encounter-table errors. It also
reduces repeated geometry comparisons across the renderer. Centered 4:3,
expanded menus, native stereo, the bottom-screen dashboard and pause defaults
are retained.

## Stability fixes

The hardware dump matched the installed update 12 release image, including all
96 captured instruction bytes. It stopped at `mnEvent_8024E524+0x184`: a joint
lookup used invalid data because Event Match derived asset names and its fifth
asset destination from unrelated globals. The port now names each actual
archive section and destination explicitly.

Collect the Trophies reproduced an infinite retry in `Toy_80305058`: both the
selected trophy and every new candidate were `-1`. Initialization wrote through
a 12-byte header as though it were the original contiguous trophy context;
ARM placed the trophy flags elsewhere. One correctly sized shared allocation
now backs all views of that context. The three trophy archive/model/animation
tables are also referenced individually, without assuming linker order.

Expanded Classic testing found two more layout dependencies. The randomized
encounter order must follow the intro context at offset `0x20`, and matchup
data must be addressed directly rather than relative to the scene table.
The incorrect ARM addresses could select the wrong encounter or request an
invalid asset. The original matchup contents, selection algorithm, trophy
rules and round transitions are preserved.

The ten-fighter team introduction also requested a GameCube depth-texture
capture that the native renderer does not support. Its roster now uses the
existing color portrait sprites without per-pixel depth replacement. This
preserves the roster layout and timing while avoiding the load-time panic,
three depth captures and about 1.8 MB of depth buffers.

## Rendering work

Different meshes often index overlapping portions of one vertex array. The
cache now shares a containing array-prefix snapshot instead of independently
copying and comparing each index window. It still compares source bytes after
invalidation; changes invalidate all users of that snapshot. The capture ends
at a referenced array element, never at a rounded allocation boundary. The
12 MiB cache limit and uncached fallback are retained.

Geometry and material keys also use the existing checked ARM block comparator.
These cache changes apply throughout gameplay and menus without changing
simulation rate, fighters, collision or stage logic.

In alternating checks of the **same paused Giant encounter** with range
sharing off/on/off/on, source bytes compared per rendered frame fell from
about 766 KB to 407 KB (47%). Source comparison calls fell from about 654 to
278 (57%), and retained decoded-cache memory fell from 1.68 MB to 1.31 MB.
Sampled geometry lookup time fell from about 1.05 ms to 0.65–0.77 ms. Both
variants used the block comparator. This isolates renderer work with fixed
game state; it is **not a hardware frame-rate benchmark**.

## Validation and limits

Nine encounter fixtures passed: normal, team, targets, Giant, trophies,
ten-fighter team, Race to the Finish, metal and boss. The trophy timer also
advanced into the next encounter. The animated team battle passed 42,348
fresh geometry comparisons, 35,118 material comparisons and 1,614 native
geometry checks. Event Match passed three entry/scroll/page/exit cycles.
The widescreen CSS/stage-select path and three default Start pause/resume
cycles also passed in Versus.

`tools/event_menu_test.py` uses original controller input for repeated Event
Match entry, row scrolling, page changes and exit. Completed-event trophy
icons are optional on the fresh offline save; all nine labels must exist.

`tools/classic_scenario_test.py` uses Classic's existing debug starting-round
setting on character select to target encounter types. Subsequent loading,
opponents, gameplay and transitions run through the original engine. The
bonus completion check lets the original trophy timer expire; it does not
force a victory. `--validate-geometry` compares cached vertices, indices and
materials against fresh decoding while checking that fighters actually
animate. `--compare-geometry` benchmarks both snapshot modes in the same
paused encounter and restores normal play afterward.

`tools/test_classic_layout.py` checks both shared context layouts, all 752
original Classic matchup bytes, the linked trophy joint/animation name
tables, and original archive filename strings against the locally owned DOL.
Release packaging checks the executable against its exact ELF, validates BE8
relocation and rejects native development input/capture hooks.

These are host/emulator checks. Update 13 has not yet been measured on a
physical New 3DS. Stable 30 or 60 FPS across all gameplay scenarios, and full
single-player campaign compatibility, remain unverified. The original SD
assets are unchanged; only the executable and build metadata need updating.
