/* Minimal JSON reader/writer (see slippi_json.h). */
#include "slippi_json.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const char *p, *end;
    int depth;
    int failed;
} sj_parser;

static void skip_ws(sj_parser *ps)
{
    while (ps->p < ps->end && (*ps->p == ' ' || *ps->p == '\t' || *ps->p == '\n' || *ps->p == '\r')) ++ps->p;
}

static sj_node *new_node(sj_type type)
{
    sj_node *n = (sj_node *)calloc(1, sizeof(sj_node));
    if (n) n->type = type;
    return n;
}

static void put_utf8(char **out, unsigned cp)
{
    char *o = *out;
    if (cp < 0x80) *o++ = (char)cp;
    else if (cp < 0x800) { *o++ = (char)(0xC0 | (cp >> 6)); *o++ = (char)(0x80 | (cp & 0x3F)); }
    else if (cp < 0x10000) { *o++ = (char)(0xE0 | (cp >> 12)); *o++ = (char)(0x80 | ((cp >> 6) & 0x3F)); *o++ = (char)(0x80 | (cp & 0x3F)); }
    else { *o++ = (char)(0xF0 | (cp >> 18)); *o++ = (char)(0x80 | ((cp >> 12) & 0x3F)); *o++ = (char)(0x80 | ((cp >> 6) & 0x3F)); *o++ = (char)(0x80 | (cp & 0x3F)); }
    *out = o;
}

static int hex4(const char *p, unsigned *v)
{
    unsigned r = 0;
    for (int i = 0; i < 4; ++i) {
        char c = p[i];
        r <<= 4;
        if (c >= '0' && c <= '9') r |= (unsigned)(c - '0');
        else if (c >= 'a' && c <= 'f') r |= (unsigned)(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') r |= (unsigned)(c - 'A' + 10);
        else return 0;
    }
    *v = r;
    return 1;
}

/* Parses a string starting at the opening quote; returns a malloc'd copy. */
static char *parse_string(sj_parser *ps)
{
    if (ps->p >= ps->end || *ps->p != '"') { ps->failed = 1; return NULL; }
    ++ps->p;
    const char *start = ps->p;
    size_t raw = 0;
    while (ps->p + raw < ps->end && ps->p[raw] != '"') { if (ps->p[raw] == '\\') ++raw; ++raw; }
    if (ps->p + raw >= ps->end) { ps->failed = 1; return NULL; }
    char *out = (char *)malloc(raw + 1), *o = out;
    if (!out) { ps->failed = 1; return NULL; }
    const char *p = start, *stop = start + raw;
    while (p < stop) {
        char c = *p++;
        if (c != '\\') { *o++ = c; continue; }
        if (p >= stop) break;
        c = *p++;
        switch (c) {
            case '"': *o++ = '"'; break;
            case '\\': *o++ = '\\'; break;
            case '/': *o++ = '/'; break;
            case 'b': *o++ = '\b'; break;
            case 'f': *o++ = '\f'; break;
            case 'n': *o++ = '\n'; break;
            case 'r': *o++ = '\r'; break;
            case 't': *o++ = '\t'; break;
            case 'u': {
                unsigned cp;
                if (stop - p < 4 || !hex4(p, &cp)) { free(out); ps->failed = 1; return NULL; }
                p += 4;
                if (cp >= 0xD800 && cp < 0xDC00 && stop - p >= 6 && p[0] == '\\' && p[1] == 'u') {
                    unsigned lo;
                    if (hex4(p + 2, &lo) && lo >= 0xDC00 && lo < 0xE000) { cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00); p += 6; }
                }
                if (cp == 0) cp = 0xFFFD; /* keep strings NUL-free */
                put_utf8(&o, cp);
                break;
            }
            default: free(out); ps->failed = 1; return NULL;
        }
    }
    *o = '\0';
    ps->p = stop + 1;
    return out;
}

static sj_node *parse_value(sj_parser *ps);

static int literal(sj_parser *ps, const char *word)
{
    size_t n = strlen(word);
    if ((size_t)(ps->end - ps->p) < n || memcmp(ps->p, word, n) != 0) return 0;
    ps->p += n;
    return 1;
}

static sj_node *parse_container(sj_parser *ps, int object)
{
    sj_node *node = new_node(object ? SJ_OBJECT : SJ_ARRAY), *last = NULL;
    if (!node) { ps->failed = 1; return NULL; }
    if (++ps->depth > 64) { ps->failed = 1; return node; }
    ++ps->p;
    skip_ws(ps);
    if (ps->p < ps->end && *ps->p == (object ? '}' : ']')) { ++ps->p; --ps->depth; return node; }
    for (;;) {
        char *key = NULL;
        skip_ws(ps);
        if (object) {
            key = parse_string(ps);
            if (ps->failed) return node;
            skip_ws(ps);
            if (ps->p >= ps->end || *ps->p != ':') { free(key); ps->failed = 1; return node; }
            ++ps->p;
        }
        sj_node *child = parse_value(ps);
        if (!child) { free(key); ps->failed = 1; return node; }
        child->key = key;
        if (last) last->next = child; else node->child = child;
        last = child;
        ++node->count;
        if (ps->failed) return node;
        skip_ws(ps);
        if (ps->p >= ps->end) { ps->failed = 1; return node; }
        if (*ps->p == ',') { ++ps->p; continue; }
        if (*ps->p == (object ? '}' : ']')) { ++ps->p; break; }
        ps->failed = 1;
        return node;
    }
    --ps->depth;
    return node;
}

