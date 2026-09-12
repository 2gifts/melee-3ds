# Standalone HOME Menu app

The native port can be packaged as a New 3DS CIA for FBI. It contains the game
executable, a full-color Melee disc icon, a 3D Final Destination diorama, the
original title lettering, and the announcer's "Melee!" call over the Menu 1 theme.
It uses the same SD assets as the Homebrew Launcher version.

This is unofficial homebrew. The public repository contains authoring and
packaging source. Disc artwork, banner images, meshes, audio, captures and
finished CIAs remain local.

## Installation

1. Keep `SD:/3ds/melee/files/` and any `SD:/3ds/melee/visuals/` folder.
2. Copy the locally built `melee-3ds.cia` to `SD:/cias/`.
3. Open **FBI > SD > cias > melee-3ds.cia > Install CIA**.
4. Fully power off and restart the console after updating, so HOME reloads the
   banner. Unwrap the gift if shown and launch the Melee disc icon.

Reinstalling the newer CIA updates the same title; replacing the `.3dsx` alone
does not update the installed app. The existing `.3dsx` can stay as a fallback.
SELECT exits. Touch controls, pause, aspect options and stereo are unchanged.
Game data and DSP support still come from the existing homebrew setup.

Target: **New 3DS / New 3DS XL** with CFW and FBI. New 2DS XL uses the same
application mode without stereo; it has not been physically tested. Original
3DS/2DS models are excluded by the package metadata.

## HOME package 5

**Package 4 was confirmed to unwrap and display on physical New 3DS.** Its four
rigid draws, four materials, two textures and seven bones are retained. Packages
1-3 crashed or froze during unwrapping.

Package 5 addresses the next reported issues:

