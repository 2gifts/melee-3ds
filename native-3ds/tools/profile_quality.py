"""Compare visual reductions in one live Training match, restoring defaults."""
import json,time
from gameplay_test import ROOT
from profile_switch import set_word
from profile_render_detail import snapshot,summarize

FLAGS=['mp_performance_low_poly','mp_performance_hide_stars','mp_performance_skip_reflection']
def mode(values):
    for flag,value in zip(FLAGS,values):set_word(flag,value,'big')

def main():
    results=[]
    try:
        for label,values in [('original',(0,0,0)),('low_poly',(1,0,0)),('no_stars',(1,1,0)),('performance',(1,1,1)),('original_repeat',(0,0,0)),('performance_repeat',(1,1,1))]:
            mode(values);time.sleep(.4);start=snapshot(True);time.sleep(4);end=snapshot(False)
            r={'label':label,'flags':values,**summarize(start,end)}
            assert r['same_scene'] and start['scene']==[28,2],r
            results.append(r);print(json.dumps(r),flush=True)
    finally:
        mode((1,1,1));(ROOT/'build/gameplay-quality-profile.json').write_text(json.dumps(results,indent=2))
if __name__=='__main__':main()
