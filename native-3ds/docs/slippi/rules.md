# Slippi gameplay codes in the port

Slippi Dolphin always runs three Gecko code sections:

- "Required: General Codes"
- "Required: Slippi Recording"
- "Required: Slippi Online"

It also enables the "Recommended" ones by default (`references/slippi-ssbm-asm/netplay.json` → `Output/Netplay/GALE01r2.ini`).

Every replay in `Documents/Slippi` carries its code list (event 0x3D). The list was dumped and each injection address mapped to its source file. The codes that change the simulation are reproduced as C, at the decomp function the injection address falls in.

- **Code:** `port/engine/slippi/rules.c` (helpers), `port/include/slippi_rules.h` (switches, setters), `tools/slippi_edits/online_rules.py` (call sites).
- **UCF 0.84:** the port's own `port/engine/ucf.c` (hooks in `tools/offline_overlays.py`), corrected here.
- **Reference:** Melee Unlocked's Source Port (GPL-3.0-or-later; `melee-native.patch`, `shim/mu_online_rules.c`, `mu_gecko.c`, `mu_replay.c`) for the call-site structure. Slippi's ASM (and a disassembly of the DOL, and of the binary codes recorded in the replays) is the ground truth.

## Switches and setters

| Symbol | Meaning |
|---|---|
| `mp_slippi_rules_enabled` (default 1) | Master switch for every code below. |
| `mp_slippi_rules_mask` (default all bits) | One `MP_SR_*` bit per code. Both are `volatile` globals, so they can be poked with a debugger for A/B runs. |
| `mp_ucf_enabled` | UCF's existing switch; it is still honoured. |
| `mp_slippi_rules_set_frozen_stadium(int)` | Frozen Pokémon Stadium for the next match: the toggle byte that `IngameCheckIfFrozen` reads, which InitOnlinePlay fills from `alt_stage_mode`. It is kept until changed and is 0 at boot. |
| `mp_slippi_rules_set_online(mode, local_player)` | Marks an online match (console scene 0x0208): mode 0 ranked, 1 unranked, 2 direct, 3 teams, 4 party; mode < 0 means offline. Only the two online-only items below read it. |

The coordinator calls both setters before the match.

## The codes

"Oracle" means `tools/slippi/replay_run.py`. Results are in the last section.

### Required: General Codes

| Code (injection) | Ported where | Verified how / doubts |
|---|---|---|
| **Neutral Spawns** (8016e510) | `mp_slippi_neutral_spawn`, called in `fn_8016E2BC` after `Player_80032768` | Line-by-line against `External/NeutralSpawn/NeutralSpawn.asm` and MU. The code's 1P-mode test calls 8016b41c, which "C-Stick in Single Player" (04 8016b480 nop) turns into a constant "no", so it is dropped. The two scene tests (0x021C training, 0x010F target test) and the slot-5 test remain. **Oracle:** `212318` (Battlefield) diverged at frame −123 (spawn x 0 vs −38.8) before the fix and is bit-exact after (552/552 records). |
| **UCF 0.84** (8006b460 pad buffer + 1.0 cardinals, 800c9a44 dashback, 8008e54c SDI, 80093294 shield SDI, 800908f4 tumble, 800998a4 shield drop, 8009a0b8 shield drop extended, 800d65ec DBOOC/SquatRv) | `port/engine/ucf.c` (existing), now behind `MP_SR_UCF` | Disassembled every binary UCF code from `External/UCF 0.84/UCF` and compared each with `ucf.c` and with MU's `mu_ucf_*`. Two differences were fixed, listed after this table. Checked and already equal: the ring and its index, the `qread-1`/wrap read of the raw pad, the Zelda up-B exception, the flick counter, the 2.0f frame test and the Nana record in dashback, the 75²/62²/44² thresholds, the 0.59 SquatRv threshold, the vanilla windows. PlCo `x214` = 1, so `x670 < x214 \|\| ucf` equals UCF's replacement of the tumble window. The shield-drop return address (+8) lands on `li r3,0` in both callers (DOL disassembly), so "return false" is right. `tests/ucf_tests.c` (run with `tools/test_ucf.py`) was extended: 2M random `fmadds` against `fmaf`, the tie case, and the floor test. The ring is reset per match (`mp_ucf_reset`); on console it persists across matches, which affects at most the first 2 input frames of the countdown. |
| **Prevent freeze glitch** (04 801239a8 nop) | `ftNn_Init_80123954`: `x1A5C = NULL` skipped | DOL shows the store at 801239a8 is exactly that line. |
| Costume bound check (8016ded4, 8013c388) | not ported | Costume ids only (CSS/costume parts). The game info block already carries the final costumes. |
| C-Stick in Single Player (8016b480) | not ported | Changes only 8016b41c, which answers "no" in VS (2), debug VS (0x0E) and online (8) anyway. |
| Unrestricted camera / C-stick pan while paused (80452f54…, 8002cb34) | not ported | Pause camera only; no simulation runs while paused. |
| Unlock all, default rules (stock, 4 stocks, 8 min, no items, singles stages), random stage/CSS music, menu, CSS, nametag, memcard, salty runback, debug menu, FoD-in-doubles | not ported (out of scope) | The match rules come from the game info block; no in-match effect. |

