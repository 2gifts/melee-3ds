/* Slippi Direct menu flow, native side (see slippi_ui.h).
 *
 * The online CSS follows Slippi's Online/CSS codes:
 * - START with a character chosen opens connect-code entry. Confirming locks
 *   in and searches; the first stage is random.
 * - Z cancels a search or clears an error; holding Z (49 frames) disconnects.
 * - After a game, the winner presses START to lock in, and the loser
 *   presses START to pick the stage.
 * The engine exits the CSS when this returns SLIPPI_UI_START or SLIPPI_UI_SSS.
 *
 * Code entry is the bottom screen's keyboard. It mirrors Melee's name entry
 * as Slippi patches it: 8 characters, '#', recent codes on L/R, Z (here X)
 * takes the suggestion, B erases.
 */
#include "slippi_ui.h"
#include "slippi_internal.h"
#include <3ds.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* port/3ds/slippi/slippi_bridge.c */
int slippi_net_start(const char *opponent_code);
int slippi_net_status(void);
const char *slippi_net_error(void);
void slippi_net_poll(void);
void slippi_net_stop(void);
int slippi_net_remote_ready(void);
int slippi_net_ping_ms(void);
int slippi_net_remote_character(void);
extern int mp_native_file_write_async(const char *path, const void *src, unsigned size);
extern void *mp_native_slippi_file_load(const char *path);
extern unsigned mp_native_slippi_file_size(void);
extern void mp_native_log(const char *);

#define USER_PATH "sdmc:/3ds/melee/slippi/user.json"
#define HISTORY_PATH "sdmc:/3ds/melee/slippi/direct-codes.txt"

/* GameCube buttons (HSD_PAD_*) */
enum { GC_LEFT = 1, GC_RIGHT = 2, GC_DOWN = 4, GC_UP = 8, GC_Z = 0x10, GC_A = 0x100, GC_B = 0x200,
       GC_X = 0x400, GC_Y = 0x800, GC_START = 0x1000 };
/* Engine sound requests, returned with the flags (online_mode.c plays them). */
enum { SFX_FORWARD = 0x100, SFX_BACK = 0x200, SFX_ERROR = 0x400, SFX_MOVE = 0x800 };

/* 37 characters, then ERASE, CANCEL, OLDER, NEWER, OK. */
const char slippi_ui_keys[SLIPPI_KEYS + 1] = "1234567890QWERTYUIOPASDFGHJKL#ZXCVBNM\b\x1b<>\r";

static SlippiUiView ui;
static int started, sent, games, ckind = -1, color, lock_ckind, lock_color, stage = -1, alt;
static int user_loaded, history_loaded, frame, pending_start, pending_cancel, sfx;
static unsigned stage_used;   /* stages drawn from the pool this connection */
static char history[SLIPPI_HISTORY_MAX][SLIPPI_CODE_MAX + 1];

static void load_settings(void);

const SlippiUiView *slippi_ui_view(void) { return &ui; }

/* FrozenStadiumToggle: a static byte in Slippi, kept between visits. */
int slippi_ui_alt(int toggle)
{
    if (toggle) {
        ui.frozen_stadium ^= 1;
        ++ui.serial;
    }
    return ui.frozen_stadium;
}
unsigned slippi_ui_serial(void) { return ui.serial; }

static void changed(void) { ++ui.serial; }

static void copy(char *dst, size_t size, const char *src)
{
    snprintf(dst, size, "%s", src ? src : "");
}

static void load_user(void)
{
    slippi_user user;
    char err[96];
    if (user_loaded) return;
    user_loaded = 1;
    if (slippi_user_load(&user, USER_PATH, err, sizeof err) == 0) {
        copy(ui.user_name, sizeof ui.user_name, user.display_name);
        copy(ui.user_code, sizeof ui.user_code, user.connect_code);
    }
    memset(&user, 0, sizeof user);   /* the play key stays out of memory dumps */
    load_settings();
}

/* direct-codes.txt: one code per line, most recent first. */
static void load_history(void)
{
    char *text;
    unsigned size, i = 0;
    if (history_loaded) return;
    history_loaded = 1;
    text = mp_native_slippi_file_load(HISTORY_PATH);
    if (!text) {
        slippi_config cfg;
        slippi_config_defaults(&cfg);
        if (slippi_config_load(&cfg, "sdmc:/3ds/melee/slippi/config.ini") == 0 && cfg.opponent[0])
            copy(history[ui.history_count++], sizeof history[0], cfg.opponent);
        return;
    }
    size = mp_native_slippi_file_size();
    while (i < size && ui.history_count < SLIPPI_HISTORY_MAX) {
        char line[SLIPPI_CODE_MAX + 1];
        unsigned n = 0;
        while (i < size && text[i] != '\n') {
            if (text[i] != '\r' && n < SLIPPI_CODE_MAX) line[n++] = text[i];
            i++;
        }
        i++;
        line[n] = 0;
        if (n) copy(history[ui.history_count++], sizeof history[0], line);
    }
    free(text);
}

