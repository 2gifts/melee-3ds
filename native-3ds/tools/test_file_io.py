"""Exercise production bounded SD streams against generated disc byte patterns."""
import subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
directory=ROOT/'build/file-io-fixture';directory.mkdir(parents=True,exist_ok=True)
(directory/'visuals').mkdir(exist_ok=True)
(directory/'GrIz.dat').write_bytes(b'original fountain')
(directory/'visuals/GrIz.dat').write_bytes((492294).to_bytes(4,'big')+b'V'*(492294-4))
for name,size in [('GrSt.dat',248938),('GrOp.dat',118854),('GrNBa.dat',67235),('GrNLa.dat',660692)]:
    (directory/name).write_bytes(b'original stage')
    (directory/'visuals'/name).write_bytes(size.to_bytes(4,'big')+b'W'*(size-4))
for file in range(20):
    (directory/f'asset-{file}.bin').write_bytes(bytes((i*17+(i>>8)*13+file*79)&255 for i in range(200003)))
for file,name in enumerate(('GmTtAll.usd','MnMaAll.usd','MnSlChr.usd','MnSlMap.usd','MnExtAll.usd',
                            'audio/us/nr_title.ssm','audio/us/nr_select.ssm','audio/us/nr_name.ssm','audio/us/nr_vs.ssm')):
    (directory/name).parent.mkdir(parents=True,exist_ok=True)
    (directory/name).write_bytes((directory/f'asset-{file}.bin').read_bytes())
cc=ROOT/'.toolchain/llvm-mingw-20260908-ucrt-x86_64/bin/clang.exe'
exe=ROOT/'build/file-io-tests.exe'
subprocess.run([str(cc),'-O2','-Wall','-Wextra',str(ROOT/'tests/file_io_tests.c'),'-o',str(exe)],check=True)
subprocess.run([str(exe)],cwd=ROOT,check=True)
