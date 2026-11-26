# group

Group play: Melee -> Group on the VS menu, a lobby of up to four nearby 3DS consoles over local wireless
(`port/3ds/localwireless`), with the vanilla four-door character select as the lobby. No account, no internet.

| File | What |
|---|---|
| `group_session.c/.h` | The session: the localwireless module carrying the Melee lobby and lockstep matches; the backend of the engine's online bridge while a group is open; settings (`group.ini`) |
| `group_lobby.c/.h` | The lobby rules, pure logic: four doors (Human / CPU / empty), who may edit which door, Start and the first stage pick, the results screen's ready bits, the stars, the wire format, and the match block |
| `group_menu.c/.h` | Melee -> Solo / Group, the Host / Join page, "End the session?", the bottom-screen strip and toasts |

Around it:
- `port/3ds/net_select.c` hands the engine's online bridge (`slippi_net_bridge.h`) to this backend while a group
  session is open, and to Slippi Direct otherwise. The Slippi code itself knows nothing about group play.
- `port/include/group_bridge.h` is the engine-facing API (`mp_platform_group_*`), `port/include/group_engine.h` and
  `port/engine/group/group.c` the engine side (scene flow, CPU takeover, name tags, the CSS / stage select camera turned by every console's C-stick, the results screen,
  stars and auto handicap after a match), and `tools/group_edits/` the patches to the decomp (VS menu, character
  select, stage select, results screen, name tags).
- After a match (or its sudden death) every console shows vanilla's results screen. Presses that move it on (Start,
  and the press that leaves its intro) go to the host and come back through the lobby, so every screen takes the same
  steps and ends once every Human player is ready; a console that left counts as ready. Then the host counts the stars
  and reopens the lobby. Vanilla's own results exit is not run: it would record remote fighters in this console's save
  and can start a "new challenger" fight or a trophy depending on the save.
- While a session is open (browsing, the lobby, a match, its results), HOME and sleep are off (`group_system_frame`,
  called each frame from `port/3ds/game.c`); a refused HOME press shows "HOME IS UNAVAILABLE DURING WIRELESS PLAY" on
  the bottom screen for 2 s. A lost session reopens the Host / Join page with its reason (the host left, stopped
  responding, ...).
- A door in use on one console (its slider, name tag list, name entry or held token) cannot be switched between Human,
  CPU and closed from another (the busy command carries a door mask; the lobby's `in_use`), and each door edit carries
  the kind it was made from, so the host drops edits from an older view.
- From Slippi it reuses the lockstep frame driver (`port/engine/slippi/online.c`, through a few hooks), the online scene
  table (`online_mode.c`), the match block format and the port's platform layer (`slippi_plat.c`).

## Settings

`group.ini` in the data folder (`sdmc:/3ds/melee/slippi/` on the 3DS), all optional; see `group_session.h`.
Melee -> Solo / Group is on by default; `menu=0` makes the VS menu vanilla. Development (smoke) builds
default to off, so their older emulator tests keep vanilla VS; the group tests write `menu=1`.

Build switch: `python tools/build_game.py ... --no-door-names` leaves out the console names under the
character select doors (shown by default; the bottom-screen strip shows names and signal bars either way).

## Tests

Three tiers, fastest first. Run them in that order: a failure in an earlier one makes the later ones meaningless.

1. **Host C, seconds:** `python3 tools/test_group.py`. Every pure rule, built with AddressSanitizer and
   UndefinedBehaviorSanitizer by the system compiler: the character select (`port/engine/group/css_rules.c`:
   which pad a cursor reads, doors in use, busy, READY, the read-only character select while watching, the door sync
   with the lobby, auto handicap, the host's rules, the stars, Start and Back), the stage select (`sss_rules.c`: who
   has a cursor, Random from a joiner, a pick shown, locked or unknown here, Back, a lost session), the results screen
   (`results_rules.c`: presses kept and replayed, ready, stars, auto handicap, names), Melee -> Solo / Group
   (`group_menu.c` over a fake session: the pages, Host, Join, a full game, "End the session?", a lost session's
   note, HOME and sleep, the toasts), the lobby the host keeps (`group_lobby.c`) and vanilla's auto handicap rule (from
   the decomp's `gm_1601.c`, when it is there). The tests are `tests/group_*_tests.c` and
   `tests/auto_handicap_tests.c`.
2. **PC consoles, minutes:** `docker/emu-test.sh run tools/group/run_pc_suite.py`. The real session, lobby and link
   code built for the PC (`tools/group/src/group_client.c`), several consoles over the loopback link, every scenario of
   `group_test.py` (lobby, matches, drops, late joiners, results, leaving, a lost session, wireless off, rejoins, a slow
   link thread), the localwireless module alone (`tools/localwireless/lw_lobby_test.py`) and the menu art over the
   disc's archives (`menu_art_test.py`, `tests/menu_art_tests.c`); several at once.
3. **The emulator, minutes:** `python3 tools/group/run_azahar_suite.py` (on the host, next to Docker). Only what needs
   the real game on an emulated 3DS, several at once (at most 6 emulators; the frame-time tests alone):
   - `group_smoke_test.py`: one session of two emulated consoles and a PC one through the glue between the scenes and
     the rules: the Solo / Group submenu, "Choose a game.", HOME and sleep and the refused-HOME notice, a door in use
     on the other console, the camera following the other console's C-stick, a tied match and its sudden death, the
     names on the results, the saves after a match, "End the session?" and the joiner's notice, the "Match in progress"
     page, the soft reset;
   - `group_lobby_test.py`: the emulated console hosts a PC console through a whole match;
   - `group_picker_test.py`: Turns; the picker, who also pressed Start, leaves during the stage select: the stage
     select goes on and the cursor appears on the console the pick passes to;
   - `group_quad_test.py`: identical records across 2 to 4 emulated consoles, a CPU taking over a dropped one (one that
     had paused the match: the others unpause for it);
   - `group_mii_test.py`: Mii heads (cfl-mii loads, renders without stalling the engine, unloads);
   - `group_scan_test.py`: the real UDS scan does not stall the engine;
   - `group_link_test.py`: a slow link thread (open, close, join) does not stall the engine.

The Azahar tests read the emulated console's memory through the GDB stub (`emu_group.py`, `css_probe.py`).
`uds_smoke.py`: one emulated console on the real UDS link. See `docker/README.md`.
