# Standalone HOME Menu app

The native port can now be packaged as a New 3DS CIA for FBI. This contains
the game executable, a full-color Melee disc icon, an animated 3D Final Destination
diorama with two Foxes, the original title lettering, and the intro announcer's
“Melee!” call. It uses the same SD assets as the Homebrew Launcher version.

This is an unofficial homebrew port. The public repository contains the
authoring and packaging source; disc artwork, banner images, meshes, audio,
captures and finished CIAs remain local.

## Install an already built CIA

1. Keep `SD:/3ds/melee/files/` and any `SD:/3ds/melee/visuals/` folder.
2. Copy the locally built `melee-3ds.cia` to `SD:/cias/`.
3. On the console, open **FBI → SD → cias → melee-3ds.cia → Install CIA**.
4. After updating an existing installation, fully power the console off and on
   before selecting Melee, so HOME reloads the banner.
5. Unwrap the new gift if shown, and launch the Melee disc icon.

The existing `.3dsx` can stay on the card as a fallback. Reinstall a newer CIA
with the same title ID to update the HOME app; replacing the `.3dsx` alone does
not update it. SELECT exits the game. Existing touch-screen controls, pause,
aspect options and slider-controlled stereo are unchanged.

Supported target: **New 3DS / New 3DS XL** with CFW and FBI. New 2DS XL uses the
same application mode without stereoscopic output; it has not been tested on
physical hardware. Original 3DS/2DS models are excluded by the package metadata.
Audio still needs the DSP support from the existing homebrew setup.

### HOME banner compatibility

The first CIA crashed during unwrapping; package 2 still froze HOME Menu.
Package 3 groups material parts under their actual moving objects: seven bones
and two animation tracks replace 78 bones and 62 tracks. It retains all 1,702
triangles and the same fighter motion, and uses the float RGB vertex streams
found in the working reference banners. The disc icon and announcer clip stay
the same. CIA title version 2 replaces version 1 using the same title ID.

The earlier dump belongs to US HOME Menu (`0004003000008F02`). Analysis of the
matching executable places the fault in a heap-list traversal, consistent with
prior memory corruption. No new dump accompanied the package 2 freeze. Reducing
the transform workload addresses a major difference from the working banners;
the exact corrupting instruction has not been isolated. The validator now
rejects both earlier banner layouts. **Physical validation of package 3 is
still required; structural checks alone cannot guarantee HOME compatibility.**

## Local authoring and packaging

First follow the normal native port build and asset extraction instructions.
Banner authoring additionally needs Python 3.12+, NumPy, Pillow, FFmpeg on PATH,
and the project's configured Azahar development environment. On Windows:

```powershell
python -m pip install numpy Pillow
python tools/bootstrap_home_menu.py
python tools/banner_assets.py
python tools/build_game.py --smoke --audio-hle --boot --banner-capture --skip-engine
```

Launch `dist/3ds/melee/melee-development.3dsx` in the configured New 3DS Azahar
environment, using the existing extracted SD assets and GDB port 24689. Then:

```powershell
python tools/capture_banner_scene.py
python tools/make_home_menu_art.py --disc-image path/to/local-disc-artwork.png
python tools/convert_home_menu_banner.py
python tools/build_game.py --release --skip-engine --output dist/home-menu/3ds/melee/melee.3dsx
python tools/package_cia.py
```

`capture_banner_scene.py` starts from a fresh boot, navigates ordinary Versus
controls, verifies both fighters are Fox on Final Destination, and captures
45 complete rendered frames. This is an offline authoring step; the release
executable has no capture hooks or test controller injection. The separate
banner bakes two captured fighter poses and animates their complete meshes
rigidly through a lunge/evade/counterattack loop. This avoids soft skins and
cracks between separately transformed envelopes. Fighters are enlarged and
repositioned for the small HOME display. Its compact purple stage material
replaces the gameplay renderer's multipass effects.

Supply a square, full-color disc-label image at least 256 pixels across.
Transparent corners and the hub are composited onto a light background before
downsampling to HOME Menu's 48×48 RGB565 icon. The image remains a local input;
no disc artwork is bundled with this repository.

Once `build/home-menu/art/` exists, ordinary executable updates only need the
release build and `package_cia.py` commands. The output is
`dist/home-menu/melee-3ds.cia`, with a machine-readable verification report.
The CIA builder accepts `--elf`, `--art`, and `--output` for explicit paths.
`--cci` optionally creates an emulator test cartridge. Development packages
require the explicit `--development` option and must not be copied to consoles.

## Package and verification details

- Homebrew title ID: `000400000F4D4500`; product code: `CTR-P-M3LE`.
- Native executable, 124 MB application mode, 804 MHz CPU request, L2 enabled.
- No embedded game filesystem. Both launch methods read `/3ds/melee/`.
- Banner: 1,702 triangles, a looping 3.2-second rigid animation, and a
  439,032-byte CGFX under HOME's 524,288-byte limit. Material parts share one
  transform per object, with float RGB vertex streams and two animation tracks.
- Sound: original English intro bank `nr_title.ssm`, sample 1 (game sound ID
  20001), final word isolated and shortened without changing pitch to fit
  HOME's audio limit. Output: stereo, 32 kHz, approximately 2.92 seconds.
- The verifier reads back the CIA content hash, ExeFS hashes, icon, compressed
  CGFX, application permissions and memory mode. It decompresses `.code` and
  compares all three initialized segments against the finalized BE8 ELF.
- Serialized CGFX verification follows relative pointers, validates mesh/bone/
  animation bindings and curve values, checks triangle indices and rejects
  soft skins, bone-weight/index streams and non-identity billboard transforms.
  It enforces this scene's seven-bone budget and checks finite float attributes.
  The sound verifier checks both CWAV channel pointers, block sizes and sample
  bounds, and compares every PCM sample against the local source WAV.
  Run `python tools/test_home_banner.py` after generating the banner to check
  that malformed variants are rejected. This does not render HOME Menu.
- Local validation includes CIA installation and installed-executable startup
  in Azahar, original menu navigation into Versus, stereo pixel checks and
  Start pause/resume. This does not validate the physical HOME Menu's banner
  renderer, audio playback, suspension behavior, or console frame rate.

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
