"""Read the explicitly enabled native renderer's offline authoring export."""
import struct,json
from pathlib import Path
import numpy as np
from PIL import Image,ImageDraw
from assets import ROOT

FIELDS='image w h format palette palfmt palcount depth write func blend src dst cull alpha color_mask texture_rgb texture_alpha wrap_s wrap_t gpu indices index_count geometry_id sx sy sw sh points point_size point_offset screen_width layer convergence'.split()

def read_capture(path):
    raw=path.read_bytes();assert raw[:8]==b'MPBN0001';p=8;draws=[];textures={}
    while p<len(raw):
        tag,n,ni,nu=struct.unpack_from('<4I',raw,p);p+=16
        assert tag==0x57415244 and nu==1544 and n<65536
        d=dict(zip(FIELDS,struct.unpack_from('<34I',raw,p)));p+=136
        u=np.frombuffer(raw,dtype='>f4',count=380,offset=p).astype('f4').reshape(95,4)
        mat=np.frombuffer(raw,dtype='>f4',count=4,offset=p+1524).astype('f4')
        flat=struct.unpack_from('>I',raw,p+1540)[0];p+=nu
        v=np.frombuffer(raw,dtype='>f4',count=n*14,offset=p).astype('f4').reshape(n,14);p+=n*56
        ix=np.frombuffer(raw,dtype='>u2',count=ni,offset=p).astype('u2');p+=ni*2
        tex=struct.unpack_from('<I',raw,p)[0];p+=4;key=tex&0x7fffffff
        if tex&0x80000000:
            size=d['w']*d['h']*4
            textures[key]=Image.frombytes('RGBA',(d['w'],d['h']),raw[p:p+size]);p+=size
        assert not key or key in textures
        pos=[]
        for vert in v:
            row=int(vert[13])
            pos.append(u[row:row+3]@vert[:4] if 0<=row<=27 else [float('nan')]*3)
        draws.append(dict(d=d,u=u,v=v,indices=ix,material=mat,flat=flat,texture=key,positions=np.array(pos)))
    assert p==len(raw)
    return draws,textures

def inspect(path):
    draws,textures=read_capture(path);out=ROOT/'build/home-menu/inspect';out.mkdir(exist_ok=True)
    results=[]
    for i,row in enumerate(draws):
        d=row['d'];pos=row['positions'];valid=np.isfinite(pos).all()
        results.append(dict(draw=i,geometry=d['geometry_id'],verts=len(pos),triangles=len(row['indices'])//3,texture=row['texture'],size=[d['w'],d['h']],format=d['format'],palette=d['palette'],material=row['material'].tolist(),projection=row['u'][60:64].tolist(),bounds=[pos.min(0).tolist(),pos.max(0).tolist()] if valid else None))
    (out/'draws.json').write_text(json.dumps(results,indent=2)+'\n')
    sheet=Image.new('RGB',(640,132*((len(textures)+4)//5)),(35,38,44));pen=ImageDraw.Draw(sheet)
    for i,(key,im) in enumerate(textures.items()):
        im.save(out/f'tex-{key}.png');copy=im.copy();copy.thumbnail((120,108));x=i%5*128;y=i//5*132
        sheet.paste(copy,(x,y),copy);pen.text((x,y+110),f'{key} {im.width}x{im.height}',fill='white')
    sheet.save(out/'textures.png')
    print('draws',len(draws),'vertices',sum(len(d['v']) for d in draws),'textures',len(textures))
    for row in results:
        if row['bounds']:print(row['draw'],row['geometry'],row['verts'],row['texture'],np.round(row['bounds'],1).tolist())

if __name__=='__main__':
    import sys
    inspect(Path(sys.argv[1]) if len(sys.argv)>1 else ROOT/'build/home-menu/capture/banner-0000.bin')
