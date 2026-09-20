"""Inspect original CSS portrait images and their bound palettes locally."""
import json,struct
from PIL import Image,ImageDraw
import numpy as np
from audit_diet_fountain import Archive
from assets import ROOT

def decode(a,image,w,h,fmt,palette):
    paldata=a.word(palette);palfmt=a.word(palette+4);count=struct.unpack_from('>H',a.b,palette+12)[0]
    assert palfmt==2 and count in (16,256)
    v=np.frombuffer(a.b,dtype='>u2',count=count,offset=paldata).astype(np.uint32)
    r=np.where(v&32768,((v>>10)&31)*255//31,((v>>8)&15)*17)
    g=np.where(v&32768,((v>>5)&31)*255//31,((v>>4)&15)*17)
    b=np.where(v&32768,(v&31)*255//31,(v&15)*17)
    alpha=np.where(v&32768,255,((v>>12)&7)*255//7)
    colors=np.array([r,g,b,alpha],dtype=np.uint8).T
    yy,xx=np.indices((h,w));bw,bh=8,4 if fmt==9 else 8
    index=((yy//bh)*((w+7)//8)+xx//8)*bw*bh+(yy%bh)*bw+xx%8
    raw=np.frombuffer(a.b,dtype=np.uint8,offset=image)
    ix=raw[index] if fmt==9 else (raw[index//2]>>np.where(index%2,0,4))&15
    return Image.fromarray(colors[ix])

def collect():
    a=Archive(ROOT/'assets/GALE01/files/MnSlChr.usd');offs=struct.unpack_from('>'+str(a.inv['relocations'])+'I',a.raw,32+len(a.b));valid=set(offs);seen=set();images=[]
    for p in offs:
        o=a.word(p)
        if o not in valid or o+24>len(a.b):continue
        image,w,h,fmt=struct.unpack_from('>IHHI',a.b,o)
        if (w,h,fmt)!=(136,188,9) or image in seen:continue
        # This archive stores each portrait, its 256-entry palette, then
        # the TLUT descriptor. Validate the binding before accepting it.
        pal=image+w*h+512
        if pal+16>len(a.b) or pal not in valid or a.word(pal+4)!=2 or struct.unpack_from('>H',a.b,pal+12)[0]!=256:continue
        if a.word(pal)!=image+w*h:continue
        seen.add(image);images.append({'image':image,'width':w,'height':h,'format':fmt,'palette':pal,'descriptor':o})
    return a,sorted(images,key=lambda d:d['image'])

if __name__=='__main__':
    a,images=collect();sheet=Image.new('RGB',(8*110,((len(images)+7)//8)*164),(22,26,34));pen=ImageDraw.Draw(sheet)
    for i,d in enumerate(images):
        im=decode(a,d['image'],d['width'],d['height'],d['format'],d['palette']);im.thumbnail((106,142))
        x=(i%8)*110;y=(i//8)*164;sheet.paste(im,(x,y),im);pen.text((x+3,y+143),str(i)+' / '+str(d['image']),fill='white')
    sheet.save(ROOT/'build/bottom-portraits.png');(ROOT/'build/bottom-portraits.json').write_text(json.dumps(images,indent=2));print(len(images),'unique portraits')
