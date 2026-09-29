"""QA sweep scenarios. Each takes a driver.Sweep and raises driver.Outcome on
failure; returning normally is a pass. Save-block offsets are relative to
*gmMainLib_804D3EE0: 0x521 Classic round, 0x527 Adventure stage, 0x535
event menu index, 0x1A68 cleared-event bits, 0x1CB0 item preferences."""
import struct
import sys
import time

from sweeplib import Gdb, Outcome
import select_test_stage as select

SCENARIOS = {}
# Character-select icon order (mnCharSel): varied so every fighter appears.
ICONS = list(range(26))


def scenario(name):
    def register(function):
        SCENARIOS[name] = function
        return function
    return register


def boot_to_menu(s):
    s.to_main_menu()
    s.capture('main-menu')


def play_rounds(s, seconds, ko_every, shots=4):
    """Fight with random input through whatever scenes follow."""
    per = seconds / shots
    for i in range(shots):
        s.monkey(per, ko_every=ko_every)
        s.capture(f'play-{i}')


# --- Classic: every round, started directly ---------------------------------
def classic(round_number, icon):
    def run(s):
        boot_to_menu(s)
        s.poke_mainlib(0x521, bytes([round_number]))
        s.jump_mode(0x03)
        s.choose(icon)
        s.wait_scene((2, 3, 4, 32), 90, press=0x100)
        play_rounds(s, 100, ko_every=12)
    return run


# Rounds 0-10; an 11th start routes to the Coming Soon scene.
for r in range(11):
    scenario(f'classic-r{r:02d}')(classic(r, ICONS[(r * 7) % 25]))


# --- Adventure: every stage -------------------------------------------------
def adventure(stage, icon):
    def run(s):
        boot_to_menu(s)
        s.poke_mainlib(0x527, bytes([stage]))
        s.jump_mode(0x04)
        s.choose(icon)
        # Skip the stage's intro cutscene, then play through its parts.
        s.wait_scene(2, 120, press=0x1000, every=3)
        play_rounds(s, 150, ko_every=15, shots=5)
    return run


for st in range(12):
    scenario(f'adventure-s{st:02d}')(adventure(st, ICONS[(st * 5 + 3) % 25]))


@scenario('allstar')
def allstar(s):
    boot_to_menu(s)
    s.jump_mode(0x05)
    s.choose(4)
    s.wait_scene(2, 120, press=0x100, every=3)
    # KOs advance through several rounds and the rest area.
    play_rounds(s, 180, ko_every=8, shots=6)


# --- Event matches: all 51 ---------------------------------------------------
def event(index):
    def run(s):
        boot_to_menu(s)
        s.poke_mainlib(0x1A68, struct.pack('>Q', (1 << 51) - 1))  # all events available
        s.poke_mainlib(0x535, bytes([index]))
        s.jump_mode(0x2B)
        time.sleep(6)
        s.state()
        s.capture('event-menu')
        s.hold(0x100, 3, settle=3)
        # Events that let the player pick a character show character select.
        deadline = time.monotonic() + 60
        while time.monotonic() < deadline:
            st = s.state()
            if st['scene'] == 8:
                try:
                    s.choose(ICONS[index % 25])
                except Outcome:
                    # Fixed-roster events: accept the preset fighter.
                    s.hold(0x1000, 4, settle=2)
            if st['scene'] in (2, 3, 4):
                break
            s.hold(0x100, 3, settle=1.5)
        else:
            raise Outcome('error', 'event match did not start')
        play_rounds(s, 70, ko_every=14, shots=3)
    return run


for e in range(51):
    scenario(f'event-{e + 1:02d}')(event(e))


# --- Stadium -----------------------------------------------------------------
def target_test(icon):
    def run(s):
        boot_to_menu(s)
        s.jump_mode(0x0F)
        s.choose(icon)
        s.wait_scene(2, 90, press=0x100)
        play_rounds(s, 50, ko_every=None, shots=2)
    return run


for i in range(25):
    scenario(f'target-{i:02d}')(target_test(i))


