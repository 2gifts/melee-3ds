#ifndef MELEE_PORT_INPUT_H
#define MELEE_PORT_INPUT_H
#include <dolphin/pad.h>
typedef struct {
    uint32_t held;
    int circle_x, circle_y, cstick_x, cstick_y;
} MpInput;
void mp_input_map(const MpInput *in, PADStatus out[4]);
#endif
