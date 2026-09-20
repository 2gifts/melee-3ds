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
from menu_display_pause_test import clock_state
from texture_visibility_test import read as native_counters


def press(button):
    select.act(button,2);select.act(frames=18)


def validate_match(args,settings):
    if args.menu_2d:
        set_word('mp_test_stereo_slider',1000);select.act(frames=3)
    initial=bottom.snapshot()
    assert initial['scene']==2 and initial['stage']==args.stage_kind,initial
    assert all(settings['items']) and len(settings['items'])==31 and settings['frequency']==5,settings
    if args.stereo_reuse:
        set_word('stereo_reuse_shared_disable',0);set_word('stereo_reuse_disable',0)
    if args.early_queue or args.stream_queue:
        set_word('stereo_reuse_disable',1)
        set_word('gpu_early_queue_bytes',16384);set_word('gpu_early_queue_disable',0)
    if args.stream_queue:set_word('gpu_stream_queue_disable',0)
    if args.async_presentation:set_word('gpu_async_present_disable',0)
    if args.reuse_with_stream_queue:
        set_word('stereo_reuse_shared_disable',0);set_word('stereo_reuse_disable',0)
    if args.clamped_shade:
        from render_update16_test import counters
        set_word('shade_clamped_disable',0,'big');set_word('shade_clamped_validate',1,'big')
        clamped_before=counters()
    if args.unit_attenuation:
        for name in ('unit_attenuation_disable','efb_rgb5a3_disable','efb_discard_disable'):
            set_word(name,0)
        unit_before=native_counters(('unit_attenuation_vertices','unit_attenuation_lights'))
    if args.unlit_affine:
        from render_update16_test import counters
        set_word('unlit_affine_disable',0)
        unlit_before=counters()['unlit_affine_vertices']
    if args.render_worker:
        if args.render_worker_borrow:
            assert native_counters(('mp_render_worker_geometry_contract',))['mp_render_worker_geometry_contract']==1
            set_word('mp_render_worker_geometry_borrow',1);set_word('native_geometry_validate',1)
        if args.render_worker_source_cache:
            set_word('mp_render_worker_source_cache_disable',0);set_word('mp_render_worker_source_cache_validate',1)
        if args.render_worker_async:set_word('mp_render_worker_async',1)
        set_word('mp_render_worker_disable',0)
        worker_before=native_counters(('mp_render_worker_jobs','mp_render_worker_failures'))
        if args.render_worker_async:
            worker_before.update(native_counters(('mp_render_worker_snapshots','mp_render_worker_snapshot_fallbacks','mp_render_worker_arena_flushes')))
        if args.render_worker_borrow:
            worker_before.update(native_counters(('mp_render_worker_borrowed_draws','mp_render_worker_geometry_retirements','mp_render_worker_geometry_busy_retirements','native_geometry_checks')))
    queue_before=native_counters(('mp_early_queue_deferrals','mp_early_queue_retirements')) if args.async_presentation else None
    if clock_state()['pause_flags']:
        select.act(0x1000,3);select.act(frames=20)
    assert not clock_state()['pause_flags']
    set_word('mp_rotation_cache_validate',1,'big')
    set_word('draw_packet_validate',1)
    rows=[];started=time.monotonic();deadline=started+max(180,args.seconds*8)
    summary_path=bottom.OUT/(args.label+'-scenario.json')
    summary_path.unlink(missing_ok=True)
    try:
        while True:
            row=objects(include_hitboxes=False)
            assert not row['failed'] and row['stage_id']==args.stage_kind,row
            assert len(row['fighters'])==4,row
            rows.append(row)
            elapsed=time.monotonic()-started
            updates=rows[-1]['simulation']-rows[0]['simulation']
            if elapsed>=args.seconds and updates>600:break
            if time.monotonic()>deadline:
                raise TimeoutError(f'Encounter advanced only {updates} simulation frames in {elapsed:.1f}s')
            # Linked-object snapshots suspend the guest. Leave it running
            # between samples; this is coverage, not an FPS benchmark.
            time.sleep(1)
        assert any(row['items'] for row in rows),'No items appeared'
        final=bottom.snapshot();assert final['scene']==2 and not final['engine_failed'],final
        summary={'passed':True,'stage_kind':args.stage_kind,'settings':settings,
            'initial':rows[0],'final':rows[-1],
            'peak_items':max(len(row['items']) for row in rows),
            'item_kinds':sorted({item['kind'] for row in rows for item in row['items']}),
            'simulation_frames':updates,'wall_seconds_including_debugger':elapsed,
            'input_only':True,'stereo_slider':1000,'physical_fps_verified':False,
            'experimental_shared_stereo_reuse':args.stereo_reuse or args.reuse_with_stream_queue,
            'experimental_early_queue':args.early_queue or args.stream_queue,
            'experimental_stream_queue':args.stream_queue,
            'experimental_async_presentation':args.async_presentation}
        if args.clamped_shade:
            from render_update16_test import counters
            summary['clamped_shade_counters']={k:v[0] for k,v in counters().items() if k.startswith('shade_clamped_')}
            clamped_after=counters()
            if 'gpu_clamped_vertices' in clamped_after:
                summary['gpu_clamped_delta']={k:clamped_after[k][0]-clamped_before[k][0]
                    for k in ('gpu_clamped_draws','gpu_clamped_vertices')}
                assert all(v>0 for v in summary['gpu_clamped_delta'].values()),summary
            else:
                assert summary['clamped_shade_counters']['shade_clamped_checks']>1000
        if args.async_presentation:
            queue_after=native_counters(tuple(queue_before))
            summary['async_queue_before']=queue_before;summary['async_queue_after']=queue_after
            assert all(queue_after[key]>value for key,value in queue_before.items())
        if args.unit_attenuation:
            unit_after=native_counters(tuple(unit_before))
            summary['unit_attenuation_delta']={k:unit_after[k]-v for k,v in unit_before.items()}
            assert summary['unit_attenuation_delta']['unit_attenuation_vertices']>10000
            summary['results_copy_optimizations_enabled']=True
        if args.unlit_affine:
            unlit_after=counters()['unlit_affine_vertices']
            summary['unlit_affine_vertices_delta']=[b-a for a,b in zip(unlit_before,unlit_after)]
            assert summary['unlit_affine_vertices_delta'][1]>1000,'Unlit shortcut was not exercised'
        if args.render_worker:
            worker_after=native_counters((*worker_before,'mp_render_worker_core','mp_render_worker_active'))
            from render_worker_evidence import processor_evidence
            summary['render_worker_processor']=processor_evidence(worker_after)
            assert worker_after['mp_render_worker_failures']==worker_before['mp_render_worker_failures']==0,worker_after
            assert worker_after['mp_render_worker_jobs']-worker_before['mp_render_worker_jobs']>1000,worker_after
            summary['render_worker_before']=worker_before;summary['render_worker_after']=worker_after
            summary['render_worker_synchronous_ownership_test']=not args.render_worker_async
            if args.render_worker_async:
                assert worker_after['mp_render_worker_snapshots']-worker_before['mp_render_worker_snapshots']>1000,worker_after
                summary['render_worker_peak']=native_counters(('mp_render_worker_queue_peak','mp_render_worker_arena_peak'))
            if args.render_worker_source_cache:
                cache=native_counters(('mp_render_worker_source_hits','mp_render_worker_source_checks','mp_render_worker_source_mismatches','mp_render_worker_source_bytes','mp_render_worker_source_evictions'))
                assert cache['mp_render_worker_source_checks']>1000 and cache['mp_render_worker_source_mismatches']==0,cache
                summary['render_worker_source_cache']=cache
            if args.render_worker_borrow:
                assert worker_after['mp_render_worker_borrowed_draws']-worker_before['mp_render_worker_borrowed_draws']>1000,worker_after
                assert worker_after['native_geometry_checks']-worker_before['native_geometry_checks']>1000,worker_after
                assert worker_after['mp_render_worker_geometry_retirements']>0,worker_after
        summary['menu_setup_stereo_slider']=0 if args.menu_2d else 1000
        summary_path.write_text(json.dumps(summary,indent=2)+'\n')
        print(json.dumps(summary),flush=True)
    finally:
        (bottom.OUT/(args.label+'-objects.json')).write_text(json.dumps(rows,indent=2)+'\n')
        set_word('mp_rotation_cache_validate',0,'big');set_word('draw_packet_validate',0)
        if args.clamped_shade:set_word('shade_clamped_validate',0,'big')
        if args.render_worker_source_cache:set_word('mp_render_worker_source_cache_validate',0)
        if args.render_worker_borrow:set_word('native_geometry_validate',0)
    if args.pause_after:
        assert bottom.snapshot()['players'][0]['stocks']>0,'Human player has no stocks to pause'
        for _ in range(5):
            if clock_state()['pause_flags']:break
            select.act(0x1000,3);select.act(frames=20)
        assert clock_state()['pause_flags']


