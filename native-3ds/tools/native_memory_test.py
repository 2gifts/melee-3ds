"""Run the ARM/BE8 comparison fixture, not a host substitute."""
import json,socket,struct,time
from gameplay_test import ROOT,symbols,packet,receive
from profile_switch import set_word

def main():
    set_word('mp_test_memory_compare',1);deadline=time.monotonic()+60
    while True:
        time.sleep(.2)
        with socket.create_connection(('127.0.0.1',24689),3) as s:
            s.settimeout(5);packet(s,'?');receive(s)
            try:
                packet(s,f'm{symbols["mp_test_memory_compare"]:x},4');cases=int.from_bytes(bytes.fromhex(receive(s)),'little')
                packet(s,f'm{symbols["engine_failed"]:x},4');failed=int.from_bytes(bytes.fromhex(receive(s)),'little')
                packet(s,f'm{symbols["mp_memory_compare_ticks"]:x},c');ticks=struct.unpack('>3I',bytes.fromhex(receive(s)))
            finally:packet(s,'c');packet(s,'D');receive(s)
        assert not failed,'BE8 memory comparison failed'
        if cases>1:break
        if time.monotonic()>deadline:raise TimeoutError('Memory fixture did not finish')
    assert cases==262208,(cases,ticks)
    result={'cases':cases,'arm_ms':ticks[0]/40500,'c_reference_ms':ticks[1]/40500,'block_arm_ms':ticks[2]/40500,'scope':'Emulated ARM11, distinct equal buffers, matched/mismatched alignment, lengths, mismatch locations and byte ordering'}
    (ROOT/'build/native-memory-test.json').write_text(json.dumps(result,indent=2));print(json.dumps(result,indent=2))
if __name__=='__main__':main()
