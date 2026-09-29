"""Explicit source adaptations, without changing the pinned upstream checkout."""
import re
from build import ROOT
def adapt(source):
    # Per-file overlays below, then layout fixes (layout_fixes/), then the
    # gameplay mods both builds ship (gameplay_mods.py), then 3DS
    # presentation edits (presentation_mods.py).
    result=_adapt(source)
    import layout_fixes,gameplay_mods,presentation_mods
    edits=layout_fixes.edits_for(source)+gameplay_mods.edits_for(source)+presentation_mods.edits_for(source)
    if not edits:return result
    text=result.read_text(encoding='utf-8')
    for old,new,count in edits:
        assert text.count(old)==count,(source.name,old[:80],text.count(old),count)
        text=text.replace(old,new)
    out=ROOT/'build/overlays'/source.name;out.parent.mkdir(parents=True,exist_ok=True)
    out.write_text(text,encoding='utf-8');return out
def _adapt(source):
    if source.name=='ftkirby.c':
        changed=source.read_text(encoding='utf-8')
        marker='void ftKb_SpecialN_800F14B4(Fighter_GObj* gobj)'
        assert changed.count(marker)==1
        helper='''/* The copied Game & Watch visibility lookup has one model group;
 * Kirby's body has two. Its next archive word is a relocated pointer,
 * interpreted as a negative/empty count on PPC but positive on ARM.
 * Keep the real group and explicitly empty the unused body groups.
 * Fighter player IDs are the six original player slots; Kirby has no
 * simultaneous partner fighter sharing a slot. No archive is modified. */
static FtPartsVisLookup mp_kirby_body_vis[6][11];
static FtPartsVisLookup* mp_kirby_body_visibility(Fighter* fp,
                                                FtPartsVisLookup* lookup)
{
    unsigned count = fp->u.kb.hat.x24.model_num;
    FtPartsVisLookup* body;
    HSD_ASSERT(0, fp->player_id < 6 && fp->x5AC.model_num <= 11 && count <= 11);
    if (count > fp->x5AC.model_num) count = fp->x5AC.model_num;
    body = mp_kirby_body_vis[fp->player_id];
    memset(body, 0, sizeof(mp_kirby_body_vis[0]));
    memcpy(body, lookup, count * sizeof(*body));
    return body;
}

'''
        changed=changed.replace(marker,helper+marker)
        old='    fp->x5AC.xC[4] = lookup;'
        assert changed.count(old)==1
        changed=changed.replace(old,'    fp->x5AC.xC[4] = mp_kirby_body_visibility(fp, lookup);')
        out=ROOT/'build/overlays'/source.name;out.parent.mkdir(parents=True,exist_ok=True)
        out.write_text(changed,encoding='utf-8');return out
    if source.name=='grzebes.c':
        changed=source.read_text(encoding='utf-8')
        old='/* 8049F140 */ static Vec3 grZe_8049F140[2];\n/* 8049F158 */ static Vec3 grZe_8049F158[2];\n/* 8049F170 */ static grZe_BubbleEntry grZe_8049F170[20];'
        assert changed.count(old)==1
        changed=changed.replace(old,'extern Vec3 grZe_8049F140[4],grZe_8049F158[2];\nextern grZe_BubbleEntry grZe_8049F170[20];')
        old='} grZe_BubbleState;'
        new=old+'\n/* The original views span all four endpoints and the bubble pool. ARM\n * section ordering cannot stand in for the original contiguous allocation. */\ngrZe_BubbleState mp_brinstar_bubbles __attribute__((aligned(8),used));\n_Static_assert(__builtin_offsetof(grZe_BubbleState,bubbles)==0x30,"Brinstar bubble ABI");\n_Static_assert(sizeof(grZe_BubbleState)==0x300,"Brinstar bubble context size");\n__asm__(".global grZe_8049F140\\n.set grZe_8049F140,mp_brinstar_bubbles\\n");\n__asm__(".global grZe_8049F158\\n.set grZe_8049F158,mp_brinstar_bubbles+24\\n");\n__asm__(".global grZe_8049F170\\n.set grZe_8049F170,mp_brinstar_bubbles+48\\n");\n'
        assert changed.count(old)==1;changed=changed.replace(old,new)
        old='        heights = column_heights;'
        assert changed.count(old)==1
        changed=changed.replace(old,old+'\n        /* A destroyed bubble need not visit every column. Retain the\n         * original uniform x-grid for those columns, rather than stack\n         * contents from unrelated calls. Active bubbles use this same\n         * expression; the two endpoints are still updated below. */\n        for (i = 0; i < 6; ++i) {\n            column_x[i] = (f32)i * column_width + state->positions[0].x;\n        }')
        out=ROOT/'build/overlays'/source.name;out.parent.mkdir(parents=True,exist_ok=True)
        out.write_text(changed,encoding='utf-8');return out
    if source.name=='mtx.c' and 'baselib' in source.parts:
        changed=source.read_text(encoding='utf-8')
        old='''    f32 sinX = sinf(vec2->x);
    f32 cosX = cosf(vec2->x);
    f32 sinY = sinf(vec2->y);
    f32 cosY = cosf(vec2->y);
    f32 sinZ = sinf(vec2->z);
    f32 cosZ = cosf(vec2->z);'''
        assert changed.count(old)==1
        changed=changed.replace(old,'''    f32 sinX, cosX, sinY, cosY, sinZ, cosZ;
    mp_rotation_sincos(vec2->x, &sinX, &cosX);
    mp_rotation_sincos(vec2->y, &sinY, &cosY);
    mp_rotation_sincos(vec2->z, &sinZ, &cosZ);''')
        changed='extern void mp_rotation_sincos(float,float*,float*);\n'+changed
        out=ROOT/'build/overlays'/source.name;out.parent.mkdir(parents=True,exist_ok=True)
        out.write_text(changed,encoding='utf-8');return out
    if source.name=='lbmthp.c':
        changed=source.read_text(encoding='utf-8')
        old='    fn_8001EB14(&MoviePlayer, filename);'
        assert changed.count(old)==1
        changed=changed.replace(old,'''    /* Movie decoding is optional on the native port. Report completion
     * without opening files, allocating decode buffers or starting alarms;
     * the scene owns the transition and cleanup. No decoded frame is faked. */
    if (!mp_movie_available()) {
        OSReport("Optional movie skipped (native decoder unavailable): %s\\n", filename);
        MoviePlayer = (THPDecComp){0};
        MoviePlayer.unk_144 = 1;
        return;
    }
'''+old)
        old='    THPDecComp* streamPlayer = &MoviePlayer;\n    PAD_STACK(8);'
        assert changed.count(old)==1
        changed=changed.replace(old,old+'\n    if (!MoviePlayer.power) return;')
        changed='extern int mp_movie_available(void);\n'+changed
        out=ROOT/'build/overlays'/source.name;out.parent.mkdir(parents=True,exist_ok=True)
        out.write_text(changed,encoding='utf-8');return out
    if source.name in ('gmopening.c','gmmovieend.c'):
        changed=source.read_text(encoding='utf-8')
        if source.name=='gmopening.c':
            old='    lbMthp_8001F578();'
            new='''    if (!mp_movie_available()) {
        gmMainLib_8015F500();
        lbAudioAx_800236DC();
        lbAudioAx_80023694();
        gm_801A4B60();
        gm_SetPendingGameMode(GM_TITLE);
        gm_SetNewGameModePending();
        return;
    }
'''+old
        else:
            old='    if (gm_804D6738 >= 0x1A4 ||'
            new='    if (!mp_movie_available() || gm_804D6738 >= 0x1A4 ||'
        assert changed.count(old)==1
        changed='extern int mp_movie_available(void);\n'+changed.replace(old,new)
        out=ROOT/'build/overlays'/source.name;out.parent.mkdir(parents=True,exist_ok=True)
        out.write_text(changed,encoding='utf-8');return out
    if source.name=='gmclassic.c':
        changed=source.read_text(encoding='utf-8')
        # Original code treats the intro plus randomized encounter order as
        # one context. Section alignment otherwise separates them on ARM.
        changed=changed.replace('u8 gm_804908A0[112];','extern u8 gm_804908A0[112];')
        old='gmClassicIntroData gmClassicIntroDataBuffer;'
        assert changed.count(old)==1
        changed=changed.replace(old,'''gmClassicRuntimeData mp_classic_runtime __attribute__((aligned(8),used));
_Static_assert(__builtin_offsetof(gmClassicRuntimeData,state)==0x20,"Classic runtime ABI");
extern gmClassicIntroData gmClassicIntroDataBuffer;
__asm__(".global gmClassicIntroDataBuffer\\n.set gmClassicIntroDataBuffer,mp_classic_runtime\\n");
__asm__(".global gm_804908A0\\n.set gm_804908A0,mp_classic_runtime+32\\n");''')
        old='    gmClassicSceneData* scene_data =\n        (gmClassicSceneData*) gm_Mode_Classic_States;'
        assert changed.count(old)==2
        changed=changed.replace(old,'')
        changed=changed.replace('scene_data->matchups.','gmClassic_803DDEC8.')
        out=ROOT/'build/overlays'/source.name;out.parent.mkdir(parents=True,exist_ok=True)
        out.write_text(changed,encoding='utf-8');return out
    if source.name=='toy.c':
        changed=source.read_text(encoding='utf-8')
        start=changed.index('/* 4A26B8 */')
        end=changed.index('/* 4D5A40 */',start)
        # Trophy code casts this header to a 0x404-byte context. The split
        # decompilation globals must share that allocation on ARM too, or
        # initialization writes elsewhere and selection sees an empty pool.
        changed=changed[:start]+'''u8 mp_toy_storage[0x404] __attribute__((aligned(8),used));
extern struct _Toy_804A26B8_t _Toy_804A26B8;
extern char _Toy_devtext_buf_804A26C4[0x8C];
extern char _Toy_devtext_buf_804A2750[0xFC];
extern u16 Toy_804A284C[302];
extern ToyAnimState Toy_804A2AA8;
'''+''.join('__asm__(".global '+name+'\\n.set '+name+',mp_toy_storage+'+str(offset)+'\\n");\n'
           for name,offset in [('_Toy_804A26B8',0),('_Toy_devtext_buf_804A26C4',0xC),
                               ('_Toy_devtext_buf_804A2750',0x98),('Toy_804A284C',0x194),
                               ('Toy_804A2AA8',0x3F0)])+changed[end:]
        # Several functions treat "TyLight.dat" (803FDD18) as the base of
        # the whole GameCube .data block and index the light, panel and
        # backdrop tables at their PPC offsets (+CC, +FC, +188, +1A4, +224,
        # +290). ARM lays those objects out separately, so the trophy
        # gallery, lottery and collection read garbage symbol indices
        # (hardware: data abort in Toy_80306D70 entering the lottery).
        # Name the tables those offsets denote.
        for old,new,count in [
            ('            entry = tbl + M2C_FIELD(state, s32*, 0x10) * 0xC;\n'
             '            if (*(s32*) (entry + 0x104) != 0) {\n'
             '                HSD_SetEraseColor(\n'
             '                    *(u8*) (entry + 0x100), *(u8*) (entry + 0x101),\n'
             '                    *(u8*) (entry + 0x102), *(u8*) (entry + 0x103));',
             '            s32 light = M2C_FIELD(state, s32*, 0x10);\n'
             '            if (_Toy_803FDDE4.values[light].flag != 0) {\n'
             '                GXColor color = _Toy_803FDDE4.values[light].color;\n'
             '                HSD_SetEraseColor(color.r, color.g, color.b, color.a);',1),
            ('    tbl = _Toy_str_TyLight_dat;\n','    (void) tbl;\n    (void) entry;\n',1),
            ('base->entries[arg0].idx','_Toy_803FDDE4.values[arg0].index',3),
            ('base->symbols[idx].name','_Toy_803FDDE4.symbols[idx].name',3),
            ('        base = (TyLightFile*) _Toy_str_TyLight_dat;\n','        (void) base;\n',1),
            ('    label = &data->ptrs[arg0];\n    joint[0] = HSD_ArchiveGetPublicAddress(tg->x50, *(label += 0x188 / 4));',
             '    label = &_Toy_803FDEA0[arg0];\n    joint[0] = HSD_ArchiveGetPublicAddress(tg->x50, *label);',1),
            ('(&data->ptrs[arg0 * 3])[0x224 / 4]','(&_Toy_803FDF3C)[arg0].animjoint',1),
            ('(&data->ptrs[arg0 * 3])[0x228 / 4]','(&_Toy_803FDF3C)[arg0].matanim_joint',1),
            ('(&data->ptrs[arg0 * 3])[0x22C / 4]','(&_Toy_803FDF3C)[arg0].shapeanim_joint',1),
            ('    ptr = (char**) (data + arg0 * 4);\n    if (*(ptr += 0x69) != NULL) {',
             '    ptr = &_Toy_803FDEBC[arg0];\n    if (*ptr != NULL) {',1),
            ('            arg0 = (u32) data + arg0 * 0xC;\n            ptr = ((ToyPanelLabelData*) arg0)->ptrs;\n'
             '            joint = HSD_ArchiveGetPublicAddress(td->archive, ptr[0x290 / 4]);\n'
             '            data = HSD_ArchiveGetPublicAddress(td->archive, ptr[0x294 / 4]);\n'
             '            shapanim =\n                HSD_ArchiveGetPublicAddress(td->archive, ptr[0x298 / 4]);',
             '            joint = HSD_ArchiveGetPublicAddress(td->archive, _Toy_803FDFA8[arg0].animjoint);\n'
             '            data = HSD_ArchiveGetPublicAddress(td->archive, _Toy_803FDFA8[arg0].matanim_joint);\n'
             '            shapanim =\n                HSD_ArchiveGetPublicAddress(td->archive, _Toy_803FDFA8[arg0].shapeanim_joint);',1)]:
            assert changed.count(old)==count,(old[:70],changed.count(old))
            changed=changed.replace(old,new)
        for stale in ('data->ptrs','base->entries','base->symbols','entry + 0x10','ptr[0x29'):
            assert stale not in changed,stale
        out=ROOT/'build/overlays'/source.name;out.parent.mkdir(parents=True,exist_ok=True)
        out.write_text(changed,encoding='utf-8');return out
    if source.name in ('gm_180A.c','gm_181A.c','gm_16F1.c','gmtoulib.c','gmtou_0.c','gmtou_1.c','gmtou_2.c','gmtoumode.c'):
        changed=source.read_text(encoding='utf-8')
        def once(old,new,count=1):
            nonlocal changed
            assert changed.count(old)==count,(source.name,old[:70],changed.count(old))
            changed=changed.replace(old,new)
        def shared(storage,size,members):
            # One allocation for globals the matching code reaches by offset
            # from the first; ARM data sections are placed independently.
            text='u8 '+storage+'['+hex(size)+'] __attribute__((aligned(8),used));\n'
            for name,offset in members:
                text+='__asm__(".global '+name+'\\n.set '+name+','+storage+'+'+str(offset)+'\\n");\n'
            return text
        if source.name=='gm_180A.c':
            # Home-Run Contest reads its distance tracker (80472EC8) as +0x80
            # of the contest state (80472E48). Separately placed, the writes
            # hit the model pointers after it (NULL joint at contest start)
            # and the real tracker stayed 0 (no distance, records or unlocks).
            once('static struct lbl_80472E48_t lbl_80472E48;\nstatic s32 lbl_80472EC8[4];',
                 'extern struct lbl_80472E48_t lbl_80472E48;\nextern s32 lbl_80472EC8[4];\n'
                 '_Static_assert(sizeof(struct lbl_80472E48_t)==0x80,"Home-Run state ABI");\n'+
                 shared('mp_homerun_state',0x90,[('lbl_80472E48',0),('lbl_80472EC8',0x80)]))
        if source.name=='gm_181A.c':
            # Multi-Man Melee reaches its record state (80473594) as +0x6BC of
            # 80472ED8; that only held by link-order chance.
            once('lbl_80472ED8_t lbl_80472ED8;\nRegClearRecordState lbl_80473594;',
                 'extern lbl_80472ED8_t lbl_80472ED8;\nextern RegClearRecordState lbl_80473594;\n'
                 '_Static_assert(sizeof(lbl_80472ED8_t)==0x6BC&&sizeof(RegClearRecordState)==0x14,"Multi-Man record ABI");\n'+
                 shared('mp_multiman_state',0x6D0,[('lbl_80472ED8',0),('lbl_80473594',0x6BC)]))
        if source.name=='gm_16F1.c':
            # Scores for all six player slots, as in fn_8016FAD4.
            once('    s32 player_net;\n    s32 scores[4];','    s32 player_net;\n    s32 scores[6];')
        if source.name in ('gmtoulib.c','gmtou_0.c','gmtou_1.c','gmtou_2.c'):
            # Tournament code mixes named TmData fields with GameCube byte
            # offsets (0x37 + n*0x12, 0x4B8...), which assume the packed
            # entrant rows gm/types.h declares only for matching builds.
            # LINT selects that packing (and checks the layout's size).
            changed='#define LINT 1\n'+changed
        if source.name=='gmtoulib.c':
            # The bracket camera is +0x24 of the three camera vectors, i.e.
            # the separate 803D9DD0; unset, zoom and pan used a NULL CObj.
            once('''        CObjData* cobj_data = (CObjData*) &lbl_803D9DAC;
        cobj_data->cobj_data.cobj = cobj;
        {
            HSD_CObj** cobj_ptr = &cobj_data->cobj_data.cobj;''','''        lbl_803D9DD0.cobj = cobj;
        {
            HSD_CObj** cobj_ptr = &lbl_803D9DD0.cobj;''')
            # The tournament settings (804771C4) follow the 64 bracket rows.
            once('((TmData*) &((BracketData*) lbl_80473AB8)->srcs[3])','(&gm_804771C4)',8)
            # Two-digit names write a terminator at [9]; the original rows
            # had linker padding there.
            for name in ('lbl_803D9EE8','lbl_803D9EF4','lbl_803D9F00'):
                once('static char '+name+'[] = {','static char '+name+'[12] = {')
        if source.name=='gmtoumode.c':
            # Sudden-death setup reads the match result that followed the
            # enter data in the GameCube image.
            once('&((MatchExitInfo*) (src + 1))->match_end','&gm_80487810.match_end')
        out=ROOT/'build/overlays'/source.name;out.parent.mkdir(parents=True,exist_ok=True)
        out.write_text(changed,encoding='utf-8');return out
    if source.name=='mnevent.c':
        changed=source.read_text(encoding='utf-8')
        # Strings and the fifth asset belong to separate C objects. The PPC
        # addresses happened to be adjacent; ARM section ordering need not be.
        changed=changed.replace('    char* strs;\n','').replace('    strs = (char*) &mnEvent_803EF740;\n','')
        for old,new in [('strs + 0x70','mnEvent_803EF7A0 + 0x10'),
                        ('strs + 0x88','mnEvent_803EF7A0 + 0x28'),
                        ('strs + 0x94','mnEvent_803EF7A0 + 0x34')]:
            assert changed.count(old)==1;changed=changed.replace(old,new)
        changed=changed.replace('    char* base = (char*) &mnEvent_803EF740;\n','')
        old='''        lbArchive_LoadSections(archive, arr, base + 0xA0, arr + 1, base + 0xB8,
                               arr + 2, base + 0xD4, arr + 3, base + 0xF4,
                               arr + 4, base + 0x118, 0);'''
        assert changed.count(old)==1
        changed=changed.replace(old,'''        lbArchive_LoadSections(archive,
            &arr[0], "MenMainConEv_Top_joint",
            &arr[1], "MenMainConEv_Top_animjoint",
            &arr[2], "MenMainConEv_Top_matanim_joint",
            &arr[3], "MenMainConEv_Top_shapeanim_joint",
            &mnEvent_804A0908[0], "MenMainMarkEv_Top_joint", NULL);''')
        out=ROOT/'build/overlays'/source.name;out.parent.mkdir(parents=True,exist_ok=True)
        out.write_text(changed,encoding='utf-8');return out
    if source.name=='mnmain.c':
        changed=source.read_text(encoding='utf-8')
        # The PPC stack's padding masked undersized scratch arrays. On ARM,
        # the fifth option overwrites fn_8022AFEC's saved r4. Special Melee
        # also writes ten entries to mn_8022A5D0's seven-entry array.
        for old in ('HSD_JObj* sp20[4];','HSD_JObj* spA0[7];'):
            assert changed.count(old)==1
            changed=changed.replace(old,old[:old.index('[')]+
                '[sizeof(mn_803EAE68) / sizeof(mn_803EAE68[0])];')
        out=ROOT/'build/overlays'/source.name;out.parent.mkdir(parents=True,exist_ok=True)
        out.write_text(changed,encoding='utf-8');return out
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
        # The original clears byte by byte; -fno-builtin keeps that loop, and
        # stage code clears large structures with it every frame. The word
        # store implementation writes the same zero bytes.
        old='''void memzero(void* mem, ssize_t size)
{
    u8* bytes = mem;
    while (size--) {
        *bytes++ = 0;
    }
}'''
        assert changed.count(old)==1
        changed=changed.replace(old,'''void memzero(void* mem, ssize_t size)
{
    if (size > 0) {
        __builtin_memset(mem, 0, size);
    }
}''')
        out=ROOT/'build/overlays'/source.name;out.parent.mkdir(parents=True,exist_ok=True)
        out.write_text(changed,encoding='utf-8');return out
    if source.name=='grpstadium.c':
        # Pokemon Stadium's big screen shows EFB copies of the frame. Keep the
        # 250x160 text window (rendered and copied every frame). Skip the two
        # live views, a 640x406 full-screen capture and a 124x80 player
        # close-up, which each need a GPU sync and a CPU readback here; during
        # those modes the screen shows its image preloaded from the archive.
        changed=source.read_text(encoding='utf-8')
        for old,new in (('        lb_800122C8(&copy->desc, 0, 36, 0);\n','        /* Live view disabled (native performance). */\n'),
                        ('        lb_800122C8(&new_var->desc, new_var->x1A, new_var->x1C, 0);\n','        /* Live view disabled (native performance). */\n')):
            assert changed.count(old)==1,(source.name,old)
            changed=changed.replace(old,new)
        # Without captures the live view images (7: whole stage, 8: player
        # close-up) are uninitialized preload memory. The screen shows its
        # text/Pokemon display instead; training, which only uses the live
        # view, keeps the idle screen. The chooser still rotates normally.
        old='''    gp->u.display.xEA = gp->u.display.xE4;
    gp->u.display.xE4 = arg1;'''
        assert changed.count(old)==1,source.name
        changed=changed.replace(old,'''    if (arg1 == 7 || arg1 == 8) {
        arg1 = gm_8018841C() ? 0 : 1;
    }
'''+old)
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
    if source.name not in ('lbaudio_ax.c','lbarq.c','camera.c','cobj.c','itcoin.c','mninfo.c','gmmain.c','devcom.c','lbmemory.c','lbfile.c','ftdata.c','gm_1A3F.c','lbcardnew.c','gmtitle.c','hsd_4D11.c','tydisplay.c','texp.c','psdisp.c','hsd_3915.c','gm_1832.c','synth.c','ftmaterial.c','grdisplay.c','gobjuserdata.c','eflib.c','shadow.c','lbrefract.c','itspawn.c','gm_1798.c','gm_1601.c','itfreeze.c','itlinkarrow.c','player.c','ftCo_Damage.c','particle.c','mnitemsw.c','mnname.c','mnnamenew.c','tobj.c','gm_1A45.c','lb_0192.c','cmsnap.c','lb_01F8.c','ground.c','gmstaffroll.c','lbbgflash.c','lbspdisplay.c'):return source
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
        changed=changed.replace('    current = cobj;', '    current = cobj;\n    mp_gx_display_width(mp_display_camera_width(cobj));\n    extern unsigned mp_display_camera_mips(HSD_CObj*);\n    extern void mp_gx_texture_mips(unsigned);\n    mp_gx_texture_mips(mp_display_camera_mips(cobj));\n    extern float mp_display_camera_convergence(HSD_CObj*);\n    extern void mp_gx_camera_convergence(float);\n    mp_gx_camera_convergence(mp_display_camera_convergence(cobj));')
        # The erase rectangle exactly fills the frustum at mid depth, where
        # stereo shifts it by up to 6 pixels per eye and bares the viewport
        # edge. Extend it sideways; the camera scissor still bounds the fill.
        old='''    HSD_EraseRect(top_res, bottom_res, left_res, right_res, -z_val,
                  enable_color, enable_alpha, enable_depth);'''
        assert changed.count(old)==1
        changed=changed.replace(old,'''    if (HSD_CObjGetProjectionType(cobj) != PROJ_ORTHO) {
        left_res *= 1.125F;
        right_res *= 1.125F;
    }
'''+old)
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
        # Only fn_80179990's two offscreen portrait-camera calls use arg0.
        # All four visible results cameras use gobj and stay untouched.
        old='Camera_800313E0(arg0, 0);'
        assert changed.count(old)==2,'Results portrait camera sites changed'
        changed=changed.replace(old,'mp_display_results_capture(arg0, 0);')
        # shared_img is only allocated and written in this translation unit;
        # unlike player_img1/player_img2, it is never bound or read. Retain
        # GX's copy-clear side effect without transferring its unused pixels.
        old='HSD_ImageDescCopyFromEFB(&lbl_8046E1B0.shared_img,'
        assert changed.count(old)==2,'Results scratch-image copy sites changed'
        changed=changed.replace(old,'mp_image_copy_clear(&lbl_8046E1B0.shared_img,')
        marker='extern ResultsData lbl_8046DBE8;'
        assert changed.count(marker)==1
        changed=changed.replace(marker,marker+'\nextern void mp_image_copy_clear(HSD_ImageDesc*, u16, u16, GXBool, bool);\nextern void mp_display_results_capture(HSD_GObj*, int);')
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
        if source.name=='tobj.c':
            # Keep the original descriptor/copy state setup and clear masks.
            # Only the explicitly audited unused destination uses this entry.
            helper=body.replace('HSD_ImageDescCopyFromEFB(', 'mp_image_copy_clear(',1).replace('GXCopyTex(', 'mp_gx_copy_clear_only(')
            assert helper.count('mp_gx_copy_clear_only(')==1
            changed+='\nextern void mp_gx_copy_clear_only(void*, int);\n'+helper+'\n'
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
    if source.name=='lbspdisplay.c':
        # The screen-capture display (Classic's stage-clear blur) redraws a
        # 640x480 capture of the whole display; span it in widescreen, and
        # take the capture with the same full-display column mapping.
        old='    cobj = HSD_CObjAlloc();'
        assert changed.count(old)==1
        changed=changed.replace(old,'    cobj = mp_display_overlay_camera(HSD_CObjAlloc());')
        old='''    if (data->callback != NULL) {
        data->callback(gobj);
    }'''
        assert changed.count(old)==1
        changed=changed.replace(old,'''    if (data->callback != NULL) {
        mp_gx_display_width(mp_display_camera_width(GET_COBJ(gobj)));
        data->callback(gobj);
    }''')
        changed='extern struct HSD_CObj* mp_display_overlay_camera(struct HSD_CObj*);\nextern unsigned mp_display_camera_width(struct HSD_CObj*);\nextern void mp_gx_display_width(unsigned);\n'+changed
    if source.name=='lbbgflash.c':
        # Hit flashes, fades and strip wipes are 640x480 quads under this
        # camera; in widescreen they must span the whole display.
        old='HSD_CObjLoadDesc((HSD_CObjDesc*) &lbl_803BB028)'
        assert changed.count(old)==2
        changed=changed.replace(old,'mp_display_overlay_camera('+old+')')
        changed=changed.replace('void lbBgFlash_800208EC(int arg0)','extern HSD_CObj* mp_display_overlay_camera(HSD_CObj*);\nvoid lbBgFlash_800208EC(int arg0)',1)
    if source.name=='gmstaffroll.c':
        # The staff roll's second camera draws only the HUD (sight, name plate,
        # score) as models just in front of it; show that layer flat. The 3D
        # corridor camera keeps its depth.
        old='        gm_804D6834 = cobj;'
        assert changed.count(old)==1
        changed=changed.replace(old,old+'\n        {\n            extern void mp_display_flat_camera(HSD_CObj*);\n            mp_display_flat_camera(cobj);\n        }')
    if source.name=='ground.c':
        # A map part's own background camera widens with the world view.
        old='            temp_r27 = lb_80013B14(archive->unk4->unk8[map_id].x10);'
        assert changed.count(old)==1
        changed=changed.replace(old,old+'\n            {\n                extern void mp_display_background_camera(HSD_CObj*);\n                mp_display_background_camera(temp_r27);\n            }')
    if source.name=='lb_01F8.c':
        # Congratulations art is one baseline JPEG ("THP" still). The THP
        # library's decoder is paired-single assembly; decode natively into
        # an RGB565 texture and show it as an ordinary sprite (the original
        # composites Y/U/V planes through a four-stage TEV).
        start=changed.index('    THPInit();\n    lbFile_80016760(filename, &lbl_804335B8.unk94, &lbl_804335B8.unk98);')
        end=changed.index('    HSD_Free(decode_buf);\n}',start)+len('    HSD_Free(decode_buf);\n}')
        changed=changed[:start]+'''    lbFile_80016760(filename, &lbl_804335B8.unk94, &lbl_804335B8.unk98);
    {
        u32 bytes = ((width + 3) / 4) * ((height + 3) / 4) * 32;
        lbl_804335B8.x20 = HSD_MemAlloc(bytes);
        if (!mp_platform_jpeg_rgb565(lbl_804335B8.unk94, lbl_804335B8.unk98,
                                     ((u32) width << 16) | (u32) height,
                                     lbl_804335B8.x20))
        {
            OSReport("Congratulations image not decoded: %s\\n", filename);
            memset(lbl_804335B8.x20, 0, bytes);
        }
        DCFlushRange(lbl_804335B8.x20, bytes);
        lbl_804335B8.x44 = NULL;
        lbl_804335B8.x68 = NULL;
    }
}'''+changed[end:]
        old='    lbl_804335B8.x70.image_ptr = NULL;'
        assert changed.count(old)==1;changed=changed.replace(old,'    lbl_804335B8.x70.image_ptr = lbl_804335B8.x20;')
        old='    lbl_804335B8.x70.format = GX_TF_RGBA8;'
        assert changed.count(old)==1;changed=changed.replace(old,'    lbl_804335B8.x70.format = GX_TF_RGB565;')
        old='    lbl_804335B8.x90->x40 |= 0x10;\n'
        assert changed.count(old)==1;changed=changed.replace(old,'')
        start=changed.index('void lbMthp8001F928(HSD_GObj* gobj, int arg1)\n{')
        end=changed.index('    HSD_SObjLib_803A49E0(gobj, arg1);\n}',start)
        changed=changed[:start]+'void lbMthp8001F928(HSD_GObj* gobj, int arg1)\n{\n'+changed[end:]
        changed='extern unsigned mp_platform_jpeg_rgb565(const void*, unsigned, unsigned, void*);\n'+changed
    if source.name=='cmsnap.c':
        # The camera snapshot is handed to the memory card code as pixels.
        # Every other texture copy is only sampled by GX and stays on the GPU.
        old='        lb_800122C8(&_p(unk1), 0, 0, 0);'
        assert changed.count(old)==1
        changed='extern void mp_gx_copy_mode(unsigned);\n'+changed.replace(old,'        mp_gx_copy_mode(1); /* CPU readback */\n'+old+'\n        mp_gx_copy_mode(0);')
    if source.name=='gm_1832.c':
        # Classic's team roster splash composites ten pre-rendered fighters
        # with per-pixel Z24X8 replacement. Use ordinary alpha portrait sprites
        # on PICA, retaining the original layout, reveal timing and colors.
        # One color capture replaces color+depth and saves three depth images.
        old='''            HSD_ImageDescCopyFromEFB(&lbl_804735E8.x40[i], 0x82, 0, 0, 0);
            HSD_ImageDescCopyFromEFB(&lbl_804735E8.x88[i], 0x82, 0, 1, 1);'''
        assert changed.count(old)==1
        # Alpha is each fighter's coverage: the depth image's role on GameCube.
        changed='extern void mp_gx_copy_mode(unsigned);\n'+changed.replace(old,
            '            mp_gx_copy_mode(2); /* coverage alpha */\n'
            '            HSD_ImageDescCopyFromEFB(&lbl_804735E8.x40[i], 0x82, 0, 1, 1);\n'
            '            mp_gx_copy_mode(0);')
        old='''        img[3].image_ptr = NULL;
        lb_800121FC(&img[3], 0x17C, 0x190, GX_TF_Z24X8, 0);'''
        assert changed.count(old)==1;changed=changed.replace(old,'')
        old='            desc.image2 = &lbl_804735E8.x88[img_idx[0x90]];'
        assert changed.count(old)==1;changed=changed.replace(old,'            desc.image2 = NULL;')
        old='''            sobj = HSD_SObjLib_803A477C(lbl_804735E8.xDC, &desc.desc, 0, 0,
                                        0x80, 1);'''
        assert changed.count(old)==1;changed=changed.replace(old,old.replace('0x80, 1','0x80, 0'))
        # The "Stage N" layout camera stages its title, VS emblem and caption
        # at unrelated depths (the title at a third of the focus distance, the
        # emblem behind the fighters it covers). Show that layer flat; the
        # fighter camera keeps its relief.
        old='    cobj = HSD_CObjLoadDesc(lbl_804D65FC->cameras[0].desc);'
        assert changed.count(old)==1
        changed=changed.replace(old,old+'\n    extern void mp_display_flat_camera(HSD_CObj*);\n    mp_display_flat_camera(cobj);')
        # The progress road map (IrRdMap) is drawn by the fighter camera but
        # staged near it; as part of the layout it belongs at the screen plane.
        old='    GObj_SetupGXLink(gobj, HSD_GObj_JObjCallback, 0xC, 0);'
        assert changed.count(old)==1
        changed=changed.replace(old,'    {\n        extern void mp_display_flat_callback(HSD_GObj*, int);\n        GObj_SetupGXLink(gobj, mp_display_flat_callback, 0xC, 0);\n    }')
    if source.name=='psdisp.c':
        changed=changed.replace('if (gp == NULL) {\n        *x = gp->pos.x;\n        *y = gp->pos.y;\n        *z = gp->pos.z;', 'if (gp == NULL) {\n        *x = pp->pos.x;\n        *y = pp->pos.y;\n        *z = pp->pos.z;')
    if source.name=='tydisplay.c':
        changed=re.sub(r'(?m)^inline (.*?) (_tyDisplay_803(?:18CB4|19994)_sort)',r'static inline \1 \2',changed)
        # Use the actual archive/joint/animation tables, not a struct cast
        # that assumes their PPC linker addresses are contiguous.
        for old,new,count in [('temp->arch_names','_tyDisplay_803B8AE0',2),
                              ('tables->arch_names','_tyDisplay_803B8AE0',1),
                              ('*(TyDspArchNames*) tables->jobj_names','_tyDisplay_803B8988',3),
                              ('*(TyDspArchNames*) tables->matanim_names','_tyDisplay_803B8A34',1)]:
            assert changed.count(old)==count;changed=changed.replace(old,new)
        for old in ('    const TyDspNameTables* temp;\n','    const TyDspNameTables* tables;\n',
                    '    tables = (TyDspNameTables const*) &_tyDisplay_803B8988;\n',
                    '    temp = tables;\n',
                    '    const TyDspNameTables* tables =\n        (TyDspNameTables const*) &_tyDisplay_803B8988;\n'):
            assert changed.count(old)==1;changed=changed.replace(old,'')
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
    if source.name=='lbaudio_ax.c':
        # The second setup block owns AXFX_DELAY, not AXFX_REVERBSTD. Passing
        # type 2 reads a reverb preDelay beyond the delay object's stack storage.
        # ThinLTO correctly exposes that undefined access as unreachable code.
        old='HSD_AudioGetAuxHeapSize(2, &delay)'
        assert changed.count(old)==1, 'Review the delay heap-size type fix'
        changed=changed.replace(old,'HSD_AudioGetAuxHeapSize(AXDRIVER_AUX_DELAY, &delay)')
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
        # Development hook: a debugger may name the next game mode (the menus
        # otherwise choose it). Zero, and unused, in normal play.
        changed=changed.replace('    mode = findMode(mode_kind);','    if (mp_test_mode_override) {\n        mode_kind = mp_test_mode_override;\n        mp_test_mode_override = 0;\n        /* As menu navigation does: 1P-mode checks read this. */\n        state_machine.routing.curr_mode = mode_kind;\n    }\n    OSReport("Enter game mode %u\\n",mode_kind);\n    mode = findMode(mode_kind);')
        changed+='\nvolatile u8 mp_test_mode_override;\n'
        changed=changed.replace('u8 runGameMode(u8 mode_kind)\n{','extern volatile u8 mp_test_mode_override;\nu8 runGameMode(u8 mode_kind)\n{',1)
        assert changed.count('extern volatile u8 mp_test_mode_override;')==1
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