The two UCF fixes:

1. **SDI's previous-stick magnitude.** The code computes it with `fmuls` + `fmadds` (one rounding), so `ucf.c` now uses `mp_ucf_fmadds`, a correctly rounded fused multiply-add made with an exact double product and a round-to-odd sum.
2. **Shield drop's platform test.** The code reads the cached floor (`floor.index != -1 && floor.flags & 0x100`); `ucf.c` used to call `mpColl_IsOnPlatform` (the line table).

### Required: Slippi Recording (Common)

| Code | Ported where | Verified how / doubts |
|---|---|---|
| **Initialize Player Data** (80068eec) | `Fighter_Create`: `memset(fp, 0, sizeof(Fighter))` after `HSD_ObjAlloc` | The console zeroes the extended block (0x2600, `ExtendPlayerBlock`); the port allocates and zeroes `sizeof(Fighter)`. |
| **Initialize Stage Data** (801c154c) | `Ground_GetStageGObj` only (not `Ground_801C1A20`): zero 516 bytes | The DOL confirms the hook is in `Ground_GetStageGObj`. A static assert keeps `sizeof(Ground) == 516`. |
| ExtendPlayerBlock (04 800679bc) | not needed | Allocation size only; the recording byte it adds is not simulation. |
| Recording / SendGame* / L-cancel status | not ported | Recording only (the harness has its own). |

### Required: Slippi Online

