/* Group play on the character select: the rules (group_rules.h). The scene's side is tools/group_edits/group_css.py. */
#include <string.h>
#include <group_rules.h>

int gr_css_pad_for(int group, int dialog_up, int my, int port)
{
    if (!group) {
        return port;
    }
    if (dialog_up) {
        return GR_PAD_EMPTY;   /* the dialog reads pad 0 itself; the character select sees no buttons */
    }
    if (port == my) {
        return 0;              /* the console's only pad */
    }
    if (port == 0) {
        return my;             /* port 0 gets the channel of my port: unplugged */
    }
    return port;
}

int gr_css_remote(int group, int my, int door)
{
    return group && door != my;
}

int gr_css_unplugged(int group, int lob_ok, unsigned present, int pad_err, int door)
{
    if (group && lob_ok) {
        return !(lob_ok && (present >> door & 1));
    }
    return pad_err != 0;
}

int gr_css_can_edit_rules(int group, int lob_ok, int host)
{
    return !group || !lob_ok || host != 0;
}

int gr_css_shows_host_rules(int lob_ok, int host, int melee_valid)
{
    return lob_ok && !host && melee_valid;
}

unsigned gr_css_trigger(int watching, int hand, float x, float y, unsigned trigger)
{
    if (!watching) {
        return trigger;
    }
    if (hand != 1 && x > 17.3f && y > 22.0f) {   /* hand 1: holding a puck or a slider */
        return trigger & GR_PAD_A;
    }
    return 0;
}

int gr_css_hide_ready(int group, int dialog_up, int lob_ok, unsigned busy, int my, int watching)
{
    if (group && dialog_up) {
        return 1;
    }
    return group && lob_ok && ((busy & 0xF & ~(1u << my)) != 0 || watching);
}

int gr_css_hovering(int group, int p_kind, int token)
{
    return group && p_kind != 3 && token != 0;
}

int gr_css_door_in_use(int group, int lob_ok, unsigned in_use, unsigned my_doors, int d)
{
    return group && lob_ok && d >= 0 && d < 4 && (in_use >> d & 1) && !(my_doors >> d & 1);
}

int gr_css_auto_hc_from(int lob_ok, int port, int hc_mode, int kind, int my, int host)
{
    if (!lob_ok || port > 3) {
        return GR_HC_VANILLA;
    }
    if (hc_mode != 1) {
        return GR_HC_ZERO;
    }
    if ((kind == 0 && port == my) || (kind == 1 && host)) {
        return GR_HC_SAVE;      /* this console's own Human door, or (host) a CPU: from this save */
    }
    return GR_HC_SHOWN;         /* as the lobby last showed it */
}

GrByte gr_css_door_hc(int kind, int hc_mode, int value)
{
    if (kind == 2 || hc_mode == 0) {
        return 0;
    }
    return value >= 1 && value <= 9 ? (GrByte) value : 0;
}

int gr_css_door_has_tag(int kind, int d, int my, int use_tag, int nametag)
{
    return kind == 0 && d == my && use_tag && nametag != GR_NO_TAG;
}

void gr_css_read_door(const GrCssScene *s, int my, int applied_ok, const GrByte applied[GR_DOOR_BYTES],
                      GrByte o[GR_DOOR_BYTES])
{
    int kind = s->p_kind == 0 ? 0 : s->p_kind == 1 ? 1 : 2;
    int placed, hover, held, chosen;
    if (s->token == GR_TOKEN_REMOTE_HELD && applied_ok) {
        memmove(o, applied, GR_DOOR_BYTES);   /* another console holds this token: nothing here to report */
        return;
    }
    placed = kind != 2 && s->sel_icon < GR_ICONS && s->token == 0 && s->ckind >= 0 && s->ckind < GR_FIGHTERS;
    /* Held over a fighter: the door shows it see-through, and the others should too. Over no fighter the scene keeps
     * the last one in sel_icon and only marks sel_icon_prev, so both must agree. */
    hover = kind != 2 && !placed && s->sel_icon < GR_ICONS && s->sel_icon_prev == s->sel_icon && s->token == my + 1 &&
            s->icon_ckind >= 0 && s->icon_ckind < GR_FIGHTERS;
    held = kind != 2 && !placed && !hover && s->token == my + 1;
    chosen = placed ? 1 : hover ? 2 : held ? 3 : 0;
    o[0] = (GrByte) kind;
    o[1] = (GrByte) (placed ? s->ckind : hover ? s->icon_ckind : 0);
    /* the costume; with Teams the team's (what the door shows and the match uses) */
    o[2] = (GrByte) (chosen && chosen != 3 ? (s->teams ? s->color : s->costume) : 0);
    o[3] = (GrByte) (kind == 2 ? 0 : s->team);
    o[4] = (GrByte) (kind == 1 ? s->cpu_level : 3);
    o[5] = (GrByte) chosen;
    memset(o + 6, 0, 8);
    if (s->tag != NULL) {
        strncpy((char *) o + 6, s->tag, 8);   /* the tag's own text: an id means nothing in another save */
    }
    o[14] = (GrByte) s->handicap;
}

