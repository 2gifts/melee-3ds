# group

Group play: Melee -> Group on the VS menu, a lobby of up to four nearby 3DS consoles over local wireless
(`port/3ds/localwireless`), with the vanilla four-door character select as the lobby. No account, no internet.

| File | What |
|---|---|
| `group_session.c/.h` | The session: the localwireless module carrying the Melee lobby and lockstep matches; the backend of the engine's online bridge while a group is open; settings (`group.ini`) |
| `group_lobby.c/.h` | The lobby rules, pure logic: four doors (Human / CPU / empty), who may edit which door, Start and the first stage pick, the wire format, and the match block |
| `group_menu.c/.h` | Melee -> Solo / Group, the Host / Join page, "End the session?", the bottom-screen strip and toasts |

Around it:
- `port/3ds/net_select.c` hands the engine's online bridge (`slippi_net_bridge.h`) to this backend while a group
  session is open, and to Slippi Direct otherwise. The Slippi code itself knows nothing about group play.
- `port/include/group_bridge.h` is the engine-facing API (`mp_platform_group_*`), `port/include/group_engine.h` and
  `port/engine/group/group.c` the engine side (scene flow, CPU takeover, name tags), and `tools/group_edits/` the
  patches to the decomp (VS menu, character select, stage select, name tags).
- From Slippi it reuses the lockstep frame driver (`port/engine/slippi/online.c`, through a few hooks), the online scene
  table (`online_mode.c`), the match block format and the port's platform layer (`slippi_plat.c`).

## Settings

`group.ini` in the data folder (`sdmc:/3ds/melee/slippi/` on the 3DS), all optional; see `group_session.h`.
`menu=1` turns Melee -> Solo / Group on; without it the VS menu is vanilla.

## Tests

`tools/group/`: `group_test.py` (PC consoles; runs `src/lobby_unit.c` first), `group_quad_test.py`, `group_menu_test.py`,
`group_lobby_test.py` (the emulated console hosts), `group_join_test.py` (it joins a PC host the test drives) and
`group_late_test.py` (it joins during a match), all in Azahar, reading the CSS from memory with `css_probe.py`;
`uds_smoke.py` (one emulated console on the real UDS link). See `docker/README.md`.
