/* Exercise the actual draw bridge twice, with/without redundant-state skips.
 * Repeated draws overlap so a lost blend/depth/alpha update changes pixels. */
static volatile unsigned raster_state_verify;
void mp_native_efb_copy(const u32*request);
static void verify_raster_state(void){
    C3D_RenderTarget*saved_target=target;
    C3D_RenderTarget*test=C3D_RenderTargetCreate(240,400,GPU_RB_RGBA8,(C3D_DEPTHTYPE){.__i=GPU_RB_DEPTH24_STENCIL8});
    u8*readback=linearAlloc(240*400*4),*copy=malloc(240*400*4);
    static u8 pixels[128] __attribute__((aligned(32)));
    static u8 copied_pixels[4096] __attribute__((aligned(32)));
    static const u16 ix[6]={0x0000,0x0100,0x0200,0x0000,0x0200,0x0300};
    if(!test||!readback||!copy)mp_native_panic("Raster state verification allocation failed");
    for(unsigned i=0;i<128;++i)pixels[i]=(i*37+71)&255;
    unsigned saved_disable=raster_state_disable;target=test;
    for(unsigned bypass=0;bypass<2;++bypass){
        raster_state_disable=bypass;raster_state_invalidate();
        C3D_RenderTargetClear(test,C3D_CLEAR_ALL,0x345678ab,0);C3D_FrameDrawOn(test);
        C3D_StencilTest(false,GPU_ALWAYS,0,255,255);
        for(unsigned i=0;i<6;++i)C3D_TexEnvInit(C3D_GetTexEnv(i));
        for(unsigned cell=0;cell<160;++cell){
            unsigned column=cell%16,row=cell/16;
            Draw d={0};d.image=(cell%5)?(u32)pixels:0;d.w=d.h=8;d.format=cell%3==0?1:4;
            d.depth=(cell>>1)&1;d.write=(cell>>2)&1;d.func=(cell/8)&7;
            d.blend=(cell&3)|(((cell/4)&15)<<8)|(1<<16);d.src=(cell/2)&7;d.dst=(cell/3)&7;
            d.cull=(cell/5)%4;d.color_mask=cell%7==0?GPU_WRITE_RED|GPU_WRITE_BLUE:GPU_WRITE_COLOR;
            d.texture_rgb=cell&1;d.texture_alpha=(cell>>1)&1;d.wrap_s=cell%3;d.wrap_t=(cell/3)%3;
            static const unsigned alpha[]={7|(7<<13),4|(110<<3)|(7<<13),
                4|(64<<3)|(1<<13)|(192<<16),4|(64<<3)|(2<<11)|(1<<13)|(192<<16)};
            d.alpha=alpha[(cell/7)%4];d.indices=(u32)ix;d.index_count=6;
            d.scissor[0]=column*40;d.scissor[1]=row*48;d.scissor[2]=cell%2?26:40;d.scissor[3]=48;
            d.points=cell%11==0;d.point_size=72;d.point_offset=cell%6;
            float x=column/8.f-1+.015f,y=1-row/5.f-.015f;
            float xy[4][2]={{x,y},{x+.09f,y},{x+.09f,y-.16f},{x,y-.16f}};
            Vertex v[4];memset(v,0,sizeof(v));
            for(unsigned i=0;i<4;++i){
                v[i].p[0]=xy[i][0];v[i].p[1]=xy[i][1];v[i].p[2]=-.5f;v[i].p[3]=1;v[i].n[3]=-1;
                v[i].c[0]=.25f+(cell%5)*.14f;v[i].c[1]=.7f;v[i].c[2]=.35f;v[i].c[3]=.3f+i*.2f;
                v[i].t[0]=i&1;v[i].t[1]=i/2;
            }
            Draw wire;Vertex wire_v[4];
            for(unsigned i=0;i<sizeof(d)/4;++i)((u32*)&wire)[i]=__builtin_bswap32(((u32*)&d)[i]);
            for(unsigned i=0;i<sizeof(v)/4;++i)((u32*)wire_v)[i]=__builtin_bswap32(((u32*)v)[i]);
            for(unsigned repeat=0;repeat<3;++repeat){
                mp_native_submit(wire_v,4,&wire);
                if(cell==80&&repeat==0){
                    /* A real EFB copy followed by copy-clear changes culling,
                     * viewport/scissor, depth, alpha, blend and texture env. */
                    u32 q[14]={(u32)copied_pixels,0,0,64,32,64,32,4,1,0x13579bff,0,15,1,1},be_q[14];
                    for(unsigned i=0;i<14;++i)be_q[i]=__builtin_bswap32(q[i]);mp_native_efb_copy(be_q);
                }
            }
        }
        flush_dynamic();C3D_SyncDisplayTransfer(test->frameBuf.colorBuf,GX_BUFFER_DIM(240,400),(u32*)readback,GX_BUFFER_DIM(240,400),0);
        bool used=saved_target->used;saved_target->used=false;target->used=false;C3D_FrameEnd(0);C3D_FrameBegin(0);saved_target->used=used;
        GSPGPU_InvalidateDataCache(readback,240*400*4);for(unsigned i=0;i<240*400*4;++i)copy[i]=((volatile u8*)readback)[i];
        char path[96];snprintf(path,sizeof(path),"sdmc:/3ds/melee/raster-state-%u.bin",bypass);FILE*f=fopen(path,"wb");
        if(!f)mp_native_panic("Raster state verification output failed");fwrite(copy,1,240*400*4,f);fclose(f);
    }
    bool used=saved_target->used;saved_target->used=false;C3D_FrameEnd(0);C3D_RenderTargetDelete(test);linearFree(readback);free(copy);
    target=saved_target;C3D_FrameBegin(0);target->used=used;C3D_FrameDrawOn(target);bind_program(0);
    raster_state_disable=saved_disable;raster_state_invalidate();raster_state_verify=0;
}
