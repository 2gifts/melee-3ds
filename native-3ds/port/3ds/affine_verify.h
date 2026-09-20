/* Separate update-17 shader versus the identity-color candidate. */
static volatile unsigned affine_verify,affine_verify_page;
static void verify_affine_paths(void){
    if(!stereo_active)return;
    unsigned token=affine_verify,lighting=lighting_uniform_disable,disabled=affine_identity_disable;
    unsigned shortcuts=shader_shortcuts_disable;shader_shortcuts_disable=0;affine_identity_disable=0;
    C3D_RenderTarget*saved=target,*test=C3D_RenderTargetCreate(240,800,GPU_RB_RGBA8,(C3D_DEPTHTYPE){.__i=GPU_RB_DEPTH24_STENCIL8});
    u8*readback=linearAlloc(240*800*4),*copy=malloc(240*800*4);
    if(!test||!readback||!copy)mp_native_panic("Affine fixture allocation failed");
    const u16 ix[6]={0,0x100,0x200,0,0x200,0x300};unsigned routes[2][2]={{0}};
    target=test;
    for(unsigned page=0;page<16;++page){affine_verify_page=page;
        for(unsigned mode=0;mode<2;++mode){
            lighting_uniform_disable=mode==0;
            unsigned before[2];memcpy(before,affine_identity_draws,sizeof(before));
            C3D_RenderTargetClear(test,C3D_CLEAR_ALL,0x000000ff,0);C3D_FrameDrawOn(test);
            screen_viewport(400);raster_state_invalidate();
            for(unsigned cell=0;cell<48;++cell){
                unsigned item=page*48+cell,row=(item&1)?27:0;
                MPGPUUniforms u={0};u.matrix_rows=30;
                for(unsigned k=0;k<3;++k){u.value[row+k][k]=1;u.value[30+row+k][k]=1;}
                u.value[60][1]=1;u.value[61][0]=-1;u.value[62][2]=1;u.value[63][3]=1;
                for(unsigned k=0;k<4;++k){
                    u.value[85][k]=u.value[94][k]=1;u.value[93][k]=.6f;
                    u.material0[k]=(item%3==0||(item%3==2&&k%2))?-1:((item*37+k*31)&255)/255.f;
                    if(item&2)u.value[84][k]=-0.f;
                }
                if(item&1){u.value[85][3]=0;u.value[86][3]=1;}
                if(item%4==0)u.value[84][0]=.125f; /* General fallback amid identity draws. */
                if(item%3){
                    u.value[91][0]=1;u.value[91][2]=1;u.value[91][3]=2;
                    for(unsigned k=0;k<3;++k)u.value[89][k]=.15f;
                    if(item%3==2){u.value[64][2]=u.value[64][3]=1;
                        u.value[72][0]=.7f;u.value[72][1]=.3f;u.value[72][2]=.6f;u.value[72][3]=1;}
                }
                Vertex v[4]={0};float x=-.94f+.245f*(cell%8),y=-.91f+.32f*(cell/8);
                float xy[4][2]={{x,y},{x,y+.23f},{x+.17f,y+.23f},{x+.17f,y}};
                for(unsigned i=0;i<4;++i){
                    memcpy(v[i].p,xy[i],8);v[i].p[2]=-.5f;v[i].p[3]=1;
                    v[i].n[0]=.15f;v[i].n[1]=.1f;v[i].n[2]=1;v[i].n[3]=row;
                    for(unsigned k=0;k<4;++k)v[i].c[k]=((item+i*83+k*47)&255)/255.f;
                    for(unsigned k=0;k<14;++k)((u32*)&v[i])[k]=__builtin_bswap32(((u32*)&v[i])[k]);
                }
                for(unsigned k=0;k<sizeof(u)/4;++k)((u32*)&u)[k]=__builtin_bswap32(((u32*)&u)[k]);
                Draw d={.func=7,.color_mask=15,.alpha=7|(7<<13),.scissor={0,0,640,480},.screen_width=400,
                    .indices=(u32)ix,.index_count=6,.gpu=(u32)&u,.convergence=1};
                for(unsigned k=0;k<sizeof(d)/4;++k)((u32*)&d)[k]=__builtin_bswap32(((u32*)&d)[k]);
                mp_native_submit(v,4,&d);
            }
            for(unsigned i=0;i<2;++i)routes[mode][i]+=affine_identity_draws[i]-before[i];
            flush_dynamic();C3D_SyncDisplayTransfer(test->frameBuf.colorBuf,GX_BUFFER_DIM(240,800),(u32*)readback,GX_BUFFER_DIM(240,800),0);
            C3D_FrameEnd(0);C3D_FrameBegin(0);GSPGPU_InvalidateDataCache(readback,240*800*4);
            for(unsigned i=0;i<240*800*4;++i)copy[i]=((volatile u8*)readback)[i];
            char path[100];snprintf(path,sizeof(path),"sdmc:/3ds/melee/affine-fixture-%u-%u.bin",page,mode);
            FILE*f=fopen(path,"wb");if(!f)mp_native_panic("Affine fixture output failed");
            if(fwrite(copy,1,240*800*4,f)!=240*800*4)mp_native_panic("Affine fixture output truncated");fclose(f);
        }
    }
    FILE*f=fopen("sdmc:/3ds/melee/affine-fixture-report.json","wb");if(!f)mp_native_panic("Affine report failed");
    fprintf(f,"{\"token\":%u,\"pages\":16,\"cases\":768,\"routes\":[[%u,%u],[%u,%u]]}\n",token,routes[0][0],routes[0][1],routes[1][0],routes[1][1]);fclose(f);
    C3D_FrameEnd(0);C3D_RenderTargetDelete(test);linearFree(readback);free(copy);C3D_FrameBegin(0);
    shader_shortcuts_disable=shortcuts;lighting_uniform_disable=lighting;affine_identity_disable=disabled;
    target=saved;C3D_FrameDrawOn(target);screen_viewport(320);raster_state_invalidate();affine_verify=0;
}
