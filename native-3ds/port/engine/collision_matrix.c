#include <sysdolphin/baselib/jobj.h>
unsigned mp_collision_matrix_repairs,mp_collision_matrix_failed;
unsigned mp_collision_parent_repairs;
void mp_collision_repair_matrix(HSD_JObj* joint){
    /* An observed DK pose had a nonfinite cached matrix despite finite SRT
     * and a finite parent. Rebuild through its actual class and dirty its
     * descendants, retaining the game's pose and hierarchy. Never invent a
     * collision position or use an unrelated previous-frame transform. */
    ++mp_collision_matrix_repairs;
    /* Kirby's captured pose had several invalid ancestors with valid SRT.
     * SetupMatrix only rebuilds dirty joints, so rebuilding a child alone
     * inherited the stale parent's NaNs. Rebuild the invalid chain from its
     * highest ancestor, preserving every valid parent and the class methods.
     * Bound the walk defensively; a malformed hierarchy must not overflow
     * the engine stack. The collision solver retains its finite-result guard. */
    HSD_JObj* pending[64];unsigned count=0;
    for(HSD_JObj*p=joint;p&&!__builtin_isfinite(p->mtx[0][0]);p=p->parent){
        if(count==64){++mp_collision_matrix_failed;return;}
        pending[count++]=p;
    }
    while(count){
        HSD_JObj*p=pending[--count];
        if(p!=joint)++mp_collision_parent_repairs;
        HSD_JObjSetMtxDirty(p);
        HSD_JObjSetupMatrix(p);
    }
    if(!__builtin_isfinite(joint->mtx[0][0]))++mp_collision_matrix_failed;
}