def homerun(icon):
    def run(s):
        boot_to_menu(s)
        s.jump_mode(0x20)
        s.choose(icon)
        s.wait_scene(2, 90, press=0x100)
        play_rounds(s, 40, ko_every=None, shots=2)
        s.wait(8)
        s.capture('result')
    return run


for i in (1, 9, 13, 20):
    scenario(f'homerun-{i:02d}')(homerun(i))


MULTIMAN = {0x21: '10man', 0x22: '100man', 0x23: '3min', 0x24: '15min', 0x25: 'endless', 0x26: 'cruel'}
for mode, label in MULTIMAN.items():
    def multiman(s, mode=mode):
        boot_to_menu(s)
        s.jump_mode(mode)
        s.choose(12)
        s.wait_scene(2, 90, press=0x100)
        play_rounds(s, 90, ko_every=5, shots=3)
    scenario(f'multiman-{label}')(multiman)


# --- VS-type Special Melee modes and fighter/item fuzzing -----------------------
def select_match(s, character, cpu, stage, versus=False, extra_cpus=(), costumes=()):
    """From character select (or the main menu, opening VS or Training),
    start a match on STAGE. EXTRA_CPUS fill ports 3 and 4; COSTUMES are
    colour indices per port."""
    argv = sys.argv
    sys.argv = ['select_test_stage.py', str(stage), '--character', str(character), '--cpu', str(cpu),
                *(['--versus'] if versus else []),
                *[arg for icon in extra_cpus for arg in ('--extra-cpu', str(icon))],
                *(['--costumes', ','.join(map(str, costumes))] if costumes else [])]
    try:
        select.main()
    except Exception as error:  # noqa: BLE001
        raise Outcome('error', f'match setup failed: {error}')
    finally:
        sys.argv = argv
    s.hold(0, 2)


SPECIAL = {0x10: 'sudden-death', 0x11: 'invisible', 0x12: 'slomo', 0x13: 'lightning',
           0x1D: 'tiny', 0x1E: 'giant', 0x1F: 'stamina', 0x2A: 'fixed-camera', 0x2C: 'single-button'}
for mode, label in SPECIAL.items():
    def special(s, mode=mode):
        boot_to_menu(s)
        s.jump_mode(mode)
        s.wait_scene(8, 60)
        select_match(s, 1 + mode % 20, 9, mode % 26)
        play_rounds(s, 70, ko_every=20, shots=2)
    scenario(f'special-{label}')(special)


def items_on(s):
    base = s.mainlib_base()
    # Very high frequency, every item.
    with Gdb() as g:
        g.write(base + 0x1CB0, bytes([4]))
        g.write(base + 0x1CB8, struct.pack('>Q', 0x00000000FFFFFFFF))


def fighter(icon, stage):
    def run(s):
        boot_to_menu(s)
        items_on(s)
        select_match(s, icon, (icon + 11) % 25, stage, versus=True)
        play_rounds(s, 90, ko_every=25, shots=3)
    return run


# 25 character-select icons (Sheik comes from Zelda).
for i in range(25):
    scenario(f'fighter-{i:02d}')(fighter(i, (i * 3) % 29))


# --- Other modes, menus, movies, memory card ---------------------------------------
@scenario('tournament')
def tournament(s):
    boot_to_menu(s)
    s.jump_mode(0x1B)
    for i in range(40):
        s.hold(0x1000 if i % 3 == 2 else 0x100, 3, settle=1.2)
        if i % 8 == 7:
            s.capture(f'step-{i}')
        if s.state()['scene'] in (2, 3):
            break
    if s.state()['scene'] in (2, 3):
        play_rounds(s, 60, ko_every=10, shots=2)


@scenario('opening-movie')
def opening_movie(s):
    boot_to_menu(s)
    s.jump_mode(0x18)
    for i in range(4):
        s.wait(8)
        s.capture(f'movie-{i}')


for mode, label in ((0x15, 'classic'), (0x16, 'adventure'), (0x17, 'allstar')):
    def game_over(s, mode=mode):
        boot_to_menu(s)
        s.jump_mode(mode)
        for i in range(8):
            s.wait(15)
            s.capture(f'ending-{i}')
            s.hold(0x100, 3)
    scenario(f'ending-{label}')(game_over)


