/* Minimal JSON reader/writer for the Slippi matchmaking protocol and user.json.
 * Platform-neutral C (3DS and the Windows test tools). */
#ifndef SLIPPI_JSON_H
#define SLIPPI_JSON_H
#include <stddef.h>

typedef enum { SJ_NULL, SJ_BOOL, SJ_NUMBER, SJ_STRING, SJ_ARRAY, SJ_OBJECT } sj_type;

typedef struct sj_node {
    sj_type type;
    int boolean;
    double number;
    char *string;          /* SJ_STRING: UTF-8, NUL-terminated (may contain no NULs) */
    char *key;             /* member name when the parent is an object */
    int count;             /* children of an array/object */
    struct sj_node *child; /* first child */
    struct sj_node *next;  /* next sibling */
} sj_node;

/* Parses text (len bytes, need not be NUL-terminated). NULL on error. */
sj_node *sj_parse(const char *text, size_t len);
void sj_free(sj_node *node);

const sj_node *sj_get(const sj_node *object, const char *key);
const sj_node *sj_at(const sj_node *array, int index);
/* Typed lookups with defaults (wrong type -> default). */
const char *sj_get_str(const sj_node *object, const char *key, const char *fallback);
double sj_get_num(const sj_node *object, const char *key, double fallback);
int sj_get_bool(const sj_node *object, const char *key, int fallback);

/* Growable output buffer. */
typedef struct {
    char *data;
    size_t length, capacity;
    int failed;
} sj_buf;
void sj_buf_init(sj_buf *b);
void sj_buf_free(sj_buf *b);
void sj_raw(sj_buf *b, const char *text);
void sj_rawn(sj_buf *b, const char *text, size_t n);
void sj_string(sj_buf *b, const char *text);   /* quoted and escaped */
void sj_int(sj_buf *b, long long value);
/* Helpers for "key": value inside an object; they add a comma unless first. */
void sj_key(sj_buf *b, const char *key, int *first);

#endif
