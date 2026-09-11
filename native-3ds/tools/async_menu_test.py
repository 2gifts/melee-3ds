"""Prove menu frames continue while delayed SD reads are outstanding."""
import json,time
from gameplay_test import ROOT
from texture_visibility_test import read
from profile_switch import set_word
from stage_sweep import exit_training
import select_test_stage as select

def main():
    names=('engine_frames','engine_failed','io_state','mp_async_reads','mp_async_busy_polls','stereo_active')
    original=read(('mp_test_disc_delay_ms','framebuffer_range_disable','mp_test_stereo_slider'))
    rows=[]
    try:
        set_word('framebuffer_range_disable',0);set_word('mp_test_stereo_slider',1000)
        if 'hand' not in select.observe():exit_training()
        set_word('mp_test_disc_delay_ms',20)
        for icon in (21,23,6):
            before=read(names);select.select_character(select.observe(),0,icon)
            samples=[];deadline=time.monotonic()+1.5
            while time.monotonic()<deadline:
                samples.append(read(names));time.sleep(.05)
            assert not any(s['engine_failed'] for s in samples),samples
            assert not any(s['stereo_active'] for s in samples),'Flat menus should render one view'
            pending=[s for s in samples if s['io_state']==1]
            if len(pending)>1:
                assert pending[-1]['engine_frames']-pending[0]['engine_frames']>=15,pending
            row={'icon':icon,'before':before,'samples':samples,'pending_frames':pending[-1]['engine_frames']-pending[0]['engine_frames'] if len(pending)>1 else 0}
            rows.append(row);print(json.dumps(row),flush=True)
        assert sum(r['pending_frames'] for r in rows)>=30,rows
        print('Menus advance while SD worker reads are pending; slider keeps flat menus on one view',flush=True)
    finally:
        for name,value in original.items():set_word(name,value)
        (ROOT/'build/async-menu-test.json').write_text(json.dumps(rows,indent=2)+'\n')

if __name__=='__main__':main()
