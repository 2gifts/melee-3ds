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

`tools/group/`: `group_test.py` (PC consoles; runs `src/lobby_unit.c` first), `group_quad_test.py`, `group_menu_test.py`,
`group_lobby_test.py` (the emulated console hosts), `group_join_test.py` (it joins a PC host the test drives) and
`group_late_test.py` (it joins during a match) and `group_door_test.py` (doors in use), all in Azahar, reading the CSS from memory with `css_probe.py`;
`uds_smoke.py` (one emulated console on the real UDS link). See `docker/README.md`.
