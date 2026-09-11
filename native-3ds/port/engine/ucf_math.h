#ifndef MP_UCF_MATH_H
#define MP_UCF_MATH_H
/* UCF 0.84 algorithms by tauKhan, Altimor, PracticalTAS, CarVac and Krohnos.
 * Source/provenance: references/ucf at 5634468e; Slippi at fcf47f10.
 * These helpers use raw controller units and simulation-frame history. */
typedef struct { signed char x,y; } MPUcfStick;
typedef struct { MPUcfStick entries[4]; unsigned char index,drop_frames; } MPUcfPad;
static int mp_ucf_rim(float x,float y){
    if(x<0)x=-x;if(y<0)y=-y;
    /* Double intermediate reproduces PPC's single rounded fmsubs here. */
    int ix=(int)(float)((double)x*80.0-(double)0.0001f)+2;
    int iy=(int)(float)((double)y*80.0-(double)0.0001f)+2;
    return ix*ix+iy*iy>6400;
}
static void mp_ucf_cardinal(int x,int y,float*fx,float*fy){
    if((x<=-80||x>=80)&&y>=-6&&y<=6){*fx=x<0?-1.f:1.f;*fy=0;}
    else if((y<=-80||y>=80)&&x>=-6&&x<=6){*fx=0;*fy=y<0?-1.f:1.f;}
}
static int mp_ucf_delta2(const MPUcfPad*p,int axes){
    const MPUcfStick*a=&p->entries[p->index],*b=&p->entries[(p->index+2)&3];
    int x=a->x-b->x,y=a->y-b->y;
    return ((axes&1)?x*x:0)+((axes&2)?y*y:0);
}
static void mp_ucf_push(MPUcfPad*p,int x,int y){p->index=(p->index+1)&3;p->entries[p->index]=(MPUcfStick){x,y};}
static void mp_ucf_drop_history(MPUcfPad*p,float x,float y,unsigned hold){
    /* -.6125 translated through the Popo-to-Nana input quantization. */
    if(y<=-.609375f&&mp_ucf_rim(x,y)&&
       (p->drop_frames||(hold<2&&mp_ucf_delta2(p,2)>44*44)))++p->drop_frames;
    else p->drop_frames=0;
}
#endif
