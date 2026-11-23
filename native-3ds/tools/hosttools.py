"""Locations of the CIA authoring tools for the current host.

Windows uses the downloaded binaries from tools/bootstrap_home_menu.py. Other
hosts (the Docker builder) compile them from pinned source into .toolchain/bin.
"""
import os
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
if os.name == 'nt':
    BANNERTOOL = ROOT/'.toolchain/home-menu/bannertool/windows-x86_64/bannertool.exe'
    MAKEROM = ROOT/'.toolchain/home-menu/makerom/makerom.exe'
else:
    BANNERTOOL = ROOT/'.toolchain/bin/bannertool'
    MAKEROM = ROOT/'.toolchain/bin/makerom'
