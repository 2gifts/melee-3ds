"""Library for the QA sweep driver (see driver.py). Configured by the
environment: MP_SWEEP_INSTANCE (index), MP_TEST_ELF, MP_SWEEP_DSX."""
import json
import os
import random
import re
import shutil
import socket
import struct
import subprocess
import sys
import time
from pathlib import Path

import psutil

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
sys.path.insert(0, str(Path(__file__).resolve().parent))
from instances import prepare  # noqa: E402

HOME, PORT = prepare(int(os.environ['MP_SWEEP_INSTANCE']))
DSX = Path(os.environ['MP_SWEEP_DSX'])
SD = HOME / 'user/sdmc/3ds/melee'
_create = socket.create_connection


def _redirect(address, *rest, **kw):
    host, port = address
    return _create((host, PORT if port == 24689 else port), *rest, **kw)


socket.create_connection = _redirect
import bottom_screen_test as bottom  # noqa: E402
import select_test_stage as select  # noqa: E402
from gameplay_test import packet, receive, symbols  # noqa: E402

ERROR_PATTERNS = re.compile(r'PANIC|platform error|Cannot find symbol|Unhandled|Data abort|Prefetch abort|assert|Assertion|Unaligned device|userdata conflict|Invalid material|Cannot load|Can not')


class Outcome(Exception):
    def __init__(self, status, detail):
        super().__init__(detail)
        self.status = status
        self.detail = detail


class Gdb:
    def __enter__(self):
        self.s = socket.create_connection(('127.0.0.1', PORT), 20)
        self.s.settimeout(30)
        packet(self.s, '?')
        receive(self.s)
        return self

    def read(self, address, size):
        packet(self.s, f'm{address:x},{size:x}')
        return bytes.fromhex(receive(self.s))

    def write(self, address, data):
        packet(self.s, f'M{address:x},{len(data):x}:' + data.hex())
        if receive(self.s) != 'OK':
            raise Outcome('error', f'GDB write failed at {address:x}')

    def word(self, address, endian='big'):
        return int.from_bytes(self.read(address, 4), endian)

    def __exit__(self, *exc):
        try:
            packet(self.s, 'c')
            packet(self.s, 'D')
            receive(self.s)
        finally:
            self.s.close()


