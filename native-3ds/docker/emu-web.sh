#!/bin/sh
# Play the Melee 3DS development build in Azahar from your web browser (Docker, localhost only).
#
#   docker/emu-web.sh            # start (or restart) the container, then open http://localhost:3188
#   docker/emu-web.sh stop       # stop and remove it
#   docker/emu-web.sh logs       # container log (add -f to follow)
#
# How it works: the stock linuxserver/azahar image is a Selkies web desktop (labwc + Azahar streamed to the
# browser by Selkies). We replace its labwc autostart (docker/emu-web-session.sh, mounted read-only) so the
# desktop session starts Azahar directly on melee-development.3dsx (OpenGL renderer by default) and a profile whose
# SD card holds sdmc:/3ds/melee/slippi/group.ini (menu=1, udp=1, port=44100), config.ini (prefetch=0) and
# no-scripted-input (the development build presses nothing by itself and never stops on its own).
# The work volume (disc extract, build) is mounted READ-ONLY; nothing is copied or uploaded. The port is bound
# to 127.0.0.1 only. The emulator profile and saves live in the container and vanish with it.
#
# Controls (click the browser page first so it captures the keyboard; the stream window is the whole desktop):
#   Touch screen (bottom screen)   mouse: click / drag on the bottom screen
#   Circle pad                     arrow keys (hold Shift for half tilt)
#   D-pad                          T = up, G = down, F = left, H = right
#   A  B  X  Y                     A  S  Z  X
#   L  R                           Q  W
#   Start / Select                 M / N
#   ZL / ZR (New 3DS)              1 / 2
# Rebind with MELEE_KEYS, a comma list of BUTTON=KEY, KEY being a letter/digit or Left Right Up Down Space Return
# Tab Shift, e.g.  MELEE_KEYS="A=Space,B=Left" docker/emu-web.sh   (buttons: A B X Y L R START SELECT UP DOWN
# LEFT RIGHT ZL ZR CUP CDOWN CLEFT CRIGHT SUP SDOWN SLEFT SRIGHT, where UP..RIGHT is the D-pad, C* the circle pad and S* the C-stick)
#
# Environment: MELEE_VOLUME (work volume, default melee3ds-slippi-work), MELEE_WEB_PORT (default 3188),
# MELEE_WEB_NAME (container name, default melee3ds-emu-web), MELEE_GRAPHICS (software|opengl, default opengl: the software renderer is about 40x slower under emulation here),
# MELEE_CFL_RES (your console's CFL_Res.dat: installed as the Mii resource title, so Mii heads render with real face
# parts instead of Azahar's stand-in; see docker/cfl_res.sh), MELEE_WEB_RES (WxH of the desktop stream, default 1024x768), MELEE_KEYS (above), MELEE_WEB_DSX (a .3dsx on this
# machine to run instead of the development build, e.g. the release build, to compare speeds), MELEE_WEB_GDB=1 (open
# Azahar's GDB stub inside the container, to read the game's frame counter), MELEE_WEB_INSTANCES=2 (a second console in
# the same desktop with its own saves, so two consoles can host and join each other over the loopback link; console A
# opens in the right half of the desktop, console B in the left; give MELEE_WEB_RES=1600x900 for two comfortable windows).
# Console A's user name (the game's Mii name) is Alice, console B's Bob (emu-web-session.sh sets them at every start).
# docker/emu-web-hosts.sh adds PC hosts with Miis to the consoles' Available Games lists.
set -eu
IMG=linuxserver/azahar:latest
NAME=${MELEE_WEB_NAME:-melee3ds-emu-web}
PORT=${MELEE_WEB_PORT:-3188}
VOLUME=${MELEE_VOLUME:-melee3ds-slippi-work}
RES=${MELEE_WEB_RES:-1024x768}
HERE=$(cd "$(dirname "$0")" && pwd)

case "${1:-start}" in
    stop) docker rm -f "$NAME" >/dev/null 2>&1 || true; echo "Stopped $NAME."; exit 0 ;;
    logs) shift; exec docker logs "$@" "$NAME" ;;
    start) ;;
    *) sed -n '2,6p' "$0" | sed 's/^# \{0,1\}//' >&2; exit 2 ;;
esac

. "$HERE/cfl_res.sh"
CFL_RES_CIA=$(cfl_res_cia "$VOLUME" "$(cd "$HERE/.." && pwd)")
DSX_IN=/work/src/dist/3ds/melee/melee-development.3dsx
[ -z "${MELEE_WEB_DSX:-}" ] || DSX_IN=/vanilla.3dsx
docker rm -f "$NAME" >/dev/null 2>&1 || true
docker run -d --name "$NAME" --platform linux/amd64 --shm-size=1gb \
    -p "127.0.0.1:$PORT:3000" \
    -e PUID=1000 -e PGID=1000 -e TZ=UTC \
    -e SELKIES_MANUAL_WIDTH="${RES%x*}" -e SELKIES_MANUAL_HEIGHT="${RES#*x}" \
    -e MELEE_GRAPHICS="${MELEE_GRAPHICS:-opengl}" -e MELEE_KEYS="${MELEE_KEYS:-}" \
    -e MELEE_GAME_FILES=/work/src/dist/native-alpha/3ds/melee/files \
    -e MELEE_SLIPPI_FILES=/work/src/dist/native-alpha/3ds/melee/slippi/files \
    -e MELEE_DSX="$DSX_IN" \
    -e MELEE_WEB_GDB="${MELEE_WEB_GDB:-}" -e MELEE_WEB_INSTANCES="${MELEE_WEB_INSTANCES:-1}" \
    ${MELEE_WEB_DSX:+-v "$MELEE_WEB_DSX":/vanilla.3dsx:ro} \
    ${CFL_RES_CIA:+-v "$CFL_RES_CIA":/cfl-res.cia:ro -e MELEE_CFL_RES_CIA=/cfl-res.cia} \
    -v "$HERE/emu-web-session.sh":/defaults/autostart:ro \
    -v "$HERE/emu-web-session.sh":/defaults/autostart_wayland:ro \
    -v "$VOLUME":/work:ro \
    "$IMG" >/dev/null
echo "Started $NAME. Open http://localhost:$PORT (the game takes about a minute to boot under emulation)."
