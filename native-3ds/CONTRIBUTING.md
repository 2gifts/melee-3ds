# Contributing

The project is complete for now. Community forks and contributions are welcome, but active maintenance and review turnaround are not promised.

Work on the `3ds` branch. The port is in `native-3ds/`; preserve the original decompilation at the repository root. Follow the [build guide](README.md) and keep game files, fonts, firmware, generated packages, toolchains, and personal logs out of commits. Retain upstream and third-party notices.

Preserve gameplay and collision behavior when changing rendering. Validate 2D and both stereo eyes, and label console performance separately from emulator results. Report rendered FPS separately from the game update rate.

Useful asset-free checks after bootstrap include `python tools/test_geometry_bounds.py`, `python tools/test_matrix_recovery.py`, `python tools/test_collision_contacts.py`, and `python tools/test_engine_encoding.py`. Other developer scripts may require a configured emulator and local assets; use `--release` for console builds.

For bug reports, include the source commit, console model, launch method, stage and fighters, display mode, and steps to reproduce. Do not attach ROMs, extracted assets, firmware, or raw memory dumps.
