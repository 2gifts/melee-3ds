"""Extended QA sweep: stages, items, VS rules, attract mode, Adventure
cutscenes, per-character endings, every trophy, Training movesets and every
costume. Registered into scenarios.SCENARIOS on import.

Addresses: GameRules at save 0x1850 (mode +2: 0 time, 1 stock, 2 coin),
trophy counts at 0x1CD4 (u16 each, low byte owned), player_slots entries
0xE90 apart (costume +0x44, stocks +0x8E), state_machine routing +5 = next
game-mode state + 1."""
import struct
import time

from sweeplib import Gdb, Outcome, symbols
from scenarios import boot_to_menu, menu_path, play_rounds, scenario, select_match

RULE_FIELDS = {'mode': 2, 'time_limit': 3, 'stock_count': 4, 'handicap': 5, 'damage_ratio': 6,
               'stock_time_limit': 8, 'friendly_fire': 9, 'pause': 10}
PLAYER_SLOT = 0xE90
# Colours per external character kind (gm_GetNumCostumesForCKind).
COLORS = [6, 5, 4, 4, 6, 4, 5, 4, 5, 5, 4, 4, 5, 4, 4, 5, 5, 6, 5, 5, 4, 5, 5, 5, 4, 5]


def set_rules(s, **values):
    base = s.mainlib_base()
    with Gdb() as g:
        for name, value in values.items():
            g.write(base + 0x1850 + RULE_FIELDS[name], bytes([value]))


def refill_stocks(s):
    """Keep every fighter in a stock match so the stage keeps running."""
    with Gdb() as g:
        for slot in range(4):
            address = symbols['player_slots'] + slot * PLAYER_SLOT + 0x8E
            if 0 < g.read(address, 1)[0] < 4:
                g.write(address, bytes([4]))


def slot_costumes():
    with Gdb() as g:
        return [g.read(symbols['player_slots'] + slot * PLAYER_SLOT + 0x44, 1)[0] for slot in range(4)]


def next_state(state):
    """Route the current game mode to STATE when its current scene ends."""
    with Gdb() as g:
        g.write(symbols['state_machine'] + 5, bytes([state + 1]))


def fight(s, seconds, shots, refill=True, label='play'):
    per = seconds / max(1, shots)
    for i in range(shots):
        end = time.monotonic() + per
        while time.monotonic() < end:
            s.monkey(min(15, max(1, end - time.monotonic())))
            if refill:
                refill_stocks(s)
        s.capture(f'{label}-{i}')


def finish_match(s, label='results', timeout=420):
    """Play to the results screen (KOs settle ties and Sudden Death), capture
    it, then return to character select."""
    end = time.monotonic() + timeout
    scene = None
    while time.monotonic() < end:
        scene = s.state()['scene']
        if scene in (5, 8):
            break
        s.monkey(1.5, ko_every=1)
    else:
        raise Outcome('error', f'results screen not reached (at {s.scenes[-1]})')
    if scene == 5:
        s.wait(4)
        s.capture(label)
    for i in range(40):
        if s.state()['scene'] == 8:
            return
        s.hold((0x100, 0x1000)[i % 2], 3, settle=2.5)
    raise Outcome('error', f'results screen did not return to character select (at {s.scenes[-1]})')


# --- Every stage, four fighters, four minutes ----------------------------------
def stage(index):
    def run(s):
        boot_to_menu(s)
        icons = [(index * 7 + k * 6 + 3) % 25 for k in range(4)]
        select_match(s, icons[0], icons[1], index, versus=True, extra_cpus=icons[2:])
        s.capture('start')
        fight(s, 240, 4)
    return run


for i in range(29):
    scenario(f'stage-{i:02d}')(stage(i))


# --- Items four at a time, very high frequency ----------------------------------
def item_group(group):
    def run(s):
        boot_to_menu(s)
        base = s.mainlib_base()
        with Gdb() as g:
            g.write(base + 0x1CB0, bytes([4]))
            g.write(base + 0x1CB8, struct.pack('>Q', 0xF << (4 * group)))
        icons = [(group * 3 + k * 7 + 1) % 25 for k in range(4)]
        select_match(s, icons[0], icons[1], (group * 7 + 4) % 29, versus=True, extra_cpus=icons[2:])
        fight(s, 150, 5)
    return run


