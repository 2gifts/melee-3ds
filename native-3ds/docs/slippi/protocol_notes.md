# Slippi Online (Direct mode) protocol notes: third-party native-client feasibility

Research date: 2026-10-02. All findings come from reading source, not from guessing. Shallow clones are in this folder:

| Repo | Commit read |
|---|---|
| project-slippi/Ishiiruka (branch `slippi`) | 60f7b63 (2026-09-22) – extracted to `x_Ishiiruka/` |
| project-slippi/dolphin (mainline, branch `slippi`) | 41a7a3a (2026-08-30) – extracted to `x_dolphin/` |
| project-slippi/slippi-rust-extensions | c7888e0 (2026-09-14) |
| project-slippi/slippi-ssbm-asm | fcf47f1 (2026-05-01) |
| project-slippi/slippi-wiki | 71c6a39 (2025-12-06) |

(A full checkout of Ishiiruka/dolphin failed on Windows, so the relevant files were extracted with `git archive` into `x_*`.)

Paths below are given relative to `slippi-research/`. Unless stated otherwise, C++ refers to the mainline file. The Ishiiruka copy is byte-for-byte equivalent on the wire, and the mainline README says "Netplay functionality is cross compatible".

---

## 1. Matchmaking (Direct / connect-code)

### Server, transport
`x_dolphin/Source/Core/Core/Slippi/SlippiMatchmaking.h`
```cpp
const std::string MM_HOST_DEV = "mm2.slippi.gg";  // Dev host
const std::string MM_HOST_PROD = "mm.slippi.gg";  // Production host
const u16 MM_PORT = 43113;
```
- `MM_HOST` is PROD unless the version string contains `"dev"` (`SlippiMatchmaking::SlippiMatchmaking`). No env var, config key or other override exists in the shipping C++.
- The transport is **ENet over UDP**. `enet_host_create(&client_addr, 1, 3, 0, 0)` binds to a **random local port 41000 + rand()%10000**. The config `Slippi.ForceNetplayPort`/`NetplayPort` (default 2626) can force the port. The client then calls `enet_host_connect(m_client, &addr, 3, 0)` (3 channels).
- Each message is a single **JSON text** (nlohmann `dump()`), sent as a reliable ENet packet on channel 0 (`sendMessage`).

### Messages (`MmMessageType`)
`"create-ticket"` → `"create-ticket-resp"`, then the client waits for `"get-ticket-resp"`.

The create-ticket request is built in `SlippiMatchmaking::startMatchmaking()`:
```cpp
request["type"] = "create-ticket";
request["user"] = {{"uid", user_info.uid},
                   {"playKey", user_info.play_key},
                   {"connectCode", user_info.connect_code},
                   {"displayName", user_info.display_name}};
request["search"] = {{"mode", m_search_settings.mode},           // RANKED=0 UNRANKED=1 DIRECT=2 TEAMS=3 PARTY=4
                     {"connectCode", connect_code_buf}};         // std::vector<u8> -> JSON array of byte values
request["appVersion"] = Common::GetSemVerStr();                  // e.g. "3.x.y"
request["ipAddressLan"] = lan_addr;                              // "a.b.c.d:port" (port = the bound ENet port)
```
- `search.connectCode` holds the **raw Shift-JIS bytes** of the opponent's code as typed in the game's name-entry screen: 18 bytes, NULs trimmed (`CEXISlippi::startFindMatch`). Letters are full-width; the reverted Rust test uses `[0x82,0x60]` for "Ａ".
- Credentials come from `user.json`. The Rust `user/src/lib.rs` `UserInfo` reads `uid`, `playKey`, `displayName`, `connectCode`, `latestVersion` and `chatMessages`. The file is produced by the web login at `https://slippi.gg/online/enable?path=...`. `playKey` is a secret and is sent in plaintext JSON over ENet/UDP (ENet has no encryption).
- `ipAddressLan` is found by `connect()`ing a UDP socket toward the MM server and reading the local address. The fallback is `gethostbyname`. It can be overridden with `Slippi.ForceLanIP` / `Slippi.LanIP`.

