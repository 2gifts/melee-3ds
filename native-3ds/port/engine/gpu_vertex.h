#ifndef MP_GPU_VERTEX_H
#define MP_GPU_VERTEX_H
/* Shared BE8/LE wire layout. A negative normal.w selects CPU clip/color data;
 * otherwise it is the first row of a three-row GPU matrix palette entry. */
typedef struct { float pos[4], color[4], uv[2], normal[4]; } MPGPUVertex;
enum { MP_GPU_POS=0, MP_GPU_NORMAL=30, MP_GPU_PROJECTION=60,
       MP_GPU_LIGHT_POS=64, MP_GPU_LIGHT_DIR=68, MP_GPU_LIGHT_COLOR=72,
       MP_GPU_ATTENUATION=76, MP_GPU_COS_ATTENUATION=80, MP_GPU_SHADE=84,
       MP_GPU_AMBIENT=89, MP_GPU_CONFIG=91, MP_GPU_MATERIAL1=93,
       MP_GPU_CLAMP=94, MP_GPU_UNIFORMS=95 };
/* A fixed vertex attribute carries the changing material without consuming
 * another of PICA's 96 float uniforms or rewriting cached vertex buffers. */
typedef struct { float value[MP_GPU_UNIFORMS][4];unsigned matrix_rows;float material0[4]; } MPGPUUniforms;
#endif