for group in range(8):
    scenario(f'items-g{group}')(item_group(group))


# --- VS rule variants ---------------------------------------------------------------
@scenario('vs-time-sudden-death')
def vs_time_sudden_death(s):
    """A one-minute time match nobody scores in goes to Sudden Death."""
    boot_to_menu(s)
    set_rules(s, mode=0, time_limit=1)
    select_match(s, 5, 12, 16, versus=True)
    # Clear the score (falls, KOs, self-destructs) until time runs out.
    end = time.monotonic() + 180
    while time.monotonic() < end:
        st = s.state()
        if st['scene'] in (3, 5):
            break
        with Gdb() as g:
            for slot in range(4):
                base = symbols['player_slots'] + slot * PLAYER_SLOT
                g.write(base + 0x68, bytes(8))
                g.write(base + 0x70, bytes(24))
                g.write(base + 0x8C, bytes(2))
        time.sleep(.5)
    if st['scene'] == 3:
        s.wait(3)
        s.capture('sudden-death')
    else:
        s.note('no tie: somebody scored')
    finish_match(s)


@scenario('vs-coin')
def vs_coin(s):
    boot_to_menu(s)
    set_rules(s, mode=2, time_limit=1)
    select_match(s, 9, 2, 3, versus=True, extra_cpus=(17, 21))
    s.monkey(50, ko_every=12)
    s.capture('coins')
    finish_match(s)


@scenario('vs-stock-handicap')
def vs_stock_handicap(s):
    boot_to_menu(s)
    set_rules(s, mode=1, stock_count=2, handicap=2, damage_ratio=20)
    select_match(s, 13, 6, 8, versus=True, extra_cpus=(0,))
    s.monkey(20, ko_every=6)
    finish_match(s)


@scenario('vs-pause-quit')
def vs_pause_quit(s):
    """Pause camera controls, then quit with L+R+A+Start (No Contest)."""
    boot_to_menu(s)
    select_match(s, 20, 14, 11, versus=True)
    s.monkey(15)
    s.hold(0x1000, 3, settle=1.5)
    s.capture('paused')
    for buttons, x, y, cx, cy in ((0, 80, 0, 0, 0), (0, 0, 80, 0, 0), (0, 0, 0, 0, 80), (0, 0, 0, 0, -80),
                                  (0x10, 0, 0, 0, 0), (0, -80, -80, 0, 0)):
        s.hold(buttons, 40, x, y, cx, cy, settle=1.2)
    s.capture('pause-camera')
    s.hold(0x1000, 3, settle=1.5)
    s.monkey(8)
    s.hold(0x1000, 3, settle=1.5)
    s.hold(0x40 | 0x20 | 0x100 | 0x1000, 20, settle=4)
    finish_match(s, 'no-contest', timeout=30)


@scenario('title-attract')
def title_attract(s):
    """Leave the title screen idle: attract demo and movies."""
    for _ in range(240):
        st = s.state()
        if st['scene'] == 42:
            s.hold(0x100, 2, settle=.5)  # create the save
        elif st['scene'] == 0 and any(scene == 42 for scene, _ in s.scenes):
            break
        time.sleep(.5)
    for i in range(9):
        s.wait(20)
        s.capture(f'idle-{i}')
    s.note(f'scenes while idle: {sorted({scene for scene, _ in s.scenes})}')


# --- Adventure cutscenes, unskipped -------------------------------------------------
CUTSCENES = {0x02: (0, 'luigi'), 0x1A: (3, 'brinstar-escape'), 0x1C: (3, 'planet-explosion'),
             0x22: (4, 'kirby-team'), 0x24: (4, 'giant-kirby'), 0x2A: (5, 'star-fox'),
             0x38: (7, 'f-zero'), 0x52: (10, 'metal'), 0x5A: (11, 'bowser-trophy'),
             0x5B: (11, 'giga-bowser'), 0x5D: (11, 'giga-bowser-defeated')}


