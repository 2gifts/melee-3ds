"""One-click build of Melee for New 3DS from the user's own disc (Windows).

"Build Melee CIA.bat" runs tools/easy_build/start.ps1, which fetches a
portable Python into the work folder and starts this script with it. This
fetches portable Git, builds both versions of the game and their CIAs with
the regular tools, and collects everything for the SD card:

  <work>/Melee for 3DS/Copy to SD card/   3ds/melee/... and cias/*.cia
  <work>/Melee for 3DS/What to do next.txt
  <work>/build-log.txt                    every tool's full output

Every download is pinned and hash-checked. The disc's files, and everything
made from them, stay on this computer. Each step can be repeated: a build
that stops (window closed, connection lost) continues the next time.

  python tools/easy_build.py --work C:\\MeleeBuild [--iso PATH]

The Slippi Direct beta builder (tools/make_builder_zip.py --slippi, which
writes BUILDER_VARIANT) builds only the beta: its own HOME Menu title and
3ds/melee-slippi/ folder next to the regular game, Slippi's menu files made
from the disc, and, if the user agrees, their Slippi account from this PC.
"""
import argparse
import ctypes
import hashlib
import json
import os
import shutil
import subprocess
import sys
import time
import traceback
import zipfile
from pathlib import Path

from fetch import fetch_to

HERE = Path(__file__).resolve().parents[1]  # the builder's copy of native-3ds
ISSUES = 'https://github.com/2gifts/melee-3ds/issues'
MINGIT = ('https://github.com/git-for-windows/git/releases/download/v2.56.0.windows.1/MinGit-2.56.0-64-bit.zip',
          '064b440ff870ed5198527e8f3a92cdf5bd2fd0fedf5e718af95e3fdaddeff718')
# Pillow and numpy for the HOME Menu banner, for the portable Python 3.14.
WHEELS = [
    ('https://files.pythonhosted.org/packages/f1/e0/492879f69d94f91f60fc8cd05ba03650e9520afebb2fb7aa12777d7c7f38/'
     'pillow-12.3.0-cp314-cp314-win_amd64.whl',
     'fdafc9cce40277e0f7a0feabce0ee50dd2fa1800f3b38015e51296b5e814048d', 'PIL'),
    ('https://files.pythonhosted.org/packages/a4/73/d2c08231e4fde7e415501fd02c715d96e98599b2d8384445933944152984/'
     'numpy-2.5.3-cp314-cp314-win_amd64.whl',
     '2c25dfa72943e4336ddb6b0ee4277b47a0c85bede0807530ec68103bf58e2c10', 'numpy'),
]
DISC_BYTES = 1459978240  # a full GameCube disc image
ENGINE_FILES = 1010      # compiled engine files, for the progress display
# Slippi's online character-select art (project-slippi/dolphin, GPL-2.0+),
# pinned like every other download. The builder zip never ships it.
SLPCSS = ('https://raw.githubusercontent.com/project-slippi/dolphin/41a7a3a110ed52999486ae1901c8fbb9a63d4f13/'
          'Data/Sys/GameFiles/GALE01/slpCSS.dat',
          '3b1a3f254b37cf5ce4f56b38d237d9809d0b11e72d05320f0e56fa3abd7a1692')
VARIANT = (HERE/'BUILDER_VARIANT').read_text().strip() if (HERE/'BUILDER_VARIANT').exists() else 'main'
# Tool outputs and caches in the builder's folder; never copied as source.
NOT_SOURCE = {'.toolchain', 'build', 'dist', 'assets', 'upstream', 'references', '__pycache__'}


class Stop(Exception):
    """A problem the user can fix; the message says how."""
    footer = True  # "run again / ask for help"; not for messages complete in themselves


class AlreadyRunning(Stop):
    footer = False


# --- console and log --------------------------------------------------------------
LOG = None


def say(text=''):
    print(text, flush=True)
    log(text)


def log(text):
    if LOG:
        LOG.write(text + '\n')
        LOG.flush()


def minutes(seconds):
    m = round(seconds/60)
    return 'less than a minute' if m < 1 else '1 minute' if m == 1 else f'{m} minutes'


def run(args, cwd, env, progress=None):
    """Run a tool with its output in the log; show that it is still working."""
    log('\n$ ' + ' '.join(str(a) for a in args))
    start = time.monotonic()
    shown = start
    process = subprocess.Popen([str(a) for a in args], cwd=cwd, env=env, stdout=LOG, stderr=subprocess.STDOUT,
                               stdin=subprocess.DEVNULL)
    while process.poll() is None:
        time.sleep(1)
        if time.monotonic()-shown >= 60:
            shown = time.monotonic()
            extra = progress() if progress else ''
            print(f'      still working ({minutes(shown-start)} so far{extra})', flush=True)
    LOG.flush()
    return process.returncode


