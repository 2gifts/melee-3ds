/* Derive the stage select cursor's GX program with the decomp's own TEV
 * compiler (sysdolphin texp.c/texpdag.c), without running the game.
 *
 * The expression is built as MObjMakeTExp and TObjMakeTExp build it for the
 * cursor's material in MnSlMap.usd/.dat (MnSelectStageDataTable +0x90):
 *   mobj rendermode 0x60000031 (CONSTANT, TEX0, TEX1; no VERTEX, DIFFUSE,
 *   SPECULAR), diffuse 99b3b3ff, alpha 0.999;
 *   tobj 0 (ring, I8 32x32): flags 0x00340010, colormap MODULATE, alphamap
 *   MODULATE, lightmap DIFFUSE, no TObjTev;
 *   tobj 1 (label, IA4 48x32, texture-animated): flags 0x00110010, colormap
 *   ALPHA_MASK, alphamap ALPHA_MASK, lightmap DIFFUSE; its TObjTev has
 *   active=0, so MakeColorGenTExp is not used.
 * The compiled stages are recorded the way port/engine/gx.c's GXSetTev*
 * setters store them (tev_configuration[stage][field]) and printed as JSON.
 * Built by tools/derive_cursor_material.py. */
#include <sysdolphin/baselib/texp.h>
#include <sysdolphin/baselib/tobj.h>
#include <sysdolphin/baselib/mobj.h>
#include <sysdolphin/baselib/tev.h>

int printf(const char*,...);
void* calloc(unsigned long,unsigned long);
void free(void*);
void exit(int);
double atof(const char*);

void* hsdAllocMemPiece(s32 size){return calloc(1,(unsigned long)size);}
void hsdFreeMemPiece(void* mem,s32 size){(void)size;free(mem);}
void __assert(char* file,u32 line,char* expr){printf("assert %s:%u %s\n",file,(unsigned)line,expr);exit(1);}
void HSD_Panic(char* file,u32 line,char* msg){printf("panic %s:%u %s\n",file,(unsigned)line,msg);exit(1);}
int HSD_Index2TevStage(int i){return i;}
int HSD_StateAssignTev(void){return 0;}
void HSD_StateInvalidate(int state){(void)state;}
void GXPixModeSync(void){}

static unsigned tev_configuration[16][30],stages;
static unsigned char tev_color[4][4],konst_color[4][4];
void GXSetTevColor(GXTevRegID reg,GXColor c){unsigned char*o=tev_color[reg&3];o[0]=c.r;o[1]=c.g;o[2]=c.b;o[3]=c.a;}
void GXSetTevKColor(GXTevKColorID id,GXColor c){unsigned char*o=konst_color[id&3];o[0]=c.r;o[1]=c.g;o[2]=c.b;o[3]=c.a;}
/* tev.c's HSD_SetupTevStage, recording into gx.c's field layout. */
void HSD_SetupTevStage(HSD_TevDesc* d){
    unsigned*v=tev_configuration[d->stage&15];HSD_TevConf*t=&d->u.tevconf;
    v[0]=d->coord;v[1]=d->map;v[2]=d->color;
    v[3]=t->clr_a;v[4]=t->clr_b;v[5]=t->clr_c;v[6]=t->clr_d;
    v[7]=t->alpha_a;v[8]=t->alpha_b;v[9]=t->alpha_c;v[10]=t->alpha_d;
    v[11]=t->clr_op;v[12]=t->clr_bias;v[13]=t->clr_scale;v[14]=t->clr_clamp;v[15]=t->clr_out_reg;
    v[16]=t->alpha_op;v[17]=t->alpha_bias;v[18]=t->alpha_scale;v[19]=t->alpha_clamp;v[20]=t->alpha_out_reg;
    v[21]=t->ras_swap;v[22]=t->tex_swap;v[23]=t->kcsel;v[24]=t->kasel;
    if((d->stage&15)+1>stages)stages=(d->stage&15)+1;
}

