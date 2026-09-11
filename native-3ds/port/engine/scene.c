#include <sysdolphin/baselib/archive.h>
#include <sysdolphin/baselib/id.h>
#include <sysdolphin/baselib/jobj.h>
#include <sysdolphin/baselib/list.h>
#include <sysdolphin/baselib/aobj.h>
#include <sysdolphin/baselib/fobj.h>
#include <sysdolphin/baselib/mtx.h>
#include <sysdolphin/baselib/robj.h>
#include <sysdolphin/baselib/debug.h>
#include <sysdolphin/baselib/cobj.h>
#include <sysdolphin/baselib/video.h>
#include <sysdolphin/baselib/displayfunc.h>
#include <sysdolphin/baselib/lobj.h>

static HSD_Archive fighter_archive;
HSD_JObj *mp_fighter_jobj;
static HSD_CObj *camera;
int mp_scene_load(void *bytes,unsigned size)
{
    HSD_IDSetup();HSD_IDInitAllocData();HSD_ListInitAllocData();
    HSD_AObjInitAllocData();HSD_FObjInitAllocData();HSD_VecInitAllocData();
    HSD_MtxInitAllocData();HSD_RObjInitAllocData();
    HSD_ZListInitAllocData();
    if(HSD_ArchiveParse(&fighter_archive,bytes,size))return -1;
    HSD_Joint *joint=HSD_ArchiveGetPublicAddress(&fighter_archive,"PlyFox5K_Share_joint");
    if(!joint)return -2;
    OSReport("Loading original Fox joint tree\n");
    mp_fighter_jobj=HSD_JObjLoadJoint(joint);
    OSReport("Fox joint tree loaded: %p\n",mp_fighter_jobj);
    HSD_VIData.current.vi.rmode.fbWidth=400;
    HSD_VIData.current.vi.rmode.efbHeight=240;
    HSD_VIData.current.vi.rmode.viWidth=400;
    HSD_VIData.current.vi.rmode.viHeight=240;
    HSD_VIData.current.vi.rmode.xfbHeight=240;
    camera=HSD_CObjAlloc();
    HSD_CObjSetViewportfx4(camera,0,400,0,240);
    HSD_CObjSetScissorx4(camera,0,400,0,240);
    HSD_CObjSetNear(camera,1);HSD_CObjSetFar(camera,1000);
    HSD_CObjSetPerspective(camera,35,400.f/240.f);
    Vec3 eye={0,10,65},target={0,8,0};
    HSD_CObjSetEyePosition(camera,&eye);HSD_CObjSetInterest(camera,&target);
    return mp_fighter_jobj?0:-3;
}
void mp_scene_draw(void)
{
    HSD_CObjSetCurrent(camera);
    HSD_LObjSetupInit(camera);
    HSD_JObjDispAll(mp_fighter_jobj,NULL,7,0);
    HSD_CObjEndCurrent();
}
