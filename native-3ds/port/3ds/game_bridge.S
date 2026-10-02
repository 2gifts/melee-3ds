#include "../include/mp_fpscr.h"
.syntax unified
.arm
.text
.macro SDKBRIDGE name,target
.section .text.\name,"ax",%progbits
.global \name
.type \name,%function
\name:
    push {r4,lr}
    setend le
    bl \target
    setend be
    pop {r4,pc}
.size \name,.-\name
.endm
/* Native code entering the engine runs it under the engine's FPSCR
 * (port/include/mp_fpscr.h) and gets its own back afterwards. */
.macro ENGINEBRIDGE name,target
.section .text.\name,"ax",%progbits
.global \name
.type \name,%function
\name:
    push {r4,lr}
    vmrs r4,fpscr
    ldr r12,=MP_ENGINE_FPSCR
    vmsr fpscr,r12
    setend be
    bl \target
    setend le
    vmsr fpscr,r4
    pop {r4,pc}
.ltorg
.size \name,.-\name
.endm
.section .bss.mp_engine_stack,"aw",%nobits
.balign 32
.global _stack_end,_stack_addr
_stack_end: .space 1048576
_stack_addr:
.section .text.mp_game_boot,"ax",%progbits
.global mp_game_boot
.type mp_game_boot,%function
mp_game_boot:
    push {r4,r5,r6,lr}
    mov r4,sp
    vmrs r5,fpscr
    ldr r12,=MP_ENGINE_FPSCR
    vmsr fpscr,r12
    ldr sp,=_stack_addr
    setend be
    bl mp_engine_main
    setend le
    vmsr fpscr,r5
    mov sp,r4
    pop {r4,r5,r6,pc}
