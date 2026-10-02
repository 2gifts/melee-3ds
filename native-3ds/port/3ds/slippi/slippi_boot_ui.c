/* Slippi experiment: the boot menu on the bottom screen's text console.
 *
 * Shown when sdmc:/3ds/melee/slippi/user.json exists: pick the opponent's
 * connect code (system keyboard), character, colour, stage (used when we
 * are the host) and input delay, then START to play Slippi Direct or B to
 * play offline. Choices are saved to config.ini, which the engine reads. */
#include <3ds.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "slippi_internal.h"
#include "slippi_plat.h"

static int session = 0;   /* 0 not asked, 1 online, 2 offline */

int mp_native_slippi_session(void) { return session; }

static const char* const characters[26] = {
    "Captain Falcon", "Donkey Kong", "Fox", "Mr. Game & Watch", "Kirby", "Bowser", "Link", "Luigi",
    "Mario", "Marth", "Mewtwo", "Ness", "Peach", "Pikachu", "Ice Climbers", "Jigglypuff",
    "Samus", "Yoshi", "Zelda", "Sheik", "Falco", "Young Link", "Dr. Mario", "Roy", "Pichu", "Ganondorf"};
static const struct { int id; const char* name; } stages[] = {
    {2, "Fountain of Dreams"}, {3, "Pokemon Stadium"}, {8, "Yoshi's Story"},
    {28, "Dream Land"}, {31, "Battlefield"}, {32, "Final Destination"}};
#define STAGE_COUNT ((int) (sizeof stages / sizeof stages[0]))
/* Costumes per character (external order). */
static const unsigned char costumes[26] = {6, 5, 4, 4, 6, 4, 5, 4, 5, 5, 4, 4, 5, 4, 4, 5, 5, 6, 5, 5, 4, 4, 5, 5, 4, 5};

typedef struct {
    char opponent[24];
    int character, color, stage, delay;
} choices;

static int key_int(const char* text, const char* key, int fallback)
{
    char pat[32];
    const char* p;
    snprintf(pat, sizeof pat, "\n%s=", key);
    p = strstr(text, pat);
    return p ? atoi(p + strlen(pat)) : fallback;
}

static void key_str(const char* text, const char* key, char* out, int len)
{
    char pat[32];
    const char* p;
    int n = 0;
    snprintf(pat, sizeof pat, "\n%s=", key);
    p = strstr(text, pat);
    if (!p) return;
    p += strlen(pat);
    while (*p && *p != '\r' && *p != '\n' && n + 1 < len) out[n++] = *p++;
    out[n] = 0;
}

static char* read_text(const char* path)
{
    FILE* f = fopen(path, "rb");
    long n;
    char* t;
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    t = malloc((size_t) n + 2);
    if (!t) { fclose(f); return NULL; }
    t[0] = '\n';   /* so "\nkey=" also finds the first line */
    n = (long) fread(t + 1, 1, (size_t) n, f);
    t[n + 1] = 0;
    fclose(f);
    return t;
}

/* Rewrite config.ini: our keys replaced, every other line kept. */
static void save(const char* path, const choices* c)
{
    static const char* const ours[] = {"opponent=", "character=", "color=", "stage=", "stage_select=", "delay="};
    char* old = read_text(path);
    FILE* f = fopen(path, "wb");
    if (!f) { free(old); return; }
    if (old) {
        char* line = old + 1;
        while (*line) {
            char* end = strchr(line, '\n');
            size_t len = end ? (size_t) (end - line) : strlen(line);
            int skip = 0;
            for (unsigned i = 0; i < sizeof ours / sizeof ours[0]; i++)
                if (!strncmp(line, ours[i], strlen(ours[i]))) skip = 1;
            if (!skip && len) { fwrite(line, 1, len, f); fputc('\n', f); }
            if (!end) break;
            line = end + 1;
        }
    }
    fprintf(f, "opponent=%s\ncharacter=%d\ncolor=%d\nstage=%d\nstage_select=1\ndelay=%d\n", c->opponent,
            c->character, c->color, c->stage, c->delay);
    fclose(f);
    free(old);
}

static int valid_code(const char* s)
{
    const char* hash = strchr(s, '#');
    int letters = 0;
    if (!hash || hash == s || !hash[1]) return 0;
    for (const char* p = s; p < hash; p++, letters++)
        if (!isalpha((unsigned char) *p)) return 0;
    for (const char* p = hash + 1; *p; p++)
        if (!isdigit((unsigned char) *p)) return 0;
    return letters <= 4;
}