def last_log_lines(count=12):
    LOG.flush()
    lines = Path(LOG.name).read_text(encoding='utf-8', errors='replace').splitlines()
    return [line for line in lines if line.strip()][-count:]


# Plain advice for the failures people actually hit, found in a tool's output.
HINTS = [
    (('CERTIFICATE_VERIFY_FAILED', 'certificate has expired', 'certificate is not yet valid', 'SEC_E_'),
     'Your PC could not check a download site\'s security certificate. Make sure the date, time\n'
     'and time zone on your PC are correct (Windows Settings > Time & language > Date & time,\n'
     'turn on "Set time automatically"), then run the builder again.'),
    (('getaddrinfo failed', '[Errno 11001]', '[Errno 11002]', 'Could not resolve host', 'Name or service not known',
      'Network is unreachable', '[WinError 10051]', '[WinError 10065]'),
     'Your PC could not reach the internet. Check your connection (Wi-Fi or cable), then run the\n'
     'builder again. School and work networks sometimes block downloads; try a home network.'),
    (('timed out', '[WinError 10060]', 'Connection reset', '[WinError 10054]', 'Connection aborted',
      'RemoteDisconnected', 'IncompleteRead', 'early EOF', 'Operation too slow'),
     'The internet connection dropped or was too slow. Run the builder again; it continues where\n'
     'it stopped.'),
    (('HTTP Error 429', 'Too Many Requests', 'toomanyrequests'),
     'A download server is busy right now. Wait about an hour, then run the builder again.'),
    (('No space left', '[Errno 28]', 'There is not enough space', '[WinError 112]', 'disk full'),
     'Your disk is full. Free up at least 8 GB (empty the Recycle Bin, delete big files you do\n'
     'not need), then run the builder again.'),
    (('Access is denied', '[WinError 5]', 'PermissionError', 'Permission denied', '[WinError 32]',
      'being used by another process', 'Operation did not complete successfully because the file contains a virus'),
     'Windows blocked a file. Close other programs that might use the MeleeBuild folder (File\n'
     'Explorer windows, editors). If you use antivirus software, it may have blocked one of the\n'
     'build tools: allow the MeleeBuild folder in it, then run the builder again.'),
]


def hint_for(text):
    for patterns, advice in HINTS:
        if any(p.lower() in text.lower() for p in patterns):
            return advice
    return None


def log_hint():
    """Advice for the most recent failure in the log, if one is recognised."""
    LOG.flush()
    text = Path(LOG.name).read_text(encoding='utf-8', errors='replace')
    return hint_for(text[text.rfind('\n$ '):])


def failure(message):
    """A Stop for a failed tool: advice for its error if known, then the log's end."""
    advice = log_hint()
    return Stop((advice or message)+'\n\nLast lines of the log (for a bug report):\n  ' +
                '\n  '.join(last_log_lines()))


# --- downloads ----------------------------------------------------------------------
def download(url, dest, sha256):
    if dest.exists() and hashlib.sha256(dest.read_bytes()).hexdigest() == sha256:
        return dest
    dest.parent.mkdir(parents=True, exist_ok=True)
    partial = dest.with_suffix(dest.suffix + '.partial')
    try:
        fetch_to(url, partial, {'User-Agent': 'melee-3ds-easy-build'})
    except OSError as error:
        raise Stop(f'Could not download {url.rsplit("/", 1)[-1]} ({error}).\n' +
                   (hint_for(str(error)) or 'Check your internet connection, then run the builder again.'))
    if hashlib.sha256(partial.read_bytes()).hexdigest() != sha256:
        partial.unlink()
        raise Stop(f'The download of {url.rsplit("/", 1)[-1]} was damaged. Run the builder again.')
    partial.replace(dest)
    return dest


def unzip(archive, dest):
    dest = dest.resolve()
    with zipfile.ZipFile(archive) as z:
        for entry in z.infolist():
            target = (dest/entry.filename).resolve()
            if not target.is_relative_to(dest):
                raise Stop(f'Unsafe file in {archive.name}')
        z.extractall(dest)


