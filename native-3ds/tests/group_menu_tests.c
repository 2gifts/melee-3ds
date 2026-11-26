/* Host tests for Melee -> Solo / Group (port/3ds/group/group_menu.c) over a fake session (below): the pages (Solo /
 * Group, the Group list, Host, Join, Back), a full game, a join or host that fails, the session failing under the list,
 * "End the session?", a lost session's note and the reopened Host / Join page, HOME and sleep during a session (off
 * while it is open, held while it opens, the refused-HOME notice), the join / leave toasts and the strip; and the menus
 * around the lobby (port/engine/group/menu_rules.c). Run by tools/test_group.py. The fake replaces group_session.c and
 * the platform's log and clock; HOME and sleep are checked through the menu's "group: HOME and sleep ..." lines, as
 * the APT calls build only for the 3DS. */
#include "group_menu.h"
#include <group_rules.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

extern volatile unsigned group_test_home_reject;   /* group_menu.c: a refused HOME press, for tests */

static int failures;
#define CHECK(c) do { if (!(c)) { printf("group_menu: FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)

/* ---- the fake session ---- */
static struct {
    int start_rc, starts, stops, last_host;
    int open, opening, link_up;
    int status;                 /* 0 idle, 1 searching, 3 connected, 4 failed, 5 ended */
    char error[96];
    group_game games[GROUP_KEEP];
    int ngames;
    int join_rc, join_result, joins;
    uint32_t join_key;
    int is_host, my_port, watching;
    unsigned present;
    int has_lobby;
    lobby_state lobby;
    char names[LOBBY_SLOTS][32];
    int has_mii;
    uint64_t now;
    char log[4096];
} F;

void group_config_load(group_config *cfg)
{
    memset(cfg, 0, sizeof *cfg);
    cfg->menu = 1;
    cfg->top_menu = 1;
}
int group_session_start(int host, int automatic)
{
    (void) automatic;
    F.starts++;
    F.last_host = host;
    if (F.start_rc) return -1;
    F.opening = 1;
    F.status = 1;
    return 0;
}
void group_session_stop(void)
{
    F.stops++;
    F.open = F.opening = 0;
    if (F.status == 1 || F.status == 3) F.status = 0;
}
int group_session_open(void) { return F.open || F.opening; }
int group_session_opening(void) { return F.opening; }
int group_session_busy(void) { return 0; }
int group_session_link_up(void) { return F.link_up; }
int group_my_mii(uint8_t *out)
{
    memset(out, 0x11, LOBBY_MII_BYTES);
    return F.has_mii;
}
int group_is_host(void) { return F.is_host; }
int group_my_port(void) { return F.my_port; }
int group_scan(group_game *out, int max)
{
    int n = F.ngames < max ? F.ngames : max;
    memcpy(out, F.games, sizeof(group_game) * (size_t) n);
    return n;
}
int group_join(uint32_t key)
{
    F.joins++;
    F.join_key = key;
    return F.join_rc;
}
int group_join_result(void)
{
    int r = F.join_result;
    F.join_result = 0;
    return r;
}
int group_lobby_view(lobby_state *out)
{
    if (!F.has_lobby) return 0;
    *out = F.lobby;
    return 1;
}
unsigned group_present(void) { return F.present; }
int group_watching(void) { return F.watching; }
int group_signal(int port) { (void) port; return 3; }
void group_name(int port, char *out, int len) { snprintf(out, (size_t) len, "%s", F.names[port]); }
int group_net_status(void) { return F.status; }
const char *group_net_error(void) { return F.error; }
void group_net_poll(void) {}
uint64_t sp_time_us(void) { return F.now; }
void sp_log(const char *fmt, ...)
{
    char line[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);
    if (strlen(F.log) + strlen(line) + 2 < sizeof F.log) {
        strcat(F.log, line);
        strcat(F.log, "\n");
    }
}
void sp_log_drain(void) {}

static int logged(const char *text) { return strstr(F.log, text) != NULL; }
static int count(const char *text)
{
    int n = 0;
    const char *p = F.log;
    while ((p = strstr(p, text)) != NULL) { n++; p += strlen(text); }
    return n;
}

static void game(int i, uint32_t key, const char *name, int players)
{
    memset(&F.games[i], 0, sizeof F.games[i]);
    F.games[i].key = key;
    snprintf(F.games[i].host, sizeof F.games[i].host, "%s", name);
    F.games[i].players = players;
    F.games[i].max = 4;
}

