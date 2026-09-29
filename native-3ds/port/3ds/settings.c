#include <stdio.h>
#include <string.h>
/* Player options kept between launches, shared by both builds:
 *   sdmc:/3ds/melee/settings.txt   ("tap_jump=0" or "tap_jump=1")
 * Read once at startup; written in the background when changed. */
#define MP_SETTINGS_PATH "sdmc:/3ds/melee/settings.txt"
extern void mp_native_log(const char* text);
extern int mp_native_file_write_async(const char* path, const void* src, unsigned size);

static unsigned tap_jump = 1;

unsigned mp_native_tap_jump(void) { return tap_jump; }

void mp_native_settings_load(void) {
    FILE* f = fopen(MP_SETTINGS_PATH, "r");
    if (!f)
        return;
    char line[64];
    unsigned value;
    while (fgets(line, sizeof(line), f))
        if (sscanf(line, "tap_jump=%u", &value) == 1)
            tap_jump = !!value;
    fclose(f);
    if (!tap_jump)
        mp_native_log("Settings: tap jump off\n");
}

void mp_native_set_tap_jump(unsigned enabled) {
    tap_jump = !!enabled;
    char text[32];
    int n = snprintf(text, sizeof(text), "tap_jump=%u\n", tap_jump);
    mp_native_file_write_async(MP_SETTINGS_PATH, text, (unsigned) n);
    mp_native_log(tap_jump ? "Settings: tap jump on\n" : "Settings: tap jump off\n");
}