def main():
    ap=argparse.ArgumentParser();ap.add_argument('--seconds',type=int,default=60)
    ap.add_argument('--stage-kind',type=int,default=14,help='StKind (selected-stage numbering), 14 is Temple')
    ap.add_argument('--label',default='casual');ap.add_argument('--output',default='build/update15-qa')
    ap.add_argument('--pause-after',action='store_true')
    ap.add_argument('--clamped-shade',action='store_true',help='Compare compiled CPU materials with the original evaluator on every affected vertex')
    ap.add_argument('--unit-attenuation',action='store_true',help='Enable exact constant lighting and results-copy optimizations')
    ap.add_argument('--unlit-affine',action='store_true',help='Exercise the development unlit color shortcut during gameplay')
    ap.add_argument('--render-worker',action='store_true',help='Enable the core-2 worker at the reached encounter; keep menu setup synchronous on main')
    ap.add_argument('--render-worker-async',action='store_true',help='Use immutable draw snapshots and allow producer/consumer overlap')
    ap.add_argument('--render-worker-source-cache',action='store_true',help='Validate all reused immutable source bytes against current engine memory')
    ap.add_argument('--render-worker-borrow',action='store_true',help='Exercise immutable engine geometry and validate native cached vertex/index bytes')
    ap.add_argument('--menu-2d',action='store_true',help='Use 2D during slow debugger-driven menu setup, then full 3D for the active test')
    ap.add_argument('--async-presentation',action='store_true',help='Exercise candidate publication and deferred final wait; requires --stream-queue')
    ap.add_argument('--reuse-with-stream-queue',action='store_true',help='Exercise the shared stereo shader together with --stream-queue')
    experiments=ap.add_mutually_exclusive_group()
    experiments.add_argument('--stereo-reuse',action='store_true',help='Exercise experimental shared stereo reuse during the active encounter')
    experiments.add_argument('--early-queue',action='store_true',help='Start a completed GPU command prefix during the active encounter')
    experiments.add_argument('--stream-queue',action='store_true',help='Append completed batches throughout each frame')
    ap.add_argument('--resume',action='store_true',help='Continue the already selected encounter; requires its saved menu settings')
    args=ap.parse_args()
    if args.async_presentation and not args.stream_queue:ap.error('--async-presentation requires --stream-queue')
    if args.reuse_with_stream_queue and not args.stream_queue:ap.error('--reuse-with-stream-queue requires --stream-queue')
    if args.render_worker_async and not args.render_worker:ap.error('--render-worker-async requires --render-worker')
    if args.render_worker_source_cache and not args.render_worker_async:ap.error('--render-worker-source-cache requires --render-worker-async')
    if args.render_worker_borrow and not args.render_worker_async:ap.error('--render-worker-borrow requires --render-worker-async')
    bottom.OUT=bottom.ROOT/args.output;bottom.OUT.mkdir(parents=True,exist_ok=True)
    select.observe((0,1,0,0));set_word('mp_test_stereo_slider',0 if args.menu_2d else 1000)
    if args.render_worker:
        if args.render_worker_borrow:
            assert native_counters(('mp_render_worker_geometry_contract',))['mp_render_worker_geometry_contract']==1
            set_word('mp_render_worker_geometry_borrow',1)
        if args.render_worker_source_cache:
            set_word('mp_render_worker_source_cache_disable',0);set_word('mp_render_worker_source_cache_validate',1)
        if args.render_worker_async:set_word('mp_render_worker_async',1)
        set_word('mp_render_worker_disable',int(not args.render_worker_async))
    settings_path=bottom.OUT/(args.label+'-settings.json')
    if args.resume:
        validate_match(args,json.loads(settings_path.read_text()))
        return
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
    settings_path.write_text(json.dumps(settings,indent=2)+'\n')
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
    validate_match(args,settings)


if __name__=='__main__':main()
