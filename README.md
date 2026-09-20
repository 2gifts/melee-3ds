# Melee for New Nintendo 3DS

Super Smash Bros. Melee, running natively on a homebrewed **New Nintendo 3DS**. This fork of [doldecomp/melee](https://github.com/doldecomp/melee) brings the original game engine to ARM, with stereoscopic 3D, handheld controls, and a Melee-inspired touch-screen companion.

Play Versus against CPUs, practice in Training, and explore Melee on a different kind of handheld. The port aims to preserve the original mechanics while adapting the presentation to the 3DS. **Development is complete for now at a playable community milestone. Performance varies, and this is not a locked-60-FPS or fully compatible replacement for the GameCube game.**

## Features

- Native stereoscopic **3D in gameplay and perspective menus**, adjusted with the system slider.
- **4:3 by default**, with an optional expanded view that reveals more of the scene without stretching it.
- A **touch-screen dashboard** with portraits, stocks, and damage for up to four fighters, plus menu guidance and an optional FPS display.
- Original music and sound effects, all characters and stages unlocked, **UCF 0.84 input fixes**, and editable tournament-friendly defaults.
- **HOME Menu launch through an installable CIA**, with a disc icon and a Final Destination/Fox diorama. Homebrew Launcher is also supported.
- Optional **Diet Melee scenery** for five stages, simplifying visuals while retaining the original stage gameplay and collision.

## Performance to expect

These are approximate observations on a physical New 3DS, not guarantees or emulator benchmarks:

| Scenario | Typical experience |
|---|---|
| Lighter 1v1 matches on Diet stages, 2D | Roughly **55–60 FPS** |
| Lighter 1v1 matches on Diet stages, 3D | Often **around 50–55 FPS**, with heavier matchups/effects dipping into the 40s |
| Large casual stages with four fighters, 3D | Can fall into the **teens or low 20s** |

Stages, fighters, items, and effects all matter. For the smoothest experience, use 1v1 matches, prepared Diet scenery, and 2D. **Neither stable 60 FPS in 3D nor a 30 FPS minimum in crowded matches is achieved.** The optional display distinguishes rendered FPS from game updates; a 60 Hz update reading does not mean 60 rendered frames.

## What you need

- A homebrewed **New Nintendo 3DS or New Nintendo 3DS XL**, with custom firmware and **FBI** for CIA installation. New 2DS XL is also targeted in 2D, but has not been physically tested. Original 3DS, 3DS XL, and 2DS models are unsupported.
- Your own **US Melee v1.02 disc dump** (`GALE01`, revision 2).
- About **1.5 GB of free SD space**, plus space for the installed application.

**This repository distributes source and build tools, not a prebuilt CIA or game data.** Build with your own disc dump using the [build guide](native-3ds/README.md). CIA packaging additionally requires locally prepared HOME Menu artwork; the guide explains that requirement. No ISO, extracted assets, fonts, or firmware are included.

## Install with FBI

Once you have your locally built `melee-3ds.cia` and extracted SD package:

1. Power off the console and put its SD card in your computer.
2. Copy the generated **`3ds` folder from `native-3ds/dist/native-alpha/`** to the SD root, merging folders. The game files must end up in **`SD:/3ds/melee/files/`**.
3. Copy **`melee-3ds.cia`** to **`SD:/cias/`**; create that folder if needed.
4. Safely eject the card, return it to the console, and power on.
5. Open **FBI → SD → cias → melee-3ds.cia → Install CIA**, and confirm.
6. Return to the HOME Menu, unwrap the Melee icon if prompted, and launch it.

**The CIA does not contain the game assets.** Keep `SD:/3ds/melee/files/` on the card after installation. Optional prepared scenery goes in `SD:/3ds/melee/visuals/`. [Installation, updating, and troubleshooting](native-3ds/docs/HOME_MENU.md).

For Homebrew Launcher, select **melee** after copying the same SD package. Updating `melee.3dsx` updates only that launch method; an installed HOME Menu version needs its CIA reinstalled through FBI.

## Playing

Choose **VS Mode → Melee**, select your fighter and CPU opponents, press START, and pick a stage. Training is under **1-P Mode → Training**. Only **one human player** is supported; the other fighters are CPUs.

Versus starts with **4 stocks, 8 minutes, items off, team attack on, and pause enabled**. Change the rules in the usual menus for casual play. Settings reset when the application restarts.

| Input | Action |
|---|---|
| Circle Pad | Move |
| A / B | Attack / special |
| X / Y | Jump |
| L / R | Shield |
| ZL / ZR, or L + A | Grab |
| C-stick | Directional attack |
| START | Confirm / pause / Training menu |
| 3D slider | Adjust depth; fully down selects 2D |
| Touch FPS / VIEW / CONTROLS | Toggle FPS, change the view, or show controls |
| Hold ZL + ZR, then press SELECT | Toggle 4:3 / expanded view |
| SELECT | Exit to HOME or Homebrew Launcher |

The touch-screen controls guide does not pause a match.

## Scope and credits

This is an unofficial homebrew port. There is **no multiplayer connection, Slippi rollback, replay recording, or persistent memory-card saving**. Optional movies are skipped, some graphics are simplified or approximate, and single-player modes have less coverage than Versus and Training. Bugs may remain. See [project status](native-3ds/docs/PORT_STATUS.md).

Thanks to **doldecomp/melee and its contributors**, devkitPro, Diet Melee, the UCF authors and Project Slippi, and the Mario 64 3DS ports used as references. Development used OpenAI Codex alongside repeated testing on a physical New 3DS.

The original decompilation source and history are preserved; the port lives in **`native-3ds/` on the `3ds` branch**. Community forks and contributions are welcome, although further development is not currently planned.

[Upstream README](.github/UPSTREAM.md) · [Contributing](native-3ds/CONTRIBUTING.md) · [Credits and notices](native-3ds/docs/THIRD_PARTY_NOTICES.md) · [Source terms](native-3ds/LICENSE-SCOPE.md)
