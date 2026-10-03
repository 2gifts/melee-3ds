"""Raw-pad replay check: did the game's own pad code turn the recorded raw
pads into exactly the processed inputs Slippi recorded?

    python tools/slippi/replay_run.py REPLAY.slp --raw
    python tools/slippi/raw_compare.py REPLAY.slp build/slippi-replays/<name>-raw/replay-out.bin
Also prints replay_summary's state comparison for the same run.
"""
import struct
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from slp import f32, parse  # noqa: E402

Q = struct.Struct('>cB2xi8xIIIIII')


def main():
    slp, out = sys.argv[1], sys.argv[2]
    r = parse(slp)
    data = Path(out).read_bytes()
    mine = {}
    for off in range(0, len(data) - 39, 40):
        if data[off:off + 1] == b'Q':
            t, port, frame, lx, ly, cx, cy, trig, btn = Q.unpack_from(data, off)
            mine[(frame, port)] = dict(lstick=(lx, ly), cstick=(cx, cy), trigger=trig, buttons=btn)
    bad, checked, shown = 0, 0, 0
    first = {}
    for (frame, port, follower), pre in sorted(r.pre.items()):
        if follower or (frame, port) not in mine:
            continue
        m = mine[(frame, port)]
        ref = dict(lstick=tuple(struct.unpack('>I', b)[0] for b in pre['lstick']),
                   cstick=tuple(struct.unpack('>I', b)[0] for b in pre['cstick']),
                   trigger=struct.unpack('>I', pre['trigger'])[0],
                   buttons=struct.unpack('>I', pre['buttons'])[0])
        checked += 1
        diff = [k for k in ref if ref[k] != m[k]]
        for k in diff:
            first.setdefault(k, frame)
        if diff:
            bad += 1
            if shown < 15:
                shown += 1
                print(f'frame {frame} port {port}: ' + '; '.join(
                    f'{k} port {m[k]} slp {ref[k]}' if k == 'buttons' else
                    f'{k} port {[round(f32(v), 6) for v in (m[k] if isinstance(m[k], tuple) else (m[k],))]} '
                    f'slp {[round(f32(v), 6) for v in (ref[k] if isinstance(ref[k], tuple) else (ref[k],))]}'
                    for k in diff) + f'  raw {pre["raw"]}')
    print(f'processed inputs: {checked} checked, {bad} differ; first by field: {first}')
    print(subprocess.run([sys.executable, str(HERE / 'replay_summary.py'), slp, out],
                         capture_output=True, text=True).stdout)


if __name__ == '__main__':
    main()