.size mp_game_boot,.-mp_game_boot
ENGINEBRIDGE mp_game_load,mp_scene_load
ENGINEBRIDGE mp_game_draw,mp_scene_draw
ENGINEBRIDGE mp_game_set_display,mp_display_set_mode
ENGINEBRIDGE mp_game_set_stereo,mp_display_set_stereo
ENGINEBRIDGE mp_game_stereo_scene,mp_display_stereo_scene
ENGINEBRIDGE mp_game_simulation,gm_801A4BA8
ENGINEBRIDGE mp_game_configure_cache,mp_gx_configure_cache
ENGINEBRIDGE mp_game_gx_enable,mp_gx_async_enable
ENGINEBRIDGE mp_game_gx_translate,mp_gx_translate
ENGINEBRIDGE mp_game_gx_stop,mp_gx_async_stop
ENGINEBRIDGE mp_game_gx_disable,mp_gx_async_disable
ENGINEBRIDGE mp_game_bottom_snapshot,mp_bottom_snapshot
ENGINEBRIDGE mp_game_card_flush,mp_card_flush
ENGINEBRIDGE mp_game_bottom_font,mp_bottom_font
#ifdef MP_SMOKE_TEST
ENGINEBRIDGE mp_game_memory_check,mp_memory_compare_self_test
ENGINEBRIDGE mp_game_rotation_check,mp_rotation_self_test
ENGINEBRIDGE mp_game_ppc_math_check,mp_ppc_math_self_test
ENGINEBRIDGE mp_game_fp_exact_check,mp_fp_exact_self_test
#endif
SDKBRIDGE mp_platform_submit,mp_native_submit
SDKBRIDGE mp_platform_alloc,mp_native_alloc
SDKBRIDGE mp_platform_free,mp_native_free
SDKBRIDGE mp_platform_geometry_retire,mp_native_geometry_retire
SDKBRIDGE mp_platform_log,mp_native_log
SDKBRIDGE mp_platform_panic,mp_native_panic
SDKBRIDGE mp_platform_ticks,mp_native_ticks
SDKBRIDGE mp_platform_file_id,mp_native_file_id
SDKBRIDGE mp_platform_file_size,mp_native_file_size
SDKBRIDGE mp_platform_file_read,mp_native_file_read
SDKBRIDGE mp_platform_file_read_async_begin,mp_native_file_read_async_begin
SDKBRIDGE mp_platform_file_read_async_poll,mp_native_file_read_async_poll
SDKBRIDGE mp_platform_pad,mp_native_pad
SDKBRIDGE mp_platform_pad_battle,mp_native_pad_battle
SDKBRIDGE mp_platform_frame_render,mp_native_frame_render
SDKBRIDGE mp_platform_frame_main,mp_native_frame_main
SDKBRIDGE mp_platform_frame_interval,mp_native_frame_interval
SDKBRIDGE mp_platform_vblank_count,mp_native_vblank_count
SDKBRIDGE mp_platform_render_stereo,mp_native_render_stereo
SDKBRIDGE mp_platform_gx_sleep,mp_native_gx_sleep
SDKBRIDGE mp_platform_gx_wake,mp_native_gx_wake
SDKBRIDGE mp_platform_gx_check,mp_native_gx_check
SDKBRIDGE mp_platform_idle,mp_native_idle
SDKBRIDGE mp_platform_audio,mp_native_audio
SDKBRIDGE mp_platform_texture_invalidate,mp_native_texture_invalidate
SDKBRIDGE mp_platform_texture_dirty,mp_native_texture_dirty
SDKBRIDGE mp_platform_frame_texture_visibility,mp_native_frame_texture_visibility
SDKBRIDGE mp_platform_efb_copy,mp_native_efb_copy
SDKBRIDGE mp_platform_jpeg_rgb565,mp_native_jpeg_rgb565
SDKBRIDGE mp_platform_card_list,mp_native_card_list
SDKBRIDGE mp_platform_card_size,mp_native_card_size
SDKBRIDGE mp_platform_card_read,mp_native_card_read
SDKBRIDGE mp_platform_card_write,mp_native_card_write
SDKBRIDGE mp_platform_card_remove,mp_native_card_remove
SDKBRIDGE mp_platform_card_time,mp_native_card_time
SDKBRIDGE mp_platform_card_sync,mp_native_card_sync
SDKBRIDGE mp_platform_tap_jump,mp_native_tap_jump
SDKBRIDGE mp_platform_profile,mp_native_profile
SDKBRIDGE mp_platform_slippi_file_load,mp_native_slippi_file_load
SDKBRIDGE mp_platform_slippi_file_size,mp_native_slippi_file_size
SDKBRIDGE mp_platform_slippi_file_write,mp_native_slippi_file_write
SDKBRIDGE mp_platform_slippi_unlimited,mp_native_slippi_unlimited
SDKBRIDGE mp_platform_slippi_session,mp_native_slippi_session
SDKBRIDGE mp_be_sin,sin
SDKBRIDGE mp_be_cos,cos
SDKBRIDGE mp_be_tan,tan
SDKBRIDGE mp_be_asin,asin
SDKBRIDGE mp_be_acos,acos
SDKBRIDGE mp_be_atan,atan
SDKBRIDGE mp_be_atan2,atan2
SDKBRIDGE mp_be_sqrt,sqrt
SDKBRIDGE mp_be_pow,pow
SDKBRIDGE mp_be_floor,floor
SDKBRIDGE mp_be_ceil,ceil
SDKBRIDGE mp_be_fabs,fabs
SDKBRIDGE mp_be_fmod,fmod
SDKBRIDGE mp_be_exp,exp
SDKBRIDGE mp_be_log,log
SDKBRIDGE mp_be_log10,log10
/* sinf cosf tanf asinf acosf atanf atan2f logf sqrtf fmodf: the console's
 * own implementations are compiled into the engine (docs/slippi/determinism.md). */

SDKBRIDGE mp_be_floorf,floorf
SDKBRIDGE mp_be_ceilf,ceilf
SDKBRIDGE mp_be_fabsf,fabsf
SDKBRIDGE mp_be___aeabi_idiv,__aeabi_idiv
SDKBRIDGE mp_be___aeabi_uidiv,__aeabi_uidiv
SDKBRIDGE mp_be___aeabi_idivmod,__aeabi_idivmod
SDKBRIDGE mp_be___aeabi_uidivmod,__aeabi_uidivmod
.macro SWAPPAIR a,b
    mov r12,\a
    mov \a,\b
    mov \b,r12
