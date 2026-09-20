#ifndef MP_RENDER_WORKER_H
#define MP_RENDER_WORKER_H
/* Native CPU ownership boundary. Engine callbacks/input stay on main;
 * immutable draw packets and explicit CPU retirement fences protect inputs. */
#ifdef MP_RENDER_WORKER
void mp_render_worker_start(int is_new);
void mp_render_worker_stop(void);
void mp_render_worker_barrier(void);
void mp_render_worker_retire_geometry(void);
void mp_render_worker_panic(const char *message);
void mp_render_worker_report(unsigned frame);
const void *mp_render_worker_texture_source(unsigned address, unsigned bytes);
#ifdef MP_RENDER_WORKER_IMPLEMENTATION
#define mp_native_submit mp_renderer_impl_submit
#define mp_renderer_begin mp_renderer_impl_begin
#define mp_renderer_end mp_renderer_impl_end
#define mp_renderer_exit mp_renderer_impl_exit
#define mp_native_efb_copy mp_renderer_impl_efb_copy
#define mp_native_texture_dirty mp_renderer_impl_texture_dirty
#define mp_native_texture_invalidate mp_renderer_impl_texture_invalidate
#define mp_native_frame_texture_visibility mp_renderer_impl_frame_texture_visibility
#define mp_renderer_counts mp_renderer_impl_counts
#define mp_renderer_benchmark_frame mp_renderer_impl_benchmark_frame
#endif
#endif
#endif
