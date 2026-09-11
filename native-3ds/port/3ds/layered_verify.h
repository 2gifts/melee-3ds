/* Compare actual two-UV GPU draws with an independently baked reference.
 * All textures, framebuffers and controls are development fixtures only. */
static volatile unsigned layered_verify;
static void fixture_rgba8(u8*p,unsigned x,unsigned y,const unsigned c[4]){
    unsigned k=((y/4)*2+x/4)*64+((y%4)*4+x%4)*2;
    p[k]=c[3];p[k+1]=c[0];p[k+32]=c[1];p[k+33]=c[2];
}
static void verify_layered_material(void){
    C3D_RenderTarget*saved=target,*test=C3D_RenderTargetCreate(240,400,GPU_RB_RGBA8,(C3D_DEPTHTYPE){.__i=GPU_RB_DEPTH24_STENCIL8});
    u8*readback=linearAlloc(240*400*4),*copy=malloc(240*400*4);
    static u8 base[256] __attribute__((aligned(32))),face[64] __attribute__((aligned(32))),reference[256] __attribute__((aligned(32)));
    static const u16 ix[6]={0,0x100,0x200,0,0x200,0x300};
    if(!test||!readback||!copy)mp_native_panic("Layer verification allocation failed");
    unsigned saved_budget=texture_budget,saved_slots=texture_slot_budget;target=test;
    for(unsigned pressure=0;pressure<2;++pressure)for(unsigned sample=0;sample<4;++sample){
        texture_budget=pressure?128:saved_budget;texture_slot_budget=pressure?2:saved_slots;
        unsigned tint[3]={67+sample*41,220-sample*33,157},weight=sample*85,initial_alpha=255-sample*39;
        for(unsigned y=0;y<8;++y)for(unsigned x=0;x<8;++x)face[(y/4)*32+(y%4)*8+x]=(x*29+y*19+sample*43)&255;
        for(unsigned y=0;y<8;++y)for(unsigned x=0;x<8;++x){
            unsigned c[4]={(x*31+y*11+37)&255,(x*17+y*23+91)&255,(x*47+y*7+143)&255,255},r[4];
            fixture_rgba8(base,x,y,c);unsigned mask=face[(y/4)*32+(y%4)*8+7-x];
            for(unsigned k=0;k<3;++k){int v=((int)tint[k]*(255-weight)+(int)c[k]*weight+127)/255-(int)mask;r[k]=v>0?v:0;}
            r[3]=(initial_alpha*(255-mask)+mask*mask+127)/255;fixture_rgba8(reference,x,y,r);
        }
        ++texture_generation;
        for(unsigned pass=0;pass<2;++pass){
            raster_state_invalidate();C3D_RenderTargetClear(test,C3D_CLEAR_ALL,0x12345678,0);C3D_FrameDrawOn(test);C3D_StencilTest(false,GPU_ALWAYS,0,255,255);
            Draw d={0};d.image=(u32)(pass?reference:base);d.w=d.h=8;d.format=6;d.color_mask=15;d.alpha=7|(7<<13);d.blend=1|(1<<16);d.src=1;
            d.texture_rgb=d.texture_alpha=1;d.indices=(u32)ix;d.index_count=6;d.scissor[2]=640;d.scissor[3]=480;d.screen_width=400;
            MPTextureLayer layer={(u32)face,8,8,1,0,0,0,0,0,0,tint[0]|(tint[1]<<8)|(tint[2]<<16),weight,initial_alpha},wire_layer;
            float uv[4][2]={{1,0},{0,0},{0,1},{1,1}};u32 wire_uv[8];
            for(unsigned i=0;i<8;++i)wire_uv[i]=__builtin_bswap32(((u32*)uv)[i]);layer.uv=(u32)wire_uv;
            for(unsigned i=0;i<sizeof(layer)/4;++i)((u32*)&wire_layer)[i]=__builtin_bswap32(((u32*)&layer)[i]);
            if(!pass)d.layer=(u32)&wire_layer;
            /* Set nearest filtering on both already-uploaded sources; the
             * fixture compares exact texels, not different filtering orders. */
            Draw second={0};memcpy(&second,&layer,7*4);
            Texture*t=texture(&second);C3D_TexSetFilter(&t->tex,GPU_NEAREST,GPU_NEAREST);
            texture_pin=&second;texture_pin_bytes=t->tex.size;t=texture(&d);C3D_TexSetFilter(&t->tex,GPU_NEAREST,GPU_NEAREST);texture_pin=NULL;texture_pin_bytes=0;
            Vertex v[4]={0},wire_v[4];float xy[4][2]={{-1,1},{1,1},{1,-1},{-1,-1}};
            for(unsigned i=0;i<4;++i){v[i].p[0]=xy[i][0];v[i].p[1]=xy[i][1];v[i].p[2]=-.5f;v[i].p[3]=1;v[i].n[3]=-1;
                v[i].c[0]=.9f;v[i].c[1]=.7f;v[i].c[2]=.5f;v[i].c[3]=.8f;v[i].t[0]=1-uv[i][0];v[i].t[1]=uv[i][1];}
            Draw wire;for(unsigned i=0;i<sizeof(d)/4;++i)((u32*)&wire)[i]=__builtin_bswap32(((u32*)&d)[i]);
            for(unsigned i=0;i<sizeof(v)/4;++i)((u32*)wire_v)[i]=__builtin_bswap32(((u32*)v)[i]);
            mp_native_submit(wire_v,4,&wire);flush_dynamic();
            C3D_SyncDisplayTransfer(test->frameBuf.colorBuf,GX_BUFFER_DIM(240,400),(u32*)readback,GX_BUFFER_DIM(240,400),0);
            bool used=saved->used;saved->used=false;test->used=false;C3D_FrameEnd(0);C3D_FrameBegin(0);saved->used=used;
            GSPGPU_InvalidateDataCache(readback,240*400*4);for(unsigned i=0;i<240*400*4;++i)copy[i]=((volatile u8*)readback)[i];
            char path[96];snprintf(path,sizeof(path),"sdmc:/3ds/melee/layer-%u-%u-%u.bin",pressure,sample,pass);FILE*f=fopen(path,"wb");
            if(!f)mp_native_panic("Layer verification output failed");fwrite(copy,1,240*400*4,f);fclose(f);
        }
    }
    texture_budget=saved_budget;texture_slot_budget=saved_slots;
    bool used=saved->used;saved->used=false;C3D_FrameEnd(0);C3D_RenderTargetDelete(test);linearFree(readback);free(copy);
    target=saved;C3D_FrameBegin(0);target->used=used;C3D_FrameDrawOn(target);bind_program(0);screen_viewport(320);
    raster_state_invalidate();for(unsigned i=0;i<6;++i)C3D_TexEnvInit(C3D_GetTexEnv(i));layered_verify=0;
}
