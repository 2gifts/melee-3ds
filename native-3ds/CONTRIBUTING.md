# Contributing to the 3DS port

Work on the `3ds` branch. Port code, overlays and build tools live under
`native-3ds/`; the original decompilation at the repository root is preserved.
Keep generated files, ROMs, fonts, firmware, toolchain binaries and local
game packages out of commits. Keep third-party attribution with adaptations.

Use the README's release build commands. Useful asset-free host checks after
bootstrap include `python tools/test_geometry_bounds.py`,
`python tools/test_matrix_recovery.py`, `python tools/test_collision_contacts.py`,
and `python tools/test_engine_encoding.py` from this directory.

Many other `tools/*_test.py` scripts are developer experiments requiring an
active Azahar 2126.1 portable Windows instance, its SD data, a matching smoke
ELF, and GDB port 24689. Optional Python dependencies are NumPy, Pillow and
Capstone. They are not required to build the release. Use one GDB client at a
time, and do not replace its ELF while a live test uses its symbols.
`python tools/build_game.py --smoke --audio-hle --boot` builds that test image;
the physical build must use `--release`, which excludes these test hooks.

Explain the issue and resulting behavior, include relevant checks, and report
rendered FPS separately from simulation update rate. Console measurements and
emulator measurements must be labeled separately. Preserve gameplay/collision
behavior when reducing rendering work, and test both eyes and 2D where relevant.

For bugs, provide console/launcher versions, commit, fighter/stage, display mode,
and steps to reproduce. Do not upload game files or firmware. Raw hardware
dumps can contain game data; begin with the readable log and crash registers.
