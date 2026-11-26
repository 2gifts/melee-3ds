/* Host tests for group play's character select rules (port/engine/group/css_rules.c): which pad a cursor reads, doors
 * in use, busy, READY, the read-only character select while watching, the door sync with the lobby (joining opens no
 * door, local edits sent and held, the lobby's answer applied, another console's door restored), auto handicap, the
 * host's rules, the stars, Start and Back. Run by tools/test_group.py. */
#include <group_rules.h>
#include <stdio.h>
#include <string.h>

static int failures;
#define CHECK(c) do { if (!(c)) { printf("group_css: FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)

static void door_bytes(GrByte o[GR_DOOR_BYTES], int kind, int fighter, int color, int team, int level, int chosen,
                       int hc)
{
    memset(o, 0, GR_DOOR_BYTES);
    o[0] = (GrByte) kind; o[1] = (GrByte) fighter; o[2] = (GrByte) color; o[3] = (GrByte) team;
    o[4] = (GrByte) level; o[5] = (GrByte) chosen; o[14] = (GrByte) hc;
}

static GrCssScene scene_door(int p_kind, int fighter)
{
    GrCssScene s;
    memset(&s, 0, sizeof s);
    s.p_kind = p_kind;
    s.sel_icon = 3;
    s.sel_icon_prev = 3;
    s.icon_ckind = fighter;
    s.ckind = fighter;
    s.costume = 2;
    s.color = 4;
    s.team = 1;
    s.cpu_level = 7;
    return s;
}

static void pads_and_switches(void)
{
    /* outside a group every port reads its own pad */
    CHECK(gr_css_pad_for(0, 1, 2, 3) == 3);
    /* in a group: my door reads pad 0 (the console's only pad), port 0 my port's (unplugged) channel */
    CHECK(gr_css_pad_for(1, 0, 2, 2) == 0);
    CHECK(gr_css_pad_for(1, 0, 2, 0) == 2);
    CHECK(gr_css_pad_for(1, 0, 2, 3) == 3);
    CHECK(gr_css_pad_for(1, 0, 0, 0) == 0);
    /* under a dialog nothing reaches the character select */
    CHECK(gr_css_pad_for(1, 1, 2, 2) == GR_PAD_EMPTY);
    CHECK(gr_css_remote(1, 1, 0) && !gr_css_remote(1, 1, 1) && !gr_css_remote(0, 1, 0));
    /* the Human / CPU / closed cycle stops on Human only where a console sits; without a lobby, by the pad */
    CHECK(!gr_css_unplugged(1, 1, 0x5, 1, 2));
    CHECK(gr_css_unplugged(1, 1, 0x5, 0, 1));
    CHECK(gr_css_unplugged(0, 1, 0x5, 1, 2) && !gr_css_unplugged(1, 0, 0, 0, 1));
    /* rules and the Teams switch are the host's; a joiner shows the host's rules */
    CHECK(gr_css_can_edit_rules(1, 1, 1) && !gr_css_can_edit_rules(1, 1, 0) && gr_css_can_edit_rules(0, 1, 0) &&
          gr_css_can_edit_rules(1, 0, 0));
    CHECK(gr_css_shows_host_rules(1, 0, 1) && !gr_css_shows_host_rules(1, 1, 1) && !gr_css_shows_host_rules(1, 0, 0) &&
          !gr_css_shows_host_rules(0, 0, 1));
}

static void watching(void)
{
    /* a console watching a match: its cursor moves; A on the Back button (top right) is all that acts */
    CHECK(gr_css_trigger(0, 0, 0.0f, 0.0f, 0x1F00) == 0x1F00);
    CHECK(gr_css_trigger(1, 0, 20.0f, 23.0f, GR_PAD_A | 0x200 | GR_PAD_START) == GR_PAD_A);
    CHECK(gr_css_trigger(1, 0, 20.0f, 23.0f, 0x200) == 0);                 /* B there does nothing */
    CHECK(gr_css_trigger(1, 0, 0.0f, 0.0f, GR_PAD_A) == 0);                /* A over a fighter does nothing */
    CHECK(gr_css_trigger(1, 0, 20.0f, 10.0f, GR_PAD_A | GR_PAD_START) == 0);
    CHECK(gr_css_trigger(1, 1, 20.0f, 23.0f, GR_PAD_A) == 0);              /* holding a puck or slider: nothing */
    /* Start does nothing while watching; READY never shows */
    CHECK(gr_css_start(1, 0, 1) == GR_START_IGNORE);
    CHECK(gr_css_hide_ready(1, 0, 1, 0, 0, 1));
}

