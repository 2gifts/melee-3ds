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
| Replay playback + per-frame state dump (determinism oracle) | `port/engine/slippi/replay.c`, `tools/slippi_edits/replay.py`, `tools/slippi/` | in progress |
| Bit-exact maths (contraction, sqrtf, trig, PSMTX, FPSCR) | `tools/slippi_edits/determinism.py`, IR pass in `engine_build.py` | in progress |
| Slippi online gameplay codes (RNG reseed, UCF parity, zero-init, ...) | `tools/slippi_edits/online_rules.py`, `port/engine/slippi/` | todo |
| Network: soc:U, ENet, matchmaking, Slippi P2P protocol | `port/3ds/slippi/` | in progress |
| Online frame driver (lockstep pads, delay, checksum, match start) | `port/engine/slippi/online.c` + edits | todo |
| Minimal UI (connect code via swkbd, character pick, status) | bottom screen | todo |

## Test assets

- `C:\Users\kirby\Documents\Slippi\**\*.slp` (Slippi 3.19.1). All were recorded with the
  netplay code set. Two are Direct online games (2026-06-22 21:23); the rest are
  offline vs CPU.
- Melee Unlocked's sources: `C:\Users\kirby\Melee Decomp\slippi-research\melee-unlocked`
  (shims in `sourceport/game/shim`, decomp patch `sourceport/patches/melee-native.patch`,
  Slippi netcode `port/runtime/hle/slippi_*.cpp`).
