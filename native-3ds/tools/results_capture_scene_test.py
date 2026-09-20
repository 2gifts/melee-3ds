"""Freeze only test-fixture object processes to compare original Results eyes.

The temporary HSD process flag edits are restored before animated timings.
They are not shipped game behavior or evidence of controller-only play.
"""
import argparse,hashlib,json,socket,time
from contextlib import contextmanager
import numpy as np
from PIL import Image
from gameplay_test import ROOT,TEST_ELF,symbols,packet,receive
from efb_copy_test import SD
from profile_switch import set_word
from profile_render_detail import snapshot,summarize
from texture_visibility_test import read
import bottom_screen_test as bottom
import select_test_stage as select

@contextmanager
def connection():
    with socket.create_connection(('127.0.0.1',24689),3) as sock:
        sock.settimeout(5);packet(sock,'?');receive(sock)
        def get(address,size):
            packet(sock,f'm{address:x},{size:x}');r=bytes.fromhex(receive(sock));assert len(r)==size;return r
        def put(address,data):
            packet(sock,f'M{address:x},{len(data):x}:'+data.hex());assert receive(sock)=='OK'
        try:yield get,put
        finally:packet(sock,'c');packet(sock,'D');receive(sock)

@contextmanager
def frozen_processes():
    saved={}
    try:
        with connection() as (get,put):
            maximum=get(symbols['HSD_GObjLibInitData']+2,1)[0];assert maximum<64
            heads=int.from_bytes(get(symbols['HSD_GObj_GObjProcHead'],4),'big')
            for priority in range(maximum+1):
                node=int.from_bytes(get(heads+4*priority,4),'big')
                while node:
                    assert node not in saved and len(saved)<4096,'Invalid original process list'
                    raw=get(node,24);saved[node]=raw[13]&128
                    # HSD_GObjProc flags_1 is the first BE8 u8 bitfield.
                    put(node+13,bytes([raw[13]|128]));node=int.from_bytes(raw[4:8],'big')
        assert len(saved)>10
        yield len(saved)
    finally:
        with connection() as (get,put):
            for node,flag in saved.items():put(node+13,bytes([(get(node+13,1)[0]&127)|flag]))

def capture(label):
    bottom.capture(label)
    left=np.array(Image.open(bottom.OUT/(label+'-top.png')))
    right=np.rot90(np.fromfile(SD/'engine-right.bgr',dtype=np.uint8).reshape(400,240,3)[:,:,::-1]).copy()
    Image.fromarray(right).save(bottom.OUT/(label+'-right.png'))
    return left,right

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--output',required=True);args=ap.parse_args()
    bottom.OUT=ROOT/args.output;bottom.OUT.mkdir(parents=True,exist_ok=True)
    assert bottom.snapshot()['scene']==5
    counts=('engine_frames','engine_failed','results_capture_draws','results_capture_vertices','efb_cpu_copies')
    saved=read(('results_capture_disable','efb_rgb5a3_validate'))
    pairs=[];profiles=[]
    try:
        set_word('efb_rgb5a3_validate',1)
        with frozen_processes() as process_count:
            set_word('results_capture_disable',1);select.act(frames=15)
            reference=capture('original');repeat=capture('original-repeat')
            stable=[int(np.count_nonzero(a!=b)) for a,b in zip(reference,repeat)]
            assert stable==[0,0],('Results fixture is still animated',stable)
            before=read(counts);set_word('results_capture_disable',0);select.act(frames=15)
            optimized=capture('optimized');after=read(counts)
            assert after['results_capture_draws']>before['results_capture_draws']+100,after
            pairs=[int(np.count_nonzero(a!=b)) for a,b in zip(reference,optimized)]
            (bottom.OUT/'paired.json').write_text(json.dumps(dict(different_channels=pairs,stable_channels=stable,
                frozen_processes=process_count,before=before,after=after),indent=2)+'\n')
            assert pairs==[0,0],pairs
        # Original processes/animations are live again for balanced windows.
        set_word('efb_rgb5a3_validate',0)
        for disabled in (1,0,0,1,1,0):
            set_word('results_capture_disable',disabled);time.sleep(1)
            c0=read(counts);a=snapshot(True);time.sleep(8);b=snapshot(False);c1=read(counts)
            assert bottom.snapshot()['scene']==5 and not c1['engine_failed']
            delta={k:c1[k]-c0[k] for k in counts};n=delta['engine_frames'];assert n>30
            assert delta['results_capture_draws']==0 if disabled else delta['results_capture_draws']>n
            row=dict(disabled=disabled,counts=delta,**summarize(a,b));profiles.append(row)
            (bottom.OUT/'progress.json').write_text(json.dumps(profiles,indent=2)+'\n');print(json.dumps(row),flush=True)
        report=dict(passed=True,elf_sha256=hashlib.sha256(TEST_ELF.read_bytes()).hexdigest(),different_channels=pairs,
            profiles=profiles,physical_fps_verified=False,
            scope='Paired original Results with temporary process suspension; animated balanced emulator timings after restoration')
        (bottom.OUT/'summary.json').write_text(json.dumps(report,indent=2)+'\n')
    finally:
        for name,value in saved.items():set_word(name,value)

if __name__=='__main__':main()