# --- the disc image -------------------------------------------------------------------
def pick_iso():
    """A Windows file dialog, in front of the console window."""
    script = ('[Console]::OutputEncoding=[Text.Encoding]::UTF8;'
              'Add-Type -AssemblyName System.Windows.Forms;'
              '$f=New-Object System.Windows.Forms.Form -Property @{TopMost=$true};'
              '$d=New-Object System.Windows.Forms.OpenFileDialog;'
              '$d.Title="Choose your Super Smash Bros. Melee disc image (US v1.02 .iso)";'
              '$d.Filter="GameCube disc image (*.iso;*.gcm)|*.iso;*.gcm|All files (*.*)|*.*";'
              'if($d.ShowDialog($f) -eq "OK"){$d.FileName}')
    result = subprocess.run(['powershell.exe', '-NoProfile', '-STA', '-Command', script],
                            capture_output=True, encoding='utf-8', errors='replace')
    path = result.stdout.strip()
    return Path(path) if path else None


def check_iso(iso):
    """Plain-language checks before anything is downloaded; assets.py then
    verifies the disc fully while extracting."""
    if iso.is_dir():
        raise Stop(f'"{iso}" is a folder. Choose the Melee .iso file itself.')
    if not iso.is_file():
        raise Stop(f'The file "{iso}" was not found. If it is on a USB drive, plug it in and try again.')
    name = iso.name.lower()
    if name.endswith(('.rvz', '.wia', '.gcz', '.ciso', '.wbfs', '.nkit.iso', '.nkit.gcz', '.zip', '.7z', '.rar')):
        raise Stop('This disc image is compressed or packed. The builder needs a plain .iso file.\n'
                   'If it is a .zip/.7z/.rar, extract it first. For .rvz, .gcz or .wia, open Dolphin,\n'
                   'right-click the game, choose "Convert File...", and pick "Uncompressed Disc (ISO)".')
    try:
        with iso.open('rb') as f:
            header = f.read(0x440)
            f.seek(0x200)
            nkit = f.read(4) == b'NKIT'
            # Read across the whole image: a cloud-only (OneDrive) or damaged
            # file fails here, not halfway through extraction.
            for offset in range(0, iso.stat().st_size, 64 << 20):
                f.seek(offset)
                f.read(4096)
    except OSError as error:
        raise Stop(f'Windows could not read "{iso.name}" ({error.strerror or error}).\n'
                   'If it is in OneDrive or on a USB drive, copy it to a normal folder on this PC\n'
                   '(for example Downloads) and choose that copy.')
    if len(header) < 0x440:
        raise Stop('This file is far too small to be a Melee disc image. Please choose your Melee .iso file.')
    if header[:4] in (b'RVZ\x01', b'WIA\x01', b'WBFS', b'CISO') or header[:4] == b'\x01\xc0\x0b\xb1':
        raise Stop('This disc image is compressed. In Dolphin, right-click the game, choose\n'
                   '"Convert File...", and pick "Uncompressed Disc (ISO)". Then use that .iso file.')
    game = header[:6]
    if game[:3] != b'GAL':
        raise Stop('This file does not look like Super Smash Bros. Melee. Please choose your Melee .iso file.')
    if game != b'GALE01':
        region = {b'GALP01': 'the European (PAL)', b'GALJ01': 'the Japanese'}.get(game, 'a non-US')
        raise Stop(f'This is {region} version of Melee. The port needs the US version (v1.02).')
    if header[7] != 2:
        raise Stop(f'This is Melee version 1.0{header[7]}. The port needs version 1.02, the most common US version.')
    if nkit or iso.stat().st_size < DISC_BYTES:
        raise Stop('This is a shrunk ("NKit" or trimmed) Melee image. The builder needs a full 1.4 GB .iso.\n'
                   'Restore it to a full image with the NKit tool, or make a fresh dump of your disc.')


