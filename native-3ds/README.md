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

## Create a CIA for HOME Menu

CIA packaging uses the same release executable and SD files. **The custom disc icon, diorama, and sound are local assets and are not included in Git.** A fresh clone does not produce the custom CIA with the game-build commands alone. Banner authoring is a separate advanced step; use Homebrew Launcher if you do not have compatible prepared artwork.

With a prepared, validated art directory containing `banner.cgfx`, `banner.bin`, `icon.png`, and `announcer.wav`:

```powershell
python tools/bootstrap_home_menu.py --skip-python
python tools/package_cia.py --art path/to/prepared-art --version 15
```

The output is **`dist/home-menu/melee-3ds.cia`**. The packager accepts the confirmed banner profile and checks the executable and package before writing its verification report. It intentionally rejects an incompatible custom banner. The included `banner_assets.py`, `capture_banner_scene.py`, `make_home_menu_art.py`, and `convert_home_menu_banner.py` are developer authoring tools, not an automatic disc-to-CIA installer.

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

Run `git pull --ff-only`, then rebuild with `python tools/build_game.py --release` and `python tools/package_native.py`. Repackage and reinstall the CIA to update the HOME Menu application, or replace `melee.3dsx` for Homebrew Launcher. Existing extracted assets can stay on the card.

[Source terms](LICENSE-SCOPE.md) · [Third-party notices](docs/THIRD_PARTY_NOTICES.md)
