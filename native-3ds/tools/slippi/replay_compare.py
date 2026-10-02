"""Compare the port's replay-out.bin with the original .slp, frame by frame.

Fields compared bit-exactly: pre-frame RNG seed, action, X, Y; post-frame
action, X, Y, facing, percent, shield, stocks, animation frame.

    python tools/slippi/replay_compare.py REPLAY.slp replay-out.bin [--all]
Exit 0 when every recorded frame matches.
"""
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from slp import f32, parse  # noqa: E402

RECORD = struct.Struct('>cBBBiIH2xIIIIII')


def read_out(path):
    pre, post, end = {}, {}, None
    data = Path(path).read_bytes()
    for off in range(0, len(data) - RECORD.size + 1, RECORD.size):
        t, port, follower, stocks, frame, seed, action, x, y, facing, percent, shield, anim = \
            RECORD.unpack_from(data, off)
        key = (frame, port, follower)
        if t == b'P':
            pre[key] = dict(seed=seed, action=action, x=x, y=y)
        elif t == b'O':
            post[key] = dict(action=action, x=x, y=y, facing=facing, percent=percent,
                             shield=shield, stocks=stocks, anim=anim)
        elif t == b'E':
            end = frame
    return pre, post, end


def show(name, a, b):
    if name in ('x', 'y', 'facing', 'percent', 'shield', 'anim'):
        return f'{name}: port {f32(a):.9g} ({a:08X}) vs slp {f32(b):.9g} ({b:08X})'
    if name == 'seed':
        return f'{name}: port {a:08X} vs slp {b:08X}'
    return f'{name}: port {a} vs slp {b}'


def main():
    slp_path, out_path = sys.argv[1], sys.argv[2]
    show_all = '--all' in sys.argv
    r = parse(slp_path)
    pre, post, end = read_out(out_path)
    first_bad, mismatches, checked, missing = None, 0, 0, 0
    lines = []
    for kind, mine, ref, fields in (('pre', pre, r.pre, ('seed', 'action', 'x', 'y')),
                                    ('post', post, r.post, ('action', 'x', 'y', 'facing', 'percent',
                                                            'shield', 'stocks', 'anim'))):
        for key in sorted(ref):
            if key not in mine:
                missing += 1
                continue
            checked += 1
            bad = [f for f in fields if mine[key][f] != ref[key][f]]
            if bad:
                mismatches += 1
                if first_bad is None or key[0] < first_bad[0]:
                    first_bad = key
                if show_all or len(lines) < 12:
                    lines.append(f'{kind} frame {key[0]} port {key[1]}{" nana" if key[2] else ""}: ' +
                                 '; '.join(show(f, mine[key][f], ref[key][f]) for f in bad))
    frames = r.last - r.first + 1
    print(f'replay {Path(slp_path).name}: {frames} frames ({r.first}..{r.last}); port output ends at {end}')
    print(f'checked {checked} records, missing {missing}, mismatched {mismatches}')
    if first_bad:
        print(f'first mismatch: frame {first_bad[0]} (replay frame index), port {first_bad[1]}')
        lines.sort(key=lambda l: int(l.split()[2]))
        print('\n'.join(lines))
    sys.exit(0 if mismatches == 0 and missing == 0 else 1)


if __name__ == '__main__':
    main()
