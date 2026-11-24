# localwireless

Game-independent local wireless lockstep for up to 4 consoles (3DS UDS). Plain C, no game headers; the host project
supplies `lw_platform.h` (time, log, random, socket init) and calls the API in `localwireless.h`.

| File | What |
|---|---|
| `localwireless.h` | Public API: open/poll, browsing and joining, the lobby (blob, commands, who is here), start blobs, per-frame submit and bundle |
| `lw_session.c` | The star protocol: the host's lobby and node-to-port table, host-built per-frame bundles, acks, keep-alive, the drop rule and CPU mask |
| `lw_link.h`, `lw_link_uds.c` | The UDS link (3DS only; stubs elsewhere) |
| `lw_link_udp.c` | Loopback UDP stand-in for PC tools and development builds (`use_udp`); left out when `LW_NO_UDP` is defined (release 3DS builds) |

A session is a lobby: the host is port 0, consoles join (the lowest free port) and leave at any time, and the host may
be alone. A console browsing for games sees each host's beacon blob and joins one by its key. The host starts each
game with a start blob; consoles that are present take part, later arrivals wait for the next game.

## Using it from a game: a "syncing provider"

The module moves bytes and decides who has dropped. Everything game-specific lives in a provider in the game's own code:

- the per-frame input format (any fixed size up to 16 bytes),
- the lobby blob and its commands, and the start blob the host builds from the lobby,
- applying a bundle (set every port's input) and turning a port whose CPU bit appears into an AI,
- an optional desync checksum.

The Melee provider is `native-3ds/port/3ds/group/` (`group_session.c`, `group_lobby.c`; the engine side is
`port/engine/group/group.c` and the lockstep driver in `port/engine/slippi/online.c`). A Smash 64 provider would
implement the same things for that game's inputs and state.

## Tests

`python tools/localwireless/lw_lobby_test.py` runs `lw_lobby_client` (the module and nothing else) over loopback:
a host alone, browsing between two hosts, and consoles joining, leaving and vanishing during games; every console that
stays must print the same bundle hash. `tools/group/group_test.py` does the same through the Melee provider, and
`tools/group/group_quad_test.py` runs the whole game in Azahar. Build the tools with `tools/group/build_tools.py`.