static void save_history(const char *code)
{
    char text[SLIPPI_HISTORY_MAX * (SLIPPI_CODE_MAX + 2)];
    int n = 0, i, count = 1;
    char keep[SLIPPI_HISTORY_MAX][SLIPPI_CODE_MAX + 1];
    copy(keep[0], sizeof keep[0], code);
    for (i = 0; i < ui.history_count && count < SLIPPI_HISTORY_MAX; i++)
        if (strcmp(history[i], code) != 0) copy(keep[count++], sizeof keep[0], history[i]);
    memcpy(history, keep, sizeof keep);
    ui.history_count = count;
    for (i = 0; i < count; i++) n += snprintf(text + n, sizeof text - n, "%s\n", history[i]);
    mp_native_file_write_async(HISTORY_PATH, text, (unsigned) n);
}

/* Slippi's autocomplete: the index-th most recent code that starts with
 * what was typed (FN_FetchSuggestion / doesTagMatchInput). */
static void fetch_suggestion(void)
{
    int i, found = 0, len = (int) strlen(ui.typed);
    ui.suggestion[0] = 0;
    for (i = 0; i < ui.history_count; i++) {
        if (strncmp(history[i], ui.typed, len) == 0 && (int) strlen(history[i]) > len) {
            if (found++ == ui.history_index) {
                copy(ui.suggestion, sizeof ui.suggestion, history[i]);
                break;
            }
        }
    }
    if (!ui.suggestion[0] && ui.history_index > 0) ui.history_index = found ? found - 1 : 0;
    if (!ui.suggestion[0] && found) {
        ui.history_index = 0;
        fetch_suggestion();
    }
}

/* Dolphin's getRandomStage: the server's legal stages, no repeat until all
 * were drawn. */
static int random_stage(void)
{
    static const uint8_t fallback[] = {0x2, 0x3, 0x8, 0x1C, 0x1F, 0x20};
    const uint8_t *pool = fallback;
    int count = (int) sizeof fallback, left = 0, i, pick;
    sp_lock();
    if (g_slippi.match.stage_count > 0) {
        pool = g_slippi.match.stages;
        count = g_slippi.match.stage_count;
    }
    sp_unlock();
    if (count > 16) count = 16;
    for (i = 0; i < count; i++) if (!(stage_used & (1u << i))) left++;
    if (!left) {
        stage_used = 0;
        left = count;
    }
    pick = (int) (sp_random() % (uint32_t) left);
    for (i = 0; i < count; i++) {
        if (stage_used & (1u << i)) continue;
        if (pick-- == 0) {
            stage_used |= 1u << i;
            return pool[i];
        }
    }
    return 0x1F;
}

static void send_selections(void)
{
    int s = -1, a = 0;
    if (games == 0) {
        s = random_stage();                 /* SB_RAND: the first stage is random */
    } else if (ui.chose_stage) {
        s = stage;
        a = alt;
    }                                       /* the winner leaves it to the loser */
    slippi_set_selections(lock_ckind, lock_color, s, a << 8);
    sent = 1;
    {
        char text[96];
        snprintf(text, sizeof text, "Slippi UI: locked in character %d colour %d, stage %d%s\n", lock_ckind, lock_color, s,
                 a ? " (frozen)" : "");
        mp_native_log(text);
    }
}

static void stop(int phase)
{
    if (started) slippi_net_stop();
    started = sent = 0;
    ui.locked = ui.need_stage = ui.chose_stage = 0;
    ui.hold_z = 0;
    ui.phase = phase;
    ui.opponent_name[0] = ui.opponent_code[0] = 0;
    games = 0;
    stage_used = 0;
    ui.chat_page = -1;
    ui.chat_count = 0;
    if (ui.page == SLIPPI_PAGE_CHAT) ui.page = SLIPPI_PAGE_CSS;
    changed();
}

static void begin_search(const char *code)
{
    copy(ui.target, sizeof ui.target, code);
    save_history(code);
    lock_ckind = ckind;
    lock_color = color;
    ui.locked = 1;
    ui.need_stage = ui.chose_stage = 0;
    sent = 0;
    games = 0;
    stage_used = 0;
    ui.error[0] = 0;
    {
        char text[80];
        snprintf(text, sizeof text, "Slippi UI: searching for %s\n", code);
        mp_native_log(text);
    }
    if (slippi_net_start(code) < 0) {
        copy(ui.error, sizeof ui.error, slippi_net_error());
        ui.phase = SLIPPI_PHASE_ERROR;
        ui.locked = 0;
        sfx |= SFX_ERROR;
    } else {
        started = 1;
        ui.phase = SLIPPI_PHASE_SEARCH;
    }
    changed();
}


/* ---- quick chat (Slippi's CSS chat; ids from SlippiNetplay) ----
 * Message id = page bit (Up 0x80, Left 0x10, Right 0x20, Down 0x40) | direction
 * bit (Left 1, Right 2, Down 4, Up 8). 0x10 alone: "chat disabled" reply. */
