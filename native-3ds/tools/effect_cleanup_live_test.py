"""Trigger Fox effects with pad input; require the crashed cleanup path to run."""
import json,socket,time
from gameplay_test import ROOT,symbols,packet,receive,exchange
from combat_test import act

def counters():
    with socket.create_connection(('127.0.0.1',24689),3) as sock:
        packet(sock,'?');receive(sock)
        try:
            result={}
            for name in ('mp_particle_cleanup_calls','mp_particle_cleanup_deleted'):
                packet(sock,f'm{symbols[name]:x},4');result[name]=int.from_bytes(bytes.fromhex(receive(sock)),'big')
            return result
        finally:packet(sock,'c');packet(sock,'D');receive(sock)

def main():
    start=counters();records=[]
    for i in range(8):
        state=exchange();assert not state['failed'] and state['fighters'][0]['kind']==1,state # internal Fox kind
        # Shine, jump cancel, and up-special exercise generator destruction.
        for buttons,frames,x,y in [(0x200,8,0,-80),(0x400,2,0,0),(0,18,0,0),(0x200,45,0,80),(0,35,0,0)]:
            records.append(act(buttons,frames,x,y))
        end=counters()
        if end['mp_particle_cleanup_deleted']>start['mp_particle_cleanup_deleted']:break
    assert end['mp_particle_cleanup_calls']>start['mp_particle_cleanup_calls'],end
    assert end['mp_particle_cleanup_deleted']>start['mp_particle_cleanup_deleted'],end
    result={'start':start,'end':end,'input_samples':records,'game_state_memory_modified':False}
    (ROOT/'build/effect-cleanup-live-test.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps({'particle_cleanup':end,'passed':True}),flush=True)

if __name__=='__main__':main()
