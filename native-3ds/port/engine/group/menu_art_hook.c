/* Fills the top-screen Solo / Group submenu's art into the menu's tables, in memory, each time MnMaAll loads (see
 * menu_art.c). The tables are found by the archive's structure (the Slippi archive is a full re-serialisation with every
 * offset moved), checked, and only then changed; anything unexpected (the Japanese archive, a mod) leaves them as they
 * are, and the Melee row keeps the bottom-screen Solo / Group page. */
#include <stdio.h>
#include <string.h>
#include <sysdolphin/baselib/aobj.h>
#include <sysdolphin/baselib/fobj.h>
#include <sysdolphin/baselib/mobj.h>
#include <sysdolphin/baselib/tobj.h>
#include <group_engine.h>
#include "menu_art.h"

void mp_platform_log(const char* text);

#define LABEL_JOINT 3    /* MenMainCursor_Top: the row label */
#define TAB_JOINT 85     /* MenMainPanel_Top: the submenu tab beside the header */

static int art_ok;
static int composed;
static unsigned char solo_px[MP_ART_LABEL_BYTES] __attribute__((aligned(32)));
static unsigned char group_px[MP_ART_LABEL_BYTES] __attribute__((aligned(32)));
static unsigned char tab_px[MP_ART_TAB_BYTES] __attribute__((aligned(32)));
static HSD_ImageDesc solo_desc, group_desc, tab_desc;
static MpArtTables tables;   /* 3 KB: not on the stack */

/* The n'th joint of a material animation tree, in the joint tree's order (a joint, then its children, then the next). */
static HSD_MatAnimJoint* nth_joint(HSD_MatAnimJoint* j, int* left)
{
    for (; j != NULL; j = j->next) {
        HSD_MatAnimJoint* found;
        if ((*left)-- == 0) {
            return j;
        }
        if ((found = nth_joint(j->child, left)) != NULL) {
            return found;
        }
    }
    return NULL;
}

/* The joint's first texture animation and its image track. */
static HSD_TexAnim* texanim_of(HSD_MatAnimJoint* root, int index, HSD_FObjDesc** track)
{
    HSD_MatAnimJoint* j = nth_joint(root, &index);
    HSD_TexAnim* ta;
    *track = NULL;
    if (j == NULL || j->matanim == NULL || (ta = j->matanim->texanim) == NULL || ta->imagetbl == NULL ||
        ta->aobjdesc == NULL || (*track = ta->aobjdesc->fobjdesc) == NULL || (*track)->ad == NULL)
    {
        return NULL;
    }
    return ta;
}

static unsigned read_table(HSD_TexAnim* ta, MpArtImage* out)
{
    unsigned i, n = ta->n_imagetbl;
    if (n > MP_ART_MAX) {
        return n;   /* refused by the check */
    }
    for (i = 0; i < n; i++) {
        HSD_ImageDesc* d = ta->imagetbl[i];
        out[i].px = d != NULL ? d->image_ptr : NULL;
        out[i].w = d != NULL ? d->width : 0;
        out[i].h = d != NULL ? d->height : 0;
        out[i].fmt = d != NULL ? (unsigned) d->format : 0;
    }
    return n;
}

static void point_at(HSD_ImageDesc** slot, HSD_ImageDesc* desc, unsigned char* px, unsigned w, unsigned h)
{
    *desc = **slot;   /* the blank's LOD settings */
    desc->image_ptr = px;
    desc->width = w;
    desc->height = h;
    *slot = desc;
}

void mp_group_menu_art(struct HSD_MatAnimJoint* labels, struct HSD_MatAnimJoint* tabs)
{
    HSD_FObjDesc *label_track, *tab_track;
    HSD_TexAnim* lta = texanim_of(labels, LABEL_JOINT, &label_track);
    HSD_TexAnim* tta = texanim_of(tabs, TAB_JOINT, &tab_track);
    const char* why = NULL;
    unsigned key = 0;
    char line[160];

    art_ok = 0;
    if (lta == NULL || tta == NULL) {
        why = "the label or tab animation is missing";
    } else if (lta->imagetbl[MP_ART_SOLO] == &solo_desc) {
        art_ok = 1;   /* already filled: this archive was not reloaded */
        return;
    } else {
        memset(&tables, 0, sizeof tables);
        tables.labels = read_table(lta, tables.label);
        tables.tabs = read_table(tta, tables.tab);
        tables.label_track = label_track->ad;
        tables.label_track_len = label_track->length;
        tables.label_frac = label_track->frac_value;
        tables.tab_track = tab_track->ad;
        tables.tab_track_len = tab_track->length;
        tables.tab_frac = tab_track->frac_value;
        why = mp_menu_art_check(&tables, &key);
    }
    if (why != NULL) {
        snprintf(line, sizeof line, "group: menu art not filled (%s): the Solo / Group page stays on the bottom screen\n",
                 why);
        mp_platform_log(line);
        return;
    }
    if (!composed) {
        mp_menu_art_compose(&tables, solo_px, group_px, tab_px);
        composed = 1;   /* the sources were checked by hash: the same every time */
    }
    point_at(&lta->imagetbl[MP_ART_SOLO], &solo_desc, solo_px, MP_ART_LABEL_W, MP_ART_LABEL_H);
    point_at(&lta->imagetbl[MP_ART_GROUP], &group_desc, group_px, MP_ART_LABEL_W, MP_ART_LABEL_H);
    point_at(&tta->imagetbl[MP_ART_TAB], &tab_desc, tab_px, MP_ART_TAB_W, MP_ART_TAB_H);
    tab_track->ad[key] = (u8) (MP_ART_TAB << (tab_track->frac_value & 0x1F));
    art_ok = 1;
    mp_platform_log("group: menu art filled (Solo, Group, the Melee tab)\n");
}

int mp_group_menu_art_ok(void)
{
    return art_ok;
}
