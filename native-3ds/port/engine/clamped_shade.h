#ifndef MP_CLAMPED_SHADE_H
#define MP_CLAMPED_SHADE_H
/* A second material representation for affine work after one intermediate
 * clamp. Each component is linear + gain*clamp(inner). Distinct nonlinear
 * expressions and variable products stay on the original CPU evaluator.
 * This is algebra over the existing texture-as-white vertex-color model;
 * it does not change texture sampling, simulation or material definitions. */
typedef struct {float linear[5],inner[5],gain;unsigned valid,clipped;} MPClampedValue;
typedef struct {float coefficients[5][4],scale[4],bias[4];unsigned clamp[4],valid,second;} MPClampedPlan;
static MPClampedValue mp_clamped_constant(float x){return(MPClampedValue){.linear={x},.valid=1};}
static int mp_clamped_is_constant(MPClampedValue x){
    return x.valid&&!x.clipped&&x.linear[1]==0&&x.linear[2]==0&&x.linear[3]==0&&x.linear[4]==0;
}
static MPClampedValue mp_clamped_scale(MPClampedValue x,float k){
    if(k==0)return mp_clamped_constant(0);if(k==1)return x;
    for(unsigned i=0;i<5;++i)x.linear[i]*=k;x.gain*=k;return x;
}
static MPClampedValue mp_clamped_add(MPClampedValue a,MPClampedValue b,float sign){
    if(sign==0||(mp_clamped_is_constant(b)&&b.linear[0]==0))return a;
    if(sign==1&&mp_clamped_is_constant(a)&&a.linear[0]==0)return b;
    if(!a.valid||!b.valid)return(MPClampedValue){0};
    if(a.clipped&&b.clipped){
        for(unsigned i=0;i<5;++i)if(a.inner[i]!=b.inner[i])return(MPClampedValue){0};
    }else if(b.clipped){a.clipped=1;for(unsigned i=0;i<5;++i)a.inner[i]=b.inner[i];}
    for(unsigned i=0;i<5;++i)a.linear[i]+=sign*b.linear[i];a.gain+=sign*b.gain;
    if(a.gain==0)a.clipped=0;return a;
}
static MPClampedValue mp_clamped_product(MPClampedValue a,MPClampedValue b){
    if(mp_clamped_is_constant(a))return mp_clamped_scale(b,a.linear[0]);
    if(mp_clamped_is_constant(b))return mp_clamped_scale(a,b.linear[0]);
    return(MPClampedValue){0};
}
static MPClampedValue mp_clamped_clip(MPClampedValue v){
    if(!v.valid)return v;
    float lo=v.linear[0],hi=lo;
    for(unsigned i=1;i<5;++i){if(v.linear[i]<0)lo+=v.linear[i];else hi+=v.linear[i];}
    if(v.gain<0)lo+=v.gain;else hi+=v.gain;
    if(hi<=0)return mp_clamped_constant(0);if(lo>=1)return mp_clamped_constant(1);
    if(lo>=0&&hi<=1)return v;
    if(v.clipped)return(MPClampedValue){0};
    v.clipped=1;v.gain=1;
    for(unsigned i=0;i<5;++i){v.inner[i]=v.linear[i];v.linear[i]=0;}return v;
}
static MPClampedValue mp_clamped_op(MPClampedValue a,MPClampedValue b,MPClampedValue c,MPClampedValue d,
                                    unsigned op,unsigned bias,unsigned scale,unsigned clamp){
    MPClampedValue v;
    if(op>=8){
        if(!mp_clamped_is_constant(a)||!mp_clamped_is_constant(b))return(MPClampedValue){0};
        v=mp_clamped_add(d,c,((op&1)?a.linear[0]==b.linear[0]:a.linear[0]>b.linear[0])?1:0);
    }else{
        v=mp_clamped_add(a,mp_clamped_product(mp_clamped_add(b,a,-1),c),1);
        v=mp_clamped_add(d,v,op==1?-1:1);v.linear[0]+=bias==1?.5f:bias==2?-.5f:0;
    }
    static const float scales[]={1,2,4,.5f};v=mp_clamped_scale(v,scales[scale&3]);
    return clamp?mp_clamped_clip(v):v;
}
static MPClampedValue mp_clamped_raster(unsigned channel,unsigned component,const float alpha[2]){
    if(channel>=6)return mp_clamped_constant(0);
    unsigned second=channel&1;if(channel==2||channel==3)component=3;
    if(component==3&&alpha[second]>=0)return mp_clamped_constant(alpha[second]);
    MPClampedValue v=mp_clamped_constant(0);v.linear[1+2*second+(component==3)]=1;return v;
}
static MPClampedValue mp_clamped_color_arg(unsigned arg,unsigned component,MPClampedValue reg[4][4],
                                         const float k[4][4],unsigned sel,unsigned channel,const float alpha[2]){
    if(arg<8)return reg[arg/2][(arg&1)?3:component];
    if(arg==10||arg==11)return mp_clamped_raster(channel,arg==11?3:component,alpha);
    return mp_clamped_constant(arg==8||arg==9||arg==12?1:arg==13?.5f:arg==14?mp_shade_konst(k,sel,component):0);
}
static MPClampedValue mp_clamped_alpha_arg(unsigned arg,MPClampedValue reg[4][4],const float k[4][4],
                                         unsigned sel,unsigned channel,const float alpha[2]){
    if(arg<4)return reg[arg][3];if(arg==5)return mp_clamped_raster(channel,3,alpha);
    return mp_clamped_constant(arg==4?1:arg==6?mp_shade_konst(k,sel,3):0);
}
static void mp_clamped_compile(MPClampedPlan*plan,const unsigned config[16][30],unsigned stages,
                               const float colors[4][4],const float konst[4][4],const float alpha[2]){
    *plan=(MPClampedPlan){0};MPClampedValue reg[4][4];
    for(unsigned i=0;i<4;++i)for(unsigned j=0;j<4;++j)reg[i][j]=mp_clamped_constant(colors[i][j]);
    for(unsigned i=0;i<stages&&i<16;++i){const unsigned*s=config[i];MPClampedValue value[4];
        for(unsigned j=0;j<3;++j)value[j]=mp_clamped_op(
            mp_clamped_color_arg(s[3],j,reg,konst,s[23],s[2],alpha),mp_clamped_color_arg(s[4],j,reg,konst,s[23],s[2],alpha),
            mp_clamped_color_arg(s[5],j,reg,konst,s[23],s[2],alpha),mp_clamped_color_arg(s[6],j,reg,konst,s[23],s[2],alpha),
            s[11],s[12],s[13],s[14]);
        value[3]=mp_clamped_op(mp_clamped_alpha_arg(s[7],reg,konst,s[24],s[2],alpha),mp_clamped_alpha_arg(s[8],reg,konst,s[24],s[2],alpha),
            mp_clamped_alpha_arg(s[9],reg,konst,s[24],s[2],alpha),mp_clamped_alpha_arg(s[10],reg,konst,s[24],s[2],alpha),s[16],s[17],s[18],s[19]);
        for(unsigned j=0;j<3;++j)reg[s[15]&3][j]=value[j];reg[s[20]&3][3]=value[3];
    }
    for(unsigned j=0;j<4;++j){MPClampedValue v=stages?reg[0][j]:mp_clamped_raster(4,j,alpha);
        if(!v.valid)return;
        if(v.clipped){for(unsigned k=1;k<5;++k)if(v.linear[k]!=0)return;
            for(unsigned k=0;k<5;++k)plan->coefficients[k][j]=v.inner[k];
            plan->clamp[j]=1;plan->scale[j]=v.gain;plan->bias[j]=v.linear[0];
        }else{for(unsigned k=0;k<5;++k)plan->coefficients[k][j]=v.linear[k];plan->scale[j]=1;}
        if(plan->coefficients[3][j]!=0||plan->coefficients[4][j]!=0)plan->second=1;
    }
    plan->valid=1;
}
static void mp_clamped_apply(const MPClampedPlan*p,float out[4],const float raster[2][4]){
    for(unsigned j=0;j<4;++j){float x=p->coefficients[0][j]+p->coefficients[1][j]*raster[0][j]+p->coefficients[2][j]*raster[0][3]
        +p->coefficients[3][j]*raster[1][j]+p->coefficients[4][j]*raster[1][3];
        if(p->clamp[j]){if(x<0)x=0;if(x>1)x=1;}out[j]=p->scale[j]*x+p->bias[j];}
}
#endif