int main(void){
    static HSD_TObj ring,label;static HSD_Material mat;
    HSD_TExp *list=NULL,*diff,*alpha,*e;HSD_TExpTevDesc*desc;
    ring.id=GX_TEXMAP0;ring.coord=GX_TEXCOORD0;label.id=GX_TEXMAP1;label.coord=GX_TEXCOORD1;
    mat.diffuse.r=0x99;mat.diffuse.g=0xb3;mat.diffuse.b=0xb3;mat.diffuse.a=0xff;mat.alpha=(float)atof("0.999");

    /* MObjMakeTExp without RENDER_VERTEX: the material constant. */
    HSD_TExp*dc=HSD_TExpCnst(&mat.diffuse,HSD_TE_RGB,HSD_TE_U8,&list);
    HSD_TExp*ac=HSD_TExpCnst(&mat.alpha,HSD_TE_X,HSD_TE_F32,&list);
    e=HSD_TExpTev(&list);
    HSD_TExpColorOp(e,GX_TEV_ADD,GX_TB_ZERO,GX_CS_SCALE_1,GX_ENABLE);
    HSD_TExpColorIn(e,HSD_TE_0,HSD_TEXP_ZERO,HSD_TE_0,HSD_TEXP_ZERO,HSD_TE_0,HSD_TEXP_ZERO,HSD_TE_RGB,dc);
    HSD_TExpAlphaOp(e,GX_TEV_ADD,GX_TB_ZERO,GX_CS_SCALE_1,GX_ENABLE);
    HSD_TExpAlphaIn(e,HSD_TE_0,HSD_TEXP_ZERO,HSD_TE_0,HSD_TEXP_ZERO,HSD_TE_0,HSD_TEXP_ZERO,HSD_TE_X,ac);
    diff=alpha=e;
    /* TObjMakeTExp, ring: TEX_COLORMAP_MODULATE and TEX_ALPHAMAP_MODULATE. */
    e=HSD_TExpTev(&list);HSD_TExpOrder(e,&ring,GX_COLOR_NULL);
    HSD_TExpColorOp(e,GX_TEV_ADD,GX_TB_ZERO,GX_CS_SCALE_1,GX_ENABLE);
    HSD_TExpColorIn(e,HSD_TE_0,HSD_TEXP_ZERO,HSD_TE_RGB,diff,HSD_TE_RGB,HSD_TEXP_TEX,HSD_TE_0,HSD_TEXP_ZERO);
    HSD_TExpAlphaOp(e,GX_TEV_ADD,GX_TB_ZERO,GX_CS_SCALE_1,GX_ENABLE);
    HSD_TExpAlphaIn(e,HSD_TE_0,HSD_TEXP_ZERO,HSD_TE_A,alpha,HSD_TE_A,HSD_TEXP_TEX,HSD_TE_0,HSD_TEXP_ZERO);
    diff=alpha=e;
    /* TObjMakeTExp, label: TEX_COLORMAP_ALPHA_MASK and TEX_ALPHAMAP_ALPHA_MASK. */
    e=HSD_TExpTev(&list);HSD_TExpOrder(e,&label,GX_COLOR_NULL);
    HSD_TExpColorOp(e,GX_TEV_ADD,GX_TB_ZERO,GX_CS_SCALE_1,GX_ENABLE);
    HSD_TExpColorIn(e,HSD_TE_RGB,diff,HSD_TE_RGB,HSD_TEXP_TEX,HSD_TE_A,HSD_TEXP_TEX,HSD_TE_0,HSD_TEXP_ZERO);
    HSD_TExpAlphaOp(e,GX_TEV_ADD,GX_TB_ZERO,GX_CS_SCALE_1,GX_ENABLE);
    HSD_TExpAlphaIn(e,HSD_TE_A,alpha,HSD_TE_A,HSD_TEXP_TEX,HSD_TE_A,HSD_TEXP_TEX,HSD_TE_0,HSD_TEXP_ZERO);

    /* HSD_MObjCompileTev, then HSD_TExpSetupTev as HSD_MObjSetup issues it. */
    HSD_TExpCompile(e,&desc,&list);
    HSD_TExpSetupTev(desc,list);
    /* HSD_TExpSetReg leaves the components no stage reads uninitialised; keep
     * only those the program reads, so the output is deterministic. */
    unsigned char read[4][4]={{0}};
    for(unsigned i=0;i<stages;++i){unsigned*v=tev_configuration[i];
        for(unsigned j=3;j<7;++j)if(v[j]<8){if(v[j]&1)read[v[j]/2][3]=1;else read[v[j]/2][0]=read[v[j]/2][1]=read[v[j]/2][2]=1;}
        for(unsigned j=7;j<11;++j)if(v[j]<4)read[v[j]][3]=1;}
    for(unsigned i=0;i<16;++i)if(!read[i/4][i%4])tev_color[i/4][i%4]=0;

    printf("{\n  \"source\": \"derived: tests/cursor_texp.c through the decomp's HSD_TExpCompile\",\n");
    printf("  \"stages\": %u,\n  \"tev_configuration\": [",stages);
    for(unsigned i=0;i<16*30;++i)printf("%s%u",i?",":"",tev_configuration[i/30][i%30]);
    printf("],\n  \"tev_color\": \"");for(unsigned i=0;i<16;++i)printf("%02x",tev_color[i/4][i%4]);
    printf("\",\n  \"konst_color\": \"");for(unsigned i=0;i<16;++i)printf("%02x",konst_color[i/4][i%4]);
    printf("\"\n}\n");
    return 0;
}