static const char *const default_chat[16] = {
    "ggs", "one more", "brb", "good luck", "well played", "that was fun", "thanks", "too good",
    "sorry", "my b", "lol", "wow", "gotta go", "one sec", "let's play again later", "bad connection",
};
static const int chat_page_bits[4] = {0x80, 0x10, 0x20, 0x40};
static const int chat_dir_bits[4] = {8, 1, 2, 4};
static int chat_cooldown, chat_page_frames, chat_age[3];

static const char *chat_message(int remote, int index)
{
    const char *s;
    if (index < 0 || index > 15) return "";
    s = remote ? g_slippi.match.remote_chat[index] : g_slippi.match.local_chat[index];
    return s[0] ? s : default_chat[index];
}

const char *slippi_ui_chat_message(int index) { return chat_message(0, index); }

static int chat_index(int id)
{
    int p, d;
    for (p = 0; p < 4; p++)
        for (d = 0; d < 4; d++)
            if (id == (chat_page_bits[p] | chat_dir_bits[d])) return p * 4 + d;
    return -1;
}

static void chat_add(int port, const char *who, const char *text)
{
    int i;
    if (ui.chat_count == 3) {
        for (i = 0; i < 2; i++) {
            memcpy(ui.chat_text[i], ui.chat_text[i + 1], sizeof ui.chat_text[i]);
            ui.chat_port[i] = ui.chat_port[i + 1];
            chat_age[i] = chat_age[i + 1];
        }
        ui.chat_count = 2;
    }
    snprintf(ui.chat_text[ui.chat_count], sizeof ui.chat_text[0], "%s: %s", who, text);
    ui.chat_port[ui.chat_count] = port;
    chat_age[ui.chat_count] = 0;
    ui.chat_count++;
    changed();
}

static int local_port(void)
{
    int i;
    sp_lock();
    i = g_slippi.match.local_index;
    sp_unlock();
    return i == 1 ? 1 : 0;
}

void slippi_ui_send_chat(int index)
{
    if (ui.phase != SLIPPI_PHASE_CONNECTED || !ui.chat_enabled || index < 0 || index > 15) return;
    if (ui.page == SLIPPI_PAGE_CHAT) ui.page = SLIPPI_PAGE_CSS;
    ui.chat_page = -1;
    if (chat_cooldown > 0) {
        sfx |= SFX_ERROR;
        changed();
        return;
    }
    slippi_send_chat(chat_page_bits[index / 4] | chat_dir_bits[index % 4]);
    chat_add(local_port(), ui.user_name[0] ? ui.user_name : ui.user_code, chat_message(0, index));
    chat_cooldown = 60;
    sfx |= SFX_FORWARD;
}

void slippi_ui_open_chat(void)
{
    if (ui.phase != SLIPPI_PHASE_CONNECTED || !ui.chat_enabled) {
        sfx |= SFX_ERROR;
        return;
    }
    ui.page = SLIPPI_PAGE_CHAT;
    sfx |= SFX_FORWARD;
    changed();
}

void slippi_ui_close_chat(void)
{
    ui.page = SLIPPI_PAGE_CSS;
    ui.chat_page = -1;
    sfx |= SFX_BACK;
    changed();
}

/* Once per CSS frame: D-pad page then direction, incoming messages, ageing. */
static void chat_frame(int trigger)
{
    int d = -1, i, id;
    if (chat_cooldown > 0) chat_cooldown--;
    for (i = 0; i < ui.chat_count; i++) {
        if (++chat_age[i] > 600) {   /* a message stays about ten seconds */
            int k;
            for (k = i; k + 1 < ui.chat_count; k++) {
                memcpy(ui.chat_text[k], ui.chat_text[k + 1], sizeof ui.chat_text[k]);
                ui.chat_port[k] = ui.chat_port[k + 1];
                chat_age[k] = chat_age[k + 1];
            }
            ui.chat_count--;
            i--;
            changed();
        }
    }
    if (ui.phase != SLIPPI_PHASE_CONNECTED) {
        if (ui.chat_page >= 0 || ui.page == SLIPPI_PAGE_CHAT) {
            ui.chat_page = -1;
            if (ui.page == SLIPPI_PAGE_CHAT) ui.page = SLIPPI_PAGE_CSS;
            changed();
        }
        return;
    }
    id = slippi_chat_poll();
    if (id) {
        const char *who = ui.opponent_name[0] ? ui.opponent_name : ui.opponent_code;
        if (id == SLIPPI_CHAT_DISABLED) {
            char line[64];
            snprintf(line, sizeof line, "%s has chat disabled", who);
            chat_add(!local_port(), "", line);
            /* "name: " prefix not wanted here */
            snprintf(ui.chat_text[ui.chat_count - 1], sizeof ui.chat_text[0], "%s", line);
        } else if ((i = chat_index(id)) >= 0) {
            chat_add(!local_port(), who, chat_message(1, i));
        }
        sfx |= SFX_MOVE;
    }
    if (!ui.chat_enabled || ui.page == SLIPPI_PAGE_CODE) return;
    if (trigger & GC_UP) d = 0;
    else if (trigger & GC_LEFT) d = 1;
    else if (trigger & GC_RIGHT) d = 2;
    else if (trigger & GC_DOWN) d = 3;
    if (ui.chat_page >= 0 && ++chat_page_frames > 300) {
        ui.chat_page = -1;
        changed();
    }
    if (d < 0) return;
    if (ui.chat_page < 0) {
        ui.chat_page = d;
        chat_page_frames = 0;
        sfx |= SFX_MOVE;
        changed();
    } else {
        slippi_ui_send_chat(ui.chat_page * 4 + d);
    }
}

