/* Melee -> Solo / Group (see group_menu.h). Touches only set a request; the engine's once-per-frame poll carries it
 * out, so the session calls (which can block a moment on UDS) never run on the bottom screen's thread. */
#include <stdio.h>
#include <string.h>
#include "group_menu.h"
#include "slippi_plat.h"   /* the port's platform layer: log, time */
#ifdef __3DS__
#include <3ds.h>           /* aptSetHomeAllowed, aptSetSleepAllowed, aptCheckHomePressRejected */
#endif

static int lost, reopen;            /* the session ended under us; reopen the Host/Join page on the VS menu */
static char lost_note[96];         /* the size of GroupView.error */
static int fresh_menu;              /* we just came back from a lobby: the VS menu must take input again */

enum { REQ_NONE, REQ_SOLO, REQ_GROUP, REQ_HOST, REQ_JOIN, REQ_BACK };

static GroupView view;
static volatile int req;
static volatile uint32_t req_key;
static int session;                          /* a group session is open (from this menu) */
static int returning;
static int enabled = -1, top_menu;

int group_enabled(void)
{
    if (enabled < 0) {
        group_config cfg;
        group_config_load(&cfg);
        enabled = cfg.menu != 0;
        top_menu = enabled && cfg.top_menu;
    }
    return enabled;
}

int group_menu(void)
{
    group_enabled();
    return top_menu;
}

const GroupView *group_view(void) { return &view; }
int group_waiting(void) { return view.page != GROUP_PAGE_NONE; }
int group_listing(void) { return view.page == GROUP_PAGE_LIST; }
int group_active(void) { return session; }

int group_returning(void)
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
    view.pages = 1;
}

/* The pages for view.count games; the page shown moves back when its games are gone. */
static void fix_pages(void)
{
    int count = view.count < GROUP_KEEP ? view.count : GROUP_KEEP;
    view.pages = count > 0 ? (count + GROUP_ROWS - 1) / GROUP_ROWS : 1;
    if (view.list_page >= view.pages) view.list_page = view.pages - 1;
    if (view.list_page < 0) view.list_page = 0;
}

void group_list_turn(int delta)
{
    if (view.page != GROUP_PAGE_LIST || view.busy) return;
    int to = view.list_page + delta;
    if (to < 0 || to >= view.pages) return;
    view.list_page = to;
    ++view.serial;
    sp_log("group: list page %d of %d", to + 1, view.pages);
}

int group_open(void)
{
    if (!group_enabled()) return 0;
    mp_native_slippi_unlimited();
    reset_view();
    view.page = GROUP_PAGE_ASK;
    ++view.serial;
    req = REQ_NONE;
    sp_log("group: the Solo/Group page is up");
    sp_log_drain();
    return 1;
}

int group_pick(int group)
{
    if (group) group_pick_group();
    else group_pick_solo();
    return 0;
}
void group_pick_solo(void) { if (view.page == GROUP_PAGE_ASK) req = REQ_SOLO; }
void group_pick_group(void) { if (view.page == GROUP_PAGE_ASK) req = REQ_GROUP; }
void group_host(void) { if (view.page == GROUP_PAGE_LIST && !view.busy) req = REQ_HOST; }
void group_join_row(int row)
{
    if (view.page != GROUP_PAGE_LIST || view.busy || row < 0 || row >= GROUP_ROWS) return;
    int i = view.list_page * GROUP_ROWS + row;
    if (i >= view.count || i >= GROUP_KEEP) return;
    if (view.row[i].max > 0 && view.row[i].players >= view.row[i].max) {
        set_error("That game is full (4 consoles)");
        return;
    }
    req_key = view.row[i].key;         /* the game shown in that row, even if the list moves before the poll */
    req = REQ_JOIN;
}
void group_back(void) { req = REQ_BACK; }

/* Start (or restart) the session as a browser or a host. 0 ok. */
static int open_session(int host)
{
    if (group_session_start(host, 0) != 0) {
        char e[96];
        snprintf(e, sizeof(e), "%s", group_net_error());
        if (!e[0]) snprintf(e, sizeof(e), "Local wireless is unavailable");
        set_error(e);
        group_session_stop();
        session = 0;
        return -1;
    }
    session = 1;
    return 0;
}

static void close_session(void)
{
    group_session_stop();
    session = 0;
}

