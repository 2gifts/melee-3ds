#!/bin/sh
# Run the Slippi emulator tests (Azahar) inside Docker; nothing is installed on the host.
#
#   docker/emu-test.sh build-dev                  # compile the development (smoke) build into the work volume
#   docker/emu-test.sh pair [online_pair_test.py options]   # two emulated 3DSes play each other
#   docker/emu-test.sh net  [net_test.py options]            # fake-MM tests (pc-pair, pc-client, azahar DSX)
#   docker/emu-test.sh shell                      # a shell in the test container
#   docker/emu-test.sh --clean                    # remove the test image (the work volume is build.sh's)
#
# Prerequisite: docker/build.sh (with MELEE_VARIANT=slippi to match the volume below) has run once, so the
# work volume holds the toolchain and the extracted disc. build-dev then compiles the development build the
# tests need (it carries the test-input hooks; release builds do not). The first build-dev takes as long as
# a release build (an hour or more); later ones are incremental.
#
# Environment:
#   MELEE_VOLUME   work volume (default melee3ds-slippi-work, the one MELEE_VARIANT=slippi builds use)
#   MELEE_TEST_OUT where test logs and records land (default native-3ds/build/emu-test)
#   MELEE_GRAPHICS emulator renderer: software (default), opengl
#
# The test container is the stock linuxserver/azahar image (amd64 only, so it runs under Rosetta/QEMU on
# Apple Silicon). Azahar runs headless on Qt's offscreen platform with its software renderer; no desktop.
set -eu
BUILDER=melee3ds-builder
TESTIMG=linuxserver/azahar:latest
VOLUME=${MELEE_VOLUME:-melee3ds-slippi-work}
HERE=$(cd "$(dirname "$0")/.." && pwd)
OUT=${MELEE_TEST_OUT:-$HERE/build/emu-test}

case "${1:-}" in
    --clean)
        docker rmi "$TESTIMG" 2>/dev/null || true
        echo "Removed $TESTIMG. The work volume $VOLUME and the $BUILDER image belong to build.sh (build.sh --clean)."
        exit 0 ;;
    build-dev)
        docker build -q -f "$HERE/docker/Dockerfile" -t "$BUILDER" "$HERE" >/dev/null
        exec docker run --rm -t -v "$VOLUME":/work -v "$HERE":/src:ro --entrypoint sh "$BUILDER" -ec '
            SRC=/work/src
            [ -f $SRC/assets/GALE01/manifest.json ] || { echo "No extracted disc in the work volume; run docker/build.sh first." >&2; exit 2; }
            rsync -a --delete --exclude=.toolchain --exclude=build --exclude=dist --exclude=assets \
                --exclude=upstream --exclude=references --exclude=__pycache__ /src/ $SRC/
            cd $SRC
            python3 tools/bootstrap.py --portable-linux
            python3 tools/build_game.py --smoke --boot --render-worker --async-presentation \
                --build-dir build/game-opt --engine-build-dir build/engine-opt
            ls -la dist/3ds/melee/melee-development.3dsx build/game-opt/melee.elf' ;;
    pair) script=tools/slippi/online_pair_test.py ;;
    net) script=tools/slippi/net_test.py ;;
    shell) script= ;;
    *) sed -n '2,12p' "$0" | sed 's/^# \{0,1\}//' >&2; exit 2 ;;
esac
shift

mkdir -p "$OUT"
if [ -n "$script" ]; then
    set -- "$script" "$@"
    run='python tools/slippi/build_tools.py && exec python "$@"'
else
    set --
    run='exec sh'
fi
# /src is this checkout (read-only); the build products come from the work volume; everything the tests
# write goes to /out (results) or the container's /tmp (emulator profiles, PC tools).
exec docker run --rm -t --platform linux/amd64 --entrypoint sh \
    -e PYTHONDONTWRITEBYTECODE=1 \
    -e MELEE_TEST_OUT=/out \
    -e MELEE_TOOLS_DIR=/tmp/slippi-tools \
    -e MELEE_EMU_DIR=/tmp/azahar \
    -e MELEE_GRAPHICS="${MELEE_GRAPHICS:-software}" \
    -e MELEE_GAME_FILES=/work/src/dist/native-alpha/3ds/melee/files \
    -e MELEE_SLIPPI_FILES=/work/src/dist/native-alpha/3ds/melee/slippi/files \
    -e MELEE_DSX=/work/src/dist/3ds/melee/melee-development.3dsx \
    -e MELEE_ELF=/work/src/build/game-opt/melee.elf \
    -e CC=gcc \
    -v "$HERE":/src:ro \
    -v "$VOLUME":/work:ro \
    -v "$OUT":/out \
    -w /src \
    "$TESTIMG" -c "$run" sh "$@"
