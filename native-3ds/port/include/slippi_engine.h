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
/* Boot menu result: 0 not shown, 1 play online, 2 play offline. */
int mp_platform_slippi_session(void);
/* While waiting without drawing: HOME, power, SELECT (quit). */
void mp_platform_slippi_wait_poll(void);
/* Match-load diagnostics: log SD opens; a millisecond clock; SD totals
 * (0 opens, 1 ms spent opening, 2 bytes read). */
void mp_platform_slippi_load_log(int on);
unsigned mp_platform_slippi_ms(void);
unsigned mp_platform_slippi_sd_stat(int which);
/* Read a disc file (mp_platform_file_id) into the RAM prefetch cache in the
 * background; later reads of it never touch the SD card. */
void mp_platform_slippi_prefetch(int id);
unsigned mp_platform_slippi_prefetch_hits(void);
/* Hold the background reader while a match loads or runs. */
void mp_platform_slippi_prefetch_pause(int paused);
/* 0: files cached so far, 1: KB cached. */
unsigned mp_platform_slippi_prefetch_stat(int which);

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
void mp_slippi_replay_body_begin(void);

/* ---- online play (port/engine/slippi/online.c) ---- */
struct PADStatus;
int mp_slippi_online_configured(void);
int mp_slippi_online_active(void);
void mp_slippi_online_boot_mode(unsigned char* mode);
int mp_slippi_online_wait_match(void);
const unsigned char* mp_slippi_online_pending_block(void);
void mp_slippi_online_start_melee(struct StartMeleeData* data);
int mp_slippi_online_pad_renew(struct PADStatus* stat);
void mp_slippi_online_frame_begin(void);
void mp_slippi_tick_without_draw(void);
void mp_slippi_online_frame_end(void);
void mp_slippi_online_match_exit(void);
void mp_slippi_record_online_begin(void);
void mp_slippi_record_online_frame(int engine_frame);
void mp_slippi_record_online_inputs(const unsigned char* port0, const unsigned char* port1, unsigned checksum);
void mp_slippi_record_online_end(void);

#endif
