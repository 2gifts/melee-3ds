#ifndef MP_BOTTOM_H
#define MP_BOTTOM_H
#include "bottom_state.h"
#include <stdint.h>
enum { MP_BOTTOM_FPS = 1, MP_BOTTOM_VIEW = 2, MP_BOTTOM_GUIDE = 3 };
void mp_bottom_font_init(const unsigned char* font);
int mp_bottom_art_init(const unsigned char* font, const char* css_path, const char* hud_path);
void mp_bottom_art_exit(void);
void mp_bottom_draw(uint16_t* pixels, const MPBottomState* state,
                    unsigned fps, unsigned show_fps, unsigned expanded, unsigned guide);
void mp_bottom_loading(uint16_t* pixels, const char* message);
unsigned mp_bottom_hit(unsigned x, unsigned y);
void mp_native_bottom_init(void);
void mp_native_bottom_frame(unsigned fps, unsigned expanded, unsigned touch, unsigned x, unsigned y);
void mp_native_bottom_exit(void);
extern const uint16_t* mp_native_bottom_pixels;
#endif