# --- steps ----------------------------------------------------------------------------
class Build:
    def __init__(self, work, iso):
        self.work = work
        self.iso = iso
        self.src = work/'source'
        self.python = Path(sys.executable)
        self.git = work/'git'
        self.slippi = VARIANT == 'slippi'
        self.out = work/('Melee Slippi Beta for 3DS' if self.slippi else 'Melee for 3DS')
        self.progress_file = work/'progress.json'
        self.version = (HERE/'BUILDER_VERSION').read_text().strip() if (HERE/'BUILDER_VERSION').exists() else 'dev'
        self.done = self.load_progress()
        env = dict(os.environ)
        env['PATH'] = os.pathsep.join([str(self.git/'cmd'), str(self.python.parent), env.get('PATH', '')])
        env.update(GIT_TERMINAL_PROMPT='0', PYTHONUTF8='1', PYTHONIOENCODING='utf-8', PYTHONDONTWRITEBYTECODE='1')
        self.env = env

    def load_progress(self):
        try:
            data = json.loads(self.progress_file.read_text())
            return set(data['done']) if data.get('version') == self.version else set()
        except (OSError, ValueError, KeyError):
            return set()

    def mark(self, name):
        self.done.add(name)
        self.progress_file.write_text(json.dumps(dict(version=self.version, done=sorted(self.done)), indent=2))

    def tool(self, *args, progress=None, fail='A build step failed.'):
        code = run([self.python, *args], self.src, self.env, progress)
        if code:
            raise failure(fail)

    # 1
    def source(self):
        """Copy the builder's source into the work folder (short, ASCII path)."""
        marker = self.src/'BUILDER_VERSION'
        if self.src.resolve() == HERE.resolve():
            return
        current = marker.read_text().strip() if marker.exists() else None
        if current == self.version and (self.src/'tools/build_game.py').exists():
            return
        if current is not None and current != self.version:
            # A different builder version: rebuild, but keep downloads and
            # the extracted disc (both verified again by the tools).
            for name in ('build', 'dist'):
                shutil.rmtree(self.src/name, ignore_errors=True)
            self.done.clear()
        top = str(HERE)
        shutil.copytree(HERE, self.src, dirs_exist_ok=True,
                        ignore=lambda d, names: [n for n in names if n in NOT_SOURCE and (d == top or n == '__pycache__')])

    # 2
    def prerequisites(self):
        if not (self.git/'cmd/git.exe').exists():
            archive = download(MINGIT[0], self.work/'downloads/MinGit.zip', MINGIT[1])
            shutil.rmtree(self.git, ignore_errors=True)
            unzip(archive, self.git)
        if self.slippi:
            download(SLPCSS[0], self.work/'downloads/slpCSS.dat', SLPCSS[1])
        site = Path(sys.prefix)/'Lib/site-packages'
        for url, sha256, module in WHEELS:
            if not (site/module).exists():
                unzip(download(url, self.work/'downloads'/url.rsplit('/', 1)[-1], sha256), site)

    # 3
    def tools(self):
        if 'tools' in self.done:
            return
        for attempt in range(3):
            code = run([self.python, 'tools/bootstrap.py', '--portable-windows'], self.src, self.env)
            if not code:
                break
            # A pinned source checkout from an interrupted run: fetch it again.
            text = '\n'.join(last_log_lines(40))
            broken = [p for p in ('upstream/melee', 'references/citro3d', 'references/3dstools', 'references/picasso')
                      if str(Path(p)) in text or p in text]
            dropped = hint_for(text) is not None and 'connection dropped' in hint_for(text)
            if attempt == 2 or not (broken or dropped):
                raise failure('Downloading the build tools failed. Check your internet connection and run '
                              'the builder again.')
            say('   The download was interrupted; trying again...')
            for p in broken:
                remove_tree(self.src/p)
            time.sleep(10)
        self.tool('tools/bootstrap_home_menu.py', '--cia-only',
                  fail='Downloading the CIA tools failed. Check your internet connection and run the builder again.')
        # The 3D HOME Menu banner's converter. Optional: without it the CIAs
        # get the 2D banner (tools/package_cia.py falls back by itself).
        if run([self.python, 'tools/bootstrap_home_menu.py', '--diorama-only'], self.src, self.env):
            say('  (The 3D banner tools did not download; the CIAs will use the 2D banner.)')
        self.mark('tools')

    # 4
    def extract(self):
        assets = self.src/'assets/GALE01'
        if (assets/'manifest.json').exists():
            return
        remove_tree(assets)  # an extraction that did not finish
        self.tool('tools/assets.py', 'extract', self.iso, 'assets/GALE01',
                  fail='Reading your disc image failed. Make sure it is an unmodified US v1.02 Melee .iso.')

    # 5
    def fonts(self):
        fonts = [self.src/'build/generated/sysdolphin/baselib'/n for n in ('debug_font.inc', 'sislib_font.inc')]
        if all(f.exists() and f.stat().st_size > 1000 for f in fonts) and 'fonts' in self.done:
            return
        for f in fonts:
            f.unlink(missing_ok=True)
        self.tool('tools/assets.py', 'fonts', 'assets/GALE01/sys/main.dol', 'build/generated',
                  fail='Reading the game font from your disc failed.')
        self.mark('fonts')

    # 6
    def compile(self):
        if 'compile' in self.done:
            return
        objects = self.src/'build/engine-be8'

        def progress():
            done = len(list(objects.glob('*.sha256'))) if objects.exists() else 0
            return f', about {min(99, done*100//ENGINE_FILES)}% compiled'
        if self.slippi:
            self.tool('tools/build_game.py', '--release', '--profile', 'slippi', progress=progress,
                      fail='Compiling the game failed.')
            self.mark('compile')
            return
        self.tool('tools/build_game.py', '--release', progress=progress,
                  fail='Compiling the game failed.')
        self.tool('tools/build_game.py', '--release', '--profile', 'fresh', '--skip-engine',
                  fail='Compiling the Fresh Save version failed.')
        self.mark('compile')

    # 7
    def package(self):
        self.tool('tools/package_native.py', '--link', fail='Collecting the game files failed.')
        if self.slippi:
            # Slippi's patched menus (Online Play, Direct, the stage select)
            # from this disc's own files, and its CSS art.
            self.tool('tools/slippi/slippi_files.py', 'assets/GALE01/files', 'dist/native-alpha/3ds/melee/slippi/files',
                      '--slpcss', self.work/'downloads/slpCSS.dat',
                      fail='Making the Slippi menu files from your disc failed.')
            self.tool('tools/package_cia.py', '--generated-banner', '--profile', 'slippi',
                      fail='Making the CIA file failed.')
            return
        for profile in ('unlocked', 'fresh'):
            self.tool('tools/package_cia.py', '--generated-banner', '--profile', profile,
                      fail='Making the CIA file failed.')

    # 8
    def collect(self):
        dist = self.src/'dist'
        sd = self.out/'Copy to SD card'
        remove_tree(sd)
        melee = dist/'native-alpha/3ds/melee'
        for path in sorted(melee.rglob('*')):
            parts = path.relative_to(melee).parts
            # The beta adds Slippi's menu files and never replaces melee.3dsx.
            wanted = 'files' in parts[:1] or (self.slippi and parts[:2] == ('slippi', 'files'))
            if path.is_file() and (wanted or (path.suffix == '.3dsx' and not self.slippi)):
                link_or_copy(path, sd/'3ds/melee'/path.relative_to(melee))
        if self.slippi:
            link_or_copy(dist/'native-alpha/3ds/melee-slippi/melee-slippi.3dsx', sd/'3ds/melee-slippi/melee-slippi.3dsx')
            link_or_copy(dist/'home-menu/melee-slippi-beta.cia', sd/'cias/melee-slippi-beta.cia')
        else:
            for name in ('melee-3ds.cia', 'melee-3ds-fresh.cia'):
                link_or_copy(dist/'home-menu'/name, sd/'cias'/name)
        for name in ('THIRD_PARTY_NOTICES.md', 'CITRO3D-LICENSE.txt', 'DOLPHIN-LICENSE.txt', 'ENET-LICENSE.txt'):
            if (dist/'native-alpha'/name).exists():
                link_or_copy(dist/'native-alpha'/name, self.out/'Licenses'/name)
        next_steps = NEXT_STEPS_SLIPPI if self.slippi else NEXT_STEPS
        (self.out/'What to do next.txt').write_text(next_steps.format(work=self.work), encoding='utf-8')
        if self.slippi:
            guide = self.src/'docs/slippi/SLIPPI_BETA.md'
            if guide.exists():
                shutil.copyfile(guide, self.out/'Slippi beta guide.txt')

    # 7 (Slippi beta)
    def account(self):
        """Offer the Slippi account from this PC's Slippi Launcher. Its login key
        stays in the SD folder; it is never printed or written to the log."""
        dest = self.out/'Copy to SD card/3ds/melee/slippi/user.json'
        appdata = os.environ.get('APPDATA')
        source = Path(appdata)/'Slippi Launcher/netplay/User/Slippi/user.json' if appdata else None
        try:
            user = json.loads(source.read_text(encoding='utf-8')) if source and source.is_file() else None
        except (OSError, ValueError):
            user = None
        if not user or not user.get('playKey') or not user.get('connectCode'):
            say('   No Slippi Launcher login was found on this PC. Before playing online, copy your')
            say('   user.json to SD:/3ds/melee/slippi/ (see "What to do next.txt").')
            return
        name = str(user.get('displayName') or '').encode('ascii', 'replace').decode()
        code = str(user.get('connectCode')).encode('ascii', 'replace').decode()
        say(f'   Found the Slippi account on this PC: {name} ({code}).')
        say('   The 3DS plays online as this account. Its login key is copied to the SD card only.')
        try:
            answer = input('   Put this account on the SD card? Type Y and press Enter (Enter alone skips): ')
        except EOFError:
            answer = ''
        if answer.strip().lower() not in ('y', 'yes'):
            say('   Skipped. Copy user.json yourself before playing online (see "What to do next.txt").')
            return
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, dest)
        log('Slippi account copied to the SD folder')
        say('   Added. Keep the "Copy to SD card" folder private: user.json is your Slippi login.')


