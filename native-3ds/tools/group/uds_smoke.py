"""Smoke test of group play over UDS in one Azahar instance: boots the development build with opponent=@host or @join
(an automatic group session on the real UDS link) and prints the uds / lw / group log lines. One instance cannot
connect (Azahar needs a multiplayer room to link two); this checks that the UDS calls run.

    docker/emu-test.sh run tools/group/uds_smoke.py @host
"""
import sys, time, shutil, os
from pathlib import Path
sys.path.insert(0, 'tools/slippi')
import emu
role = sys.argv[1]
out = Path(os.environ['MELEE_TEST_OUT']) / 'local1'; out.mkdir(parents=True, exist_ok=True)
home = emu.prepare('local-' + role.strip('@'), 24860)
sd = emu.sd(home)
(sd/'slippi').mkdir(parents=True, exist_ok=True)
(sd/'slippi'/'config.ini').write_text(f'opponent={role}\ndelay=2\ncharacter=2\nstage=32\nauto_match=1\nstage_select=1\ntest_inputs=1\ntest_end_frame=300\n')
(sd/'slippi'/'group.ini').write_text('udp=0\n')
(sd / 'no-scripted-input').write_text('')   # no smoke-build scripted presses
(sd/'game.log').unlink(missing_ok=True)
proc = emu.launch(home, Path(os.environ['MELEE_DSX']))
t=time.monotonic()
while time.monotonic()-t < 60 and proc.poll() is None:
    time.sleep(2)
emu.stop(proc)
shutil.copy2(sd/'game.log', out/f'{role.strip("@")}.log')
print('\n'.join(l for l in (sd/'game.log').read_text(errors='replace').splitlines() if 'uds' in l or 'lw:' in l or 'group' in l or 'Slippi online' in l)[:3000])
