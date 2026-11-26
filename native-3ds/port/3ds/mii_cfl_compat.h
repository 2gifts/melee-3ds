/* Force-included when building cfl-mii (tools/build_game.py) so the submodule stays as it is: cfl-mii passes plain
 * integers as C3D_DEPTHTYPE, which relies on GCC's transparent union; clang with -fshort-enums ignores that attribute
 * (renderqueue.h's warning), so wrap the integer here as the port's own code does ((C3D_DEPTHTYPE){.__i=...}). */
#ifndef MII_CFL_COMPAT_H
#define MII_CFL_COMPAT_H
#include <3ds.h>
#include <citro3d.h>
/* cfl-mii prints its errors (no Mii Maker database, the recently-seen write turned down, ...) with vprintf, which
 * libctru's console draws over the game's bottom screen and scrolls it: into the game's log instead (mii_icon.c). */
#include <stdarg.h>
#include <stdio.h>
int mp_cfl_vprintf(const char *fmt, va_list ap);
#define vprintf(fmt, ap) mp_cfl_vprintf((fmt), (ap))
#define printf(...) mp_cfl_printf(__VA_ARGS__)
int mp_cfl_printf(const char *fmt, ...);
#define C3D_RenderTargetCreateFromTex(tex, face, level, depth) \
    C3D_RenderTargetCreateFromTex((tex), (face), (level), (C3D_DEPTHTYPE){.__i = (int)(depth)})
#endif
