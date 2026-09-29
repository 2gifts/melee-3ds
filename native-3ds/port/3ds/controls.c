#include <3ds.h>
#include <stdio.h>
#include <string.h>
#include "controls.h"
extern void mp_native_log(const char* text);
/* Custom button mapping; see controls.h.
 *
 * A button keeps its original GameCube button while it does its default
 * action, so the standard layout is exactly the original one (Y is GameCube
 * Y, L is L, D-pad down still starts Fox's Star Fox taunt). A button given
 * another action sends that action's GameCube button. */
const char* const mp_button_labels[MP_BUTTONS] = {"A", "B", "X", "Y", "L", "R", "ZL", "ZR",
                                                  "D-UP", "D-DOWN", "D-LEFT", "D-RIGHT"};
const char* const mp_button_keys[MP_BUTTONS] = {"a", "b", "x", "y", "l", "r", "zl", "zr",
                                                "dup", "ddown", "dleft", "dright"};
const char* const mp_action_labels[MP_ACTIONS] = {"NONE", "ATTACK", "SPECIAL", "JUMP", "SHIELD", "GRAB", "TAUNT"};
const char* const mp_action_keys[MP_ACTIONS] = {"none", "attack", "special", "jump", "shield", "grab", "taunt"};

static const u32 keys[MP_BUTTONS] = {KEY_A, KEY_B, KEY_X, KEY_Y, KEY_L, KEY_R, KEY_ZL, KEY_ZR,
                                     KEY_DUP, KEY_DDOWN, KEY_DLEFT, KEY_DRIGHT};
/* GameCube buttons: A 0x100, B 0x200, X 0x400, Y 0x800, L 0x40, R 0x20,
 * Z 0x10, D-pad left 1, right 2, down 4, up 8 (START 0x1000 is fixed). */
static const unsigned original[MP_BUTTONS] = {0x100, 0x200, 0x400, 0x800, 0x40, 0x20, 0x10, 0x10, 8, 4, 1, 2};
static const unsigned char defaults[MP_BUTTONS] = {
    MP_ACTION_ATTACK, MP_ACTION_SPECIAL, MP_ACTION_JUMP, MP_ACTION_JUMP, MP_ACTION_SHIELD, MP_ACTION_SHIELD,
    MP_ACTION_GRAB, MP_ACTION_GRAB, MP_ACTION_TAUNT, MP_ACTION_NONE, MP_ACTION_NONE, MP_ACTION_NONE};
/* A shield press also sets its trigger fully (services.c). */
static const unsigned action_buttons[MP_ACTIONS] = {0, 0x100, 0x200, 0x400, 0x20, 0x10, 8};
static unsigned char actions[MP_BUTTONS] = {
    MP_ACTION_ATTACK, MP_ACTION_SPECIAL, MP_ACTION_JUMP, MP_ACTION_JUMP, MP_ACTION_SHIELD, MP_ACTION_SHIELD,
    MP_ACTION_GRAB, MP_ACTION_GRAB, MP_ACTION_TAUNT, MP_ACTION_NONE, MP_ACTION_NONE, MP_ACTION_NONE};

unsigned mp_native_button_action(unsigned button) { return button < MP_BUTTONS ? actions[button] : MP_ACTION_NONE; }

unsigned mp_native_default_action(unsigned button) { return button < MP_BUTTONS ? defaults[button] : MP_ACTION_NONE; }

void mp_native_set_button_action(unsigned button, unsigned action) {
    if (button >= MP_BUTTONS || action >= MP_ACTIONS || actions[button] == action)
        return;
    actions[button] = (unsigned char) action;
    mp_native_settings_save();
    char text[64];
    snprintf(text, sizeof(text), "Settings: button %s = %s\n", mp_button_keys[button], mp_action_keys[action]);
    mp_native_log(text);
}

void mp_native_reset_buttons(void) {
    if (!memcmp(actions, defaults, sizeof(actions)))
        return;
    memcpy(actions, defaults, sizeof(actions));
    mp_native_settings_save();
    mp_native_log("Settings: buttons reset to the standard layout\n");
}

void mp_native_load_button(const char* button, const char* action) {
    for (unsigned b = 0; b < MP_BUTTONS; ++b)
        if (!strcmp(button, mp_button_keys[b]))
            for (unsigned a = 0; a < MP_ACTIONS; ++a)
                if (!strcmp(action, mp_action_keys[a]))
                    actions[b] = (unsigned char) a;
}

unsigned mp_native_map_keys(unsigned held, unsigned battle) {
    unsigned buttons = held & KEY_START ? 0x1000 : 0;
    for (unsigned b = 0; b < MP_BUTTONS; ++b) {
        if (!(held & keys[b]))
            continue;
        unsigned action = battle ? actions[b] : defaults[b];
        buttons |= action == defaults[b] ? original[b] : action_buttons[action];
    }
    return buttons;
}
