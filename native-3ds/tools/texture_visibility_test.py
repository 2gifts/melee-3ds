"""Validate cached source bytes while navigating real scenes and characters."""
import argparse,json,socket,subprocess,sys
from gameplay_test import ROOT,symbols
from gdb_probe import packet,receive
from profile_switch import set_word

def read(names):
    with socket.create_connection(('127.0.0.1',24689),3) as s:
        s.settimeout(5);packet(s,'?');receive(s)
        try:
            result={}
            for name in names:
                packet(s,f'm{symbols[name]:x},4');result[name]=int.from_bytes(bytes.fromhex(receive(s)),'little')
            return result
        finally:packet(s,'c');packet(s,'D');receive(s)

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--stages',default='18,19,11,8,6,23')
    ap.add_argument('--label',default='texture-visibility-live');args=ap.parse_args()
    flags=read(('framebuffer_range_disable','texture_content_validate','efb_rgb565_validate'))
    names=('texture_content_checks','texture_dirty_calls','texture_dirty_invalidated','efb_rgb565_checks')
    initial=read(names)
    try:
        set_word('framebuffer_range_disable',0);set_word('texture_content_validate',1);set_word('efb_rgb565_validate',1)
        subprocess.run([sys.executable,str(ROOT/'tools/stage_sweep.py'),'--stages',args.stages,'--seconds','2',
                        '--label',args.label+'-stages','--image-directory',args.label,'--character-offset','4'],check=True)
        final=read(names);delta={n:final[n]-initial[n] for n in names}
        assert delta['texture_content_checks']>1000 and delta['texture_dirty_invalidated']>100,delta
        (ROOT/f'build/{args.label}.json').write_text(json.dumps({'checks':delta,'source_content_compared':True},indent=2))
        print('Texture visibility checks passed:',delta,flush=True)
    finally:
        for name,value in flags.items():set_word(name,value)

if __name__=='__main__':main()
