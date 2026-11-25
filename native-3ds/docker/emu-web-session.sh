#!/bin/bash
# labwc autostart for docker/emu-web.sh (mounted over the image's /defaults/autostart[_wayland], which the image
# copies to ~/.config/labwc/autostart; the image runs Selkies on a Wayland labwc desktop). Runs as the abc user.
set -eu
# setup CONFIG_HOME DATA_HOME: one emulator profile (its own saves, same SD layout and config).
setup() {
export XDG_CONFIG_HOME=$1 XDG_DATA_HOME=$2
CFG=$1/azahar-emu
DATA=$2/azahar-emu
export CFG
SD=$DATA/sdmc/3ds/melee
mkdir -p "$CFG" "$SD/slippi" "$DATA/log"
ln -sfn "$MELEE_GAME_FILES" "$SD/files"
[ ! -d "$MELEE_SLIPPI_FILES" ] || ln -sfn "$MELEE_SLIPPI_FILES" "$SD/slippi/files"
printf 'prefetch=0\ndelay=2\n' > "$SD/slippi/config.ini"
printf 'menu=1\nudp=1\nport=44100\ndelay=2\n' > "$SD/slippi/group.ini"
: > "$SD/no-scripted-input"   # the development build's scripted presses and automatic stop off

python3 - <<'PY'
import os, re
keys = dict(A='A', B='S', X='Z', Y='X', L='Q', R='W', START='M', SELECT='N', UP='T', DOWN='G', LEFT='F', RIGHT='H',
            ZL='1', ZR='2', CUP='Up', CDOWN='Down', CLEFT='Left', CRIGHT='Right',
            SUP='I', SDOWN='K', SLEFT='J', SRIGHT='L')
for item in filter(None, os.environ.get('MELEE_KEYS', '').split(',')):
    k, v = item.split('=')
    keys[k.strip().upper()] = v.strip()
named = dict(Left=0x1000012, Up=0x1000013, Right=0x1000014, Down=0x1000015, Space=32, Return=0x1000004,
             Tab=0x1000001, Shift=0x1000020)
code = lambda n: named[n.capitalize()] if n.capitalize() in named else ord(n.upper())
kb = lambda n: f'"code:{code(keys[n])},engine:keyboard"'
text = open('/defaults/qt-config.ini', encoding='utf-8').read()

def put(key, value):
    global text
    text = re.sub(rf'^{re.escape(key)}=.*$', lambda m: f'{key}={value}', text, flags=re.M)
    text = re.sub(rf'^{re.escape(key)}\\default=.*$', lambda m: f'{key}\\default=false', text, flags=re.M)
for button, name in dict(a='A', b='B', x='X', y='Y', l='L', r='R', start='START', select='SELECT', up='UP', down='DOWN',
                         left='LEFT', right='RIGHT', zl='ZL', zr='ZR').items():
    put(rf'profiles\1\button_{button}', kb(name))
def stick(prefix):
    enc = lambda n: f'code$0{code(keys[prefix + n])}$1engine$0keyboard'
    return ('"' + f'down:{enc("DOWN")},engine:analog_from_button,left:{enc("LEFT")},modifier:code$0{0x1000020}$1engine$0keyboard,'
            f'modifier_scale:0.500000,right:{enc("RIGHT")},up:{enc("UP")}' + '"')
put(r'profiles\1\circle_pad', stick('C'))
put(r'profiles\1\c_stick', stick('S'))
g = {'software': 0, 'opengl': 1}[os.environ.get('MELEE_GRAPHICS', 'software')]
put('graphics_api', g)
put('use_vsync_new', 'false')
put('firstStart', 'false')
if os.environ.get('MELEE_WEB_GDB'):
    put('use_gdbstub', 'true')
    put('gdbstub_port', '24861')   # reachable inside the container only (speed measurements)
put('confirmClose', 'false')
put('check_for_update_on_start', 'false')
open(os.path.join(os.environ['CFG'], 'qt-config.ini'), 'w', encoding='utf-8').write(text)
PY
# MELEE_CFL_RES (emu-web.sh): the console's Mii resource as a system title, installed into this profile's NAND.
if [ -n "${MELEE_CFL_RES_CIA:-}" ]; then
    QT_QPA_PLATFORM=offscreen /usr/sbin/azahar -i "$MELEE_CFL_RES_CIA" >"$DATA/log/cfl-res-install.txt" 2>&1 || true
fi
# The console's user name ($3), written into the emulated NAND's CFG config savegame (system save 0x00010017, block
# 0x000A0000) on every start. Azahar has no setting for it: FRD GetMyScreenName (the game's "Mii name") and the CFG
# system name both return this block. (FRD GetMyMii's MiiData name is hardcoded to "Citra" in Azahar.) A missing
# config file is created with just this block and the console unique IDs (random, as Azahar's FormatConfig does);
# Azahar adds every other block from its defaults on first use.
CONFIG_SAVE=$DATA/nand/data/00000000000000000000000000000000/sysdata/00010017/00000000/config \
USER_NAME=$3 python3 - <<'PY'
import os, random, struct
path, name = os.environ['CONFIG_SAVE'], os.environ['USER_NAME'][:10]
SIZE, DATA_START, ENTRIES = 0x8000, 0x455C, 1479
GLOBAL = 0xE   # UserRead | SystemRead | SystemWrite
try:
    buf = bytearray(open(path, 'rb').read())
except FileNotFoundError:
    buf = bytearray()
if len(buf) != SIZE:
    buf = bytearray(SIZE)
    struct.pack_into('<HH', buf, 0, 0, DATA_START)
entries = lambda: [struct.unpack_from('<IIHH', buf, 4 + 12 * i) for i in range(struct.unpack_from('<H', buf, 0)[0])]

def put(block, data):
    for i, (bid, off, size, _) in enumerate(entries()):
        if bid == block:
            assert size == len(data), (hex(block), size)
            if size <= 4:   # small blocks live in the entry itself
                struct.pack_into('<4s', buf, 4 + 12 * i + 4, data)
            else:
                buf[off:off + size] = data
            return
    n = len(entries())
    assert n < ENTRIES
    off = next((o + s for _, o, s, _ in reversed(entries()) if s > 4), DATA_START)
    if len(data) > 4:
        buf[off:off + len(data)] = data
        struct.pack_into('<IIHH', buf, 4 + 12 * n, block, off, len(data), GLOBAL)
    else:
        struct.pack_into('<I4sHH', buf, 4 + 12 * n, block, data, len(data), GLOBAL)
    struct.pack_into('<H', buf, 0, n + 1)

if not entries():
    rnd = random.getrandbits(16)
    console_id = random.getrandbits(34) | rnd << 48
    put(0x00090000, struct.pack('<Q', console_id))
    put(0x00090001, struct.pack('<Q', console_id))
    put(0x00090002, struct.pack('<I', rnd))
put(0x000A0000, name.encode('utf-16-le').ljust(20, b'\0') + bytes(8))   # UsernameBlock: 10 UTF-16 chars, zero, ng_word
os.makedirs(os.path.dirname(path), exist_ok=True)
open(path, 'wb').write(buf)
PY
}

