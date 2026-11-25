/* Host tests for the name converter (port/3ds/name_text.c): names in the game's text font. Run by
 * tools/test_name_text.py. */
#include "../port/3ds/name_text.h"
#include <stdio.h>
#include <string.h>

static int failures;
#define CHECK(c) do { if (!(c)) { printf("name_text: FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)

/* name_to_game's output as hex, for comparing */
static const char *game_hex(const char *utf8, int len)
{
    static char hex[256];
    char out[128];
    int n = name_to_game(utf8, out, len), i;
    hex[0] = 0;
    for (i = 0; i < n; ++i) snprintf(hex + 2 * i, 3, "%02X", (unsigned char)out[i]);
    return hex;
}

static int count_chars(const char *s)
{
    int n = 0;
    while (name_utf8_next(&s)) ++n;
    return n;
}

int main(void)
{
    char out[64];
    /* ASCII is passed as the SIS text has always taken it */
    CHECK(!strcmp(game_hex("Anna", 64), "416E6E61"));
    CHECK(!strcmp(game_hex("a!b", 64), "618149" "62"));               /* "!" full-width, as sis() did */
    /* kana */
    CHECK(!strcmp(game_hex("\xE3\x82\x86\xE3\x81\x86\xE3\x81\x8D", 64), "82E482A482AB"));   /* ゆうき */
    CHECK(!strcmp(game_hex("\xE3\x83\x9E\xE3\x83\xAA\xE3\x82\xAA", 64), "837D838A8349"));   /* マリオ */
    /* kanji in the font, and not */
    CHECK(!strcmp(game_hex("\xE8\xAA\xAC\xE6\x98\x8E", 64), "90E096BE"));   /* 説明 */
    CHECK(!strcmp(game_hex("\xE6\xBC\xA2\xE5\xAD\x97", 64), "81488148"));   /* 漢字 -> ？？ */
    /* accented Latin folds to the base letters */
    name_to_game("Zo\xC3\xAB \xC3\x85ngstr\xC3\xB6m", out, sizeof(out));
    CHECK(!strcmp(out, "Zoe Angstrom"));
    name_to_game("Stra\xC3\x9F" "e", out, sizeof(out));
    CHECK(!strcmp(out, "Strasse"));
    /* half-width katakana -> full-width */
    CHECK(!strcmp(game_hex("\xEF\xBE\x8F\xEF\xBE\x98\xEF\xBD\xB5", 64), "837D838A8349"));   /* ﾏﾘｵ -> マリオ */
    /* an emoji, invalid UTF-8 (no read past the end) */
    CHECK(!strcmp(game_hex("\xF0\x9F\x98\x80", 64), "8148"));
    CHECK(!strcmp(game_hex("\xC3", 64), "8148"));
    CHECK(!strcmp(game_hex("\xE3\x81", 64), "81488148"));                /* two invalid bytes: one "？" each */
    CHECK(!strcmp(game_hex("", 64), ""));
    /* the output buffer: a character that does not fit is left out, never cut, never written past */
    {
        char small[6];
        memset(small, 0x55, sizeof(small));
        int n = name_to_game("\xE3\x81\x82\xE3\x81\x82\xE3\x81\x82", small, 5);   /* あああ: 2 fit in 5 with the NUL */
        CHECK(n == 4 && small[4] == 0 && (unsigned char)small[5] == 0x55);
    }
    /* name_clean: 10 characters, whole ones only, controls out, invalid bytes dropped */
    {
        const char *ten = "\xE3\x82\x86\xE3\x81\x86\xE3\x81\x8D\xE3\x82\x86\xE3\x81\x86\xE3\x81\x8D"
                          "\xE3\x82\x86\xE3\x81\x86\xE3\x81\x8D\xE3\x82\x86";   /* ゆうきゆうきゆうきゆ */
        char eleven[64];
        snprintf(eleven, sizeof(eleven), "%s\xE3\x81\x82", ten);
        CHECK(name_clean(ten, out, sizeof(out), 10) == 30 && !strcmp(out, ten));
        CHECK(name_clean(eleven, out, sizeof(out), 10) == 30 && !strcmp(out, ten));
        CHECK(name_clean(ten, out, 32, 10) == 30);                      /* LW_NAME_MAX holds 10 kana */
        CHECK(name_clean(ten, out, 8, 10) == 6 && count_chars(out) == 2);   /* cut between characters */
        CHECK(name_clean("A\tB\x01" "C\xC3", out, sizeof(out), 10) == 3 && !strcmp(out, "ABC"));
        CHECK(name_clean("Zo\xC3\xAB \xC3\x85ngstr\xC3\xB6m", out, sizeof(out), 10) == 12 && count_chars(out) == 10);
        CHECK(name_clean(NULL, out, sizeof(out), 10) == 0 && out[0] == 0);
    }
    /* UTF-16 (a 3DS or Mii name) to UTF-8 */
    {
        const uint16_t yuki[10] = {0x3086, 0x3046, 0x304D, 0x3086, 0x3046, 0x304D, 0x3086, 0x3046, 0x304D, 0x3086};
        const uint16_t anna[10] = {'A', 'n', 'n', 'a', 0};
        const uint16_t emoji[4] = {0xD83D, 0xDE00, 'x', 0};
        CHECK(name_from_utf16(yuki, 10, out, 32, 10) == 30 && count_chars(out) == 10);
        CHECK(name_from_utf16(anna, 10, out, 32, 10) == 4 && !strcmp(out, "Anna"));
        CHECK(name_from_utf16(emoji, 4, out, 32, 10) == 5 && !strcmp(out, "\xF0\x9F\x98\x80x"));
    }
    /* the atlas: a kana's glyph, a fold's, and "？" for the rest */
    CHECK(name_to_atlas(0x3086) == 131);                                  /* ゆ */
    CHECK(name_to_atlas(0xFF8F) == name_to_atlas(0x30DE));               /* ﾏ -> マ */
    CHECK(name_to_atlas(0x6F22) == name_to_atlas(0xFF1F) && name_to_atlas(0xFF1F) >= 0);   /* 漢 -> ？ */
    CHECK(name_to_atlas(0x3099) == -1);
    if (failures) return 1;
    printf("name_text: ok\n");
    return 0;
}
