"""Run one QA sweep scenario on one emulator instance.

    python tools/qa_sweep/driver.py --instance N --scenario NAME --out DIR [--dsx F --elf F]

The existing GDB test helpers (select_test_stage, bottom_screen_test, ...)
connect to port 24689; sweeplib redirects that port to the instance's.
Outcomes: pass, fail (engine panic), hang (frames stopped), crash (emulator
unreachable), error (the scenario could not reach its content).
"""
import argparse
import json
import os
import sys
import time
import traceback
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
ap = argparse.ArgumentParser()
ap.add_argument('--instance', type=int, required=True)
ap.add_argument('--scenario', required=True)
ap.add_argument('--out', type=Path, required=True)
ap.add_argument('--dsx', type=Path, default=ROOT / 'dist/3ds/melee/melee-development.3dsx')
ap.add_argument('--elf', type=Path, default=ROOT / 'build/game-opt/melee.elf')
ap.add_argument('--seed', type=int, default=0)
ap.add_argument('--keep-saves', action='store_true')
args = ap.parse_args()
os.environ['MP_SWEEP_INSTANCE'] = str(args.instance)
# NumPy's OpenBLAS otherwise commits ~0.5 GB of per-thread buffers per
# process, which several parallel emulators cannot spare.
os.environ.setdefault('OPENBLAS_NUM_THREADS', '1')
os.environ['MP_SWEEP_DSX'] = str(args.dsx)
os.environ['MP_TEST_ELF'] = str(args.elf)
sys.path.insert(0, str(Path(__file__).resolve().parent))
from sweeplib import HOME, Outcome, Sweep  # noqa: E402


def gpu_faults_since(started_at):
    """NVIDIA driver fault/reset events in the Windows System log since then."""
    import subprocess
    minutes = max(1, int((time.time() - started_at) / 60) + 1)
    script = ("Get-WinEvent -FilterHashtable @{LogName='System'; ProviderName='nvlddmkm'; "
              f"StartTime=(Get-Date).AddMinutes(-{minutes})}} -ErrorAction SilentlyContinue | "
              "ForEach-Object { $_.TimeCreated.ToString('HH:mm:ss') + ' id ' + $_.Id }")
    try:
        out = subprocess.run(['powershell', '-NoProfile', '-Command', script], capture_output=True,
                             text=True, timeout=60).stdout.split()
    except (OSError, subprocess.TimeoutExpired):
        return ''
    return ' '.join(out[:6])


def main():
    from scenarios import SCENARIOS
    args.out.mkdir(parents=True, exist_ok=True)
    sweep = Sweep(args.scenario, args.out, args.seed)
    sweep.started = time.monotonic()
    global started_at
    started_at = time.time()
    status, detail = 'pass', ''
    try:
        if not args.keep_saves:
            from instances import reset_saves
            reset_saves(HOME)
        sweep.launch()
        SCENARIOS[args.scenario](sweep)
        sweep.state()
    except Outcome as outcome:
        status, detail = outcome.status, outcome.detail
    except Exception:  # noqa: BLE001
        status, detail = 'error', traceback.format_exc(limit=4)
    finally:
        if status in ('fail', 'hang', 'error'):
            try:
                sweep.capture('final')
            except Exception:  # noqa: BLE001
                pass
        sweep.stop()
    errors = sweep.log_errors()
    if status == 'pass' and any('PANIC' in line or 'platform error' in line for line in errors):
        status, detail = 'fail', 'panic in log'
    # Azahar's own failures (Vulkan device loss, rasterizer-cache asserts)
    # end the emulator without anything wrong in the game's state.
    emulator_log = args.out / 'azahar_log.txt'
    emulator_text = emulator_log.read_text(encoding='utf-8', errors='replace') if emulator_log.exists() else ''
    for marker in ('ErrorDeviceLost', 'Assertion Failed!'):
        if status in ('error', 'hang', 'crash') and marker in emulator_text:
            line = next(l for l in emulator_text.splitlines() if marker in l)
            status, detail = 'emulator', f'Azahar failure: {line.strip()[:200]}'
            break
    if status in ('error', 'hang', 'crash') and gpu_faults_since(started_at):
        # A host GPU fault (NVIDIA Xid / TDR) kills Azahar's Vulkan device,
        # sometimes before it can log anything.
        status, detail = 'emulator', f'host GPU fault during the scenario: {gpu_faults_since(started_at)}'
    faults = [line for line in errors if line.startswith('MEMORY FAULT')]
    if faults and status in ('pass', 'error', 'hang', 'crash', 'emulator'):
        status, detail = 'fail', faults[0] + (f' (+{len(faults) - 1} more)' if len(faults) > 1 else '')
    result = dict(scenario=args.scenario, status=status, detail=detail, events=sweep.events,
                  scenes=sweep.scenes, shots=sweep.shots, log_errors=errors,
                  seconds=round(time.monotonic() - sweep.started, 1), dsx=str(args.dsx))
    (args.out / 'result.json').write_text(json.dumps(result, indent=2))
    print(args.scenario, status, detail.splitlines()[0] if detail else '', flush=True)


if __name__ == '__main__':
    main()
