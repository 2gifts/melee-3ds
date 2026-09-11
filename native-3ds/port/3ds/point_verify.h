/* Emulator-only comparison of GPU points with CPU-expanded reference quads.
 * Separate offscreen targets leave the game's color/depth buffers intact. */
#include "../engine/primitive_expand.h"
static volatile unsigned point_verify;
static void verify_points(void){
    C3D_RenderTarget*test=C3D_RenderTargetCreate(256,256,GPU_RB_RGBA8,(C3D_DEPTHTYPE){.__i=-1});
    C3D_Tex tex;u8*readback=linearAlloc(256*256*4);u8*copy=malloc(256*256*4);
    if(!test||!readback||!copy||!C3D_TexInit(&tex,8,8,GPU_RGBA8))mp_native_panic("Point verification allocation failed");
    for(unsigned y=0;y<8;++y)for(unsigned x=0;x<8;++x)((u32*)tex.data)[morton(x,y)]=rgba(x*32,y*32,(x^y)*32,255);
    C3D_TexSetFilter(&tex,GPU_NEAREST,GPU_NEAREST);C3D_TexSetWrap(&tex,GPU_CLAMP_TO_EDGE,GPU_CLAMP_TO_EDGE);C3D_TexFlush(&tex);
    const unsigned widths[]={0,1,6,18,63,127,191,255},corners[]={0,2,1,1,2,3};
    for(unsigned gpu=0;gpu<2;++gpu){
        C3D_RenderTargetClear(test,C3D_CLEAR_COLOR,0x123456ff,0);C3D_FrameDrawOn(test);bind_program(gpu);C3D_CullFace(GPU_CULL_NONE);
        C3D_SetScissor(GPU_SCISSOR_DISABLE,0,0,0,0);
        C3D_TexBind(0,&tex);for(unsigned i=0;i<6;++i)C3D_TexEnvInit(C3D_GetTexEnv(i));
        C3D_TexEnv*env=C3D_GetTexEnv(0);C3D_TexEnvSrc(env,C3D_Both,GPU_TEXTURE0,GPU_PRIMARY_COLOR,0);C3D_TexEnvFunc(env,C3D_Both,GPU_MODULATE);
        C3D_DepthTest(false,GPU_ALWAYS,GPU_WRITE_COLOR);C3D_AlphaTest(false,GPU_ALWAYS,0);C3D_StencilTest(false,GPU_ALWAYS,0,255,255);
        C3D_AlphaBlend(GPU_BLEND_ADD,GPU_BLEND_ADD,GPU_ONE,GPU_ZERO,GPU_ONE,GPU_ZERO);
        for(unsigned row=0;row<8;++row)for(unsigned col=0;col<8;++col){
            const float depths[]={1,10,100,1000,-1,1,1,10};
            float w=depths[row],uv=mp_tex_offset(row%6);MPGPUVertex v={0},quad[4];
            v.pos[0]=(-.875f+col*.25f)*w;v.pos[1]=(-.875f+row*.25f)*w;v.pos[2]=-.5f*w;v.pos[3]=w;
            if(row==5)v.pos[2]=-1.01f*w;if(row==6)v.pos[2]=.01f*w;if(row==7)v.pos[2]=-w;
            v.color[0]=.3f+col*.1f;v.color[1]=.3f+row*.1f;v.color[2]=.8f;v.color[3]=1;
            v.uv[0]=.2f;v.uv[1]=.3f;v.normal[3]=-1;
            unsigned count=0;
            if(gpu){if(!widths[col])continue;quad[0]=v;count=1;C3D_FVUnifSet(GPU_GEOMETRY_SHADER,0,widths[col]/2880.f,widths[col]/3840.f,uv*.625f,uv*.75f);}
            else if(mp_expand_point(&v,widths[col],uv,quad))count=6;
            reserve_dynamic(count,0);
            for(unsigned i=0;i<count;++i){MPGPUVertex*q=&quad[gpu?0:corners[i]];Vertex*n=&vertices[vertex_count+i];memcpy(n,q,sizeof(*n));
                n->p[0]=q->pos[1];n->p[1]=-q->pos[0];n->t[0]=q->uv[0]*.625f;n->t[1]=1-q->uv[1]*.75f;}
            if(count){vertex_base(0);C3D_DrawArrays(gpu?GPU_GEOMETRY_PRIM:GPU_TRIANGLES,vertex_count,count);vertex_count+=count;}
        }
        flush_dynamic();C3D_SyncDisplayTransfer(test->frameBuf.colorBuf,GX_BUFFER_DIM(256,256),(u32*)readback,GX_BUFFER_DIM(256,256),0);
        bool used=target->used;target->used=false;C3D_FrameEnd(0);C3D_FrameBegin(0);target->used=used;
        GSPGPU_InvalidateDataCache(readback,256*256*4);for(unsigned i=0;i<256*256*4;++i)copy[i]=((volatile u8*)readback)[i];
        char path[80];snprintf(path,sizeof(path),"sdmc:/3ds/melee/points-%u.bin",gpu);FILE*f=fopen(path,"wb");
        if(!f)mp_native_panic("Point verification output failed");fwrite(copy,1,256*256*4,f);fclose(f);
    }
    bool used=target->used;target->used=false;C3D_FrameEnd(0);
    C3D_RenderTargetDelete(test);C3D_TexDelete(&tex);linearFree(readback);free(copy);
    C3D_FrameBegin(0);target->used=used;C3D_FrameDrawOn(target);bind_program(0);point_verify=0;
}
