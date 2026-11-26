"""PC hosts for the interactive Azahar demo (docker/emu-web-hosts.sh runs this inside the web container): fills the
consoles' Available Games list so the Mii heads, the emblem, an "IN MATCH" row and the pages can be tried by hand.

  - a pair of PC consoles ("Match" and a joiner) playing one long match: an IN MATCH row (started first, so the joiner
    finds that game and no other);
  - lobby hosts with distinct Miis (tools/group/test_miis.py), one without a Mii (test_mii_fail=1, the emblem) and one
    whose Mii is corrupted after its CRC (test_mii_corrupt=1: browsers must show the emblem);
  - each host takes CPU doors so its row shows badges. A host that ends (they give up after 20 minutes) is restarted.

    python3 tools/group/web_hosts.py [--hosts 5] [--port 44100]
Logs: /tmp/web-hosts/<name>/game.log."""
import argparse
import os
import subprocess
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import test_miis  # noqa: E402

TOOLS = Path(os.environ.get('MELEE_GROUP_TOOLS_DIR') or '/tmp/group-tools')
OUT = Path('/tmp/web-hosts')


def start(name, lines, control=None):
    d = OUT / name
    d.mkdir(parents=True, exist_ok=True)
    (d / 'group.ini').write_text('\n'.join(lines) + '\n')
    if control is not None:
        (d / 'control.txt').write_text(''.join(c + '\n' for c in control))
    log = open(d / 'stdout.txt', 'ab')
    return subprocess.Popen([str(TOOLS / 'group_client'), '--dir', str(d)], stdout=log, stderr=subprocess.STDOUT)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--hosts', type=int, default=5)
    ap.add_argument('--port', type=int, default=44100)
    args = ap.parse_args()
    env = dict(os.environ, MELEE_GROUP_TOOLS_DIR=str(TOOLS))
    subprocess.run([sys.executable, str(HERE / 'build_tools.py')], check=True, env=env)
    base = ['udp=1', f'port={args.port}', 'delay=2']
    miis = test_miis.hosts()
    procs = {}

    def match_pair():
        procs['Match'] = start('Match', base + ['role=host', 'name=Match', 'test_players=2', 'test_games=1',
                                                 'test_frames=40000', 'test_character=8', f'test_mii={miis[-1][1]}'])
        time.sleep(2)
        procs['Joiner'] = start('Joiner', base + ['role=join', 'name=Joiner', 'test_games=1', 'test_frames=40000',
                                                   'test_character=3'])
        time.sleep(8)                               # in the match before any other host is listed

    def lobby_host(i):
        name, hexmii = miis[i % len(miis)]
        extra = [f'test_mii={hexmii}']
        if i == 3:
            extra = ['test_mii_fail=1']             # no Mii: the emblem
        if i == 4:
            extra.append('test_mii_corrupt=1')      # a corrupt Mii: the emblem
        control = [f'door {p} 1 {(i * 5 + p * 7) % 26} 3 1 0' for p in range(1, 1 + i % 3)]
        procs[name] = start(name, base + ['role=host', f'name={name}', 'test_players=9', 'test_control=1',
                                          f'test_character={i * 4}'] + extra, control)

    match_pair()
    for i in range(args.hosts):
        lobby_host(i)
    print(f'web_hosts: {len(procs)} PC consoles up; logs in {OUT}', flush=True)
    while True:
        time.sleep(5)
        if procs['Match'].poll() is not None and procs['Joiner'].poll() is not None:
            match_pair()
        for i in range(args.hosts):
            name = miis[i % len(miis)][0]
            if procs[name].poll() is not None:
                lobby_host(i)


if __name__ == '__main__':
    main()
