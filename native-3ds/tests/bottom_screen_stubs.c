/* What tests/bottom_screen.c needs besides port/3ds/bottom_draw.c: the controls, Slippi and group functions the pages
 * call, as stand-ins. The group ones return fixed data a test sets (bottom_stub_* below), so the Available Games list,
 * the strip and the waiting room can be drawn on the host. */
#include "bottom_screen_stubs.h"
#include <string.h>

/* controls.c */
const char* const mp_button_labels[MP_BUTTONS] = {"A", "B", "X", "Y", "L", "R", "ZL", "ZR",
                                                  "D-UP", "D-DOWN", "D-LEFT", "D-RIGHT"};
const char* const mp_action_labels[MP_ACTIONS] = {"NONE", "ATTACK", "SPECIAL", "JUMP", "SHIELD", "GRAB", "TAUNT"};
unsigned mp_native_button_action(unsigned button) { (void)button; return 0; }
unsigned mp_native_default_action(unsigned button) { (void)button; return 0; }
void mp_native_set_button_action(unsigned button, unsigned action) { (void)button; (void)action; }
void mp_native_reset_buttons(void) {}
volatile unsigned mp_native_camera_mode, mp_native_camera_control;
unsigned mp_native_tap_jump(void) { return 1; }
void mp_native_set_tap_jump(unsigned on) { (void)on; }

/* slippi_ui.c: never on a Slippi page here */
const char slippi_ui_keys[SLIPPI_KEYS + 1] = "";
static SlippiUiView slippi_view;
const SlippiUiView* slippi_ui_view(void) { return &slippi_view; }
int slippi_ui_alt(int toggle) { (void)toggle; return 0; }
void slippi_ui_cancel(void) {}
const char *slippi_ui_chat_message(int index) { (void)index; return ""; }
void slippi_ui_clear_history(void) {}
void slippi_ui_close_chat(void) {}
void slippi_ui_close_settings(void) {}
int slippi_ui_local_port(void) { return -1; }
void slippi_ui_open_chat(void) {}
void slippi_ui_open_settings(void) {}
void slippi_ui_press_start(void) {}
void slippi_ui_send_chat(int index) { (void)index; }
void slippi_ui_set_delay(int delta) { (void)delta; }
void slippi_ui_toggle_chat(void) {}
void slippi_ui_toggle_zelda(void) {}
void slippi_ui_type(int key) { (void)key; }
void slippi_ui_use_suggestion(void) {}

/* group_menu.c: what the test set */
GroupView bottom_stub_view;
GroupStrip bottom_stub_strip;
int bottom_stub_strip_on, bottom_stub_home_note;
const GroupView *group_view(void) { return &bottom_stub_view; }
int group_strip(GroupStrip *out)
{
    if (!bottom_stub_strip_on) { memset(out, 0, sizeof(*out)); return 0; }
    *out = bottom_stub_strip;
    return 1;
}
int group_home_note(void) { return bottom_stub_home_note; }
int group_dialog(void) { return 0; }
void group_dialog_answer(int yes) { (void)yes; }
void group_back(void) {}
void group_host(void) {}
void group_join_row(int row) { (void)row; }
void group_note_touch(unsigned x, unsigned y) { (void)x; (void)y; }
void group_pick_group(void) {}
void group_pick_solo(void) {}
