"""Select an observed attract encounter before its assets load, for debugging.

This diagnostic overrides the next demo's configuration through GDB. It is
separate from the input-only gameplay and untouched attract-flow regressions.
"""
import argparse,json,socket,time
from gameplay_test import ROOT,symbols,packet,receive
import bottom_screen_test as bottom
import select_test_stage as select
from profile_switch import set_word


def configure():
    address=symbols['gm_PreloadTitleDemo']
    with socket.create_connection(('127.0.0.1',24689),3) as s:
        s.settimeout(45);packet(s,'?');receive(s)
        packet(s,f'Z0,{address:x},4');assert receive(s)=='OK'
        packet(s,'c');receive(s)
        old={};new={}
        # LLVM splits this private configuration into independently named
        # fields; use their actual symbols instead of assuming adjacency.
        for suffix,raw in [(str(i),bytes([c])) for i,c in enumerate((4,6,0,3))]+[('10',b'\1'),('11',b'\0\r')]:
            data=symbols['gm_8049E548.'+suffix];n=len(raw)
            packet(s,f'm{data:x},{n:x}');old[suffix]=receive(s);new[suffix]=raw.hex()
            packet(s,f'M{data:x},{n:x}:'+raw.hex());assert receive(s)=='OK'
        packet(s,f'z0,{address:x},4');assert receive(s)=='OK'
        packet(s,'c');packet(s,'D');receive(s)
    return {'old_config':old,'debug_config':new}


def main():
    ap=argparse.ArgumentParser();ap.add_argument('--label',default='visibility-repro')
    ap.add_argument('--no-growth',action='store_true');ap.add_argument('--no-pipeline',action='store_true')
    args=ap.parse_args();select.observe((0,1,0,0));set_word('mp_test_stereo_slider',1000)
    set_word('geometry_source_grow',int(not args.no_growth),'big')
    set_word('gpu_pipeline_disable',int(args.no_pipeline));result=configure();print(result,flush=True)
    result['states']=[];deadline=time.monotonic()+50;last=None;progress=time.monotonic()
    while time.monotonic()<deadline:
        state=bottom.snapshot()
        if state['scene']==2:state['kirby']=kirby()
        result['states'].append(state)
        if state['engine_frames']!=last:last=state['engine_frames'];progress=time.monotonic()
        if state['engine_failed'] or time.monotonic()-progress>12:
            result['stalled']=True;break
        time.sleep(.3)
    observed=[p for state in result['states'] for p in state.get('kirby',[]) if p['copy_kind']==24]
    result['copied_gamewatch']=len(observed)
    result['passed']=bool(observed) and not result.get('stalled',False)
    (ROOT/'build/update15-qa'/(args.label+'.json')).write_text(json.dumps(result,indent=2)+'\n')
    print({'stalled':result.get('stalled',False),'copied_gamewatch':len(observed),'passed':result['passed'],'last':result['states'][-1]},flush=True)


def kirby():
    with socket.create_connection(('127.0.0.1',24689),3) as s:
        s.settimeout(5);packet(s,'?');receive(s)
        def read(a,n):packet(s,f'm{a:x},{n:x}');return bytes.fromhex(receive(s))
        def word(a):return int.from_bytes(read(a,4),'big')
        try:
            gobj=word(word(symbols['HSD_GObjPLinkHead'])+32);result=[]
            for i in range(8):
                if not gobj:break
                fp=word(gobj+44)
                if word(fp+4)==4:
                    kind=word(fp+0x2238);table=word(fp+0x5c8);count=word(fp+0x5ac)
                    row=dict(copy_kind=kind,body_count=count,hat_count=word(fp+0x2250),body_table=table,hat_table=word(fp+0x226c))
                    if kind==24 and table:
                        row['body_groups']=read(table,count*8).hex()
                        if 'mp_kirby_body_vis' in symbols:
                            player=read(fp+12,1)[0]
                            assert table==symbols['mp_kirby_body_vis']+player*88,row
                            hat=read(row['hat_table'],row['hat_count']*8)
                            assert bytes.fromhex(row['body_groups'])==hat+bytes((count-row['hat_count'])*8),row
                    result.append(row)
                gobj=word(gobj+8)
            return result
        finally:packet(s,'c');packet(s,'D');receive(s)


if __name__=='__main__':main()