Response handling:
- `create-ticket-resp`: if an `"error"` string is present it is shown to the user.
- `get-ticket-resp` (`handleMatchmaking`, polled with a 2 s `enet_host_service` loop):
  - `error` (string). If present, `latestVersion` is also read and saved via `OverwriteLatestVersion` (the "your version is outdated" path). **Minimum-version rejection happens on the server**: the client only sends `appVersion`, and the private server decides.
  - `matchId` (string, e.g. `mode.unranked-2022-...`; it is checked for the substring `mode.ranked`)
  - `players[]`: `{isLocalPlayer, uid, displayName, connectCode, port (1..4), isBot, chatMessages[16], rank{rating,updateCount,globalPlacement,regionalPlacement}, ipAddress "ext_ip:port", ipAddressLan "lan_ip:port"}`
  - `isHost` (bool; this is the "decider", see §2)
  - `stages[]` (int stage IDs), `items` (u32 bitfield)
- How the remote address is chosen: if the remote's external IP equals our own external IP (the `ipAddress` of the `isLocalPlayer` entry, which is how the server saw us), the client uses the remote's `ipAddressLan`. Otherwise it uses `ipAddress`. Only one of the two is tried ("TODO: ... try both").
- When the match is received, `terminateMmConnection()` destroys the MM ENet host. `SlippiNetplayClient` then creates a **new ENet host bound to the same local port** (`enet_host_create(local_addr, 10, 3, 0, 0)`) and `enet_host_connect`s to every remote.

### NAT traversal
- This is plain **UDP hole punching through the MM server's observed address**. The server sees our NAT mapping for the bound port, and both peers simultaneously `enet_host_connect` to each other from that same port. Comment in `SlippiNetplay.cpp`: "It is important to be able to set the local port ... not doing so will break hole punching".
- `ThreadFunc` gives the connection a **timeout of 8000 ms**. Duplicate connections (both directions succeeding) are de-duplicated: the lower `m_player_idx` keeps one and `enet_peer_disconnect`s the extras. Matching is done by host only, without the port, to tolerate port-randomizing NATs.
- There is no STUN, TURN or relay. On failure in Direct mode the state goes back to `INITIALIZING`, which re-tickets.
- The **reverted** commit `ecc10ff` in slippi-rust-extensions ("add websocket based mm logic", reverted the same day, 2026-09-14) shows the planned future. That design uses `wss://matchmaking.slippi.gg/v1/queue`, a `SLIPPI_MM_URL` env override, STUN candidates (`host`/`srflx`/`observed`), `natType`, and `DebugOverrides` that "the service honors only when it runs with authentication disabled". It is **not live in current code**, but it means the MM protocol may change soon.

### Local / LAN / direct-IP modes?
- **There are none in shipping builds.** `#define LOCAL_TESTING` (commented out in `EXI_DeviceSlippi.cpp`) is a compile-time dev hack. It skips MM and creates a dummy `SlippiNetplayClient(true)` whose status is `NET_CONNECT_STATUS_FAILED`, then fakes an opponent locally. It never connects to another machine.
- `ForceNetplayPort` and `ForceLanIP` only help with port forwarding. MM is still required.
- Traditional Dolphin netplay (`NetPlayClient.cpp`, `NP_MSG_*` < 0x80) still exists in both forks. It is emulator-level lockstep: it syncs SI pad polls, requires identical emulator state, and has nothing to do with the Slippi rollback protocol. A native port could not take part in it.
- The P2P protocol (§2) itself is independent of MM: a peer only needs the remote IP:port, its own/remote player index, and the decider flag. A **patched** Slippi Dolphin could connect directly. Stock Slippi Dolphin can only reach a peer through `mm.slippi.gg`.

---

## 2. P2P netplay protocol (`SlippiNetplay.cpp` / `.h`)

### Framing
- Transport is ENet 1.3.x (`Externals/enet`, the stock lsalzman submodule in mainline), with 3 channels, no compression and no checksum. Application connection data = 0.
- Payloads are `sf::Packet` (SFML) serialization: **multi-byte ints big-endian** (`htonl`), `bool` → u8, `std::string` → u32 BE length + bytes.
- First byte = `NetPlay::MessageID` (mainline `NetPlayProto.h`; Ishiiruka `NP_MSG_*`):

