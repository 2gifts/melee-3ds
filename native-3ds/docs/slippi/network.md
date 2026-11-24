# Slippi Direct networking on the 3DS

The 3DS side of a Slippi **Direct** (connect code) 1v1 against stock Slippi Dolphin:
sockets, ENet, matchmaking and the Slippi P2P netplay protocol, plus local test tools.
The wire formats come from Slippi's own sources (see `protocol_notes.md`). Melee
Unlocked's port of them (GPL-3.0-or-later) was used as a cross-reference.

## Layout

| Path | What |
|---|---|
| `port/3ds/slippi/enet/` | ENet 1.3.13 as vendored by Slippi Ishiiruka (MIT, `LICENSE`). Adds `ctru.c`, the 3DS platform layer. `unix.c` is excluded on the 3DS, and `include/enet/unix.h` pulls `arpa/inet.h` for libctru. |
| `slippi_plat.[ch]` | Platform layer: time, sleep, lock, thread, log, file, `socInit`/`acInit`, LAN IP fallback. Uses libctru on the 3DS and Win32 for the PC tools. |
| `slippi_json.[ch]` | Small JSON reader and writer for the matchmaking messages and `user.json`. |
| `slippi_config.c` | `config.ini`, `user.json`, connect-code Shift-JIS encoding, `ip:port` parsing. |
| `slippi_mm.c` | Matchmaking client (Dolphin `SlippiMatchmaking`, DIRECT mode). |
| `slippi_p2p.c` | P2P netplay (Dolphin `SlippiNetplayClient`, 1v1). |
| `slippi_net.[ch]` | Lifecycle, service loop, network thread, native API. |
| `slippi_bridge.c` | Engine API from `port/include/slippi_net_bridge.h`: lockstep `send_inputs` and the match block. |
| `slippi_selftest.c`, `slippi_testpattern.h` | Boot self-test, also built for the PC. |
| `port/3ds/game_bridge.S` | `SDKBRIDGE mp_platform_slippi_net_*` → `slippi_net_*`. `send_inputs` takes 6 arguments and goes through `SDKBRIDGE6` (see below). |
| `tools/slippi/build_tools.py`, `tools/slippi/src/` | PC tools: fake matchmaking server, fake Dolphin peer, PC build of the self-test. |
| `tools/slippi/net_test.py` | Test runner: PC↔PC, PC client, Azahar. |

`tools/build_game.py` compiles `port/3ds/slippi/*.c`. It also compiles `port/3ds/slippi/enet/*.c` with `-w`, into objects named `enet_*.o`. The CIA RSF (`port/3ds/melee.rsf`) now lists `soc:U` and `ac:u`, and the `ac`/`socket` dependencies.

## Design

**One ENet host per search.** Dolphin binds its matchmaking host to `41000 + rand() % 10000` (or the forced port). After a match it destroys that host and creates the P2P host on the same port, which is what makes hole punching work. That swap blocks for up to 3 s while it waits for the MM disconnect. Here a single host does both jobs: 10 peers, 3 channels, no bandwidth limits. The MM peer is disconnected while the P2P `enet_host_connect` starts. What goes on the wire is the same: same port, same ENet connect (3 channels, data 0).

**Threading.** All client state sits behind one lock. By default a network thread runs `enet_host_service(host, 0)` every `net_thread_interval_us`:
- default 2000 µs;
- core `net_thread_core`, default -2, the app core;
- priority one above the caller, like the port's log and file workers.

Every API call takes the lock, so the engine thread can call anything. `slippi_net_poll()` also services ENet, so a driver can poll while it waits for an input. Nothing blocks:
- ENet runs with timeout 0;
- the DNS lookup releases the lock;
- pads are sent and flushed straight from `send_inputs`.

Log lines produced on the network thread are queued. They reach `game.log` from the next API call on the engine thread, because `mp_log_append` is not thread-safe in every build flavour.

