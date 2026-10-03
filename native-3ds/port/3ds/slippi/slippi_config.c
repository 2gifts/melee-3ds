/* config.ini, user.json, connect-code encoding and address helpers. */
#include "slippi_internal.h"
#include "slippi_json.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void slippi_config_defaults(slippi_config *cfg)
{
    memset(cfg, 0, sizeof(*cfg));
    cfg->delay = 2;                       /* Dolphin Slippi.OnlineDelay default */
    snprintf(cfg->mm_host, sizeof(cfg->mm_host), "%s", SLIPPI_MM_HOST_PROD);
    cfg->mm_port = SLIPPI_MM_PORT_DEFAULT;
    snprintf(cfg->app_version, sizeof(cfg->app_version), "%s", SLIPPI_APP_VERSION_DEFAULT);
    cfg->code_fullwidth = 1;
    cfg->net_thread = 1;
    cfg->net_thread_core = -2;
    cfg->net_thread_interval_us = 2000;
    cfg->chat_enabled = 1;
    cfg->auto_resend = 1;
    cfg->send_checksum = 1;
    cfg->mm_retries = -1;
    cfg->selftest_frames = 600;
    cfg->selftest_character = 2;          /* Fox */
}

static char *trim(char *s)
{
    while (*s && isspace((unsigned char)*s)) ++s;
    char *e = s + strlen(s);
    while (e > s && isspace((unsigned char)e[-1])) *--e = '\0';
    return s;
}

static void copy(char *dst, size_t n, const char *src) { snprintf(dst, n, "%s", src); }

/* key=value lines; '#' or ';' start a comment only at the start of a line
 * (connect codes contain '#'). Unknown keys are logged and ignored. */
int slippi_config_load(slippi_config *cfg, const char *path)
{
    size_t len = 0;
    char *text = sp_read_file(path, &len);
    if (!text) return -1;
    char *line = text;
    while (line && *line) {
        char *next = strpbrk(line, "\r\n");
        if (next) { *next++ = '\0'; while (*next == '\r' || *next == '\n') ++next; }
        char *s = trim(line);
        line = next;
        if (!*s || *s == '#' || *s == ';' || *s == '[') continue;
        char *eq = strchr(s, '=');
        if (!eq) continue;
        *eq = '\0';
        char *key = trim(s), *value = trim(eq + 1);
        int v = atoi(value);
        if (!strcmp(key, "opponent")) copy(cfg->opponent, sizeof(cfg->opponent), value);
        else if (!strcmp(key, "delay")) cfg->delay = v < 1 ? 1 : v > 15 ? 15 : v;
        else if (!strcmp(key, "mm_host")) copy(cfg->mm_host, sizeof(cfg->mm_host), value);
        else if (!strcmp(key, "mm_port")) cfg->mm_port = v;
        else if (!strcmp(key, "local_port")) cfg->local_port = v;
        else if (!strcmp(key, "lan_ip")) copy(cfg->lan_ip, sizeof(cfg->lan_ip), value);
        else if (!strcmp(key, "app_version")) copy(cfg->app_version, sizeof(cfg->app_version), value);
        else if (!strcmp(key, "code_encoding")) cfg->code_fullwidth = strcmp(value, "ascii") != 0;
        else if (!strcmp(key, "net_thread")) cfg->net_thread = v;
        else if (!strcmp(key, "net_thread_core")) cfg->net_thread_core = v;
        else if (!strcmp(key, "net_thread_interval_us")) cfg->net_thread_interval_us = v < 250 ? 250 : v;
        else if (!strcmp(key, "chat")) cfg->chat_enabled = v;
        else if (!strcmp(key, "auto_resend")) cfg->auto_resend = v;
        else if (!strcmp(key, "log_packets")) cfg->log_packets = v;
        else if (!strcmp(key, "send_checksum")) cfg->send_checksum = v;
        else if (!strcmp(key, "test_drop_pct")) cfg->test_drop_pct = v < 0 ? 0 : v > 90 ? 90 : v;
        else if (!strcmp(key, "mm_retries")) cfg->mm_retries = v;
        else if (!strcmp(key, "selftest")) cfg->selftest = v;
        else if (!strcmp(key, "character") || !strcmp(key, "color") || !strcmp(key, "stage") ||
                 !strcmp(key, "stage_select") || !strcmp(key, "boot_menu") || !strcmp(key, "record") || !strcmp(key, "test_inputs") || !strcmp(key, "test_end_frame") || !strcmp(key, "prefetch")) { /* engine / boot menu keys */ }
        else if (!strcmp(key, "selftest_frames")) cfg->selftest_frames = v;
        else if (!strcmp(key, "selftest_character")) cfg->selftest_character = v;
        else if (!strcmp(key, "selftest_allow_real_mm")) cfg->selftest_allow_real_mm = v;
        else if (!strcmp(key, "selftest_exit")) cfg->selftest_exit = v;
        else sp_log("config: unknown key '%s' ignored", key);
    }
    free(text);
    return 0;
}

/* user.json as written by the Slippi Launcher (uid, playKey, connectCode,
 * displayName, latestVersion). The play key is never logged. */