static void ready_and_start(void)
{
    /* READY waits while another console is busy, never for this console's own bit (vanilla waits for its own lists) */
    CHECK(gr_css_hide_ready(1, 0, 1, 0x2, 0, 0));
    CHECK(!gr_css_hide_ready(1, 0, 1, 0x1, 0, 0));
    CHECK(!gr_css_hide_ready(1, 0, 1, 0x10, 0, 0));   /* only bits 0..3 are consoles */
    CHECK(gr_css_hide_ready(1, 1, 0, 0, 0, 0));       /* under a dialog */
    CHECK(!gr_css_hide_ready(0, 1, 1, 0xF, 0, 1));    /* outside a group: vanilla */
    CHECK(gr_css_hovering(1, 0, 1) && !gr_css_hovering(1, 3, 1) && !gr_css_hovering(1, 0, 0) && !gr_css_hovering(0, 0, 1));
    /* Start asks the host; after the host sent everyone on, vanilla's */
    CHECK(gr_css_start(1, 0, 0) == GR_START_ASK_HOST);
    CHECK(gr_css_start(1, 1, 0) == GR_START_VANILLA && gr_css_start(0, 0, 0) == GR_START_VANILLA);
    /* the stage select comes once the lobby left its character select after this scene saw it */
    CHECK(!gr_css_go_sss(0, 0, 1));                   /* a view from before (the match's phase) */
    CHECK(gr_css_go_sss(1, 0, 1) && !gr_css_go_sss(1, 1, 1) && !gr_css_go_sss(1, 0, 0));
    /* Back: "End the session?" unless the menu says leave now; its answer */
    CHECK(gr_css_back_waits(1, 0) && !gr_css_back_waits(1, 1) && !gr_css_back_waits(0, 0));
    CHECK(gr_css_leave_step(0) == GR_LEAVE_WAIT && gr_css_leave_step(1) == GR_LEAVE_GO &&
          gr_css_leave_step(2) == GR_LEAVE_STAY);
    /* L+R+Start in the lobby is Back, not under a dialog or a sub-scene */
    CHECK(gr_css_soft_reset_backs(0, 0) && !gr_css_soft_reset_backs(0, 1) && !gr_css_soft_reset_backs(5, 0));
}

static void doors_in_use(void)
{
    /* door 2 held on another console: its Human / CPU / closed switch does nothing here ... */
    CHECK(gr_css_door_in_use(1, 1, 0x4, 0x0, 2));
    /* ... but does on the console using it, and on every console once it is let go */
    CHECK(!gr_css_door_in_use(1, 1, 0x4, 0x4, 2));
    CHECK(!gr_css_door_in_use(1, 1, 0x0, 0x0, 2));
    CHECK(!gr_css_door_in_use(0, 1, 0x4, 0, 2) && !gr_css_door_in_use(1, 0, 0x4, 0, 2));
    CHECK(!gr_css_door_in_use(1, 1, 0xFF, 0, 4) && !gr_css_door_in_use(1, 1, 0xFF, 0, -1));
}

static int busy_frames(GrCssBusy *b, GrCssBusyIn *in, int frames, int *last)
{
    int sends = 0, send, i;
    for (i = 0; i < frames; i++) {
        if (gr_css_busy_step(b, in, &send)) {
            sends++;
            *last = send;
            in->lob_busy = (unsigned) (b->busy << in->my);   /* the host takes it */
            in->lob_in_use = (unsigned) send >> 4;
            in->lob_why = send & 0xF;
        }
    }
    return sends;
}

