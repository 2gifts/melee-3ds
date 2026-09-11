"""Native equivalents of the selected offline Slippi/UCF 0.84 hooks."""
from build import ROOT

def adapt(source):
    name=source.name
    supported={'fighter.c','ftCo_Turn.c','ftCo_Damage.c','ftCo_Guard.c',
               'ftCo_DamageFall.c','ftCo_Pass.c','ftCo_Escape.c','ftCo_SquatRv.c','gmmain_lib.c','gm_1AED.c'}
    if name not in supported:return source
    text=source.read_text(encoding='utf-8')
    def replace(old,new,count=1):
        nonlocal text
        assert text.count(old)==count,(name,old,text.count(old))
        text=text.replace(old,new)
    if name=='gm_1AED.c':
        replace('        if (temp_r29 == 0 || temp_r29 == 2) {',
                '        if (temp_r29 == 13 || temp_r29 == 15) {\n            gm_80480DA8.unk8.unk0 = 0;\n            gm_80480DA8.unk14 = 0x14;\n            lb_8001CBAC(4); /* Slippi: continue with no card, saving disabled. */\n        } else if (temp_r29 == 0 || temp_r29 == 2) {')
    elif name=='gmmain_lib.c':
        for function in ('gmMainLib_8015FA34','gmMainLib_8015FBA4'):
            begin=text.index('void '+function+'(');end=text.index('\n}',begin)
            part=text[begin:end]
            assert part.count('    lbAudioAx_80028690();')==1
            part=part.replace('    lbAudioAx_80028690();','    mp_offline_defaults();\n    lbAudioAx_80028690();')
            text=text[:begin]+part+text[end:]
        text='extern void mp_offline_defaults(void);\n'+text
    else:
        text='#include "ucf.h"\n'+text
        if name=='fighter.c':
            replace('            fp->x678++;','            mp_ucf_input(fp);\n            fp->x678++;')
        elif name=='ftCo_Turn.c':
            replace('        fp->facing_dir = -fp->facing_dir;\n    }\n\n    RETURN_IF(ftCo_SpecialS_CheckInput(gobj));',
                    '        fp->facing_dir = -fp->facing_dir;\n        mp_ucf_dashback(fp);\n    }\n\n    RETURN_IF(ftCo_SpecialS_CheckInput(gobj));')
        elif name=='ftCo_Damage.c':
            replace('fp->x671_timer_lstick_tilt_y < p_ftCommonData->sdi_stick_window))',
                    'fp->x671_timer_lstick_tilt_y < p_ftCommonData->sdi_stick_window || mp_ucf_sdi(fp)))')
        elif name=='ftCo_Guard.c':
            replace('fp->x670_timer_lstick_tilt_x < p_ftCommonData->sdi_stick_window)',
                    '(fp->x670_timer_lstick_tilt_x < p_ftCommonData->sdi_stick_window || mp_ucf_shield_sdi(fp)))')
        elif name=='ftCo_DamageFall.c':
            replace('fp->x670_timer_lstick_tilt_x < p_ftCommonData->x214)',
                    '(fp->x670_timer_lstick_tilt_x < p_ftCommonData->x214 || mp_ucf_tumble(fp)))')
        elif name=='ftCo_Pass.c':
            replace('fp->input.held_buttons[0] & HSD_PAD_LR && ftCo_80099F1C(gobj)',
                    'fp->input.held_buttons[0] & HSD_PAD_LR && (ftCo_80099F1C(gobj) ||\n        (mp_ucf_drop_extended(fp) && fp->x671_timer_lstick_tilt_y < p_ftCommonData->x468 && mpColl_IsOnPlatform(&fp->coll_data)))')
        elif name=='ftCo_Escape.c':
            replace('        ftCo_80099894(gobj);','        if (mp_ucf_suppress_spotdodge(fp)) return false;\n        ftCo_80099894(gobj);',2)
        elif name=='ftCo_SquatRv.c':
            replace('fp->input.lstick[0].y > -p_ftCommonData->x94','fp->input.lstick[0].y > -mp_ucf_squat_threshold(fp)')
    out=ROOT/'build/overlays/offline'/name;out.parent.mkdir(parents=True,exist_ok=True)
    out.write_text(text,encoding='utf-8');return out