int slippi_user_load(slippi_user *user, const char *path, char *err, int errlen)
{
    memset(user, 0, sizeof(*user));
    size_t len = 0;
    char *text = sp_read_file(path, &len);
    if (!text) { snprintf(err, errlen, "No Slippi user file (%s)", path); return -1; }
    sj_node *root = sj_parse(text, len);
    free(text);
    if (!root || root->type != SJ_OBJECT) { sj_free(root); snprintf(err, errlen, "Cannot parse %s", path); return -1; }
    copy(user->uid, sizeof(user->uid), sj_get_str(root, "uid", ""));
    copy(user->play_key, sizeof(user->play_key), sj_get_str(root, "playKey", ""));
    copy(user->connect_code, sizeof(user->connect_code), sj_get_str(root, "connectCode", ""));
    copy(user->display_name, sizeof(user->display_name), sj_get_str(root, "displayName", ""));
    copy(user->latest_version, sizeof(user->latest_version), sj_get_str(root, "latestVersion", ""));
    sj_free(root);
    if (!user->uid[0] || !user->play_key[0]) { snprintf(err, errlen, "%s lacks uid/playKey", path); return -1; }
    user->loaded = 1;
    return 0;
}

/* Dolphin's narrow-to-wide punctuation table (ConvertNarrowSpecialSHIFTJIS). */
static const struct { char c; uint16_t sjis; } narrow_special[] = {
    {'!', 0x8149}, {'"', 0x8168}, {'#', 0x8194}, {'$', 0x8190}, {'%', 0x8193}, {'&', 0x8195}, {'\'', 0x8166},
    {'(', 0x8169}, {')', 0x816a}, {'*', 0x8196}, {'+', 0x817b}, {',', 0x8143}, {'-', 0x817c}, {'.', 0x8144},
    {'/', 0x815e}, {':', 0x8146}, {';', 0x8147}, {'<', 0x8183}, {'=', 0x8181}, {'>', 0x8184}, {'?', 0x8148},
    {'@', 0x8197}, {'[', 0x816d}, {'\\', 0x815f}, {']', 0x816e}, {'^', 0x814f}, {'_', 0x8151}, {'`', 0x814d},
    {'{', 0x816f}, {'|', 0x8162}, {'}', 0x8170}, {'~', 0x8160},
};

static int wide_char(char c, uint16_t *out)
{
    if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
    if (c >= 'A' && c <= 'Z') { *out = (uint16_t)(0x8260 + (c - 'A')); return 1; }
    if (c >= '0' && c <= '9') { *out = (uint16_t)(0x824F + (c - '0')); return 1; }
    if (c == ' ') { *out = 0x8140; return 1; }
    for (size_t i = 0; i < sizeof(narrow_special) / sizeof(narrow_special[0]); ++i)
        if (narrow_special[i].c == c) { *out = narrow_special[i].sjis; return 1; }
    return 0;
}

/* The opponent code as Slippi's name-entry screen hands it to Dolphin
 * (FN_TX_FIND_MATCH copies 9 Shift-JIS halfwords, Dolphin trims NULs):
 * full-width letters/digits and '＃'. fullwidth=0 sends the ASCII bytes.
 * Returns the byte count (<= 18). */
int slippi_code_to_sjis(const char *ascii, int fullwidth, uint8_t out[18])
{
    int n = 0;
    memset(out, 0, 18);
    for (const char *p = ascii; *p && n < 18; ++p) {
        uint16_t w;
        if (fullwidth && wide_char(*p, &w)) {
            if (n + 2 > 18) break;
            out[n++] = (uint8_t)(w >> 8);
            out[n++] = (uint8_t)w;
        } else {
            char c = *p;
            if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
            out[n++] = (uint8_t)c;
        }
    }
    return n;
}

void slippi_sjis_to_ascii(const uint8_t *in, int len, char *out, int outlen)
{
    int o = 0;
    for (int i = 0; i < len && o < outlen - 1; ++i) {
        uint8_t b = in[i];
        if (b == 0) break;
        if (b < 0x80) { out[o++] = (char)b; continue; }
        if (i + 1 >= len) break;
        uint16_t w = (uint16_t)((b << 8) | in[++i]);
        char c = '?';
        if (w >= 0x8260 && w <= 0x8279) c = (char)('A' + (w - 0x8260));
        else if (w >= 0x8281 && w <= 0x829A) c = (char)('A' + (w - 0x8281));
        else if (w >= 0x824F && w <= 0x8258) c = (char)('0' + (w - 0x824F));
        else if (w == 0x8140) c = ' ';
        else
            for (size_t k = 0; k < sizeof(narrow_special) / sizeof(narrow_special[0]); ++k)
                if (narrow_special[k].sjis == w) c = narrow_special[k].c;
        out[o++] = c;
    }
    out[o] = '\0';
}

static int parse_quad(const char *s, uint32_t *out)
{
    unsigned v[4];
    char tail;
    if (sscanf(s, "%u.%u.%u.%u%c", &v[0], &v[1], &v[2], &v[3], &tail) != 4) return -1;
    uint8_t *b = (uint8_t *)out;
    for (int i = 0; i < 4; ++i) { if (v[i] > 255) return -1; b[i] = (uint8_t)v[i]; }
    return 0;
}

/* "a.b.c.d:port" (Slippi's ipAddress / ipAddressLan format). */
int slippi_parse_addr(const char *text, ENetAddress *addr)
{
    char host[64];
    const char *colon = strrchr(text, ':');
    if (!colon || colon == text || (size_t)(colon - text) >= sizeof(host)) return -1;
    memcpy(host, text, (size_t)(colon - text));
    host[colon - text] = '\0';
    int port = atoi(colon + 1);
    if (port <= 0 || port > 65535) return -1;
    if (parse_quad(host, &addr->host) != 0 && enet_address_set_host(addr, host) != 0) return -1;
    addr->port = (enet_uint16)port;
    return 0;
}

void slippi_format_ip(uint32_t host, char *out, int len)
{
    const uint8_t *b = (const uint8_t *)&host;  /* network byte order in memory */
    snprintf(out, (size_t)len, "%u.%u.%u.%u", b[0], b[1], b[2], b[3]);
}