static void busy(void)
{
    GrCssBusy b = { 0, -1, 0, 0, 0, 0, 0, 0 };
    GrCssBusyIn in;
    int last = -1;
    memset(&in, 0, sizeof in);
    in.my = 1;
    /* idle and the lobby agrees: nothing to send */
    CHECK(busy_frames(&b, &in, 100, &last) == 0);
    /* a CPU's level slider grabbed on door 2: busy (1, a slider) with door 2 in use, sent once */
    in.sliders = 0x4;
    CHECK(busy_frames(&b, &in, 30, &last) == 1 && last == (1 | 0x4 << 4) && b.busy && b.doors == 0x4);
    /* a name tag list on door 1 as well: a list wins over a slider (2) */
    in.tag_lists = 0x2;
    CHECK(busy_frames(&b, &in, 30, &last) == 1 && last == (2 | 0x6 << 4));
    /* let go of both: not busy, no doors */
    in.sliders = in.tag_lists = 0;
    CHECK(busy_frames(&b, &in, 30, &last) == 1 && last == 0 && !b.busy && b.doors == 0);
    /* a lost send is sent again once the hold-off is over (the lobby still lacks the bit) */
    in.sliders = 0x1;
    CHECK(gr_css_busy_step(&b, &in, &last) == 1 && last == (1 | 1 << 4));
    {
        int i, again = 0, send;
        for (i = 0; i < 15; i++) {
            again += gr_css_busy_step(&b, &in, &send);
        }
        CHECK(again == 0);                            /* 15 frames' hold-off */
        CHECK(gr_css_busy_step(&b, &in, &send) == 1 && send == (1 | 1 << 4));
    }
    in.lob_busy = 1u << in.my;
    in.lob_in_use = 0x1;
    in.lob_why = 1;
    in.sliders = 0;
    busy_frames(&b, &in, 40, &last);                  /* the release, after the last send's hold-off */
    /* name entry asked for door 1 (scene 4), then up (5): busy (4) with door 1 in use until the scene is back */
    in.scene = 4;
    in.name_door = 1;
    CHECK(busy_frames(&b, &in, 1, &last) == 1 && last == (4 | 0x2 << 4) && b.naming == 1);
    in.scene = 5;
    CHECK(busy_frames(&b, &in, 30, &last) == 0 && b.busy && b.doors == 0x2);
    in.scene = 0;
    CHECK(busy_frames(&b, &in, 30, &last) == 1 && last == 0 && b.naming == -1);
    /* the rules page (scene 3): busy (3), no door */
    in.scene = 3;
    CHECK(busy_frames(&b, &in, 30, &last) == 1 && last == 3);
    in.scene = 0;
    busy_frames(&b, &in, 30, &last);
    /* "End the session?" up: busy (6) */
    in.dialog_up = 1;
    CHECK(busy_frames(&b, &in, 30, &last) == 1 && last == 6);
    in.dialog_up = 0;
    busy_frames(&b, &in, 30, &last);
    /* this console's cursor holding its own token: the door is in use, not busy; a lost release is sent again after
     * 60 idle frames */
    in.holding = 1;
    CHECK(busy_frames(&b, &in, 30, &last) == 1 && last == (0x2 << 4) && !b.busy);
    in.holding = 0;
    CHECK(busy_frames(&b, &in, 30, &last) == 1 && last == 0);
    in.lob_in_use = 0x2;                              /* the host never got the release */
    CHECK(busy_frames(&b, &in, 59, &last) == 0);
    CHECK(busy_frames(&b, &in, 1, &last) == 1 && last == 0);
    /* a new scene forgets what was sent */
    gr_css_busy_scene(&b);
    CHECK(b.doors == 0 && b.doors_sent == 0 && b.why_sent == 0);
}

