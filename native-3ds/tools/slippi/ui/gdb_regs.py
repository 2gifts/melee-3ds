"""Print the stop reason and the registers of a stopped (crashed) emulator,
symbolized with the development ELF. Usage: gdb_regs.py [port]"""
import os
import socket
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
ELF = Path(os.environ.get('MP_TEST_ELF', ROOT / 'build/game-opt/melee.elf'))
ADDR2LINE = ROOT / '.toolchain/llvm-mingw-20260908-ucrt-x86_64/bin/llvm-addr2line.exe'


def packet(sock, body):
    data = body.encode()
    sock.sendall(b'$' + data + b'#' + f'{sum(data) & 255:02x}'.encode())


def receive(sock):
    buf = b''
    while True:
        part = sock.recv(65536)
        if not part:
            raise ConnectionError('closed')
        buf += part
        start = buf.find(b'$')
        end = buf.find(b'#', start)
        if start >= 0 and end >= 0 and len(buf) >= end + 3:
            sock.sendall(b'+')
            return buf[start + 1:end].decode(errors='replace')


def symbolize(addr):
    out = subprocess.run([str(ADDR2LINE), '-f', '-C', '-e', str(ELF), hex(addr)], capture_output=True, text=True).stdout
    return ' '.join(out.split())


def main():
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 24689
    with socket.create_connection(('127.0.0.1', port), 5) as sock:
        sock.settimeout(10)
        packet(sock, '?')
        print('stop:', receive(sock))
        packet(sock, 'g')
        regs = receive(sock)
        words = [int.from_bytes(bytes.fromhex(regs[i:i + 8]), 'little') for i in range(0, min(len(regs), 16 * 8), 8)]
        for i, w in enumerate(words):
            name = {13: 'sp', 14: 'lr', 15: 'pc'}.get(i, f'r{i}')
            line = f'{name:>3} {w:08x}'
            if i in (14, 15):
                line += '  ' + symbolize(w)
            print(line)
        sp = words[13]
        packet(sock, f'm{sp:x},100')
        stack = bytes.fromhex(receive(sock))
        print('stack return candidates:')
        for i in range(0, len(stack), 4):
            w = int.from_bytes(stack[i:i + 4], 'little')
            if 0x100000 <= w < 0x1000000:
                print(f'  sp+{i:#x} {w:08x} {symbolize(w)}')


if __name__ == '__main__':
    main()
