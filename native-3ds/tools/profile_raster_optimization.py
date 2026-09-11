"""Original/index-only/both/original comparison in one running match."""
import argparse,json,time
from gameplay_test import ROOT
from profile_render_detail import snapshot,summarize
from raster_state_gpu_test import state as raster_state
from texture_index_live_test import state as texture_state

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--seconds',type=int,default=12);args=ap.parse_args()
    initial_raster=raster_state();initial_texture=texture_state();result=[]
    try:
        texture_state(texture_lookup_validate=0)
        for name,texture_disabled,raster_disabled in [('original',1,1),('texture_index',0,1),('both',0,0),('original',1,1)]:
            texture_state(texture_lookup_disable=texture_disabled);raster_state(raster_state_disable=raster_disabled)
            start=snapshot(True)
            try:time.sleep(args.seconds)
            finally:end=snapshot(False)
            result.append({'mode':name,**summarize(start,end)});print(json.dumps(result[-1]),flush=True)
    finally:
        raster_state(raster_state_disable=initial_raster['raster_state_disable'])
        texture_state(texture_lookup_disable=initial_texture['texture_lookup_disable'],texture_lookup_validate=initial_texture['texture_lookup_validate'])
        (ROOT/'build/raster-optimization-profile.json').write_text(json.dumps(result,indent=2))
if __name__=='__main__':main()
