#ifndef MP_LOG_PROGRESS_H
#define MP_LOG_PROGRESS_H
/* Native thread checkpoints; no engine pointers are read by the logger. */
enum { MP_LOG_ENGINE=1,MP_LOG_GPU_BEGIN,MP_LOG_GPU_END,MP_LOG_DISC_READ };
#ifdef __3DS__
unsigned mp_log_phase(unsigned phase);
void mp_log_frame(unsigned frame);
#else
static inline unsigned mp_log_phase(unsigned phase){(void)phase;return 0;}
static inline void mp_log_frame(unsigned frame){(void)frame;}
#endif
#endif