Why a thread and not a blocking `poll()`: soc:U is a single IPC session. A blocking `poll` would hold the session and delay the engine thread's `sendto`. Polling with timeout 0 costs one `recvfrom` IPC per pass. With `net_thread=0`, only `slippi_net_poll()` services ENet. A send then still goes out at once, but received packets wait for the next poll.

**Remote pads** sit in a 512-entry ring keyed by frame. `remote_head_frame` is Dolphin's `head_frame`: the newest queued frame, 0 when empty.

**Local pads** are a ring with the newest pad first. Each packet carries every pad not yet acked, capped at `newest - 128`, exactly like `SendSlippiPad`.

## Wire behaviour (checked against Dolphin's source)

### Matchmaking
- Transport and target:
  - ENet over UDP to `mm_host:mm_port` (default `mm.slippi.gg:43113`);
  - 3 channels;
  - one JSON text per reliable packet on channel 0.
- `create-ticket`:
  - `user{uid, playKey, connectCode, displayName}`;
  - `search{mode: 2, connectCode: [bytes]}`;
  - `appVersion`;
  - `ipAddressLan "a.b.c.d:port"`.
- Keys are sorted with no whitespace, like `nlohmann::json::dump()`.
- `search.connectCode` holds the bytes the game's name-entry screen hands to Dolphin: full-width Shift-JIS.
  - `Ａ`..`Ｚ` = 0x8260+, `０`..`９` = 0x824F+, `＃` = 0x8194.
  - Example: `ABCD#123` becomes 16 bytes.
  - `code_encoding=ascii` sends plain bytes instead.
- LAN address:
  - `lan_ip=` if forced;
  - else the UDP-connect trick toward the MM server;
  - else `gethostid()` on the 3DS.
- The response handling follows `handleMatchmaking`:
  - `create-ticket-resp` / `get-ticket-resp`, with `error` and `latestVersion` logged;
  - `players[]`, `isLocalPlayer`, `port`, `isHost`, `stages`, `items`, `matchId`.
- Remote address choice: the remote's `ipAddressLan` if both external IPs match, otherwise its `ipAddress`.
- Timeouts:
  - MM connect 10 s;
  - create-ticket reply 5 s;
  - P2P connect 8 s, after which it re-tickets with a new port (`mm_retries`, default forever, like Dolphin Direct).

### P2P
All SFML-style big-endian.

| ID | Layout | ENet |
|---|---|---|
| 0x80 pad | `s32 frame, u8 port, s32 checksum_frame, u32 checksum, N×8 pads newest first` | unsequenced, ch 1 |
| 0x81 ack | `s32 frame, u8 port` (only when the pad packet added frames) | unsequenced, ch 2 |
| 0x82 selections | `char, color, char_sel, player_idx, u16 stage, stage_sel, u32 rng_offset, team, alt_stage_mode` | reliable, ch 0 |
| 0x84 chat | `s32 id, u8 port` | reliable, ch 0 |

Receive side, as in `OnData`:
- `inputs_to_copy = frame - head_frame`. Packets that are too short, or that carry more than 128 inputs, are rejected.
- The checksum pair of every pad packet is stored.
- Ping is the time from sending a pad packet to its ack.
- Each pad packet adds a time-offset sample to a 30-entry ring. `CalcTimeOffsetUs` sorts it and averages the middle third.
- Selections are merged with `Merge`. They also clear the remote pad queue and `has_game_started`.
- Chat ids are checked against Dolphin's list. With `chat=0`, the client answers `CHAT_DISABLED (0x10)` the way Dolphin does with chat off.

Connections:
- Duplicate connections from simultaneous connects are resolved like Dolphin: when a pad arrives with more than one live connection, the lower player index keeps that peer and disconnects the others. As a fallback, a disconnected primary is swapped for a live duplicate.
- During connect, the remote host is matched by IP only, to tolerate NATs that change the port.
- `DISCONNECT` events and the 7 s stall rule end the match; the disconnect reason is kept.

Right after connecting, the current selections go out with a random allowed stage, not yet selected. Dolphin does the same.

