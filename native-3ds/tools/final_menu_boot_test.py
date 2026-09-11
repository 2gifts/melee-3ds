"""Boot the final four-file cache build and verify its real title/menu reads."""
import json,socket,time
from select_test_stage import ROOT,observe,act,open_mode,select_character,symbols,packet,receive
from match_fix_menu_test import capture
from menu_cache_capacity_test import counters

def main():
    deadline=time.monotonic()+150
    while time.monotonic()<deadline:
        try:s=observe()
        except (ConnectionRefusedError,ConnectionResetError):time.sleep(.1);continue
        assert not s['failed'],s
        if s['frame']>=450 and 'menu' in s:act();break
        time.sleep(.1)
    else:raise TimeoutError('Main menu')
    start=counters();capture('final-main')
    assert start['mp_file_cache_read_bytes']>=276257,start
    s=open_mode(observe(),True);act(frames=35)
    s=select_character(observe(),0,10);s=select_character(s,1,19)
    capture('final-css');act(0x1000,4);act(frames=45);capture('final-stages')
    end=counters();assert end['mp_file_cache_read_bytes']>=6879780,end
    assert end['mp_file_sd_bytes']>0 and not end['engine_failed'],end
    result={'cached_title_and_menus_bytes':6879780,'start':start,'end':end}
    (ROOT/'build/final-menu-boot-test.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result),flush=True)

if __name__=='__main__':main()
