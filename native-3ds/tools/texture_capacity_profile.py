"""Compare live texture capacities using counters and the emulated VI clock."""
import json,socket,struct,time
from gameplay_test import ROOT,symbols,packet,receive

def inspect(budget=None):
    with socket.create_connection(('127.0.0.1',24689),3) as sock:
        sock.settimeout(5);packet(sock,'?');receive(sock)
        def read(name,n=4):
            packet(sock,f'm{symbols[name]:x},{n:x}');return bytes.fromhex(receive(sock))
        try:
            if budget is not None:
                packet(sock,f'M{symbols["texture_budget"]:x},4:'+budget.to_bytes(4,'little').hex());assert receive(sock)=='OK'
            result={name:int.from_bytes(read(name),'little') for name in
                ('engine_failed','engine_frames','texture_bytes','texture_barriers','texture_budget','native_geometry_bytes')}
            result['simulation'],result['render'],result['match']=struct.unpack('>3I',read('gm_80479D58',12))
            result['ticks']=int.from_bytes(read('last_retrace'),'big')
            result['route']=list(read('state_machine',6)[:4])
            return result
        finally:packet(sock,'c');packet(sock,'D');receive(sock)

def main():
    records=[];initial=inspect()
    try:
        for mb in (8,12,8):
            inspect(mb*1024*1024);time.sleep(3);start=inspect();time.sleep(10);end=inspect()
            assert not end['engine_failed'] and end['route']==start['route'],end
            elapsed=((end['ticks']-start['ticks'])&0xffffffff)/40500000
            result={'budget_mib':mb,'start':start,'end':end,'emulated_seconds':elapsed,
                'rates':{key:(end[key]-start[key])/elapsed for key in ('simulation','render','texture_barriers')}}
            records.append(result);print(json.dumps(result),flush=True)
    finally:
        inspect(initial['texture_budget'])
        (ROOT/'build/texture-capacity-profile.json').write_text(json.dumps(records,indent=2))
if __name__=='__main__':main()
