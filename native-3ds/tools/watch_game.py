"""Locate an original-engine invalid memory access in the local Azahar run."""
import argparse,json,socket,subprocess
from gdb_probe import ROOT,packet,receive

ap=argparse.ArgumentParser();ap.add_argument('address',type=lambda s:int(s,0));ap.add_argument('--timeout',type=int,default=40);args=ap.parse_args()
if args.address<0x100000:ap.error('Azahar cannot install watchpoints on unmapped low memory')
listing=subprocess.check_output([str(ROOT/'.toolchain/llvm-mingw-20260908-ucrt-x86_64/bin/llvm-nm.exe'),'-n',str(ROOT/'build/game/melee.elf')],text=True)
symbols={line.split()[2]:int(line.split()[0],16) for line in listing.splitlines() if len(line.split())==3}
functions=sorted((address,name) for name,address in symbols.items() if not name.startswith(('mp_be_fix_','$')))
def label(address):
    p=next(((a,n) for a,n in reversed(functions) if a<=address),None)
    return f'{p[1]}+{address-p[0]:#x}' if p else '?'
with socket.create_connection(('127.0.0.1',24689),3) as sock:
    sock.settimeout(args.timeout);packet(sock,'?');receive(sock)
    try:
        packet(sock,f'Z3,{args.address:x},4');print('Watchpoint:',receive(sock),flush=True)
        packet(sock,'c');print('Stop:',receive(sock),flush=True)
        packet(sock,'g');data=bytes.fromhex(receive(sock));regs=[int.from_bytes(data[i:i+4],'little') for i in range(0,64,4)]
        report={'registers':[hex(v) for v in regs],'pc':label(regs[15]),'lr':label(regs[14])}
        packet(sock,f'm{symbols["HSD_GObj_CurrentInvokedProc"]:x},4');proc=int(receive(sock),16)
        report['proc']=hex(proc)
        if proc:
            packet(sock,f'm{proc:x},18');raw=receive(sock);report['proc_data']=raw;report['callback']=label(int(raw[40:48],16))
        print(json.dumps(report,indent=2));(ROOT/'build/game/watch.json').write_text(json.dumps(report,indent=2))
    finally:
        packet(sock,f'z3,{args.address:x},4');receive(sock)
        packet(sock,'c');packet(sock,'D');receive(sock)
