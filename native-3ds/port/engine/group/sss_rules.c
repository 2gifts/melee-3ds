/* Group play on the stage select: the rules (group_rules.h). The scene's side is tools/group_edits/group_css.py
 * (SSS_CODE) and port/engine/group/group.c (entering and leaving). */
#include <group_rules.h>

int gr_sss_picker(int lobby_picker)
{
    return lobby_picker < 4 ? lobby_picker : -1;
}

int gr_sss_no_cursor(int group, int picker, int me)
{
    return group && picker >= 0 && picker != me;
}

int gr_sss_random_to_host(int group, int lob_ok, int host)
{
    return group && lob_ok && !host;
}

int gr_sss_frame(const GrSssIn *in, GrSssOut *out)
{
    out->stage = 0;
    out->set_stage = 0;
    out->remote = 0;
    if (in->lost) {
        return GR_SSS_LOST;
    }
    if (in->hidden >= 0) {
        /* A hidden pick's "Now loading" (30 to 90 frames, as for any pick): leave with its stage once it is over. */
        if (in->loading == 2 && in->timer == 0) {
            if (in->hidden != GR_SSS_FROM_LOBBY) {
                out->stage = in->hidden;
                out->set_stage = 1;
            } else if (in->stage_picked && in->stage != MP_GROUP_STAGE_RANDOM) {
                out->stage = in->stage;   /* else the match's block names it */
                out->set_stage = 1;
            }
            return GR_SSS_LEAVE;
        }
        return GR_SSS_NONE;
    }
    if (in->loading != 0) {
        /* our pick animates here: tell the others at once */
        return !in->remote && !in->sent ? GR_SSS_SEND : GR_SSS_NONE;
    }
    if (in->stage_picked || in->phase == 2) {
        /* someone else picked: show the same pick here */
        out->remote = 1;
        out->stage = in->stage;
        if (in->stage == MP_GROUP_STAGE_RANDOM) {
            if (in->host) {
                return in->resolving ? GR_SSS_NONE : GR_SSS_RESOLVE;   /* this save picks; shown next frame */
            }
            out->stage = GR_SSS_FROM_LOBBY;
            return GR_SSS_HIDDEN;   /* the host picks: load without a stage */
        }
        return GR_SSS_SHOW;
    }
    if (in->phase == 0) {
        out->remote = 1;
        return GR_SSS_BACK;
    }
    return GR_SSS_NONE;
}

int gr_sss_find(const int stkind[GR_SSS_ICONS], const int unlocked[GR_SSS_ICONS], int stage)
{
    int i;
    for (i = 0; i < GR_SSS_ICONS; i++) {
        if (stkind[i] == stage) {
            return unlocked[i] < 2 ? GR_SSS_LOCKED : i;
        }
    }
    return GR_SSS_UNKNOWN;
}

int gr_sss_host_picks(int host, int stage_sel)
{
    return host && (stage_sel == 1 || stage_sel == 2);
}

int gr_sss_unk_stage(int lobby_picker)
{
    return lobby_picker < 4 ? lobby_picker + 1 : 0;
}

int gr_sss_exit_sends(int sent, int remote)
{
    return !sent && !remote;
}
