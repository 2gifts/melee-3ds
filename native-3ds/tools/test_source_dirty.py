"""Check selective geometry invalidation against an independent history."""
import subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
cc=ROOT/'.toolchain/llvm-mingw-20260908-ucrt-x86_64/bin/clang.exe'
exe=ROOT/'build/source-dirty-tests.exe'
subprocess.run([str(cc),'-O2','-Wall','-Wextra',str(ROOT/'tests/source_dirty_tests.c'),'-o',str(exe)],check=True)
subprocess.run([str(exe)],check=True)
