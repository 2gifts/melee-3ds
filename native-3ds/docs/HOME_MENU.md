# Install Melee on the HOME Menu

The CIA installs a native Melee application with its own disc icon, Final Destination/Fox preview, and menu-theme/announcer sound. It reads game assets from the SD card. It does not require Homebrew Launcher to start after installation.

You need a supported **New 3DS-family console**, an existing custom-firmware setup, **[FBI](https://github.com/Steveice10/FBI)**, your locally built CIA, and the extracted files from your own US Melee v1.02 dump. [Build and packaging guide](../README.md).

## Install

1. Power off your console and insert its SD card into your computer.
2. Copy the generated `3ds` folder from `dist/native-alpha/` to the SD root, merging folders.
3. Copy `dist/home-menu/melee-3ds.cia` into `SD:/cias/`.
4. Safely eject the card, return it to the console, and power on.
5. Open **FBI → SD → cias → melee-3ds.cia → Install CIA** and confirm.
6. Return to HOME, unwrap the icon if prompted, and launch Melee. If the new icon has not appeared, restart the console.

Your card should contain:

```text
SD:/cias/melee-3ds.cia           Installer
SD:/3ds/melee/files/            Required game assets
SD:/3ds/melee/visuals/          Optional prepared Diet scenery
SD:/3ds/melee/melee.3dsx        Optional Homebrew Launcher executable
```

Installing the CIA alone is insufficient. **Keep the `files` folder on the SD card.** After installation, you may remove the CIA installer from `cias`; the installed application remains on HOME. Allow roughly 1.5 GB for the game data plus space for the application.

## Updates and alternate launch

Install a replacement CIA through FBI to update the HOME Menu application. There is normally no need to delete the title first. Keep the existing game files and optional scenery.

The `.3dsx` is a separate launch option. Select **melee** in Homebrew Launcher to use it. Replacing that file does **not** update the application installed from a CIA, and reinstalling a CIA does not replace the `.3dsx`.

## If something is wrong

- **Missing game files:** check for `SD:/3ds/melee/files/`, rather than an extra nested `native-alpha/3ds/` directory. Use the supported unmodified base dump.
- **No audio:** the port needs your homebrew setup's DSP support or an existing `SD:/3ds/dspfirm.cdc`. Firmware is not bundled.
- **Low frame rate:** try 2D and 1v1 on prepared Diet scenery. See the [performance expectations](../../README.md#performance-to-expect); crowded scenes remain demanding.
- **Crash or freeze:** note the console model, launch method, fighters, stage, and 2D/3D setting. The readable log is `SD:/3ds/melee/game.log`. Check it before sharing; do not upload game files, firmware, or raw memory dumps.

The project is complete for now. [Issues](https://github.com/2gifts/melee-3ds/issues) remain available for documenting problems, without a promise of further fixes.
