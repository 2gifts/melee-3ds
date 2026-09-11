"""Compile the actual item-drop builder overlay with separated guarded globals."""
import subprocess
from build import ROOT,UPSTREAM,local_clang
from engine_overlays import adapt
source=adapt(UPSTREAM/'src/melee/it/itspawn.c').read_text()
def function(name):
    start=source.index('void '+name+'(');body=source.index('{',start);depth=1;end=body+1
    while depth:
        depth+=(source[end]=='{')-(source[end]=='}');end+=1
    return source[start:end]
generated=ROOT/'build/generated';generated.mkdir(parents=True,exist_ok=True)
(generated/'item_table_functions.inc').write_text('\n'.join(function(n) for n in ('it_8026CA4C','it_8026CD50')))
binary=ROOT/'build/item-table-tests.exe'
subprocess.run([local_clang(),'-O2','-I'+str(generated),str(ROOT/'tests/item_table_tests.c'),'-o',str(binary)],check=True)
subprocess.run([str(binary)],check=True)
