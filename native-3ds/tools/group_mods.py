"""Group play (Melee -> Group, local wireless lobbies): source edits to the decomp, applied after the Slippi edits
(see engine_overlays.adapt), so a group edit may anchor on text a Slippi edit put there.

Same edit format as gameplay_mods: {'path/suffix.c': [(old, new, count)]}. Edits are collected from the modules in
tools/group_edits/ in name order. Every patch is inert outside a group session (mp_group_mode() or the group.ini
switch), so the rest of the game is untouched.
"""
import importlib
import pkgutil
from pathlib import Path

import group_edits


def _fixes():
    merged = {}
    for info in sorted(pkgutil.iter_modules(group_edits.__path__), key=lambda m: m.name):
        module = importlib.import_module('group_edits.' + info.name)
        for key, edits in getattr(module, 'FIXES', {}).items():
            merged.setdefault(key, []).extend(edits)
    return merged


def edits_for(source):
    path = Path(source).as_posix()
    return [edit for key, edits in _fixes().items() if path.endswith('/' + key) for edit in edits]
