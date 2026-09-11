# Melee for New Nintendo 3DS

A work-in-progress native ARM port of Super Smash Bros. Melee, built from the [doldecomp/melee](https://github.com/doldecomp/melee) decompilation. This is native game code with a 3DS graphics/audio/platform layer, not a Dolphin emulator build.

**Current milestone: update 15. Playable on a New Nintendo 3DS, with native stereoscopic 3D. Performance varies substantially; stable 60 FPS has not been achieved.** Full matches have been played on a physical New 3DS. This remains an experimental community development build.

Update 15 overlaps game simulation with GPU rendering, uses shorter material programs, uploads only referenced bone matrices, and shares overlapping geometry snapshots. It also fixes a reproduced freeze in Kirby’s copied-model visibility. Stereo rendering and casual-play regressions passed in Azahar; the 60 FPS competitive and 30 FPS casual targets still require physical-console validation. [Changes and validation](docs/STEREO_PERFORMANCE_UPDATE_15.md).

## What works

- **Touch-screen match dashboard**, with FPS hidden by default. Tap FPS to toggle it, VIEW to change projection, or CONTROLS for the guide. The guide does not pause gameplay.
- Original menus, character selection, Training and Versus against a CPU, including results and rematches.
- Native slider-controlled stereo in gameplay and perspective menus, with a forward CSS Ready to Fight ribbon. Lowering the slider fully selects 2D.
- Centered 4:3 by default. Optional expanded 5:3 reveals more horizontal world and menu scenery without stretching; HUD/menu proportions stay intact.
- Music, sound effects, all character/stage unlocks, offline tournament settings with pause enabled and native UCF 0.84 input fixes.
- Background disc reads, bounded menu/texture/geometry caches, conservative off-screen rejection, and reduced stereo GPU state work.
- Optional audited Diet scenery for Fountain of Dreams, Yoshi's Story, Battlefield, Final Destination and Dream Land, prepared locally from your own files.

Update 9 also fixes closed-slot portrait leakage and the reproduced Jungle Japes distant-hit problem. [Implementation and test details](docs/MENU_STEREO_COLLISION_UPDATE_9.md) distinguish successful recovery of captured invalid transforms from the still-unidentified operation first producing them.

## Requirements

- **New Nintendo 3DS or New Nintendo 3DS XL**, with existing homebrew support and Homebrew Launcher. New 2DS XL is a 2D target but has not been physically tested. Old 3DS/2DS models are not supported.
- Your own **US Melee v1.02 / GALE01 revision 2** disc dump in uncompressed ISO/GCM format. PAL, Japanese, v1.00/v1.01 and modified base images are not accepted. Convert RVZ to ISO with Dolphin before extraction.
- Approximately 1.5 GB of SD space for extracted game data.
- For the tested build route: **64-bit Windows, Git, Python 3.11 or newer**, several GB of free disk space, and an internet connection for the pinned tools. Python's standard library is sufficient for the release build. Native Linux/macOS game builds are not currently documented or validated.

This repository supplies source and build tools. **It does not include an ISO, extracted game files, fonts, DSP firmware, Diet stage data, or prebuilt game binaries.** The local build extracts its two font resources from your own DOL; generated packages are for your own SD card and are excluded from Git.

## Build and install

Clone this fork's `3ds` branch and open PowerShell at the repository root. Then:

```powershell
cd native-3ds
python tools/bootstrap.py --portable-windows
python tools/assets.py extract "C:\path\to\Melee-US-1.02.iso" assets/GALE01
python tools/assets.py fonts assets/GALE01/sys/main.dol build/generated
python tools/build_game.py --release
python tools/package_native.py
```

Bootstrap downloads checksum/commit-pinned LLVM, the official devkitPro ARM libraries and SDK headers, Picasso, and 3dsxtool. It also creates a separate pinned source checkout under `upstream/melee/`. This preserves the decompilation at the fork root and keeps generated compatibility overlays out of the original source.

The extractor validates the supported DOL SHA-1, `08e0bf20134dfcb260699671004527b2d6bb1a45`, disc bounds and file paths. Extraction/font generation refuses to overwrite existing outputs. Run those two steps once; after source updates, rerun the build and packaging commands.

Copy the **`3ds` folder inside `native-3ds/dist/native-alpha/`** to your SD root. The resulting layout is:

```text
SD:/3ds/melee/melee.3dsx
SD:/3ds/melee/files/...
SD:/3ds/melee/visuals/...    (optional)
```

Launch **melee** from Homebrew Launcher. Audio uses libctru's normal DSP discovery through the launcher or your existing `SD:/3ds/dspfirm.cdc`. No firmware is bundled. A standalone HOME Menu app/icon is planned; this milestone uses `.3dsx`.

For subsequent updates, replace the executable and any deliberately changed optional visuals; keep the original `files` directory. Back up the previous executable before replacing it.

## Play

Choose **VS Mode → Melee**, select your fighter and a CPU opponent, press START, and select a stage. Training is under **1-P Mode → Training**.

Default Versus rules are 4 stocks, 8 minutes, items off, team attack on and pause on, with the six singles stages enabled for Random. Rules can be changed for the current session. There is no persistent memory-card save.

| 3DS input | Action |
|---|---|
| Circle Pad | Move |
| A / B | Attack / special |
| X / Y | Jump |
| L / R | Shield |
| ZL / ZR, or L + A | Grab |
| C-stick | Directional attack |
| START | Confirm / Training menu; pause (enabled by default) |
| Hold ZL + ZR, press SELECT | Toggle 4:3 / expanded view |
| 3D slider | Depth; fully down selects 2D |
| Touch FPS / VIEW / CONTROLS | Toggle rendered FPS, projection, or the control guide |
| SELECT | Return to Homebrew Launcher |

## Optional Diet scenery

Use the official [Diet Melee](https://diet.melee.tv/download/) Classic patcher on a separate copy of your own US v1.02 image. The audited Classic 1.0.2/1.0.3 stage inputs are supported; other revisions deliberately fail the hash checks. Keep the original ISO/extraction as the base game.

Using Dolphin's filesystem extraction, place these five files from the patched image under `native-3ds/references/diet-melee/files/`: `GrIz.dat`, `GrSt.dat`, `GrNBa.dat`, `GrNLa.dat`, `GrOp.dat`. Then, from `native-3ds`:

```powershell
python tools/audit_diet_fountain.py
python tools/prepare_diet_stages.py
```

Copy the audited `references/diet-melee/files/GrIz.dat` and the four prepared `.dat` files from `build/visuals/` to `SD:/3ds/melee/visuals/`. The preparation restores required original control data and animations; do not substitute arbitrary Diet archives directly into `files/`. No Diet executable or game data is distributed here. [Stage audit details](docs/OFFLINE_PERFORMANCE_UPDATE_4.md).

## Limits and testing

Only one physical player is mapped. Multiplayer, online/rollback, replay recording, memory-card saving and optional movies are unfinished. UCF/offline defaults do not make this an official Slippi build. Materials, some shadows and reflections remain approximate. Frame rate varies with stage, fighters and stereo; 2D avoids the additional eye's drawing work. The lower screen reports **rendered FPS separately from simulation updates**.

The update 9 emulator regression covered all 29 stage selections and 25 character icons, plus 120 simulation seconds of Marth/DK combat on each of Poké Floats and Jungle Japes, stereo pixel references, cache pressure, turnips, audio and protected-memory checks. Emulator results are not physical FPS measurements. [Current status](docs/PORT_STATUS.md), [hardware history](docs/HARDWARE_TESTS.md) and the implementation notes preserve the scope and limits of those checks. Paths to `build/` evidence in historical notes refer to local development records; raw logs, dumps, screenshots and game textures are not published.

For bug reports, include the source commit, console model, launcher/Luma version, 2D/3D and aspect mode, stage/fighters, and reproduction steps. Review `SD:/3ds/melee/game.log` for personal information before sharing it. Do not attach disc data, extracted assets or firmware. [Contribution notes](CONTRIBUTING.md).

## Credits and source terms

The decompilation and original-engine work come from **doldecomp/melee and its contributors**. The platform layer uses devkitPro's libctru, Citro3D/Citro2D, Picasso and 3dsxtool. Credit also goes to Diet Melee, UCF's authors and Project Slippi for the referenced offline/input work, and the native Mario 64 3DS ports for stereo implementation references.

This port was developed iteratively with OpenAI Codex and physical testing on a New 3DS. It is not an official Nintendo, HAL, doldecomp, Slippi or devkitPro release. See [third-party notices](docs/THIRD_PARTY_NOTICES.md) and [license scope](LICENSE-SCOPE.md); upstream and third-party terms are preserved.
