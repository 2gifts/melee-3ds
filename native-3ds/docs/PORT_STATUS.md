# Project status

Development is complete for now at the retained, hardware-tested playable milestone. The repository contains the pre-rewrite runtime (internal update 22); the later experimental renderer is not part of this release.

**Versus and Training on New 3DS have received the most testing.** Full matches, results, rematches, audio, the touch-screen dashboard, stereoscopic 3D, and HOME Menu launch have been exercised. The smaller New 3DS is the physically tested model. New 3DS XL and New 2DS XL are targets; original-model systems are unsupported.

The port preserves Melee's original gameplay code and stage collision while adapting rendering, audio, controls, and platform services. UCF input fixes and editable offline defaults are intentional additions. This is not a claim of exhaustive, tournament-certified equivalence across every mode.

Performance depends on the scene. Lighter Diet-stage 1v1 matches commonly reach about 55–60 FPS in 2D and around 50–55 in 3D, with lower dips. Crowded large-stage matches can drop into the teens or low 20s in 3D. Stable 60 FPS stereo and a universal 30 FPS floor have not been achieved. [Overview and controls](../../README.md).

Known limits:

- One human player; up to four fighters using CPU opponents. No local wireless, online play, or rollback.
- No persistent memory-card saves or replay recording. Unlocks and default settings are applied on each launch.
- Optional movies are skipped. Some materials, shadows, and other effects are simplified or approximate.
- Single-player modes have less testing than Versus/Training; full compatibility is not guaranteed.
- New 2DS XL has no stereoscopic display and has not been physically tested with this port.

The repository remains available for community study and further work. No additional optimization work or release schedule is currently planned.