### Lockstep frame rule (`mp_platform_slippi_net_send_inputs`)
This mirrors `CEXISlippi::handleOnlineInputs` / `shouldSkipOnlineFrame` for a peer that never predicts:

| Result | When | What happens |
|---|---|---|
| `2` SKIP | Remote pad for `frame` not here yet | Nothing new is queued. Un-acked pads are re-sent (`SendSlippiPad(nullptr)`). After 420 consecutive skips the peer is force-disconnected and the call returns `3`. |
| `2` SKIP | Time sync: frame % 30 == 0, frame ≤ 120, and we are more than 10 ms ahead | Skip `(offset - 10000) / 16683 + 1` frames, at most 5. |
| `1` OK | Otherwise | Queue the local pad tagged `frame + delay`; at frame 1 also queue zero pads for 1..delay, one packet each (`handleSendInputs`). Send, then copy out the remote pad (8 wire bytes + 4 zero bytes). |

- The header checksum is `(finalized_frame, checksum)`. With `send_checksum=0` it is `0/0`, and the PC never checks (frame ≤ 84).
- At frame 1, before anything is queued, `StartSlippiGame` runs automatically. It clears the local queue, acks and timing, and the selections. `mp_platform_slippi_net_new_game` does the same explicitly.
- Not ported:
  - `shouldAdvanceOnlineFrame`, the speed-up when behind: a lockstep 3DS cannot run faster than real time;
  - ranked-only performance termination.

### Match block (`mp_platform_slippi_net_match_block`)
This rebuilds `prepareOnlineMatchState` for DIRECT 1v1 from Dolphin's static template:
- characters, colours and shades at `0x60/0x63/0x67 + 0x24·i`, with team 0;
- p3/p4 type 3 (no player);
- stage: the first player in port order who selected one, else 0x1F;
- pause allowed (`[2] &= 0xF7`);
- stock mode `0x32`, `0x4C`, 480 s, 4 stocks, 0 %;
- items off, or the MM `items` bits.

Trailer:
- `+0x138` rng_offset: the decider's, big-endian;
- `+0x13C` local index (decider = 0);
- `+0x13D` delay;
- `+0x13E` alt_stage_mode.

Both peers log the block's CRC32 so their blocks can be compared. In the tests they match.

## Engine API

`port/include/slippi_net_bridge.h` defines the API the coordinator's frame driver calls:
- `start`, `status`, `error`, `poll`, `stop`;
- `local_index`, `set_selections`, `remote_ready`, `match_block`, `new_game`;
- `send_inputs`;
- `remote_checksum_frame` / `remote_checksum_value`, `ping_ms`.

Status codes:

| Code | Meaning |
|---|---|
| 0 | idle |
| 1 | matchmaking |
| 2 | connecting |
| 3 | connected |
| 4 | failed |
| 5 | disconnected |

Only scalars, byte buffers and C strings cross the boundary. `send_inputs` has six arguments, and arguments 5 and 6 arrive on the stack. They are big-endian words written by the BE engine, so `SDKBRIDGE6` loads them before `setend le` and stores them again in little-endian for the native callee.

The native API in `port/3ds/slippi/slippi_net.h` is richer: info strings, stats, chat, raw `send_pad`/`remote_pad`. The tools use it.

## Files on the SD card

`sdmc:/3ds/melee/slippi/user.json`: the Slippi Launcher's file. The fields read are `uid`, `playKey`, `connectCode`, `displayName` and `latestVersion`. The play key is only ever sent inside the create-ticket to the configured MM server, and it is never logged; only its length is.

`sdmc:/3ds/melee/slippi/config.ini`: `key=value`. A comment line starts with `#` or `;` at column 0, because connect codes contain `#`.

