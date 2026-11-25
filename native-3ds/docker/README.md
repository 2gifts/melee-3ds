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

Group play (Melee -> Group, local wireless lobbies; `port/3ds/group`, `tools/group`):

```sh
docker/emu-test.sh group --players 4 --games 2 --drop 50        # PC consoles: lobby, doors, Start, stage, matches
docker/emu-test.sh group --players 4 --games 1 --crash 3:200    # a console vanishes mid-match: the others make it a CPU
docker/emu-test.sh group --players 3 --via-menu                 # meeting through the Solo / Group pages
docker/emu-test.sh group --players 4 --fifth                    # a fifth console sees the game as full and is refused
docker/emu-test.sh group --players 3 --games 2 --late 3         # a console joins during game 1, watches, plays game 2
docker/emu-test.sh group --players 3 --games 2 --results-hold 3:2000     # nobody's results end before the last ready
docker/emu-test.sh group --players 3 --games 2 --results-linger 2:2500   # Start waits for a console still on its results
docker/emu-test.sh group --players 4 --games 2 --results-leave 3[:vanish] # a console leaves on the results screen
docker/emu-test.sh group --players 3 --games 2 --stale-view 2    # a joiner's results start on a view from before the match
docker/emu-test.sh group --lost host-left        # (or host-vanish) the joiner's Host/Join page shows why the session ended
docker/emu-test.sh group --last-leaves           # the last joiner leaves: the host's session is not lost
docker/emu-test.sh group --wireless-off          # "Please enable wireless and try again", and a retry fails the same way
docker/emu-test.sh group --players 3 --games 2 --frames 900 --rejoin 3:200   # a console drops mid-match and comes back during it
docker/emu-test.sh group --players 3 --games 2 --results-rejoin 2            # a console leaves on the results and comes back
docker/emu-test.sh group --cycles 3              # port 2 changes hands after every game; nobody inherits a door
docker/emu-test.sh run tools/localwireless/lw_lobby_test.py     # the localwireless module alone (incl. menu input)
docker/emu-test.sh run tools/group/group_quad_test.py --players 3 --end-frame 600 --kill 3:60 --timeout-ms 20000   # engine, Azahar
docker/emu-test.sh run tools/group/group_menu_test.py            # Melee -> Solo / Group in Azahar; wireless off; HOME and sleep off in a session
docker/emu-test.sh run tools/group/group_lobby_test.py           # Azahar hosts, a PC console joins: doors, Rules, Start, stage, match
docker/emu-test.sh run tools/group/group_join_test.py            # a PC console hosts, Azahar joins: doors, CPU level, READY, handicap, rejoin
docker/emu-test.sh run tools/group/group_late_test.py            # Azahar joins during a match: the read-only CSS, then the lobby
docker/emu-test.sh run tools/group/group_cstick_test.py          # the CSS / stage select camera follows every console's C-stick; door plates
docker/emu-test.sh run tools/group/group_results_test.py         # two emulated consoles: sudden death, the results screen, stars, auto handicap, Back
docker/emu-test.sh run tools/group/group_door_test.py            # a door in use on one console cannot be switched from another
docker/emu-test.sh run tools/group/group_names_test.py           # three emulated consoles and a PC one: the consoles' names under the results windows
```

