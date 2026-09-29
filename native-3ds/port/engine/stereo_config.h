#ifndef MP_STEREO_CONFIG_H
#define MP_STEREO_CONFIG_H
/* Full-slider pixel separation at half the convergence distance. Shared by
 * the GPU submission and conservative CPU culling of both eye frusta. */
#define MP_STEREO_PIXELS_PER_SLIDER .012f
/* Largest pop-out, as a fraction of the far-field (behind-screen) shift.
 * Nearer content is held at this depth; see generate_vertex_shader.py. */
#define MP_STEREO_POPOUT_LIMIT 1.f
#endif
