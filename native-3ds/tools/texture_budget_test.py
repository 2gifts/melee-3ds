"""Force live GPU texture eviction; restore the production budget afterward."""
import json,socket,time
from gameplay_test import ROOT,symbols,packet,receive
def inspect(budget=None):
    with socket.create_connection(('127.0.0.1',24689),3) as sock:
        sock.settimeout(8);packet(sock,'?');receive(sock)
        try:
            if budget is not None:
                packet(sock,f'M{symbols["texture_budget"]:x},4:'+budget.to_bytes(4,'little').hex());assert receive(sock)=='OK'
            result={}
            for name in ('engine_failed','engine_frames','texture_bytes','texture_barriers','texture_budget'):
                packet(sock,f'm{symbols[name]:x},4');result[name]=int.from_bytes(bytes.fromhex(receive(sock)),'little')
            return result
        finally:packet(sock,'c');packet(sock,'D');receive(sock)
records=[]
try:
    initial=inspect();records.append(initial);print(json.dumps(initial),flush=True)
    inspect(1024*1024);deadline=time.monotonic()+120
    while time.monotonic()<deadline:
        time.sleep(.5);state=inspect();records.append(state)
        assert not state['engine_failed'],state
        if state['engine_frames']>=initial['engine_frames']+90:break
    else:raise TimeoutError('Texture pressure run stopped advancing')
    assert state['texture_barriers']>initial['texture_barriers'],state
finally:
    records.append(inspect(initial['texture_budget']))
    (ROOT/'build/texture-budget-test.json').write_text(json.dumps(records,indent=2))
print(json.dumps(records[-1]),flush=True)
