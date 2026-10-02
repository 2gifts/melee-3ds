/* Slippi experiment: engine-side (big-endian) declarations.
 *
 * Native calls cross the endianness bridge in port/3ds/game_bridge.S: only
 * scalars (in registers) and byte buffers may pass. Never hand the native side
 * a pointer to a multi-byte integer it should write. */
#ifndef MP_SLIPPI_ENGINE_H
#define MP_SLIPPI_ENGINE_H

/* ---- native file helpers (port/3ds/slippi/slippi_files.c) ---- */
/* Whole file into a native allocation, or NULL. Size via the next call. */
void* mp_platform_slippi_file_load(const char* path);
unsigned mp_platform_slippi_file_size(void);
/* append=0 truncates first. Returns bytes written. */
int mp_platform_slippi_file_write(const char* path, const void* data, unsigned size, int append);
/* Development builds: disable the automatic end-of-run frame limit. */
void mp_platform_slippi_unlimited(void);

/* ---- replay playback (port/engine/slippi/replay.c) ---- */
struct Fighter;
struct StartMeleeData;
int mp_slippi_replay_on(void);
void mp_slippi_replay_boot_mode(unsigned char* mode);
void mp_slippi_replay_prepare_scene(void);
void mp_slippi_replay_start_melee(struct StartMeleeData* data);
void mp_slippi_replay_scene_think(int match_result);
int mp_slippi_replay_terminated(void);
void mp_slippi_replay_input(struct Fighter* fp);
void mp_slippi_replay_post_frame(struct Fighter* fp);
void mp_slippi_replay_match_exit(void);
int mp_slippi_replay_frame_index(void);

#endif
