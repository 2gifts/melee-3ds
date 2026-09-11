"""Measure I/O and texture upload work around actual character selections."""
import json
from gameplay_test import ROOT
from texture_visibility_test import read
from profile_switch import set_word
from stage_sweep import exit_training
import select_test_stage as select

def main():
    names=('texture_uploads','texture_upload_ticks','texture_upload_max_ticks','mp_file_cache_hits','mp_file_sd_reads','mp_file_sd_bytes','engine_failed')
    original=read(('framebuffer_range_disable','mp_file_trace'))
    records=[]
    try:
        set_word('framebuffer_range_disable',0);set_word('mp_file_trace',1)
        if 'hand' not in select.observe():exit_training()
        for icon in (23,6,4,23,6,4):
            set_word('texture_upload_max_ticks',0);start=read(names)
            s=select.select_character(select.observe(),0,icon);select.act(frames=60)
            end=read(names);assert not end['engine_failed'],end
            record={'icon':icon,'texture_uploads':end['texture_uploads']-start['texture_uploads'],
                    'texture_upload_ms':((end['texture_upload_ticks']-start['texture_upload_ticks'])&0xffffffff)/40500,
                    'longest_upload_ms':end['texture_upload_max_ticks']/40500,
                    'cached_reads':end['mp_file_cache_hits']-start['mp_file_cache_hits'],
                    'sd_reads':end['mp_file_sd_reads']-start['mp_file_sd_reads'],'sd_bytes':end['mp_file_sd_bytes']-start['mp_file_sd_bytes']}
            records.append(record);print(json.dumps(record),flush=True)
    finally:
        for name,value in original.items():set_word(name,value)
        (ROOT/'build/menu-latency-probe.json').write_text(json.dumps(records,indent=2)+'\n')

if __name__=='__main__':main()
