"""Statistical emulator CPU samples; these are not hardware timings."""
import argparse,bisect,collections,hashlib,json,random,socket,struct,subprocess,time
from gameplay_test import ROOT,TEST_ELF,packet,receive

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--samples',type=int,default=400);ap.add_argument('--label',default='cpu-samples');args=ap.parse_args()
    nm=subprocess.check_output([str(ROOT/'.toolchain/llvm-mingw-20260908-ucrt-x86_64/bin/llvm-nm.exe'),'-n',str(TEST_ELF)],text=True)
    funcs=[]
    for line in nm.splitlines():
        p=line.split()
        if len(p)==3 and p[1] in ('t','T') and not p[2].startswith(('$','mp_be_fix_')):funcs.append((int(p[0],16),p[2]))
    addresses=[x[0] for x in funcs]
    def label(a):
        i=bisect.bisect_right(addresses,a)-1
        return funcs[i][1] if i>=0 else hex(a)
    counts=collections.Counter();callers=collections.Counter()
    for _ in range(args.samples):
        with socket.create_connection(('127.0.0.1',24689),3) as s:
            s.settimeout(5);packet(s,'?');receive(s)
            try:
                packet(s,'g');r=struct.unpack('<16I',bytes.fromhex(receive(s))[:64])
                counts[label(r[15])]+=1;callers[(label(r[15]),label(r[14]))]+=1
            finally:packet(s,'c');packet(s,'D');receive(s)
        time.sleep(random.uniform(.005,.035))
    result={'samples':args.samples,'elf_sha256':hashlib.sha256(TEST_ELF.read_bytes()).hexdigest(),
            'scope':'Debugger-interrupted emulator PC samples; hotspot evidence, not physical timing.',
            'pc':counts.most_common(35),'pc_lr':[(a,b,n) for (a,b),n in callers.most_common(35)]}
    (ROOT/'build'/f'{args.label}.json').write_text(json.dumps(result,indent=2));print(json.dumps(result,indent=2))
if __name__=='__main__':main()