/* ---- online settings ---- */
#define CONFIG_PATH "sdmc:/3ds/melee/slippi/config.ini"
static int settings_loaded;

static void load_settings(void)
{
    slippi_config cfg;
    if (settings_loaded) return;
    settings_loaded = 1;
    slippi_config_defaults(&cfg);
    slippi_config_load(&cfg, CONFIG_PATH);
    ui.delay = cfg.delay;
    ui.chat_enabled = cfg.chat_enabled;
}

/* Rewrite config.ini with key=value replaced (or added), keeping the rest. */
static void save_setting(const char *key, int value)
{
    char *text = mp_native_slippi_file_load(CONFIG_PATH);
    unsigned size = text ? mp_native_slippi_file_size() : 0, i = 0;
    char out[2048];
    int n = 0, found = 0;
    size_t klen = strlen(key);
    while (i < size && n < (int) sizeof out - 64) {
        unsigned start = i;
        while (i < size && text[i] != '\n') i++;
        if (i - start > klen && !strncmp(text + start, key, klen) && text[start + klen] == '=') {
            n += snprintf(out + n, sizeof out - n, "%s=%d\n", key, value);
            found = 1;
        } else {
            n += snprintf(out + n, sizeof out - n, "%.*s\n", (int) (i - start), text + start);
        }
        i++;
    }
    if (!found) n += snprintf(out + n, sizeof out - n, "%s=%d\n", key, value);
    free(text);
    mp_native_file_write_async(CONFIG_PATH, out, (unsigned) n);
}

/* Sheik <-> Zelda on the Zelda icon (not while locked in). */
void slippi_ui_toggle_zelda(void)
{
    if (ui.locked) {
        sfx |= SFX_ERROR;
        return;
    }
    ui.play_zelda ^= 1;
    sfx |= SFX_MOVE;
    changed();
}

void slippi_ui_open_settings(void)
{
    load_settings();
    load_history();
    ui.page = SLIPPI_PAGE_SETTINGS;
    sfx |= SFX_FORWARD;
    changed();
}

void slippi_ui_close_settings(void)
{
    ui.page = SLIPPI_PAGE_CSS;
    sfx |= SFX_BACK;
    changed();
}

void slippi_ui_set_delay(int delta)
{
    int d = ui.delay + delta;
    if (d < 1 || d > 9) {   /* Slippi Dolphin's range */
        sfx |= SFX_ERROR;
        return;
    }
    ui.delay = d;
    sp_lock();
    g_slippi.cfg.delay = d;   /* the next match (if the network is up) */
    sp_unlock();
    save_setting("delay", d);
    sfx |= SFX_MOVE;
    changed();
}

void slippi_ui_toggle_chat(void)
{
    ui.chat_enabled ^= 1;
    sp_lock();
    g_slippi.cfg.chat_enabled = ui.chat_enabled;
    sp_unlock();
    save_setting("chat", ui.chat_enabled);
    sfx |= SFX_MOVE;
    changed();
}

void slippi_ui_clear_history(void)
{
    ui.history_count = 0;
    mp_native_file_write_async(HISTORY_PATH, "", 0);
    sfx |= SFX_BACK;
    changed();
}

int slippi_ui_local_port(void)
{
    if (ui.phase != SLIPPI_PHASE_CONNECTED && ui.page != SLIPPI_PAGE_MATCH) return -1;
    return local_port();
}

static void update_net(void)
{
    int st = slippi_net_status();
    if (st != ui.net_status) {
        ui.net_status = st;
        changed();
    }
    switch (st) {
    case 4:
        copy(ui.error, sizeof ui.error, slippi_net_error());
        stop(SLIPPI_PHASE_ERROR);
        sfx |= SFX_ERROR;
        break;
    case 5:
        /* The opponent left: back to idle, as Slippi does (no message). */
        stop(SLIPPI_PHASE_IDLE);
        sfx |= SFX_BACK;
        break;
    case 1:
    case 2:
        if (ui.phase != SLIPPI_PHASE_SEARCH) {
            ui.phase = SLIPPI_PHASE_SEARCH;
            changed();
        }
        break;
    case 3:
        if (ui.phase != SLIPPI_PHASE_CONNECTED) {
            ui.phase = SLIPPI_PHASE_CONNECTED;
            sp_lock();
            copy(ui.opponent_name, sizeof ui.opponent_name, g_slippi.match.remote_name);
            copy(ui.opponent_code, sizeof ui.opponent_code, g_slippi.match.remote_code);
            sp_unlock();
            changed();
        }
        if (ui.locked && !sent) send_selections();
        if (frame % 30 == 0) {
            int ping = slippi_net_ping_ms();
            if (ping != ui.ping_ms) {
                ui.ping_ms = ping;
                changed();
            }
        }
        break;
    }
}