def remove_tree(path):
    def writable(func, target, _):
        os.chmod(target, 0o700)  # git marks its object files read-only
        func(target)
    if path.exists():
        shutil.rmtree(path, onexc=writable)


def link_or_copy(source, dest):
    dest.parent.mkdir(parents=True, exist_ok=True)
    dest.unlink(missing_ok=True)
    try:
        os.link(source, dest)
    except OSError:
        shutil.copyfile(source, dest)


NEXT_STEPS = """Melee for New 3DS - your build is ready
========================================

1. Put your 3DS's SD card in this computer.

2. Open the "Copy to SD card" folder. Select everything inside it (the "3ds"
   and "cias" folders) and copy it to the SD card. If Windows asks about
   files that already exist, choose "Replace the files in the destination".

3. Safely eject the SD card and put it back in your 3DS.

4. On the 3DS, open FBI, then choose: SD > cias. Install:
     melee-3ds.cia        Super Smash Bros. Melee (everything unlocked)
     melee-3ds-fresh.cia  Melee: Fresh Save (unlock things by playing)
   You can install one or both. They keep separate saves.

5. Go back to the HOME Menu and start Melee.

Keep the 3ds/melee/files folder on the SD card: the game reads its files
from there. Homebrew Launcher users can also start melee.3dsx.

Updating later: download the newest builder, build again, and copy the
"Copy to SD card" contents over the old ones, then install the new CIA
files in FBI. Your saves are kept.

Problems? The full log is {work}\\build-log.txt. Please attach it when
asking for help at https://github.com/2gifts/melee-3ds/issues
"""

