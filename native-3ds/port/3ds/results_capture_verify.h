/* Exercise both possible starting eyes, a real left-eye RGB5A3 readback,
 * the required two-eye clear, and subsequent visible textured geometry. */
static volatile unsigned results_capture_verify;
static void capture_fixture_quad(unsigned column,unsigned row,unsigned width,
                                 unsigned flags,unsigned color,unsigned image){
    static const u16 ix[6]={0,0x100,0x200,0,0x200,0x300};
    Draw d={0},wired;Vertex v[4]={0},wirev[4];
    d.depth=1;d.write=1;d.func=3;d.color_mask=15;d.blend=flags;
    d.alpha=7|(7<<13);d.indices=(u32)ix;d.index_count=6;
    d.scissor[2]=640;d.scissor[3]=480;d.screen_width=width;
    if(image){d.image=image;d.w=d.h=8;d.format=5;d.texture_rgb=d.texture_alpha=1;}
    float x=column*.5f-1,y=1-row*.5f;
    float xy[4][2]={{x+.02f,y-.02f},{x+.48f,y-.02f},{x+.48f,y-.48f},{x+.02f,y-.48f}};
    for(unsigned i=0;i<4;++i){
        /* mp_native_submit rotates BE8 clip-space positions for the LCD. */
        v[i].p[0]=xy[i][0];v[i].p[1]=xy[i][1];v[i].p[2]=flags?-.9f:-.6f;v[i].p[3]=1;v[i].n[3]=-1;
        for(unsigned c=0;c<4;++c)v[i].c[c]=((color>>(24-8*c))&255)/255.f;
        v[i].t[0]=(i==1||i==2);v[i].t[1]=(i>=2);
    }
    for(unsigned i=0;i<sizeof(d)/4;++i)((u32*)&wired)[i]=__builtin_bswap32(((u32*)&d)[i]);
    for(unsigned i=0;i<sizeof(v)/4;++i)((u32*)wirev)[i]=__builtin_bswap32(((u32*)v)[i]);
    mp_native_submit(wirev,4,&wired);
}
static void verify_results_capture(void){
    if(!stereo_active)return;
    unsigned token=results_capture_verify,saved_disable=results_capture_disable;
    unsigned saved_width=render_width,saved_discard=efb_discard_disable;
    unsigned before=results_capture_draws;
    C3D_RenderTarget*saved=target,*test=C3D_RenderTargetCreate(240,800,GPU_RB_RGBA8,(C3D_DEPTHTYPE){.__i=GPU_RB_DEPTH24_STENCIL8});
    u8*readback=linearAlloc(240*800*4),*copy=malloc(240*800*4);
    static u8 portrait[128] __attribute__((aligned(32)));
    if(!test||!readback||!copy)mp_native_panic("Portrait fixture allocation failed");
    target=test;efb_discard_disable=0;
    for(unsigned wi=0;wi<2;++wi){unsigned width=wi?400:320;
        for(unsigned mode=0;mode<2;++mode){
            results_capture_disable=!mode;
            C3D_RenderTargetClear(test,C3D_CLEAR_ALL,0x293a4bff,0x800000);C3D_FrameDrawOn(test);
            screen_viewport(width);raster_state_invalidate();C3D_StencilTest(false,GPU_ALWAYS,0,255,255);
            for(unsigned cell=0;cell<16;++cell){unsigned column=cell%4,row=cell/4;
                screen_viewport(width);
                /* The normal pair ends in the right eye. Alternate whether
                 * the marked capture inherits that eye or starts left. */
                capture_fixture_quad(column,row,width,0,0xff0000ff,0);
                if(!(cell&1))screen_viewport(width);
                unsigned color=0x307050ffu+cell*0x09050300u;
                capture_fixture_quad(column,row,width,MP_DRAW_LEFT_CAPTURE,color,0);
                u32 q[14]={(u32)portrait,column*160,row*120,160,120,8,8,5,1,0x293a4bff,0x800000,15,1,0},wireq[14];
                for(unsigned i=0;i<14;++i)wireq[i]=__builtin_bswap32(q[i]);
                mp_native_efb_copy(wireq);
                /* The displayed portrait must be identical in BOTH eyes.
                 * Repeated source address also exercises copy invalidation. */
                capture_fixture_quad(column,row,width,0,0xffffffff,(u32)portrait);
            }
            flush_dynamic();C3D_SyncDisplayTransfer(test->frameBuf.colorBuf,GX_BUFFER_DIM(240,800),(u32*)readback,GX_BUFFER_DIM(240,800),0);
            bool used=saved->used;saved->used=false;test->used=false;C3D_FrameEnd(0);C3D_FrameBegin(0);saved->used=used;
            GSPGPU_InvalidateDataCache(readback,240*800*4);
            for(unsigned i=0;i<240*800*4;++i)copy[i]=((volatile u8*)readback)[i];
            char path[128];snprintf(path,sizeof(path),"sdmc:/3ds/melee/results-capture-%u-%u.bin",width,mode);FILE*f=fopen(path,"wb");
            if(!f||fwrite(copy,1,240*800*4,f)!=240*800*4||fclose(f))mp_native_panic("Portrait fixture write failed");
        }
    }
    bool used=saved->used;saved->used=false;test->used=false;C3D_FrameEnd(0);C3D_RenderTargetDelete(test);linearFree(readback);free(copy);
    target=saved;C3D_FrameBegin(0);saved->used=used;C3D_FrameDrawOn(target);screen_viewport(saved_width);raster_state_invalidate();
    results_capture_disable=saved_disable;efb_discard_disable=saved_discard;
    FILE*f=fopen("sdmc:/3ds/melee/results-capture-report.json","w");if(!f)mp_native_panic("Portrait report failed");
    fprintf(f,"{\"request\":%u,\"omitted_draws\":%u,\"cases\":32}\n",token,results_capture_draws-before);fclose(f);results_capture_verify=0;
}
