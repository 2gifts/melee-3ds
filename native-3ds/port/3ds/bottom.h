#ifndef MP_BOTTOM_H
#define MP_BOTTOM_H
#include "bottom_state.h"
#include <stdint.h>
enum { MP_BOTTOM_FPS = 1, MP_BOTTOM_VIEW = 2, MP_BOTTOM_GUIDE = 3, MP_BOTTOM_RATE = 4 };
void mp_bottom_font_init(const unsigned char* font);
int mp_bottom_art_init(const unsigned char* font, const char* css_path, const char* hud_path);
void mp_bottom_art_exit(void);
/* rate: frame-rate mode*2 + (capped at 30), see frame_rate.h. */
void mp_bottom_draw(uint16_t* pixels, const MPBottomState* state,
                    unsigned fps, unsigned show_fps, unsigned expanded, unsigned rate, unsigned guide);
void mp_bottom_loading(uint16_t* pixels, const char* message);
/* A full-screen message (missing game files); the hint is the last line. */
void mp_bottom_notice(uint16_t* pixels, const char* title, const char* lead, const char* line1,
                      const char* line2, const char* line3, const char* hint);
void mp_native_bottom_notice(const char* title, const char* lead, const char* line1,
                             const char* line2, const char* line3, const char* hint);
unsigned mp_bottom_hit(unsigned x, unsigned y);
/* The CONTROLS page (guide, button customization): a touch above the
 * footer, 1 if used; reset whenever the page opens or closes. */
unsigned mp_bottom_guide_touch(unsigned x, unsigned y);
void mp_bottom_guide_reset(void);
/* Slippi Direct pages (online CSS, code keyboard): touch, and whether shown. */
unsigned mp_bottom_online_touch(unsigned x, unsigned y);
unsigned mp_bottom_online_active(const MPBottomState* state);
void mp_native_bottom_init(void);
void mp_native_bottom_art(void);
void mp_native_bottom_frame(unsigned fps, unsigned expanded, unsigned rate, unsigned touch, unsigned x, unsigned y);
void mp_native_bottom_exit(void);
extern const uint16_t* mp_native_bottom_pixels;
#endif