def cutscene(state, block):
    def run(s):
        boot_to_menu(s)
        s.poke_mainlib(0x527, bytes([block]))
        s.jump_mode(0x04)
        s.choose(8)
        scenes = tuple(range(0x11, 0x1C))
        if state % 8:
            # The stage splash first; route the mode from there.
            s.wait_scene(14, 90)
            next_state(state)
            s.note(f'next adventure state {state:#x}')
        try:
            s.wait_scene(scenes, 25)
        except Outcome:
            s.wait_scene(scenes, 60, press=0x1000, every=10)
        for i in range(5):
            s.wait(7)
            s.capture(f'cutscene-{i}')
        # Whatever follows: a fight or the next cutscene.
        play_rounds(s, 45, ko_every=8, shots=2)
    return run


for state, (block, label) in CUTSCENES.items():
    scenario(f'cutscene-{label}')(cutscene(state, block))


# --- Endings: each character's trophy fall and congratulations movie ----------------
def congratulations(mode, ckind):
    def run(s):
        boot_to_menu(s)
        with Gdb() as g:
            g.write(symbols['gm_8049C178.0'], bytes([ckind]))
        s.jump_mode(mode)
        s.wait_scene(15, 90)
        s.wait(8)
        s.capture('trophy-fall')
        next_state(3)
        s.wait_scene(16, 90, press=0x100, every=4)
        for i in range(2):
            s.wait(5)
            s.capture(f'congratulations-{i}')
    return run


for mode, label in ((0x15, 'classic'), (0x16, 'adventure'), (0x17, 'allstar')):
    for ckind in range(26):
        scenario(f'ending-{label}-c{ckind:02d}')(congratulations(mode, ckind))


# --- Every trophy in the gallery ------------------------------------------------------
@scenario('trophies-all')
def trophies_all(s):
    boot_to_menu(s)
    base = s.mainlib_base()
    with Gdb() as g:
        owned = bytearray(g.read(base + 0x1CD4, 293 * 2))
        for i in range(293):
            owned[2 * i + 1] = max(1, owned[2 * i + 1])
        g.write(base + 0x1CD4, bytes(owned))
        # The gallery lists and wraps at the owned count (s16).
        g.write(base + 0x1CD0, struct.pack('>h', 293))
    menu_path(s, 2, [0])
    s.wait(4)
    s.capture('gallery-0')
    seen = set()
    for i in range(1, 300):
        s.hold(0x20, 2, settle=.7)  # R: next trophy
        if i % 10 == 0:
            s.state()
            with Gdb() as g:
                display = g.word(symbols['Toy_sbss_804D6EE0'])
                if display:
                    seen.add(int.from_bytes(g.read(display + 0x154, 2), 'big', signed=True))
        if i % 25 == 0:
            s.capture(f'gallery-{i}')
    s.note(f'gallery positions sampled: {len(seen)}, highest {max(seen) if seen else None}')
    if len(seen) < 20:
        raise Outcome('error', f'gallery did not advance: {sorted(seen)}')


# --- Training: each fighter's moveset, then the Training menu ---------------------
MOVESET = [
    [(0x100, 3)], [(0x100, 3)], [(0x100, 4, 45, 0)], [(0x100, 4, 0, 45)], [(0x100, 4, 0, -45)],
    [(0, 4, 0, 0, 80, 0)], [(0, 4, 0, 0, 0, 80)], [(0, 4, 0, 0, 0, -80)],
    [(0, 14, 80, 0), (0x100, 3, 80, 0)],                                         # dash attack
    [(0x400, 3), (0, 3), (0x100, 3)], [(0x400, 3), (0, 3), (0, 3, 0, 0, 80, 0)],
    [(0x400, 3), (0, 3), (0, 3, 0, 0, -80, 0)], [(0x400, 3), (0, 3), (0, 3, 0, 0, 0, 80)],
    [(0x400, 3), (0, 3), (0, 3, 0, 0, 0, -80)],                                  # aerials
    [(0x200, 4)], [(0x200, 4, 80, 0)], [(0x200, 4, -80, 0)], [(0x200, 4, 0, 80)], [(0x200, 4, 0, -80)],
    [(0x400, 3), (0, 3), (0x200, 4)], [(0x400, 3), (0, 3), (0x200, 4, 0, -80)],  # aerial specials
    [(0, 20, 60, 0), (0x10, 3), (0, 6), (0, 4, 80, 0)], [(0x10, 3), (0, 6), (0, 4, -80, 0)],
    [(0x10, 3), (0, 6), (0, 4, 0, 80)], [(0x10, 3), (0, 6), (0, 4, 0, -80)],   # grabs and throws
    [(0x20, 20)], [(0x20, 6, 80, 0)], [(0x20, 6, -80, 0)], [(0x20, 6, 0, -80)],
    [(0x400, 3), (0, 4), (0x20, 3, 60, 60)],                                     # air dodge
    [(0x8, 3)],                                                                  # taunt
]