| Code | Ported where | Verified how / doubts |
|---|---|---|
| **BrawlOffscreenDamage** (8006a880) | `mp_slippi_offscreen_zone` replaces `ifMagnify_802FC998` in `Fighter_8006A360` | Same checks as the ASM and MU: home-run scene 0x0120, dead `221F&0x40`, motion 4/6, then the camera limits. The vanilla precondition `Camera_80031144() == 1.0f` (`game_camera.x2BC`) stays, as on console. |
| **FreezeDeadUpFallPhysics** (800d4c1c InitHitVelocity, 800d4d68 UpdateFallVelocity, 80080e80 UpdateModelPos) | `ft_0D31.c` DeadUpFall Anim case 2 / Phys case 3; `ftdrawcommon.c` inline2 | The velocity lives at fp+0x2348/0x234C and `self_vel` is zeroed. The fall uses a double `fsub`, compare, `stfs`, then `fadds` into 0x2360/0x2364, exactly as the ASM. The draw hook moves only the model (`HSD_JObjSetTranslate`, the same as the ASM, which rejoins at 80080ee4 for the dirty flag) and no longer writes `cur_pos`. **This matters more for the port than for Dolphin:** the port's render callback runs on its own schedule, so the vanilla render→`cur_pos` feedback is not deterministic here. **Not covered by the oracle:** no replay has a screen KO (action 6). |
| **WhispyBlowDirFix** (8008653c) | `ftLib_800864A8`: a fighter with motion ≤ 0xB adds nothing, after `ftLib_800866DC` (as the hook sits after that call) | DOL check: the branch target 8008655c is the add, so skipping equals adding 0. |
| **DesyncProofBGTransformations** (8021aae4) | `grLast_8021AAB0`: save/restore `*seed_ptr` around `grLast_8021B2E8` | Straight from the ASM. **Oracle:** FD replays `212149`, `205212`. |
| **IngameCheckIfFrozen** (801d457c) | `grStadium_801D4548`: `gm_8018841C() \|\| frozen` | Setter `mp_slippi_rules_set_frozen_stadium`. All available replays have frozen-PS = 0. |
| **StadiumFileLoad** (800165ac) + **GrPsxIsValid** (801d4760) | `grStadium_801D4548` cases 0/1: synchronous `lbFile_8001668C` + `grDatFiles_801C6478`; "loaded" = the map archive's first word is non-zero | The recorded code (disassembled from the replay) is unconditional inside `lbFile_80016580`. Its only in-match async caller is this one; the others are the synchronous wrappers that Slippi's EXIFileLoad codes redirect. The vanilla load callback (which clears `xC4_b1`) never runs, as on Slippi. Doubt: none known; it needs a PS replay that transforms (`211748`). |
| CustomZeroBuffer (801c65c8) | **not ported** | The only behavioural change is skipping `lbArchive_80016EFC` for `unk8 == 0` archives when freeing (rollback safety). It changes memory management only, and skipping a destructor in the port risks leaking stage memory every match. MU also leaves it out. |
| ChangeJumbotronText (801d2d38) | not ported | Text only ("Frozen"). |
| **Stadium screen modes 7/8** (port overlay, not a Slippi code) | `grStadium_801D2528`: the overlay's 7/8 → 0/1 swap is disabled under `MP_SR_PS_LIVE_VIEW`; live-view images are cleared to black | The port's `engine_overlays.py` replaced the live-view modes with the text display (no EFB capture on 3DS). That changes the monitor's random timers (`randi_between*`, the chooser), so it consumes RNG differently from every console. The state machine is vanilla again; only the picture differs (black while a live view would show). |
| **PSCameraIndependentMonitor** (801d24fc) | `mp_slippi_ps_monitor_ok`, replacing `grStadium_801D32D0`'s result in case 8 (the call still runs) | Box ±120 / 80 / −20 from the ASM. |
| **PreventWobbling** (800db880, 800dbbd4 init; 8008f090 check) | `mp_slippi_wobble_reset` in `fn_800DB790`/`fn_800DBAE4`; `inlineB2_wobble` for the third inlined copy in `ftCo_8008EC90` only (DOL: three copies, the hook is the one after the input clear) | The top-level (non-Ranked) files are the ones in the code set. `AS_218_CatchCut` receives r4 = the previous id (`gm_8016B168` does not touch r4), so `prev != 0` is passed; `ftCo_800DA698` tests the full word. The rtoc constants (0.0, 1.0) were read from the DOL. No Ice Climbers replay is available, so this is checked by reading only. |
| **NanaDeterminism** (800ac5b8) | `ftCo_800AC5A0`: `stick_x = stick_y = 0` | The DOL shows no calls between the hook and the uses, so the registers stay 0, the same as initialised locals. It cannot be switched off: the "off" behaviour is undefined in C. |
| **FastForward/DynamicsFix** (8009e090) | `mp_slippi_dynamics_fix` at the end of `ftCo_8009DD94`: set up dirty, non-user-defined joint matrices of the fighter's tree | Not tagged gameplay, but it removes a render→simulation dependency (the shadow render set these matrices up), and the port's renderer does not follow the console's order. r30 = fp was confirmed in the DOL. |
| **LGLExceededGameEnd** (802f70c4) | `mp_slippi_lgl_timeout_message` in `ifStatus_802F7034`'s timeout branch | Online singles only (needs `set_online`). It picks Success/Failure for a > 45 ledge-grab timeout and holds GAME! for 0xFD frames (`StartMeleeRules+0xD`). The subtext is not drawn. The LGL winner itself is decided by Slippi's results code (`Slippi Online Scene/main.asm`), which is the coordinator's job. |
| **InitOnlinePlay: clear held A** (8016e748) | `mp_slippi_rules_match_start` at the top of `fn_8016E730` | Online only: zeroes `HSD_PadCopyStatus[0..3].button` so a held A cannot swap Zelda/Sheik at spawn. The per-frame RNG reseed, pad exchange and frame gating are left to the coordinator. |
| PreventCharacterCrowdChants (04 80321d70) | not ported | `un_80321EBC` has no RNG; only a crowd sound and a player stat change. |
| Sound/Music/PreventFileAlarms, Rumble, Pause, Teams/PreventDeadStranding, rollback/FFW loops, EXIFileLoad, m-ex.bin, PD+VB | not ported | Audio, rollback machinery, file transport, polling, or teams-only. m-ex (803753b4…) is a pass-through for vanilla content: no vanilla-character replay diverges structurally. |
| Preload Stadium Transformations | **not ported** | It is in `console_core.json` only. None of its addresses (801d460c, 801d14c8, …) is in any replay's code list, so Slippi netplay does not run it. |