| ID | Name | ENet flags / channel |
|---|---|---|
| 0x80 | SLIPPI_PAD | UNSEQUENCED, ch 1 (`Send()`) |
| 0x81 | SLIPPI_PAD_ACK | UNSEQUENCED, ch 2 (sent directly with `enet_peer_send(peer, 2, ..)`) |
| 0x82 | SLIPPI_MATCH_SELECTIONS | RELIABLE, ch 0 |
| 0x83 | SLIPPI_CONN_SELECTED | RELIABLE, ch 0 (marked "currently unused") |
| 0x84 | SLIPPI_CHAT_MESSAGE | RELIABLE, ch 0 |
| 0x85 | SLIPPI_COMPLETE_STEP | RELIABLE, ch 0 (ranked "game prep" steps) |
| 0x86 | SLIPPI_SYNCED_STATE | RELIABLE, ch 0 (ranked desync recovery) |

- There is **no application-level hello, version field or magic** between peers. The ENet CONNECT event is the whole handshake.
- Disconnect reason: `enet_peer_disconnect(peer, reason)` with `0 = UNSPECIFIED`, `1 = POOR_PERFORMANCE`.

### SLIPPI_PAD (0x80)
Sender: `SendSlippiPad`.
```
off size
0   u8   0x80
1   s32  frame           // newest frame in this packet (= local frame + local delay)
5   u8   player_port     // sender's global player index 0..3
6   s32  checksum_frame  // finalized frame the checksum belongs to
10  u32  checksum
14  N*8  pads            // newest first: frame, frame-1, ..., all not yet acked by every peer (capped to 128 frames back)
```
The pad data is the first 8 bytes of Melee's 12-byte raw PAD report (`SLIPPI_PAD_DATA_SIZE 0x8`, full size 0xC). Layout per `Online/Online.s`:
```
0x0 u8  ---SYXBA      0x1 u8 -LRZUDRL
0x2 s8  stick X       0x3 s8 stick Y
0x4 s8  c-stick X     0x5 s8 c-stick Y
0x6 u8  L analog      0x7 u8 R analog
(0x8 analogA, 0x9 analogB, 0xA status, 0xB pad -- NOT transmitted; receiver zero-fills)
```
Receiver (`OnData`):
- `inputs_to_copy = frame - head_frame`, where `head_frame` is the newest queued remote frame, or 0 if the queue is empty.
- It reads that many pads from offset 14 and pushes them oldest→newest.
- It rejects the packet if it is too short or if `inputs_to_copy > 128`.
- It stores `(checksum_frame, checksum)` as that player's latest remote checksum.
- **The packet must therefore always contain every frame contiguously from the receiver's last frame.** The sender keeps all un-acked pads, and at frame 1 it queues zero pads for frames `1..delay` (`CEXISlippi::handleSendInputs`).

### SLIPPI_PAD_ACK (0x81)
```
0 u8 0x81 | 1 s32 frame (the 'frame' field of the pad packet being acked) | 5 u8 acker_player_idx
```
- An ack is only sent when `inputs_to_copy > 0`.
- The sender drops local pads older than the minimum acked frame across active peers. If nothing is acked, the backlog is capped at 128 frames.
- **Ping** is measured as ack-arrival time minus the send time of the pad packet with exactly that `frame` (`ack_timers`).

### SLIPPI_MATCH_SELECTIONS (0x82)
Sent whenever selections change (`writeToPacket`):
```
0 u8 0x82 | 1 u8 character_id | 2 u8 character_color | 3 u8 is_character_selected
4 u8 player_idx | 5 u16 stage_id | 7 u8 is_stage_selected | 8 u32 rng_offset
12 u8 team_id | 13 u8 alt_stage_mode        (14 bytes)
```
- When received, it merges into `remote_player_selections`, **clears that player's remote pad queue and resets `has_game_started`**. This is how a new game starts.
- `rng_offset = generator() % 0xFFFF` is generated locally on every `setMatchSelections`.
- `alt_stage_mode` is the frozen Pokémon Stadium toggle (Direct/Teams only, otherwise forced to 0).

