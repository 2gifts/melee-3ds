#ifndef MP_BOTTOM_H
#define MP_BOTTOM_H
#include "bottom_state.h"
#include <stdint.h>
enum { MP_BOTTOM_FPS = 1, MP_BOTTOM_VIEW = 2, MP_BOTTOM_GUIDE = 3, MP_BOTTOM_RATE = 4, MP_BOTTOM_TAP_JUMP = 5 };
void mp_bottom_font_init(const unsigned char* font);
int mp_bottom_art_init(const unsigned char* font, const char* css_path, const char* hud_path);
void mp_bottom_art_exit(void);
/* rate: frame-rate mode*2 + (capped at 30), see frame_rate.h. */
void mp_bottom_draw(uint16_t* pixels, const MPBottomState* state,
                    unsigned fps, unsigned show_fps, unsigned expanded, unsigned rate, unsigned guide);
void mp_bottom_loading(uint16_t* pixels, const char* message);
unsigned mp_bottom_hit(unsigned x, unsigned y);
void mp_native_bottom_init(void);
void mp_native_bottom_art(void);
void mp_native_bottom_frame(unsigned fps, unsigned expanded, unsigned rate, unsigned touch, unsigned x, unsigned y);
void mp_native_bottom_exit(void);
extern const uint16_t* mp_native_bottom_pixels;
#endif
