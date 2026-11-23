"""Run the CSS door-mirroring spike build (build-dev with MELEE_CSS_SPIKE=1) in Azahar and collect its
"CSSSPIKE" log lines and numbered screenshots (shot-NN.bgr, written by the game itself) as PNGs.

    MELEE_CSS_SPIKE=1 docker/emu-test.sh build-dev
    docker/emu-test.sh spike [--seconds 900] [--dsx PATH]
Results: $MELEE_TEST_OUT/css-spike/ (game.log, shot-NN.png)."""
import argparse, os, shutil, struct, sys, time, zlib
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE / 'slippi'))
import emu  # noqa: E402


def png(path, rows, w, h):
    raw = b''.join(b'\0' + bytes(r) for r in rows)
    def chunk(t, d):
        c = struct.pack('>I', len(d)) + t + d
        return c + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)
    Path(path).write_bytes(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0)) +
                           chunk(b'IDAT', zlib.compress(raw, 9)) + chunk(b'IEND', b''))


def convert(src, dst):
    """engine-top.bgr: 400 columns x 240 rows, column-major, rows bottom-up, BGR."""
    d = Path(src).read_bytes()
    rows = []
    for i in range(240):
        r = bytearray()
        for j in range(400):
            o = (j * 240 + (239 - i)) * 3
            r += bytes((d[o + 2], d[o + 1], d[o]))
        rows.append(r)
    png(dst, rows, 400, 240)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--seconds', type=float, default=900)
    ap.add_argument('--until', default='', help='stop early once game.log contains this text')
    ap.add_argument('--dsx', default=os.environ.get('MELEE_DSX') or str(emu.ROOT / 'dist/3ds/melee/melee-development.3dsx'))
    ap.add_argument('--name', default='css-spike')
    args = ap.parse_args()
    out = Path(os.environ.get('MELEE_TEST_OUT') or emu.ROOT / 'build/emu-test') / 'css-spike'
    shutil.rmtree(out, ignore_errors=True)
    out.mkdir(parents=True)
    home = emu.prepare(args.name, 24860)
    sd = emu.sd(home)
    for f in sd.glob('shot-*'):
        f.unlink()
    (sd / 'game.log').unlink(missing_ok=True)
    proc = emu.launch(home, Path(args.dsx).resolve())
    done = set()
    t0 = time.monotonic()
    try:
        while time.monotonic() - t0 < args.seconds and proc.poll() is None:
            time.sleep(2)
            for f in sorted(sd.glob('shot-*.bgr')):
                if f.name not in done and time.time() - f.stat().st_mtime > 3:
                    convert(f, out / (f.stem + '.png'))
                    done.add(f.name)
                    print('shot', f.name, f'{time.monotonic() - t0:.0f}s', flush=True)
            log = sd / 'game.log'
            if args.until and log.exists() and args.until in log.read_text(errors='replace'):
                time.sleep(5)
                break
    finally:
        emu.stop(proc)
    for f in sorted(sd.glob('shot-*.bgr')):
        if f.name not in done:
            convert(f, out / (f.stem + '.png'))
    log = sd / 'game.log'
    if log.exists():
        shutil.copy2(log, out / 'game.log')
        for line in log.read_text(errors='replace').splitlines():
            if 'CSSSPIKE' in line:
                print(line)
    print('results ->', out)


if __name__ == '__main__':
    main()
