"""Per-field first divergence of a replay comparison.

    python tools/slippi/replay_summary.py REPLAY.slp replay-out.bin
"""
import collections
import re
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
out = subprocess.run([sys.executable, str(HERE / 'replay_compare.py'), sys.argv[1], sys.argv[2], '--all'],
                     capture_output=True, text=True).stdout
first, count, examples = {}, collections.Counter(), {}
for line in out.splitlines():
    if line.startswith(('replay ', 'checked ')):
        print(line)
    m = re.match(r'(pre|post) frame (-?\d+) port (\d)', line)
    if not m:
        continue
    for field in re.findall(r'(\w+): port', line):
        key = (m.group(1), field)
        count[key] += 1
        if key not in first:
            first[key] = int(m.group(2))
            examples[key] = line
for key in sorted(first, key=first.get):
    print(f'{key[0]:4} {key[1]:8} first frame {first[key]:6}  mismatched records {count[key]}')
