#ifndef MP_FEASIBILITY_H
#define MP_FEASIBILITY_H
#ifdef MP_FEASIBILITY_TEST
extern volatile unsigned mp_probe_enabled,mp_probe_mode,mp_probe_links;
unsigned mp_probe_begin(unsigned domain,unsigned function,unsigned context);
void mp_probe_end(unsigned token);
void mp_probe_render(void *object,int pass);
void mp_probe_proc(void *object,void (*callback)(void*));
unsigned mp_probe_drop(unsigned mode);
void mp_probe_checkpoint(void);
void mp_probe_scene(unsigned scene);
#define MP_PROBE_START(name,domain,fn,context) unsigned name=mp_probe_begin(domain,(unsigned)(fn),context)
#define MP_PROBE_END(name) mp_probe_end(name)
#else
#define MP_PROBE_START(name,domain,fn,context)
#define MP_PROBE_END(name)
#endif
#endif
