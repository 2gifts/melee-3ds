"""Navigate original menu input, including every Special Melee option.

Run on a development build at the main menu. No game state is injected;
the only writes are controller input and native display test controls.
"""
import json,time
import select_test_stage as select
import bottom_screen_test as bottom
from profile_switch import set_word

bottom.OUT=bottom.ROOT/'build/update12-qa'

def idle(kind=None):
    for _ in range(60):
        s=select.observe()
        if 'menu' in s and s['menu']['state']==0:
            if kind is not None:assert s['menu']['kind']==kind,s
            return s
        select.act(frames=12)
    raise AssertionError(('Menu did not settle',s))

def hover(index):
    s=idle()
    for _ in range(24):
        if s['menu']['selection']==index:return s
        select.act(4 if s['menu']['selection']<index else 8,2)
        select.act(frames=16);s=idle()
    raise AssertionError(('Selection did not settle',index,s))

def enter(index,kind):
    hover(index);select.act(0x100,3);select.act(frames=60);return idle(kind)

def back(kind):
    select.act(0x200,3);select.act(frames=60);return idle(kind)

def sweep(kind,indices,label):
    idle(kind)
    for index in indices:
        hover(index);select.act(frames=36)
    bottom.capture(label)

def main():
    set_word('mp_test_frame_limit',0);select.observe((0,1,0,0))
    idle(0)
    # Main/1P/VS have five entries, Options six, Special Melee ten.
    # Repeated entry and exit catches scratch writes across scene transitions.
    for round in range(3):
        enter(0,1);sweep(1,[0,1,3,4,0],f'1p-{round}')
        enter(0,6);sweep(6,[0,1,2],f'regular-{round}');back(1)
        enter(3,9);sweep(9,[0,1,2],f'stadium-{round}');back(1);back(0)
        enter(1,2);enter(2,12);sweep(12,list(range(10))+[0],f'special-{round}');back(2);back(0)
        enter(3,4);sweep(4,[0,1,2,4,5,0],f'options-{round}');back(0)
        enter(4,5);sweep(5,[0,1,2,3,4,0],f'data-{round}');back(0)
    bottom.touch(160,225,'mp_native_expanded',0);select.act(frames=5);bottom.capture('main-final-4x3')
    bottom.touch(160,225,'mp_native_expanded',1);select.act(frames=5);bottom.capture('main-final-wide')
    (bottom.OUT/'menu-sweep.json').write_text(json.dumps({'passed':True,'rounds':3,'menus':[0,1,2,4,5,6,9,12],'special_options':10},indent=2))
    print('Menu traversal passed: three rounds, all ten Special Melee entries, both display widths.',flush=True)

if __name__=='__main__':main()
