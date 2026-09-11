"""Inspect this workspace's Azahar test process using its GDB remote endpoint."""
import argparse,json,socket,subprocess,weakref
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def packet(sock,cmd):
    # GDB acknowledgements followed by tiny requests otherwise incur Windows'
    # delayed-ACK/Nagle wait at every read, freezing the guest for seconds.
    sock.setsockopt(socket.IPPROTO_TCP,socket.TCP_NODELAY,1)
    raw=cmd.encode();sock.sendall(b'$'+raw+b'#'+f'{sum(raw)&255:02x}'.encode())
_received=weakref.WeakKeyDictionary()
def receive(sock):
    data=_received.get(sock,b'')
    while True:
        start=data.find(b'$');end=data.find(b'#',start+1) if start>=0 else -1
        if end>=0 and len(data)>=end+3:
            result=data[start+1:end];checksum=int(data[end+1:end+3],16)
            if sum(result)&255!=checksum:raise ValueError('Invalid debugger packet checksum')
            _received[sock]=data[end+3:];sock.sendall(b'+');return result.decode()
        part=sock.recv(4096)
        if not part:raise ConnectionError('Emulator debugger disconnected')
        data+=part
def main():
    ap=argparse.ArgumentParser();ap.add_argument('--resume',action='store_true');ap.add_argument('--memory');ap.add_argument('--breakpoint');ap.add_argument('--hold',action='store_true');args=ap.parse_args()
    with socket.create_connection(('127.0.0.1',24689),3) as sock:
        sock.settimeout(4)
        packet(sock,'?');receive(sock)
        if args.resume:packet(sock,'c');packet(sock,'D');receive(sock);return
        packet(sock,'g');regs=bytes.fromhex(receive(sock));values=[int.from_bytes(regs[i:i+4],'little') for i in range(0,64,4)]
        symbols=[]
        text=subprocess.check_output([str(ROOT/'.toolchain/llvm-mingw-20260908-ucrt-x86_64/bin/llvm-nm.exe'),'-n',str(ROOT/'build/game/melee.elf')],text=True)
        for line in text.splitlines():
            parts=line.split()
            if len(parts)==3 and parts[1] in ('T','t') and not parts[2].startswith(('mp_be_fix_','$')):symbols.append((int(parts[0],16),parts[2]))
        def label(addr):
            nearest=next(((v,n) for v,n in reversed(symbols) if v<=addr),None)
            return f'{nearest[1]}+0x{addr-nearest[0]:x}' if nearest else '?'
        result={('sp' if i==13 else 'lr' if i==14 else 'pc' if i==15 else f'r{i}'):f'{v:08x}' for i,v in enumerate(values)}
        result['location']=label(values[15]);result['caller']=label(values[14])
        if args.memory:packet(sock,'m'+args.memory);result['memory']=receive(sock)
        if args.breakpoint:packet(sock,'Z0,'+args.breakpoint+',4');result['breakpoint']=receive(sock)
        print(json.dumps(result,indent=2));(ROOT/'build/game/gdb.json').write_text(json.dumps(result,indent=2))
        if not args.hold:packet(sock,'c');packet(sock,'D');receive(sock)
if __name__=='__main__':main()