static sj_node *parse_value(sj_parser *ps)
{
    skip_ws(ps);
    if (ps->p >= ps->end) { ps->failed = 1; return NULL; }
    char c = *ps->p;
    if (c == '{') return parse_container(ps, 1);
    if (c == '[') return parse_container(ps, 0);
    if (c == '"') {
        sj_node *n = new_node(SJ_STRING);
        if (!n) { ps->failed = 1; return NULL; }
        n->string = parse_string(ps);
        return n;
    }
    if (literal(ps, "true")) { sj_node *n = new_node(SJ_BOOL); if (n) n->boolean = 1; return n; }
    if (literal(ps, "false")) return new_node(SJ_BOOL);
    if (literal(ps, "null")) return new_node(SJ_NULL);
    if (c == '-' || (c >= '0' && c <= '9')) {
        char tmp[64];
        size_t n = 0;
        while (ps->p + n < ps->end && n < sizeof(tmp) - 1 && strchr("+-0123456789.eE", ps->p[n])) { tmp[n] = ps->p[n]; ++n; }
        tmp[n] = '\0';
        char *endp = NULL;
        double v = strtod(tmp, &endp);
        if (endp == tmp) { ps->failed = 1; return NULL; }
        ps->p += endp - tmp;
        sj_node *node = new_node(SJ_NUMBER);
        if (node) node->number = v;
        return node;
    }
    ps->failed = 1;
    return NULL;
}

sj_node *sj_parse(const char *text, size_t len)
{
    sj_parser ps = { text, text + len, 0, 0 };
    sj_node *root = parse_value(&ps);
    skip_ws(&ps);
    /* Tolerate trailing NULs (ENet payloads sometimes include the terminator). */
    while (ps.p < ps.end && *ps.p == '\0') ++ps.p;
    if (ps.failed || ps.p != ps.end) { sj_free(root); return NULL; }
    return root;
}

void sj_free(sj_node *node)
{
    while (node) {
        sj_node *next = node->next;
        sj_free(node->child);
        free(node->string);
        free(node->key);
        free(node);
        node = next;
    }
}

const sj_node *sj_get(const sj_node *object, const char *key)
{
    if (!object || object->type != SJ_OBJECT) return NULL;
    for (const sj_node *c = object->child; c; c = c->next)
        if (c->key && strcmp(c->key, key) == 0) return c;
    return NULL;
}

const sj_node *sj_at(const sj_node *array, int index)
{
    if (!array || (array->type != SJ_ARRAY && array->type != SJ_OBJECT)) return NULL;
    const sj_node *c = array->child;
    while (c && index-- > 0) c = c->next;
    return c;
}

const char *sj_get_str(const sj_node *object, const char *key, const char *fallback)
{
    const sj_node *n = sj_get(object, key);
    return n && n->type == SJ_STRING && n->string ? n->string : fallback;
}

double sj_get_num(const sj_node *object, const char *key, double fallback)
{
    const sj_node *n = sj_get(object, key);
    return n && n->type == SJ_NUMBER ? n->number : fallback;
}

int sj_get_bool(const sj_node *object, const char *key, int fallback)
{
    const sj_node *n = sj_get(object, key);
    return n && n->type == SJ_BOOL ? n->boolean : fallback;
}

/* ---------------------------------------------------------------- writer */
void sj_buf_init(sj_buf *b) { memset(b, 0, sizeof(*b)); }
void sj_buf_free(sj_buf *b) { free(b->data); memset(b, 0, sizeof(*b)); }

void sj_rawn(sj_buf *b, const char *text, size_t n)
{
    if (b->failed) return;
    if (b->length + n + 1 > b->capacity) {
        size_t cap = b->capacity ? b->capacity : 256;
        while (cap < b->length + n + 1) cap *= 2;
        char *d = (char *)realloc(b->data, cap);
        if (!d) { b->failed = 1; return; }
        b->data = d;
        b->capacity = cap;
    }
    memcpy(b->data + b->length, text, n);
    b->length += n;
    b->data[b->length] = '\0';
}

void sj_raw(sj_buf *b, const char *text) { sj_rawn(b, text, strlen(text)); }

void sj_string(sj_buf *b, const char *text)
{
    sj_raw(b, "\"");
    for (const unsigned char *p = (const unsigned char *)text; p && *p; ++p) {
        char esc[8];
        switch (*p) {
            case '"': sj_raw(b, "\\\""); break;
            case '\\': sj_raw(b, "\\\\"); break;
            case '\n': sj_raw(b, "\\n"); break;
            case '\r': sj_raw(b, "\\r"); break;
            case '\t': sj_raw(b, "\\t"); break;
            case '\b': sj_raw(b, "\\b"); break;
            case '\f': sj_raw(b, "\\f"); break;
            default:
                if (*p < 0x20) { snprintf(esc, sizeof(esc), "\\u%04x", *p); sj_raw(b, esc); }
                else sj_rawn(b, (const char *)p, 1);
        }
    }
    sj_raw(b, "\"");
}

void sj_int(sj_buf *b, long long value)
{
    char tmp[32];
    snprintf(tmp, sizeof(tmp), "%lld", value);
    sj_raw(b, tmp);
}

void sj_key(sj_buf *b, const char *key, int *first)
{
    if (!*first) sj_raw(b, ",");
    *first = 0;
    sj_string(b, key);
    sj_raw(b, ":");
}