class Sweep:
    def __init__(self, name, out, seed):
        self.name = name
        self.out = out
        self.rng = random.Random(seed or __import__("zlib").crc32(name.encode()))
        self.events = []
        self.shots = []
        self.scenes = []
        self.proc = None
        self.sequence = int(time.time()) & 0xffff
        self.last_frame = None
        self.last_progress = time.monotonic()

    # --- emulator ---
    def note(self, text):
        stamp = round(time.monotonic() - self.started, 1)
        self.events.append(f'{stamp:7.1f}s {text}')
        print(self.name, stamp, text, flush=True)

    def launch(self, attempt=0):
        self.started = getattr(self, 'started', None) or time.monotonic()
        # The previous emulator on this instance may still hold the port.
        for _ in range(60):
            if not self._listening():
                break
            time.sleep(1)
        (SD / 'game.log').unlink(missing_ok=True)
        info = subprocess.STARTUPINFO()
        info.dwFlags |= subprocess.STARTF_USESHOWWINDOW
        info.wShowWindow = 0
        self.proc = subprocess.Popen([str(HOME / 'azahar.exe'), str(DSX)], startupinfo=info)
        (self.out / 'emulator.pid').write_text(str(self.proc.pid))
        # Watch for the listening socket instead of probing it: a client that
        # connects and drops while the emulator boots can wedge Azahar's stub.
        # A boot normally listens within two seconds; a minute means a hang.
        reason = 'GDB port never opened'
        deadline = time.monotonic() + 60
        opened = False
        while time.monotonic() < deadline and self.proc.poll() is None:
            if self._listening(self.proc.pid):
                opened = True
                break
            time.sleep(.25)
        if opened:
            try:
                with Gdb() as g:
                    g.write(symbols['mp_test_frame_limit'], (0).to_bytes(4, 'little'))
            except OSError as error:
                opened, reason = False, f'GDB handshake failed: {error!r}'
        if not opened:
            code = self.proc.poll()
            self.stop()
            if code is not None:
                reason += f', exit code {code}'
            if attempt < 2:
                self.note(f'emulator did not start ({reason}); retrying')
                time.sleep(5)
                return self.launch(attempt + 1)
            raise Outcome('crash', f'emulator did not start after 3 attempts ({reason})')
        self.note('booted' + (f' on attempt {attempt + 1}' if attempt else ''))

    @staticmethod
    def _listening(pid=None):
        try:
            conns = psutil.Process(pid).net_connections('tcp') if pid else psutil.net_connections('tcp')
        except psutil.Error:
            return False
        return any(c.status == psutil.CONN_LISTEN and c.laddr and c.laddr.port == PORT for c in conns)

    def stop(self):
        if self.proc and self.proc.poll() is None:
            self.proc.kill()
            self.proc.wait()

    # --- state ---
    def state(self):
        # A loaded host can delay the GDB stub; only a minute of silence or
        # an exited emulator counts.
        deadline = time.monotonic() + 60
        while True:
            try:
                s = bottom.snapshot()
                break
            except (OSError, TimeoutError, socket.timeout) as error:
                if self.proc.poll() is not None:
                    raise Outcome('crash', 'emulator exited')
                if time.monotonic() > deadline:
                    raise Outcome('hang', f'emulator unresponsive to GDB for 60 s: {error}')
                time.sleep(2)
        if s['engine_failed']:
            raise Outcome('fail', 'engine reported a platform error / panic')
        frame = s['engine_frames']
        if frame != self.last_frame:
            self.last_frame = frame
            self.last_progress = time.monotonic()
        elif time.monotonic() - self.last_progress > 45:
            raise Outcome('hang', f'engine frames stopped at {frame} for 45 s')
        if not self.scenes or self.scenes[-1][0] != s['scene']:
            self.scenes.append((s['scene'], frame))
            self.note(f'scene {s["scene"]} mode {s["mode"]} frame {frame}')
        return s

    def menu(self):
        return select.observe()

    # --- input ---
    def pad(self, buttons=0, frames=2, x=0, y=0, cx=0, cy=0):
        self.sequence = (self.sequence + 1) & 0xffffffff or 1
        data = struct.pack('<IIIiiii', self.sequence, buttons, frames, x, y, cx, cy)
        with Gdb() as g:
            g.write(symbols['mp_test_control'], data)

    def hold(self, buttons=0, frames=2, x=0, y=0, cx=0, cy=0, settle=None):
        self.pad(buttons, frames, x, y, cx, cy)
        time.sleep(settle if settle is not None else max(.05, frames / 45))

    def wait(self, seconds, poll=1.0):
        end = time.monotonic() + seconds
        while time.monotonic() < end:
            self.state()
            time.sleep(min(poll, max(0, end - time.monotonic())))

    def wait_scene(self, scenes, timeout, press=None, every=2.0):
        """Wait until the scene is one of SCENES, optionally pressing buttons."""
        if isinstance(scenes, int):
            scenes = (scenes,)
        end = time.monotonic() + timeout
        next_press = time.monotonic() + every
        while time.monotonic() < end:
            s = self.state()
            if s['scene'] in scenes:
                return s
            if press and time.monotonic() >= next_press:
                self.hold(press, 3)
                next_press = time.monotonic() + every
            time.sleep(.3)
        raise Outcome('error', f'scene {scenes} not reached (at {self.scenes[-1] if self.scenes else None})')

    # --- navigation ---
    def to_main_menu(self):
        with Gdb() as g:
            g.write(symbols['mp_test_frame_limit'], (0).to_bytes(4, 'little'))
        for _ in range(90):
            self.state()
            m = self.menu()
            if 'menu' in m and m['menu']['state'] == 0 and m['menu']['kind'] == 0:
                self.note('main menu')
                return
            scene = self.scenes[-1][0] if self.scenes else -1
            # Memory-card prompts (scene 42) accept A: create the save.
            self.hold(0x100 if scene == 42 else 0x1000, 2, settle=.4)
        raise Outcome('error', 'main menu not reached')

    def mainlib_base(self):
        with Gdb() as g:
            return g.word(symbols['gmMainLib_804D3EE0'])

    def poke_mainlib(self, offset, data):
        base = self.mainlib_base()
        with Gdb() as g:
            g.write(base + offset, data)

    def jump_mode(self, mode):
        """Leave the main menu for game mode MODE (dev-only override)."""
        sm = symbols['state_machine']
        flag = symbols['gm_80479D58']
        with Gdb() as g:
            g.write(symbols['mp_test_mode_override'], bytes([mode]))
            g.write(sm + 0xC, b'\x01')
            g.write(flag + 0xC, (1).to_bytes(4, 'big'))
        self.note(f'jump to game mode {mode:#x}')

    def choose(self, icon, start=True):
        """At character select, take ICON for P1 and optionally start."""
        self.wait_scene(8, 60)
        time.sleep(2)
        css = self.menu()
        try:
            bottom.choose(css, 0, icon)
        except Exception as error:  # noqa: BLE001 - recorded, then continue
            raise Outcome('error', f'character select failed: {error}')
        self.hold(0, 2, settle=.5)
        if start:
            self.hold(0x1000, 4, settle=1.0)
        self.note(f'chose character icon {icon}')

    def ko_cpus(self):
        """Push every fighter but P1 below the blast zone."""
        # Only in a match: the results screen's loser models are fighters
        # too, and moving them leaves their portrait captures empty.
        if self.state()['scene'] not in (2, 3, 4):
            return
        try:
            with Gdb() as g:
                head = g.word(g.word(symbols['HSD_GObjPLinkHead']) + 32)
                gobj, index = head, 0
                while gobj and index < 8:
                    fp = g.word(gobj + 0x2c)
                    if index > 0 and fp:
                        if g.word(fp + 4) in (0x1B, 0x1C):
                            # Master/Crazy Hand have HP, not blast zones, and
                            # moving them strands their attached laser items.
                            # Exhaust the HP; the next hit defeats them.
                            g.write(fp + 0x1830, struct.pack('>f', 999.0))
                        else:
                            g.write(fp + 0xb4, struct.pack('>f', -2000.0))
                    gobj = g.word(gobj + 8)
                    index += 1
        except Outcome:
            raise
        except Exception as error:  # noqa: BLE001
            self.note(f'ko failed: {error}')

    def monkey(self, seconds, cstick=True, ko_every=None, allow_pause=False):
        """Random fighter input; checks health throughout."""
        end = time.monotonic() + seconds
        next_check = time.monotonic()
        next_ko = time.monotonic() + ko_every if ko_every else None
        rng = self.rng
        while time.monotonic() < end:
            roll = rng.random()
            x = rng.choice((-80, -40, 0, 0, 40, 80))
            y = rng.choice((-80, 0, 0, 0, 80))
            if roll < .22:
                self.hold(0x100, rng.randint(2, 6), x, y)            # attacks
            elif roll < .40:
                self.hold(0x200, rng.randint(2, 8), x, y)            # specials
            elif roll < .50:
                self.hold(0x400, rng.randint(2, 5), x, 0)            # jump
            elif roll < .57:
                self.hold(0x20, rng.randint(3, 12), x, y)            # shield / dodge
            elif roll < .62:
                self.hold(0x10, rng.randint(2, 4))                   # grab / Z
            elif roll < .75 and cstick:
                self.hold(0, rng.randint(2, 5), 0, 0, rng.choice((-80, 80, 0)), rng.choice((-80, 80, 0)))
            elif roll < .78:
                self.hold(0x8, 3)                                    # taunt
            elif roll < .80 and allow_pause:
                self.hold(0x1000, 2, settle=.6)
                self.hold(0x1000, 2, settle=.6)
            else:
                self.hold(0, rng.randint(4, 20), x, 0)               # run / walk
            if time.monotonic() >= next_check:
                self.state()
                next_check = time.monotonic() + 2
            if next_ko and time.monotonic() >= next_ko:
                self.ko_cpus()
                next_ko = time.monotonic() + ko_every
        self.hold(0, 2)

    def capture(self, label):
        try:
            with Gdb() as g:
                g.write(symbols['mp_test_capture'], (1).to_bytes(4, 'little'))
            end = time.monotonic() + 30
            while time.monotonic() < end:
                time.sleep(.25)
                with Gdb() as g:
                    if g.word(symbols['mp_test_capture'], 'little') == 0:
                        break
            from PIL import Image
            data = bytearray((SD / 'engine-top.bgr').read_bytes())
            data[0::3], data[2::3] = data[2::3], data[0::3]
            image = Image.frombytes('RGB', (240, 400), bytes(data)).transpose(Image.Transpose.ROTATE_90)
            name = f'{len(self.shots):02d}-{label}.png'
            image.save(self.out / name)
            self.shots.append(name)
            bottom_file = SD / 'engine-bottom.rgb565'
            if label.startswith('bottom') and bottom_file.exists():
                raw = bottom_file.read_bytes()
                pixels = [struct.unpack_from('<H', raw, i * 2)[0] for i in range(320 * 240)]
                rgb = bytes(c for v in pixels for c in ((v >> 11) * 255 // 31, (v >> 5 & 63) * 255 // 63, (v & 31) * 255 // 31))
                lower = Image.frombytes('RGB', (240, 320), rgb).transpose(Image.Transpose.ROTATE_90)
                lower.save(self.out / name.replace('.png', '-bottom.png'))
        except Outcome:
            raise
        except Exception as error:  # noqa: BLE001
            self.note(f'capture failed: {error}')

    def log_errors(self):
        found = []
        emulator_log = HOME / 'user/log/azahar_log.txt'
        if emulator_log.exists():
            shutil.copyfile(emulator_log, self.out / 'azahar_log.txt')
            found = self.memory_faults(emulator_log)
        log = SD / 'game.log'
        if not log.exists():
            return found
        shutil.copyfile(log, self.out / 'game.log')
        lines = log.read_text(encoding='utf-8', errors='replace').splitlines()
        return found + [line for line in lines if ERROR_PATTERNS.search(line)][:40]

    @staticmethod
    def memory_faults(emulator_log):
        """Azahar logs an unmapped access and carries on; a 3DS takes a data
        abort. Report each faulting PC with its function."""
        text = emulator_log.read_text(encoding='utf-8', errors='replace')
        faults = re.findall(r'unmapped (\w+) (?:0x[0-9A-F]+ )?@ (0x[0-9A-F]+) at PC (0x[0-9A-F]+)', text)
        if not faults:
            return []
        pcs = sorted({pc for _, _, pc in faults})
        try:
            names = subprocess.check_output(
                [str(ROOT / '.toolchain/llvm-mingw-20260908-ucrt-x86_64/bin/llvm-addr2line.exe'), '-f', '-e',
                 os.environ['MP_TEST_ELF'], *pcs], text=True).splitlines()[::2]
        except (OSError, subprocess.CalledProcessError):
            names = ['?'] * len(pcs)
        where = dict(zip(pcs, names))
        return [f'MEMORY FAULT {kind} @ {address} at PC {pc} ({where.get(pc, "?")})'
                for kind, address, pc in faults[:10]]