/* the session is up, as the link thread would have it */
static void link_opened(void)
{
    F.opening = 0;
    F.open = 1;
    F.link_up = 1;
    F.status = 1;
}

static void pages(void)
{
    const GroupView *v = group_view();
    CHECK(group_open() && v->page == GROUP_PAGE_ASK && group_waiting() && !group_listing());
    CHECK(group_poll(0) == 0);
    /* Host and Join are not on this page */
    group_host();
    group_join_row(0);
    CHECK(group_poll(0) == 0 && F.starts == 0 && F.joins == 0);
    /* Solo */
    group_pick(0);
    CHECK(group_poll(0) == 1 && v->page == GROUP_PAGE_NONE && logged("group: Solo"));
    CHECK(group_poll(0) == 0);                        /* nothing is up */
    /* Back (B) from the Solo / Group page cancels */
    group_open();
    CHECK(group_poll(1) == -1 && v->page == GROUP_PAGE_NONE);
}

static void the_list(void)
{
    const GroupView *v = group_view();
    int i;
    group_open();
    group_pick(1);
    CHECK(group_poll(0) == 0 && group_listing() && F.starts == 1 && F.last_host == 0 && group_active());
    /* the Host button's Mii only once the session is open */
    F.has_mii = 1;
    CHECK(group_poll(0) == 0 && !v->my_has_mii);
    link_opened();
    CHECK(group_poll(0) == 0 && v->my_has_mii && v->my_mii_crc == 0x1111);
    /* the games nearby: three to a page, the order logged */
    for (i = 0; i < 7; i++) game(i, 100u + (unsigned) i, "Host", 1);
    F.ngames = 7;
    group_poll(0);
    CHECK(v->count == 7 && v->pages == 3 && v->list_page == 0 && logged("group: 7 games listed") &&
          logged("group: list order: Host(-)"));
    group_list_turn(1);
    group_list_turn(1);
    group_list_turn(1);                               /* no fourth page */
    CHECK(v->list_page == 2);
    /* games gone: the page moves back */
    F.ngames = 2;
    group_poll(0);
    CHECK(v->count == 2 && v->pages == 1 && v->list_page == 0);
    /* a full game is refused at once */
    game(1, 201, "Full", 4);
    group_poll(0);
    group_join_row(1);
    CHECK(group_poll(0) == 0 && F.joins == 0 && !strcmp(v->error, "That game is full (4 consoles)"));
    group_join_row(2);                                /* no game in that row */
    CHECK(group_poll(0) == 0 && F.joins == 0);
    /* a join: busy until the link thread has joined */
    group_join_row(0);
    CHECK(group_poll(0) == 0 && F.joins == 1 && F.join_key == 100 && v->busy);
    group_host();                                     /* the page is busy: nothing else */
    group_join_row(0);
    CHECK(group_poll(0) == 0 && F.joins == 1);
    F.join_result = -1;
    snprintf(F.error, sizeof F.error, "That game is gone");
    CHECK(group_poll(0) == 0 && !v->busy && !strcmp(v->error, "That game is gone"));
    F.error[0] = 0;
    group_join_row(0);
    group_poll(0);
    F.join_result = 1;
    CHECK(group_poll(0) == 2 && v->page == GROUP_PAGE_NONE && group_active());
    group_leave();
    CHECK(!group_active() && group_returning() && !group_returning());
}