int slippi_ui_css(int packed, int trigger, int held)
{
    int flags = 0, ready = packed & 1;
    ckind = (packed >> 8) & 0xFF;
    color = (packed >> 16) & 0xFF;
    if (ckind != ui.ckind) {
        ui.ckind = ckind;
        changed();
    }
    if (ready != ui.ready) {
        ui.ready = ready;
        changed();
    }
    if (ui.page != SLIPPI_PAGE_CODE && ui.page != SLIPPI_PAGE_CSS && ui.page != SLIPPI_PAGE_CHAT &&
        ui.page != SLIPPI_PAGE_SETTINGS) {
        ui.page = SLIPPI_PAGE_CSS;
        changed();
    }
    load_user();
    if (++frame % 15 == 0) {
        ui.spinner ^= 1;
        changed();
    }
    if (started) {
        slippi_net_poll();
        update_net();
    }
    chat_frame(trigger);
    if (ui.page == SLIPPI_PAGE_CSS) {
        if (trigger & GC_START) pending_start = 1;
        if (trigger & GC_Z) pending_cancel = 1;
        if (ui.phase == SLIPPI_PHASE_CONNECTED && (held & GC_Z)) {
            if (++ui.hold_z > 0x30) {
                mp_native_log("Slippi UI: disconnected (Z held)\n");
                stop(SLIPPI_PHASE_IDLE);
                sfx |= SFX_BACK;
            }
            changed();
        } else if (ui.hold_z) {
            ui.hold_z = 0;
            changed();
        }
    }
    if (pending_cancel) {
        pending_cancel = 0;
        if (ui.phase == SLIPPI_PHASE_SEARCH) {
            mp_native_log("Slippi UI: search cancelled\n");
            stop(SLIPPI_PHASE_IDLE);
            sfx |= SFX_BACK;
        } else if (ui.phase == SLIPPI_PHASE_ERROR) {
            ui.phase = SLIPPI_PHASE_IDLE;
            ui.error[0] = 0;
            sfx |= SFX_BACK;
            changed();
        }
    }
    if (pending_start) {
        pending_start = 0;
        if (!ui.ready) {
            sfx |= SFX_ERROR;
        } else if (ui.phase == SLIPPI_PHASE_IDLE) {
            load_history();
            ui.page = SLIPPI_PAGE_CODE;
            ui.typed[0] = 0;
            ui.history_index = 0;
            ui.key = 0;
            fetch_suggestion();
            sfx |= SFX_FORWARD;
            changed();
        } else if (ui.phase == SLIPPI_PHASE_CONNECTED && !ui.locked) {
            if (ui.need_stage && !ui.chose_stage) {
                flags |= SLIPPI_UI_SSS;
            } else {
                lock_ckind = ckind;
                lock_color = color;
                ui.locked = 1;
                sent = 0;
                send_selections();
            }
            sfx |= SFX_FORWARD;
            changed();
        }
    }
    if (ui.phase == SLIPPI_PHASE_CONNECTED && ui.locked && sent && slippi_net_remote_ready()) flags |= SLIPPI_UI_START;
    if (ui.locked) flags |= SLIPPI_UI_LOCKED;
    if (!ui.play_zelda) flags |= SLIPPI_UI_SHEIK;
    flags |= sfx;
    sfx = 0;
    return flags;
}

void slippi_ui_event(int event, int arg)
{
    switch (event) {
    case SLIPPI_UI_EV_CSS_ENTER:
        load_user();
        ui.page = SLIPPI_PAGE_CSS;
        changed();
        break;
    case SLIPPI_UI_EV_LEAVE:
        if (started) mp_native_log("Slippi UI: left the online CSS; connection closed\n");
        stop(SLIPPI_PHASE_IDLE);
        ui.page = SLIPPI_PAGE_NONE;
        break;
    case SLIPPI_UI_EV_STAGE:
        if (arg >= 0) {
            stage = arg & 0xFFFF;
            alt = (arg >> 16) & 0xFF;
            ui.chose_stage = 1;
            lock_ckind = ckind;
            lock_color = color;
            ui.locked = 1;
            sent = 0;
        }
        changed();
        break;
    case SLIPPI_UI_EV_RESULT:
        ui.won_last = arg;
        ui.need_stage = !arg;
        ui.chose_stage = 0;
        ui.locked = 0;
        sent = 0;
        games++;
        changed();
        break;
    case SLIPPI_UI_EV_MATCH:
        ui.page = SLIPPI_PAGE_MATCH;
        changed();
        break;
    }
}