### SLIPPI_CHAT_MESSAGE (0x84)
`0 u8 0x84 | 1 s32 message_id | 5 u8 player_idx`. Only ids {136,129,130,132,34,40,33,36,72,66,68,65,24,18,20,17} and 0x10 (CHAT_DISABLED) are accepted.

### SLIPPI_CONN_SELECTED (0x83)
Just the byte `0x83`.

### SLIPPI_COMPLETE_STEP (0x85)
`0x85 | u8 step_idx | u8 char | u8 color | u8 stage0 | u8 stage1`. Used by ranked game-prep only.

### SLIPPI_SYNCED_STATE (0x86)
```
0x86 | u8 player_port | string match_id (u32 len + bytes) | u32 game_idx | u32 tiebreak_idx | u32 seconds_remaining
     | 4 x { u8 stocks_remaining, u16 current_health }
```
Sent only in **ranked** when a game ends with method 7 (desync/quit) (`CEXISlippi::handleReportGame`). It is used to restart the next game at the reconciled timer, stocks and percent (`GetDesyncRecoveryState`).

### Who is P1 / RNG authority
`CEXISlippi::prepareOnlineMatchState`:
- In 1v1, `m_local_player_idx = is_decider ? 0 : 1`, where decider = MM `isHost`.
- `rng_offset = is_decider ? local.rng_offset : remote[0].rng_offset`.
- Stage: the first player in port order with `is_stage_selected` decides. Otherwise the default is `0x1F` (Battlefield).
- The **0x138-byte match struct is built by Dolphin** (`online_match_block`):
  - stock mode (`[0]=0x32`), 4 stocks, 8:00 timer (`[0x10]=480`), items off (`[0xB]=0xFF`, item bits `0xF80000000F000000` unless MM `items`)
  - characters/colors written at `0x60/0x63 + 0x24*i`, shades at `0x67`, team at `0x69`
  - p3/p4 = empty (type 3)
  - pause bit `[2]&0x08`: cleared (vanilla pause allowed) **only in DIRECT**; set in every other mode
  - desync-recovery stocks/percent at `0x62`/`0x70`
- This block is delivered to the game (`MSRB_GAME_INFO_BLOCK`) and copied in `Online/Core/InitOnlinePlay.asm`. A third-party peer must construct a byte-identical block.

### Time sync / frame advantage
Offset estimation (`OnData`, SLIPPI_PAD case):
```cpp
s64 opponent_send_time_us = curr_time - (ping_us[p_idx] / 2);
s64 frame_diff_offset_us  = 16683 * (timing.frame - frame);   // timing = our last SENT pad (frame tag & time)
s64 time_offset_us        = opponent_send_time_us - timing.time_us + frame_diff_offset_us;
```
This is stored in a 30-entry ring. `CalcTimeOffsetUs()` takes the mean of the middle third of the sorted samples, then the minimum across peers. Positive means we are ahead. Frame tags include each side's delay, so the sync targets equal wall time for equal *tagged* frames.

`CEXISlippi::shouldSkipOnlineFrame`. This function makes the ahead peer **stall**:
- **Rollback-limit halt (always on):** for each remote, if `latest_remote - finalized < frame - finalized - 7`, the frame is skipped (the game does not advance). After **420 consecutive stalled frames (7 s)** the peer is force-disconnected.
- **Time-sync skip:** only on frames that are multiples of 30 and **≤ 120**. If offset > 10 ms, skip up to 5 frames.

`shouldAdvanceOnlineFrame`, every 30 frames:
- It sets emulation speed `1 + deviation`: up to **+1 %** when behind, up to **−0.5 %** when ahead more than 8 ms, scaled over a 3-frame window.
- When behind by more than 26.7 ms after frame 120, it "advances" up to 3 frames: result code 4, one extra frame every 5 frames.
- Ranked only (`handlePoorMatchPerformance`): a debt accumulator over speed ratio and average ping, every 150 frames. A threshold of 30 terminates the match with reason 1.

