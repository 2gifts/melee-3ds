"""Explicit source adaptations, without changing the pinned upstream checkout."""
import re
from build import ROOT
def adapt(source):
    if source.name=='lbcollision.c':
        changed=source.read_text(encoding='utf-8')
        changed='unsigned mp_collision_invalid_count;\nfloat mp_collision_invalid_sample[32];\n'+changed
        start=changed.index('bool lbColl_80006E58(')
        end=changed.index('\nstatic inline float sqrDistance',start)
        block=changed[start:end]
        old='    if (allowed_distance < axis.x) {'
        assert block.count(old)==1
        block=block.replace(old,'''    /* An unordered comparison must not accept an invalid contact. Keep a
     * bounded record in the ordinary game log for hardware diagnosis. */
    if (!__builtin_isfinite(allowed_distance) || !__builtin_isfinite(axis.x) ||
        !__builtin_isfinite(out_contact_pos->x) ||
        !__builtin_isfinite(out_contact_pos->y) ||
        !__builtin_isfinite(out_contact_pos->z)) {
        if (!mp_collision_invalid_count) {
            float* record = mp_collision_invalid_sample;
            const Vec3* points[4] = {hit_start, hit_end, hurt_start, hurt_end};
            int i, j;
            for (i = 0; i < 4; ++i) {
                *record++ = points[i]->x; *record++ = points[i]->y; *record++ = points[i]->z;
            }
            *record++ = hit_radius; *record++ = hurt_radius; *record++ = broadphase_scale;
            for (i = 0; i < 3; ++i) for (j = 0; j < 4; ++j) *record++ = hurt_mtx[i][j];
            *record++ = axis.x; *record++ = local_dist_sq; *record++ = allowed_distance;
        }
        if (mp_collision_invalid_count++ < 4) {
            OSReport("Invalid capsule contact rejected: hit=(%.2f %.2f %.2f) hurt=(%.2f %.2f %.2f) radii=%.3f %.3f scale=%.3f\\n",
                hit_start->x, hit_start->y, hit_start->z,
                hurt_start->x, hurt_start->y, hurt_start->z,
                hit_radius, hurt_radius, broadphase_scale);
        }
        return false;
    }
'''+old)
        changed=changed[:start]+block+changed[end:]
        out=ROOT/'build/overlays'/source.name;out.parent.mkdir(parents=True,exist_ok=True)
        out.write_text(changed,encoding='utf-8');return out
    if source.name=='lb_00B0.c':
        changed=source.read_text(encoding='utf-8')
        start=changed.index('void lb_8000B1CC(')
        end=changed.index('\n}',start)+2
        block=changed[start:end]
        old='        HSD_JObjSetupMatrix(arg0);'
        assert block.count(old)==1
        block=block.replace(old,old+"\n        if (!__builtin_isfinite(arg0->mtx[0][0])) {\n            extern void mp_collision_repair_matrix(HSD_JObj*);\n            mp_collision_repair_matrix(arg0);\n        }")
        changed=changed[:start]+block+changed[end:]
        out=ROOT/'build/overlays'/source.name;out.parent.mkdir(parents=True,exist_ok=True)
        out.write_text(changed,encoding='utf-8');return out
    if source.name=='granime.c':
        changed=source.read_text(encoding='utf-8')
        old='    HSD_AObj* sp14 = NULL;'
        assert changed.count(old)==1
        # The callback writes this automatic object and longjmps back. C
        # requires volatile here; optimized ARM otherwise returns the saved
        # initial NULL and stage hazards never see their animation finish.
        changed=changed.replace(old,'    HSD_AObj* volatile sp14 = NULL;')
        changed=changed.replace('void fn_801C82E8(int arg0, int* arg1)',
                                'void fn_801C82E8(int arg0, volatile int* arg1)')
        out=ROOT/'build/overlays'/source.name;out.parent.mkdir(parents=True,exist_ok=True)
        out.write_text(changed,encoding='utf-8');return out
    if source.name=='mncharsel.c':
        changed=source.read_text(encoding='utf-8')
        # The portrait's branch animation may clear the explicit hidden bit
        # after a door closes. Enforce the closed-slot invariant after it runs.
        start=changed.index('void fn_8025F0E0(')
        end=changed.index('\nvoid fn_8025FAC0(',start)
        block=changed[start:end]
        old='    HSD_JObjAnimAll(jobj);'
        assert block.count(old)==1
        block=block.replace(old,old+'''
    if (mnCharSel_804D6CF5 != 1) {
        for (i = 0; i < (s32) mnCharSel_804D6CF5; ++i) {
            if (mnCharSel_803F0DFC.doors[i].p_kind == 3) {
                lb_80011E24(mnCharSel_804D6CC0, &sp54,
                    mnCharSel_803F0DFC.doors[i].costume_joint, -1);
                HSD_JObjSetFlags(sp54, JOBJ_HIDDEN);
                lb_80011E24(mnCharSel_804D6CC0, &sp54,
                    mnCharSel_803F0DFC.doors[i].emblem_joint, -1);
                HSD_JObjSetFlags(sp54, JOBJ_HIDDEN);
            }
        }
    }''')
        changed=changed[:start]+block+changed[end:]
        start=changed.index('    jobj = HSD_JObjLoadJoint(css_models->press_start.joint);')
        end=changed.index('    HSD_GObj_SetupProc(gobj, fn_80262F44, 3);',start)
        block=changed[start:end]
        old='    GObj_SetupGXLink(gobj, HSD_GObj_JObjCallback, 4, 0x80);'
        assert block.count(old)==1
        block=block.replace(old,'''    extern void mp_display_ribbon_callback(HSD_GObj*, int);
    GObj_SetupGXLink(gobj, mp_display_ribbon_callback, 4, 0x80);''')
        changed=changed[:start]+block+changed[end:]
        changed+='''
/* Read only persistent CSS door data; no animated portrait object escapes. */
unsigned mp_bottom_css(unsigned slot)
{
    CSSDoor* d;
    unsigned character, color, count = mnCharSel_804D6CF5;
    /* Training reports one human door, but also exposes the CPU door. */
    if (mnCharSel_804D6CB0 && mnCharSel_804D6CB0->match_type == TRAINING_MODE) count = 2;
    if (slot >= 4 || slot >= count) return 3;
    d = &mnCharSel_803F0DFC.doors[slot];
    character = d->sel_icon < 25 ? icons[d->sel_icon].char_kind : 255;
    color = slot;
    if (mnCharSel_804D6CB0 && mnCharSel_804D6CB0->match_type != TRAINING_MODE &&
        mnCharSel_804D6CB0->vs.start.rules.is_teams && d->team < 3)
        color = d->team == 2 ? 3 : d->team;
    return d->p_kind | (character << 8) | (d->costume << 16) | (color << 24);
}
'''
        out=ROOT/'build/overlays'/source.name;out.parent.mkdir(parents=True,exist_ok=True)
        out.write_text(changed,encoding='utf-8');return out
    if source.name in ('gricemt.c','itkyasarinegg.c'):
        changed=source.read_text(encoding='utf-8')
        if source.name=='gricemt.c':
            start=changed.index('float grIceMt_801F96E0(')
            end=changed.index('\n}',start)
            # Original PPC preserves input f1 while moving both segments.
            changed=changed[:end]+'\n    return y; /* Explicit PPC f1 result for the scrolling caller. */'+changed[end:]
        else:
            old='bool itKyasarinegg_UnkMotion4_Anim(Item_GObj* gobj)\n{\n    it_802751D8(gobj);\n}'
            assert changed.count(old)==1
            changed=changed.replace(old,old.replace('    it_802751D8(gobj);','    return it_802751D8(gobj);'))
        out=ROOT/'build/overlays'/source.name;out.parent.mkdir(parents=True,exist_ok=True)
        out.write_text(changed,encoding='utf-8');return out
    if source.name=='grvenom.c':
        from stage_overlays import adapt as stage_adapt
        changed=stage_adapt(source.name,source.read_text(encoding='utf-8'))
        out=ROOT/'build/overlays'/source.name;out.parent.mkdir(parents=True,exist_ok=True)
        out.write_text(changed,encoding='utf-8');return out
    from offline_overlays import adapt as offline_adapt
    offline_source=offline_adapt(source)
    if offline_source!=source:
        # ftCo_Damage already has a return-value adaptation below; retain it.
        if source.name!='ftCo_Damage.c':return offline_source
        source=offline_source
    if source.name=='ftdrawcommon.c':
        contents=source.read_text(encoding='utf-8')
        old='            ftParts_800750C8(fighter, 2, 0);\n            ftParts_800750C8(fighter, 0, 1);'
        assert contents.count(old)==1
        changed=contents.replace(old,'''            extern volatile unsigned mp_performance_low_poly;
            ftParts_800750C8(fighter, 2, 0);
            if (mp_performance_low_poly && fighter->x5AC.xC[1] != NULL) {
                ftParts_800750C8(fighter, 0, 0);
                ftParts_800750C8(fighter, 1, 1);
            } else {
                ftParts_800750C8(fighter, 0, 1);
            }''')
        out=ROOT/'build/overlays'/source.name;out.parent.mkdir(parents=True,exist_ok=True)
        out.write_text(changed,encoding='utf-8');return out
    if source.name=='grizumi.c':
        changed=source.read_text(encoding='utf-8')
        for name,flag in [('grIzumi_801CCB90','mp_performance_hide_stars'),('grIzumi_801CCEA0','mp_performance_skip_reflection')]:
            old='void '+name+'(HSD_GObj* gobj, int renderpass)\n{'
            assert changed.count(old)==1
            changed=changed.replace(old,old+'\n    extern volatile unsigned '+flag+';\n    if ('+flag+') return;')
        old='        lb_800121FC(refl->image, 80, 60, 4, 2001);'
        assert changed.count(old)==1
        changed=changed.replace(old,old+'''
        /* The performance path uses a dark, static water surface rather
         * than rendering the world a second time into an 80x60 mirror. */
        PSMTXIdentity(refl->texture_matrix);
        if (refl->image->image_ptr != NULL) {
            u8* pixels = refl->image->image_ptr;
            int n = GXGetTexBufferSize(80, 60, 4, 0, 0);
            int i;
            for (i = 0; i < n; i += 2) { pixels[i] = 0x10; pixels[i+1] = 0x68; }
        }''')
        out=ROOT/'build/overlays'/source.name;out.parent.mkdir(parents=True,exist_ok=True)
        out.write_text(changed,encoding='utf-8');return out
    if source.name=='lbshadow.c':
        changed=source.read_text(encoding='utf-8')
        for function,signature in [('lbShadow_8000F38C','s32 arg0'),('lbShadow_8000EEE0','HSD_GObj* gobj'),('lbShadow_8000EFEC','void')]:
            old='void '+function+'('+signature+')\n{'
            assert changed.count(old)==1
            guard='\n    extern volatile unsigned mp_performance_no_shadows;\n    if (mp_performance_no_shadows) return;'
            if function=='lbShadow_8000F38C':
                guard='''
    extern volatile unsigned mp_performance_no_shadows;
    if (mp_performance_no_shadows) {
        HSD_GObj* cur;
        for (cur=HSD_GObjPLinkHead[HSD_GOBJ_PLINK_FIGHTER];cur;cur=cur->next) {
            LbShadow* lbs=ftLib_800872B0(cur);
            if(lbs && lbs->shadow) HSD_ShadowSetActive(lbs->shadow,0);
        }
        return;
    }'''
            changed=changed.replace(old,old+guard)
        out=ROOT/'build/overlays'/source.name;out.parent.mkdir(parents=True,exist_ok=True)
        out.write_text(changed,encoding='utf-8');return out
    if source.name not in ('lbarq.c','camera.c','cobj.c','itcoin.c','mninfo.c','gmmain.c','devcom.c','lbmemory.c','lbfile.c','ftdata.c','gm_1A3F.c','lbcardnew.c','gmtitle.c','hsd_4D11.c','tydisplay.c','texp.c','psdisp.c','hsd_3915.c','gm_1832.c','synth.c','ftmaterial.c','grdisplay.c','gobjuserdata.c','eflib.c','shadow.c','lbrefract.c','itspawn.c','gm_1798.c','gm_1601.c','itfreeze.c','itlinkarrow.c','player.c','ftCo_Damage.c','particle.c','mnitemsw.c','mnname.c','mnnamenew.c','tobj.c','gm_1A45.c','lb_0192.c'):return source
    contents=source.read_text(encoding='utf-8')
    changed=contents.replace('void inline itCoin_ResetRotation','static inline void itCoin_ResetRotation').replace('inline void mnInfo_CreateEntries','static inline void mnInfo_CreateEntries')
    if source.name in ('gm_1A45.c','lb_0192.c'):
        old='GXInvalidateTexAll();'
        assert changed.count(old)==1,source.name
        changed='extern void mp_gx_frame_texture_visibility(void);\n'+changed.replace(old,'mp_gx_frame_texture_visibility();')
    if source.name=='gm_1A45.c':
        old='    gm_804D6720 = info;'
        assert changed.count(old)==1
        changed=changed.replace(old, old+'\n    extern void mp_bottom_scene_begin(unsigned);\n    mp_bottom_scene_begin(info ? info->scene_kind : 255);')
    if source.name=='lbarq.c':
        # Hardware DMA interrupts complete this empty busy wait on GameCube.
        # Our transfers are cooperative. Restoring interrupts drains only one
        # request, and the general poll can already be active in this caller.
        old='        while (lbArq_80014ABC(rp) != LB_ARQ_STATE_DONE) {\n        }'
        assert changed.count(old)==1
        changed=changed.replace(old,'        while (lbArq_80014ABC(rp) != LB_ARQ_STATE_DONE) {\n            extern void mp_ar_pump(void); mp_ar_pump();\n        }')
    if source.name=='camera.c':
        changed=changed.replace('    lbRefract_8002247C(cobj);',
            '    extern void mp_display_set_world(HSD_CObj*);\n    mp_display_set_world(cobj);\n    lbRefract_8002247C(cobj);')
    if source.name=='cobj.c':
        # Widen projection/erase extents and geometric visibility queries,
        # without modifying the descriptor or the camera tracking parameters.
        changed=changed.replace('#include "cobj.h"','#include "cobj.h"\nextern float mp_display_camera_aspect(HSD_CObj*);\nextern unsigned mp_display_camera_width(HSD_CObj*);\nextern void mp_gx_display_width(unsigned);')
        token='cobj->projection_param.perspective.aspect'
        # Perspective aspect is also the union's bottom field for other
        # projection types; the helper is identity for those cameras.
        changed=re.sub(re.escape(token)+r'(?!\s*=)', 'mp_display_camera_aspect(cobj)',changed)
        changed=changed.replace('    current = cobj;', '    current = cobj;\n    mp_gx_display_width(mp_display_camera_width(cobj));\n    extern float mp_display_camera_convergence(HSD_CObj*);\n    extern void mp_gx_camera_convergence(float);\n    mp_gx_camera_convergence(mp_display_camera_convergence(cobj));')
    if source.name=='lbrefract.c':
        changed=changed.replace('    switch (HSD_CObjGetProjectionType(cobj)) {',
            '    extern float mp_display_camera_aspect(HSD_CObj*);\n    switch (HSD_CObjGetProjectionType(cobj)) {')
        changed=changed.replace('cobj->projection_param.perspective.aspect, 0.5F,','mp_display_camera_aspect(cobj), 0.5F,')
    if source.name=='mnnamenew.c':
        # The keyboard's old overlay starts at the animation array and spans
        # separate key, glyph-variant and coordinate globals in the PPC DOL.
        start=changed.index('typedef struct MnNameNewDataLayout {')
        end=changed.index('} MnNameNewDataLayout;',start)+len('} MnNameNewDataLayout;')
        changed=changed[:start]+'''typedef struct MnNameNewDataLayout {
    AnimLoopSettings *anim;
    u16 *key_jobj_ids;
    char **x34, **xFC, **character_bytes;
    GlyphRow *lower_glyphs, *upper_glyphs;
    Vec3 *x8CC, *x8D8;
} MnNameNewDataLayout;'''+changed[end:]
        getter='''static MnNameNewDataLayout* mp_name_keyboard_layout(void)
{
    static MnNameNewDataLayout view = {
        mnNameNew_803EDA58, mnNameNew_KeyMap.key_jobj_ids,
        (char**)mnNameNew_KeyMap.x34, (char**)mnNameNew_KeyMap.xFC,
        (char**)mnNameNew_KeyMap.character_bytes,
        (GlyphRow*)mnNameNew_GlyphTable.lower_glyphs,
        (GlyphRow*)mnNameNew_GlyphTable.upper_glyphs,
        &unk_vec, &mnNameNew_803EE330
    };
    return &view;
}

'''
        changed=changed.replace('void mnNameNew_8023B0F8(',getter+'void mnNameNew_8023B0F8(',1)
        changed=changed.replace('(MnNameNewDataLayout*) mnNameNew_803EDA58','mp_name_keyboard_layout()')
        changed=changed.replace('&layout->x8CC','layout->x8CC').replace('&layout->x8D8','layout->x8D8')
    if source.name=='particle.c':
        # Retail 8039D0A0 uses 804D08E8+0x20 for list heads and +0x678
        # for the allocator. Those are separate symbols in the ARM image.
        # The hardware dump's first list read instead picked up 0x400ae148.
        start=changed.index('    typedef struct {',changed.index('void hsd_8039D0A0'))
        end=changed.index('    HSD_Particle* prev;',start)
        changed=changed[:start]+changed[end:]
        changed=changed.replace('data->particle[gen->linkNo]','hsd_804D0908[gen->linkNo]')
        changed=changed.replace('data->jobj[jidx]','hsd_804D08E8[jidx]')
        changed=changed.replace('&data->alloc_data','&hsd_804D0F60.alloc_data')
        changed=changed.replace('void hsd_8039D0A0(HSD_Generator* gen)\n{',
            'unsigned mp_particle_cleanup_calls, mp_particle_cleanup_deleted;\nvoid hsd_8039D0A0(HSD_Generator* gen)\n{')
        changed=changed.replace('    head = &hsd_804D0908[gen->linkNo];',
            '    ++mp_particle_cleanup_calls;\n    head = &hsd_804D0908[gen->linkNo];')
        changed=changed.replace('            HSD_ObjFree(&hsd_804D0F60.alloc_data, prt);',
            '            ++mp_particle_cleanup_deleted;\n            HSD_ObjFree(&hsd_804D0F60.alloc_data, prt);')
    if source.name=='mnitemsw.c':
        # Represent the overlay as references to the actual blocks. Preserve
        # all animation values and the item order without relying on layout.
        start=changed.index('struct MnItemSwTable {')
        end=changed.index('};',start)+2
        changed=changed[:start]+'''struct MnItemSwTable {
    f32 (*x00)[3];
    f32 *x30, *items;
    u8 *item_order;
};'''+changed[end:]
        changed=changed.replace('    return (struct MnItemSwTable*) mnItemSw_803ED340;',
            '    static struct MnItemSwTable view = {mnItemSw_803ED340, mnItemSw_AnimTable.x30, mnItemSw_AnimTable.items, mnItemSw_803ED438};\n    return &view;')
        # When leaving the frequency row, no previous item highlight exists
        # to sample. Start the newly selected item's highlight at frame zero.
        changed=changed.replace('        f32 anim_val;','        f32 anim_val = 0.0f;')
    if source.name=='mnname.c':
        # Six loop windows follow a four-entry table only in the PPC image.
        for i in (4,5,6,8):
            changed=changed.replace(f'base[{i}].start_frame',f'mnName_803B8510[{i-4}]->start_frame')
        for i in (6,8):
            changed=changed.replace(f'base[{i} + (index == target)].start_frame',f'mnName_803B8510[{i-4} + (index == target)]->start_frame')
        changed=changed.replace('== base + 5','== mnName_803B8510[1]').replace('&base[5]','mnName_803B8510[1]')
        for offset,value in [('0xC8','mnName_803ED600'),('0xCC','&mnName_803ED600[1]'),('0xEC','&mnName_803ED618[1].x'),('0xF0','&mnName_803ED618[1].y'),('0xF4','&mnName_803ED618[1].z')]:
            changed=changed.replace('(base + '+offset+')','('+value+')')
        for offset,label in [('0x4D0','mnNameAutoNameUs'),('0x4E4','mnNameRefuseNameUs'),('0x4F8','mnNameAutoName'),('0x508','mnNameRefuseName')]:
            changed=changed.replace('(char*) mnName_803ED538 + '+offset,'"'+label+'"')
    if source.name=='ftCo_Damage.c':
        # 8008da4c saves LR in r0. With zero percentTemp, 8008da68 jumps
        # directly to 8008dafc (mr r3,r0), returning that nonzero address.
        # Preserve its boolean truth value without an undefined C local.
        old='    bool result;\n    if (fp->dmg.x1838_percentTemp)'
        assert changed.count(old)==1, 'Damage effect return source changed'
        changed=changed.replace(old,'    bool result = true;\n    if (fp->dmg.x1838_percentTemp)')
    if source.name=='player.c':
        # The matching PPC layout puts ftMapping_list 32 bytes after a file
        # name. Neither ARM section order nor string padding preserves that.
        # Use the named table in all five consumers, including demo archives,
        # transformed fighter falls, and the Ice Climbers' secondary fighter.
        changed,n=re.subn(r'    struct Unk_Struct_w_Array\* \w+ =\n'
                           r'        \(struct Unk_Struct_w_Array\*\) &str_PdPmdat_start_of_data;\n',
                           '',changed)
        assert n==5, 'Player mapping source changed'
        members={'x':'internal_id','y':'extra_internal_id','z':'has_transformation'}
        changed,n=re.subn(r'\b(?:unkStruct|unk_struct|some_struct)->vec_arr\[([^\]]+)\]\.([xyz])',
                           lambda m:'ftMapping_list['+m[1]+'].'+members[m[2]],changed)
        assert n==10, 'Player mapping consumers changed'
    if source.name=='itfreeze.c':
        # The explicit no-parent position fallback requires a null item view.
        changed=changed.replace('    Item* ip;\n\n    if (ref_gobj != NULL)',
                                '    Item* ip = NULL;\n\n    if (ref_gobj != NULL)')
    if source.name=='itlinkarrow.c':
        changed=changed.replace('    HSD_JObj* jobj;\n    if (joint != NULL)',
                                '    HSD_JObj* jobj = NULL;\n    if (joint != NULL)')
    if source.name=='gm_1601.c':
        # The PPC routine retains its input in r3 on the ordinary-character
        # path (80168bc4: ble 80168bcc). The recovered C left `base` undefined.
        changed=changed.replace('f32 gm_80168B34(CharacterKind ckind, int arg1, int arg2)\n{\n    int base;',
                                'f32 gm_80168B34(CharacterKind ckind, int arg1, int arg2)\n{\n    int base = ckind;')
        # 80168c3c calls the frame lookup; its epilogue preserves the float
        # return in f1. Missing `return` lets ARM discard the lookup entirely.
        old='    gm_80168B34(ckind, Player_80036394(arg0), costume);'
        assert changed.count(old)==1
        changed=changed.replace(old,'    return gm_80168B34(ckind, Player_80036394(arg0), costume);')
        for offset,table in [('0x21','lbl_803B767C'),('0x42','lbl_803B7700'),('0x63','lbl_803B7784')]:
            changed=changed.replace('lbl_803B75F8[tmp_ckind + '+offset+']',table+'[tmp_ckind]')
    if source.name=='gm_1798.c':
        # ResultsDisplayLayout is an overlay across four separate GC globals.
        # Give every view one allocation; never rely on linker section order.
        start=changed.index('ResultsDisplayData lbl_8046E1B0;')
        end=changed.index('lbl_8046E3AC_t lbl_8046E3AC;',start)+len('lbl_8046E3AC_t lbl_8046E3AC;')
        storage='ResultsDisplayLayout mp_results_display __attribute__((aligned(8),used));\n'
        for name,field,offset in [('lbl_8046E1B0','pad_000',0),('lbl_8046E38C','gobjs',0x1dc),('lbl_8046E39C','jobjs',0x1ec),('lbl_8046E3AC','state',0x1fc)]:
            storage+='_Static_assert(__builtin_offsetof(ResultsDisplayLayout,'+field+') == '+str(offset)+', "Results display ABI");\n'
            storage+='__asm__(".global '+name+'\\n.set '+name+',mp_results_display+'+str(offset)+'\\n");\n'
        changed=changed[:start]+storage+changed[end:]
        # CameraKindData spans unrelated data globals in the matching binary.
        # Its actual camera and character tables already have named objects.
        changed=changed.replace('    CameraKindData* data = (CameraKindData*) gmResultPlayerColors;','')
        changed=changed.replace('&data->cobj_desc','(HSD_CObjDesc*) &gmResultCameraDesc')
        changed=changed.replace('data->kind','((CameraKindParams*) gmResultCharacterScaleData)')
        changed=changed.replace('data->slot_off','gmResultCharacterData.slot_off')
    if source.name=='itspawn.c':
        # Capsule/container drops use the table following the random spawner
        # in the GameCube image. ARM globals need an explicit table reference.
        changed=changed.replace('((ItemPickTable*) (spawner + 1))','(&it_804A0E50)')
    if source.name in ('shadow.c','lbrefract.c'):
        # These two consumers sample the capture as a texture in the same
        # frame; neither reads its pixel bytes on the CPU. Other GXCopyTex
        # callers retain the materialized GameCube-layout copy.
        changed='extern void mp_gx_copy_texture_only(void*, int);\n'+changed.replace('GXCopyTex(', 'mp_gx_copy_texture_only(')
    if source.name in ('tobj.c','shadow.c','lbrefract.c'):
        # These exact functions invalidate immediately after GXCopyTex. The
        # native copy bridge now updates the destination's image/palette
        # aliases. CPU cache-visibility calls separately track other writes;
        # explicit GXInvalidateTexAll calls elsewhere retain their full scope.
        function={'tobj.c':'HSD_ImageDescCopyFromEFB','shadow.c':'HSD_ShadowEndRender','lbrefract.c':'lbRefract_80022560'}[source.name]
        start=changed.index('void '+function+'(')
        end=changed.index('\n}',start)+2
        body=changed[start:end]
        assert body.count('GXInvalidateTexAll();')==1,(source.name,body)
        body=body.replace('GXInvalidateTexAll();','/* Destination texture visibility is handled by the native copy. */')
        changed=changed[:start]+body+changed[end:]
    if source.name=='shadow.c':
        old='void HSD_ShadowSetActive(HSD_Shadow* shadow, int active)\n{'
        assert changed.count(old)==1
        changed=changed.replace(old,old+'\n    extern volatile unsigned mp_performance_no_shadows;\n    if (mp_performance_no_shadows) active = 0;')
    if source.name=='eflib.c':
        # The matching GameCube linker placed the parameter table immediately
        # after the animation queue. On ARM, these writes corrupted the effect
        # allocator's free-list head with a packed gfx_id/alpha value.
        changed=changed.replace('efLib_AnimQueue + 0x10','efLib_ParamTable')
        changed=changed.replace('efLib_AnimQueue[idx + 0x10]','efLib_ParamTable[idx]')
    if source.name=='gobjuserdata.c':
        changed=changed.replace('    HSD_ASSERT(40, gobj->user_data_kind == HSD_GOBJ_USER_DATA_NONE);',
            '    if(gobj->user_data_kind != HSD_GOBJ_USER_DATA_NONE) OSReport("GObj userdata conflict object=%x old=%u new=%u olddata=%x newdata=%x caller=%x\\n",gobj,gobj->user_data_kind,kind,gobj->user_data,data,__builtin_return_address(0));\n    HSD_ASSERT(40, gobj->user_data_kind == HSD_GOBJ_USER_DATA_NONE);')
    if source.name=='grdisplay.c':
        changed=changed.replace('((intptr_t) gp->x18 & ~0x7FFFFFFF) == 0', '(uintptr_t) gp->x18 < 0x02000000')
        changed='extern volatile unsigned mp_performance_no_shadows;\n'+changed
        changed=changed.replace('if (gp->x10_flags.b5 != 0)', 'if (gp->x10_flags.b5 != 0 && !mp_performance_no_shadows)')
        changed=changed.replace('for (fighter = HSD_GObjPLinkHead[HSD_GOBJ_PLINK_FIGHTER];', 'for (fighter = mp_performance_no_shadows ? NULL : HSD_GObjPLinkHead[HSD_GOBJ_PLINK_FIGHTER];')
    if source.name=='ftmaterial.c':
        # ftMObj's matching-build cast extends beyond its declared object into
        # two following globals. Their placement is not an ARM ABI guarantee.
        changed=changed.replace('info->texp_tmpl','ftMaterial_803C6A44').replace('info->tevdesc_tmpl','ftMaterial_803C69D0')
    if source.name=='synth.c':
        # The matching binary reaches the next global by indexing past this
        # array. Name that global explicitly on the ARM linker layout.
        changed=changed.replace('HSD_Synth_804C2AE0[bank_id + 0x80 / 4] = (void*) offset;', 'hsd_SynthSFXBank[bank_id] = offset;')
        changed='extern void mp_engine_poll(void);\n'+changed
        old='    do {\n    } while (HSD_Synth_804D7778 != 0);'
        assert changed.count(old)==1
        changed='extern void mp_engine_io_poll(void);\n'+changed.replace(old,'''    while (HSD_Synth_804D7778 != 0) {
        /* DVD/ARAM completion releases the previous music-stream lock.
         * ARM has cooperative DMA; an empty PPC interrupt wait deadlocks. */
        mp_engine_io_poll();
    }''')
        changed=changed.replace('while (sfxGroupDataReaddressCounter) {\n        continue;', 'while (sfxGroupDataReaddressCounter) {\n        mp_engine_poll();')
        changed=changed.replace('while (HSD_Synth_804D772C >= 6) {', 'while (HSD_Synth_804D772C >= 6) {\n        mp_engine_poll();')
        changed=changed.replace('while (HSD_Synth_804D772C != 0) {\n        callback();', 'while (HSD_Synth_804D772C != 0) {\n        mp_engine_poll();\n        callback();')
    if source.name in ('psdisp.c','hsd_3915.c','gm_1832.c'):
        changed=re.sub(r'GXWGFifo\.(u8|u16|u32|f32)\s*=\s*([^;]+);',r'mp_gx_write_\1(\2);',changed)
    if source.name=='psdisp.c':
        changed=changed.replace('if (gp == NULL) {\n        *x = gp->pos.x;\n        *y = gp->pos.y;\n        *z = gp->pos.z;', 'if (gp == NULL) {\n        *x = pp->pos.x;\n        *y = pp->pos.y;\n        *z = pp->pos.z;')
    if source.name=='tydisplay.c':
        changed=re.sub(r'(?m)^inline (.*?) (_tyDisplay_803(?:18CB4|19994)_sort)',r'static inline \1 \2',changed)
    if source.name=='texp.c':
        changed=changed.replace('tdesc->desc.next = &(*tevdesc)->desc;', 'tdesc->desc.next = *tevdesc ? &(*tevdesc)->desc : NULL;')
        changed=changed.replace('clist = &texp->cnst;', 'clist = texp ? &texp->cnst : NULL;')
        changed=changed.replace('clist = &clist->next->cnst;', 'clist = clist->next ? &clist->next->cnst : NULL;')
        changed=changed.replace('        HSD_ASSERT(0x591, clist->type == HSD_TE_CNST);','        if(clist->type != HSD_TE_CNST) OSReport("Invalid material constant list head=%x node=%x type=%x next=%x\\n",texp,clist,clist->type,clist->next);\n        HSD_ASSERT(0x591, clist->type == HSD_TE_CNST);')
    if source.name=='hsd_4D11.c':
        # Original Metrowerks emits these BSS declarations in reverse order.
        # CardContext accesses all three as one contiguous allocation. ARM
        # section ordering must not split/reverse that shared work area.
        start=changed.index('/* 4D2348 */')
        end=changed.index('/* 4D799C */')
        storage='u8 mp_card_storage[0x1510] __attribute__((aligned(32),used));\n'
        for name,offset in [('hsd_804D1138',0),('hsd_804D1148',0x10),('hsd_804D2348',0x1210)]:
            storage+='__asm__(".global '+name+'\\n.set '+name+',mp_card_storage+'+str(offset)+'\\n");\n'
        changed=changed[:start]+storage+'\n'+changed[end:]
    if source.name=='gmmain.c':
        start=changed.index('int main(void)')
        prefix,body=changed[:start],changed[start:]
        body=re.sub(r'^(    )([A-Za-z_][A-Za-z_0-9]*)\(([^;\n]*)\);$',lambda m:m[1]+'OSReport("Startup: '+m[2]+'\\n");\n'+m[0],body,flags=re.M)
        changed=prefix+body
        changed=changed.replace('    gm_801A4510();','    gmMainLib_8046B0F0.skip_intro = true; /* Homebrew boots past optional THP movie. */\n    gm_801A4510();')
    if source.name in ('lbmemory.c','lbfile.c','ftdata.c'):
        # These comparisons distinguish virtual ARAM offsets (under 16 MiB)
        # from dynamically allocated MRAM buffers (3DS heap starts at 128 MiB).
        changed=changed.replace('< 0x80000000','< 0x02000000').replace('>= 0x80000000','>= 0x02000000')
    if source.name=='gm_1A3F.c':
        changed=changed.replace('    preloadState(state);','    extern void mp_ucf_reset(void); mp_ucf_reset();\n    preloadState(state);')
        changed=changed.replace('    preloadState(state);','    extern void mp_display_set_world(void*); mp_display_set_world(NULL);\n    preloadState(state);')
        changed=changed.replace('    mode = findMode(mode_kind);','    OSReport("Enter game mode %u\\n",mode_kind);\n    mode = findMode(mode_kind);')
        changed=changed.replace('    preloadState(state);','    OSReport("Preload scene %u\\n",state->info.scene_kind);\n    u32 mp_load_start = OSGetTick();\n    preloadState(state);\n    OSReport("Preload complete ticks=%u\\n", OSGetTick() - mp_load_start);')
    if source.name=='devcom.c':
        changed=changed.replace('    return (bool) devComStatus[idx];','    extern void mp_engine_poll(void); mp_engine_poll();\n    return (bool) devComStatus[idx];')
        changed=changed.replace('HSD_ASSERT(0x1F0, dest % 32 == 0);','if(dest%32) OSReport("Unaligned device request file=%d src=%x dest=%x size=%x type=%x\\n",file,src,dest,size,type);\n    HSD_ASSERT(0x1F0, dest % 32 == 0);')
    if source.name=='lbcardnew.c':
        # An absent physical card cannot contain the optional Pikmin save.
        changed=changed.replace('    lb_80019EF0(chan, 0, 0, 0);','    if(!CARDProbe(chan)) return lb_80019BB8(CARD_RESULT_NOCARD);\n    lb_80019EF0(chan, 0, 0, 0);')
    if changed==contents:return source
    selected=ROOT/'build/generated/engine-overlays'/source.relative_to(ROOT)
    selected.parent.mkdir(parents=True,exist_ok=True)
    selected.write_text(changed,encoding='utf-8')
    return selected
