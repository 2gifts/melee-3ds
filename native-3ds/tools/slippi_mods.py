"""Slippi online experiment: source edits to the decomp (applied after the
other overlays, see engine_overlays.adapt).

Same edit format as gameplay_mods: {'path/suffix.c': [(old, new, count)]}.
Edits are collected from the modules in tools/slippi_edits/ (in name order;
base_replay.py first, other modules may anchor on its hooks) so separate
areas (determinism, replay playback, online play) stay in separate files.
"""
import importlib
import pkgutil
from pathlib import Path

import slippi_edits


def _fixes():
    merged = {}
    for info in sorted(pkgutil.iter_modules(slippi_edits.__path__), key=lambda m: m.name):
        module = importlib.import_module('slippi_edits.' + info.name)
        for key, edits in getattr(module, 'FIXES', {}).items():
            merged.setdefault(key, []).extend(edits)
    return merged


def edits_for(source):
    path = Path(source).as_posix()
    return [edit for key, edits in _fixes().items() if path.endswith('/' + key) for edit in edits]
