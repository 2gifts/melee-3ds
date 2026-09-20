/* Compare the original copy+clear against clear-only on both eyes. A depth-
 * tested overlay covers half each cell, so the images exercise both depth
 * preservation/clearing and every independent color-write mask. */
static volatile unsigned efb_discard_verify;
static void verify_efb_discard(void){
    if(!stereo_active)return;
    unsigned token=efb_discard_verify,saved_disable=efb_discard_disable;
    unsigned saved_width=render_width,saved_depth=mp_native_stereo_depth;
    C3D_RenderTarget*saved=target,*test=C3D_RenderTargetCreate(240,800,GPU_RB_RGBA8,(C3D_DEPTHTYPE){.__i=GPU_RB_DEPTH24_STENCIL8});
    u8*readback=linearAlloc(240*800*4),*copy=malloc(240*800*4);
    static u8 unused[128] __attribute__((aligned(32)));
    static const u16 ix[6]={0,0x100,0x200,0,0x200,0x300};
    if(!test||!readback||!copy)mp_native_panic("Copy-clear fixture allocation failed");
    unsigned before=efb_discarded_copies;target=test;
    for(unsigned width_index=0;width_index<2;++width_index){unsigned width=width_index?400:320;
        for(unsigned mode=0;mode<2;++mode){
            efb_discard_disable=!mode;
            C3D_RenderTargetClear(test,C3D_CLEAR_ALL,0x345678ab,0x800000);C3D_FrameDrawOn(test);
            screen_viewport(width);raster_state_invalidate();
            C3D_StencilTest(false,GPU_ALWAYS,0,255,255);
            for(unsigned cell=0;cell<32;++cell){
                unsigned column=cell%8,row=cell/8;
                u32 q[14]={(u32)unused,column*80,row*120,80,120,8,8,5,1,0xbe4183cf,0x400000,cell&15,cell>>4,2},wireq[14];
                memset(unused,0xcd,sizeof(unused));
                for(unsigned i=0;i<14;++i)wireq[i]=__builtin_bswap32(q[i]);
                mp_native_efb_copy(wireq);
                if(mode)for(unsigned i=0;i<sizeof(unused);++i)if(unused[i]!=0xcd)mp_native_panic("Discarded framebuffer destination was written");
                /* Foreground in one of the two depth states only. Leave the
                 * upper half uncovered to observe the clear's color mask. */
                Draw d={0};d.depth=1;d.write=0;d.func=3;d.color_mask=15;
                d.alpha=7|(7<<13);d.indices=(u32)ix;d.index_count=6;
                d.scissor[2]=640;d.scissor[3]=480;d.screen_width=width;
                float x=column*.25f-1,y=1-row*.5f;
                float xy[4][2]={{x,y-.26f},{x+.24f,y-.26f},{x+.24f,y-.49f},{x,y-.49f}};
                Vertex v[4]={0},wirev[4];Draw wired;
                for(unsigned i=0;i<4;++i){v[i].p[0]=xy[i][1];v[i].p[1]=-xy[i][0];v[i].p[2]=-.6f;v[i].p[3]=1;v[i].n[3]=-1;
                    v[i].c[0]=.12f;v[i].c[1]=.9f;v[i].c[2]=.35f;v[i].c[3]=1;}
                for(unsigned i=0;i<sizeof(d)/4;++i)((u32*)&wired)[i]=__builtin_bswap32(((u32*)&d)[i]);
                for(unsigned i=0;i<sizeof(v)/4;++i)((u32*)wirev)[i]=__builtin_bswap32(((u32*)v)[i]);
                mp_native_submit(wirev,4,&wired);
            }
            flush_dynamic();C3D_SyncDisplayTransfer(test->frameBuf.colorBuf,GX_BUFFER_DIM(240,800),(u32*)readback,GX_BUFFER_DIM(240,800),0);
            bool used=saved->used;saved->used=false;test->used=false;C3D_FrameEnd(0);C3D_FrameBegin(0);saved->used=used;
            GSPGPU_InvalidateDataCache(readback,240*800*4);
            for(unsigned i=0;i<240*800*4;++i)copy[i]=((volatile u8*)readback)[i];
            char path[128];snprintf(path,sizeof(path),"sdmc:/3ds/melee/efb-discard-%u-%u.bin",width,mode);FILE*f=fopen(path,"wb");
            if(!f||fwrite(copy,1,240*800*4,f)!=240*800*4||fclose(f))mp_native_panic("Copy-clear fixture write failed");
        }
    }
    bool used=saved->used;saved->used=false;test->used=false;C3D_FrameEnd(0);C3D_RenderTargetDelete(test);linearFree(readback);free(copy);
    target=saved;C3D_FrameBegin(0);saved->used=used;C3D_FrameDrawOn(target);screen_viewport(saved_width);raster_state_invalidate();
    efb_discard_disable=saved_disable;mp_native_stereo_depth=saved_depth;discarded_image=discarded_image_bytes=0;
    FILE*f=fopen("sdmc:/3ds/melee/efb-discard-report.json","w");
    if(!f)mp_native_panic("Copy-clear report failed");
    fprintf(f,"{\"request\":%u,\"discarded\":%u,\"cases\":64}\n",token,efb_discarded_copies-before);fclose(f);efb_discard_verify=0;
}
