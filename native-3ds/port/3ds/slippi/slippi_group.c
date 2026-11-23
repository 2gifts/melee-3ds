/* Melee -> Solo / Group (see slippi_group.h). Touches only set a request; the engine's once-per-frame poll carries it
 * out, so the session calls (which can block a moment on UDS) never run on the bottom screen's thread. */
#include <stdio.h>
#include <string.h>
#include "slippi_internal.h"
#include "slippi_group.h"

static int lost, reopen;            /* the session ended under us; reopen the Host/Join page on the VS menu */
static char lost_note[64];
static int fresh_menu;              /* we just came back from a lobby: the VS menu must take input again */

int slippi_group_force;                     /* slippi_net.c reads it at init: a lobby session whatever config.ini says */

enum { REQ_NONE, REQ_SOLO, REQ_GROUP, REQ_HOST, REQ_JOIN, REQ_BACK };

static SlippiGroupView view;
static volatile int req, req_arg;
static int session;                          /* a group session is open */
static int returning;
static int enabled = -1, top_menu;

int slippi_group_enabled(void)
{
    if (enabled < 0) {
        slippi_config cfg;
        char path[256];
        slippi_config_defaults(&cfg);
        snprintf(path, sizeof(path), "%s/config.ini", sp_data_dir());
        enabled = slippi_config_load(&cfg, path) == 0 && cfg.local_group;
        top_menu = enabled && cfg.local_group_menu;
    }
    return enabled;
}

int slippi_group_menu(void)
{
    slippi_group_enabled();
    return top_menu;
}

const SlippiGroupView *slippi_group_view(void) { return &view; }
int slippi_group_waiting(void) { return view.page != GROUP_PAGE_NONE; }
int slippi_group_active(void) { return session; }

int slippi_group_returning(void)
{
    int r = returning;
    returning = 0;
    return r;
}

static void set_error(const char *text)
{
    snprintf(view.error, sizeof(view.error), "%s", text ? text : "");
    ++view.serial;
}

#ifdef __3DS__
extern void mp_native_slippi_unlimited(void);   /* a development build would stop after its frame limit */
#else
static void mp_native_slippi_unlimited(void) {}
#endif

static void reset_view(void)
{
    unsigned serial = view.serial;      /* keeps counting up, so the bottom screen sees a new page and redraws */
    memset(&view, 0, sizeof(view));
    view.serial = serial + 1;
}

int slippi_group_open(void)
{
    if (!slippi_group_enabled()) return 0;
    mp_native_slippi_unlimited();
    reset_view();
    view.page = GROUP_PAGE_ASK;
    ++view.serial;
    req = REQ_NONE;
    sp_log("group: the Solo/Group page is up");
    sp_log_drain();
    return 1;
}

int slippi_group_pick(int group)
{
    if (group) slippi_group_pick_group();
    else slippi_group_pick_solo();
    return 0;
}
void slippi_group_pick_solo(void) { if (view.page == GROUP_PAGE_ASK) req = REQ_SOLO; }
void slippi_group_pick_group(void) { if (view.page == GROUP_PAGE_ASK) req = REQ_GROUP; }
void slippi_group_host(void) { if (view.page == GROUP_PAGE_LIST && !view.busy) req = REQ_HOST; }
void slippi_group_join(int row)
{
    if (view.page == GROUP_PAGE_LIST && !view.busy && row >= 0 && row < view.count && row < GROUP_ROWS) {
        req_arg = row;
        req = REQ_JOIN;
    }
}
void slippi_group_back(void) { req = REQ_BACK; }

/* Start (or restart) the session as a browser or a host. 0 ok. */
static int open_session(const char *role)
{
    int rc;
    slippi_group_force = 1;
    rc = slippi_net_start(role);
    if (rc != 0) {
        char e[96];
        snprintf(e, sizeof(e), "%s", slippi_net_error());
        if (!e[0]) snprintf(e, sizeof(e), "Local wireless is unavailable");
        set_error(e);
        slippi_net_stop();
        session = 0;
        slippi_group_force = 0;
        return -1;
    }
    session = 1;
    return 0;
}

/* The games nearby, from the module's scan. */
static void refresh_rows(void)
{
    int n = slippi_local_scan_count(), i, changed = 0;
    SlippiGroupRow rows[GROUP_ROWS];
    memset(rows, 0, sizeof(rows));
    if (n > GROUP_ROWS) n = GROUP_ROWS;
    for (i = 0; i < n; i++) {
        if (slippi_local_scan_get(i, rows[i].host, sizeof(rows[i].host), &rows[i].players, &rows[i].lobby) != 0) {
            n = i;
            break;
        }
    }
    if (n != view.count || memcmp(rows, view.row, sizeof(rows)) != 0) changed = 1;
    if (changed) {
        view.count = n;
        memcpy(view.row, rows, sizeof(rows));
        ++view.serial;
    }
}

