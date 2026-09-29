#ifndef MP_GX_THREAD_H
#define MP_GX_THREAD_H
/* GX translator thread: the engine thread records GX commands and this
 * thread replays them into the renderer (port/engine/gx_fifo.c). */
int mp_gx_thread_create(int is_new);
void mp_gx_thread_go(void);
void mp_gx_thread_stop(void);
int mp_gx_thread_is_current(void);
void mp_gx_thread_panic(const char *message) __attribute__((noreturn));
extern unsigned mp_gx_translator_active, mp_gx_translator_core;
#endif
