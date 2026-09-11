"""Exercise both eye views while recycling native GPU resources mid-frame."""
import argparse,json,subprocess,sys
from gameplay_test import ROOT,exchange
from texture_visibility_test import read
from profile_switch import set_word
from combat_test import act

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--phases',default='command,stream,texture');args=ap.parse_args()
    flags=('mp_test_stereo_slider','command_byte_budget','stream_vertex_budget','texture_budget')
    names=('command_barriers','stream_barriers','texture_barriers','engine_frames','engine_failed','stereo_active')
    original=read(flags);records=[]
    original_geometry=int.from_bytes(read(('geometry_validate',))['geometry_validate'].to_bytes(4,'little'),'big')
    try:
        set_word('mp_test_stereo_slider',1000);act(0,10)
        for name,limit,counter in (('command_byte_budget',128*1024,'command_barriers'),('stream_vertex_budget',512,'stream_barriers'),('texture_budget',128*1024,'texture_barriers')):
            if name.split('_')[0] not in args.phases.split(','):continue
            for flag,value in original.items():
                if flag!='mp_test_stereo_slider':set_word(flag,value)
            set_word(name,limit);before=read(names)
            # FD's cached meshes may stream fewer than 512 vertices. Request
            # the existing fresh-decode oracle so this phase actually reuses
            # the streaming arena, without altering any game content.
            set_word('geometry_validate',1 if name=='stream_vertex_budget' else original_geometry,'big')
            if name=='texture_budget':
                # A fully cached FD creates no uploads and cannot exercise an
                # allocation-time budget. Load Stadium through normal input;
                # its live screens then keep creating textures during stereo.
                from stage_sweep import exit_training
                exit_training()
                subprocess.run([sys.executable,str(ROOT/'tools/select_test_stage.py'),
                                '18','--character','10','--cpu','16'],check=True)
                before=read(names)
            for button in (0x400,0x200,0x100,0x200,0x400,0x100):act(button,3);act(0,57)
            after=read(names);assert not after['engine_failed'] and after['stereo_active'],after
            assert after[counter]>before[counter],(name,before,after)
            row={'limit':name,'value':limit,'before':before,'after':after,'state':exchange()};records.append(row);print(json.dumps(row),flush=True)
    finally:
        set_word('geometry_validate',original_geometry,'big')
        for name,value in original.items():set_word(name,value)
        (ROOT/'build/stereo-pressure-test.json').write_text(json.dumps(records,indent=2)+'\n')

if __name__=='__main__':main()