| Key | Default | Meaning |
|---|---|---|
| `opponent` | – | Opponent connect code used when the caller passes none |
| `delay` | 2 | Input delay frames (1..15), returned in the match block trailer |
| `mm_host` / `mm_port` | `mm.slippi.gg` / 43113 | Matchmaking server. Tests use `127.0.0.1`. |
| `local_port` | 0 | Force the local UDP port (0 = 41000..50999 random) |
| `lan_ip` | – | Force the `ipAddressLan` IP |
| `app_version` | `3.6.4` | `appVersion` sent in create-ticket (Ishiiruka's netplay version) |
| `code_encoding` | `fullwidth` | `ascii` sends the code bytes as typed |
| `send_checksum` | 1 | 0: pad header checksum 0/0 (PC never checks for desyncs) |
| `net_thread`, `net_thread_core`, `net_thread_interval_us` | 1, -2, 2000 | Network thread |
| `chat` | 0 | 1: keep received chat for `slippi_chat_poll`; 0: reply CHAT_DISABLED |
| `auto_resend` | 1 | Re-send un-acked pads every 16.7 ms when the engine sends nothing |
| `mm_retries` | -1 | Re-tickets after a failed P2P connect (-1 = forever) |
| `log_packets` | 0 | Per-packet logging |
| `test_drop_pct` | 0 | Tests: drop this share of outgoing pad packets |
| `selftest`, `selftest_frames`, `selftest_character`, `selftest_exit` | 0, 600, 2, 0 | Boot self-test |
| `selftest_allow_real_mm` | 0 | The self-test refuses `mm.slippi.gg` unless this is 1 |

When the folder or `config.ini` is missing, boot is unchanged: `slippi_selftest_requested()` returns 0, and nothing initialises sockets until the engine calls `start`.

## Testing

```
python tools/slippi/build_tools.py                       # PC tools (llvm-mingw)
python tools/slippi/net_test.py pc-pair                  # two Dolphin-like peers
python tools/slippi/net_test.py pc-client [--drop 25]    # 3DS self-test code on the PC vs a fake peer
python tools/build_game.py --smoke --boot --render-worker --async-presentation --build-dir build/game-opt --engine-build-dir build/engine-opt
python tools/slippi/net_test.py azahar dist/3ds/melee/melee-development.3dsx
```

`net_test.py` writes dummy profiles and runs the fake MM on UDP 43113. Logs go to `build/slippi-tests/<mode>/`. In the `azahar` mode:
- the profile goes into the private emulator's SD (`build/azahar/net/user/sdmc/3ds/melee/slippi/`);
- the emulator boots the dev build, and the self-test runs before the game files check;
- the result comes from `game.log` (`[slippi] selftest: PASS`).

Fake peer (`slippi_fake_peer.exe`):
- frames at 16 683 µs;
- inputs tagged `frame + delay`, with zero pads for the delay frames;
- predicted play up to 7 frames ahead of the newest remote input;
- Dolphin's time-sync halts, the 7 s stall disconnect, and finalized-frame checksums;
- every received pad is verified against `slippi_testpattern.h`.

Fake MM (`slippi_fake_mm.exe`):
- logs each create-ticket field, with the playKey redacted;
- pairs two tickets that search for each other;
- port 1 / `isHost` goes to the first ticket (`--second-is-host` flips this);
- `--error` and `--min-version` exercise the error paths.

## Results (2026-10-02)

All runs used dummy credentials and the fake server on 127.0.0.1. Logs are in `build/slippi-tests/`.

| Run | Setup | Outcome |
|---|---|---|
| `pc-pair` | Two Dolphin-like peers, 600 frames | Both PASS; 602/602 remote pads verified, 0 bad; ping ~1 ms; match block CRC32 identical on both sides |
| `pc-client` | 3DS self-test code on the PC vs fake peer | PASS; 600/600 frames at 60.04 fps, 0 mismatches, 597 checksums verified |
| `pc-client --drop 25 --delay-b 3` | Peer drops 25 % of its pad packets | PASS; 99 gaps recovered from the un-acked history, 0 mismatches; offset -12 ms (the peer's extra delay frame) |
| `azahar` | Dev build in Azahar vs fake peer; fake peer is decider | PASS (see below) |
| `azahar --second-is-host --drop 20 --delay-a 3 --frames 900` | 3DS is decider (player index 0) | PASS; 900/900 frames at 59.87 fps, 104 gaps recovered, 0 mismatches; the 3DS (lower index) resolved the duplicate connection |
| Fake MM `--min-version 9.0.0` | Server rejects the version | Client fails cleanly with the server's error text |
| `mm_host` left at default with `selftest=1` | Real server configured | Self-test refuses before any network traffic |
| Boot without `slippi/config.ini` | Normal boot | No `[slippi]` log lines; the game boots as before |

Azahar run, `game.log` excerpt:

```
[slippi] mm: 127.0.0.1 resolved to 127.0.0.1:43113
[slippi] ENet host bound to local port 44899
[slippi] mm: sending LAN address '127.0.0.1:44899'
[slippi] mm: match mode.direct-fake-...: local port 2, decider 0, opponent 'Fake Dolphin' (PEER#002) at 127.0.0.1:46928
[slippi] p2p: connected to 'Fake Dolphin' (PEER#002) at 127.0.0.1:46928; we are player index 1, decider 0
[slippi] match block crc32 0xb4a5a1e2: stage 0x0020, p1 char 20 color 0, p2 char 2 color 1, rng 0x6d3a, local index 1, delay 2
[slippi] selftest: 600/600 frames in 10.01 s (59.94 fps), 601 ticks, 1 skips (longest run 1), remote pad mismatches 0, checksums 551 checked 0 bad
[slippi] selftest: ping avg 8.1 ms max 17.0 ms, time offset -9296 us, pad packets tx 602 rx 603 stale 1 gaps 0 resends 0 test-dropped 0, acks tx 602 rx 602
[slippi] selftest: PASS
```

What the Azahar runs show:
- Azahar's soc:U emulation works for this client. Guest `127.0.0.1` is the host's loopback, and a guest bind to port P binds host port P, so the fake MM sees the 3DS at `127.0.0.1:P`.
- `gethostid()` returns the host's LAN address (for example 0x0104a8c0 = 192.168.4.1).
- `ac:u` reports Wi-Fi as connected.
- The PC side logged 602/602 3DS pads verified and every 3DS checksum `ok`.
- Ping in the emulator is 2–17 ms, because emulated time is coarse and the network thread runs at 2 ms.

## First-connection race (fixed 2026-10-03)

- **Symptom (hardware runs 5–7).** The first connection after boot hung or dropped. The PC got our selections and started its match; the 3DS never got the PC's selections, and logged no "late connection".
- **Cause.** Matchmaking and P2P share one ENet host here. Dolphin instead opens its P2P host only after matchmaking. When the opponent hears "match found" first, it connects and sends its selections straight away. Our ENet accepts and acknowledges both, but `service_locked` dropped every non-MM event while status was SEARCHING.
- **Fix.** Those events are queued (`early_event_keep`, `slippi_net.c`). `slippi_p2p_start` replays them once the opponent's address is known. The replay skips stale peers: disconnects, and connects for peers that are no longer connected.
- **Tests.** `slippi_fake_mm --delay-first-ms N` holds the first ticket's reply. Use it via `online_game_test.py --mm-hold-first 3000 [--peer-wait 16 to hold the 3DS]` and `online_pair_test.py --mm-hold-first 3000`.
- **Logging.** Connection events before a match are logged as `net: event ...`.

## Slow links (hardware run 9, phone hotspot, 72-113 ms)

- **What happened:** the games stayed in sync and ran to the end, but the 3DS waited for the PC's pad 4345 times (81 s) in a 6420-frame game.
- **Why:** a lockstep peer needs the remote pad for a frame when it plays that frame. Running level with the PC, the pad arrives after the PC's 2 frames of delay have run out.
- **The old behaviour made it worse:** catching up whenever the 3DS was 1.6 frames behind removed the very lag that would have hidden the latency.
- **Fix (`online.c`):** the allowed lag grows by half a frame per 30-frame window with 4 or more waits, up to 5 frames (inside the PC's 7-frame rollback window). It shrinks by a quarter frame after 10 quiet windows. LAN play stays at 1.6 frames.
- **On the PC side:** a larger delay setting on the PC helps the 3DS most, because it is the remote delay that hides the latency.

## Match load time (hardware run 10)

- **Before:** each load took about 9-10 s, 27-29 SD opens of about 220 ms each.
  - Files in the 1000-entry disc folder cost about 220 ms per open. Stage files (another folder) cost 3-35 ms.
  - The boot prefetch only runs for `auto_match`, so the menu flow had nothing in RAM.
- **Fixes:**
  - **Empty player slots** no longer preload the template's fighter. Young Link's five files (about 3 MB, 1.1 s) had been loaded for nobody in every game. Fixed in `mp_slippi_prepare_match_files`.
  - **VS splash is off.** This saves 6-8 opens plus the scene itself.
  - **Keep on read** (`file_io.c`, `keep_file`): during a match load (match ready to frame 120), each file except .hps streams of at most 3 MB is read whole on its first open and kept.
    - The cache holds up to 10 MB and evicts least recently used files, but never one the current load used.
    - Nothing is kept if fewer than 12 MB of the heap would remain. Run 10 peaked at 65 MB used of 86 MB.
  - **Result:** in the emulator, the second game against the same opponent opens no files during the scene preload (158 ms, down from 976 ms). Only the stage's sound bank and music are opened.
- **Time sync:** the 3DS pads at exactly 60 Hz against Dolphin's 59.94 Hz, so on LAN it drops one frame about every 12 s (`dropping 1 frames`). This is expected and matches what Dolphin does when it is ahead.

## Open issues

- **Real server acceptance.**
  - `appVersion`: the server decides whether a version is too old (`get-ticket-resp.error` + `latestVersion`). Ishiiruka currently sends `3.6.4`; mainline sends `4.0.0-mainline-beta.19`. Set `app_version=` to whatever a current Slippi build sends. Run the first real test with a throwaway account.
  - Connect-code encoding: the full-width encoding matches what Slippi's name-entry ASM sends. The server presumably also accepts ASCII, since Melee Unlocked's native path sends `utf8_to_shiftjis` of ASCII. `code_encoding=ascii` is the fallback.
  - The `isHost` / port-1 assignment comes from the private server. Every rule here follows `isHost` exactly as Dolphin does.
  - The reverted websocket matchmaking in slippi-rust-extensions (`ecc10ff`) shows that the MM protocol may change: STUN, websocket. This client speaks the current ENet/JSON protocol only.
- **NAT on 3DS Wi-Fi.**
  - Hole punching depends on the router keeping the same external port for our UDP socket, the one the MM server observed. That is endpoint-independent mapping, as in Dolphin.
  - Symmetric NAT / CGNAT on either side fails just like it does for two Dolphins; there is no relay.
  - The 3DS stack (soc:U) adds no NAT itself. Test it on real hardware behind a typical home router, and with a phone hotspot (often CGNAT).
  - `local_port=` plus a router port-forward is the manual workaround, as `Slippi.ForceNetplayPort` is for Dolphin.
- **Not verified on hardware yet:**
  - soc:U behaviour of `getsockname` after a UDP `connect` (the LAN IP); `gethostid()` is the fallback;
  - socket buffer clamping to 32 KiB;
  - thread scheduling cost of the 2 ms network thread on core 0, which could move to core 1 with `APT_SetAppCpuTimeLimit`.
- **Speed matching.** Dolphin speeds up by at most 1 % when behind. A lockstep 3DS that falls behind can only be waited for: the PC stalls when we are more than 7 frames behind its prediction. Slow frames on the 3DS turn directly into PC stalls.
- **Determinism.** Lockstep networking is only useful once the simulation is bit-exact. That is outside this layer; see `protocol_notes.md` §3–4.
