#include "melee_port/input.h"
#include <string.h>

static s8 axis(int n)
{
    /* libctru's nominal Circle Pad range is +/-156. Melee's PAD processing
     * then clamps to 80. Do not apply another gameplay deadzone here. */
    if (n < -156) n = -156;
    if (n > 156) n = 156;
    return (s8)(n * 80 / 156);
}

void mp_input_map(const MpInput *in, PADStatus out[4])
{
    static const struct { unsigned bit; u16 gc; } map[] = {
        {0, PAD_BUTTON_A}, {1, PAD_BUTTON_B}, {3, PAD_BUTTON_START},
        {4, PAD_BUTTON_RIGHT}, {5, PAD_BUTTON_LEFT},
        {6, PAD_BUTTON_UP}, {7, PAD_BUTTON_DOWN},
        {8, PAD_TRIGGER_R}, {9, PAD_TRIGGER_L},
        {10, PAD_BUTTON_X}, {11, PAD_BUTTON_Y},
        {14, PAD_TRIGGER_Z}, {15, PAD_TRIGGER_Z}
    };
    memset(out, 0, sizeof(*out) * 4);
    for (int i = 1; i < 4; ++i) out[i].err = PAD_ERR_NO_CONTROLLER;
    for (size_t i = 0; i < sizeof(map)/sizeof(map[0]); ++i)
        if (in->held & (1u << map[i].bit)) out[0].button |= map[i].gc;
    out[0].stickX = axis(in->circle_x);
    out[0].stickY = axis(in->circle_y);
    out[0].substickX = axis(in->cstick_x);
    out[0].substickY = axis(in->cstick_y);
    out[0].triggerLeft = (out[0].button & PAD_TRIGGER_L) ? 255 : 0;
    out[0].triggerRight = (out[0].button & PAD_TRIGGER_R) ? 255 : 0;
    out[0].analogA = (out[0].button & PAD_BUTTON_A) ? 255 : 0;
    out[0].analogB = (out[0].button & PAD_BUTTON_B) ? 255 : 0;
}
