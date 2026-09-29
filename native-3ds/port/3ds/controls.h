#ifndef MP_CONTROLS_H
#define MP_CONTROLS_H
/* Custom button mapping (bottom screen: CONTROLS > CUSTOMIZE). Each 3DS
 * button does one action during a match; menus, pauses and the Training
 * menu keep the standard layout. The Circle Pad, C-stick, START and SELECT
 * are not remappable. Saved in settings.txt (settings.c). */
enum {
    MP_BUTTON_A, MP_BUTTON_B, MP_BUTTON_X, MP_BUTTON_Y, MP_BUTTON_L, MP_BUTTON_R, MP_BUTTON_ZL, MP_BUTTON_ZR,
    MP_BUTTON_DUP, MP_BUTTON_DDOWN, MP_BUTTON_DLEFT, MP_BUTTON_DRIGHT, MP_BUTTONS
};
enum {
    MP_ACTION_NONE, MP_ACTION_ATTACK, MP_ACTION_SPECIAL, MP_ACTION_JUMP, MP_ACTION_SHIELD, MP_ACTION_GRAB,
    MP_ACTION_TAUNT, MP_ACTIONS
};
extern const char* const mp_button_labels[MP_BUTTONS]; /* "A", "ZL", "D-UP", ... */
extern const char* const mp_button_keys[MP_BUTTONS];   /* settings.txt names: "a", "zl", "dup", ... */
extern const char* const mp_action_labels[MP_ACTIONS]; /* "NONE", "ATTACK", ... */
extern const char* const mp_action_keys[MP_ACTIONS];   /* "none", "attack", ... */
unsigned mp_native_button_action(unsigned button);
unsigned mp_native_default_action(unsigned button);
void mp_native_set_button_action(unsigned button, unsigned action); /* saves */
void mp_native_reset_buttons(void);                                /* saves */
/* The GameCube buttons for held 3DS KEYS: the saved mapping in a match
 * (BATTLE), the standard layout otherwise. */
unsigned mp_native_map_keys(unsigned keys, unsigned battle);
/* settings.c */
void mp_native_load_button(const char* button, const char* action);
void mp_native_settings_save(void);
#endif