int slippi_ui_remote(void)
{
    int c;
    if (ui.phase != SLIPPI_PHASE_CONNECTED) return -1;
    c = slippi_net_remote_character();
    if (c < 0) return -1;
    {
        unsigned char s[13];
        slippi_remote_selections(s);
        return c | s[1] << 8;
    }
}

/* ---- code entry ---- */

void slippi_ui_press_start(void) { pending_start = 1; }
void slippi_ui_cancel(void)
{
    if (ui.phase == SLIPPI_PHASE_CONNECTED) {
        mp_native_log("Slippi UI: disconnected (touch)\n");
        stop(SLIPPI_PHASE_IDLE);
        sfx |= SFX_BACK;
    } else {
        pending_cancel = 1;
    }
}

void slippi_ui_type(int key)
{
    char c;
    size_t n = strlen(ui.typed);
    if (key < 0 || key >= SLIPPI_KEYS) return;
    c = slippi_ui_keys[key];
    switch (c) {
    case '\b': slippi_ui_erase(); return;
    case '\x1b': slippi_ui_close_code(); return;
    case '<': slippi_ui_history(1); return;
    case '>': slippi_ui_history(-1); return;
    case '\r': slippi_ui_confirm(); return;
    }
    if (n >= SLIPPI_CODE_MAX) {
        sfx |= SFX_ERROR;
        return;
    }
    ui.typed[n] = c;
    ui.typed[n + 1] = 0;
    ui.history_index = 0;
    fetch_suggestion();
    sfx |= SFX_MOVE;
    changed();
}

void slippi_ui_erase(void)
{
    size_t n = strlen(ui.typed);
    if (!n) {
        slippi_ui_close_code();
        return;
    }
    ui.typed[n - 1] = 0;
    ui.history_index = 0;
    fetch_suggestion();
    sfx |= SFX_BACK;
    changed();
}

void slippi_ui_history(int step)
{
    int before = ui.history_index;
    ui.history_index += step;
    if (ui.history_index < 0) ui.history_index = 0;
    fetch_suggestion();
    if (ui.history_index == before && step) sfx |= SFX_ERROR;
    else sfx |= SFX_MOVE;
    changed();
}

void slippi_ui_use_suggestion(void)
{
    if (!ui.suggestion[0]) {
        sfx |= SFX_ERROR;
        return;
    }
    copy(ui.typed, sizeof ui.typed, ui.suggestion);
    ui.key = SLIPPI_KEYS - 1;   /* to OK, as Slippi jumps to the confirm button */
    fetch_suggestion();
    sfx |= SFX_FORWARD;
    changed();
}

void slippi_ui_confirm(void)
{
    if (!ui.typed[0]) {
        sfx |= SFX_ERROR;
        return;
    }
    ui.page = SLIPPI_PAGE_CSS;
    ui.suggestion[0] = 0;
    sfx |= SFX_FORWARD;
    begin_search(ui.typed);
}

void slippi_ui_close_code(void)
{
    ui.page = SLIPPI_PAGE_CSS;
    sfx |= SFX_BACK;
    changed();
}

/* Key navigation on the 10-column grid plus the action row. */
static void move_key(int dx, int dy)
{
    extern const short slippi_ui_key_rects[SLIPPI_KEYS][4];
    const short *r = slippi_ui_key_rects[ui.key];
    int cx = r[0] + r[2] / 2, cy = r[1] + r[3] / 2, best = -1, best_d = 1 << 30, i;
    for (i = 0; i < SLIPPI_KEYS; i++) {
        const short *k = slippi_ui_key_rects[i];
        int kx = k[0] + k[2] / 2, ky = k[1] + k[3] / 2, ddx = kx - cx, ddy = ky - cy, d;
        if (i == ui.key) continue;
        if (dx && (ddx * dx <= 0 || abs(ddy) > 12)) continue;
        if (dy && (ddy * dy <= 0)) continue;
        d = dx ? abs(ddx) + 4 * abs(ddy) : abs(ddy) * 4 + abs(ddx);
        if (d < best_d) {
            best_d = d;
            best = i;
        }
    }
    if (best >= 0) {
        ui.key = best;
        sfx |= SFX_MOVE;
        changed();
    }
}

/* Edges come from the held keys: game.c scans HID once per rendered frame
 * as well, so hidKeysDown() here missed about half of the presses.
 * Directions repeat while held (after 20 reads, every 5). */