int slippi_group_poll(int back)
{
    int r = req;
    if (view.page == GROUP_PAGE_NONE) return 0;
    if (back) r = REQ_BACK;
    req = REQ_NONE;
    switch (r) {
    case REQ_SOLO:
        sp_log("group: Solo");
        sp_log_drain();
        view.page = GROUP_PAGE_NONE;
        ++view.serial;
        return 1;
    case REQ_GROUP:
        set_error("");
        sp_log("group: Group, looking for games");
        if (open_session("@join") == 0) {
            if (lost_note[0]) { set_error(lost_note); lost_note[0] = 0; }
            view.page = GROUP_PAGE_LIST;
            view.count = 0;
            ++view.serial;
        }
        break;
    case REQ_HOST:
        view.busy = 1;
        set_error("");
        sp_log("group: Host");
        if (open_session("@host") == 0) {
            sp_log("group: hosting, entering the lobby");
            sp_log_drain();
            view.busy = 0;
            view.page = GROUP_PAGE_NONE;
            ++view.serial;
            return 2;
        }
        view.busy = 0;
        view.page = GROUP_PAGE_ASK;
        ++view.serial;
        break;
    case REQ_JOIN:
        view.busy = 1;
        ++view.serial;
        if (slippi_local_join(req_arg) == 0) {
            view.busy = 0;
            view.page = GROUP_PAGE_NONE;
            ++view.serial;
            return 2;
        }
        view.busy = 0;
        set_error("Could not join that game");
        break;
    case REQ_BACK:
        sp_log("group: Back from the %s page", view.page == GROUP_PAGE_LIST ? "Group" : "Solo/Group");
        sp_log_drain();
        if (view.page == GROUP_PAGE_LIST) {
            slippi_net_stop();
            session = 0;
            slippi_group_force = 0;
            view.page = GROUP_PAGE_ASK;
            view.count = 0;
            set_error("");
            ++view.serial;
        } else {
            view.page = GROUP_PAGE_NONE;
            ++view.serial;
            return -1;
        }
        break;
    default:
        break;
    }
    if (view.page == GROUP_PAGE_LIST) {
        slippi_net_poll();
        if (slippi_net_status() == 4 /* failed */) {
            set_error(slippi_net_error());
            slippi_net_stop();
            session = 0;
            slippi_group_force = 0;
            view.page = GROUP_PAGE_ASK;
            ++view.serial;
        } else {
            refresh_rows();
        }
    }
    return 0;
}

static unsigned toast_present;
static int toast_known;
static char toast_names[LOBBY_SLOTS][32], toast_text[48];
static uint64_t toast_until;
static int toast_port;
static int dialog, dialog_result;   /* "End the session?": up, and the answer (1 yes, 2 no) until read */

int slippi_group_confirm_leave(void)
{
    int others;
    if (!session) return 1;
    sp_lock();
    others = g_slippi.match.is_host && __builtin_popcount(slippi_local_present()) > 1;
    sp_unlock();
    if (!others) return 1;
    if (dialog_result == 1) { dialog_result = 0; dialog = 0; return 1; }
    if (!dialog) { dialog = 1; dialog_result = 0; ++view.serial; }
    return 0;
}

int slippi_group_confirm_state(void)
{
    int r = dialog_result;
    if (r == 2) dialog_result = 0;
    return r;
}

int slippi_group_dialog(void) { return dialog; }

void slippi_group_dialog_answer(int yes)
{
    if (!dialog) return;
    dialog_result = yes ? 1 : 2;
    if (!yes) dialog = 0;
    ++view.serial;
}

int slippi_group_lost(void)
{
    int st;
    if (lost) return 1;
    if (!session) return 0;
    st = slippi_net_status();
    if (st != 4 && st != 5) return 0;
    lost = reopen = 1;
    snprintf(lost_note, sizeof(lost_note), "%s", st == 5 ? "The session ended" : "The connection was lost");
    sp_log("group: %s", lost_note);
    return 1;
}

int slippi_group_fresh(void)
{
    int r = fresh_menu;
    fresh_menu = 0;
    return r;
}

int slippi_group_reopen(void)
{
    if (!reopen) return 0;
    reopen = 0;
    if (!slippi_group_open()) return 0;
    slippi_group_pick_group();       /* straight to the Host / Join page */
    return 1;
}

int slippi_group_toast_port(void) { return toast_port; }

