"""Run every replay in a folder through the harness and summarise.

    python tools/slippi/replay_batch.py [FOLDER] [--out build/slippi-replays/batch.txt]
"""
import argparse
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('folder', nargs='?', default=r'C:\Users\kirby\Documents\Slippi')
    ap.add_argument('--out', default=str(ROOT / 'build/slippi-replays/batch.txt'))
    a = ap.parse_args()
    lines = []
    for slp in sorted(Path(a.folder).rglob('*.slp')):
        subprocess.run([sys.executable, str(HERE / 'replay_run.py'), str(slp)], capture_output=True, text=True)
        out = ROOT / 'build/slippi-replays' / slp.stem / 'replay-out.bin'
        if not out.exists():
            lines.append(f'== {slp.name}: NO OUTPUT')
        else:
            s = subprocess.run([sys.executable, str(HERE / 'replay_summary.py'), str(slp), str(out)],
                               capture_output=True, text=True).stdout
            lines.append(f'== {slp.name}\n{s.rstrip()}')
        print(lines[-1], flush=True)
    Path(a.out).write_text('\n'.join(lines) + '\n')


if __name__ == '__main__':
    main()
