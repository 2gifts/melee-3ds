"""Retain original FD animation/control data alongside simplified visuals.

The original data block is appended intact except relocated pointers. Keeping
the complete block makes closure safe without guessing variable object sizes.
Only animation lists are attached; its high-detail meshes remain unreachable.
"""
import struct

def restore_fd(original,diet):
    # Appending this closure is valid only for self-contained archives.
    assert int.from_bytes(original.raw[16:20],'big')==0
    assert int.from_bytes(diet.raw[16:20],'big')==0
    assert original.word(original.roots['map_head']+12)==diet.word(diet.roots['map_head']+12)==10
    mapping={};orows=original.word(original.roots['map_head']+8);drows=diet.word(diet.roots['map_head']+8)
    def map_joints(a,b):
        assert bool(a)==bool(b),(a,b)
        if not a:return
        if a in mapping:assert mapping[a]==b;return
        mapping[a]=b
        for offset in (8,12):map_joints(original.word(a+offset),diet.word(b+offset))
    for i in [0,*range(2,10)]:map_joints(original.word(orows+i*52),diet.word(drows+i*52))
    data=bytearray(diet.b);data.extend(b'\0'*((-len(data))%32));base=len(data)
    data.extend(original.b)
    def relocs(archive):
        start=32+len(archive.b);count=int.from_bytes(archive.raw[8:12],'big')
        return [struct.unpack_from('>I',archive.raw,start+i*4)[0] for i in range(count)]
    sites=relocs(diet)
    for site in relocs(original):
        target=original.word(site)
        struct.pack_into('>I',data,base+site,mapping.get(target,base+target))
        sites.append(base+site)
    for i in [0,*range(2,10)]:
        site=drows+i*52+4;target=original.word(orows+i*52+4)
        struct.pack_into('>I',data,site,base+target if target else 0)
        if site in sites:sites.remove(site)
        if target:sites.append(site)
    # Diet zeroed this controller joint's depth scale. Retain its original
    # invertible transform along with the original 600-frame animation.
    a=original.word(orows+3*52);b=diet.word(drows+3*52)
    data[b+20:b+56]=original.b[a+20:a+56]
    count=int.from_bytes(diet.raw[8:12],'big')
    tail=diet.raw[32+len(diet.b)+count*4:]
    header=bytearray(diet.raw[:32]);size=32+len(data)+4*len(sites)+len(tail)
    struct.pack_into('>III',header,0,size,len(data),len(sites))
    return header+data+b''.join(struct.pack('>I',s) for s in sorted(sites))+tail

def audit_fd_wait_controllers(original,prepared):
    """Check the exact joints queried by grLast's animation wait states.

    HSD_ForeachAnim visits a joint's own AObj before material/shape AObjs.
    Every wait query below has that original joint AObj, so removing purely
    visual material animation cannot change the callback's timing source.
    """
    checked=0
    for model,index in [(4,1),(5,1),(6,1),(7,1),(8,1),(7,2)]:
        streams=[]
        for archive in (original,prepared):
            rows=archive.word(archive.roots['map_head']+8)
            assert not archive.word(rows+model*52+12),'Unexpected shape animation'
            p=archive.word(rows+model*52+4);values=[]
            while archive.word(p):
                nodes=[]
                def walk(n):
                    if not n:return
                    nodes.append(n);walk(archive.word(n));walk(archive.word(n+4))
                walk(archive.word(p));aobj=archive.word(nodes[index]+8)
                assert aobj,(model,index,len(values))
                values.append(archive.aobj(aobj));p+=4
            streams.append(values)
        assert streams[0]==streams[1],(model,index)
        checked+=len(streams[0])
    return checked
