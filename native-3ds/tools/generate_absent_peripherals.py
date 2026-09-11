"""Generate typed adapters for physically absent GameCube peripherals."""
from pathlib import Path
import re
ROOT=Path(__file__).resolve().parents[1]
headers=ROOT/'upstream/melee/extern/dolphin/include/dolphin'
text='#include <dolphin/card.h>\n#include <dolphin/mcc.h>\n'
text+='/* The 3DS has no GameCube memory card or development-host interface.\n'
text+=' * Return the SDK absence/failure result. No operation is accepted. */\n'
seen=set()
for p in [headers/'card.h',*sorted((headers/'card').glob('*.h')),headers/'mcc.h']:
    for m in re.finditer(r'^(void|int|s32|u32|u8|long)\s+((?:CARD|MCC|FIO)\w+)\s*(\([^;]+?\));',p.read_text(),re.M):
        ret,name,args=m.groups()
        if name in seen:continue
        seen.add(name)
        body=''
        if ret!='void':
            value='CARD_RESULT_NOCARD' if name.startswith('CARD') else '0'
            if name in ('CARDProbe','CARDGetXferredBytes'):value='0'
            if name in ('FIOFopen','FIOFclose','FIOFindFirst','FIOFindNext'):value='-1'
            if name in ('FIOGetLastError','MCCGetLastError'):value='1'
            if name=='MCCGetConnectionStatus':body='if(connect)*connect=0;'
            body+='return '+value+';'
        text+=ret+' '+name+args+'{'+body+'}\n'
(ROOT/'port/engine/peripherals.c').write_text(text)
