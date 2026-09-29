"""Layout fixes: decompiled code that reaches one global through another.

The matching PowerPC build placed related globals next to each other, and
some decompiled code relies on that: it casts one object to a larger layout,
or indexes past its end, to reach the next. ARM data sections are placed
independently, so those reads and writes land elsewhere. Worse, an object
written only through such a cast looks read-only to LLVM, which folds it to
zero. Each module here names the object the matching offset denotes.

A fix_*.py module defines FIXES = {'path/suffix.c': [(old, new, count), ...]}. The
edits apply after engine_overlays.adapt, each asserting its match count.
"""
import importlib
import pkgutil
from pathlib import Path

FIXES = {}
for _module in pkgutil.iter_modules([str(Path(__file__).parent)]):
    if not _module.name.startswith('fix_'):
        continue
    for _key, _edits in importlib.import_module(__name__ + '.' + _module.name).FIXES.items():
        FIXES.setdefault(_key, []).extend(_edits)


def edits_for(source):
    path = Path(source).as_posix()
    return [edit for key, edits in FIXES.items() if path.endswith('/' + key) for edit in edits]


def apply_text(source, text):
    for old, new, count in edits_for(source):
        found = text.count(old)
        assert found == count, (Path(source).name, old[:80], found, count)
        text = text.replace(old, new)
    return text
