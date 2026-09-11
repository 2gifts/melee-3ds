"""Resume final QA from the Training character-select menu."""
import json
from update4_final_qa import ROOT, select, stage, run
from versus_flow_test import observe
state=select.observe()
for _ in range(4):
    if 'menu' in state:break
    state=select.act(0x200,40);state=select.act(frames=30)
else:raise RuntimeError('Held B did not leave character selection')
select.open_versus(state);stage(6,True)
initial=observe()
assert initial['mode']==2 and 470<=initial['timer']<=480,initial
(ROOT/'build/update4-default-versus-start.json').write_text(json.dumps(initial,indent=2))
run('versus_flow_test.py');run('versus_return_test.py')
print('Final default Versus match/results/return checks passed',flush=True)