static void ask_code(choices* c)
{
    SwkbdState kb;
    char buf[24];
    swkbdInit(&kb, SWKBD_TYPE_QWERTY, 2, 9);
    swkbdSetHintText(&kb, "Opponent connect code (ABCD#123)");
    swkbdSetInitialText(&kb, c->opponent);
    swkbdSetValidation(&kb, SWKBD_NOTEMPTY_NOTBLANK, 0, 0);
    if (swkbdInputText(&kb, buf, sizeof buf) == SWKBD_BUTTON_CONFIRM) {
        for (char* p = buf; *p; p++) *p = (char) toupper((unsigned char) *p);
        if (valid_code(buf)) strcpy(c->opponent, buf);
    }
}

static void draw(const choices* c, const slippi_user* u, int row)
{
    int s = 0;
    for (int i = 0; i < STAGE_COUNT; i++) if (stages[i].id == c->stage) s = i;
    printf("\x1b[2J\x1b[1;1H");
    printf("  SLIPPI DIRECT (experimental)\n\n");
    printf("  You: %s (%s)\n\n", u->display_name, u->connect_code);
    printf(" %c Opponent:  %s\n", row == 0 ? '>' : ' ', c->opponent[0] ? c->opponent : "(press A)");
    printf(" %c Character: %s\n", row == 1 ? '>' : ' ', characters[c->character]);
    printf(" %c Colour:    %d\n", row == 2 ? '>' : ' ', c->color);
    printf(" %c Stage:     %s\n", row == 3 ? '>' : ' ', stages[s].name);
    printf("               (used when you host)\n");
    printf(" %c Delay:     %d frames\n\n", row == 4 ? '>' : ' ', c->delay);
    printf("  Up/Down: choose   Left/Right: change\n");
    printf("  A: enter code     START: connect\n");
    printf("  B: play offline\n\n");
    printf("  The PC player picks your code in\n  Slippi's Direct mode.\n");
}

void slippi_boot_menu(void)
{
    char upath[256], cpath[256], *cfg;
    slippi_user user;
    choices c;
    char err[96];
    int row = 0;
    snprintf(upath, sizeof upath, "%s/user.json", sp_data_dir());
    snprintf(cpath, sizeof cpath, "%s/config.ini", sp_data_dir());
    memset(&user, 0, sizeof user);
    if (slippi_user_load(&user, upath, err, sizeof err) != 0) return;   /* no account: offline */
    memset(&c, 0, sizeof c);
    c.character = 2;
    c.stage = 32;
    c.delay = 3;
    cfg = read_text(cpath);
    if (cfg) {
        key_str(cfg, "opponent", c.opponent, sizeof c.opponent);
        c.character = key_int(cfg, "character", c.character);
        c.color = key_int(cfg, "color", c.color);
        c.stage = key_int(cfg, "stage", c.stage);
        c.delay = key_int(cfg, "delay", c.delay);
        if (key_int(cfg, "selftest", 0) || !key_int(cfg, "boot_menu", 1)) { free(cfg); return; }
        free(cfg);
    }
    if (c.character < 0 || c.character > 25) c.character = 2;
    if (c.color < 0 || c.color >= costumes[c.character]) c.color = 0;
    if (c.delay < 1 || c.delay > 15) c.delay = 3;
    draw(&c, &user, row);
    while (aptMainLoop()) {
        u32 k;
        hidScanInput();
        k = hidKeysDown();
        if (k & KEY_B) { session = 2; break; }
        if ((k & KEY_START) && c.opponent[0]) { session = 1; save(cpath, &c); break; }
        if (k & KEY_A) ask_code(&c);
        if (k & KEY_DOWN) row = (row + 1) % 5;
        if (k & KEY_UP) row = (row + 4) % 5;
        if (k & (KEY_LEFT | KEY_RIGHT)) {
            int d = (k & KEY_RIGHT) ? 1 : -1;
            if (row == 0) ask_code(&c);
            if (row == 1) { c.character = (c.character + d + 26) % 26; c.color = 0; }
            if (row == 2) c.color = (c.color + d + costumes[c.character]) % costumes[c.character];
            if (row == 3) {
                int s = 0;
                for (int i = 0; i < STAGE_COUNT; i++) if (stages[i].id == c.stage) s = i;
                c.stage = stages[(s + d + STAGE_COUNT) % STAGE_COUNT].id;
            }
            if (row == 4) c.delay = c.delay + d < 1 ? 1 : c.delay + d > 15 ? 15 : c.delay + d;
        }
        if (k) draw(&c, &user, row);
        gspWaitForVBlank();
    }
    printf("\x1b[2J\x1b[1;1H%s\n", session == 1 ? "Connecting to Slippi..." : "Offline");
}