### Rollback window and delay
- `ROLLBACK_MAX_FRAMES 7` (C++), `ROLLBACK_MAX_FRAME_COUNT 7` (ASM).
- Delay is clamped to `MIN_DELAY_FRAMES 1` .. `MAX_DELAY_FRAMES 15` in ASM. The Dolphin default is `Slippi.OnlineDelay = 2`.
- **Each side's delay is private**: the input captured at local frame F is sent tagged `F + delay`.

### Desync detection
The checksum is computed in ASM by `FN_COMPUTE_CHECKSUM` in `Online/Core/StartEngineLoop.asm`, once per processed frame at the engine-loop start hook `0x801a4de4`:
```
for each of 4 static player blocks (0x80453080 + i*0xE90), for gobj in {+0xB0 (main), +0xB4 (Nana/Sheik-Zelda secondary)}:
   fd = gobj->0x2C
   ck ^= fd+0x10 (action state u32) ^ fd+0xB0 (pos X bits) ^ fd+0xB4 (pos Y bits) ^ fd+0x1830 (percent bits) ^ fd+0x8 (spawn #)
   fsum += posX + posY + percent        (single precision fadds)
 ck ^= static+0x8E (stocks byte)   [per player block]
result = ((ck>>16 ^ ck&0xFFFF) << 16) | (u16)(fctiwz(fsum))
```
- Entries `{frame, checksum, timer, stocks/percent[4]}` are kept in a 21-entry ring (`DESYNC_ENTRY_COUNT = 3*7`).
- The ASM sends the checksum of `ODB_STABLE_FINALIZED_FRAME` in every TXB (`TXB_FINALIZED_FRAME_CHECKSUM`). Dolphin copies it into the pad packet header.
- On the receiving side, if the remote checksum frame satisfies `84 < frame ≤ our stable finalized` and is in our ring:
  - The low 16 bits are compared as s16; a difference **> ±1 is a hard desync**. The game shows "DESYNC DETECTED" and `FN_END_GAME` ends it (exit type 7 = no contest; in ranked the synced-state recovery then runs).
  - A mismatch in the high 16 bits only shows a "Desync Risk" HUD.
- Note: a peer that sends `checksum_frame = 0` (≤ 84) is **never checked**.

---

## 3. How rollback is done, and whether a lockstep peer can interoperate

### Division of labour
- **ASM (`Online/Core`) drives everything.**
  - `TriggerSendInput.asm` hooks `HSD_PadRenewRawStatus` right after `PAD_Read` (0x80376a28). Each raw poll is one online frame. Per poll it:
    - zeroes all 4 pads while `frame < 78 - delay` (START_SYNC_FRAME = UNFREEZE 84 − 6)
    - clamps stick and c-stick to 0,0 when both |axis| ≤ 2
    - replaces status −3 ("stale") pads with the last pad
    - EXI-sends `TXB {cmd, s32 frame, s32 finalized, u32 checksum, u8 delay, 12B pad}`
    - reads `RXB {result, opp count, 3×{checksum frame, checksum}, 3×latest frame, smallest latest, 3×7×12B inputs, 3×despawn}`
    - substitutes its own delayed pad from a delay ring
    - for remotes: uses the real input if available, else **predicts with the most recent received pad** and records the prediction
    - compares predictions against arrived inputs: buttons masked 0x1F/0x7F, sticks exact, triggers equal-if-both ≤ 42. On a mismatch it sets `ROLLBACK_IS_ACTIVE` with a target savestate frame.
  - `StartEngineLoop.asm` (0x801a4de4) loads the state when a rollback is active (`FN_LoadSavestate`), rewinds the raw pad ring index at 0x804c1f78, writes the checksum, checks desync, and captures a state (`FN_CaptureSavestate`) when the current frame has predicted inputs.
  - `LoopEngineForRollback.asm` (0x801a5014) loops the engine update without rendering until `ROLLBACK_END_FRAME`. It also kills sounds whose triggering frames were rolled back.
