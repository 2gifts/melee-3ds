# Project status

Development is complete for now at a hardware-tested playable milestone. This release records the game's graphics commands on the main CPU core and translates and renders them on another core of the New 3DS, with saves on a virtual memory card.

**Versus and Training on New 3DS have received the most testing.** Full matches, results, rematches, audio, the touch-screen dashboard, stereoscopic 3D, and HOME Menu launch have been exercised. The smaller New 3DS is the physically tested model. New 3DS XL and New 2DS XL are targets; original-model systems are unsupported.

The port preserves Melee's original gameplay code and stage collision while adapting rendering, audio, controls, and platform services. UCF input fixes and editable offline defaults are intentional additions. This is not a claim of exhaustive, tournament-certified equivalence across every mode.

Performance depends on the scene. With 3D on, 1v1 matches run at 60 FPS on most stages, and casual four-fighter matches on large stages hold a steady 30 FPS (the bottom screen's AUTO rate mode switches between the two). A few single-player stages with fighter teams can still drop into the teens or 20s. Game logic runs at full speed almost everywhere. [Overview and controls](../../README.md).

Known limits:

- One human player; up to four fighters using CPU opponents. No local wireless, online play, or rollback.
- Saves use a virtual memory card on the SD card (Dolphin-compatible `.gci` files). There are two builds: everything unlocked, or a fresh save that unlocks fighters, stages, trophies and events through play. Replay recording is not supported.
- Optional movies are skipped. Some materials, shadows, and other effects are simplified or approximate.
- Single-player modes have been swept in the emulator: every Classic round, Adventure stage and cutscene, event, Stadium mode, ending, trophy, stage and costume. They have had less hardware testing than Versus and Training, and full compatibility is not guaranteed.
- New 2DS XL has no stereoscopic display and has not been physically tested with this port.

The repository remains available for community study and further work. No additional optimization work or release schedule is currently planned.