def training(icon):
    def run(s):
        from gameplay_test import exchange
        boot_to_menu(s)
        select_match(s, icon, (icon + 9) % 25, (icon * 5 + 2) % 29)
        s.wait(4)
        motions = set()
        for sequence in MOVESET:
            for step in sequence:
                s.hold(*step)
            for _ in range(3):
                try:
                    motions.add(exchange()['fighters'][0]['motion'])
                except Exception:  # noqa: BLE001 - sampled between frames
                    pass
                time.sleep(.2)
            s.hold(0, 40, settle=.8)
        s.capture('moveset')
        s.state()
        s.hold(0x1000, 3, settle=1.5)
        for _ in range(30):
            s.hold(s.rng.choice((1, 2, 4, 8, 0x100)), 3, settle=.4)
        s.capture('training-menu')
        s.hold(0x1000, 3, settle=1.5)
        s.monkey(40)
        s.capture('after-menu')
        s.note(f'{len(motions)} distinct motion states')
        if len(motions) < 8:
            raise Outcome('fail', f'fighter reached only {len(motions)} motion states: {sorted(motions)}')
    return run


for i in range(25):
    scenario(f'training-{i:02d}')(training(i))


# --- Every costume, through the results screen -------------------------------------
def costumes(icon):
    def run(s):
        boot_to_menu(s)
        # One stock: the match ends at the KOs, straight to the results.
        set_rules(s, mode=1, stock_count=1)
        with Gdb() as g:
            ckind = g.read(symbols['icons'] + 28 * icon + 1, 1)[0]
        count = COLORS[ckind]
        for first in range(0, count, 4):
            group = list(range(first, min(count, first + 4)))
            if len(group) == 1:
                group.append(0)
            select_match(s, icon, icon, (icon * 3 + first + 1) % 29, versus=True,
                         extra_cpus=[icon] * (len(group) - 2), costumes=group)
            got = slot_costumes()[:len(group)]
            s.note(f'costumes {got}')
            if got != group:
                raise Outcome('fail', f'costumes {got} in the match, {group} chosen')
            s.capture(f'costumes-{first}')
            s.monkey(20)
            finish_match(s, f'results-{first}')
    return run


for i in range(25):
    scenario(f'costumes-{i:02d}')(costumes(i))


@scenario('results-watch')
def results_watch(s):
    """The VS results screen over time, untouched: every panel's portrait."""
    boot_to_menu(s)
    set_rules(s, mode=1, stock_count=1)
    select_match(s, 7, 2, 8, versus=True, extra_cpus=(12,))
    s.monkey(8)
    end = time.monotonic() + 300
    while time.monotonic() < end and s.state()['scene'] != 5:
        s.monkey(1.5, ko_every=1)
    for i in range(6):
        s.wait(5)
        s.capture(f'results-{i * 5 + 5:02d}s')



def push_out(s, indices):
    """Move the fighters at these object-list positions below the blast zone."""
    with Gdb() as g:
        gobj, index = g.word(g.word(symbols['HSD_GObjPLinkHead']) + 32), 0
        while gobj and index < 8:
            fp = g.word(gobj + 0x2c)
            if index in indices and fp:
                g.write(fp + 0xb4, struct.pack('>f', -2000.0))
            gobj = g.word(gobj + 8)
            index += 1


