/* Compare native face culling to signed-area selection in the port's clip
 * coordinates. Clockwise is the visible winding captured from the real CSS
 * portraits (build/menu-draws-before.json), not an assumed enum-name match.
 * Each quadrant covers a GX mode; columns alternate winding. */
static volatile unsigned cull_verify;
static void verify_culling(void){
    C3D_RenderTarget*test=C3D_RenderTargetCreate(256,256,GPU_RB_RGBA8,(C3D_DEPTHTYPE){.__i=-1});
    u8*readback=linearAlloc(256*256*4);u8*copy=malloc(256*256*4);
    if(!test||!readback||!copy)mp_native_panic("Cull verification allocation failed");
    for(unsigned reference=0;reference<2;++reference){
        C3D_RenderTargetClear(test,C3D_CLEAR_COLOR,0x123456ff,0);C3D_FrameDrawOn(test);bind_program(0);
        C3D_SetScissor(GPU_SCISSOR_DISABLE,0,0,0,0);
        for(unsigned i=0;i<6;++i)C3D_TexEnvInit(C3D_GetTexEnv(i));
        C3D_TexEnv*env=C3D_GetTexEnv(0);C3D_TexEnvSrc(env,C3D_Both,GPU_PRIMARY_COLOR,0,0);C3D_TexEnvFunc(env,C3D_Both,GPU_REPLACE);
        C3D_DepthTest(false,GPU_ALWAYS,GPU_WRITE_COLOR);C3D_AlphaTest(false,GPU_ALWAYS,0);C3D_StencilTest(false,GPU_ALWAYS,0,255,255);
        C3D_AlphaBlend(GPU_BLEND_ADD,GPU_BLEND_ADD,GPU_ONE,GPU_ZERO,GPU_ONE,GPU_ZERO);
        for(unsigned mode=0;mode<4;++mode)for(unsigned column=0;column<8;++column){
            float x=-.95f+.25f*column,y=-.9f+.5f*mode;
            float xy[3][2]={{x,y},{x+.2f,y},{x+.1f,y+.3f}};
            if(column&1){float tmp=xy[1][0];xy[1][0]=xy[2][0];xy[2][0]=tmp;tmp=xy[1][1];xy[1][1]=xy[2][1];xy[2][1]=tmp;}
            float area=(xy[1][0]-xy[0][0])*(xy[2][1]-xy[0][1])-(xy[1][1]-xy[0][1])*(xy[2][0]-xy[0][0]);
            if(reference){C3D_CullFace(GPU_CULL_NONE);if(mode==3||(mode==1&&area<0)||(mode==2&&area>0))continue;}
            else if(!raster_cull(mode))continue;
            reserve_dynamic(3,0);
            for(unsigned i=0;i<3;++i){Vertex*v=&vertices[vertex_count+i];memset(v,0,sizeof(*v));
                float w=column<4?1:100;v->p[0]=xy[i][1]*w;v->p[1]=-xy[i][0]*w;v->p[2]=-.5f*w;v->p[3]=w;
                v->c[0]=.3f+.2f*mode;v->c[1]=.3f+.08f*column;v->c[2]=.8f;v->c[3]=1;v->n[3]=-1;}
            vertex_base(0);C3D_DrawArrays(GPU_TRIANGLES,vertex_count,3);vertex_count+=3;
        }
        flush_dynamic();C3D_SyncDisplayTransfer(test->frameBuf.colorBuf,GX_BUFFER_DIM(256,256),(u32*)readback,GX_BUFFER_DIM(256,256),0);
        bool used=target->used;target->used=false;C3D_FrameEnd(0);C3D_FrameBegin(0);target->used=used;
        GSPGPU_InvalidateDataCache(readback,256*256*4);for(unsigned i=0;i<256*256*4;++i)copy[i]=((volatile u8*)readback)[i];
        char path[80];snprintf(path,sizeof(path),"sdmc:/3ds/melee/culling-%u.bin",reference);FILE*f=fopen(path,"wb");
        if(!f)mp_native_panic("Cull verification output failed");fwrite(copy,1,256*256*4,f);fclose(f);
    }
    bool used=target->used;target->used=false;C3D_FrameEnd(0);C3D_RenderTargetDelete(test);linearFree(readback);free(copy);
    C3D_FrameBegin(0);target->used=used;C3D_FrameDrawOn(target);C3D_CullFace(GPU_CULL_NONE);bind_program(0);cull_verify=0;
}