def menu_path(s, top, entries):
    """Open main-menu entry TOP, then press A down ENTRIES (downs, then A)."""
    import menu_refinement_test as mr
    kinds = {0: 1, 1: 2, 2: 3, 3: 4, 4: 5}
    mr.idle(0)
    mr.enter(top, kinds[top])
    for downs in entries:
        for _ in range(downs):
            s.hold(0x4, 2, settle=.4)
        s.hold(0x100, 3, settle=2.5)
        s.state()


MENUS = {
    'options-rumble': (3, [0]), 'options-sound': (3, [1]), 'options-screen': (3, [2]),
    'options-erase': (3, [4]), 'data-snapshots': (4, [0]), 'data-archives-movie': (4, [1, 0]),
    'data-archives-howto': (4, [1, 1]), 'data-soundtest': (4, [2]), 'data-records-vs': (4, [3, 0]),
    'data-records-bonus': (4, [3, 1]), 'data-records-misc': (4, [3, 2]), 'data-special': (4, [4]),
    'trophies-gallery': (2, [0]), 'trophies-lottery': (2, [1]), 'trophies-collection': (2, [2]),
}
for label, (top, entries) in MENUS.items():
    def open_menu(s, top=top, entries=entries):
        boot_to_menu(s)
        menu_path(s, top, entries)
        s.capture('opened')
        # Exercise the screen: scroll, page, confirm, then leave.
        for button in (0x8, 0x4, 0x1, 0x2, 0x40, 0x20, 0x100):
            s.hold(button, 3, settle=1.2)
        s.capture('used')
        for _ in range(4):
            s.hold(0x200, 3, settle=1.5)
        s.wait(3)
    scenario(f'menu-{label}')(open_menu)


@scenario('name-entry')
def name_entry(s):
    boot_to_menu(s)
    menu_path(s, 1, [0])  # VS Melee character select
    s.wait_scene(8, 30)
    # The name button under P1's slot: move there and type a name.
    s.hold(0, 25, 0, -80)
    s.hold(0x100, 3, settle=2)
    for button in (0x100, 0x100, 0x100, 0x8, 0x100, 0x2, 0x100, 0x1000):
        s.hold(button, 3, settle=.6)
    s.capture('name')
    s.wait(3)


@scenario('card-roundtrip')
def card_roundtrip(s):
    """Boot twice: the first creates the save, the second must load it."""
    boot_to_menu(s)
    s.stop()
    s.last_frame = None
    s.launch()
    boot_to_menu(s)


@scenario('camera-snapshot')
def camera_snapshot(s):
    """Camera Mode with the bottom-screen CAMERA toggle: take and save a photo."""
    from sweeplib import Gdb, symbols, SD
    import menu_refinement_test as mr
    boot_to_menu(s)
    # Through the menus (the test mode jump leaves the game-mode id unset):
    # VS Mode > Special Melee > Camera Mode.
    mr.idle(0)
    mr.enter(1, 2)
    mr.enter(2, 12)
    mr.hover(0)
    s.hold(0x100, 3, settle=3)
    s.wait_scene(8, 60, press=0x100, every=3)  # past the card-space notice
    select_match(s, 1, 9, 16)  # P1 and a CPU, like VS
    s.monkey(8, cstick=False)
    with Gdb() as g:
        g.write(symbols['mp_test_bottom_touch'], (280 | (225 << 16)).to_bytes(4, 'little'))
    time.sleep(2)
    s.capture('camera-control')
    # Z is the shutter; the save prompt then takes A.
    for i, button in enumerate((0x10, 0x100, 0x100, 0x100)):
        s.hold(button, 4, settle=4)
        s.capture(f'step{i}-{button:x}')
    s.wait(10)
    saves = sorted(p.name for p in (SD / 'saves').rglob('*.gci'))
    s.note(f'card files: {saves}')
    if not [n for n in saves if 'SuperSmashBros' not in n]:
        raise Outcome('error', f'no snapshot file saved: {saves}')
    # Quit the match (pause, then L+R+A+Start), back out to the main menu,
    # and view the photo in Data > Snapshots (the album's JPEG decoder).
    s.hold(0x1000, 3, settle=2)
    s.hold(0x40 | 0x20 | 0x100 | 0x1000, 20, settle=4)
    for _ in range(12):
        m = s.menu()
        if 'menu' in m and m['menu']['state'] == 0 and m['menu']['kind'] == 0:
            break
        # Character select leaves on a held B; menus on a press.
        s.hold(0x200, 60 if 'hand' in m else 3, settle=2.5)
    menu_path(s, 4, [0])
    s.capture('album')
    for i, button in enumerate((0x100, 0x2, 0x100, 0x1)):
        s.hold(button, 3, settle=3)
        s.capture(f'album-{i}')


