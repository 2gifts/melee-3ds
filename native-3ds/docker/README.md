# Building in Docker (macOS / Linux)

Builds both CIAs and the SD card folder from your own Melee **US v1.02** disc
image (`.iso`, `.gcm` or `.ciso`) without installing anything on the host except
Docker. This is a port of the Windows builder's toolchain to Linux; it uses the
same pinned compiler, SDK and source revisions.

```sh
native-3ds/docker/build.sh "/path/to/Melee 1.02.ciso" [output-dir]
```

Expect 30-60+ minutes the first time. The image builds natively for your CPU
(arm64 or x86-64). Give Docker at least 8 GB of RAM and ~15 GB of disk.
Results land in `<output-dir>/Melee for 3DS/` (default: `../out` next to the
repo): `Copy to SD card/` (3ds + cias folders), `Licenses/` and
`What to do next.txt`.

Re-running reuses the work volume, so it resumes quickly. The disc is read
through a read-only mount and is never modified.

## What it touches

| Thing | Name | Purpose |
|---|---|---|
| Docker image | `melee3ds-builder` | Ubuntu + compilers |
| Docker volume | `melee3ds-work` | Toolchain, downloaded sources, extracted disc (~5 GB), objects |
| Output folder | your choice | Final SD card files; never deleted by cleanup |

Nothing else is written outside those. The container itself is `--rm`.

## Cleanup

```sh
native-3ds/docker/build.sh --clean
```

That removes the image, the work volume and the build cache. Equivalent to:

```sh
docker volume rm melee3ds-work
docker rmi melee3ds-builder
```

## CISO support

`tools/assets.py` reads `.ciso` files directly (see `tools/ciso.py`), so no
conversion step is needed. Other formats (RVZ, WIA, NKit, GCZ) must still be
converted to a plain ISO in Dolphin first.

## Emulator tests (Slippi beta)

`docker/emu-test.sh` runs the Azahar-based Slippi tests, including the
two-console `online_pair_test.py`, without installing anything on the host:

```sh
MELEE_VARIANT=slippi MELEE_VOLUME=melee3ds-slippi-work docker/build.sh "<disc>" out   # once
MELEE_VOLUME=melee3ds-slippi-work docker/emu-test.sh build-dev   # the development build the tests need
docker/emu-test.sh pair --games 1 --end-frame 600
docker/emu-test.sh net pc-pair
```

Results land in `native-3ds/build/emu-test/` (set `MELEE_TEST_OUT` to change it).
It uses the `linuxserver/azahar` image (about 5 GB, amd64 only, so it is
emulated on Apple Silicon) with Azahar headless: Qt `offscreen` and the software
renderer, no desktop. `MELEE_GRAPHICS=opengl` selects the GL renderer instead.
The PC tools (`slippi_fake_mm`, `slippi_fake_peer`) are compiled with the image's
gcc into the container's `/tmp` on every run.
`docker/emu-test.sh --clean` removes the image. The emulator profiles live in the
container's `/tmp`, so nothing else is left behind.
