#ifndef MP_CITRO3D_FIX_H
#define MP_CITRO3D_FIX_H
void mp_native_tex_bind(int unit, C3D_Tex* texture);
void mp_native_tex_bind_invalidate(void);
void mp_native_draw_elements(GPU_Primitive_t primitive,int count,int type,const void*indices);
#endif