NEXT_STEPS_SLIPPI = """Melee for New 3DS - Slippi Direct beta - your build is ready
============================================================

This is a BETA. It plays Slippi Direct (connect code) matches against a
friend on Slippi Dolphin (PC). Read "Slippi beta guide.txt" for how it
works and how it differs from Slippi on a PC.

1. Put your 3DS's SD card in this computer.

2. Open the "Copy to SD card" folder. Select everything inside it (the "3ds"
   and "cias" folders) and copy it to the SD card. If Windows asks about
   files that already exist, choose "Replace the files in the destination".
   This adds the beta next to the regular game; it does not replace it.

3. Your Slippi account. If the builder did not add it, copy user.json from
   %APPDATA%\\Slippi Launcher\\netplay\\User\\Slippi\\user.json
   on the PC where you log in to Slippi Launcher, to SD:/3ds/melee/slippi/
   user.json is your Slippi login: do not share it or post it anywhere.

4. Safely eject the SD card and put it back in your 3DS.

5. On the 3DS, open FBI, then choose: SD > cias, and install
     melee-slippi-beta.cia     Melee: Slippi Direct beta (green icon)
   Homebrew Launcher users can start 3ds/melee-slippi/melee-slippi.3dsx.

6. Start it, then choose 1-P Mode > Online Play > Direct. Pick your fighter, press
   START, type your friend's connect code and confirm. Your friend chooses
   Direct in Slippi Launcher and types your code.

Keep the 3ds/melee/files folder on the SD card: both the regular game and
the beta read the game files from there. The beta keeps its own saves in
3ds/melee/saves/slippi (copied once from the regular game's saves).

Problems? Right after the problem, copy SD:/3ds/melee/game.log and your
friend's replay (.slp) from the PC, and report it at
https://github.com/2gifts/melee-3ds/issues (choose "Slippi beta problem").
The build log is {work}\\build-log.txt.
"""


# --- the SD card ------------------------------------------------------------------------
def sd_cards(exclude):
    """Drives that hold a 3DS SD card (a "Nintendo 3DS" folder)."""
    kernel = ctypes.windll.kernel32
    mask = kernel.GetLogicalDrives()
    found = []
    for i in range(26):
        letter = chr(65+i)
        root = Path(f'{letter}:\\')
        if not mask >> i & 1 or letter in exclude or kernel.GetDriveTypeW(f'{letter}:\\') not in (2, 3):
            continue
        try:
            if (root/'Nintendo 3DS').is_dir():
                found.append(root)
        except OSError:
            pass
    return found


