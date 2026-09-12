"""Read original Melee texture/audio assets for the private HOME Menu package."""
import json,struct,wave,subprocess
from pathlib import Path
import numpy as np
from PIL import Image,ImageDraw
from audit_diet_fountain import Archive
from assets import ROOT

OUT=ROOT/'build/home-menu/assets'

def rgb565(v):
    return (((v>>11)*255//31),(((v>>5)&63)*255//63),((v&31)*255//31),255)

def rgb5a3(v):
    return (((v>>10)&31)*255//31,((v>>5)&31)*255//31,(v&31)*255//31,255) if v&32768 else (((v>>8)&15)*17,((v>>4)&15)*17,(v&15)*17,((v>>12)&7)*255//7)

def texture(a,desc,pal=0):
    off,w,h,fmt=struct.unpack_from('>IHHI',a.b,desc)
    assert 0<w<=2048 and 0<h<=2048 and fmt in (0,1,2,3,4,5,6,8,9,10,14)
    bw,bh=(8,8) if fmt in (0,8,14) else (8,4) if fmt in (1,2,9) else (4,4)
    size=64 if fmt==6 else 32;pitch=(w+bw-1)//bw
    assert off+pitch*((h+bh-1)//bh)*size<=len(a.b)
    palette=[]
    if fmt in (8,9,10):
        assert pal
        po,pf,_,pn=struct.unpack_from('>IIIH',a.b,pal)
        for i in range(pn):
            v=struct.unpack_from('>H',a.b,po+i*2)[0]
            palette.append(rgb5a3(v) if pf==2 else rgb565(v) if pf==1 else (v&255,v&255,v&255,v>>8))
    pixels=bytearray(w*h*4);cmpr={}
    for y in range(h):
        for x in range(w):
            p=off+((y//bh)*pitch+x//bw)*size;k=(y%bh)*bw+x%bw
            if fmt==0:v=((a.b[p+k//2]>>(0 if k&1 else 4))&15)*17;c=(v,v,v,v)
            elif fmt==1:v=a.b[p+k];c=(v,v,v,v)
            elif fmt==2:v=a.b[p+k];c=((v&15)*17,)*3+((v>>4)*17,)
            elif fmt in (3,4,5):
                v=struct.unpack_from('>H',a.b,p+k*2)[0]
                c=rgb565(v) if fmt==4 else rgb5a3(v) if fmt==5 else (v&255,v&255,v&255,v>>8)
            elif fmt==6:c=(a.b[p+k*2+1],a.b[p+32+k*2],a.b[p+33+k*2],a.b[p+k*2])
            elif fmt in (8,9,10):
                ix=(a.b[p+k//2]>>(0 if k&1 else 4))&15 if fmt==8 else a.b[p+k] if fmt==9 else struct.unpack_from('>H',a.b,p+k*2)[0]&16383
                c=palette[ix]
            else:
                p+=((y%8)//4*2+(x%8)//4)*8
                if p not in cmpr:
                    aa,bb=struct.unpack_from('>HH',a.b,p);ca,cb=rgb565(aa),rgb565(bb)
                    mix=lambda wa,wb,d:tuple((ca[i]*wa+cb[i]*wb)//d for i in range(3))+(255,)
                    cmpr[p]=[ca,cb,mix(5,3,8),mix(3,5,8)] if aa>bb else [ca,cb,mix(1,1,2),mix(1,1,2)[:3]+(0,)]
                c=cmpr[p][(a.b[p+4+y%4]>>(6-2*(x%4)))&3]
            pixels[(y*w+x)*4:(y*w+x+1)*4]=bytes(c)
    return Image.frombytes('RGBA',(w,h),bytes(pixels))

def inspect_title():
    a=Archive(ROOT/'assets/GALE01/files/GmTitle.usd');seen=set();results=[]
    relocs=struct.unpack_from('>'+str(a.inv['relocations'])+'I',a.raw,32+len(a.b))
    # TObj descriptors bind both image and palette; do not guess TLUT locations.
    for p in relocs:
        d=a.word(p)
        if d+24>len(a.b):continue
        _,w,h,fmt=struct.unpack_from('>IHHI',a.b,d)
        if (d in seen or not 8<=w<=1024 or not 8<=h<=1024 or fmt not in (0,1,2,3,4,5,6,8,9,10,14)):continue
        try:im=texture(a,d,a.word(p+4) if fmt in (8,9,10) else 0)
        except (AssertionError,IndexError,struct.error):continue
        seen.add(d);name=f'title-{d:06x}.png';im.save(OUT/name);results.append(dict(descriptor=d,width=w,height=h,format=fmt,file=name))
    sheet=Image.new('RGB',(640,180*((len(results)+3)//4)),(35,38,44));pen=ImageDraw.Draw(sheet)
    for i,row in enumerate(results):
        im=Image.open(OUT/row['file']);im.thumbnail((154,146));x=i%4*160;y=i//4*180
        sheet.paste(im,(x,y),im);pen.text((x+2,y+148),f"{row['descriptor']:06x} {row['width']}x{row['height']}",fill='white')
    sheet.save(OUT/'title-sheet.png');(OUT/'title-inventory.json').write_text(json.dumps(results,indent=2)+'\n')
    return results

def dsp(data,coeff,count,h1=0,h2=0):
    out=[]
    for p in range(0,len(data)-7,8):
        shift=data[p]&15;pred=data[p]>>4;assert pred<8
        a,b=coeff[pred*2:pred*2+2]
        for n in range(14):
            v=(data[p+1+n//2]>>(0 if n&1 else 4))&15
            if v>=8:v-=16
            s=max(-32768,min(32767,((v<<shift)*2048+a*h1+b*h2+1024)>>11))
            h2,h1=h1,s;out.append(s)
            if len(out)==count:return out
    assert len(out)>=count,(len(out),count)
    return out[:count]

def write_wav(path,channels,rate):
    pcm=np.array(channels,dtype='<i2').T.copy()
    with wave.open(str(path),'wb') as f:
        f.setnchannels(len(channels));f.setsampwidth(2);f.setframerate(rate);f.writeframes(pcm.tobytes())

def extract_announcer():
    raw=(ROOT/'assets/GALE01/files/audio/nr_title.ssm').read_bytes()
    header,data_size,count,bank=struct.unpack_from('>4I',raw);base=len(raw)-data_size;o=16;result=[]
    for i in range(count):
        channels,rate=struct.unpack_from('>II',raw,o);pcm=[]
        for ch in range(channels):
            q=o+8+ch*64;end,start=struct.unpack_from('>II',raw,q+8)
            n=end-start;samples=n//16*14+max(0,n%16-2)
            co=struct.unpack_from('>16h',raw,q+16);h1,h2=struct.unpack_from('>hh',raw,q+52)
            off=base+start//16*8
            pcm.append(dsp(raw[off:],co,samples,h1,h2))
        dest=OUT/f'announcer-{i}.wav';write_wav(dest,pcm,rate)
        result.append(dict(index=i,channels=channels,rate=rate,seconds=len(pcm[0])/rate,file=dest.name));o+=8+channels*64
    (OUT/'announcer-inventory.json').write_text(json.dumps(result,indent=2)+'\n');return result

if __name__=='__main__':
    OUT.mkdir(parents=True,exist_ok=True)
    print('title textures',len(inspect_title()))
    print(json.dumps(extract_announcer(),indent=2))
    art=ROOT/'build/home-menu/art';art.mkdir(parents=True,exist_ok=True)
    from home_banner_audio import make_banner_audio
    print(json.dumps(make_banner_audio(art),indent=2))
