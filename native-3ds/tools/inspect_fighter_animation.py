"""Read original fighter animation/skeleton state after an engine assertion."""
import json,socket,struct
from gameplay_test import ROOT,symbols,packet,receive
def inspect():
    with socket.create_connection(('127.0.0.1',24689),3) as sock:
        sock.settimeout(5);packet(sock,'?');receive(sock)
        def read(a,n):
            data=b''
            while n:
                size=min(512,n);packet(sock,f'm{a:x},{size:x}');part=bytes.fromhex(receive(sock))
                if len(part)!=size:raise RuntimeError(f'Invalid address {a:x}')
                data+=part;a+=size;n-=size
            return data
        def word(a):return int.from_bytes(read(a,4),'big')
        try:
            result=[];gobj=word(word(symbols['HSD_GObjPLinkHead'])+32)
            for _ in range(8):
                if not gobj:break
                fp=word(gobj+44);tree=word(fp+0x590);parts=word(fp+0x5e8)
                state={'gobj':gobj,'fighter':fp,'kind':word(fp+4),'motion':word(fp+16),
                    'animation_words':list(struct.unpack('>13I',read(fp+0x58c,52))),
                    'parts':parts,'bones_enabled':[]}
                state['gobj_header']=read(gobj,48).hex()
                motions=word(fp+0x24);state['motion_table']=motions
                state['motions']=[list(struct.unpack('>6I',read(motions+i*24,24))) for i in range(14)]
                if tree and tree<0x10000000:
                    t=read(tree,20);state['tree']=list(struct.unpack('>5I',t));nodes=read(word(tree+12),256)
                    state['nodes']=list(nodes[:nodes.index(255)+1]) if 255 in nodes else list(nodes)
                if parts and parts<0x10000000:
                    bones=read(parts,140*16);state['bone_flags']=list(bones[8::16]);state['bones_enabled']=[i for i,b in enumerate(bones[8::16]) if b&64]
                result.append(state);gobj=word(gobj+8)
            stack=read(symbols['_stack_addr']-4096,4096);(ROOT/'build/animation-panic-stack.bin').write_bytes(stack)
        finally:packet(sock,'c');packet(sock,'D');receive(sock)
    (ROOT/'build/fighter-animation-inspection.json').write_text(json.dumps(result,indent=2));print(json.dumps(result,indent=2))
if __name__=='__main__':inspect()