/* The games nearby, from the module's scan. */
static void refresh_rows(void)
{
    static group_game rows[GROUP_KEEP];  /* the engine thread only; too big for its stack */
    memset(rows, 0, sizeof(rows));
    int n = group_scan(rows, GROUP_KEEP);
    if (n != view.count || memcmp(rows, view.row, sizeof(rows)) != 0) {
        int moved = n != view.count;
        for (int i = 0; i < n && !moved; ++i) moved = rows[i].key != view.row[i].key || rows[i].has_mii != view.row[i].has_mii;
        if (n != view.count) sp_log("group: %d game%s listed", n, n == 1 ? "" : "s");
        if (moved) {                   /* the order and which hosts sent a Mii (tests read it) */
            char line[400];
            int len = snprintf(line, sizeof(line), "group: list order:");
            for (int i = 0; i < n && len < (int)sizeof(line) - 40; ++i)
                len += snprintf(line + len, sizeof(line) - (size_t)len, " %.31s%s", rows[i].host, rows[i].has_mii ? "(Mii)" : "(-)");
            sp_log("%s", line);
        }
        view.count = n;
        memcpy(view.row, rows, sizeof(rows));
        fix_pages();
        ++view.serial;
    }
}

int group_poll(int back)
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
        if (open_session(0) == 0) {
            if (lost_note[0]) { set_error(lost_note); lost_note[0] = 0; }   /* only if the engine did not show it */
            view.page = GROUP_PAGE_LIST;
            view.count = 0;
            view.list_page = 0;
            view.my_has_mii = group_my_mii(view.my_mii);
            view.my_mii_crc = view.my_has_mii ? (uint16_t)(view.my_mii[LOBBY_MII_BYTES - 2] << 8 | view.my_mii[LOBBY_MII_BYTES - 1]) : 0;
            fix_pages();
            ++view.serial;
        }
        break;
    case REQ_HOST:
        view.busy = 1;
        set_error("");
        sp_log("group: Host");
        if (open_session(1) == 0) {
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
        if (group_join(req_key) == 0) {
            view.busy = 0;
            view.page = GROUP_PAGE_NONE;
            ++view.serial;
            return 2;
        }
        view.busy = 0;
        set_error(group_net_error()[0] ? group_net_error() : "Could not join that game");
        break;
    case REQ_BACK:
        sp_log("group: Back from the %s page", view.page == GROUP_PAGE_LIST ? "Group" : "Solo/Group");
        sp_log_drain();
        if (view.page == GROUP_PAGE_LIST) {
            close_session();
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
        group_net_poll();
        if (group_net_status() == 4 /* failed */) {
            set_error(group_net_error());
            close_session();
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

int group_confirm_leave(void)
{
    if (!session) return 1;
    int others = group_is_host() && __builtin_popcount(group_present()) > 1;
    if (!others) return 1;
    if (dialog_result == 1) { dialog_result = 0; dialog = 0; return 1; }
    if (!dialog) { dialog = 1; dialog_result = 0; ++view.serial; }
    return 0;
}

int group_confirm_state(void)
{
    int r = dialog_result;
    if (r == 2) dialog_result = 0;
    return r;
}

int group_dialog(void) { return dialog; }

void group_dialog_answer(int yes)
{
    if (!dialog) return;
    dialog_result = yes ? 1 : 2;
    if (!yes) dialog = 0;
    ++view.serial;
}

int group_lost(void)
{
    int st;
    if (lost) return 1;
    if (!session) return 0;
    st = group_net_status();
    if (st != 4 && st != 5) return 0;
    lost = reopen = 1;
    const char *why = group_net_error();   /* the specific reason: the host left, stopped responding, ... */
    snprintf(lost_note, sizeof(lost_note), "%s", why[0] ? why : st == 5 ? "The session ended" : "The connection was lost");
    sp_log("group: %s", lost_note);
    return 1;
}

int group_lost_note(char *out, int len)
{
    if (!lost_note[0]) return 0;
    snprintf(out, (size_t)len, "%s", lost_note);
    lost_note[0] = 0;
    return 1;
}

int group_fresh(void)
{
    int r = fresh_menu;
    fresh_menu = 0;
    return r;
}

int group_reopen(void)
{
    if (!reopen) return 0;
    reopen = 0;
    if (!group_open()) return 0;
    group_pick_group();       /* straight to the Host / Join page */
    return 1;
}

int group_toast_port(void) { return toast_port; }

int group_toast(char *out, int len)
{
    GroupStrip s;
    if (len > 0) out[0] = 0;
    if (!group_strip(&s) || !s.toast[0]) return 0;
    snprintf(out, (size_t)len, "%s", s.toast);
    uint64_t now = sp_time_us();
    return toast_until > now ? (int)((toast_until - now) / 1000) : 0;
}

void group_leave(void)
{
    dialog = dialog_result = 0;
    lost = 0;
    fresh_menu = 1;
    sp_log("group: left the lobby");
    sp_log_drain();
    if (session) close_session();
    toast_known = 0;
    toast_until = 0;
    returning = 1;
    reset_view();
    ++view.serial;
}

/* Join and leave toasts: remember who was here, show the change for a few seconds. */
static void toast_update(unsigned present, char names[LOBBY_SLOTS][32])
{
    uint64_t now = sp_time_us();
    if (!toast_known) {
        toast_known = 1;
        toast_present = present;
    } else if (present != toast_present) {
        for (int i = 0; i < LOBBY_SLOTS; i++) {
            unsigned bit = 1u << i;
            if (!((present ^ toast_present) & bit)) continue;
            snprintf(toast_text, sizeof(toast_text), "%s %s", toast_names[i][0] ? toast_names[i] : "A 3DS",
                     (present & bit) ? "JOINED" : "LEFT");   /* a name is at most 31 bytes: 38 in all */
            toast_until = now + 4000000u;
            toast_port = i;
        }
        toast_present = present;
    }
    for (int i = 0; i < LOBBY_SLOTS; i++)
        if ((present >> i & 1) && names[i][0]) snprintf(toast_names[i], sizeof(toast_names[i]), "%.31s", names[i]);
}

int group_strip(GroupStrip *out)
{
    lobby_state st;
    char names[LOBBY_SLOTS][32];
    memset(out, 0, sizeof(*out));
    if (!session || !group_lobby_view(&st)) return 0;
    unsigned present = group_present();
    for (int i = 0; i < LOBBY_SLOTS; i++) group_name(i, names[i], sizeof(names[i]));
    toast_update(present, names);
    if (sp_time_us() < toast_until) snprintf(out->toast, sizeof(out->toast), "%s", toast_text);
    out->active = 1;
    out->me = group_my_port();
    out->phase = st.phase;
    out->picker = st.phase == LOBBY_STAGE && st.sss_picker < LOBBY_SLOTS ? st.sss_picker : -1;
    out->watching = group_watching();
    {
        static int was;
        if (out->watching != was) {
            was = out->watching;
            sp_log("group: %s", was ? "watching a match in progress" : "no match in progress here any more");
        }
    }
    out->status = st.status;
    out->rules = st.rules;
    for (int i = 0; i < LOBBY_SLOTS; i++) {
        out->port[i].door = st.slot[i];
        out->port[i].busy_why = st.busy_why[i];
        out->port[i].present = (int)(present >> i & 1);
        if (out->port[i].present) {
            out->port[i].signal = group_signal(i);
            snprintf(out->port[i].name, sizeof(out->port[i].name), "%.31s", names[i]);   /* always the 3DS name: a tag only shows over the fighter in the match */
        }
        /* The match overview: a console that joined since (even on the port of one that left) is not in the match */
        if (st.gone >> i & 1) snprintf(out->port[i].player, sizeof(out->port[i].player), "%.31s", st.gone_name[i]);
        else if ((st.played >> i & 1) && out->port[i].present)
            snprintf(out->port[i].player, sizeof(out->port[i].player), "%.31s", names[i]);
    }
    return 1;
}

unsigned group_serial(void)
{
    GroupStrip s;
    unsigned h = view.serial * 2654435761u;
    const unsigned char *b = (const unsigned char *)&s;
    size_t i;
    h += (unsigned)group_home_note() * 0x9E3779B9u;   /* redraw when the HOME notice comes and goes */
    if (!group_strip(&s)) return h;
    for (i = 0; i < sizeof(s); i++) h = h * 31 + b[i];
    return h;
}

/* ---- HOME and sleep during a wireless session ----
 * Pressing HOME suspends the game, and lockstep waits for that console: after 2 s the host makes it a CPU, or ends the
 * session if it is the host. Closing the lid sleeps the console with the same result. So while a session is open
 * (browsing the games nearby, the lobby, the stage select, a match, its results, watching one), HOME is refused and the
 * console does not sleep (as Smash 3DS does in local play); a refused HOME press shows a notice on the bottom screen.
 * libctru's APT handler runs on its own thread, so this holds even while the engine blocks in a lockstep wait. The
 * power button still works. */
static int policy;                       /* 1: HOME and sleep are off (a wireless session); 0 at boot, so Solo never logs */
static uint64_t home_note_until;
volatile unsigned group_test_home_reject;   /* tests: a rejected HOME press, written through the GDB stub */

int group_in_wireless(void)
{
    int st = group_net_status();
    return session && (st == 1 || st == 3);   /* searching or connected; not failed (4) or ended (5) */
}

void group_system_frame(void)
{
    int on = group_in_wireless();
    if (on != policy) {
        policy = on;
#ifdef __3DS__
        aptSetHomeAllowed(!on);
        aptSetSleepAllowed(!on);
#endif
        sp_log("group: HOME and sleep %s", on ? "off (wireless session)" : "on");
    }
    int rejected = group_test_home_reject != 0;
#ifdef __3DS__
    if (on && aptCheckHomePressRejected()) rejected = 1;
#endif
    if (rejected) {
        group_test_home_reject = 0;
        home_note_until = sp_time_us() + 2000000ull;
        ++view.serial;
        sp_log("group: HOME pressed during wireless play");
    }
}

int group_home_note(void) { return home_note_until && sp_time_us() < home_note_until; }

void group_note_touch(unsigned x, unsigned y)
{
    sp_log("group: tap at %u,%u (page %d)", x, y, (int)view.page);
}
