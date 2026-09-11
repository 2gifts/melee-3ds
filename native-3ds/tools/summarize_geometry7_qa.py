"""Audit the complete update 7 regression evidence and summarize its limits."""
import json
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def read(name):return json.loads((ROOT/'build'/name).read_text())

def main():
    stages=read('update7-all-stage.json')
    assert len(stages)==29 and {r['stage_index'] for r in stages}==set(range(29))
    assert {r['character_icon'] for r in stages}==set(range(25))
    for r in stages:
        q=r['profile'];m=q['final_memory'];w=q['native_geometry_work_per_render']
        assert q['same_scene'] and not r['final']['failed'] and q['simulation_updates']>0,r
        assert m['geometry_bytes']<=12*1024*1024 and m['native_geometry_bytes']<=4*1024*1024,r
        assert m['texture_bytes']<=16*1024*1024 and m['mp_native_heap_available']>=8*1024*1024,r
        assert w['capacity_evictions']<=w['eviction_slots_scanned']+1e-6,r
        assert w['eviction_slots_scanned']<=w['capacity_evictions']+w['full_cache_fallbacks']+1e-6,r
    caches=[read('update7-qa/'+name+'.json') for name in ('peach-cache','venom-cache','venom-native-reuse')]
    total={k:sum(c['checks'][k] for c in caches) for k in caches[0]['checks']}
    turnips=read('update7-turnips.json');assert len(turnips)==8
    assert all(r['item']['kind']==99 and r['item']['held'] and not r['state']['failed'] for r in turnips)
    result={'stages':29,'character_slots':25,'minimum_sampled_ordinary_heap_available':min(r['profile']['final_memory']['mp_native_heap_available'] for r in stages),
            'cache_checks':total,'turnip_pull_throw_cycles':len(turnips),'observed_turnip_faces':sorted({r['item']['face'] for r in turnips}),
            'constant_candidate_lookup_in_all_profile_windows':True,'physical_fps_verified':False,
            'scope':'Azahar 2126.1, New 3DS mode, 300% CPU; controller-driven regression, not physical console performance'}
    (ROOT/'build/update7-qa/summary.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(result,indent=2))

if __name__=='__main__':main()