- **Dolphin C++ only snapshots RAM.** `SlippiSavestate::Capture/Load` memcpy fixed regions:
  - `0x80005520–0x80005940`
  - `0x803b7240–0x804DEC00` (data sections + BSS)
  - `0x8065c000–0x8071b000`
  - the main heap (bounds read from `0x804d76b8`/`0x804d76bc`)
  - Excluded: a list of sound/audio areas, XFB/VI at `0x804c0980+0x15F8`, and the "preserve blocks" the ASM passes (ODB, RXB and savestate-control buffer, so they survive a load).
  - Pool of 7 savestates; no CPU/hardware state.
- Rollback is therefore "restore RAM, re-run the game loop with corrected pads". Prediction, comparison and resimulation are game-side logic.

### Can a never-rollback (pure lockstep) peer interoperate?
At the protocol level, **yes**. Nothing in the wire protocol requires the remote to predict, roll back or save state:
- The PC peer predicts our inputs, compares, and rolls back on its own. It never asks us for anything except pads, acks and checksums.
- Our pad stream only has to be contiguous, correctly tagged (`frame + our_delay`), include zero pads for frames 1..delay, and be re-sent until acked.
- A lockstep peer simulates frame F only after holding the remote pad tagged F. That is exactly the data the protocol already delivers.

Constraints and pitfalls:
1. **Ack every pad packet.** Echo the packet's `frame`, but only when it carried new frames. Without acks the PC never prunes its queue (capped at 128) and its ping is 0, which skews its time-offset math.
2. **Stall tolerance of the PC peer.** It halts whenever it is more than 7 frames past its finalized frame relative to our latest input. If we stop sending for 7 s it drops us.
3. **Our latency budget is all input delay.** Slippi's time sync drives the two sides toward equal wall time for equal tagged frames. A lockstep peer then avoids stalling only if its own delay ≥ one-way latency + jitter (classic delay netplay).
   - The PC side absorbs the rest with rollbacks of up to 7 frames.
   - Slippi's speed correction is weak: −0.5 %/+1 %, plus frame skips only before frame 120.
   - So our peer must regulate its own pacing and must not run systematically faster or slower than about 59.94 Hz (16683 µs/frame).
4. **Checksums.** Sending the real checksum for our latest finalized frame lets the PC detect desyncs. Sending `checksum_frame ≤ 84` disables detection on the PC side, because `CHECKSUM_CHECK` skips frames ≤ UNFREEZE_INPUTS_FRAME. Our own desync detection is optional.
5. **Bit-exact simulation is mandatory.** Lockstep removes no determinism requirement. The game info block, the per-frame RNG reseed (§4), the pre-unfreeze pad zeroing, the stick-rest clamp, stale-pad handling and every gameplay code (§4) must match.
6. **Input semantics.** The PC simulates *predicted* trigger values ≤ 42 without rolling back when the real value is a different ≤ 42 value, and ignores unused button bits. Melee must therefore treat all trigger values ≤ 42 identically: these are values the PC may never "correct". Our peer should simulate with exactly the transmitted bytes, as Slippi does via `TXB_PAD` → delay buffer.
7. **New-game boundary.** The remote pad queue is cleared when MATCH_SELECTIONS arrives (reliable ch 0). Pads travel on unsequenced ch 1, so ordering between them is not guaranteed. Do not start sending next-game pads (frame 1…) before selections are exchanged.
8. The `latest_frame` / `RXB_SMALLEST_LATEST_FRAME` math assumes frames start at 1 per game and increment by exactly 1 per raw poll that the game consumes. A native port must reproduce that "one PAD_Read per engine frame" model (Slippi enforces it with the `PD+VB` lag-reduction binary).

---

## 4. Gameplay-affecting modifications to replicate