- The old CIA omitted the separate launch splash. This package includes
  makerom's standard **8 KiB Homebrew logo in ExeFS**, with both screen layouts.
  This differs from the disc icon and selection banner. An incorrectly sized
  splash independently produced the same misleading "SD card was removed"
  error in [another homebrew project's tests](https://github.com/astronautlevel2/Anemone3DS/issues/146#issuecomment-381389090).
  This is the identified package defect and proposed fix; physical launch
  confirmation is still needed. It does not rule out a separate SD fault.
- Texture and baked vertex colors pass directly through the texture combiners.
  They no longer multiply by HOME's fragment-light result, which produced black
  silhouettes. The reference shader/material layout stays intact. Gentle
  directional shading is baked into the geometry.
- The complete title fits below the status bar. The left Fox uses the original
  taunt (motion 264/265); the other retains the captured idle pose. Both meet the
  actual tilted stage plane. The hopping/lunging loop has been removed.
- The original Menu 1 theme plays underneath the announcer, with headroom and
  short fades. Stereo PCM stays below three seconds.

CIA title version **4** updates the existing title ID. The game executable is
still update 15. Physical validation of the new launch and appearance changes
is pending. Luma intentionally does not save logs for card-removal errors.

## Local authoring

First follow the normal native build and asset extraction instructions. Banner
authoring additionally needs Python 3.12+, NumPy, Pillow, FFmpeg on PATH and the
configured Azahar development environment.

```powershell
python -m pip install numpy Pillow
python tools/bootstrap_home_menu.py
python tools/banner_assets.py
python tools/build_game.py --smoke --audio-hle --boot --banner-capture --skip-engine
```

Start the development `.3dsx` in the configured New 3DS emulator with GDB port
24689 and the extracted SD assets. Run the first capture from a fresh boot:

```powershell
python tools/capture_banner_scene.py
```

Restart the development application, then capture the taunt and build the art:

```powershell
python tools/capture_banner_scene.py --taunt
python tools/make_home_menu_art.py --disc-image path/to/local-disc-artwork.png
python tools/convert_home_menu_banner.py
python tools/build_game.py --release --skip-engine --output dist/home-menu/3ds/melee/melee.3dsx
python tools/package_cia.py
```

The capture script navigates ordinary Versus controls, selects Fox on Final
Destination and records rendered frames. The taunt pass sends D-pad Up, checks
the original action state and writes `capture-taunt/pose.json`. The production
executable has no capture or input-injection hooks. Entire captured poses become
rigid meshes, avoiding soft skins and cracks between separately moved envelopes.
Complete constant TRS curves preserve the working banner profile.

Supply a square, full-color disc-label image at least 256 pixels across.
Transparency is composited onto a light background before downsampling to the
48x48 RGB565 icon. This remains a local input, not a bundled asset.

Once the art exists, executable updates need only the release build and CIA
packaging commands. Output: `dist/home-menu/melee-3ds.cia` and a verification
report. `package_cia.py` accepts `--elf`, `--art`, `--output`, optional `--cci`
for an emulator cartridge, and explicit `--development` for test-only ELFs.
Never copy a development package to the console.

## Validation and limits

- Title ID `000400000F4D4500`, product `CTR-P-M3LE`; native executable, 124 MB
  application mode, 804 MHz CPU request and L2 cache enabled. No embedded game
  filesystem: both launch methods read `/3ds/melee/`.
- The atlas preserves texture-clamp boundaries. The 1,702 original triangles
  become 2,014 after UV splits, or 2,862 with reverse stage/logo faces. The
  CGFX is 403,544 bytes, below the 524,288-byte limit. Its four-draw profile is
  an authoring budget, not a claimed hardware limit for all banners.
- Audio combines `nr_title.ssm` sample 1 (sound ID 20001), cropped and shortened
  without a pitch change, with `menu01.hps`. Output: stereo PCM16, 32 kHz,
  approximately 2.92 seconds. `home_banner_audio.py` can rebuild the mix alone.
- The verifier checks CIA/ExeFS hashes, the 8 KiB splash and both layout files,
  memory mode and permissions. Decompressed `.code` is compared byte-for-byte
  with all three initialized segments of the finalized BE8 ELF. CWAV pointers,
  block sizes and every PCM sample are checked against the source WAV.
- Serialized CGFX checks cover relative pointers, mesh/bone/animation bindings,
  curve values, indices, finite float attributes and color combiners. Soft skins,
  weight/index streams and non-identity billboard transforms are rejected.
  `python tools/test_home_banner.py` exercises malformed variants;
  `python tools/test_home_banner_atlas.py` checks clipping/interpolation.
- The private native HOME harness uses the owner's matching executable in
  Azahar. Actual ARM framebuffer reads materialize GPU output; raw debugger
  memory reads can return stale pixels. Native comparison shows the old black
  stage and corrected purple materials. A temporary position-offset fixture
  exposes both colored Foxes outside the harness's unrelated system-icon
  overlay. That offset is not in the shipped banner.
- Local startup validation compares the installed content to the built CIA and
  starts its executable in Azahar. These tests do not prove physical HOME
  launch, banner audio playback, suspension behavior or console frame rate.

## References

The [Mario 64 Ultimate port's banner pipeline](https://github.com/Epic0522/Super-Mario-64-3ds-port---Ultimate/tree/ded2ad283290436687bc81cc4b52c9833e7b09c3/tools/banner_3d)
provided the HOME camera and glTF/CGFX packaging reference. This project's
banner uses Melee assets extracted locally, not Mario's artwork.

The [ClouDS hardware banner notes](https://github.com/Epic0522/ClouDS-Music-FA/tree/b9ab67788fd480bea7870a5508daf322562aa06a#readme)
and its `tools/banner/convert_banner_cgfx.py` document soft-skin crashes on
physical HOME Menu and the rigid mesh-node animation/billboard layout used
for this correction. Our scene builder and byte-level verifier are local
implementations; the reference application's artwork is not included.

Tools: [pycgfx](https://github.com/skyfloogle/pycgfx/tree/1f78850086f3a77c41e07162e842f97a5bf3c18a),
[bannertool](https://github.com/Epicpkmn11/bannertool/releases/tag/v1.2.2), and
[makerom](https://github.com/3DSGuy/Project_CTR/releases/tag/makerom-v0.19.0).
Format limits: [3dbrew CBMD](https://3dbrew.org/wiki/CBMD).
