"""Observe the original turnip's GX state; never change game or asset state."""
import json,socket,struct
from gameplay_test import ROOT,symbols,packet,receive


def main():
    with socket.create_connection(('127.0.0.1',24689),3) as sock:
        sock.settimeout(15);packet(sock,'?');receive(sock)
        def rpc(s):packet(sock,s);return receive(sock)
        def read(a,n):return bytes.fromhex(rpc(f'm{a:x},{n:x}'))
        def word(a):return int.from_bytes(read(a,4),'big')
        head=word(symbols['HSD_GObjPLinkHead']);gobj=word(head+32)
        while gobj:
            fighter=word(gobj+0x2c)
            if word(fighter+4)==9:break
            gobj=word(gobj+8)
        assert gobj,'Peach must be selected'
        item=word(fighter+0x1974);assert item,'Peach must hold a turnip'
        data=word(fighter+0x10c)
        article=word(word(data+0x48)+4)
        root=word(word(article+0x10))
        # The ftData structure is copied on load. The original joint descriptor
        # still points into the relocated archive inspected by the asset audit.
        target=root-96584+94080
        result={'fighter':fighter,'item':item,'ft_data':data,'target_list':target}
        joints=[];targets=[]
        def joint(j):
            if not j:return
            row={'joint':j,'flags':word(j+20),'objects':[]};dobj=word(j+24)
            while dobj:
                mat=word(dobj+8);pobj=word(dobj+12)
                obj={'dobj':dobj,'flags':word(dobj+20),'mobj':mat,'material':read(mat,32).hex(),'pobj':pobj,'display':word(pobj+16)}
                row['objects'].append(obj)
                if word(mat+4)==0x34:targets.append(obj['display'])
                dobj=word(dobj+4)
            joints.append(row);joint(word(j+16));joint(word(j+8))
        joint(word(item+0x28));result['joints']=joints
        (ROOT/'build/turnip-live-joints.json').write_text(json.dumps(result,indent=2))
        print('Body targets',targets,flush=True)
        if targets:target=targets[0]
        bp=symbols['GXCallDisplayList'];assert rpc(f'Z0,{bp:x},4')=='OK'
        try:
            for i in range(5000):
                rpc('c');r=struct.unpack('<16I',read_registers(sock))
                if i<3:print('Target/regs',hex(target),[hex(x) for x in r],flush=True)
                if r[0]==target:
                    result['stops']=i
                    for name,count in [('tev_configuration',16*30),('texgen_configuration',8*5),('channel_configuration',36)]:
                        result[name]=list(struct.unpack('>'+str(count)+'I',read(symbols[name],count*4)))
                    for name in ['num_stages','texgens','current_mtx']:
                        result[name]=word(symbols[name])
                    for name,n in [('tev_color',16),('konst_color',16),('material',8),('textures',8*32),('texture_mtx',64*48)]:
                        result[name]=read(symbols[name],n).hex()
                    (ROOT/'build/turnip-live-material.json').write_text(json.dumps(result,indent=2))
                    print(json.dumps({k:v for k,v in result.items() if k not in ('texture_mtx','textures','tev_configuration','channel_configuration','texgen_configuration')},indent=2),flush=True)
                    return
                # Azahar's step reply can precede actual interpreter progress.
                # Advance to the next instruction with a separate breakpoint.
                assert rpc(f'z0,{bp:x},4')=='OK';assert rpc(f'Z0,{bp+4:x},4')=='OK';rpc('c')
                assert rpc(f'z0,{bp+4:x},4')=='OK';assert rpc(f'Z0,{bp:x},4')=='OK'
                if i%500==0:print('Lists inspected',i,flush=True)
            raise RuntimeError('Turnip face display list not found')
        finally:
            rpc(f'z0,{bp:x},4');rpc(f'z0,{bp+4:x},4');packet(sock,'c');packet(sock,'D');receive(sock)


def read_registers(sock):
    packet(sock,'g');return bytes.fromhex(receive(sock))[:64]


if __name__=='__main__':main()
