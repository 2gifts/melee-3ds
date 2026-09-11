"""Four fighters, all items and full stereo through original menu input.

Run on a fresh development boot. Menu, fighter and item state is read only;
only the native controller and renderer validation controls are written.
"""
import argparse,json,sys,time
import select_test_stage as select
import bottom_screen_test as bottom
import match_fix_menu_test as rules
from menu_refinement_test import enter
from profile_switch import set_word
from damage_source_test import observe as objects


def press(button):
    select.act(button,2);select.act(frames=18)


def main():
    ap=argparse.ArgumentParser();ap.add_argument('--seconds',type=int,default=60)
    ap.add_argument('--stage-kind',type=int,default=14,help='StKind (selected-stage numbering), 14 is Temple')
    ap.add_argument('--label',default='casual');ap.add_argument('--output',default='build/update15-qa')
    ap.add_argument('--pause-after',action='store_true');args=ap.parse_args()
    bottom.OUT=bottom.ROOT/args.output;bottom.OUT.mkdir(parents=True,exist_ok=True)
    select.observe((0,1,0,0));set_word('mp_test_stereo_slider',1000)
    for _ in range(90):
        state=select.observe()
        if 'menu' in state:break
        select.act(0x100 if bottom.snapshot()['scene']==42 else 0x1000,2)
        select.act(frames=25)
    else:raise AssertionError('Main menu did not open')
    enter(1,2);rules.select(2,3);rules.select(13,5)
    assert rules.detail()['hovered']==0,rules.detail()
    press(8)
    for _ in range(6):
        if rules.detail()['frequency']==5:break
        press(1)
    assert rules.detail()['frequency']==5,rules.detail()
    press(4)
    for expected in range(16):
        state=rules.detail();assert state['hovered']==expected,state
        if not state['items'][expected]:press(0x100)
        if expected!=15:press(4)
    press(2)
    for expected in range(30,15,-1):
        state=rules.detail();assert state['hovered']==expected,state
        if not state['items'][expected]:press(0x100)
        if expected!=16:press(8)
    settings=rules.detail();assert all(settings['items']) and settings['frequency']==5,settings
    print(json.dumps({'all_items_enabled':settings}),flush=True)
    bottom.capture('casual-all-items')
    press(0x200);select.act(frames=40);press(0x200);select.act(frames=40)
    state=select.open_versus(select.observe())
    for slot,icon in enumerate((1,6,16,23)):state=bottom.choose(state,slot,icon)
    bottom.capture('casual-css')
    select.act(0x1000,4);state=select.act(frames=50)
    assert 'icons' in state,state
    index=next(row['id'] for row in state['icons'] if row['stage_kind']==args.stage_kind)
    old=sys.argv;sys.argv=['select_test_stage.py',str(index)]
    try:select.main()
    finally:sys.argv=old
    select.act(frames=160);bottom.capture(args.label+'-match')
    set_word('mp_rotation_cache_validate',1,'big')
    set_word('draw_packet_validate',1)
    rows=[];deadline=time.monotonic()+args.seconds
    while time.monotonic()<deadline:
        state=bottom.snapshot();row=objects()
        assert state['scene']==2 and not state['engine_failed'],state
        assert len(row['fighters'])==4,row
        rows.append(row);time.sleep(.2)
    assert any(row['items'] for row in rows),'No items appeared'
    assert rows[-1]['simulation']>rows[0]['simulation']+600,rows[-1]
    summary={'passed':True,'stage_kind':args.stage_kind,'settings':settings,
        'initial':rows[0],'final':rows[-1],
        'peak_items':max(len(row['items']) for row in rows),
        'item_kinds':sorted({item['kind'] for row in rows for item in row['items']}),
        'simulation_frames':rows[-1]['simulation']-rows[0]['simulation'],
        'input_only':True,'stereo_slider':1000}
    (bottom.OUT/(args.label+'-scenario.json')).write_text(json.dumps(summary,indent=2)+'\n')
    (bottom.OUT/(args.label+'-objects.json')).write_text(json.dumps(rows,indent=2)+'\n')
    print(json.dumps(summary),flush=True)
    set_word('mp_rotation_cache_validate',0,'big');set_word('draw_packet_validate',0)
    if args.pause_after:
        from menu_display_pause_test import clock_state
        assert bottom.snapshot()['players'][0]['stocks']>0,'Human player has no stocks to pause'
        for _ in range(5):
            if clock_state()['pause_flags']:break
            select.act(0x1000,3);select.act(frames=20)
        assert clock_state()['pause_flags']


if __name__=='__main__':main()