unsigned slippi_ui_pad(unsigned held, unsigned gc, int *zero_sticks)
{
    enum { DIRS = KEY_DUP | KEY_DDOWN | KEY_DLEFT | KEY_DRIGHT | KEY_CPAD_UP | KEY_CPAD_DOWN | KEY_CPAD_LEFT | KEY_CPAD_RIGHT };
    static unsigned previous, repeat, swallow;
    static int was_code;
    unsigned down = held & ~previous;
    if ((held & DIRS) && (held & DIRS) == (previous & DIRS)) {
        if (++repeat > 20 && repeat % 5 == 0) down |= held & DIRS;
    } else {
        repeat = 0;
    }
    previous = held;
    *zero_sticks = 0;
    if (was_code && ui.page != SLIPPI_PAGE_CODE) {
        /* Leaving the keyboard: what is still held (B, A on OK, START)
         * reaches the CSS only after a release. */
        swallow = gc;
    }
    was_code = ui.page == SLIPPI_PAGE_CODE;
    swallow &= gc;
    gc &= ~swallow;
    if (ui.page == SLIPPI_PAGE_CODE) {
        if (down & (KEY_DUP | KEY_CPAD_UP)) move_key(0, -1);
        if (down & (KEY_DDOWN | KEY_CPAD_DOWN)) move_key(0, 1);
        if (down & (KEY_DLEFT | KEY_CPAD_LEFT)) move_key(-1, 0);
        if (down & (KEY_DRIGHT | KEY_CPAD_RIGHT)) move_key(1, 0);
        if (down & KEY_A) slippi_ui_type(ui.key);
        if (down & KEY_B) slippi_ui_erase();
        if (down & KEY_L) slippi_ui_history(1);
        if (down & KEY_R) slippi_ui_history(-1);
        if (down & (KEY_X | KEY_ZL | KEY_ZR)) slippi_ui_use_suggestion();
        if (down & KEY_START) slippi_ui_confirm();
        *zero_sticks = 1;
        return 0;   /* nothing reaches the game while typing */
    }
    if ((ui.page == SLIPPI_PAGE_CSS || ui.page == SLIPPI_PAGE_CHAT) && ui.locked) {
        /* Locked in: A/B cannot unselect, X/Y cannot recolour
         * (PreventA/BPressCharUnselect, PreventColorChange). */
        gc &= ~(unsigned) (GC_A | GC_B | GC_X | GC_Y);
    }
    return gc;
}

/* ---- top-screen CSS text (Slippi's LoadCSSText / UserDisplayFunctions) ----
 * Lines (fixed slots, positions in online_mode.c):
 *   0 header       1 "User"      2 name       3 "Connect Code"   4 code
 *   5 line 1       6 spinner 1   7 line 2     8 spinner 2        9 line 3
 *  10 spinner 3   11 Z hint     12 chat hint 13 "Playing:"      14 opponent
 *  15-18 error lines
 * Text goes through Melee's SIS converter, which takes letters, digits,
 * space and . , - : ' " as ASCII and any other byte as a Shift-JIS lead
 * byte: other punctuation becomes its full-width form ('#' -> 0x8194). */
enum { TEXT_WHITE, TEXT_GRAY, TEXT_RED, TEXT_DONE, TEXT_WAIT };

static void sis(char *out, int len, const char *in)
{
    static const struct { char c; unsigned short sjis; } wide[] = {
        {'!', 0x8149}, {'#', 0x8194}, {'$', 0x8190}, {'%', 0x8193}, {'&', 0x8195}, {'(', 0x8169}, {')', 0x816a},
        {'*', 0x8196}, {'+', 0x817b}, {'/', 0x815e}, {';', 0x8147}, {'<', 0x8183}, {'=', 0x8181}, {'>', 0x8184},
        {'?', 0x8148}, {'@', 0x8197}, {'[', 0x816d}, {'\\', 0x815f}, {']', 0x816e}, {'^', 0x814f}, {'_', 0x8151},
        {'`', 0x814d}, {'{', 0x816f}, {'|', 0x8162}, {'}', 0x8170}, {'~', 0x8160},
    };
    int n = 0;
    for (; *in && n + 3 < len; in++) {
        unsigned char c = (unsigned char) *in;
        unsigned short w = 0;
        unsigned i;
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == ' ' || c == '.' ||
            c == ',' || c == '-' || c == ':' || c == '\'' || c == '"') {
            out[n++] = (char) c;
            continue;
        }
        if (c >= 0x80) {
            /* UTF-8 sequence: one full-width '?' for the whole character. */
            while ((in[1] & 0xC0) == 0x80) in++;
            w = 0x8148;
        } else {
            for (i = 0; i < sizeof wide / sizeof wide[0]; i++)
                if (wide[i].c == (char) c) w = wide[i].sjis;
        }
        if (w) {
            out[n++] = (char) (w >> 8);
            out[n++] = (char) w;
        }
    }
    out[n] = 0;
}

void slippi_ui_sis(const char *in, char *out, int len) { sis(out, len, in); }

static int spinner_glyph(char *out, int len, int done)
{
    unsigned short w = done ? 0x817C : ui.spinner ? 0x817E : 0x817B;
    if (len < 3) return TEXT_WAIT;
    out[0] = (char) (w >> 8);
    out[1] = (char) w;
    out[2] = 0;
    return done ? TEXT_DONE : TEXT_WAIT;
}