void gr_css_view_door(const MpGroupDoor *v, GrByte o[GR_DOOR_BYTES])
{
    int chosen = v->kind != 2 ? v->chosen : 0;   /* 0 none, 1 placed, 2 held over a fighter, 3 held over nothing */
    o[0] = v->kind;
    o[1] = chosen && chosen != 3 ? v->fighter : 0;
    o[2] = chosen && chosen != 3 ? v->color : 0;
    o[3] = v->kind == 2 ? 0 : v->team;
    o[4] = v->kind == 1 ? v->level : 3;
    o[5] = (GrByte) chosen;
    memcpy(o + 6, v->tag, 8);
    o[14] = v->kind != 2 ? v->handicap : 0;
}

void gr_css_door_to_send(const GrByte now[GR_DOOR_BYTES], int spare, MpGroupDoor *out)
{
    memset(out, 0, sizeof *out);
    memcpy(out, now, 14);
    out->handicap = now[14];
    out->spare = (GrByte) spare;   /* the door's kind before this edit, so the host can drop an edit from an older view */
}

void gr_css_put_plan(const GrByte v[GR_DOOR_BYTES], int hc_mode, GrCssPut *out)
{
    memset(out, 0, sizeof *out);
    out->kind = v[0] == 0 ? 0 : v[0] == 1 ? 1 : 3;
    out->ckind = v[5] == 1 ? v[1] : -1;   /* 2 and 3 show nothing in the door's slot of the roster */
    out->costume = v[2];
    out->team = v[3];
    out->cpu_level = v[4];
    out->fixed_hc = hc_mode == 2 && v[14] >= 1 && v[14] <= 9 ? v[14] : 0;
    out->remote_held = v[5] == 3;
    out->preview = v[5] == 2;
    out->preview_fighter = v[1];
}

int gr_css_sync_door(GrCssSync *s, const GrCssSyncIn *in, int *spare, int *why)
{
    const GrByte *now = in->now, *view = in->view;
    int changed = memcmp(now, s->applied, GR_DOOR_BYTES) != 0;
    *spare = 0;
    *why = GR_SYNC_QUIET;
    if (in->applied_ok && changed && (in->owned_elsewhere || in->watching)) {
        /* Another console's door changed here without that console: only Human / CPU / closed and the team are
         * anyone's to change. Anything else is this scene's own doing: show the lobby's door again. While watching a
         * match nothing is this console's to change. */
        if (in->watching || (now[0] == s->applied[0] && now[3] == s->applied[3])) {
            s->last_view_ok = 0;
            *why = GR_SYNC_RESTORED;
            return GR_SYNC_PUT | GR_SYNC_REREAD;
        }
    }
    if (!in->applied_ok || changed) {
        int flags = 0;
        if (in->applied_ok) {
            *spare = s->applied[0] + 1;
            s->pending = 12;
            s->last_view_ok = 0;
            *why = GR_SYNC_SENT;
            flags = GR_SYNC_SEND;
        }
        memcpy(s->applied, now, GR_DOOR_BYTES);
        if (!in->applied_ok && in->mine) {
            /* A scene that was just (re)built. Back from name entry (the door is Human in the lobby), what this
             * console's door holds (a new name tag, a fighter) is authoritative: tell the host if the lobby lacks it.
             * Otherwise the lobby's door wins: joining opens no door. */
            if (view[0] != 0 || (now[5] == 0 && now[6] == 0) || in->watching) {
                return GR_SYNC_PUT | GR_SYNC_REREAD;
            }
            if (memcmp(now, view, GR_DOOR_BYTES) != 0) {
                s->pending = 12;
                s->last_view_ok = 0;
                *spare = 0;   /* nothing was applied before: the host compares nothing */
                return GR_SYNC_SEND;
            }
        }
        return flags;
    }
    if (s->pending != 0) {
        s->pending--;
        return 0;
    }
    if (s->last_view_ok && memcmp(view, s->last_view, GR_DOOR_BYTES) == 0) {
        return 0;
    }
    memcpy(s->last_view, view, GR_DOOR_BYTES);
    s->last_view_ok = 1;
    if (memcmp(view, now, 6) != 0 || view[14] != now[14]) {
        *why = GR_SYNC_APPLIED;
        if (view[5] == 2) {
            memcpy(s->applied, view, GR_DOOR_BYTES);   /* a mirrored hover: what we show, not a local edit */
            return GR_SYNC_PUT;
        }
        return GR_SYNC_PUT | GR_SYNC_REREAD;
    }
    return 0;
}

void gr_css_sync_hc_mode(GrCssSync *s, const GrByte now[GR_DOOR_BYTES])
{
    s->applied[14] = now[14];
    s->last_view_ok = 0;
}

