"""Exercise CPU setup policy against mocked SDK successes and failures."""
import subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
cc=ROOT/'.toolchain/llvm-mingw-20260908-ucrt-x86_64/bin/clang.exe'
exe=ROOT/'build/cpu-speed-tests.exe'
subprocess.run([str(cc),'-O2','-Wall','-Wextra','-I'+str(ROOT/'tests/cpu_sdk'),str(ROOT/'tests/cpu_speed_tests.c'),'-o',str(exe)],check=True)
subprocess.run([str(exe)],check=True)