static void host_and_back(void)
{
    const GroupView *v = group_view();
    group_open();
    group_pick(1);
    group_poll(0);
    link_opened();
    group_poll(0);
    /* Host: a new session as host; busy until it is up */
    group_host();
    CHECK(group_poll(0) == 0 && F.last_host == 1 && v->busy && logged("group: Host"));
    CHECK(group_poll(0) == 0);                        /* still opening */
    link_opened();
    CHECK(group_poll(0) == 2 && logged("group: hosting, entering the lobby"));
    group_leave();
    /* Host failing to open: back to the Solo / Group page with why */
    group_open();
    group_pick(1);
    group_poll(0);
    link_opened();
    group_host();
    group_poll(0);
    F.opening = 0;
    F.status = 4;
    snprintf(F.error, sizeof F.error, "Please enable wireless and try again");
    CHECK(group_poll(0) == 0 && v->page == GROUP_PAGE_ASK && !strcmp(v->error, "Please enable wireless and try again") &&
          !group_active());
    F.error[0] = 0;
    F.status = 0;
    /* Back from the list closes the session and shows the Solo / Group page */
    group_pick(1);
    group_poll(0);
    link_opened();
    group_poll(0);
    {
        int stops = F.stops;
        group_back();
        CHECK(group_poll(0) == 0 && v->page == GROUP_PAGE_ASK && F.stops == stops + 1 && !group_active() &&
              logged("group: Back from the Group page"));
    }
    /* the session failing under the list (the link went away) */
    group_pick(1);
    group_poll(0);
    link_opened();
    F.status = 4;
    snprintf(F.error, sizeof F.error, "The link failed");
    CHECK(group_poll(0) == 0 && v->page == GROUP_PAGE_ASK && !strcmp(v->error, "The link failed"));
    F.error[0] = 0;
    F.status = 0;
    /* a session that cannot even be asked for */
    F.start_rc = 1;
    group_pick(1);
    CHECK(group_poll(0) == 0 && v->page == GROUP_PAGE_ASK && !strcmp(v->error, "Local wireless is unavailable"));
    F.start_rc = 0;
    group_poll(1);
}

static void leaving_and_lost(void)
{
    char note[96];
    /* in a lobby as a joiner, or a host alone: Back leaves at once */
    group_open();
    group_pick(1);
    group_poll(0);
    link_opened();
    F.join_result = 1;                                /* joined by the time the poll looks */
    group_join_row(0);
    CHECK(group_poll(0) == 2);
    F.is_host = 0;
    F.present = 0x3;
    CHECK(group_confirm_leave() == 1 && !group_dialog());
    F.is_host = 1;
    F.present = 0x1;
    CHECK(group_confirm_leave() == 1);
    /* a host with others here is asked; No keeps the session */
    F.present = 0x5;
    CHECK(group_confirm_leave() == 0 && group_dialog() && group_confirm_state() == 0);
    CHECK(group_confirm_leave() == 0);                /* asked once */
    group_dialog_answer(0);
    CHECK(!group_dialog() && group_confirm_state() == 2 && group_confirm_state() == 0);
    /* ... Yes leaves */
    CHECK(group_confirm_leave() == 0);
    group_dialog_answer(1);
    CHECK(group_confirm_state() == 1 && group_confirm_leave() == 1 && !group_dialog());
    group_dialog_answer(1);                           /* no dialog: nothing */
    CHECK(group_confirm_state() == 0);
    /* the host left: lost once, with the session's reason */
    F.is_host = 0;
    CHECK(!group_lost());
    F.status = 4;
    snprintf(F.error, sizeof F.error, "The host left");
    CHECK(group_lost() && group_lost() && logged("group: The host left"));
    CHECK(group_lost_note(note, sizeof note) && !strcmp(note, "The host left") && !group_lost_note(note, sizeof note));
    /* back on the VS menu: the Host / Join page again, which browses at once */
    group_leave();
    CHECK(!group_lost() && group_fresh() && !group_fresh());
    F.status = 0;
    F.error[0] = 0;
    CHECK(group_reopen() && group_view()->page == GROUP_PAGE_ASK && !group_reopen());
    CHECK(group_poll(0) == 0 && group_listing() && group_active());
    /* a lost session with no reason: by status; the note shows on the list when the engine did not show it */
    link_opened();
    group_poll(0);
    F.status = 5;
    CHECK(group_lost());
    group_leave();
    F.status = 0;
    group_reopen();
    group_poll(0);
    CHECK(!strcmp(group_view()->error, "The session ended"));
    link_opened();
    group_poll(0);
    F.status = 4;
    F.error[0] = 0;
    CHECK(group_lost());
    CHECK(group_lost_note(note, sizeof note) && !strcmp(note, "The connection was lost"));
    group_leave();
    F.status = 0;
    group_poll(1);
}

