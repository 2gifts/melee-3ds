#!/bin/sh
# Build Melee for the New 3DS inside Docker.
#   docker/build.sh /path/to/Melee-1.02.ciso [output-dir]
# Cleanup afterwards:  docker/build.sh --clean
set -eu
IMAGE=melee3ds-builder
VOLUME=${MELEE_VOLUME:-melee3ds-work}
HERE=$(cd "$(dirname "$0")/.." && pwd)

if [ "${1:-}" = "--clean" ]; then
    docker volume rm "$VOLUME" 2>/dev/null || true
    docker rmi "$IMAGE" 2>/dev/null || true
    docker builder prune -f >/dev/null 2>&1 || true
    echo "Removed the $IMAGE image and the $VOLUME volume. Your output folder is untouched."
    exit 0
fi

[ $# -ge 1 ] || { echo "usage: $0 <melee .iso/.gcm/.ciso> [output-dir]  |  $0 --clean" >&2; exit 2; }
DISC=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
[ -f "$DISC" ] || { echo "Not a file: $1" >&2; exit 2; }
OUT=${2:-$HERE/../out}
mkdir -p "$OUT"
OUT=$(cd "$OUT" && pwd)

docker build -q -f "$HERE/docker/Dockerfile" -t "$IMAGE" "$HERE" >/dev/null
exec docker run --rm -t \
    -e MELEE_VARIANT="${MELEE_VARIANT:-main}" \
    -v "$VOLUME":/work \
    -v "$HERE":/src:ro \
    -v "$DISC":/input/game:ro \
    -v "$OUT":/out \
    "$IMAGE"