@scenario('results-slots')
def results_slots(s):
    """P1 (slot 0) and CPU 2 (slot 2) lose, CPU 1 wins: which loser panels capture?"""
    boot_to_menu(s)
    set_rules(s, mode=1, stock_count=1)
    select_match(s, 7, 2, 8, versus=True, extra_cpus=(12,))
    s.monkey(6)
    end = time.monotonic() + 300
    while time.monotonic() < end and s.state()['scene'] != 5:
        push_out(s, (0, 2))
        s.hold(0, 30, settle=1.5)
    s.wait(8)
    s.capture('results')


@scenario('probe-greens-items')
def probe_greens_items(s):
    """Green Greens (Whispy Woods) with every item, captured often (visual check)."""
    boot_to_menu(s)
    base = s.mainlib_base()
    with Gdb() as g:
        g.write(base + 0x1CB0, bytes([4]))
        g.write(base + 0x1CB8, struct.pack('>Q', 0x00000000FFFFFFFF))
    select_match(s, 4, 12, 9, versus=True)
    for i in range(10):
        s.hold(0, 60, settle=4)
        refill_stocks(s)
        s.capture(f'look-{i}')


@scenario('probe-greens-nomips')
def probe_greens_nomips(s):
    """probe-greens-items with generated mip chains off (A/B)."""
    with Gdb() as g:
        g.write(symbols['mp_texture_mips_disable'], (1).to_bytes(4, 'little'))
    probe_greens_items(s)


def own_all_trophies(s):
    base = s.mainlib_base()
    with Gdb() as g:
        owned = bytearray(g.read(base + 0x1CD4, 293 * 2))
        for i in range(293):
            owned[2 * i + 1] = max(1, owned[2 * i + 1])
        g.write(base + 0x1CD4, bytes(owned))
        g.write(base + 0x1CD0, struct.pack('>h', 293))


@scenario('collection-load')
def collection_load(s):
    """Trophy Collection entry with every trophy owned (load-time probe; file trace on)."""
    boot_to_menu(s)
    own_all_trophies(s)
    with Gdb() as g:
        g.write(symbols['mp_file_trace'], (1).to_bytes(4, 'little'))
    menu_path(s, 2, [2])
    s.wait(20)
    s.capture('collection')
    s.hold(0x2, 3, settle=3)
    s.capture('collection-moved')


@scenario('collection-profile')
def collection_profile(s):
    """Sample the engine's PC while the Trophy Collection loads (poor man's profiler)."""
    import json
    import menu_refinement_test as mr
    boot_to_menu(s)
    own_all_trophies(s)
    mr.idle(0)
    mr.enter(2, 3)
    for _ in range(2):
        s.hold(0x4, 2, settle=.4)
    s.pad(0x100, 3)
    samples = []
    end = time.monotonic() + 12
    while time.monotonic() < end:
        with Gdb() as g:
            from gameplay_test import packet, receive
            packet(g.s, 'g')
            regs = bytes.fromhex(receive(g.s))
            samples.append([int.from_bytes(regs[i:i + 4], 'little') for i in (60, 56)])
        time.sleep(.02)
    (s.out / 'pc-samples.json').write_text(json.dumps(samples))
    s.note(f'{len(samples)} samples')
    s.capture('collection')


# --- Tap jump switch on the bottom screen's CONTROLS page --------------------------
def player_airborne():
    with Gdb() as g:
        gobj = g.word(g.word(symbols['HSD_GObjPLinkHead']) + 32)
        fp = g.word(gobj + 0x2c)
        return bool(g.word(fp + 0xe0))


def jumped(s, buttons=0, x=0, y=0, tries=2):
    """Stand still, then input; True if player 1 left the ground. An
    injected stick-up that lands on a frame when the fighter stores its
    input (fighter.c x221D_b3) is not a tap, in either build; retry once."""
    for _ in range(tries):
        s.hold(0, 40, settle=1.5)
        if player_airborne():
            raise Outcome('error', 'player 1 is not standing on the ground')
        s.pad(buttons, 10, x, y)
        seen = watch_airborne()
        s.hold(0, 90, settle=2.5)
        if seen:
            return True
    return False


def game_frame():
    with Gdb() as g:
        return g.word(symbols['engine_frames'], 'little')


