#ifndef MP_GX_SHADE_H
#define MP_GX_SHADE_H
#ifndef MP_SHADE_DEFERRED_CLAMPS
#define MP_SHADE_DEFERRED_CLAMPS 1
#endif
/* Compile the CPU material approximation into affine raster-color functions.
 * Expressions involving variable products or intermediate clipping fall back
 * to the full evaluator. No sampling-based approximation is used here. */
typedef struct {float c,x,a,y,b;int valid;unsigned clamped;} MPShadeValue;
typedef struct {MPShadeValue out[4];int valid,second;unsigned clamp[4];} MPShadePlan;
static MPShadeValue mp_shade_constant(float c){return(MPShadeValue){c,0,0,0,0,1};}
static int mp_shade_is_constant(MPShadeValue v){return v.valid&&!v.clamped&&v.x==0&&v.a==0&&v.y==0&&v.b==0;}
static MPShadeValue mp_shade_add(MPShadeValue a,MPShadeValue b,float sign){
    if(sign==0||(mp_shade_is_constant(b)&&b.c==0))return a;
    if(sign==1&&mp_shade_is_constant(a)&&a.c==0)return b;
    if(a.clamped||b.clamped)return(MPShadeValue){0};
    return(MPShadeValue){a.c+sign*b.c,a.x+sign*b.x,a.a+sign*b.a,a.y+sign*b.y,a.b+sign*b.b,a.valid&&b.valid};
}
static MPShadeValue mp_shade_scale(MPShadeValue v,float k){
    if(k==0)return mp_shade_constant(0);if(k==1)return v;
    if(v.clamped)return(MPShadeValue){0};
    v.c*=k;v.x*=k;v.a*=k;v.y*=k;v.b*=k;return v;
}
static MPShadeValue mp_shade_product(MPShadeValue a,MPShadeValue b){
    if(mp_shade_is_constant(a))return mp_shade_scale(b,a.c);
    if(mp_shade_is_constant(b))return mp_shade_scale(a,b.c);
    return(MPShadeValue){0};
}
static MPShadeValue mp_shade_op(MPShadeValue a,MPShadeValue b,MPShadeValue c,MPShadeValue d,unsigned op,unsigned bias,unsigned scale,unsigned clamp){
    MPShadeValue v;
    if(op>=8){
        if(!mp_shade_is_constant(a)||!mp_shade_is_constant(b))return(MPShadeValue){0};
        v=mp_shade_add(d,c,((op&1)?a.c==b.c:a.c>b.c)?1:0);
    }else{
        v=mp_shade_add(a,mp_shade_product(mp_shade_add(b,a,-1),c),1);
        v=mp_shade_add(d,v,op==1?-1:1);
        if(v.clamped&&(bias==1||bias==2))v.valid=0;
        v.c+=bias==1?.5f:bias==2?-.5f:0;
    }
    static const float scales[]={1,2,4,.5f};v=mp_shade_scale(v,scales[scale&3]);
    if(clamp&&v.valid&&!v.clamped){
        float low=v.c+(v.x<0?v.x:0)+(v.a<0?v.a:0)+(v.y<0?v.y:0)+(v.b<0?v.b:0);
        float high=v.c+(v.x>0?v.x:0)+(v.a>0?v.a:0)+(v.y>0?v.y:0)+(v.b>0?v.b:0);
        if(high<=0)v=mp_shade_constant(0);else if(low>=1)v=mp_shade_constant(1);
        /* A clamped intermediate often passes unchanged through a final
         * texture stage. Preserve the exact clamp through identity algebra,
         * then emit it once in the GPU shader. Non-identity combinations of
         * clamped expressions still fall back to the original evaluator. */
        else if(low<0||high>1){if(MP_SHADE_DEFERRED_CLAMPS)v.clamped=1;else v.valid=0;}
    }
    return v;
}
static float mp_shade_konst(const float k[4][4],unsigned sel,unsigned component){
    if(sel<8)return(8-sel)/8.f;
    if(sel>=12&&sel<16)return k[sel-12][component];
    if(sel>=16&&sel<32)return k[sel&3][(sel-16)/4];
    return 0;
}
static MPShadeValue mp_shade_raster(unsigned channel,unsigned component,const float alpha[2]){
    if(channel>=6)return mp_shade_constant(0);
    unsigned second=channel&1;if(channel==2||channel==3)component=3;
    if(component==3&&alpha[second]>=0)return mp_shade_constant(alpha[second]);
    return(MPShadeValue){0,!second&&component!=3,!second&&component==3,second&&component!=3,second&&component==3,1};
}
static void mp_raster_select(float out[4],const float ras[2][4],unsigned channel){
    for(unsigned j=0;j<4;++j)out[j]=channel>=6?0:ras[channel&1][channel==2||channel==3?3:j];
}
static MPShadeValue mp_shade_color_arg(unsigned arg,unsigned component,MPShadeValue reg[4][4],const float k[4][4],unsigned sel,unsigned channel,const float alpha[2]){
    if(arg<8)return reg[arg/2][(arg&1)?3:component];
    if(arg==10||arg==11)return mp_shade_raster(channel,arg==11?3:component,alpha);
    return mp_shade_constant(arg==8||arg==9||arg==12?1:arg==13?.5f:arg==14?mp_shade_konst(k,sel,component):0);
}
static MPShadeValue mp_shade_alpha_arg(unsigned arg,MPShadeValue reg[4][4],const float k[4][4],unsigned sel,unsigned channel,const float alpha[2]){
    if(arg<4)return reg[arg][3];
    if(arg==5)return mp_shade_raster(channel,3,alpha);
    return mp_shade_constant(arg==4?1:arg==6?mp_shade_konst(k,sel,3):0);
}
static void mp_shade_compile_channels(MPShadePlan*plan,const unsigned config[16][30],unsigned stages,const float colors[4][4],const float konst[4][4],const float alpha[2]){
    MPShadeValue reg[4][4];for(unsigned j=0;j<4;++j)plan->clamp[j]=0;
    for(unsigned i=0;i<4;++i)for(unsigned j=0;j<4;++j)reg[i][j]=mp_shade_constant(colors[i][j]);
    for(unsigned i=0;i<stages&&i<16;++i){const unsigned*s=config[i];MPShadeValue value[4];
        unsigned color_clamp=s[14],alpha_clamp=s[19];
        if(i==stages-1){if((s[15]&3)==0){for(unsigned j=0;j<3;++j)plan->clamp[j]=color_clamp;color_clamp=0;}
            if((s[20]&3)==0){plan->clamp[3]=alpha_clamp;alpha_clamp=0;}}
        for(unsigned j=0;j<3;++j)value[j]=mp_shade_op(mp_shade_color_arg(s[3],j,reg,konst,s[23],s[2],alpha),mp_shade_color_arg(s[4],j,reg,konst,s[23],s[2],alpha),mp_shade_color_arg(s[5],j,reg,konst,s[23],s[2],alpha),mp_shade_color_arg(s[6],j,reg,konst,s[23],s[2],alpha),s[11],s[12],s[13],color_clamp);
        value[3]=mp_shade_op(mp_shade_alpha_arg(s[7],reg,konst,s[24],s[2],alpha),mp_shade_alpha_arg(s[8],reg,konst,s[24],s[2],alpha),mp_shade_alpha_arg(s[9],reg,konst,s[24],s[2],alpha),mp_shade_alpha_arg(s[10],reg,konst,s[24],s[2],alpha),s[16],s[17],s[18],alpha_clamp);
        for(unsigned j=0;j<3;++j)reg[s[15]&3][j]=value[j];reg[s[20]&3][3]=value[3];
    }
    plan->valid=1;plan->second=0;
    for(unsigned j=0;j<4;++j){plan->out[j]=stages?reg[0][j]:mp_shade_raster(4,j,alpha);plan->clamp[j]|=plan->out[j].clamped;if(!plan->out[j].valid)plan->valid=0;if(plan->out[j].y!=0||plan->out[j].b!=0)plan->second=1;}
}
static void mp_shade_compile_alpha(MPShadePlan*plan,const unsigned config[16][30],unsigned stages,const float colors[4][4],const float konst[4][4],float alpha){
    float alphas[2]={alpha,-1};mp_shade_compile_channels(plan,config,stages,colors,konst,alphas);
}
static void mp_shade_compile(MPShadePlan*plan,const unsigned config[16][30],unsigned stages,const float colors[4][4],const float konst[4][4]){
    mp_shade_compile_alpha(plan,config,stages,colors,konst,-1);
}
static void mp_shade_apply_channels(const MPShadePlan*plan,float out[4],const float ras[2][4]){
    for(unsigned j=0;j<4;++j){MPShadeValue v=plan->out[j];float x=v.c+v.x*ras[0][j]+v.a*ras[0][3]+v.y*ras[1][j]+v.b*ras[1][3];
        if(plan->clamp[j]){if(x<0)x=0;if(x>1)x=1;}out[j]=x;}
}
static void mp_shade_apply(const MPShadePlan*plan,float out[4],const float ras[4]){
    float both[2][4];for(unsigned j=0;j<4;++j){both[0][j]=ras[j];both[1][j]=0;}mp_shade_apply_channels(plan,out,both);
}
#endif