static void read_and_view(void)
{
    GrByte o[GR_DOOR_BYTES], applied[GR_DOOR_BYTES];
    GrCssScene s = scene_door(0, 9);
    MpGroupDoor v;
    /* a placed fighter: kind, fighter, costume, team, level 3 for a Human, chosen 1 */
    gr_css_read_door(&s, 0, 1, applied, o);
    CHECK(o[0] == 0 && o[1] == 9 && o[2] == 2 && o[3] == 1 && o[4] == 3 && o[5] == 1);
    /* with Teams, the costume is the team's colour */
    s.teams = 1;
    gr_css_read_door(&s, 0, 1, applied, o);
    CHECK(o[2] == 4);
    s.teams = 0;
    /* a CPU reports its level; a closed door nothing */
    s.p_kind = 1;
    gr_css_read_door(&s, 0, 1, applied, o);
    CHECK(o[0] == 1 && o[4] == 7);
    s.p_kind = 3;
    gr_css_read_door(&s, 0, 1, applied, o);
    CHECK(o[0] == 2 && o[1] == 0 && o[3] == 0 && o[5] == 0);
    /* held by my cursor (port 0 + 1) over a fighter: a preview (2) with that fighter; over nothing (3) */
    s = scene_door(0, 9);
    s.token = 1;
    gr_css_read_door(&s, 0, 1, applied, o);
    CHECK(o[5] == 2 && o[1] == 9 && o[2] == 2);
    s.sel_icon_prev = GR_ICONS;
    gr_css_read_door(&s, 0, 1, applied, o);
    CHECK(o[5] == 3 && o[1] == 0 && o[2] == 0);
    /* held by another console: what was applied, never what this scene shows */
    s.token = GR_TOKEN_REMOTE_HELD;
    door_bytes(applied, 0, 5, 1, 0, 3, 2, 0);
    gr_css_read_door(&s, 0, 1, applied, o);
    CHECK(memcmp(o, applied, GR_DOOR_BYTES) == 0);
    /* only this console's own Human door carries its tag */
    CHECK(gr_css_door_has_tag(0, 1, 1, 1, 4));
    CHECK(!gr_css_door_has_tag(0, 2, 1, 1, 4) && !gr_css_door_has_tag(1, 1, 1, 1, 4) &&
          !gr_css_door_has_tag(0, 1, 1, 0, 4) && !gr_css_door_has_tag(0, 1, 1, 1, GR_NO_TAG));
    s = scene_door(0, 9);
    s.tag = "ABCDEFGHIJ";
    s.handicap = 6;
    gr_css_read_door(&s, 0, 0, applied, o);
    CHECK(memcmp(o + 6, "ABCDEFGH", 8) == 0 && o[14] == 6);
    /* the lobby's door as shown: a preview or nothing shows no fighter for "held over nothing"; a closed door none */
    memset(&v, 0, sizeof v);
    v.kind = 0; v.fighter = 9; v.color = 3; v.team = 2; v.level = 8; v.chosen = 3; v.handicap = 4;
    gr_css_view_door(&v, o);
    CHECK(o[1] == 0 && o[2] == 0 && o[4] == 3 && o[5] == 3 && o[14] == 4);
    v.kind = 1; v.chosen = 1;
    gr_css_view_door(&v, o);
    CHECK(o[1] == 9 && o[2] == 3 && o[3] == 2 && o[4] == 8);
    v.kind = 2;
    gr_css_view_door(&v, o);
    CHECK(o[0] == 2 && o[3] == 0 && o[5] == 0 && o[14] == 0);
}

static void put_plan(void)
{
    GrByte v[GR_DOOR_BYTES];
    GrCssPut p;
    door_bytes(v, 2, 0, 0, 0, 3, 0, 0);
    gr_css_put_plan(v, 0, &p);
    CHECK(p.kind == 3 && p.ckind == -1);              /* the lobby's closed is the scene's 3 */
    door_bytes(v, 1, 12, 2, 1, 9, 1, 7);
    gr_css_put_plan(v, 2, &p);
    CHECK(p.kind == 1 && p.ckind == 12 && p.cpu_level == 9 && p.fixed_hc == 7 && !p.remote_held && !p.preview);
    gr_css_put_plan(v, 1, &p);
    CHECK(p.fixed_hc == 0);                           /* auto handicap moves no slider */
    door_bytes(v, 0, 12, 2, 1, 3, 2, 0);
    gr_css_put_plan(v, 0, &p);
    CHECK(p.ckind == -1 && p.preview && p.preview_fighter == 12);
    door_bytes(v, 0, 0, 0, 1, 3, 3, 0);
    gr_css_put_plan(v, 0, &p);
    CHECK(p.remote_held && p.ckind == -1);
}