def copy_to_sd(sd_folder, card, src):
    """Merge the build into the card: reorganize an older flat files folder
    first, skip game files already there, never touch saves or settings."""
    files = card/'3ds/melee/files'
    if files.is_dir():
        import contextlib
        import io
        sys.path.insert(0, str(src/'tools'))
        from sd_layout import organize
        report = io.StringIO()
        with contextlib.redirect_stdout(report):
            organize(files)
        for line in report.getvalue().splitlines():
            say('   Older game files on the card: ' + line)
    items = [p for p in sd_folder.rglob('*') if p.is_file()]
    # Never replace a Slippi login already on the card with another account.
    account = Path('3ds/melee/slippi/user.json')
    if (sd_folder/account).is_file() and (card/account).is_file():
        def code(path):
            try:
                return json.loads(path.read_text(encoding='utf-8')).get('connectCode')
            except (OSError, ValueError, AttributeError):
                return None
        theirs = code(card/account)
        if theirs and theirs != code(sd_folder/account):
            items.remove(sd_folder/account)
            shown_code = str(theirs).encode('ascii', 'replace').decode()
            say(f'   The SD card already has a different Slippi account ({shown_code}); it was kept.')
            say('   To use the other account, copy user.json to SD:/3ds/melee/slippi/ yourself.')
    total = sum(p.stat().st_size for p in items)

    def needed(path):
        dest = card/path.relative_to(sd_folder)
        return path.suffix in ('.3dsx', '.cia') or not dest.is_file() or dest.stat().st_size != path.stat().st_size
    missing = sum(p.stat().st_size for p in items if needed(p))
    free = shutil.disk_usage(card).free
    if missing > free:
        raise Stop(f'the card needs {missing/(1 << 30):.1f} GB free but has {free/(1 << 30):.1f} GB.')
    copied = 0
    shown = -1
    for path in items:
        dest = card/path.relative_to(sd_folder)
        size = path.stat().st_size
        if needed(path):
            dest.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(path, dest)
        copied += size
        percent = copied*100//max(total, 1)
        if percent//10 != shown:
            shown = percent//10
            print(f'      {percent}% copied', flush=True)


def offer_sd_copy(build):
    cards = sd_cards({build.work.drive[:1].upper(), 'C'})
    if not cards:
        say('   No 3DS SD card is plugged in. Copy the files yourself (see "What to do next.txt").')
        return
    card = cards[0]
    say(f'   Found a 3DS SD card in {card}.')
    try:
        answer = input(f'   Copy everything to the SD card in {card} now? Type Y and press Enter (Enter alone skips): ')
    except EOFError:
        answer = ''
    if answer.strip().lower() not in ('y', 'yes'):
        say('   Skipped. Copy the files yourself (see "What to do next.txt").')
        return
    log(f'Copying to {card}')
    # The build itself is finished; a failed copy only means copying by hand.
    try:
        copy_to_sd(build.out/'Copy to SD card', card, build.src)
    except (OSError, Stop) as error:
        log(traceback.format_exc())
        say(f'   Copying to the SD card did not work: {error}')
        say('   Copy the "Copy to SD card" folder yourself (see "What to do next.txt").')
        return
    say(f'   Done. Safely eject the SD card ({card}) before removing it, then install the CIA files with FBI.')


# --- guards against common mistakes --------------------------------------------------
def no_click_pause():
    """Turn off the console's QuickEdit mode. With it, one click in the window
    starts a text selection that silently pauses the build until Esc."""
    try:
        kernel = ctypes.windll.kernel32
        handle = kernel.GetStdHandle(-10)  # STD_INPUT_HANDLE
        mode = ctypes.c_uint32()
        if kernel.GetConsoleMode(handle, ctypes.byref(mode)):
            kernel.SetConsoleMode(handle, (mode.value & ~0x40) | 0x80)  # -QUICK_EDIT, +EXTENDED_FLAGS
    except (AttributeError, OSError):
        pass


def single_instance(work):
    """Hold a lock for the whole run; a second window would overwrite the files
    this one is making."""
    import msvcrt
    lock = (work/'builder.lock').open('a+b')
    try:
        msvcrt.locking(lock.fileno(), msvcrt.LK_NBLCK, 1)
    except OSError:
        raise AlreadyRunning('The builder is already running in another window. Use that window, or close it\n'
                   'and start the builder again.')
    return lock


def check_clock():
    """Secure downloads fail with a wrong date; catch it before they do."""
    if time.localtime().tm_year < 2026:
        raise Stop(f'Your PC\'s date is set to {time.strftime("%d %B %Y")}, which is wrong. Downloads cannot\n'
                   'work until it is correct. In Windows Settings > Time & language > Date & time, turn\n'
                   'on "Set time automatically" (or set the right date), then run the builder again.')


