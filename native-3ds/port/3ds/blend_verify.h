/* Emulator fixture: native blend/logic output versus independent GX math. */
static volatile unsigned blend_verify;
static unsigned blend_reference_logic(unsigned op,unsigned s,unsigned d){
    switch(op){case 0:return 0;case 1:return s&d;case 2:return s&~d;case 3:return s;
    case 4:return ~s&d;case 5:return d;case 6:return s^d;case 7:return s|d;
    case 8:return ~(s|d);case 9:return ~(s^d);case 10:return ~d;case 11:return s|~d;
    case 12:return ~s;case 13:return ~s|d;case 14:return ~(s&d);default:return 255;}
}
static unsigned blend_reference_factor(unsigned factor,unsigned opposite,unsigned sa,unsigned da){
    switch(factor){case 0:return 0;case 1:return 255;case 2:return opposite;case 3:return 255-opposite;
    case 4:return sa;case 5:return 255-sa;case 6:return da;default:return 255-da;}
}
static void verify_blending(void){
    C3D_RenderTarget*test=C3D_RenderTargetCreate(256,256,GPU_RB_RGBA8,(C3D_DEPTHTYPE){.__i=-1});
    u8*readback=linearAlloc(256*256*4);u8*copy=malloc(256*256*4);
    if(!test||!readback||!copy)mp_native_panic("Blend verification allocation failed");
    const unsigned source[4]={173,97,43,181},destination[4]={79,131,211,113};
    for(unsigned reference=0;reference<2;++reference){
        C3D_RenderTargetClear(test,C3D_CLEAR_COLOR,0x4f83d371,0);C3D_FrameDrawOn(test);bind_program(0);
        C3D_CullFace(GPU_CULL_NONE);C3D_SetScissor(GPU_SCISSOR_DISABLE,0,0,0,0);
        for(unsigned i=0;i<6;++i)C3D_TexEnvInit(C3D_GetTexEnv(i));
        C3D_TexEnv*env=C3D_GetTexEnv(0);C3D_TexEnvSrc(env,C3D_Both,GPU_PRIMARY_COLOR,0,0);C3D_TexEnvFunc(env,C3D_Both,GPU_REPLACE);
        C3D_AlphaTest(false,GPU_ALWAYS,0);C3D_StencilTest(false,GPU_ALWAYS,0,255,255);
        for(unsigned test_case=0;test_case<148;++test_case){
            unsigned mode=test_case<128?1:test_case<144?2:test_case<146?0:3;
            unsigned has_alpha=test_case<128?test_case/64:1,src=(test_case/8)&7,dst=test_case&7,logic=(test_case-128)&15;
            unsigned expected[4];
            for(unsigned k=0;k<4;++k){
                if(mode==2)expected[k]=blend_reference_logic(logic,source[k],destination[k])&255;
                else if(mode==0)expected[k]=source[k];
                else if(mode==3)expected[k]=destination[k]>source[k]?destination[k]-source[k]:0;
                else{unsigned da=has_alpha?destination[3]:255;
                    unsigned sf=blend_reference_factor(src,k==3?da:destination[k],source[3],da);
                    unsigned df=blend_reference_factor(dst,source[k],source[3],da);
                    unsigned value=(source[k]*sf+destination[k]*df+127)/255;expected[k]=value<255?value:255;}
            }
            if(reference)raster_blend(0,0,0);else raster_blend(mode|(logic<<8)|(has_alpha<<16),src,dst);
            C3D_DepthTest(false,GPU_ALWAYS,GPU_WRITE_RED|GPU_WRITE_GREEN|GPU_WRITE_BLUE|(has_alpha?GPU_WRITE_ALPHA:0));
            float x=-1+(test_case%16*16+2)/128.f,y=-1+(test_case/16*16+2)/128.f;
            float xy[4][2]={{x,y},{x+12/128.f,y},{x+12/128.f,y+12/128.f},{x,y+12/128.f}};const unsigned corners[]={0,1,2,0,2,3};
            reserve_dynamic(6,0);
            for(unsigned i=0;i<6;++i){Vertex*v=&vertices[vertex_count+i];memset(v,0,sizeof(*v));v->n[3]=-1;
                v->p[0]=xy[corners[i]][0];v->p[1]=xy[corners[i]][1];v->p[2]=-.5f;v->p[3]=1;
                for(unsigned k=0;k<4;++k)v->c[k]=(reference?expected[k]:source[k])/255.f;}
            vertex_base(0);C3D_DrawArrays(GPU_TRIANGLES,vertex_count,6);vertex_count+=6;
        }
        flush_dynamic();C3D_SyncDisplayTransfer(test->frameBuf.colorBuf,GX_BUFFER_DIM(256,256),(u32*)readback,GX_BUFFER_DIM(256,256),0);
        bool used=target->used;target->used=false;C3D_FrameEnd(0);C3D_FrameBegin(0);target->used=used;
        GSPGPU_InvalidateDataCache(readback,256*256*4);for(unsigned i=0;i<256*256*4;++i)copy[i]=((volatile u8*)readback)[i];
        char path[80];snprintf(path,sizeof(path),"sdmc:/3ds/melee/blending-%u.bin",reference);FILE*f=fopen(path,"wb");
        if(!f)mp_native_panic("Blend verification output failed");fwrite(copy,1,256*256*4,f);fclose(f);
    }
    bool used=target->used;target->used=false;C3D_FrameEnd(0);C3D_RenderTargetDelete(test);linearFree(readback);free(copy);
    C3D_FrameBegin(0);target->used=used;C3D_FrameDrawOn(target);C3D_CullFace(GPU_CULL_NONE);bind_program(0);blend_verify=0;
}