static int sync(GrCssSync *s, int applied_ok, int mine, int elsewhere, int watching_, const GrByte *now,
                const GrByte *view, int *spare, int *why)
{
    GrCssSyncIn in;
    in.applied_ok = applied_ok;
    in.mine = mine;
    in.owned_elsewhere = elsewhere;
    in.watching = watching_;
    in.now = now;
    in.view = view;
    return gr_css_sync_door(s, &in, spare, why);
}

static void door_sync(void)
{
    GrCssSync s;
    GrByte now[GR_DOOR_BYTES], view[GR_DOOR_BYTES];
    int spare, why, act, i;

    /* joining opens no door: my door open in this console's VS data (from an earlier session), closed in the lobby */
    memset(&s, 0, sizeof s);
    door_bytes(now, 0, 9, 1, 0, 3, 1, 0);
    door_bytes(view, 2, 0, 0, 0, 3, 0, 0);
    act = sync(&s, 0, 1, 0, 0, now, view, &spare, &why);
    CHECK(act == (GR_SYNC_PUT | GR_SYNC_REREAD) && why == GR_SYNC_QUIET);
    /* ... the same when the lobby holds my door Human but this one holds nothing chosen and no tag */
    memset(&s, 0, sizeof s);
    door_bytes(now, 0, 0, 0, 0, 3, 0, 0);
    door_bytes(view, 0, 9, 1, 0, 3, 1, 0);
    CHECK(sync(&s, 0, 1, 0, 0, now, view, &spare, &why) == (GR_SYNC_PUT | GR_SYNC_REREAD));
    /* back from name entry with a new fighter on my Human door: mine is sent (no spare: nothing applied before) */
    memset(&s, 0, sizeof s);
    door_bytes(now, 0, 9, 1, 0, 3, 1, 0);
    door_bytes(view, 0, 4, 0, 0, 3, 1, 0);
    act = sync(&s, 0, 1, 0, 0, now, view, &spare, &why);
    CHECK(act == GR_SYNC_SEND && spare == 0 && s.pending == 12 && why == GR_SYNC_QUIET);
    /* another door at the first build: only taken as applied */
    memset(&s, 0, sizeof s);
    CHECK(sync(&s, 0, 0, 1, 0, now, view, &spare, &why) == 0 && memcmp(s.applied, now, GR_DOOR_BYTES) == 0);

    /* a local edit: sent with the kind it was made from, then held 12 frames so the lobby's old view does not undo it */
    memset(&s, 0, sizeof s);
    door_bytes(s.applied, 1, 9, 0, 0, 3, 1, 0);
    door_bytes(now, 1, 9, 0, 0, 5, 1, 0);           /* the CPU level slider moved */
    door_bytes(view, 1, 9, 0, 0, 3, 1, 0);
    act = sync(&s, 1, 0, 0, 0, now, view, &spare, &why);
    CHECK(act == GR_SYNC_SEND && spare == 2 && why == GR_SYNC_SENT && s.pending == 12 &&
          memcmp(s.applied, now, GR_DOOR_BYTES) == 0);
    for (i = 0; i < 12; i++) {
        CHECK(sync(&s, 1, 0, 0, 0, now, view, &spare, &why) == 0);
    }
    /* the lobby took it: nothing to do; another console then moves it: applied here */
    door_bytes(view, 1, 9, 0, 0, 5, 1, 0);
    CHECK(sync(&s, 1, 0, 0, 0, now, view, &spare, &why) == 0 && s.last_view_ok);
    door_bytes(view, 1, 9, 0, 0, 8, 1, 0);
    act = sync(&s, 1, 0, 0, 0, now, view, &spare, &why);
    CHECK(act == (GR_SYNC_PUT | GR_SYNC_REREAD) && why == GR_SYNC_APPLIED);
    /* the same view again is not applied twice */
    door_bytes(now, 1, 9, 0, 0, 8, 1, 0);
    memcpy(s.applied, now, GR_DOOR_BYTES);
    CHECK(sync(&s, 1, 0, 0, 0, now, view, &spare, &why) == 0);
    /* the handicap alone differs: applied */
    door_bytes(view, 1, 9, 0, 0, 8, 1, 4);
    CHECK(sync(&s, 1, 0, 0, 0, now, view, &spare, &why) == (GR_SYNC_PUT | GR_SYNC_REREAD));
    /* a mirrored hover (chosen 2): shown, and taken as applied without reading back */
    memset(&s, 0, sizeof s);
    door_bytes(now, 0, 0, 0, 0, 3, 0, 0);
    memcpy(s.applied, now, GR_DOOR_BYTES);
    door_bytes(view, 0, 14, 1, 0, 3, 2, 0);
    CHECK(sync(&s, 1, 0, 1, 0, now, view, &spare, &why) == GR_SYNC_PUT && memcmp(s.applied, view, GR_DOOR_BYTES) == 0);

    /* another console's Human door: its fighter changed here without it (this scene's doing): restored, never sent */
    memset(&s, 0, sizeof s);
    door_bytes(s.applied, 0, 9, 1, 0, 3, 1, 0);
    door_bytes(now, 0, 11, 1, 0, 3, 1, 0);
    door_bytes(view, 0, 9, 1, 0, 3, 1, 0);
    CHECK(sync(&s, 1, 0, 1, 0, now, view, &spare, &why) == (GR_SYNC_PUT | GR_SYNC_REREAD) && why == GR_SYNC_RESTORED);
    /* ... but Human / CPU / closed and the team are anyone's: sent */
    door_bytes(now, 1, 9, 1, 0, 3, 1, 0);
    CHECK(sync(&s, 1, 0, 1, 0, now, view, &spare, &why) == GR_SYNC_SEND && spare == 1);
    door_bytes(s.applied, 0, 9, 1, 0, 3, 1, 0);
    door_bytes(now, 0, 9, 1, 2, 3, 1, 0);
    CHECK(sync(&s, 1, 0, 1, 0, now, view, &spare, &why) == GR_SYNC_SEND);
    /* watching a match: any change is restored, even a team */
    door_bytes(s.applied, 0, 9, 1, 0, 3, 1, 0);
    door_bytes(now, 0, 9, 1, 2, 3, 1, 0);
    CHECK(sync(&s, 1, 1, 0, 1, now, view, &spare, &why) == (GR_SYNC_PUT | GR_SYNC_REREAD) && why == GR_SYNC_RESTORED);
    /* watching, my door at the first build: the lobby's (never sent) */
    memset(&s, 0, sizeof s);
    door_bytes(now, 0, 9, 1, 0, 3, 1, 0);
    door_bytes(view, 0, 4, 1, 0, 3, 1, 0);
    CHECK(sync(&s, 0, 1, 0, 1, now, view, &spare, &why) == (GR_SYNC_PUT | GR_SYNC_REREAD));

    /* the host's handicap mode changed: the new reading is taken as applied (not an edit) and the lobby's shown again */
    memset(&s, 0, sizeof s);
    s.last_view_ok = 1;
    door_bytes(now, 1, 9, 0, 0, 3, 1, 5);
    gr_css_sync_hc_mode(&s, now);
    CHECK(s.applied[14] == 5 && !s.last_view_ok);

    /* the door sent: its first 14 bytes, the handicap and the spare */
    {
        MpGroupDoor d;
        door_bytes(now, 1, 9, 2, 1, 6, 1, 3);
        now[6] = 'A';
        gr_css_door_to_send(now, 2, &d);
        CHECK(d.kind == 1 && d.fighter == 9 && d.color == 2 && d.team == 1 && d.level == 6 && d.chosen == 1 &&
              d.tag[0] == 'A' && d.handicap == 3 && d.spare == 2);
    }
}

