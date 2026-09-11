"""Sample the current workspace emulator's PC without changing game state."""
import bisect,collections,json,socket,struct,subprocess,time
from gdb_probe import ROOT,packet,receive
listing=subprocess.check_output([str(ROOT/'.toolchain/llvm-mingw-20260908-ucrt-x86_64/bin/llvm-nm.exe'),'-n',str(ROOT/'build/game/melee.elf')],text=True)
symbols=[]
for line in listing.splitlines():
    p=line.split()
    if len(p)==3 and p[1] in ('T','t') and not p[2].startswith(('mp_be_fix_','$')):symbols.append((int(p[0],16),p[2]))
addresses=[x[0] for x in symbols]
counts=collections.Counter();callers=collections.Counter()
for i in range(160):
    with socket.create_connection(('127.0.0.1',24689),3) as sock:
        sock.settimeout(5);packet(sock,'?');receive(sock)
        try:
            packet(sock,'g');registers=bytes.fromhex(receive(sock));lr,pc=struct.unpack_from('<II',registers,56)
            counts[symbols[bisect.bisect_right(addresses,pc)-1][1]]+=1
            callers[symbols[bisect.bisect_right(addresses,lr)-1][1]]+=1
        finally:packet(sock,'c');packet(sock,'D');receive(sock)
    time.sleep(.013)
result={'pc':counts.most_common(),'lr':callers.most_common()}
print(json.dumps(result,indent=2));(ROOT/'build/cpu-samples.json').write_text(json.dumps(result,indent=2))