static void home_and_sleep(void)
{
    group_session_stop();
    F.link_up = 0;
    F.log[0] = 0;
    group_system_frame();
    CHECK(!logged("HOME and sleep"));                 /* Solo never logs */
    /* the list: off once the session opens, never while it opens (it may fail) */
    group_open();
    group_pick(1);
    group_poll(0);
    group_system_frame();
    CHECK(!logged("HOME and sleep off"));
    link_opened();
    group_system_frame();
    CHECK(count("group: HOME and sleep off (wireless session)") == 1);
    /* a refused HOME press shows the notice for 2 s */
    F.now = 1000000;
    group_test_home_reject = 1;
    group_system_frame();
    CHECK(logged("group: HOME pressed during wireless play") && group_home_note() && !group_test_home_reject);
    F.now = 2900000;
    CHECK(group_home_note());
    F.now = 3100000;
    CHECK(!group_home_note());
    /* Host from the list: a close and an open; never on in between */
    group_host();
    group_poll(0);
    F.link_up = 1;
    group_system_frame();
    link_opened();
    group_poll(0);
    group_system_frame();
    CHECK(!logged("HOME and sleep on"));
    /* leaving: on only once the link thread has closed UDS */
    group_leave();
    group_system_frame();
    CHECK(!logged("HOME and sleep on"));
    F.link_up = 0;
    group_system_frame();
    CHECK(count("group: HOME and sleep on") == 1);
    /* a failed session turns them on (status 4), the link closed */
    group_open();
    group_pick(1);
    group_poll(0);
    link_opened();
    group_system_frame();
    F.status = 4;
    F.link_up = 0;
    group_system_frame();
    CHECK(count("group: HOME and sleep on") == 2);
    group_poll(1);
    F.status = 0;
}

static void toasts_and_strip(void)
{
    GroupStrip s;
    char text[48];
    group_session_stop();
    F.log[0] = 0;
    group_open();
    group_pick(1);
    group_poll(0);
    link_opened();
    F.join_result = 1;
    group_join_row(0);
    group_poll(0);
    group_poll(0);
    CHECK(!group_strip(&s));                          /* no lobby yet */
    F.has_lobby = 1;
    memset(&F.lobby, 0, sizeof F.lobby);
    F.lobby.phase = LOBBY_STAGE;
    F.lobby.sss_picker = 2;
    F.present = 0x3;
    F.my_port = 1;
    snprintf(F.names[0], 32, "Alice");
    snprintf(F.names[1], 32, "Bob");
    F.now = 10000000;
    CHECK(group_strip(&s) && s.active && s.me == 1 && s.picker == 2 && !strcmp(s.port[0].name, "Alice") &&
          s.port[1].present && !s.port[2].present && s.toast[0] == 0);
    F.lobby.phase = LOBBY_CSS;
    group_strip(&s);
    CHECK(s.picker == -1);                            /* the picker only on the stage select */
    /* a console joins: "NAME JOINED" for 4 s, in its port's colour */
    snprintf(F.names[2], 32, "Cleo");
    F.present = 0x7;
    group_strip(&s);
    CHECK(!strcmp(s.toast, "A 3DS JOINED") && group_toast_port() == 2);   /* its name comes with it */
    CHECK(group_toast(text, sizeof text) > 0);
    F.present = 0x3;
    F.now += 1000000;
    group_strip(&s);
    CHECK(!strcmp(s.toast, "Cleo LEFT"));
    F.now += 4100000;
    group_strip(&s);
    CHECK(s.toast[0] == 0 && group_toast(text, sizeof text) == 0);
    group_leave();
    F.has_lobby = 0;
}

static void menus_around_the_lobby(void)
{
    /* back from the lobby: the submenu with Group hovered; from Solo's character select: Solo; else vanilla's VS menu */
    CHECK(gr_menu_return_row(1, 0) == 1 && gr_menu_return_row(1, 1) == 1);
    CHECK(gr_menu_return_row(0, 1) == 0 && gr_menu_return_row(0, 0) == -1);
    /* "Choose a game.": a game picked goes in (even under a notice); the list gone goes back; else stay */
    CHECK(gr_choose_step(2, 1) == GR_CHOOSE_LOBBY && gr_choose_step(2, 0) == GR_CHOOSE_LOBBY);
    CHECK(gr_choose_step(0, 1) == GR_CHOOSE_STAY && gr_choose_step(0, 0) == GR_CHOOSE_BACK);
    CHECK(gr_choose_step(-1, 0) == GR_CHOOSE_BACK);
    /* entering it: the list opens again unless it is up or the menu reopened it (a lost session) */
    CHECK(gr_choose_opens_list(0, 0) && !gr_choose_opens_list(1, 1) && !gr_choose_opens_list(0, 1));
}

int main(void)
{
    menus_around_the_lobby();
    pages();
    the_list();
    host_and_back();
    leaving_and_lost();
    home_and_sleep();
    toasts_and_strip();
    printf("group_menu: %s\n", failures ? "FAIL" : "ok");
    return failures != 0;
}