static void handicap(void)
{
    /* outside a lobby, vanilla's; handicap not on auto: 0 */
    CHECK(gr_css_auto_hc_from(0, 1, 1, 0, 1, 0) == GR_HC_VANILLA);
    CHECK(gr_css_auto_hc_from(1, 4, 1, 0, 1, 0) == GR_HC_VANILLA);
    CHECK(gr_css_auto_hc_from(1, 1, 2, 0, 1, 0) == GR_HC_ZERO);
    /* my own Human door from my save; a CPU from the host's; anything else as the lobby showed it */
    CHECK(gr_css_auto_hc_from(1, 1, 1, 0, 1, 0) == GR_HC_SAVE);
    CHECK(gr_css_auto_hc_from(1, 2, 1, 0, 1, 0) == GR_HC_SHOWN);
    CHECK(gr_css_auto_hc_from(1, 2, 1, 1, 1, 1) == GR_HC_SAVE);
    CHECK(gr_css_auto_hc_from(1, 2, 1, 1, 1, 0) == GR_HC_SHOWN);
    /* a door's handicap: 0 closed or off, else 1..9 */
    CHECK(gr_css_door_hc(0, 1, 6) == 6 && gr_css_door_hc(2, 1, 6) == 0 && gr_css_door_hc(0, 0, 6) == 0);
    CHECK(gr_css_door_hc(1, 2, 0) == 0 && gr_css_door_hc(1, 2, 10) == 0 && gr_css_door_hc(1, 2, 9) == 9);
}

