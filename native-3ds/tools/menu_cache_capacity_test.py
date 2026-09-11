"""Compare repeated CSS transitions at 256 and 1024 resident texture slots."""
import json,socket,struct,time
from select_test_stage import ROOT,observe,act,open_mode,symbols,packet,receive

def counters(limit=None):
    with socket.create_connection(('127.0.0.1',24689),3) as sock:
        packet(sock,'?');receive(sock)
        try:
            if limit is not None:
                packet(sock,f'M{symbols["texture_slot_budget"]:x},4:'+struct.pack('<I',limit).hex());assert receive(sock)=='OK'
            result={}
            for name in ('texture_uploads','texture_evictions','texture_count','texture_bytes','mp_file_cache_hits','mp_file_cache_read_bytes','mp_file_sd_bytes','engine_failed'):
                packet(sock,f'm{symbols[name]:x},4');result[name]=int.from_bytes(bytes.fromhex(receive(sock)),'little')
            return result
        finally:packet(sock,'c');packet(sock,'D');receive(sock)

def cycle():
    s=act(0x200,90);s=act(frames=40)
    for _ in range(3):
        if 'menu' in s:break
        s=act(0x200,90);s=act(frames=40)
    assert 'menu' in s,s
    s=open_mode(s,True);s=act(frames=35);assert 'hand' in s,s

def main():
    records=[]
    try:
        for limit in (256,1024,256,1024):
            counters(limit);cycle();start=counters();cycle();end=counters()
            assert not end['engine_failed'] and end['texture_bytes']<=16*1024*1024,end
            result={'slot_limit':limit,'start':start,'end':end,'new_uploads':end['texture_uploads']-start['texture_uploads'],'evictions':end['texture_evictions']-start['texture_evictions']}
            records.append(result);print(json.dumps(result),flush=True)
    finally:
        counters(1024);(ROOT/'build/menu-cache-capacity-test.json').write_text(json.dumps(records,indent=2)+'\n')
if __name__=='__main__':main()
