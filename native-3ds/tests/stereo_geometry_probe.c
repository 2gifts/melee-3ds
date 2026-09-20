/* Isolated development experiment. No engine, gameplay assets or CIA changes.
 * Reference uses the current Melee vertex shader twice. Candidate shades once
 * then emits both eye triangles. Tests include each eye's clipping boundary;
 * the default naive shader and --clip polygon clipper are separate builds. */
#include <3ds.h>
#include <citro3d.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifdef MP_STEREO_FIXED_PARAMETERS
#include "stereo_fixed.h"
#endif
#ifdef MP_EARLY_QUEUE_PROBE
#include "early_queue.h"
#endif
#ifdef MP_STEREO_BOUNDED
#include "stereo_bounds.h"
#ifndef MP_STEREO_STRESS
extern int mp_stereo_bounds_reference(const MPStereoBounds*,const float[30][4],const float[4][4],float,float,float);
typedef int (*BoundsFunction)(const MPStereoBounds*,const float[30][4],const float[4][4],float,float,float);
static BoundsFunction volatile bounds_functions[2]={mp_stereo_bounds_reference,mp_stereo_bounds_inside};
static u64 bounds_ticks[2];
static unsigned bounds_calls[2];
static volatile unsigned bounds_checksum;
#endif
#ifdef MP_STEREO_STRESS
#define FIXTURES 128
#define SCENES 64
#else
#define FIXTURES 24
#define SCENES 12
#endif
#else
#define FIXTURES 10
#define SCENES 10
#endif