/* One error line of about 30 characters (Slippi wraps the same way). */
static void error_line(int index, char *out, int len)
{
    const char *e = ui.error[0] ? ui.error : "Unknown error";
    char line[40];
    int k;
    for (k = 0; k <= index && *e; k++) {
        int n = 0, last = -1;
        while (e[n] && n < 30) {
            if (e[n] == ' ') last = n;
            n++;
        }
        if (e[n] && last > 0) n = last;
        snprintf(line, sizeof line, "%.*s", n, e);
        e += n;
        while (*e == ' ') e++;
        if (k == index) {
            sis(out, len, line);
            return;
        }
    }
    out[0] = 0;
}

int slippi_ui_text(int line, char *out, int len)
{
    char b[96];
    int error = ui.phase == SLIPPI_PHASE_ERROR, connected = ui.phase == SLIPPI_PHASE_CONNECTED;
    out[0] = 0;
    switch (line) {
    case 0: sis(out, len, error ? "Error" : "Direct Mode"); return TEXT_WHITE;
    case 1: sis(out, len, "User"); return TEXT_GRAY;
    case 2: sis(out, len, ui.user_name); return TEXT_WHITE;
    case 3: sis(out, len, "Connect Code"); return TEXT_GRAY;
    case 4: sis(out, len, ui.user_code); return TEXT_WHITE;
    case 5:
        if (!error) sis(out, len, ui.ready ? "Character selected" : "Select your character");
        return TEXT_WHITE;
    case 6: return error ? TEXT_WHITE : spinner_glyph(out, len, ui.ready);
    case 7:
        if (error) return TEXT_WHITE;
        if (ui.page == SLIPPI_PAGE_CODE) sis(out, len, "Enter the code on the touch screen");
        else if (ui.locked) sis(out, len, "Locked in");
        else if (ui.phase == SLIPPI_PHASE_IDLE) sis(out, len, "Press START to enter code");
        else if (connected) sis(out, len, ui.need_stage && !ui.chose_stage ? "Press START to select stage" : "Press START to lock in");
        return TEXT_WHITE;
    case 8:
        if (error || !out) return TEXT_WHITE;
        if (ui.phase == SLIPPI_PHASE_IDLE && ui.page != SLIPPI_PAGE_CODE && !ui.locked) return spinner_glyph(out, len, 0);
        return spinner_glyph(out, len, ui.locked);
    case 9:
        if (ui.phase == SLIPPI_PHASE_SEARCH) {
            snprintf(b, sizeof b, ui.net_status == 2 ? "Connecting to %s" : "Searching for %s", ui.target);
            sis(out, len, b);
        } else if (connected) {
            sis(out, len, "Waiting on opponent");
        }
        return TEXT_WHITE;
    case 10:
        if (ui.phase == SLIPPI_PHASE_SEARCH || connected) return spinner_glyph(out, len, 0);
        return TEXT_WHITE;
    case 11:
        if (connected && ui.chat_page >= 0) return TEXT_GRAY;   /* the chat page uses the space */
        if (ui.phase == SLIPPI_PHASE_SEARCH) sis(out, len, "Press Z to cancel");
        else if (error) sis(out, len, "Press Z to clear error");
        else if (connected) sis(out, len, "Hold Z to disconnect");
        return TEXT_GRAY;
    case 12:
        if (connected && ui.chat_enabled && ui.chat_page < 0) sis(out, len, "Use D-Pad to Chat");
        return TEXT_GRAY;
    case 13:
        if (connected) sis(out, len, "Playing:");
        return TEXT_GRAY;
    case 14:
        if (connected) sis(out, len, ui.opponent_name[0] ? ui.opponent_name : ui.opponent_code);
        return TEXT_WHITE;
    case 15: case 16: case 17: case 18:
        if (error) error_line(line - 15, out, len);
        return TEXT_RED;
    case 30: case 31: {   /* in-game name tags, by port */
        int mine = (line - 30) == local_port();
        sis(out, len, mine ? (ui.user_name[0] ? ui.user_name : ui.user_code)
                           : (ui.opponent_name[0] ? ui.opponent_name : ui.opponent_code));
        return TEXT_WHITE;
    }
    case 24: case 25: case 26: case 27: case 28:   /* the open chat page */
        if (error || !connected || ui.chat_page < 0) return TEXT_WHITE;
        {
            static const char *const pages[4] = {"Up", "Left", "Right", "Down"};
            if (line == 24) {
                snprintf(b, sizeof b, "Page: %s", pages[ui.chat_page]);
                sis(out, len, b);
                return TEXT_WHITE;
            }
            snprintf(b, sizeof b, "%s: %s", pages[line - 25], chat_message(0, ui.chat_page * 4 + line - 25));
            sis(out, len, b);
            return TEXT_GRAY;
        }
    case 19: case 20: case 21: case 22: case 23:   /* chat messages (3 kept) */
        if (error || !connected || ui.chat_page >= 0) return TEXT_WHITE;
        if (line - 19 < ui.chat_count) {
            sis(out, len, ui.chat_text[line - 19]);
            return ui.chat_port[line - 19] ? 6 : 5;
        }
        return TEXT_WHITE;
    }
    return TEXT_WHITE;
}
