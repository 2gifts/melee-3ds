"""Apply every layout fix to its source through engine_overlays.adapt.
Usage (from native-3ds): python tools/layout_fixes/check.py [path-suffix...]"""
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from build import UPSTREAM
from engine_overlays import adapt
from layout_fixes import FIXES

keys = sys.argv[1:] or sorted(FIXES)
failed = 0
for key in keys:
    matches = [p for p in (UPSTREAM / 'src').rglob(Path(key).name) if p.as_posix().endswith('/' + key)]
    if len(matches) != 1:
        print('NOT FOUND or ambiguous:', key, matches); failed += 1; continue
    try:
        out = adapt(matches[0])
        print('ok', key, '->', out)
    except AssertionError as error:
        print('FAILED', key, error); failed += 1
sys.exit(1 if failed else 0)