extern const unsigned char reference[],candidate[];
extern const unsigned reference_size,candidate_size;
#ifdef MP_VIEWPORT_CONTROL
extern const unsigned char viewport_control[];
extern const unsigned viewport_control_size;
#define MODES 3
#else
#define MODES 2
#endif
#ifdef MP_STEREO_STRESS
static unsigned random_state;
static float random_unit(void){random_state=random_state*1664525u+1013904223u;return (random_state>>8)*(1.f/16777216.f);}
#endif
typedef struct {float p[4],color[4],uv[2],normal[4];} Vertex;
static float u[95][4];
static void require(int ok,const char*msg){if(!ok){FILE*f=fopen("sdmc:/stereo-probe/error.txt","w");if(f){fputs(msg,f);fclose(f);}exit(1);}}
static void uniforms(void){
    memset(u,0,sizeof(u));
    for(unsigned row=0;row<30;row+=3)for(unsigned k=0;k<3;++k){u[row+k][k]=1;u[30+row+k][k]=1;}
    for(unsigned k=0;k<4;++k){u[60+k][k]=1;u[85][k]=1;u[93][k]=.4f;u[94][k]=1;}
    u[62][2]=.5f;u[63][2]=-1;u[63][3]=0; /* z=-distance, clip W=distance */
    u[91][0]=1;u[91][2]=1;u[91][3]=2;
    for(unsigned i=0;i<4;++i){u[64+i][2]=1;u[72+i][0]=.16f;u[72+i][1]=.22f;u[72+i][2]=.12f;u[72+i][3]=1;}
    for(unsigned k=0;k<3;++k)u[89][k]=.1f;
    for(unsigned i=0;i<95;++i)C3D_FVUnifSet(GPU_VERTEX_SHADER,i,u[i][0],u[i][1],u[i][2],u[i][3]);
    /* 4 active, channel 0, no attenuation, channel 0 lit. */
    unsigned bits=(15u<<2)|(1u<<14);
    for(unsigned bit=0;bit<16;++bit)C3D_BoolUnifSet(GPU_VERTEX_SHADER,0x68+bit,!!(bits&(1u<<bit)));
    C3D_FixedAttribSet(4,-1,-1,-1,-1);
    C3D_FixedAttribSet(5,0,0,0,0);
}
int main(void){
    gfxInitDefault();romfsInit();mkdir("sdmc:/stereo-probe",0777);
    require(C3D_Init(0x80000),"C3D init");gfxSet3D(true);
    C3D_RenderTarget*t=C3D_RenderTargetCreate(240,800,GPU_RB_RGBA8,(C3D_DEPTHTYPE){.__i=GPU_RB_DEPTH24_STENCIL8});
    require(t!=NULL,"render target");
    DVLB_s*dv[MODES]={DVLB_ParseFile((u32*)reference,reference_size),DVLB_ParseFile((u32*)candidate,candidate_size)
#ifdef MP_VIEWPORT_CONTROL
        ,DVLB_ParseFile((u32*)viewport_control,viewport_control_size)
#endif
    };
    shaderProgram_s programs[MODES];
    for(unsigned i=0;i<MODES;++i){require(dv[i]!=NULL,"DVLB");shaderProgramInit(&programs[i]);shaderProgramSetVsh(&programs[i],&dv[i]->DVLE[0]);}
#ifdef MP_STEREO_SHARED_VSH
    require(dv[1]->DVLE[0].dvlp->codeSize<512,"combined shader must fit all vertex units");
    require(!shaderProgramSetVsh(&programs[0],&dv[1]->DVLE[0]),"shared reference vertex shader");
#endif
    require(!shaderProgramSetGsh(&programs[1],&dv[1]->DVLE[
#ifdef MP_STEREO_SHARED_VSH
        2
#else
        1
#endif
    ],9),"geometry shader");
    Vertex*v=linearAlloc(2*6*32*sizeof(Vertex));u8*readback=linearAlloc(240*800*4);
    require(v&&readback,"vertex/readback allocation");
    C3D_Tex texture;require(C3D_TexInit(&texture,8,8,GPU_RGBA8),"test texture");
    for(unsigned y=0;y<8;++y)for(unsigned x=0;x<8;++x){
        unsigned morton=(x&1)|((y&1)<<1)|((x&2)<<1)|((y&2)<<2)|((x&4)<<2)|((y&4)<<3);
        ((u32*)texture.data)[morton]=((x*31u+16u)<<24)|((y*31u+16u)<<16)|(((x^y)*29u+31u)<<8)|255;
    }
    C3D_TexFlush(&texture);C3D_TexSetFilter(&texture,GPU_NEAREST,GPU_NEAREST);C3D_TexSetWrap(&texture,GPU_REPEAT,GPU_REPEAT);
    unsigned completed=0,routes[FIXTURES][2]={{0}},command_bytes[FIXTURES][MODES]={{0}};
    for(unsigned page=0;page<FIXTURES;++page){
        unsigned fixture=page%SCENES,width=page<SCENES?400:320,origin=400-width;
#ifdef MP_STEREO_STRESS
        random_state=0x13c060u+fixture*701u;
#endif
        /* Fixture 0: binary-exact interior. 1: general interior at varied W.
         * 2/3: triangles crossing each eye's horizontal edge. */
        unsigned count=0;
        for(unsigned q=0;q<32;++q){
            float w=fixture==0?1.f:1.f+q*.375f;
            float x=-.875f+(q%4)*.4375f,y=-.8125f+(q/4)*.21875f;
            if(fixture==2)y=q&1?.98f:-.98f;
            if(fixture==3)y=q&1?1.16f:-1.16f;
            const unsigned order[6]={0,1,2,0,2,3};
            float xy[4][2]={{x,y},{x+.27f,y},{x+.27f,y+.15625f},{x,y+.15625f}};
            if(fixture>=4&&fixture<10){
                float cx=-.9f+(q%4)*.51f,cy=-1.18f+(q/4)*.325f;
                for(unsigned k=0;k<4;++k){xy[k][0]+=cx-x;xy[k][1]+=cy-y;}
                if(fixture==6){xy[0][1]=xy[1][1]=-2.3f;xy[2][1]=xy[3][1]=2.1f;}
                if(fixture==7)for(unsigned k=0;k<4;++k)xy[k][1]=(k<2?-1.f:1.f)+(q&1?1.f:-1.f)*.0001f;
            }
            for(unsigned k=0;k<6;++k){unsigned corner=order[k];Vertex*p=&v[count++];memset(p,0,sizeof(*p));
                float distance=fixture>=4&&fixture<10?w*(.625f+.375f*corner):w;
                if(fixture==8&&corner==0)distance=-distance;
                p->p[0]=xy[corner][0]*distance;p->p[1]=xy[corner][1]*distance;p->p[2]=-distance;p->p[3]=1;
                if(fixture==9)p->p[0]*=2.1f; /* vertical hardware clip planes */
                p->normal[2]=1;p->color[0]=.7f;p->color[1]=.8f;p->color[2]=.9f;p->color[3]=1;
                if(fixture==11)p->normal[3]=corner&1?27:3;
                if(fixture>=4){p->color[0]=.2f+.2f*corner;p->color[1]=.8f-.17f*corner;p->color[2]=.25f+.11f*corner;}
                p->uv[0]=corner==1||corner==2;p->uv[1]=corner>=2;}
#ifdef MP_STEREO_STRESS
            /* Retain exactly shared corners within each quad. Non-binary
             * depth, normals, color and UV exercise interpolation precision. */
            Vertex corners[4];float cx=-.84f+(q%4)*.42f,cy=-.79f+(q/4)*.198f;
            for(unsigned k=0;k<4;++k){Vertex*p=corners+k;memset(p,0,sizeof(*p));
                float distance=.4f+random_unit()*20.f;
#ifdef MP_STEREO_COHERENT_DEPTH
                distance=(.4f+q*.627f)*(1.f+distance*.005f);
#endif
                p->p[0]=(cx+(k==1||k==2?.28f:0.f)+random_unit()*.045f)*distance;
                p->p[1]=(cy+(k>=2?.14f:0.f)+random_unit()*.03f)*distance;
                p->p[2]=-distance;p->p[3]=1;
                for(unsigned j=0;j<3;++j){p->normal[j]=random_unit()*.5f;p->color[j]=.15f+random_unit()*.8f;}
                p->normal[2]+=.5f;p->color[3]=1;
                p->uv[0]=random_unit()*4.f;p->uv[1]=random_unit()*4.f;
            }
            for(unsigned k=0;k<6;++k)v[count-6+k]=corners[order[k]];
#endif
        }
        GSPGPU_FlushDataCache(v,count*sizeof(Vertex));
#if defined(MP_STEREO_BOUNDED) && !defined(MP_STEREO_STRESS)
        /* CPU-only A/B/ B/A samples with identical input. Indirect volatile
         * calls prevent invariant-hoisting from replacing repeated work. */
        uniforms();
        MPStereoBounds*benchmark=malloc(32*sizeof(*benchmark));require(benchmark!=NULL,"bounds benchmark allocation");
        for(unsigned q=0;q<32;++q)mp_stereo_bounds_build(benchmark+q,(const MPGPUVertex*)(v+q*6),6);
        const unsigned modes[]={0,1,1,0};
        for(unsigned sample=0;sample<4;++sample){
            unsigned variant=modes[sample];u64 start=svcGetSystemTick();
            for(unsigned repeat=0;repeat<20;++repeat)for(unsigned q=0;q<32;++q)
                bounds_checksum+=bounds_functions[variant](benchmark+q,u,u+60,fixture==0?.125f:.03f,fixture==0?-.0625f:-.0525f,width==320?.75f:1.f);
            bounds_ticks[variant]+=svcGetSystemTick()-start;bounds_calls[variant]+=20*32;
        }
        free(benchmark);
#endif
        for(unsigned mode=0;mode<MODES;++mode){
            C3D_FrameBegin(C3D_FRAME_SYNCDRAW);C3D_FrameDrawOn(t);C3D_RenderTargetClear(t,C3D_CLEAR_ALL,0x000000ff,0);
            C3D_BindProgram(&programs[
#ifdef MP_EARLY_QUEUE_PROBE
                0
#else
                mode
#endif
            ]);
            C3D_AttrInfo*a=C3D_GetAttrInfo();AttrInfo_Init(a);
            AttrInfo_AddLoader(a,0,GPU_FLOAT,4);AttrInfo_AddLoader(a,1,GPU_FLOAT,4);AttrInfo_AddLoader(a,2,GPU_FLOAT,2);AttrInfo_AddLoader(a,3,GPU_FLOAT,4);
            AttrInfo_AddFixed(a,4);AttrInfo_AddFixed(a,5);AttrInfo_AddFixed(a,6);
            C3D_BufInfo*b=C3D_GetBufInfo();BufInfo_Init(b);BufInfo_Add(b,v,sizeof(Vertex),4,0x3210);
            C3D_CullFace(GPU_CULL_NONE);C3D_DepthTest(false,GPU_ALWAYS,GPU_WRITE_COLOR);
            C3D_AlphaTest(false,GPU_ALWAYS,0);C3D_AlphaBlend(GPU_BLEND_ADD,GPU_BLEND_ADD,GPU_ONE,GPU_ZERO,GPU_ONE,GPU_ZERO);
            C3D_TexEnv*env=C3D_GetTexEnv(0);C3D_TexEnvInit(env);C3D_TexEnvSrc(env,C3D_Both,GPU_PRIMARY_COLOR,0,0);C3D_TexEnvFunc(env,C3D_Both,GPU_REPLACE);
            C3D_TexBind(0,fixture>=5?&texture:NULL);
            if(fixture>=5){C3D_TexEnvSrc(env,C3D_Both,GPU_PRIMARY_COLOR,GPU_TEXTURE0,0);C3D_TexEnvFunc(env,C3D_Both,GPU_MODULATE);}
            uniforms();float scale=fixture==0?.125f:.03f,bias=fixture==0?-.0625f:-.0525f;
            if(mode
#ifdef MP_EARLY_QUEUE_PROBE
                &&0
#endif
            ){
#ifdef MP_STEREO_BOUNDED
                for(unsigned first=0;first<count;first+=6){
                    MPStereoBounds bounds;mp_stereo_bounds_build(&bounds,(const MPGPUVertex*)(v+first),6);
                    unsigned inside=mp_stereo_bounds_inside(&bounds,u,u+60,scale,bias,width==320?.75f:1.f);
                    if(mode==1)++routes[page][inside];C3D_BindProgram(&programs[inside?mode:0]);
                    if(inside){
#ifdef MP_STEREO_SHARED_VSH
                        C3D_FixedAttribSet(6,0,0,0,0);
#endif
                        C3D_SetViewport(0,origin,240,2*width);C3D_SetScissor(GPU_SCISSOR_NORMAL,0,origin,240,origin+2*width);
#ifdef MP_VIEWPORT_CONTROL
                        if(mode==2){
                            for(unsigned eye=0;eye<2;++eye){
                                C3D_FixedAttribSet(6,eye?-scale:scale,eye?-bias:bias,eye?-200.f/width:200.f/width,.5f);
                                C3D_DrawArrays(GPU_TRIANGLES,first,6);
                            }
                            continue;
                        }
#endif
                        C3D_FVUnifSet(GPU_GEOMETRY_SHADER,0,
#ifdef MP_STEREO_FIXED_PARAMETERS
                            mp_stereo_fixed_float(scale),mp_stereo_fixed_float(bias),.5f,mp_stereo_fixed_float(200.f/width)
#else
                            scale,bias,.5f,200.f/width
#endif
                        );
                        C3D_DrawArrays(GPU_GEOMETRY_PRIM,first,6);
                    }else for(unsigned eye=0;eye<2;++eye){
                        unsigned y=(eye?0:400)+(400-width)/2;
                        C3D_SetViewport(0,y,240,width);C3D_SetScissor(GPU_SCISSOR_NORMAL,0,y,240,y+width);
                        C3D_FixedAttribSet(6,eye?-scale:scale,eye?-bias:bias,0,0);C3D_DrawArrays(GPU_TRIANGLES,first,6);
                    }
                }
#else
                C3D_SetViewport(0,origin,240,2*width);C3D_SetScissor(GPU_SCISSOR_NORMAL,0,origin,240,origin+2*width);
                C3D_FVUnifSet(GPU_GEOMETRY_SHADER,0,scale,bias,.5f,200.f/width);
                C3D_DrawArrays(GPU_GEOMETRY_PRIM,0,count);
                routes[page][1]=count/6;
#endif
            }else for(unsigned eye=0;eye<2;++eye){
                unsigned first=0;
#ifdef MP_EARLY_QUEUE_PROBE
                if(eye){
                    /* Append new streaming data after the first eye starts.
                     * Never alter the first range while PICA can read it. */
                    memcpy(v+count,v,count*sizeof(Vertex));
                    for(unsigned i=0;i<count;++i)v[count+i].color[0]*=.5f;
                    GSPGPU_FlushDataCache(v+count,count*sizeof(Vertex));first=count;
                }
#endif
                unsigned y=(eye?0:400)+(400-width)/2;
                C3D_SetViewport(0,y,240,width);C3D_SetScissor(GPU_SCISSOR_NORMAL,0,y,240,y+width);
                C3D_FixedAttribSet(6,eye?-scale:scale,eye?-bias:bias,0,0);C3D_DrawArrays(GPU_TRIANGLES,first,count);
#ifdef MP_EARLY_QUEUE_PROBE
                if(mode&&!eye)require(mp_early_queue_submit(),"early prefix did not start");
#endif
            }
#ifdef MP_EARLY_QUEUE_PROBE
            /* Exercise both a completed prefix with an unsent tail and a
             * transfer appended while the prefix queue is still active. */
            if(!(page&1))mp_early_queue_finish();
#endif
            C3D_SyncDisplayTransfer(t->frameBuf.colorBuf,GX_BUFFER_DIM(240,800),(u32*)readback,GX_BUFFER_DIM(240,800),0);
#ifdef MP_EARLY_QUEUE_PROBE
            mp_early_queue_finish();
#endif
            C3D_FrameEnd(0);command_bytes[page][mode]=(unsigned)(C3D_GetCmdBufUsage()*0x80000);
            C3D_FrameBegin(C3D_FRAME_SYNCDRAW);C3D_FrameEnd(0);
            GSPGPU_InvalidateDataCache(readback,240*800*4);
            char path[96];snprintf(path,sizeof(path),"sdmc:/stereo-probe/fixture-%u-%u.bin",page,mode);
            FILE*f=fopen(path,"wb");require(f!=NULL,"output open");require(fwrite(readback,1,240*800*4,f)==240*800*4,"output write");fclose(f);++completed;
        }
    }
    FILE*f=fopen("sdmc:/stereo-probe/report.json","w");require(f!=NULL,"report open");fprintf(f,"{\"completed\":%u,\"scenes\":%u,\"experimental\":true,\"routes\":[",completed,SCENES);
    for(unsigned p=0;p<FIXTURES;++p)fprintf(f,"%s[%u,%u]",p?",":"",routes[p][0],routes[p][1]);fputs("]",f);
    fputs(",\"command_bytes\":[",f);
    for(unsigned p=0;p<FIXTURES;++p){fprintf(f,"%s[",p?",":"");for(unsigned mode=0;mode<MODES;++mode)fprintf(f,"%s%u",mode?",":"",command_bytes[p][mode]);fputs("]",f);}fputs("]",f);
#if defined(MP_STEREO_BOUNDED) && !defined(MP_STEREO_STRESS)
    fprintf(f,",\"bounds_cpu_ticks\":[%llu,%llu],\"bounds_calls\":[%u,%u],\"bounds_checksum\":%u",
            (unsigned long long)bounds_ticks[0],(unsigned long long)bounds_ticks[1],bounds_calls[0],bounds_calls[1],bounds_checksum);
#endif
#ifdef MP_EARLY_QUEUE_PROBE
    require(mp_early_queue_starts==FIXTURES&&mp_early_queue_finishes==FIXTURES,"early prefix completion mismatch");
    fprintf(f,",\"early_queue_starts\":%u,\"early_queue_finishes\":%u",mp_early_queue_starts,mp_early_queue_finishes);
#endif
    fputs("}\n",f);fclose(f);
    C3D_RenderTargetDelete(t);C3D_TexDelete(&texture);C3D_Fini();linearFree(v);linearFree(readback);
    for(unsigned i=0;i<MODES;++i){shaderProgramFree(&programs[i]);DVLB_Free(dv[i]);}
    romfsExit();gfxExit();return 0;
}