static void rules_and_stars(void)
{
    GrOwnRules own;
    MpGroupRules r, r2;
    GrByte ko[4] = { 1, 2, 3, 4 }, lob[4] = { 1, 5, 3, 0 };
    memset(&own, 0, sizeof own);
    own.mode = 1; own.stock_count = 4; own.time_limit = 8; own.stock_time_limit = 6; own.handicap = 2;
    own.stage_sel = 3; own.friendly_fire = 1; own.teams = 1; own.item_freq = 255;
    gr_css_rules_to_lobby(&own, &r);
    CHECK(r.time_mode == 0 && r.stocks == 4 && r.minutes == 6 && r.items == 0 && r.teams == 1 && r.friendly_fire == 1 &&
          r.mode == 1 && r.handicap == 2 && r.stage_sel == 3 && !r.melee_valid);
    own.mode = 0; own.item_freq = 2;
    gr_css_rules_to_lobby(&own, &r);
    CHECK(r.time_mode == 1 && r.minutes == 8 && r.items == 3);
    /* the banner's key changes with the rules; only a new handicap mode moves the sliders */
    r2 = r;
    r2.stock_count = 2;
    CHECK(gr_css_rules_key(&r) != gr_css_rules_key(&r2));
    CHECK(!gr_css_rules_relayout(gr_css_rules_key(&r), gr_css_rules_key(&r2)));
    r2 = r;
    r2.handicap = 1;
    CHECK(gr_css_rules_relayout(gr_css_rules_key(&r), gr_css_rules_key(&r2)));
    CHECK(!gr_css_rules_relayout(-1, gr_css_rules_key(&r2)));   /* the first drawing */
    /* the stars follow the lobby's; not while the host's results are up or a sub-scene is open */
    CHECK(gr_css_stars(ko, lob, 3, 0) == 0 && ko[1] == 2);
    CHECK(gr_css_stars(ko, lob, 0, 5) == 0);
    CHECK(gr_css_stars(ko, lob, 0, 0) == 0xA && memcmp(ko, lob, 4) == 0);
}

int main(void)
{
    pads_and_switches();
    watching();
    ready_and_start();
    doors_in_use();
    busy();
    read_and_view();
    put_plan();
    door_sync();
    handicap();
    rules_and_stars();
    printf("group_css: %s\n", failures ? "FAIL" : "ok");
    return failures != 0;
}
