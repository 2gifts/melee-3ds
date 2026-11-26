#!/bin/sh
# Runs inside the container. Mounts expected:
#   /input/game   your Melee US v1.02 disc image (.iso/.gcm/.ciso), read-only
#   /out          where the SD card folder and CIA files are written
#   /work         persistent work volume (toolchain, compiled objects, extracted disc)
#   /src          this repository's native-3ds folder, read-only
set -eu

SRC=/work/src
VARIANT=${MELEE_VARIANT:-main}   # main | slippi (Slippi Direct beta)
log() { printf '\n==> %s\n' "$*"; }

if [ ! -f /input/game ]; then
    echo "No disc image mounted at /input/game; use docker/build.sh <path to .iso/.ciso>" >&2
    exit 2
fi

log "Step 1/7: Syncing the builder source"
mkdir -p "$SRC"
rsync -a --delete \
    --exclude=.toolchain --exclude=build --exclude=dist --exclude=assets \
    --exclude=upstream --exclude=references --exclude=__pycache__ \
    /src/ "$SRC"/
cd "$SRC"

log "Step 2/7: Fetching the pinned compiler, SDK and source (cached in the work volume)"
python3 tools/bootstrap.py --portable-linux
python3 tools/bootstrap_home_menu.py --cia-only
python3 tools/bootstrap_home_menu.py --diorama-only \
    || echo "(The 3D banner tools did not download; the CIAs will use the 2D banner.)"

log "Step 3/7: Reading your disc image"
if [ ! -f assets/GALE01/manifest.json ]; then
    rm -rf assets/GALE01
    python3 tools/assets.py extract /input/game assets/GALE01
fi

log "Step 4/7: Extracting the game font"
F=build/generated/sysdolphin/baselib
if [ "$(wc -c < "$F/debug_font.inc" 2>/dev/null || echo 0)" -gt 1000 ] \
   && [ "$(wc -c < "$F/sislib_font.inc" 2>/dev/null || echo 0)" -gt 1000 ]; then
    echo "Font files already extracted."
else
    rm -f "$F/debug_font.inc" "$F/sislib_font.inc"
    python3 tools/assets.py fonts assets/GALE01/sys/main.dol build/generated
fi

log "Step 5/7: Compiling the game (this is the slow part)"
if [ "$VARIANT" = slippi ]; then
    python3 tools/build_game.py --release --profile slippi
else
    python3 tools/build_game.py --release
    python3 tools/build_game.py --release --profile fresh --skip-engine
fi

log "Step 6/7: Collecting the game files and making the CIAs"
python3 tools/package_native.py --link
if [ "$VARIANT" = slippi ]; then
    # Pinned like the Windows builder (tools/easy_build.py SLPCSS); never shipped in git.
    SLP=/work/downloads/slpCSS.dat
    SLP_SHA=3b1a3f254b37cf5ce4f56b38d237d9809d0b11e72d05320f0e56fa3abd7a1692
    mkdir -p /work/downloads
    if ! echo "$SLP_SHA  $SLP" | sha256sum -c - >/dev/null 2>&1; then
        curl -fsSL -o "$SLP" "https://raw.githubusercontent.com/project-slippi/dolphin/41a7a3a110ed52999486ae1901c8fbb9a63d4f13/Data/Sys/GameFiles/GALE01/slpCSS.dat"
        echo "$SLP_SHA  $SLP" | sha256sum -c -
    fi
    python3 tools/slippi/slippi_files.py assets/GALE01/files dist/native-alpha/3ds/melee/slippi/files --slpcss "$SLP"
    python3 tools/package_cia.py --generated-banner --profile slippi
else
    for profile in unlocked fresh; do
        python3 tools/package_cia.py --generated-banner --profile "$profile"
    done
fi

log "Step 7/7: Writing results to /out"
if [ "$VARIANT" = slippi ]; then
    OUT=/out/"Melee Slippi Beta for 3DS"
    rm -rf "$OUT/Copy to SD card" "$OUT/Licenses"
    mkdir -p "$OUT/Copy to SD card/3ds/melee/slippi" "$OUT/Copy to SD card/3ds/melee-slippi" "$OUT/Copy to SD card/cias" "$OUT/Licenses"
    M=dist/native-alpha/3ds/melee
    cp -r "$M/files" "$OUT/Copy to SD card/3ds/melee/files"
    cp -r "$M/slippi/files" "$OUT/Copy to SD card/3ds/melee/slippi/files"
    cp dist/native-alpha/3ds/melee-slippi/melee-slippi.3dsx "$OUT/Copy to SD card/3ds/melee-slippi/"
    cp dist/home-menu/melee-slippi-beta.cia "$OUT/Copy to SD card/cias/"
    cp dist/native-alpha/*.md dist/native-alpha/*.txt "$OUT/Licenses/" 2>/dev/null || true
    echo "Copy your Slippi user.json to SD:/3ds/melee/slippi/ yourself (see docs/slippi/SLIPPI_BETA.md)." > "$OUT/What to do next.txt"
    echo
    echo "Done. Output: $OUT"
    exit 0
fi
OUT=/out/"Melee for 3DS"
rm -rf "$OUT/Copy to SD card" "$OUT/Licenses"
mkdir -p "$OUT/Copy to SD card/3ds/melee" "$OUT/Copy to SD card/cias" "$OUT/Licenses"
M=dist/native-alpha/3ds/melee
cp "$M"/*.3dsx "$OUT/Copy to SD card/3ds/melee/"
cp -r "$M/files" "$OUT/Copy to SD card/3ds/melee/files"
cp dist/home-menu/melee-3ds.cia dist/home-menu/melee-3ds-fresh.cia "$OUT/Copy to SD card/cias/"
cp dist/native-alpha/*.md dist/native-alpha/*.txt "$OUT/Licenses/" 2>/dev/null || true
cat > "$OUT/What to do next.txt" <<'TXT'
1. Copy everything inside "Copy to SD card" to the root of your 3DS SD card
   (merge the "3ds" and "cias" folders).
2. Put the card back in the New 3DS and open FBI > SD > cias.
3. Install melee-3ds.cia (everything unlocked) and/or melee-3ds-fresh.cia.
Keep SD:/3ds/melee/files/ on the card; the CIA does not contain the game data.
TXT
echo
echo "Done. Output: $OUT"