def classic_through(round_number, icon, seconds=240):
    """Start at ROUND and win onward with KOs, entering later rounds normally."""
    def run(s):
        boot_to_menu(s)
        s.poke_mainlib(0x521, bytes([round_number]))
        s.jump_mode(0x03)
        s.choose(icon)
        s.wait_scene((2, 3, 4, 32), 90, press=0x100)
        end = time.monotonic() + seconds
        shot = 0
        while time.monotonic() < end:
            s.monkey(15, ko_every=4)
            s.capture(f'through-{shot}')
            shot += 1
            s.hold(0x100, 3)
    return run


for r in range(0, 11, 2):
    scenario(f'classic-from-r{r:02d}')(classic_through(r, (r * 3 + 2) % 25))


@scenario('progress-panel')
def progress_panel(s):
    boot_to_menu(s)
    s.wait(4)
    s.capture('bottom-main')


@scenario('cstick-1p')
def cstick_1p(s):
    """Single-player C-stick mod: a C-stick push performs a smash attack in Classic."""
    from gameplay_test import exchange
    boot_to_menu(s)
    s.poke_mainlib(0x521, bytes([0]))
    s.jump_mode(0x03)
    s.choose(1)
    s.wait_scene(2, 90, press=0x100)
    s.wait(6)  # past READY / GO
    seen = []
    for attempt in range(6):
        s.hold(0, 20, settle=1.0)
        s.pad(0, 4, 0, 0, 80 if attempt % 2 == 0 else -80, 0)
        for _ in range(8):
            state = exchange()
            if state['fighters']:
                seen.append(state['fighters'][0]['motion'])
            time.sleep(.15)
        if any(0x3A <= m <= 0x3E for m in seen):
            s.note(f'smash attack from the C-stick (motions {sorted(set(seen))})')
            s.capture('cstick-smash')
            return
    raise Outcome('fail', f'no smash attack from the C-stick in 1P; motions seen {sorted(set(seen))}')


UNLOCKABLE = {3: 'mr-game-and-watch', 7: 'luigi', 9: 'marth', 10: 'mewtwo', 15: 'jigglypuff', 20: 'falco',
              21: 'young-link', 22: 'dr-mario', 23: 'roy', 24: 'pichu', 25: 'ganondorf'}


def challenger(cpu_kind):
    """Challenger Approaching: fight and defeat the challenger (unlocks it on a fresh save)."""
    def run(s):
        from sweeplib import Gdb, symbols
        boot_to_menu(s)
        with Gdb() as g:
            # human Mario, colour 0, port 1, no name tag; return to the menus.
            g.write(symbols['challenger_data'], bytes([8, 0, 0, 120, cpu_kind, 1, 1, 0, 0, 0]))
        s.jump_mode(0x14)
        s.wait_scene((2, 3, 4), 120, press=0x100, every=3)
        s.capture('fight')
        s.monkey(20, ko_every=6)
        # Unlock and "new challenger" screens, then back to the menus.
        for i in range(8):
            s.wait(6)
            s.capture(f'after-{i}')
            s.hold(0x100, 3)
    return run


for kind, label in UNLOCKABLE.items():
    scenario(f'challenger-{label}')(challenger(kind))


import scenarios_extended  # noqa: E402,F401  (registers the extended families)
