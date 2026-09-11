"""Compile the adapted original item helpers and exercise absent inputs."""
import subprocess
from build import ROOT,UPSTREAM,local_clang
from engine_overlays import adapt
generated=ROOT/'build/generated'
for filename,signature,output in (
    ('itfreeze.c','Item_GObj* it_8028EB88(','freeze_spawn_function.inc'),
    ('itlinkarrow.c','static inline HSD_JObj* itLinkArrow_802A850C_inline(','arrow_joint_function.inc')):
    source=adapt(UPSTREAM/'src/melee/it/kinds'/filename).read_text()
    start=source.index(signature);end=source.index('\n}\n',start)+2
    (generated/output).write_text(source[start:end])
binary=ROOT/'build/item-optional-input-tests.exe'
subprocess.run([local_clang(),'-O2','-I'+str(generated),str(ROOT/'tests/item_optional_inputs_tests.c'),'-o',str(binary)],check=True)
subprocess.run([str(binary)],check=True)
