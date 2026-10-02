# Slippi Direct on 3DS: proof of concept

Experimental fork of the 3DS port. Goal: one 1v1 Slippi **Direct** (connect code) match
between this port on a New 3DS and stock Slippi Dolphin on a PC. Nothing here goes back
into the main port.

## Why it can work

- Slippi peers only exchange controller inputs (ENet/UDP, see `protocol_notes.md`). The
  PC predicts and rolls back on its own, so a 3DS peer that never rolls back (lockstep:
  simulate frame F only after the remote input for F arrived) is protocol-compatible.
- The hard requirement is a bit-identical simulation. Melee Unlocked's Source Port
  (github.com/Hero88go/melee-unlocked, GPL-3.0-or-later; same decomp commit 039c4bf)
  shows the decomp can match Slippi replays bit for bit, frame by frame.
- GameCube rounding is reproducible on ARM:
  - single-precision `fmadds` = `(float)((double)a*b + c)`;
  - `sqrtf` = frsqrte table + 3 Newton steps, with a fused double `fnmsub`;
  - `fres`/`frsqrte` come from Dolphin tables.

## Work areas

| Area | Where | Status |
|---|---|---|
| Replay playback + per-frame state dump (determinism oracle) | `port/engine/slippi/replay.c`, `tools/slippi_edits/base_replay.py`, `tools/slippi/replay_*.py` | done |
| Bit-exact maths (contraction, sqrtf, trig, PSMTX, FPSCR) | `docs/slippi/determinism.md` | done: the Direct replay matches bit for bit (except 2 known spawn-frame playback ULPs) |
| Slippi online gameplay codes (UCF parity, zero-init, LGL, ...) | `tools/slippi_edits/online_rules.py`, `docs/slippi/rules.md` | in progress |
| Network: soc:U, ENet, matchmaking, Slippi P2P protocol | `port/3ds/slippi/`, `docs/slippi/network.md` | done; tested vs fake MM/peer (PC, Azahar) |
| Online frame driver (lockstep pads, delay, checksum, match start, RNG reseed) | `port/engine/slippi/online.c`, `tools/slippi_edits/online.py` | done; a full match runs in Azahar vs the fake peer (`tools/slippi/online_game_test.py`) |
| Boot menu (connect code via swkbd, character, colour, stage, delay) | `port/3ds/slippi/slippi_boot_ui.c` | done (needs a hardware look) |
| Real test: 3DS vs stock Slippi Dolphin through mm.slippi.gg | needs a second Slippi account for the 3DS | todo |

## Test assets

- `C:\Users\kirby\Documents\Slippi\**\*.slp` (Slippi 3.19.1). All were recorded with the
  netplay code set. Two are Direct online games (2026-06-22 21:23); the rest are
  offline vs CPU.
- Melee Unlocked's sources: `C:\Users\kirby\Melee Decomp\slippi-research\melee-unlocked`
  (shims in `sourceport/game/shim`, decomp patch `sourceport/patches/melee-native.patch`,
  Slippi netcode `port/runtime/hle/slippi_*.cpp`).