The Netplay codeset (`netplay.json` → `Output/Netplay/GALE01r2.ini`, the same as Dolphin's `Data/Sys/GameSettings/GALE01r2.ini`) always enables:

**"Required: General Codes"**
- Unlock all; Stock mode; 4 stocks; 8 minutes; No items; Singles stages; Neutral Spawns `[affects-gameplay]`
- **UCF v0.84**, the folder `External/UCF 0.84/UCF`: Dashback, Shield Drop, Shield Drop Extended, SDI, Shield SDI, Tumble, DBOOC SquatRv fix, "Pad Buffer + 1.0 Cardinals"
- Disable FoD during doubles; Prevent freeze glitch (tauKhan, `FreezeGlitchFix` @0x801239A8); Correct Costume Conflicts
- Plus many menu-only codes

**"Required: Slippi Recording"**
- `Recording/` plus the `Common/` folder, including `Initialize Player Data` (@80068eec) and `Initialize Stage Data` (@801c154c), both tagged `[affects-gameplay]` (they zero-init allocations), and `ExtendPlayerBlock`

**"Required: Slippi Online"**
- `Online/Core`
- `PD+VB` polling-drift fix
- `Common/FastForward`, `m-ex.bin`, `Common/NanaDeterminism` (zero-inits uninitialized r5/r30 used for Nana throw DI), `Common/PSCameraIndependentMonitor` (camera-independent Stadium monitor transitions)
- `External/PreventWobbling` (max 3 pummel/regrab cycles on grabbed opponent)

Gameplay-tagged files in `Online/Core`:
- `BrawlOffscreenDamage` (@8006a880: offscreen damage based on stage camera *limits*, not the live camera)
- `FreezeDeadUpFallPhysics/*` (@800d4c1c/800d4d68: custom deterministic physics for star/up-KO)
- `WhispyBlowDirFix` (@8008653c: dead fighters ignored for Whispy direction)
- `Hacks/FD/DesyncProofBGTransformations` (@8021aae4: save/restore RNG around the FD background think)
- `Hacks/Stadium/*` (frozen-PS toggle via `INJ_FREEZE_STADIUM` @801d457c, with the value coming from `alt_stage_mode`; custom transformation file load and zero buffer)
- `Common/Preload Stadium Transformations` (deterministic transformation choice independent of disc timing)
- Ledge-grab limit 45: LGL exceeded loses on timeout in singles (`LGL_LIMIT`, `LGLExceededGameEnd`)

Online scene and RNG (`Online/Core/InitOnlinePlay.asm`):
- At game start: seed `*(u32*)0x804D5F90 = rng_offset`.
- A GObj proc (type 4, runs before player animation) **re-seeds every frame**:
  ```
  seed = rotl32(globalFrame /*0x80479D60*/, 16) + rng_offset
  ```
- Also clears raw held A at `0x804c20bc` (per-port stride 68), which prevents Zelda/Sheik transformation at spawn.
- The VS splash scene asks Dolphin for a fresh seed (`CMD_GET_NEW_SEED`, `Slippi Online Scene/main.asm`).

Rules and pause:
- Pause: DIRECT keeps vanilla synchronized pause. Unranked/Teams install a client-side pause (`InitPause.asm`) used for LRA+Start exit. Ranked/Party have none.
- Teams: Dolphin assigns colors/teams (team permutation `rng_offset % 6`).

Optional codes (Widescreen, Freeze FD background, Lagless FoD, Disable Screen Shake, and so on) are meant to be desync-safe. `DesyncProofBGTransformations`, `PSCameraIndependentMonitor` and `NanaDeterminism` exist precisely because those optional codes or differing code lists caused desyncs.

The `.slp` Gecko List event (0x3D, ≥ 3.3.0) records the exact code list of a replay.

Dolphin `[Core]` settings (emulator-only): `CPUThread`, `GPUDeterminismMode = fake-completion`, `PollingMethod = OnSIRead`.

Not from Slippi sources (general knowledge, flagged): a native ARM port must also reproduce PowerPC single-precision float behaviour bit-for-bit for anything that feeds positions or percent. That includes Gekko fused `fmadds`/`fmsubs`, `fres`/`frsqrte` estimate tables, and single-rounding of intermediates. Otherwise the checksum low-16 (`fctiwz` of the summed positions/percent) and the gameplay itself will diverge.

---

## 5. `.slp` replay format (slippi-wiki `SPEC.md`; recorder `Recording/*.asm`, current version 3.19.1)

- **Game Start 0x36:** version; 312-byte Game Info Block (byte-identical to what the online code built); **Random Seed** @0x13D (0.1.0); UCF dashback/shield-drop per port (1.0.0); Frozen PS (2.0.0); Major scene 0x8 = online (3.7.0); names/codes/UIDs (3.9/3.11); Session/match ID, game number, tiebreak (3.14.0).
- **Frame Start 0x3A (2.2.0):** frame number, **RNG seed at frame start**; scene frame counter (3.10.0).
- **Pre-Frame 0x37 (per character, Nana included):**
  - frame, port, isFollower, **RNG seed**, action state, X, Y, facing
  - processed stick X/Y and c-stick X/Y floats (fighter +0x620/+0x624/+0x638/+0x63C), processed trigger float (+0x650), processed buttons u32 (+0x65C)
  - physical buttons u16 (HSD_PadMaster +0x2)
  - physical L/R trigger floats (HSD_PadMaster +0x30/+0x34: processed floats, **raw trigger bytes are NOT recorded**)
  - raw stick X s8 (1.2.0), percent (1.4.0), raw stick Y (3.15.0), raw c-stick X/Y (3.17.0). The raw values come from the 5-entry raw ring at 0x8046b108 indexed via 0x804c1f78.
- **Post-Frame 0x38:** internal char ID, action state, X, Y, facing, percent, shield, last attack, combo, last hit by, stocks (0.1.0); action frame counter (0.2.0); state bit flags 1–5, hitstun, airborne, last ground, jumps, L-cancel (2.0.0); hurtbox state (2.1.0); self/attack speeds (3.5.0); hitlag (3.8.0); animation index (3.11.0); instance IDs (3.16.0).
- **Item Update 0x3B (3.0.0):** up to 15 items per frame, with position, velocity, state, timers, spawn ID and owner (3.6.0).
- **Frame Bookend 0x3C (3.0.0):** frame, plus latest finalized frame (3.7.0). Online replays **contain re-simulated frames from rollbacks** (the same frame number repeats). Use the last copy of each frame, or only frames ≤ finalized. Replay frame = online frame − 123 − pause frames (`Recording/FlushFrameBuffer.asm`).
- **Stage events (3.18.0):** FoD platform heights 0x3F, Whispy direction 0x40, Stadium transformation 0x41.
- Gecko list 0x3D (3.3.0); Game End 0x39 (placements 3.13.0).

Verification strategy:
- **Minimum useful version:** ≥ **3.17.0** for full raw sticks, ≥ 3.7.0 for finalized frames, ≥ 2.2.0 for per-frame seeds. Netplay replays from current Slippi are 3.19.x.
- Slippi's own playback (`Playback/Core/RestoreGameFrame.asm` @8006b0dc) does not reconstruct PADStatus. It **injects the processed values directly into the fighter struct** and writes the raw stick bytes back into the raw ring for UCF. Optionally it force-resyncs RNG, position and action state.
- A reimplementation can be checked frame by frame the same way: inject pre-frame inputs and compare post-frame fields plus the Frame Start seed. The C++ checksum algorithm above can also be reproduced and compared.

---

## 6. Licensing and policy

- Ishiiruka: GPLv2 (`license.txt`). Slippi mainline Dolphin: GPLv2+ (`COPYING`). slippi-rust-extensions: GPLv2 (`LICENSE`). **slippi-ssbm-asm: GPLv3** (`LICENSE`).
- A port that reuses ASM logic or C++ code must be GPL-compatible. Reimplementing a wire protocol is not by itself copying, but porting the ASM routines (checksum, RNG reseed, UCF and similar) would be derived work under GPLv3.
- slippi-wiki `GETTING_STARTED.md`: "The source code for the matchmaking server is in a private repository and is not currently planned to be open sourced."
- None of the cloned repos (READMEs, wiki, rust crate docs) contain a ToS, an API policy, or any statement permitting or forbidding third-party clients on `mm.slippi.gg`.
- The reverted websocket client says "The identity the client claims. The service verifies it", meaning the server authenticates `uid`/`playKey`.
- Discord or website policies were not in scope and were not checked.
