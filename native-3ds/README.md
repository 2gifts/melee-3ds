# Building Melee for New 3DS

[Project overview, performance, and controls](../README.md) · [CIA installation](docs/HOME_MENU.md)

The documented build route uses **64-bit Windows, Git, Python 3.11 or newer**, several GB of free disk space, and an internet connection. You need your own unmodified **US Melee v1.02 ISO/GCM**. Other regions and revisions are unsupported; convert an RVZ to ISO before extraction.

## Build the game and SD files

In PowerShell:

```powershell
git clone --branch 3ds https://github.com/2gifts/melee-3ds.git
cd melee-3ds/native-3ds
python tools/bootstrap.py --portable-windows
python tools/assets.py extract "C:\path\to\Melee-US-1.02.iso" assets/GALE01
python tools/assets.py fonts assets/GALE01/sys/main.dol build/generated
python tools/build_game.py --release
python tools/package_native.py
```

Replace the example ISO path with your own. Bootstrap obtains the pinned compiler, SDK, and upstream source. Extraction verifies the game revision and supplies the game's font resources. Run the extraction steps only once; they refuse to overwrite existing outputs.

The SD package is **`dist/native-alpha/`**. Copy its `3ds` folder to the SD root. This is ready for Homebrew Launcher. Keep the complete `files` folder; the executable alone is not the game.

### Two ways to play: everything unlocked or a fresh save

There are two builds, and you can install either or both. They share the game files but keep separate saves on a virtual memory card in slot A.

| Launcher entry | Build command | Starts with | Saves in |
|---|---|---|---|
| **Super Smash Bros. Melee** (`melee.3dsx`) | `python tools/build_game.py --release` | Every fighter and stage unlocked, tournament-style rules (4 stocks, 8 minutes, items off). This matches the Slippi unlocked setup. | `SD:/3ds/melee/saves/unlocked/` |
| **Melee: Fresh Save** (`melee-fresh.3dsx`) | `python tools/build_game.py --release --profile fresh` | A new, vanilla save. Unlock fighters, stages, trophies and events by playing Classic, Adventure, All-Star, Events, Stadium and VS. | `SD:/3ds/melee/saves/fresh/` |

Run `python tools/package_native.py` after building either or both.

- **Save files:** Dolphin `.gci` format. You can copy a Dolphin or real GameCube Melee save (`01-GALE-SuperSmashBros0110290334.gci`) into a profile folder to continue it.
- **Safety copies:** each save keeps a `.bak` of the previous version, and deleted saves are kept as `.deleted` copies.
- **Both builds include:** UCF, and C-stick attacks in single-player modes.
- **Camera Mode:** in Special Melee, tap **CAMERA** on the bottom screen during the match to steer the camera and take photos. Z is the shutter. Photos save to the card and open under Data › Snapshots.

## Create a CIA for HOME Menu

CIA packaging uses the same release executable and SD files. **The custom disc icon, diorama, and sound are local assets and are not included in Git.** A fresh clone does not produce the custom CIA with the game-build commands alone. Banner authoring is a separate advanced step; use Homebrew Launcher if you do not have compatible prepared artwork.

With a prepared, validated art directory containing `banner.cgfx`, `banner.bin`, `icon.png`, and `announcer.wav`:

```powershell
python tools/bootstrap_home_menu.py --skip-python
python tools/package_cia.py --art path/to/prepared-art --version 15
```

The output is **`dist/home-menu/melee-3ds.cia`**. Add `--profile fresh` to package the fresh-save build as **`dist/home-menu/melee-3ds-fresh.cia`**. It is a separate title with a blue disc icon, so both can be installed at once. The packager accepts the confirmed banner profile and checks the executable and package before writing its verification report. It intentionally rejects an incompatible custom banner. The included `banner_assets.py`, `capture_banner_scene.py`, `make_home_menu_art.py`, and `convert_home_menu_banner.py` are developer authoring tools, not an automatic disc-to-CIA installer.

For an existing validated in-place cosmetic variant, retain its matching original art directory and pass `--cosmetic-baseline path/to/original-art`. Do not use that option to bypass validation of unrelated artwork. Follow the [FBI installation guide](docs/HOME_MENU.md) after packaging.

## Optional Diet scenery

The base game works without replacement scenery. For lighter versions of **Fountain of Dreams, Yoshi's Story, Battlefield, Final Destination, and Dream Land**, apply the [Diet Melee Classic](https://diet.melee.tv/) patch to a separate copy of your own ISO. The preparation tools support the audited Classic 1.0.2/1.0.3 stage files.

Extract `GrIz.dat`, `GrSt.dat`, `GrNBa.dat`, `GrNLa.dat`, and `GrOp.dat` from that patched image into `references/diet-melee/files/`. Keep the original unmodified extraction in `assets/GALE01/`. Then run:

```powershell
python tools/audit_diet_fountain.py
python tools/prepare_diet_stages.py
```

Copy the audited `references/diet-melee/files/GrIz.dat` and the four prepared `.dat` files in `build/visuals/` into **`SD:/3ds/melee/visuals/`**. The tools preserve required original control and animation data. Do not overwrite the base `files` directory with a modified ISO's contents or use arbitrary stage replacements.

## Rebuilding later

Run `git pull --ff-only`, then rebuild with `python tools/build_game.py --release` (add `--profile fresh` for the fresh-save build) and `python tools/package_native.py`. Repackage and reinstall the CIA to update the HOME Menu application, or replace `melee.3dsx` / `melee-fresh.3dsx` for Homebrew Launcher. Existing extracted assets and saves can stay on the card. If the card's `3ds/melee/files` folder still holds all game files directly (packages before September 2026), run `python tools/sd_layout.py X:/3ds/melee/files` once to move them into the faster folder layout.

[Source terms](LICENSE-SCOPE.md) · [Third-party notices](docs/THIRD_PARTY_NOTICES.md)
