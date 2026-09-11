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
.macro ENGINEBRIDGE name,target
.section .text.\name,"ax",%progbits
.global \name
.type \name,%function
\name:
    push {r4,lr}
    setend be
    bl \target
    setend le
    pop {r4,pc}
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
    push {r4,lr}
    mov r4,sp
    ldr sp,=_stack_addr
    setend be
    bl mp_engine_main
    setend le
    mov sp,r4
    pop {r4,pc}
.size mp_game_boot,.-mp_game_boot
ENGINEBRIDGE mp_game_load,mp_scene_load
ENGINEBRIDGE mp_game_draw,mp_scene_draw
ENGINEBRIDGE mp_game_set_display,mp_display_set_mode
ENGINEBRIDGE mp_game_set_stereo,mp_display_set_stereo
ENGINEBRIDGE mp_game_stereo_scene,mp_display_stereo_scene
ENGINEBRIDGE mp_game_simulation,gm_801A4BA8
ENGINEBRIDGE mp_game_configure_cache,mp_gx_configure_cache
#ifdef MP_SMOKE_TEST
ENGINEBRIDGE mp_game_memory_check,mp_memory_compare_self_test
#endif
SDKBRIDGE mp_platform_submit,mp_native_submit
SDKBRIDGE mp_platform_alloc,mp_native_alloc
SDKBRIDGE mp_platform_free,mp_native_free
SDKBRIDGE mp_platform_log,mp_native_log
SDKBRIDGE mp_platform_panic,mp_native_panic
SDKBRIDGE mp_platform_ticks,mp_native_ticks
SDKBRIDGE mp_platform_file_id,mp_native_file_id
SDKBRIDGE mp_platform_file_size,mp_native_file_size
SDKBRIDGE mp_platform_file_read,mp_native_file_read
SDKBRIDGE mp_platform_file_read_async_begin,mp_native_file_read_async_begin
SDKBRIDGE mp_platform_file_read_async_poll,mp_native_file_read_async_poll
SDKBRIDGE mp_platform_pad,mp_native_pad
SDKBRIDGE mp_platform_frame,mp_native_frame
SDKBRIDGE mp_platform_idle,mp_native_idle
SDKBRIDGE mp_platform_audio,mp_native_audio
SDKBRIDGE mp_platform_texture_invalidate,mp_native_texture_invalidate
SDKBRIDGE mp_platform_texture_dirty,mp_native_texture_dirty
SDKBRIDGE mp_platform_frame_texture_visibility,mp_native_frame_texture_visibility
SDKBRIDGE mp_platform_efb_copy,mp_native_efb_copy
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
SDKBRIDGE mp_be_logf,logf
SDKBRIDGE mp_be_sinf,sinf
SDKBRIDGE mp_be_cosf,cosf
SDKBRIDGE mp_be_tanf,tanf
SDKBRIDGE mp_be_asinf,asinf
SDKBRIDGE mp_be_acosf,acosf
SDKBRIDGE mp_be_atanf,atanf
SDKBRIDGE mp_be_atan2f,atan2f
SDKBRIDGE mp_be_sqrtf,sqrtf

SDKBRIDGE mp_be_floorf,floorf
SDKBRIDGE mp_be_ceilf,ceilf
SDKBRIDGE mp_be_fabsf,fabsf
SDKBRIDGE mp_be_fmodf,fmodf
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
