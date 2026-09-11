"""Regression-test the actual effect-table overlay independently of linking."""
import subprocess
from build import ROOT,UPSTREAM,local_clang
from engine_overlays import adapt
source=adapt(UPSTREAM/'src/melee/ef/eflib.c').read_text()
start=source.index('void efLib_SetParamAlpha(');end=source.index('void efLib_Cb_ApplyStoredAlpha(',start)
generated=ROOT/'build/generated';generated.mkdir(parents=True,exist_ok=True)
(generated/'effect_params_functions.inc').write_text(source[start:end])
binary=ROOT/'build/effect-params-tests.exe'
subprocess.run([local_clang(),'-O2','-I'+str(generated),str(ROOT/'tests/effect_params_tests.c'),'-o',str(binary)],check=True)
subprocess.run([str(binary)],check=True)