def explain_os_error(error):
    """Plain words for disk, permission and file-lock problems outside the tools."""
    text = f'{type(error).__name__}: {error}'
    return hint_for(text) or f'Windows reported a problem with a file: {error}'


# --- main ---------------------------------------------------------------------------
def main():
    global LOG
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--work', type=Path, required=True)
    ap.add_argument('--iso', type=Path)
    ap.add_argument('--no-sd', action='store_true', help='Do not offer to copy to an SD card')
    ap.add_argument('--no-open', action='store_true', help='Do not open the result folder')
    args = ap.parse_args()
    work = args.work.resolve()
    work.mkdir(parents=True, exist_ok=True)
    LOG = (work/'build-log.txt').open('a', encoding='utf-8', errors='replace')
    log(f'\n===== {time.strftime("%Y-%m-%d %H:%M:%S")} builder {HERE} python {sys.version.split()[0]}')
    started = time.monotonic()
    # Keep the PC from sleeping while this window runs (ES_CONTINUOUS|ES_SYSTEM_REQUIRED).
    ctypes.windll.kernel32.SetThreadExecutionState(0x80000001)
    no_click_pause()
    try:
        lock = single_instance(work)  # noqa: F841 - held until the process exits
        check_clock()
        build = Build(work, args.iso)
        extracted = (build.src/'assets/GALE01/manifest.json').exists()
        steps = 8 if build.slippi else 7
        if build.slippi:
            say('Slippi Direct beta: builds the beta next to the regular game, not instead of it.')
        say(f'Step 1 of {steps}: Checking your disc image')
        if not extracted:
            if not build.iso:
                say('   Choose your Melee disc image in the window that opens...')
                build.iso = pick_iso()
                if not build.iso:
                    raise Stop('No disc image was chosen. Run the builder again and choose your Melee .iso file.')
            check_iso(build.iso)
            say(f'   OK: {build.iso.name} is Super Smash Bros. Melee (US v1.02)')
            free = shutil.disk_usage(work).free
            if free < 8 << 30:
                raise Stop(f'Not enough free space on {work.drive}: the build needs about 8 GB '
                           f'and {free/(1 << 30):.1f} GB is free.')
        else:
            say('   Your disc was already read in an earlier run.')
        build.source()
        say(f'Step 2 of {steps}: Downloading build tools (about 500 MB the first time)')
        build.prerequisites()
        build.tools()
        say(f'Step 3 of {steps}: Reading the game files from your disc (a few minutes)')
        build.extract()
        build.fonts()
        say(f'Step 4 of {steps}: Compiling the game (a few minutes; up to 20 on slower PCs)')
        build.compile()
        say(f'Step 5 of {steps}: Making the HOME Menu icons and CIA files')
        build.package()
        say(f'Step 6 of {steps}: Collecting everything for your SD card')
        build.collect()
        if build.slippi:
            say(f'Step 7 of {steps}: Your Slippi account')
            build.account()
        say(f'Step {steps} of {steps}: SD card')
        if args.no_sd:
            say('   Skipped.')
        else:
            offer_sd_copy(build)
        say('')
        say(f'All done in {minutes(time.monotonic()-started)}. Your files are in:')
        say(f'   {build.out}')
        say('Open "What to do next.txt" there for the last steps on your 3DS.')
        if not args.no_open:
            os.startfile(build.out)
        return 0
    except Stop as problem:
        say('')
        say('The build stopped:' if problem.footer else 'The builder did not start:')
        say(str(problem))
        if not problem.footer:
            return 1
    except KeyboardInterrupt:
        say('\nStopped. Run the builder again to continue where it stopped.')
    except OSError as error:
        log(traceback.format_exc())
        say('')
        say('The build stopped:')
        say(explain_os_error(error))
    except Exception:  # noqa: BLE001 - a bug; keep the details
        log(traceback.format_exc())
        say('')
        say('The build stopped because of an unexpected error:')
        say('   ' + traceback.format_exc().strip().splitlines()[-1])
        advice = hint_for(traceback.format_exc())
        if advice:
            say(advice)
    say('')
    say(f'The full log is {work}\\build-log.txt')
    say('Run the builder again to retry; finished steps are not repeated.')
    say(f'If it keeps failing, ask for help at {ISSUES} and attach the log.')
    return 1


if __name__ == '__main__':
    sys.exit(main())
