"""Exercise native thread handoff with cached, uncached and interleaved reads."""
import json,time
from gameplay_test import ROOT
from texture_visibility_test import read
from profile_switch import set_word

def main():
    before=read(('mp_async_file_checks',))['mp_async_file_checks']
    set_word('mp_test_async_files',1);deadline=time.monotonic()+90
    try:
        while True:
            time.sleep(.25);s=read(('mp_test_async_files','mp_async_file_checks','engine_failed'))
            assert not s['engine_failed'],s
            if s['mp_test_async_files']==2:break
            if time.monotonic()>deadline:raise TimeoutError(s)
        result={'cases':s['mp_async_file_checks']-before,'whole_buffers_and_guards_compared':True,
                'covers':['cached menu data','uncached animation data','interleaved synchronous reads','zero-length','unaligned','buffer boundaries','EOF padding','invalid ranges and IDs']}
        assert result['cases']==64,result
        (ROOT/'build/async-file-test.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result),flush=True)
    finally:set_word('mp_test_async_files',0)

if __name__=='__main__':main()
