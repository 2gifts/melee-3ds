#include <stdio.h>
#include <string.h>
#include "controls.h"
/* Player options kept between launches, shared by both builds:
 *   sdmc:/3ds/melee/settings.txt
 *     tap_jump=0 or 1
 *     button_zl=jump   (one line per button; see controls.c for the names)
 * Read once at startup; written in the background when changed. Unknown
 * lines are ignored, so older and newer builds can share the file. */
#define MP_SETTINGS_PATH "sdmc:/3ds/melee/settings.txt"
extern void mp_native_log(const char* text);
extern int mp_native_file_write_async(const char* path, const void* src, unsigned size);

static unsigned tap_jump = 1;

unsigned mp_native_tap_jump(void) { return tap_jump; }

void mp_native_settings_load(void) {
    FILE* f = fopen(MP_SETTINGS_PATH, "r");
    if (!f)
        return;
    char line[64], button[16], action[16];
    unsigned value;
    while (fgets(line, sizeof(line), f)) {
        if (sscanf(line, "tap_jump=%u", &value) == 1)
            tap_jump = !!value;
        else if (sscanf(line, "button_%15[a-z]=%15[a-z]", button, action) == 2)
            mp_native_load_button(button, action);
    }
    fclose(f);
    if (!tap_jump)
        mp_native_log("Settings: tap jump off\n");
    char text[256];
    int n = snprintf(text, sizeof(text), "Settings: custom buttons");
    unsigned custom = 0;
    for (unsigned b = 0; b < MP_BUTTONS; ++b)
        if (mp_native_button_action(b) != mp_native_default_action(b) && n > 0 && (size_t) n < sizeof(text)) {
            n += snprintf(text + n, sizeof(text) - n, " %s=%s", mp_button_keys[b], mp_action_keys[mp_native_button_action(b)]);
            ++custom;
        }
    if (custom && n > 0 && (size_t) n < sizeof(text) - 1) {
        text[n] = '\n';
        text[n + 1] = 0;
        mp_native_log(text);
    }
}

void mp_native_settings_save(void) {
    char text[512];
    int n = snprintf(text, sizeof(text), "tap_jump=%u\n", tap_jump);
    for (unsigned b = 0; b < MP_BUTTONS && n > 0 && (size_t) n < sizeof(text); ++b)
        n += snprintf(text + n, sizeof(text) - n, "button_%s=%s\n", mp_button_keys[b],
                      mp_action_keys[mp_native_button_action(b)]);
    if (n > 0 && (size_t) n < sizeof(text))
        mp_native_file_write_async(MP_SETTINGS_PATH, text, (unsigned) n);
}

void mp_native_set_tap_jump(unsigned enabled) {
    tap_jump = !!enabled;
    mp_native_settings_save();
    mp_native_log(tap_jump ? "Settings: tap jump on\n" : "Settings: tap jump off\n");
}
