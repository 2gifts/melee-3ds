"""Mix the original menu theme and intro call for the private HOME banner."""
import json
import math
import struct
import subprocess
import wave
import numpy as np
from banner_assets import ROOT, OUT, dsp, write_wav


def menu_music(seconds=3.0):
    """Decode the beginning of Melee's menu01 HALPST/DSP stream."""
    raw=(ROOT/'assets/GALE01/files/audio/menu01.hps').read_bytes()
    assert raw[:8] == b' HALPST\0'
    rate,channels=struct.unpack_from('>II',raw,8)
    assert channels==2 and 8000<=rate<=48000
    coefficients=[struct.unpack_from('>16h',raw,0x20+0x38*c) for c in range(channels)]
    pcm=[[],[]];offset=0x80;seen=set();wanted=math.ceil(seconds*rate)
    while len(pcm[0])<wanted:
        assert offset not in seen and offset+32<=len(raw)
        seen.add(offset)
        size,nibbles,next_offset=struct.unpack_from('>III',raw,offset)
        assert size%channels==0 and offset+32+size<=len(raw)
        channel_size=size//channels
        samples=(nibbles+1)//16*14+max(0,(nibbles+1)%16-2)
        assert 0<samples<=channel_size//8*14
        for c in range(channels):
            start=offset+32+c*channel_size
            history=pcm[c][-2:] if len(pcm[c])>=2 else [0,0]
            pcm[c].extend(dsp(raw[start:start+channel_size],coefficients[c],samples,history[-1],history[-2]))
        offset=next_offset
    return [c[:wanted] for c in pcm],rate


def make_banner_audio(art):
    """Keep the call prominent over the menu theme, without clipping."""
    subprocess.run(['ffmpeg','-y','-hide_banner','-loglevel','error','-ss','2.43',
        '-i',str(OUT/'announcer-1.wav'),'-af',
        'atempo=1.455,afade=t=in:st=0:d=0.01,afade=t=out:st=2.86:d=0.06',
        '-t','2.95','-ar','32000','-ac','2','-c:a','pcm_s16le',str(art/'announcer-voice.wav')],check=True)
    music,rate=menu_music()
    write_wav(OUT/'menu-theme.wav',music,rate)
    subprocess.run(['ffmpeg','-y','-hide_banner','-loglevel','error',
        '-i',str(OUT/'menu-theme.wav'),'-ar','32000','-ac','2','-c:a','pcm_s16le',
        str(OUT/'menu-theme-32k.wav')],check=True)
    def read(path):
        with wave.open(str(path),'rb') as f:
            assert (f.getframerate(),f.getnchannels(),f.getsampwidth())==(32000,2,2)
            return np.frombuffer(f.readframes(f.getnframes()),dtype='<i2').reshape(-1,2).astype(float)/32768
    voice=read(art/'announcer-voice.wav');theme=read(OUT/'menu-theme-32k.wav')[:len(voice)]
    assert theme.shape==voice.shape and np.sqrt(np.mean(theme**2))>.005
    ramp=np.ones(len(theme));ramp[:640]=np.linspace(0,1,640);ramp[-2560:]=np.linspace(1,0,2560)
    voice*=.80/max(abs(voice).max(),1e-9)
    theme*=.32/max(abs(theme).max(),1e-9)
    mix=voice+theme*ramp[:,None]
    gain=min(1,.95/max(abs(mix).max(),1e-9));mix*=gain
    write_wav(art/'announcer.wav',np.rint(mix*32767).astype('<i2').T,32000)
    report=dict(music='audio/menu01.hps',announcer='audio/nr_title.ssm:1',
                rate=32000,frames=len(mix),seconds=len(mix)/32000,
                peak=float(abs(mix).max()),rms=float(np.sqrt(np.mean(mix**2))),
                music_peak=.32,voice_peak=.80,master_gain=gain)
    (art/'audio-report.json').write_text(json.dumps(report,indent=2)+'\n')
    return report


if __name__=='__main__':
    print(json.dumps(make_banner_audio(ROOT/'build/home-menu/art'),indent=2))
