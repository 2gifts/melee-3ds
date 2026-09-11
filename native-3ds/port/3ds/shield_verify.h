/* Development-only GPU readback: all player tints, startup/held shields,
 * independent UVs, intensity/RGBA alpha and eviction pressure. */
static volatile unsigned shield_verify;
static void verify_shield_material(void){
    C3D_RenderTarget*saved=target,*test=C3D_RenderTargetCreate(240,400,GPU_RB_RGBA8,(C3D_DEPTHTYPE){.__i=GPU_RB_DEPTH24_STENCIL8});
    u8*readback=linearAlloc(240*400*4),*copy=malloc(240*400*4);
    static u8 base_image[256] __attribute__((aligned(32))),mask_image[256] __attribute__((aligned(32))),reference[256] __attribute__((aligned(32)));
    static const u16 ix[6]={0,0x100,0x200,0,0x200,0x300};
    static const unsigned colors[6][3]={{242,89,89},{102,102,255},{255,191,64},{77,230,77},{128,128,128},{255,204,204}};
    if(!test||!readback||!copy)mp_native_panic("Shield verification allocation failed");
    unsigned saved_budget=texture_budget,saved_slots=texture_slot_budget;target=test;
    for(unsigned mode=1;mode<=2;++mode)for(unsigned sample=0;sample<36;++sample){
        texture_budget=sample>=18?128:saved_budget;texture_slot_budget=sample>=18?2:saved_slots;
        unsigned fade=sample/6%3;
        unsigned fmt=sample>=18?1:6,weight=fade*127+(fade==2),opacity=sample%2?63:211,initial_alpha=177;
        unsigned base[4]={213,189,241,initial_alpha},highlight[4]={255,243,230,opacity};
        const unsigned*tint=colors[sample%6];
        for(unsigned y=0;y<8;++y)for(unsigned x=0;x<8;++x){
            unsigned a[4]={(x*31+y*11+37)&255,(x*17+y*23+91)&255,(x*47+y*7+143)&255,(x*11+y*37+72)&255};
            unsigned b[4]={(x*23+y*37)&255,(x*47+y*17+83)&255,(x*7+y*11+21)&255,(x*29+y*19+61)&255};
            if(fmt==1){unsigned v=(x*29+y*19+61)&255;for(unsigned j=0;j<4;++j)b[j]=v;mask_image[(y/4)*32+(y%4)*8+7-x]=v;}
            else fixture_rgba8(mask_image,7-x,y,b);
            fixture_rgba8(base_image,x,y,a);
            unsigned r[4];
            for(unsigned j=0;j<4;++j){
                double shield=j==3?opacity*b[3]/255.0:(tint[j]*(255-b[j])+highlight[j]*b[j])/255.0;
                double background=mode==MP_FRAGMENT_TINT?base[j]:base[j]*a[j]/255.0;
                r[j]=(unsigned)((background*(255-weight)+shield*weight)/255.0+.5);
            }
            fixture_rgba8(reference,x,y,r);
        }
        ++texture_generation;
        for(unsigned pass=0;pass<2;++pass){
            raster_state_invalidate();C3D_RenderTargetClear(test,C3D_CLEAR_ALL,0x12345678,0);C3D_FrameDrawOn(test);C3D_StencilTest(false,GPU_ALWAYS,0,255,255);
            Draw d={0};d.image=(u32)(pass?reference:mode==MP_FRAGMENT_TINT?mask_image:base_image);d.w=d.h=8;d.format=!pass&&mode==MP_FRAGMENT_TINT?fmt:6;
            d.color_mask=15;d.alpha=7|(7<<13);d.blend=1|(1<<16);d.src=1;d.texture_rgb=d.texture_alpha=1;
            d.indices=(u32)ix;d.index_count=6;d.scissor[2]=640;d.scissor[3]=480;d.screen_width=400;
            MPTextureLayer layer={(u32)mask_image,8,8,fmt},wire_layer;layer.mode=mode;layer.blend=weight;
            float low[4];
            for(unsigned j=0;j<4;++j){
                float bg=base[j]*(255-weight)/255.f,lo=j==3?0:tint[j];
                low[j]=(mode==MP_FRAGMENT_TINT?bg+lo*weight/255.f:lo)/255.f;
                float high=mode==MP_FRAGMENT_TINT?bg+highlight[j]*weight/255.f:highlight[j];
                layer.tint|=(unsigned)(high+.5f)<<(8*j);layer.base|=(unsigned)(bg+.5f)<<(8*j);
            }
            float uv[4][2]={{1,0},{0,0},{0,1},{1,1}};u32 wire_uv[8];
            for(unsigned i=0;i<8;++i)wire_uv[i]=__builtin_bswap32(((u32*)uv)[i]);layer.uv=(u32)wire_uv;
            for(unsigned i=0;i<sizeof(layer)/4;++i)((u32*)&wire_layer)[i]=__builtin_bswap32(((u32*)&layer)[i]);
            if(!pass)d.layer=(u32)&wire_layer;
            Draw second={0};memcpy(&second,&layer,7*4);
            Texture*t=texture(&second);C3D_TexSetFilter(&t->tex,GPU_NEAREST,GPU_NEAREST);
            texture_pin=&second;texture_pin_bytes=t->tex.size;t=texture(&d);C3D_TexSetFilter(&t->tex,GPU_NEAREST,GPU_NEAREST);texture_pin=NULL;texture_pin_bytes=0;
            Vertex v[4]={0},wire_v[4];float xy[4][2]={{-1,1},{1,1},{1,-1},{-1,-1}};
            for(unsigned i=0;i<4;++i){v[i].p[0]=xy[i][0];v[i].p[1]=xy[i][1];v[i].p[2]=-.5f;v[i].p[3]=1;v[i].n[3]=-1;
                for(unsigned j=0;j<4;++j)v[i].c[j]=pass?1:low[j];
                v[i].t[0]=!pass&&mode==MP_FRAGMENT_TINT?uv[i][0]:1-uv[i][0];v[i].t[1]=uv[i][1];}
            Draw wire;for(unsigned i=0;i<sizeof(d)/4;++i)((u32*)&wire)[i]=__builtin_bswap32(((u32*)&d)[i]);
            for(unsigned i=0;i<sizeof(v)/4;++i)((u32*)wire_v)[i]=__builtin_bswap32(((u32*)v)[i]);
            mp_native_submit(wire_v,4,&wire);flush_dynamic();
            C3D_SyncDisplayTransfer(test->frameBuf.colorBuf,GX_BUFFER_DIM(240,400),(u32*)readback,GX_BUFFER_DIM(240,400),0);
            bool used=saved->used;saved->used=false;test->used=false;C3D_FrameEnd(0);C3D_FrameBegin(0);saved->used=used;
            GSPGPU_InvalidateDataCache(readback,240*400*4);for(unsigned i=0;i<240*400*4;++i)copy[i]=((volatile u8*)readback)[i];
            char path[96];snprintf(path,sizeof(path),"sdmc:/3ds/melee/shield-%u-%u-%u.bin",mode,sample,pass);FILE*f=fopen(path,"wb");
            if(!f)mp_native_panic("Shield verification output failed");fwrite(copy,1,240*400*4,f);fclose(f);
        }
    }
    texture_budget=saved_budget;texture_slot_budget=saved_slots;
    bool used=saved->used;saved->used=false;C3D_FrameEnd(0);C3D_RenderTargetDelete(test);linearFree(readback);free(copy);
    target=saved;C3D_FrameBegin(0);target->used=used;C3D_FrameDrawOn(target);bind_program(0);screen_viewport(320);
    raster_state_invalidate();for(unsigned i=0;i<6;++i)C3D_TexEnvInit(C3D_GetTexEnv(i));shield_verify=0;
}