setup "$HOME/.config" "$HOME/.local/share" Alice
# Where the next Azahar window opens: a labwc window rule (x, y, width, height), applied when the window is created.
place() {
    mkdir -p "$HOME/.config/labwc"
    cat > "$HOME/.config/labwc/rc.xml" <<XML
<?xml version="1.0"?>
<labwc_config>
  <windowRules>
    <windowRule identifier="*zahar*">
      <action name="ResizeTo" width="$3" height="$4"/>
      <action name="MoveTo" x="$1" y="$2"/>
    </windowRule>
  </windowRules>
</labwc_config>
XML
    pkill -HUP labwc 2>/dev/null || true
    sleep 1
}
W=${SELKIES_MANUAL_WIDTH:-1024}; H=${SELKIES_MANUAL_HEIGHT:-768}
HALF=$((W / 2))
export XDG_CONFIG_HOME=$HOME/.config XDG_DATA_HOME=$HOME/.local/share
if [ "${MELEE_WEB_INSTANCES:-1}" = 2 ]; then
    # Console B in the left half, console A (this profile) in the right half. Both share one desktop and network
    # namespace, which the loopback link needs.
    place 0 0 "$HALF" "$H"
    # Console B runs MELEE_DSX_B (by default the fresh-save build: its save starts empty in every new container)
    DSX_B=${MELEE_DSX_B:-$MELEE_DSX}
    [ -f "$DSX_B" ] || { echo "emu-web: $DSX_B is missing; console B runs $MELEE_DSX" >&2; DSX_B=$MELEE_DSX; }
    ( XDG_CONFIG_HOME=$HOME/p2/config XDG_DATA_HOME=$HOME/p2/data; setup "$XDG_CONFIG_HOME" "$XDG_DATA_HOME" Bob; exec /usr/sbin/azahar "$DSX_B" ) &
    sleep 15
    place "$HALF" 0 "$HALF" "$H"
fi
export XDG_CONFIG_HOME=$HOME/.config XDG_DATA_HOME=$HOME/.local/share
exec /usr/sbin/azahar "$MELEE_DSX"
