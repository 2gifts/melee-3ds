/* tests/bottom_screen_stubs.c: stand-ins for what bottom_draw.c calls, and the fixed group data the tests set. */
#ifndef BOTTOM_SCREEN_STUBS_H
#define BOTTOM_SCREEN_STUBS_H
#include "../port/3ds/controls.h"
#include "../port/3ds/slippi/slippi_ui.h"
#include "../port/3ds/group/group_menu.h"

extern GroupView bottom_stub_view;    /* group_view() */
extern GroupStrip bottom_stub_strip;  /* group_strip(), when bottom_stub_strip_on */
extern int bottom_stub_strip_on, bottom_stub_home_note;

extern int bottom_stub_joined, bottom_stub_turned, bottom_stub_mii_ready;
extern unsigned bottom_stub_mii_asks;
#endif
