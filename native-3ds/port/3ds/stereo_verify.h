/* Development-only pixel reference: perspective depth and flat HUD through
 * the same native submission path, without changing the active match. */
static volatile unsigned stereo_verify;
static void verify_stereo(void){
    if(!stereo_active)return;
    C3D_RenderTarget*saved=target,*test=C3D_RenderTargetCreate(240,800,GPU_RB_RGBA8,(C3D_DEPTHTYPE){.__i=GPU_RB_DEPTH24_STENCIL8});
    u8*readback=linearAlloc(240*800*4),*copy=malloc(240*800*4);
    if(!test||!readback||!copy)mp_native_panic("Stereo verification allocation failed");
    const u16 ix[6]={0,0x100,0x200,0,0x200,0x300};target=test;
    unsigned depth=mp_native_stereo_depth,reference=stereo_order_reference;
    for(unsigned mode=0;mode<8;++mode){
        stereo_order_reference=mode>=4;
        mp_native_stereo_depth=mode&1?500:1000;unsigned width=(mode&3)<2?320:400;
        C3D_RenderTargetClear(test,C3D_CLEAR_ALL,0x000000ff,0);C3D_FrameDrawOn(test);screen_viewport(width);raster_state_invalidate();
        for(unsigned item=0;item<5;++item){
            float w=item==0?50:item==2?200:item==3?1:100,x=item==4?0:-.6f+.4f*item;
            Vertex v[4]={0};float xy[4][2]={{x-.04f,-.2f},{x-.04f,.2f},{x+.04f,.2f},{x+.04f,-.2f}};
            if(item==4)for(unsigned i=0;i<4;++i)xy[i][1]+=.65f;
            for(unsigned i=0;i<4;++i){v[i].p[0]=xy[i][0]*w;v[i].p[1]=xy[i][1]*w;v[i].p[2]=-.5f*w;v[i].p[3]=w;v[i].n[3]=-1;
                v[i].c[0]=item==0||item==3||item==4;v[i].c[1]=item==1||item==3;v[i].c[2]=item>=2;v[i].c[3]=1;
                for(unsigned k=0;k<14;++k)((u32*)&v[i])[k]=__builtin_bswap32(((u32*)&v[i])[k]);}
            Draw d={.func=7,.color_mask=7,.alpha=7|(7<<13),.scissor={0,0,640,480},.screen_width=width,.indices=(u32)ix,.index_count=6,.convergence=item==3?0:item==4?150:100};
            for(unsigned k=0;k<sizeof(d)/4;++k)((u32*)&d)[k]=__builtin_bswap32(((u32*)&d)[k]);
            mp_native_submit(v,4,&d);
        }
        flush_dynamic();C3D_SyncDisplayTransfer(test->frameBuf.colorBuf,GX_BUFFER_DIM(240,800),(u32*)readback,GX_BUFFER_DIM(240,800),0);
        C3D_FrameEnd(0);C3D_FrameBegin(0);
        GSPGPU_InvalidateDataCache(readback,240*800*4);for(unsigned i=0;i<240*800*4;++i)copy[i]=((volatile u8*)readback)[i];
        char path[90];snprintf(path,sizeof(path),"sdmc:/3ds/melee/stereo-fixture-%u.bin",mode);FILE*f=fopen(path,"wb");
        if(!f)mp_native_panic("Stereo verification output failed");fwrite(copy,1,240*800*4,f);fclose(f);
    }
    C3D_FrameEnd(0);C3D_RenderTargetDelete(test);linearFree(readback);free(copy);C3D_FrameBegin(0);
    mp_native_stereo_depth=depth;stereo_order_reference=reference;target=saved;C3D_FrameDrawOn(target);screen_viewport(320);raster_state_invalidate();stereo_verify=0;
}
