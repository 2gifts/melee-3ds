"""Return from the original Results screens to CSS using START presses."""
import json,time
from versus_flow_test import ROOT,observe
def main():
    records=[];deadline=time.monotonic()+90
    try:
        while time.monotonic()<deadline:
            state=observe();records.append(state)
            if state['failed']:raise RuntimeError('Engine stopped in Results')
            if state['mode']==2 and state['scene']==0:
                print('Original Results returned to character selection');return
            if state['mode']!=2 or state['scene']!=4:raise RuntimeError(state)
            observe((0x1000,3,0,0));time.sleep(1)
            observe((0,3,0,0));time.sleep(1)
        raise TimeoutError('Results did not return to CSS')
    finally:(ROOT/'build/versus-return-test.json').write_text(json.dumps(records,indent=2))
if __name__=='__main__':main()