### Recommended codes enabled by default

| Code | Decision |
|---|---|
| **Lagless FoD** (801cbb90 + 04 writes at 801cbe9c/801cbef0/801cbf54/801cc8ac/801ccdcc/801cd250/80390838) | Not ported, judged visual-only. Each write was mapped: the reflection camera and object, the star sprite object, the jet scale, the map animation start of gobj 3, and a joint flag byte in the file. None touches RNG or collision lines. Slippi ships it as a user-toggleable "Recommended" code, which would desync if it changed the simulation. **Oracle:** the FoD replays (`212326` Direct, `212025`, `212049`, `221434`) show no structural divergence without it. Doubt: gobj 3's map animation drives joints 1/2, which gobj 3's update copies onto gobj 2 (the platforms' reflections, hidden when y < 0); if any of those carried collision, positions would drift. None has shown up so far. |
| Normal Lag Reduction, Apply Delay to all In-Game Scenes | Timing and polling, not simulation. |

### Codes outside the standard set

The 2026-09 replays also carry three user codes: C2 800748dc (a costume-parts swap), 04 80028d64 and 04 8005a2e8. They are visual or audio and are not ported.

## How the codes were identified

The method for each kind of code:

- **Gecko list:** each replay's 0x3D event was parsed (C2/04/06 code types).
- **Address mapping:** each injection address was mapped to the `# Address:` line of the ASM sources.
- **Binary codes:** their `.long` bodies (UCF, the recorded StadiumFileLoad) were disassembled with capstone (`.toolchain/disassembly-python`).
- **Vanilla code at each hook:** read from `assets/GALE01/sys/main.dol`, to confirm which inlined copy, return path or register value a hook sees.

## Oracle results

Before = commit 3d18c2c (no codes), after = this branch. "Structural" means pre-frame seed and action, and post-frame action, stocks and percent. Positions drift by ULPs until the float work lands.

| Replay | Stage | Players | Frames | Before: first structural divergence (bit-exact records) | After |
|---|---|---|---|---|---|
| 20260622T205212 | FD | H/H | 8 | none (32/32) | none (32/32) |
| 20260622T211748 | PS | H/H | 4421 | none (8850/17684) | none (9025/17684) |
| 20260622T211912 | YS | H/H | 3919 | none (7547/15676) | none (7547/15676) |
| 20260622T212025 | FoD | H/H | 855 | none (3099/3420) | none (3099/3420) |
| 20260622T212049 | FoD | H/H | 887 | none (3295/3548) | none (3295/3548) |
| 20260622T212111 | YS | H/H | 756 | none (2733/3024) | none (2733/3024) |
| 20260622T212129 | BF | H/H | 795 | action @ −27, seed @ −22 (2/3180) | **none (3180/3180)** |
| 20260622T212149 | FD | H/H | 933 | none (2776/3732) | none (2776/3732) |
| 20260622T212226 | DL | H/H | 179 | action @ −32, seed @ −14 (0/716) | **none (716/716)** |
| 20260622T212233 | PS | H/H | 128 | none (256/512) | **none (512/512)** |
| 20260622T212240 | FD | H/H | 130 | none (520/520) | none (520/520) |
| 20260622T212304 | YS | H/H | 153 | none (518/612) | none (518/612) |
| 20260622T212311 | PS | H/H | 144 | none (288/576) | **none (576/576)** |
| 20260622T212318 (Direct) | BF | H/H | 138 | action @ −25, seed @ −22 (0/552) | **none (552/552)** |
| 20260622T212326 (Direct) | FoD | H/H | 627 | none (1638/2508) | none (1638/2508) |
| 20260824T201814 | BF | H/CPU | 1140 | action @ 30, seed @ 40 (0/4560) | seed @ 728 (3696/4560) |
| 20260924T221434 | FoD | H/CPU | 1139 | seed @ 128 (2827/4556) | seed @ 128 (2827/4556) |
| 20260924T221458 | stage 4 | H/CPU | 1129 | seed @ 69 (2353/4516) | seed @ 69 (2353/4516) |
| 20260924T221525 | stage 11 | H/CPU | 3362 | seed @ 69 (2188/13448) | seed @ 69 (2188/13448) |

Reading it:

- **Neutral spawns.** Every divergence from frame −123 to 40 in "before" was the spawns (BF, DL, PS, and BF vs CPU). All of these are now fully bit-exact or structurally clean.
- **Human vs human.** No replay of two humans diverges structurally after the change. That covers whole matches with deaths and respawns on PS (4421 frames, including a Stadium transformation chosen at frame 3683, recorded event 0x41), YS (3919), FD (933, three deaths) and FoD (four replays, run without Lagless FoD).
- **Against a CPU.** Every vs-CPU replay still diverges in the pre-frame seed. Each time it happens after the first float mismatch (ULP drift: frames 673, 41 and −118). The CPU's AI reads positions, so this is attributed to the float work in progress, not to these codes. It needs re-checking once floats are bit-exact.
- **The June replays are online.** All 15 have two human players and scene 0x0208, so they are online matches, not vs CPU; only the 2026-08/09 ones are vs CPU. The harness runs them offline, without `mp_slippi_rules_set_online`. That is fine here: no Zelda/Sheik, no timeout.
- **Not exercised by any replay:** a screen KO (DeadUpFall, action 6; the replays' star KOs are DeadUpStar, action 4, where both builds already match), Ice Climbers beyond the first 107 frames (hardware run 8's replay is now bit-exact after the u8 conversion fix in `determinism.md`; PreventWobbling and the freeze glitch are still unexercised), Whispy, an FD background transformation that is cut short, and frozen PS. Those were checked by reading the code against the ASM and the DOL only.

Harness notes:

- **Small flushes.** With the harness's 64 KiB output flushes (and also with 16 KiB ones), every replay longer than about 700 frames stopped advancing after a few flushes on Azahar. There was no end marker, so the run timed out with only partial output; this was seen with and without these codes, and with the file kept open between writes. `replay.c` now flushes every 10 records, and every replay plays to its end. A run may still miss the final end marker now and then (reported as `timeout`), but its records are complete.
- **One emulator at a time.** Do not run two oracle runs at once. Killing the shell running a batch (TaskStop) leaves its Python child and emulator running.
