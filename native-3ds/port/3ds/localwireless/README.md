# localwireless

Game-independent local wireless lockstep for 2 to 4 consoles (3DS UDS). Plain C, no game headers; the host project
supplies `lw_platform.h` (time, log, random, socket init) and calls the API in `localwireless.h`.

| File | What |
|---|---|
| `localwireless.h` | Public API: open/poll, roster, setup and start blobs, per-frame submit and bundle, drop handling |
| `lw_session.c` | The star protocol: host-built per-frame bundles, acks, keep-alive, 2 s drop rule, CPU mask |
| `lw_link.h`, `lw_link_uds.c` | The UDS link (3DS only; stubs elsewhere) |
| `lw_link_udp.c` | Loopback UDP stand-in for PC tools and development builds (`use_udp`) |

## Using it from a game: a "syncing provider"

The module moves bytes and decides who has dropped. Everything game-specific lives in a provider in the game's own code:

- the per-frame input format (any fixed size up to 16 bytes),
- the setup blob each console sends before the match, and the start blob the host builds from them,
- applying a bundle (set every player's input) and turning a player whose CPU bit appears into an AI,
- an optional desync checksum.

The Melee provider is `native-3ds/port/3ds/slippi/slippi_local.c` (+ the match block in `slippi_bridge.c`, the engine side in
`port/engine/slippi/online.c`). A Smash 64 provider would implement the same four things for that game's inputs and state.

## Tests

`python tools/localwireless/lw_test.py --players 4 --drop 50 --crash 3:200` runs `lw_client` (the module and nothing else) over
loopback; every console that stays must print the same bundle hash. `tools/slippi/net_test.py local-quad` does the same
through the Slippi provider, and `tools/slippi/online_quad_test.py` runs the whole game in Azahar.