.endm
.macro INTEGER64_BRIDGE name,target,before,after
.section .text.\name,"ax",%progbits
.global \name
.type \name,%function
\name:
    push {r4,lr}
    .if \before > 0
    SWAPPAIR r0,r1
    .endif
    .if \before > 1
    SWAPPAIR r2,r3
    .endif
    setend le
    bl \target
    .if \after > 0
    SWAPPAIR r0,r1
    .endif
    .if \after > 1
    SWAPPAIR r2,r3
    .endif
    setend be
    pop {r4,pc}
.endm
INTEGER64_BRIDGE mp_be___aeabi_f2ulz,__aeabi_f2ulz,0,1
INTEGER64_BRIDGE mp_be___aeabi_f2lz,__aeabi_f2lz,0,1
INTEGER64_BRIDGE mp_be___aeabi_d2ulz,__aeabi_d2ulz,1,1
INTEGER64_BRIDGE mp_be___aeabi_d2lz,__aeabi_d2lz,1,1
INTEGER64_BRIDGE mp_be___aeabi_l2f,__aeabi_l2f,1,0
INTEGER64_BRIDGE mp_be___aeabi_ul2f,__aeabi_ul2f,1,0
INTEGER64_BRIDGE mp_be___aeabi_l2d,__aeabi_l2d,1,1
INTEGER64_BRIDGE mp_be___aeabi_ul2d,__aeabi_ul2d,1,1
INTEGER64_BRIDGE mp_be___aeabi_ldivmod,__aeabi_ldivmod,2,2
INTEGER64_BRIDGE mp_be___aeabi_uldivmod,__aeabi_uldivmod,2,2
.section .text.mp_be_context_swap,"ax",%progbits
.global mp_be_context_swap
.type mp_be_context_swap,%function
mp_be_context_swap:
    stmia r0!,{r4-r11,sp,lr}
    vstmia r0,{d8-d15}
    ldmia r1!,{r4-r11,sp,lr}
    vldmia r1,{d8-d15}
    bx lr
.section .text.mp_be_thread_trampoline,"ax",%progbits
.global mp_be_thread_trampoline
.type mp_be_thread_trampoline,%function
mp_be_thread_trampoline:
    mov r0,r4
    bl mp_engine_thread_start
    b .
/* Slippi online experiment: engine -> native network API
 * (port/include/slippi_net_bridge.h, port/3ds/slippi/slippi_bridge.c). */
SDKBRIDGE mp_platform_slippi_net_start,slippi_net_start
SDKBRIDGE mp_platform_slippi_net_status,slippi_net_status
SDKBRIDGE mp_platform_slippi_net_error,slippi_net_error
SDKBRIDGE mp_platform_slippi_net_poll,slippi_net_poll
SDKBRIDGE mp_platform_slippi_net_stop,slippi_net_stop
SDKBRIDGE mp_platform_slippi_net_local_index,slippi_net_local_index
SDKBRIDGE mp_platform_slippi_net_set_selections,slippi_net_set_selections
SDKBRIDGE mp_platform_slippi_net_remote_ready,slippi_net_remote_ready
SDKBRIDGE mp_platform_slippi_net_match_block,slippi_net_match_block
SDKBRIDGE mp_platform_slippi_net_new_game,slippi_net_new_game
SDKBRIDGE mp_platform_slippi_net_remote_checksum,slippi_net_remote_checksum
SDKBRIDGE mp_platform_slippi_net_remote_checksum_frame,slippi_net_remote_checksum_frame
SDKBRIDGE mp_platform_slippi_net_remote_checksum_value,slippi_net_remote_checksum_value
SDKBRIDGE mp_platform_slippi_net_ping_ms,slippi_net_ping_ms
/* Six arguments: the fifth and sixth arrive on the caller's stack as
 * big-endian words. Load them while still big-endian, then store them
 * little-endian where the native callee expects its stack arguments. */
.macro SDKBRIDGE6 name,target
.section .text.\name,"ax",%progbits
.global \name
.type \name,%function
\name:
    push {r4,r5,r6,lr}
    ldr r4,[sp,#16]
    ldr r5,[sp,#20]
    setend le
    sub sp,sp,#8
    str r4,[sp]
    str r5,[sp,#4]
    bl \target
    add sp,sp,#8
    setend be
    pop {r4,r5,r6,pc}
.size \name,.-\name
.endm
SDKBRIDGE6 mp_platform_slippi_net_send_inputs,slippi_net_send_inputs
