"""Summarize per-match performance from a console game.log (SD:/3ds/melee/game.log).

Groups consecutive in-match render contexts (scene=2) by stage, item setting
and roster, and prints medians of the periodic reports. Pass two logs to
compare a baseline against a new build. Label console and emulator results
separately; only console timings are meaningful.

    python tools/summarize_console_log.py old-game.log [new-game.log]
"""
import re
import statistics
import sys
from pathlib import Path

STAGES = {2: 'Fountain of Dreams', 3: 'Pokemon Stadium', 4: "Peach's Castle", 5: 'Kongo Jungle', 6: 'Brinstar',
          7: 'Corneria', 8: "Yoshi's Story", 9: 'Onett', 10: 'Mute City', 11: 'Rainbow Cruise', 12: 'Jungle Japes',
          13: 'Great Bay', 14: 'Temple', 15: 'Brinstar Depths', 16: "Yoshi's Island", 17: 'Green Greens',
          18: 'Fourside', 19: 'Mushroom Kingdom I', 20: 'Mushroom Kingdom II', 22: 'Venom', 23: 'Poke Floats',
          24: 'Big Blue', 25: 'Icicle Mountain', 27: 'Flat Zone', 28: 'Dream Land 64', 29: "Yoshi's Island 64",
          30: 'Kongo Jungle 64', 31: 'Battlefield', 32: 'Final Destination'}
TICKS_PER_MS = 40500.0
PATTERNS = {
    'fps': (r'^Rates: ([\d.]+) render FPS, ([\d.]+) game Hz; display=\S+ stereo=(\d+)', None),
    'pacing': (r'^Frame pacing/120: p50=([\d.]+) p95=([\d.]+) p99=([\d.]+) max=([\d.]+) ms', None),
    'profile': (r'^Profile/60 frames list=(\d+) submit=(\d+) copy=(\d+) audio=(\d+) ticks', 60 * TICKS_PER_MS),
    'worker': (r'^Renderer CPU/120 frames .* copy=([\d.]+) ms consume=([\d.]+) ms wait=([\d.]+) ms', None),
    'engine': (r'^Original engine frame \d+: (\d+) vertices, (\d+) draws; frame ([\d.]+) ms', None),
    'gpu': (r'^GPU/60 frames wait=([\d.]+) ms; completed tail mean=([\d.]+) ms', None),
    'threads': (r'^Thread load/60 frames: engine=([\d.-]+) translator=([\d.-]+) renderer=([\d.-]+)(?: gpu=([\d.-]+))? ms', None),
    'probe': (r'^GPU probe stereo=\d+ width=\d+ draws=\d+ vertices=\d+: full=([\d.]+) no-fragments=([\d.]+) '
              r'one-triangle=([\d.]+) flat-texture=([\d.]+) one-eye=([\d.]+)(?: keep-texture-cache=([\d.]+))? ms', None),
    'cpushade': (r'^CPU shading/60 frames vertices: off=(\d+) no-plan=(\d+) lit-alpha=(\d+) lit-vertex-colour=(\d+) '
                 r'colour1-vertex=(\d+) lights>4=(\d+) spot=(\d+) merged=(\d+)', 60),
    'setup': (r'^Primitive setup/60 frames GXBegin=(\d+) immediate-vertices=(\d+) ticks; colour-only material updates=(\d+)',
              (60 * TICKS_PER_MS, 60 * TICKS_PER_MS, 60)),
}


def encounters(path):
    groups, current = [], None
    for line in Path(path).read_text(encoding='utf-8', errors='replace').splitlines():
        m = re.match(r'Render context: scene=(\d+) mode=\d+ stage=(\d+) items=(\d+) players=(\S+)', line)
        if m:
            scene, stage, items, players = int(m[1]), int(m[2]), int(m[3]), m[4]
            key = (stage, items, players)
            if scene != 2: current = None
            elif not current or current['key'] != key:
                current = {'key': key, 'rows': {k: [] for k in PATTERNS}}; groups.append(current)
            continue
        if not current: continue
        for name, (pattern, scale) in PATTERNS.items():
            m = re.match(pattern, line)
            if m:
                values = [float(v) if v is not None else float('nan') for v in m.groups()]
                scales = scale if isinstance(scale, tuple) else (scale,) * len(values)
                current['rows'][name].append([v / k if k else v for v, k in zip(values, scales)])
    # Drop warm-up reports: first two of each kind per encounter.
    return [g for g in groups if sum(len(r) for r in g['rows'].values()) > 8]


def median(rows, column, skip=2):
    values = [r[column] for r in rows[skip:]] or [r[column] for r in rows]
    return statistics.median(values) if values else float('nan')


def summary(group):
    r = group['rows']
    stereo = [row for row in r['fps'] if row[2] > 0]
    return {
        'render FPS (3D rows)': median(stereo or r['fps'], 0),
        'game Hz': median(r['fps'], 1),
        'pacing p50 ms': median(r['pacing'], 0), 'pacing p95 ms': median(r['pacing'], 1),
        'pacing p99 ms': median(r['pacing'], 2),
        'engine frame ms': median(r['engine'], 2), 'draws': median(r['engine'], 1),
        'GX list ms/frame': median(r['profile'], 0), 'submit ms/frame': median(r['profile'], 1),
        'audio ms/frame': median(r['profile'], 3),
        'worker copy ms': median(r['worker'], 0), 'worker consume ms': median(r['worker'], 1),
        'worker wait ms': median(r['worker'], 2), 'GPU tail ms': median(r['gpu'], 1),
        'engine thread busy ms': median(r['threads'], 0), 'translator busy ms': median(r['threads'], 1),
        'renderer busy ms': median(r['threads'], 2), 'GPU busy ms': median(r['threads'], 3),
        'GPU probe full ms': median(r['probe'], 0, 0), 'GPU probe no-fragments ms': median(r['probe'], 1, 0),
        'GPU probe one-triangle ms': median(r['probe'], 2, 0), 'GPU probe flat-texture ms': median(r['probe'], 3, 0),
        'GPU probe one-eye ms': median(r['probe'], 4, 0),
        'GPU probe keep-texture-cache ms': median(r['probe'], 5, 0),
        'GXBegin ms/frame': median(r['setup'], 0), 'immediate vertices ms/frame': median(r['setup'], 1),
        'colour-only materials/frame': median(r['setup'], 2),
        'CPU-shaded vertices/frame': median([[sum(row)] for row in r['cpushade']], 0),
    }


def label(key):
    stage, items, players = key
    return f"{STAGES.get(stage, f'stage {stage}')}, items={'off' if items == 255 else items}, players={players}"


def main():
    logs = sys.argv[1:]
    if not logs or len(logs) > 2: raise SystemExit(__doc__)
    tables = [{label(g['key']): summary(g) for g in encounters(p)} for p in logs]
    for name in dict.fromkeys(k for t in tables for k in t):
        print(f'\n== {name}')
        rows = [t.get(name) for t in tables]
        metrics = next(r for r in rows if r)
        for metric in metrics:
            cells = ''.join(f'{(r or {}).get(metric, float("nan")):>12.2f}' for r in rows)
            print(f'  {metric:28}{cells}')


if __name__ == '__main__':
    main()
