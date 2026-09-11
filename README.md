# Melee for New Nintendo 3DS

An experimental port of **Super Smash Bros. Melee running natively on New Nintendo 3DS hardware**, built from the [doldecomp/melee](https://github.com/doldecomp/melee) decompilation. The original game engine runs as ARM code, with a 3DS graphics, audio and controller layer.

The aim is to make Melee enjoyable on a handheld while keeping its original gameplay and adding features suited to the 3DS. Full matches have been played on a physical New 3DS. **The current milestone is update 15; performance varies, and stable 60 FPS has not been achieved.**

Update 15 overlaps game simulation with GPU rendering, uses shorter material programs, uploads only referenced bone matrices, and shares overlapping geometry snapshots. It also fixes a reproduced freeze in Kirby’s copied-model visibility. Stereo rendering and casual-play regressions passed in Azahar; the 60 FPS competitive and 30 FPS casual targets still require physical-console validation. [Changes and validation](native-3ds/docs/STEREO_PERFORMANCE_UPDATE_15.md).

## Project features

- **Touch-screen match dashboard**, with FPS hidden by default. Tap FPS to toggle it, VIEW to change projection, or CONTROLS for the guide. The guide does not pause gameplay.
- Original menus, character selection, Training and Versus matches against CPU opponents, with music and sound effects.
- **Native stereoscopic 3D**, controlled by the slider, in gameplay and perspective menus—including a Ready to Fight ribbon that pops forward.
- **4:3 by default**, with an optional expanded view in gameplay and menus that reveals more of the sides without stretching.
- All characters and stages unlocked, offline tournament settings with pause enabled, and native UCF 0.84 input fixes.
- Ongoing loading and rendering optimizations, plus optional audited Diet Melee scenery for five stages.

## What you need

- A **New Nintendo 3DS or New Nintendo 3DS XL** with homebrew already installed and access to **Homebrew Launcher**. New 2DS XL is a 2D target but has not been physically tested. Original 3DS/3DS XL/2DS models are not supported.
- Your own **US Melee v1.02 disc dump** (`GALE01`, revision 2), in uncompressed `.iso` or `.gcm` format. Other regions, revisions and modified base images are not accepted.
- About **1.5 GB of free SD space** for the extracted game files.
- To build: a **64-bit Windows PC**, **Git**, **Python 3.11 or newer**, several GB of free disk space and an internet connection for the build tools.

**This repository contains source code and build tools. There is currently no prebuilt game download.** Game assets, fonts and firmware are not included; the build uses your own disc dump. The steps below assume your console's homebrew setup is already working.

## Set up the game

### 1. Build on your PC

Open PowerShell and run:

```powershell
git clone --branch 3ds https://github.com/2gifts/melee-3ds.git
cd melee-3ds/native-3ds
python tools/bootstrap.py --portable-windows
python tools/assets.py extract "C:\path\to\Melee-US-1.02.iso" assets/GALE01
python tools/assets.py fonts assets/GALE01/sys/main.dol build/generated
python tools/build_game.py --release
python tools/package_native.py
```

Replace the example ISO path with the location of your own dump. Bootstrap downloads the pinned compiler and SDK tools automatically. Extraction checks the supported game version. If your dump is an RVZ, convert it to ISO with Dolphin first.

The finished local SD package is in **`melee-3ds/native-3ds/dist/native-alpha/`**. Run the asset and font extraction steps only once; they deliberately refuse to overwrite existing outputs. [Detailed build instructions](native-3ds/README.md#build-and-install).

### 2. Copy to your SD card

1. Power off the console and insert its SD card into your PC.
2. Copy the **`3ds` folder inside `dist/native-alpha/`** to the **root of the SD card**, merging it with the existing `3ds` folder.
3. Safely eject the card, return it to the console and power on.

Your SD card should contain:

```text
SD:/3ds/melee/melee.3dsx
SD:/3ds/melee/files/          ← all extracted game files
SD:/3ds/melee/visuals/        ← optional prepared Diet scenery
```

Copy the whole generated package for the first installation; the executable alone does not contain the game files. Optional scenery setup is explained in the [Diet Melee instructions](native-3ds/README.md#optional-diet-scenery).

### 3. Launch and play

Open **Homebrew Launcher** using your existing homebrew setup and select **melee**. This version launches as a `.3dsx` app; a standalone HOME Menu app with its own icon is planned.

At the title screen, press START. Choose **VS Mode → Melee**, select your fighter and a CPU opponent, press START, and pick a stage. For practice, choose **1-P Mode → Training**.

Audio uses the DSP support supplied by your homebrew setup or your existing `SD:/3ds/dspfirm.cdc`; firmware is not bundled with this project.

## Controls

| Input | Action |
|---|---|
| Circle Pad | Move |
| A / B | Attack / special |
| X / Y | Jump |
| L / R | Shield |
| ZL / ZR, or L + A | Grab |
| C-stick | Directional attack |
| START | Confirm / Training menu; pause (enabled by default) |
| 3D slider | Adjust depth; fully down selects 2D |
| Hold ZL + ZR, then press SELECT | Toggle 4:3 / expanded view |
| Touch FPS / VIEW / CONTROLS | Toggle rendered FPS, projection, or the control guide |
| SELECT | Return to Homebrew Launcher |

Versus defaults to **4 stocks, 8 minutes, items off, team attack on and pause on**. Rules can be changed in the menus for the current session.

## Current limitations and updates

Only one physical player is mapped. Multiplayer, online/rollback, replay recording, persistent saving and optional movies are unfinished. Some materials, shadows and reflections remain approximate. Stereo adds rendering work; lowering the slider fully provides the best available performance. The lower screen reports rendered FPS separately from simulation updates.

For source updates, run `git pull --ff-only`, rebuild with `python tools/build_game.py --release`, and rerun `python tools/package_native.py` from `native-3ds/`. Back up your previous SD executable, then replace `SD:/3ds/melee/melee.3dsx`. Keep the existing `files` directory and any optional visuals unless an update specifically changes them.

Bug reports and contributions are welcome through this repository's [Issues](https://github.com/2gifts/melee-3ds/issues) and pull requests. Include your console model, source commit, stage/fighters, display mode and steps to reproduce. See the [current port status](native-3ds/docs/PORT_STATUS.md) and [contribution notes](native-3ds/CONTRIBUTING.md) for more detail.

## Credits

This project builds on **doldecomp/melee and its contributors**, devkitPro's homebrew libraries and tools, Diet Melee, UCF and Project Slippi's offline/input work, and native Mario 64 3DS ports used as stereo references. It was developed iteratively with OpenAI Codex and physical testing on a New 3DS.

The upstream decompilation source and history are preserved. Port code lives in `native-3ds/` and pins upstream commit `039c4bf4ca33338c35d21901ad19b7ede19d19ad`. This is an unofficial community project.

[Original decompilation README](.github/UPSTREAM.md) ·
[3DS contribution notes](native-3ds/CONTRIBUTING.md) ·
[Credits and third-party notices](native-3ds/docs/THIRD_PARTY_NOTICES.md) ·
[Source terms](native-3ds/LICENSE-SCOPE.md)