int slippi_group_toast(char *out, int len)
{
    SlippiGroupStrip s;
    if (len > 0) out[0] = 0;
    if (!slippi_group_strip(&s) || !s.toast[0]) return 0;
    snprintf(out, (size_t)len, "%s", s.toast);
    uint64_t now = sp_time_us();
    return toast_until > now ? (int)((toast_until - now) / 1000) : 0;
}

void slippi_group_leave(void)
{
    dialog = dialog_result = 0;
    lost = 0;
    fresh_menu = 1;
    sp_log("group: left the lobby");
    sp_log_drain();
    if (session) {
        slippi_net_stop();
        session = 0;
    }
    slippi_group_force = 0;
    toast_known = 0;
    toast_until = 0;
    returning = 1;
    reset_view();
    ++view.serial;
}

/* A name tag's text (the game's Shift-JIS-like encoding) as plain text; only what the text can say in ASCII. */
static void tag_text(const uint8_t *t, char *out, int n)
{
    int o = 0;
    for (int i = 0; i < LOBBY_TAG_BYTES && t[i] && o < n - 1; i++) {
        unsigned c = t[i];
        if (c >= 0x20 && c < 0x7F) { out[o++] = (char)c; continue; }
        if ((c == 0x81 || c == 0x82) && i + 1 < LOBBY_TAG_BYTES) {
            unsigned d = t[++i];
            if (c == 0x82 && d >= 0x4F && d <= 0x58) out[o++] = (char)('0' + (d - 0x4F));
            else if (c == 0x82 && d >= 0x60 && d <= 0x79) out[o++] = (char)('A' + (d - 0x60));
            else if (c == 0x82 && d >= 0x81 && d <= 0x9A) out[o++] = (char)('A' + (d - 0x81));   /* the strip's font is capitals */
            else if (c == 0x81 && d == 0x40) out[o++] = ' ';
            else out[o++] = '?';
            continue;
        }
        out[o++] = '?';
    }
    out[o] = 0;
}

/* Join and leave toasts: remember who was here, show the change for a few seconds. */

static void toast_update(unsigned present)
{
    uint64_t now = sp_time_us();
    if (!toast_known) {
        toast_known = 1;
        toast_present = present;
    } else if (present != toast_present) {
        for (int i = 0; i < LOBBY_SLOTS; i++) {
            unsigned bit = 1u << i;
            if (!((present ^ toast_present) & bit)) continue;
            snprintf(toast_text, sizeof(toast_text), "%.24s %s", toast_names[i][0] ? toast_names[i] : "A 3DS",
                     (present & bit) ? "JOINED" : "LEFT");
            toast_until = now + 4000000u;
            toast_port = i;
        }
        toast_present = present;
    }
    for (int i = 0; i < LOBBY_SLOTS; i++) {
        if (!(present >> i & 1)) continue;
        const char *n = slippi_local_player_name(i);
        if (n && n[0]) snprintf(toast_names[i], sizeof(toast_names[i]), "%s", n);
    }
}

int slippi_group_strip(SlippiGroupStrip *out)
{
    lobby_state st;
    unsigned present;
    int i;
    memset(out, 0, sizeof(*out));
    if (!session) return 0;
    sp_lock();
    if (!slippi_local_lobby_view(&st)) {
        sp_unlock();
        return 0;
    }
    present = slippi_local_present();
    toast_update(present);
    if (sp_time_us() < toast_until) snprintf(out->toast, sizeof(out->toast), "%s", toast_text);
    out->active = 1;
    out->me = slippi_net_local_index();
    out->phase = st.phase;
    out->rules = st.rules;
    for (i = 0; i < LOBBY_SLOTS; i++) {
        out->port[i].door = st.slot[i];
        out->port[i].present = (int)(present >> i & 1);
        if (out->port[i].present) {
            out->port[i].signal = slippi_local_signal(i);
            snprintf(out->port[i].name, sizeof(out->port[i].name), "%s", slippi_local_player_name(i));
            if (st.slot[i].kind == LOBBY_HUMAN && st.slot[i].tag[0]) {      /* a chosen name tag replaces the 3DS name here */
                char t[24];
                tag_text(st.slot[i].tag, t, sizeof(t));
                if (strspn(t, "? ") != strlen(t)) snprintf(out->port[i].name, sizeof(out->port[i].name), "%s", t);
            }
        }
    }
    sp_unlock();
    return 1;
}

unsigned slippi_group_serial(void)
{
    SlippiGroupStrip s;
    unsigned h = view.serial * 2654435761u;
    const unsigned char *b = (const unsigned char *)&s;
    size_t i;
    if (!slippi_group_strip(&s)) return h;
    for (i = 0; i < sizeof(s); i++) h = h * 31 + b[i];
    return h;
}

void slippi_group_note_touch(unsigned x, unsigned y)
{
    sp_log("group: tap at %u,%u (page %d)", x, y, (int)view.page);
}