def watch_airborne(frames=60, limit=15):
    """True if player 1 leaves the ground within FRAMES game frames. Counts
    frames, not seconds: a busy emulator (or a capture) can stall a while."""
    start = game_frame()
    end = time.monotonic() + limit
    while time.monotonic() < end:
        if player_airborne():
            return True
        if game_frame() - start > frames:
            return False
        time.sleep(.05)
    return False


def touch(x, y):
    with Gdb() as g:
        g.write(symbols['mp_test_bottom_touch'], (x | (y << 16)).to_bytes(4, 'little'))
    time.sleep(1.5)


@scenario('tap-jump-toggle')
def tap_jump_toggle(s):
    """CONTROLS page TAP JUMP switch: stick-up jumps only while it is on;
    X always jumps; the choice is saved in settings.txt."""
    from sweeplib import SD
    settings = SD / 'settings.txt'
    boot_to_menu(s)
    select_match(s, 2, 9, 25)  # Final Destination: no stage hazards
    s.wait(4)
    results = {'stick-up, tap jump on': jumped(s, y=80)}
    touch(280, 225)  # open CONTROLS
    with Gdb() as g:
        if not g.word(symbols['mp_bottom_guide_visible'], 'little'):
            raise Outcome('fail', 'CONTROLS page did not open')
    touch(265, 14)   # CUSTOMIZE tab
    touch(100, 199)  # TAP JUMP switch
    s.capture('bottom-tap-jump-off')
    with Gdb() as g:
        state = g.word(symbols['tap_jump'], 'little')
    results['stick-up, tap jump off'] = jumped(s, y=80)
    results['X, tap jump off'] = jumped(s, 0x400)
    saved_off = settings.read_text().splitlines()[0] if settings.exists() else None
    touch(100, 199)
    s.capture('bottom-tap-jump-on')
    results['stick-up, tap jump on again'] = jumped(s, y=80)
    time.sleep(1)
    saved_on = settings.read_text().splitlines()[0] if settings.exists() else None
    s.note(f'{results}; switch state after first touch {state}; settings {saved_off!r} -> {saved_on!r}')
    expected = {'stick-up, tap jump on': True, 'stick-up, tap jump off': False, 'X, tap jump off': True,
                'stick-up, tap jump on again': True}
    if results != expected or state != 0 or saved_off != 'tap_jump=0' or saved_on != 'tap_jump=1':
        raise Outcome('fail', f'tap jump switch: {results}, state {state}, saved {saved_off!r}/{saved_on!r}')


# --- Custom buttons (CONTROLS > CUSTOMIZE) ------------------------------------------
KEYS = {'A': 1, 'B': 2, 'START': 8, 'X': 1 << 10, 'ZL': 1 << 14, 'ZR': 1 << 15}


def set_keys(keys):
    """Hold 3DS KEYS, mapped like physical buttons (mp_test_keys)."""
    with Gdb() as g:
        g.write(symbols['mp_test_keys'], keys.to_bytes(4, 'little'))


def tap_keys(keys, seconds=.3):
    set_keys(keys)
    time.sleep(seconds)
    set_keys(0)


def keys_jumped(s, keys, tries=2):
    """Stand still, then hold 3DS KEYS; True if player 1 left the ground
    (retried once, like jumped)."""
    for _ in range(tries):
        s.hold(0, 40, settle=1.5)
        if player_airborne():
            raise Outcome('error', 'player 1 is not standing on the ground')
        set_keys(keys)
        seen = watch_airborne()
        set_keys(0)
        s.hold(0, 90, settle=2.5)
        if seen:
            return True
    return False


def battle_state():
    """1 while the custom buttons apply (an unpaused match; services.c)."""
    with Gdb() as g:
        return g.word(symbols['pad_battle'], 'little')


