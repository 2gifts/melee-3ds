/* Compile for ARM, never the 64-bit desktop host. These catch accidental ABI
 * drift before any binary archive can be mapped to engine structures. */
#include <Runtime/platform.h>
#include <dolphin/pad.h>
#include <melee/ft/types.h>
#include <melee/ef/types.h>
#include <melee/gm/gmresultplayer.static.h>
#include <sysdolphin/baselib/archive.h>
#include <sysdolphin/baselib/jobj.h>
#include <sysdolphin/baselib/texp.h>
_Static_assert(sizeof(void*) == 4, "32-bit pointers required");
_Static_assert(sizeof(bool) == 4, "GameCube bool required in engine units");
_Static_assert(sizeof(PADStatus) == 12, "PADStatus ABI");
_Static_assert(sizeof(HSD_Archive) == 0x44, "HSD_Archive ABI");
_Static_assert(sizeof(HSD_JObj) == 0x88, "HSD_JObj ABI");
_Static_assert(sizeof(Fighter) == 0x23ec, "Fighter ABI");
/* Native UCF hooks must address the same fields as their PPC references. */
_Static_assert(__builtin_offsetof(Fighter,x618_player_id) == 0x618, "UCF controller port ABI");
_Static_assert(__builtin_offsetof(Fighter,input.lstick[0]) == 0x620, "UCF current stick ABI");
_Static_assert(__builtin_offsetof(Fighter,input.lstick[1]) == 0x628, "UCF previous stick ABI");
_Static_assert(__builtin_offsetof(Fighter,x670_timer_lstick_tilt_x) == 0x670, "UCF stick timer ABI");
_Static_assert(__builtin_offsetof(Fighter,x673) == 0x673, "UCF true X timer ABI");
_Static_assert(__builtin_offsetof(Fighter,x674) == 0x674, "UCF true Y timer ABI");
_Static_assert(__builtin_offsetof(Fighter,cur_anim_frame) == 0x894, "UCF animation frame ABI");
_Static_assert(__builtin_offsetof(Fighter,cpu.x444) == 0x1ecc, "UCF Nana input buffer ABI");
_Static_assert(sizeof(HSD_TECnst) == 0x18, "Material constant ABI");
_Static_assert(sizeof(EF_Effect) == 0x2c, "Effect object ABI");
_Static_assert(sizeof(MatchEnd) == 0x227c, "Match results ABI");
_Static_assert(sizeof(ResultsData) == 0x5a8, "Results scene ABI");
_Static_assert(sizeof(CameraKindParams) == sizeof(CharScaleEntry), "Results camera table entry ABI");
_Static_assert(__builtin_offsetof(lbl_8046E3AC_t,dim_w1) == 0x22b4, "Results capture dimensions ABI");
