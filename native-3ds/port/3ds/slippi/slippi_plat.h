/* Platform layer for the Slippi network client: 3DS (libctru) and Windows
 * (the PC test tools). Everything else in this folder is platform-neutral. */
#ifndef SLIPPI_PLAT_H
#define SLIPPI_PLAT_H
#include <stddef.h>
#include <stdint.h>

/* Monotonic microseconds. */
uint64_t sp_time_us(void);
void sp_sleep_us(uint32_t us);
uint32_t sp_random(void);

/* printf-style log line (a newline is added). Thread-safe: lines produced on the
 * network thread are queued and written by sp_log_drain() on the caller's thread
 * when the platform log is not thread-safe (3DS game.log). */
void sp_log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void sp_log_drain(void);
/* Optional sink override (tools); NULL restores the default. */
void sp_set_log_sink(void (*sink)(const char *line));

/* One global recursive-free lock around all client state. */
void sp_lock_init(void);
void sp_lock(void);
void sp_unlock(void);

/* One background thread. core: 3DS core id (-2 = default), ignored on PC. */
int sp_thread_start(void (*fn)(void *), void *arg, int core);
void sp_thread_join(void);
int sp_on_thread(void);

/* Reads a whole file, NUL-terminated, malloc'd. NULL when missing. */
char *sp_read_file(const char *path, size_t *length);

/* Sockets: socInit()/WSAStartup() and teardown. Returns 0 on success. */
int sp_net_init(void);
void sp_net_exit(void);
/* Host-ID style fallback for the LAN address (network byte order), 0 if unknown. */
uint32_t sp_lan_ip_fallback(void);
/* Wi-Fi state for diagnostics: 1 connected, 0 not, -1 unknown. */
int sp_wifi_status(void);

/* 0 when the app should quit (3DS aptMainLoop), 1 otherwise. */
int sp_app_running(void);

/* Folder holding user.json and config.ini. */
const char *sp_data_dir(void);
void sp_set_data_dir(const char *dir);

#endif