@scenario('custom-controls')
def custom_controls(s):
    """CONTROLS > CUSTOMIZE: map ZL and A to jump ("Z-jump"). They jump in a
    match; X still jumps and ZR still grabs; A still opens menus; the
    Training menu uses the standard buttons; settings.txt keeps the mapping."""
    from sweeplib import SD
    settings = SD / 'settings.txt'
    boot_to_menu(s)
    touch(280, 225)  # CONTROLS
    s.capture('bottom-guide')
    touch(265, 14)   # CUSTOMIZE tab
    s.capture('bottom-customize')
    touch(230, 46)   # ZL
    with Gdb() as g:
        picked = g.word(symbols['mp_bottom_controls_state'], 'little')
    s.capture('bottom-picker')
    touch(90, 117)   # JUMP
    touch(80, 46)    # A
    touch(90, 117)   # JUMP
    s.capture('bottom-custom')
    touch(182, 14)   # GUIDE tab
    s.capture('bottom-guide-custom')
    touch(280, 225)  # close
    time.sleep(1)
    saved = settings.read_text() if settings.exists() else ''
    before = s.menu().get('menu')
    tap_keys(KEYS['A'])  # opens the highlighted main-menu item, as usual
    time.sleep(1.5)
    after = s.menu().get('menu')
    tap_keys(KEYS['B'])
    time.sleep(1.5)
    select_match(s, 2, 9, 25)  # Final Destination: no stage hazards
    s.wait(4)
    jumps = {name: keys_jumped(s, KEYS[name]) for name in ('ZL', 'A', 'X', 'ZR')}
    playing = battle_state()
    s.hold(0x1000, 3, settle=2)  # the Training menu
    paused = battle_state()
    s.capture('training-menu')
    s.hold(0x1000, 3, settle=2)
    s.note(f'picker state {picked:#x}; menu {before} -> {after}; jumps {jumps}; '
           f'custom buttons active: playing {playing}, Training menu {paused}')
    s.note('settings.txt: ' + ' '.join(saved.split()))
    problems = []
    if picked != 1 | 7 << 8:
        problems.append(f'picker state {picked:#x}')
    if 'button_zl=jump' not in saved or 'button_a=jump' not in saved:
        problems.append('mapping not saved')
    if before == after:
        problems.append('3DS A did not work in the main menu')
    if jumps != {'ZL': True, 'A': True, 'X': True, 'ZR': False}:
        problems.append(f'jumps {jumps}')
    if playing != 1 or paused != 0:
        problems.append(f'custom buttons playing {playing}, paused {paused}')
    if problems:
        raise Outcome('fail', '; '.join(problems))


@scenario('reload-custom-controls')
def reload_custom_controls(s):
    """After custom-controls (driver --keep-saves): the saved Z-jump works at
    start-up; RESET restores the standard layout and saves it."""
    from sweeplib import SD
    boot_to_menu(s)
    select_match(s, 2, 9, 25)  # Final Destination: no stage hazards
    s.wait(4)
    loaded = {name: keys_jumped(s, KEYS[name]) for name in ('ZL', 'ZR')}
    touch(280, 225)  # CONTROLS
    touch(265, 14)   # CUSTOMIZE
    touch(274, 199)  # RESET
    s.capture('bottom-reset')
    touch(280, 225)
    reset = {name: keys_jumped(s, KEYS[name]) for name in ('ZL', 'A')}
    time.sleep(1)
    saved = (SD / 'settings.txt').read_text()
    s.note(f'loaded {loaded}; after reset {reset}; settings.txt: ' + ' '.join(saved.split()))
    if loaded != {'ZL': True, 'ZR': False} or reset != {'ZL': False, 'A': False} or 'button_zl=grab' not in saved:
        raise Outcome('fail', f'loaded {loaded}, after reset {reset}')


# --- Boot memory-card notice in stereo (text depth vs its window) -------------------
@scenario('memcard-stereo')
def memcard_stereo(s):
    """The boot memory-card notice with the 3D slider at maximum; both eyes saved."""
    from PIL import Image
    from sweeplib import SD
    with Gdb() as g:
        g.write(symbols['mp_test_frame_limit'], (0).to_bytes(4, 'little'))
        g.write(symbols['mp_test_stereo_slider'], (1000).to_bytes(4, 'little'))
    s.wait_scene(42, 90)
    s.wait(4)
    s.capture('notice')
    data = bytearray((SD / 'engine-right.bgr').read_bytes())
    data[0::3], data[2::3] = data[2::3], data[0::3]
    Image.frombytes('RGB', (240, 400), bytes(data)).transpose(Image.Transpose.ROTATE_90).save(s.out / 'notice-right.png')
