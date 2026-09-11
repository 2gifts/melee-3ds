#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef uint16_t u16;
typedef int32_t s32;
typedef struct HSD_Particle HSD_Particle;
typedef struct {void (*hookDelete)(HSD_Particle*);} Userfunc;
typedef struct {unsigned linkNo,idnum,numChild;Userfunc *userfunc;} HSD_Generator;
typedef struct {int value;} HSD_JObj;
struct HSD_Particle {HSD_Particle*next;HSD_Generator*gen;unsigned idnum,kind;void*appsrt;};
static HSD_JObj*hsd_804D08E8[8];
static HSD_Particle*hsd_804D0908[16];
static struct {unsigned alloc_data;}hsd_804D0F60;
static unsigned hsd_804D78E2,deleted,detached,unrefed,freed;
static unsigned mp_particle_cleanup_calls,mp_particle_cleanup_deleted;
static void deletion(HSD_Particle*p){assert(p->gen);++deleted;}
static void psRemoveParticleAppSRT(HSD_Particle*p){assert(p->appsrt);p->appsrt=NULL;++detached;}
static void HSD_JObjUnref(HSD_JObj*j){assert(j->value==0x1234);++unrefed;}
static void HSD_ObjFree(unsigned*a,HSD_Particle*p){assert(a==&hsd_804D0F60.alloc_data);assert(p);++freed;}
#include "particle_cleanup_function.inc"
int main(void){
    Userfunc callbacks={deletion};HSD_JObj joint={0x1234};
    for(unsigned link=0;link<16;++link){
        HSD_Generator target={link,0x1af,3,&callbacks},other={link,0x1af,1,&callbacks};
        HSD_Particle p[6];memset(p,0,sizeof(p));
        deleted=detached=unrefed=freed=0;hsd_804D78E2=6;
        for(unsigned i=0;i<6;++i){p[i].gen=&target;p[i].idnum=target.idnum;p[i].next=i<5?&p[i+1]:NULL;}
        p[1].gen=&other;p[3].idnum++;p[4].gen=NULL;
        p[0].appsrt=&joint;p[2].kind=0xf000;hsd_804D08E8[7]=&joint;
        hsd_804D0908[link]=p;
        hsd_8039D0A0(&target);
        assert(hsd_804D0908[link]==&p[1]&&p[1].next==&p[3]&&p[3].next==&p[4]&&!p[4].next);
        assert(target.numChild==0&&other.numChild==1&&hsd_804D78E2==3);
        assert(deleted==3&&freed==3&&detached==1&&unrefed==1&&!hsd_804D08E8[7]);
        hsd_8039D0A0(&target);assert(freed==3);
        hsd_804D0908[link]=NULL;hsd_8039D0A0(&target);assert(freed==3);
    }
    assert(mp_particle_cleanup_calls==48 && mp_particle_cleanup_deleted==48);
    puts("Particle cleanup: all 16 lists; head/middle/tail unlink; same-ID other generator; null generator; joint/SRT/callback/allocator accounting passed");
}
