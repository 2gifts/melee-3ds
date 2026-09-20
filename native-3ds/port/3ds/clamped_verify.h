/* Synthetic native-GPU comparison of the exact new algebra against CPU colors.
 * This is a development fixture only, independent of the game's camera. */
static volatile unsigned clamped_verify;
static void verify_clamped_paths(void){
    if(!stereo_active)return;
    C3D_RenderTarget*saved=target,*test=C3D_RenderTargetCreate(240,800,GPU_RB_RGBA8,(C3D_DEPTHTYPE){.__i=GPU_RB_DEPTH24_STENCIL8});
    u8*readback=linearAlloc(240*800*4),*copy=malloc(240*800*4);
    if(!test||!readback||!copy)mp_native_panic("Clamped verification allocation failed");
    const u16 ix[6]={0,0x100,0x200,0,0x200,0x300};target=test;
    for(unsigned mode=0;mode<2;++mode){
        C3D_RenderTargetClear(test,C3D_CLEAR_ALL,0x000000ff,0);C3D_FrameDrawOn(test);screen_viewport(400);raster_state_invalidate();
        for(unsigned item=0;item<8;++item){
            MPGPUUniforms u={0};unsigned row=(item&1)?27:0;
            u.matrix_rows=30;u.post_transform=mode;
            for(unsigned k=0;k<3;++k){u.value[row+k][k]=1;u.value[30+row+k][k]=1;}
            u.value[60][1]=1;u.value[61][0]=-1;u.value[62][2]=1;u.value[63][3]=1;
            float color[4]={.3f,.6f,.9f,1},second[4]={.4f,.7f,.2f,1};
            float light[3]={.6f,.8f,.4f},light1[3]={.2f,.0f,.7f};
            unsigned lit=item>=4,two=item&1;
            float scale=item%3==0?.501961f:.901961f,bias=1-scale;
            for(unsigned k=0;k<4;++k){u.material0[k]=-1;u.value[93][k]=second[k];u.value[94][k]=1;
                u.value[85][k]=k==3?0:.701961f;u.value[87][k]=k==3?0:1;
                u.value[84][k]=k==3?(item==2?.5f:.992157f):item==3?-1:item==7?1:0;
                u.post_scale[k]=k==3?1:scale;u.post_bias[k]=k==3?0:bias;}
            if(lit){u.value[91][0]=1;u.value[91][2]=1;u.value[91][3]=2;
                u.value[64][2]=1;u.value[72][3]=1;
                for(unsigned k=0;k<3;++k){u.value[72][k]=light[k];u.value[89][k]=.1f;}
                if(two){u.value[92][0]=1;u.value[92][2]=1;u.value[92][3]=2;u.value[65][2]=1;u.value[73][3]=2;
                    for(unsigned k=0;k<3;++k){u.value[73][k]=light1[k];u.value[90][k]=.1f;}}
            }
            Vertex v[4]={0};float x=-.9f+.45f*(item&3),y=item<4?-.65f:.15f;
            float xy[4][2]={{x,y},{x,y+.45f},{x+.3f,y+.45f},{x+.3f,y}};
            for(unsigned i=0;i<4;++i){v[i].p[0]=xy[i][0];v[i].p[1]=xy[i][1];v[i].p[2]=-.5f;v[i].p[3]=1;v[i].n[2]=1;v[i].n[3]=mode?(float)row:-1;
                for(unsigned k=0;k<4;++k){float a=color[k],b=second[k];
                    if(lit&&k<3){a*=fminf(1,.1f+light[k]);if(two)b*=fminf(1,.1f+light1[k]);}
                    float inner=u.value[84][k]+u.value[85][k]*a+u.value[87][k]*b;
                    v[i].c[k]=mode?color[k]:u.post_bias[k]+u.post_scale[k]*fminf(1,fmaxf(0,inner));}
                for(unsigned k=0;k<14;++k)((u32*)&v[i])[k]=__builtin_bswap32(((u32*)&v[i])[k]);}
            for(unsigned k=0;k<sizeof(u)/4;++k)((u32*)&u)[k]=__builtin_bswap32(((u32*)&u)[k]);
            Draw d={.func=7,.color_mask=7,.alpha=7|(7<<13),.scissor={0,0,640,480},.screen_width=400,.indices=(u32)ix,.index_count=6,.gpu=(u32)&u,.convergence=1};
            for(unsigned k=0;k<sizeof(d)/4;++k)((u32*)&d)[k]=__builtin_bswap32(((u32*)&d)[k]);mp_native_submit(v,4,&d);
        }
        flush_dynamic();C3D_SyncDisplayTransfer(test->frameBuf.colorBuf,GX_BUFFER_DIM(240,800),(u32*)readback,GX_BUFFER_DIM(240,800),0);
        C3D_FrameEnd(0);C3D_FrameBegin(0);GSPGPU_InvalidateDataCache(readback,240*800*4);
        for(unsigned i=0;i<240*800*4;++i)copy[i]=((volatile u8*)readback)[i];
        char path[90];snprintf(path,sizeof(path),"sdmc:/3ds/melee/clamped-fixture-%u.bin",mode);FILE*f=fopen(path,"wb");
        if(!f)mp_native_panic("Clamped verification output failed");fwrite(copy,1,240*800*4,f);fclose(f);
    }
    C3D_FrameEnd(0);C3D_RenderTargetDelete(test);linearFree(readback);free(copy);C3D_FrameBegin(0);
    target=saved;C3D_FrameDrawOn(target);screen_viewport(320);raster_state_invalidate();clamped_verify=0;
}
