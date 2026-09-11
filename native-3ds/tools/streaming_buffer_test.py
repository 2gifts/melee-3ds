"""Force buffer reuse while verifying retained pixels and ongoing gameplay."""
import json,socket,time
from gameplay_test import ROOT,symbols,packet,receive
import point_gpu_test,cull_gpu_test,blend_gpu_test,efb_copy_test

def inspect(limit=None):
    with socket.create_connection(('127.0.0.1',24689),3) as sock:
        sock.settimeout(5);packet(sock,'?');receive(sock)
        try:
            if limit is not None:
                packet(sock,f'M{symbols["stream_vertex_budget"]:x},4:'+limit.to_bytes(4,'little').hex());assert receive(sock)=='OK'
            result={}
            for name in ('engine_frames','engine_failed','stream_vertex_budget','stream_barriers','texture_barriers','texture_bytes'):
                packet(sock,f'm{symbols[name]:x},4');result[name]=int.from_bytes(bytes.fromhex(receive(sock)),'little')
            return result
        finally:packet(sock,'c');packet(sock,'D');receive(sock)

def main():
    records=[];initial=inspect();records.append(initial)
    try:
        inspect(512)
        point_gpu_test.main();cull_gpu_test.main();blend_gpu_test.main();efb_copy_test.main()
        deadline=time.monotonic()+120
        while True:
            current=inspect();records.append(current)
            assert not current['engine_failed'],current
            if current['engine_frames']>=initial['engine_frames']+90:break
            if time.monotonic()>deadline:raise TimeoutError('Streaming stress stopped advancing')
            time.sleep(.5)
        assert current['stream_barriers']>initial['stream_barriers'],current
        print(json.dumps({'passed':True,'before':initial,'after':current}),flush=True)
    finally:
        records.append(inspect(initial['stream_vertex_budget']))
        (ROOT/'build/streaming-buffer-test.json').write_text(json.dumps(records,indent=2))
if __name__=='__main__':main()