int gr_css_busy_step(GrCssBusy *b, const GrCssBusyIn *in, int *send)
{
    int i, resend, why = 0;
    unsigned doors = 0;
    *send = 0;
    if (in->scene == 0) {
        b->busy = 0;
        b->naming = -1;
        b->ruling = 0;
        for (i = 0; i < 4; i++) {
            if (in->tag_lists >> i & 1) {
                b->busy = 1;
                doors |= 1u << i;
                why = 2;                                  /* a name tag list (wins over a slider) */
            }
            if (in->sliders >> i & 1) {                   /* only this console's cursor acts: a held slider is ours */
                b->busy = 1;
                doors |= 1u << i;
                if (why == 0) {
                    why = 1;
                }
            }
        }
        if (in->holding) {
            doors |= 1u << in->my;                        /* this console's cursor holds something */
        }
    } else if (in->scene == 3 || in->scene == 4) {
        b->busy = 1;
        if (in->scene == 4) {
            b->naming = in->name_door & 3;                /* name entry for this door's tag */
        } else {
            b->ruling = 1;
        }
    }
    if (in->scene != 0) {
        /* 3 rules or 4 name entry, as seen when asked for; kept while it is up (5) */
        why = b->naming >= 0 ? 4 : b->ruling ? 3 : b->busy ? 1 : 0;
    }
    if (in->dialog_up) {
        b->busy = 1;   /* "End the session?" is up: busy like the rules page (READY waits, Start is refused) */
        why = 6;
    }
    if (b->naming >= 0) {
        doors |= 1u << b->naming;                         /* the door is in use while its name entry lasts */
    }
    b->doors = doors;
    if (b->wait > 0) {
        b->wait--;
        return 0;
    }
    if (b->busy && why == 0) {
        why = 1;
    }
    /* resend when the lobby lacks our busy bit or one of our doors; a lost release of our own door (the only one a held
     * token marks without the busy bit) is sent again now and then */
    resend = (int) (in->lob_busy >> in->my & 1) != b->busy || doors != b->doors_sent ||
             (doors & ~in->lob_in_use & 0xF) != 0 || why != b->why_sent || (b->busy && in->lob_why != why);
    if (!resend && !b->busy && doors == 0 && (in->lob_in_use >> in->my & 1) && ++b->idle_resend >= 60) {
        resend = 1;
    }
    if (!resend) {
        return 0;
    }
    b->idle_resend = 0;
    b->doors_sent = doors;
    b->why_sent = why;
    b->wait = 15;
    *send = why | (int) (doors << 4);
    return 1;
}

void gr_css_busy_scene(GrCssBusy *b)
{
    b->doors = 0;
    b->doors_sent = 0;
    b->why_sent = 0;
}

unsigned gr_css_stars(GrByte ko[4], const GrByte lobby[4], int phase, int scene)
{
    unsigned changed = 0;
    int d;
    if (phase == 3 || scene != 0) {
        return 0;
    }
    for (d = 0; d < 4; d++) {
        if (ko[d] != lobby[d]) {
            ko[d] = lobby[d];
            changed |= 1u << d;
        }
    }
    return changed;
}

int gr_css_start(int group, int go, int watching)
{
    if (!group || go) {
        return GR_START_VANILLA;
    }
    return watching ? GR_START_IGNORE : GR_START_ASK_HOST;
}

int gr_css_go_sss(int seen_css, int go, int phase)
{
    return seen_css && !go && phase != 0;
}

int gr_css_back_waits(int group, int confirm_leave)
{
    return group && !confirm_leave;
}

int gr_css_leave_step(int confirm_state)
{
    return confirm_state == 1 ? GR_LEAVE_GO : confirm_state == 2 ? GR_LEAVE_STAY : GR_LEAVE_WAIT;
}

int gr_css_soft_reset_backs(int scene, int dialog_up)
{
    return scene == 0 && !dialog_up;
}

void gr_css_rules_to_lobby(const GrOwnRules *g, MpGroupRules *r)
{
    memset(r, 0, sizeof *r);
    r->time_mode = g->mode != 1;
    r->stocks = (GrByte) g->stock_count;
    r->minutes = (GrByte) (r->time_mode ? g->time_limit : g->stock_time_limit);
    r->items = (GrByte) (g->item_freq == 255 ? 0 : g->item_freq + 1);   /* 0 off, else frequency + 1 */
    r->teams = g->teams ? 1 : 0;
    r->friendly_fire = g->friendly_fire ? 1 : 0;
    r->mode = (GrByte) g->mode;
    r->time_limit = (GrByte) g->time_limit;
    r->stock_count = (GrByte) g->stock_count;
    r->stock_time_limit = (GrByte) g->stock_time_limit;
    r->handicap = (GrByte) g->handicap;
    r->stage_sel = (GrByte) g->stage_sel;   /* the host's Stage Selection rule decides who picks the stage */
}

int gr_css_rules_key(const MpGroupRules *r)
{
    unsigned key = r->mode | (unsigned) r->time_limit << 8 | (unsigned) r->stock_count << 16 |
                   (unsigned) (r->stock_time_limit & 0x3F) << 24 | (unsigned) (r->handicap & 3) << 30;
    return (int) key;
}

int gr_css_rules_relayout(int old_key, int new_key)
{
    return old_key != -1 && ((unsigned) old_key >> 30 & 3) != ((unsigned) new_key >> 30 & 3);
}
