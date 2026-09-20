/* Actual GPU comparisons of every enum predicate combination. The previous
 * shader is a separate compiled program; this is not a CPU color oracle. */
static volatile unsigned lighting_verify;
static volatile unsigned lighting_verify_page;
static volatile unsigned lighting_unit_verify;
static void verify_lighting_paths(void){
    if(!stereo_active)return;
    unsigned token=lighting_verify;
    C3D_RenderTarget*saved=target,*test=C3D_RenderTargetCreate(240,800,GPU_RB_RGBA8,(C3D_DEPTHTYPE){.__i=GPU_RB_DEPTH24_STENCIL8});
    u8*readback=linearAlloc(240*800*4),*copy=malloc(240*800*4);
    if(!test||!readback||!copy)mp_native_panic("Lighting verification allocation failed");
    unsigned shortcuts=shader_shortcuts_disable,lighting=lighting_uniform_disable;
    unsigned unit_mode=lighting_unit_verify,unit_saved=unit_attenuation_disable,skipped_before=unit_attenuation_vertices;
    shader_shortcuts_disable=1;
    const u16 ix[6]={0,0x100,0x200,0,0x200,0x300};
    unsigned draw_counts[2]={0};
    target=test;
    for(unsigned page=0;page<61;++page){
        lighting_verify_page=page;
        for(unsigned mode=0;mode<2;++mode){
            lighting_uniform_disable=unit_mode?0:mode==0;
            if(unit_mode)unit_attenuation_disable=mode==0;
            C3D_RenderTargetClear(test,C3D_CLEAR_ALL,0x000000ff,0);
            C3D_FrameDrawOn(test);screen_viewport(400);raster_state_invalidate();
            for(unsigned cell=0;cell<48;++cell){
                unsigned item=page*48+cell;if(item>=2916)break;
                unsigned tags=item%81,atten=item/81%9,enabled=item/(81*9),row=(item&1)?27:0;
                MPGPUUniforms u={0};u.matrix_rows=30;
                for(unsigned k=0;k<3;++k){u.value[row+k][k]=1;u.value[30+row+k][k]=1;}
                u.value[60][1]=1;u.value[61][0]=-1;u.value[62][2]=1;u.value[63][3]=1;
                for(unsigned k=0;k<4;++k){u.material0[k]=-1;u.value[93][k]=.6f;u.value[94][k]=1;u.value[85][k]=.55f;}
                for(unsigned ch=0;ch<2;++ch){
                    u.value[91+ch][0]=!!(enabled&(1u<<ch));
                    u.value[91+ch][1]=item%3==0;u.value[91+ch][2]=item%3!=1;
                    u.value[91+ch][3]=ch?atten/3:atten%3;
                    for(unsigned k=0;k<3;++k)u.value[89+ch][k]=.15f;
                }
                for(unsigned k=0;k<3;++k)u.value[87][k]=.4f;
                for(unsigned i=0;i<4;++i){
                    unsigned tag=tags%3;tags/=3;
                    if(tag&&!(enabled&(1u<<(tag-1))))tag=0;
                    u.value[72+i][3]=tag;
                    for(unsigned k=0;k<3;++k)u.value[72+i][k]=.1f+.1f*((i+k)%3);
                    u.value[64+i][0]=.25f*(i-1.f);u.value[64+i][1]=.3f*(i&1);u.value[64+i][2]=1;
                    u.value[64+i][3]=(item+i)&1;
                    u.value[68+i][2]=1;
                    u.value[76+i][0]=.75f;u.value[76+i][1]=.1f;u.value[76+i][2]=.02f;
                    u.value[80+i][0]=.2f;u.value[80+i][1]=.3f;u.value[80+i][2]=.2f;
                    if(unit_mode&&((item+i)%4)!=2){
                        u.value[76+i][0]=u.value[80+i][0]=1;
                        for(unsigned j=1;j<3;++j)u.value[76+i][j]=u.value[80+i][j]=((item+i)%4)==3?-0.0f:0.0f;
                        if(((item+i)%4)==1)u.value[76+i][2]=1.0101e-10f;
                    }
                }
                Vertex v[4]={0};float x=-.94f+.245f*(cell%8),y=-.91f+.32f*(cell/8);
                float xy[4][2]={{x,y},{x,y+.23f},{x+.17f,y+.23f},{x+.17f,y}};
                for(unsigned i=0;i<4;++i){
                    memcpy(v[i].p,xy[i],8);v[i].p[2]=-.5f;v[i].p[3]=1;
                    v[i].n[0]=.15f;v[i].n[1]=.1f;v[i].n[2]=1;v[i].n[3]=(float)row;
                    v[i].c[0]=.7f;v[i].c[1]=.8f;v[i].c[2]=.9f;v[i].c[3]=1;
                    for(unsigned k=0;k<14;++k)((u32*)&v[i])[k]=__builtin_bswap32(((u32*)&v[i])[k]);
                }
                for(unsigned k=0;k<sizeof(u)/4;++k)((u32*)&u)[k]=__builtin_bswap32(((u32*)&u)[k]);
                Draw d={.func=7,.color_mask=7,.alpha=7|(7<<13),.scissor={0,0,640,480},.screen_width=400,
                        .indices=(u32)ix,.index_count=6,.gpu=(u32)&u,.convergence=1};
                for(unsigned k=0;k<sizeof(d)/4;++k)((u32*)&d)[k]=__builtin_bswap32(((u32*)&d)[k]);
                mp_native_submit(v,4,&d);++draw_counts[mode];
            }
            flush_dynamic();C3D_SyncDisplayTransfer(test->frameBuf.colorBuf,GX_BUFFER_DIM(240,800),(u32*)readback,GX_BUFFER_DIM(240,800),0);
            C3D_FrameEnd(0);C3D_FrameBegin(0);GSPGPU_InvalidateDataCache(readback,240*800*4);
            for(unsigned i=0;i<240*800*4;++i)copy[i]=((volatile u8*)readback)[i];
            char path[100];snprintf(path,sizeof(path),"sdmc:/3ds/melee/%slighting-fixture-%u-%u.bin",unit_mode?"unit-":"",page,mode);
            FILE*f=fopen(path,"wb");if(!f)mp_native_panic("Lighting verification output failed");
            if(fwrite(copy,1,240*800*4,f)!=240*800*4)mp_native_panic("Lighting verification output truncated");fclose(f);
        }
    }
    FILE*f=fopen(unit_mode?"sdmc:/3ds/melee/unit-lighting-fixture-report.json":"sdmc:/3ds/melee/lighting-fixture-report.json","wb");if(!f)mp_native_panic("Lighting report failed");
    fprintf(f,"{\"token\":%u,\"pages\":61,\"cases\":2916,\"draws\":[%u,%u]",token,draw_counts[0],draw_counts[1]);
    if(unit_mode)fprintf(f,",\"unit_vertex_evaluations_skipped\":%u",unit_attenuation_vertices-skipped_before);
    fprintf(f,"}\n");fclose(f);
    C3D_FrameEnd(0);C3D_RenderTargetDelete(test);linearFree(readback);free(copy);C3D_FrameBegin(0);
    shader_shortcuts_disable=shortcuts;lighting_uniform_disable=lighting;
    unit_attenuation_disable=unit_saved;lighting_unit_verify=0;
    target=saved;C3D_FrameDrawOn(target);screen_viewport(320);raster_state_invalidate();lighting_verify=0;
}