`group` (group_test.py) runs the lobby rules' unit test (`lobby_unit`) and vanilla's auto handicap rule
(`auto_handicap_unit`, compiled from the decomp's `gm_1601.c`) first. The Azahar tests read the emulated
console's memory through Azahar's GDB stub while it runs (the CSS doors, READY, the slider knobs: `tools/group/css_probe.py`)
and save screenshots of each step. Nothing presses buttons by itself in these builds (`no-scripted-input`), so each
test drives the console from the title screen; give a test about 2 to 5 minutes (`group_results_test.py`, two
emulated consoles and a full match, about 15). Do not run them while the browser
session (`emu-web.sh`) is up: both compete for the CPU.

Results land in `native-3ds/build/emu-test/` (set `MELEE_TEST_OUT` to change it).
It uses the `linuxserver/azahar` image (about 5 GB, amd64 only, so it is
emulated on Apple Silicon) with Azahar headless: by default the OpenGL renderer,
Azahar running as a Wayland client of a headless labwc compositor started in the
test container (about full speed). `MELEE_GRAPHICS=software` selects Qt
`offscreen` and Azahar's CPU rasterizer instead, which is about 1 engine frame a
second in busy scenes such as the character select.
The PC tools (`slippi_fake_mm`, `slippi_fake_peer`) are compiled with the image's
gcc into the container's `/tmp` on every run.
`docker/emu-test.sh --clean` removes the image. The emulator profiles live in the
container's `/tmp`, so nothing else is left behind.

## Play in the browser (Azahar over localhost)

Interactive testing of the development build, no emulator installed on the host.

    docker/build.sh ...            # once, as above (the work volume has the disc extract and toolchain)
    docker/emu-test.sh build-dev   # after every code change: rebuilds melee-development.3dsx into the work volume
    docker/emu-web.sh              # (re)starts the container on the latest build; open http://localhost:3188
    docker/emu-web.sh stop         # removes it (the profile and saves live in the container and go with it)

Two consoles in one desktop, for testing the Melee group lobby (they reach each other over the loopback link, which
needs one network namespace): `MELEE_WEB_INSTANCES=2 MELEE_WEB_RES=1600x900 docker/emu-web.sh`. Console A opens in the
right half and console B in the left; each has its own save. Host from one (Melee -> Group -> Host) and join from the
other. Do not run an `emu-test.sh` test at the same time (CPU).

Restart `emu-web.sh` after each `build-dev`: Azahar loads the `.3dsx` when it starts. The game takes about a minute to
boot. `docker/emu-web.sh logs` shows the container log.

What it is: the stock `linuxserver/azahar` image (a Selkies web desktop on Wayland) with Azahar launched on
`melee-development.3dsx` from the read-only work volume. Bound to 127.0.0.1 only. At each start the session script
(`docker/emu-web-session.sh`) writes `slippi/group.ini` (`menu=1`, `udp=1`, `port=44100`) and `slippi/config.ini` (
`prefetch=0`, `delay=2`) and links the builder's Slippi menu files.

Controls (click the page first so it captures the keyboard): the mouse is the touch screen; arrows = circle pad
(Shift = half tilt); T / G / F / H = D-pad up / down / left / right; A S Z X = A B X Y; Q / W = L / R; M / N = Start /
Select; 1 / 2 = ZL / ZR; I / K / J / L = C-stick up / down / left / right (Shift = half tilt). Rebind with `MELEE_KEYS="A=Space,B=Left" docker/emu-web.sh` (buttons are listed in the script
header).

Speed (learned the hard way, 2026-11-24): **it must use the OpenGL renderer** (the script's default,
`MELEE_GRAPHICS=opengl`, Azahar's `graphics_api=1`). With `MELEE_GRAPHICS=software` the game ran at about 1.4 engine
frames a second (2% of full speed) because every pixel is rasterized on the CPU under x86 emulation; it is not a
resource limit (the Docker VM already has all 8 cores, Rosetta is on, the container uses about 1 GB) and not the game
(the release build was just as slow). The headless tests (`docker/emu-test.sh`) use the OpenGL renderer too, by default.

Other options (environment variables of `emu-web.sh`):
- `MELEE_WEB_DSX=/path/to/melee.3dsx` runs another build instead (for example the release `.3dsx` from
  `melee-3ds-build/out/.../3ds/melee/`, to compare).
- `MELEE_WEB_GDB=1` opens Azahar's GDB stub inside the container (port 24861) so a script can read the game's memory.
  To measure speed, copy `tools/gdb_probe.py` into the container and read the `engine_frames` counter twice, a few
  seconds apart (60 per second is full speed). `docker/emu-test.sh run tools/slippi/emu_speed.py` does the same
  against a headless instance.
- `MELEE_WEB_PORT`, `MELEE_WEB_NAME` (to run a second container for two consoles), `MELEE_WEB_RES`, `MELEE_VOLUME`.

A second container (another port and name, for example
`MELEE_WEB_NAME=melee3ds-emu-web2 MELEE_WEB_PORT=3189 docker/emu-web.sh`) runs a console of its own, but it cannot meet
the first one: UDS between separate emulators does not work headless, and the loopback link of `udp=1` is **not shared
between containers**. For two consoles that play together use `MELEE_WEB_INSTANCES=2` (one container, above) or the PC
tools.
