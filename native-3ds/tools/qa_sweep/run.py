"""Run QA sweep scenarios in parallel across emulator instances.

    python tools/qa_sweep/run.py [--instances 4] [--only PREFIX ...] [--label NAME]
                                 [--dsx F --elf F] [--timeout 600]

Results: build/qa-sweep/<label>/<scenario>/{result.json,*.png,game.log} and
build/qa-sweep/<label>/report.md (failures first).
"""
import argparse
import json
import queue
import subprocess
import sys
import threading
import time
from pathlib import Path

import os
os.environ.setdefault('OPENBLAS_NUM_THREADS', '1')  # see driver.py

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--instances', type=int, default=4)
    ap.add_argument('--first-instance', type=int, default=0)
    ap.add_argument('--only', nargs='*', default=[])
    ap.add_argument('--skip', nargs='*', default=[])
    ap.add_argument('--label', default=time.strftime('%Y%m%d-%H%M'))
    ap.add_argument('--dsx', type=Path, default=ROOT / 'dist/3ds/melee/melee-development.3dsx')
    ap.add_argument('--elf', type=Path, default=ROOT / 'build/game-opt/melee.elf')
    ap.add_argument('--timeout', type=int, default=600)
    ap.add_argument('--rerun-failed', action='store_true', help='Only scenarios not passed in this label')
    args = ap.parse_args()
    import os
    os.environ.setdefault('MP_SWEEP_INSTANCE', '0')
    os.environ.setdefault('MP_SWEEP_DSX', str(args.dsx))
    os.environ.setdefault('MP_TEST_ELF', str(args.elf))
    from scenarios import SCENARIOS
    names = [n for n in SCENARIOS if (not args.only or any(n.startswith(p) for p in args.only))
             and not any(n.startswith(p) for p in args.skip)]
    out = ROOT / 'build/qa-sweep' / args.label
    if args.rerun_failed:
        names = [n for n in names if not (out / n / 'result.json').exists()
                 or json.loads((out / n / 'result.json').read_text())['status'] != 'pass']
    jobs = queue.Queue()
    for name in names:
        jobs.put(name)
    print(f'{len(names)} scenarios on {args.instances} instances -> {out}', flush=True)
    lock = threading.Lock()

    def worker(instance):
        while True:
            try:
                name = jobs.get_nowait()
            except queue.Empty:
                return
            target = out / name
            command = [sys.executable, str(HERE / 'driver.py'), '--instance', str(instance), '--scenario', name,
                       '--out', str(target), '--dsx', str(args.dsx), '--elf', str(args.elf)]
            start = time.monotonic()
            target.mkdir(parents=True, exist_ok=True)
            log = target / 'driver.log'
            try:
                # The driver's progress goes to its folder as it runs.
                with open(log, 'w', encoding='utf-8') as handle:
                    subprocess.run(command, stdout=handle, stderr=subprocess.STDOUT, text=True,
                                   timeout=args.timeout)
                tail = log.read_text(encoding='utf-8', errors='replace').strip().splitlines()[-1:] or ['']
            except subprocess.TimeoutExpired:
                pid = target / 'emulator.pid'
                if pid.exists():
                    subprocess.run(['taskkill', '/F', '/PID', pid.read_text().strip()], capture_output=True)
                target.mkdir(parents=True, exist_ok=True)
                (target / 'result.json').write_text(json.dumps(dict(scenario=name, status='timeout',
                                                   detail=f'no result within {args.timeout} s')))
                tail = ['timeout']
            with lock:
                print(f'[{instance}] {time.monotonic() - start:6.0f}s {tail[0]}', flush=True)

    threads = [threading.Thread(target=worker, args=(args.first_instance + i,)) for i in range(args.instances)]
    for t in threads:
        t.start()
        time.sleep(5)  # stagger emulator start-up
    for t in threads:
        t.join()
    report(out)


def report(out):
    results = []
    for path in sorted(out.glob('*/result.json')):
        try:
            results.append(json.loads(path.read_text()))
        except json.JSONDecodeError:
            pass
    order = {'crash': 0, 'fail': 1, 'hang': 2, 'timeout': 3, 'error': 4, 'pass': 5}
    results.sort(key=lambda r: (order.get(r['status'], 9), r['scenario']))
    counts = {}
    for r in results:
        counts[r['status']] = counts.get(r['status'], 0) + 1
    lines = [f'# QA sweep {out.name}', '', ', '.join(f'{k}: {v}' for k, v in sorted(counts.items())), '']
    for r in results:
        lines.append(f"## {r['scenario']} - {r['status']}")
        if r.get('detail'):
            lines.append('```\n' + r['detail'].strip()[:1500] + '\n```')
        if r.get('log_errors'):
            lines.append('Log: ' + ' | '.join(r['log_errors'][:6]))
        if r.get('scenes'):
            lines.append('Scenes: ' + ' '.join(str(s[0]) for s in r['scenes']))
        for shot in r.get('shots', [])[:8]:
            lines.append(f"![]({r['scenario']}/{shot})")
        lines.append('')
    (out / 'report.md').write_text('\n'.join(lines), encoding='utf-8')
    print('report:', out / 'report.md', counts, flush=True)


if __name__ == '__main__':
    main()
