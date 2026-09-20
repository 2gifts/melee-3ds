"""Paired stereo checks for actual shader branches and clean uniform dispatch.

Requires a paused encounter reached through controller input. Timings are
Azahar CPU phases, not physical New 3DS performance guarantees.
"""
import argparse
import json,hashlib
import socket
import time
import numpy as np
from PIL import Image
import bottom_screen_test as bottom
import select_test_stage as select
from gameplay_test import ROOT,TEST_ELF,symbols,packet,receive
from menu_display_pause_test import clock_state
from profile_switch import set_word
from profile_render_detail import snapshot,summarize
from efb_copy_test import SD


def counters():
    with socket.create_connection(('127.0.0.1',24689),3) as s:
        packet(s,'?');receive(s)
        try:
            result={}
            for name,n in [('uniform_dispatch_calls',2),('uniform_dispatch_skips',2),
                           ('uniform_dispatch_checks',1),('shader_boolean_routes',3),('shader_shortcut_vertices',3),
                           ('uniform_scope_checks',1),('uniform_scope_writes',1)]:
                packet(s,f'm{symbols[name]:x},{n*4:x}')
                raw=bytes.fromhex(receive(s))
                result[name]=[int.from_bytes(raw[i:i+4],'little') for i in range(0,n*4,4)]
            for name in ('gpu_projection_cache_hits','gpu_projection_cache_misses','gpu_projection_cache_checks',
                         'gpu_light_cache_hits','gpu_light_cache_misses','gpu_light_cache_checks',
                         'gpu_clamped_draws','gpu_clamped_vertices',
                         'shade_clamped_hits','shade_clamped_vertices','shade_clamped_checks','shade_checks','shade_hits','shade_misses'):
                if name in symbols:
                    packet(s,f'm{symbols[name]:x},4')
                    result[name]=[int.from_bytes(bytes.fromhex(receive(s)),'big')]
            for name,n in [('lighting_uniform_checks',1),('lighting_uniform_draws',2),('affine_identity_draws',2),('affine_identity_vertices',2),('unit_attenuation_vertices',1),('unit_attenuation_lights',1)]:
                if name in symbols:
                    packet(s,f'm{symbols[name]:x},{n*4:x}');raw=bytes.fromhex(receive(s))
                    result[name]=[int.from_bytes(raw[i:i+4],'little') for i in range(0,n*4,4)]
            for name in ('unlit_affine_draws','unlit_affine_vertices'):
                if name in symbols:
                    packet(s,f'm{symbols[name]:x},8');raw=bytes.fromhex(receive(s))
                    result[name]=[int.from_bytes(raw[i:i+4],'little') for i in (0,4)]
            for name,n in [('stereo_reuse_draws',2),('stereo_reuse_vertices',2),('stereo_reuse_checks',1),('stereo_reuse_check_ticks',1)]:
                if name in symbols:
                    packet(s,f'm{symbols[name]:x},{n*4:x}');raw=bytes.fromhex(receive(s))
                    result[name]=[int.from_bytes(raw[i:i+4],'little') for i in range(0,n*4,4)]
            for name in ('mp_early_queue_starts','mp_early_queue_finishes','mp_early_queue_deferrals','mp_early_queue_retirements'):
                if name in symbols:
                    packet(s,f'm{symbols[name]:x},4');result[name]=[int.from_bytes(bytes.fromhex(receive(s)),'little')]
            for name in ('mp_render_worker_jobs','mp_render_worker_fallbacks','mp_render_worker_failures','mp_render_worker_snapshots'):
                if name in symbols:
                    packet(s,f'm{symbols[name]:x},4');result[name]=[int.from_bytes(bytes.fromhex(receive(s)),'little')]
            return result
        finally:packet(s,'c');packet(s,'D');receive(s)


def main():
    ap=argparse.ArgumentParser()
    ap.add_argument('--flags',default='uniform_dispatch_disable,shader_shortcuts_disable,uniform_scope_disable')
    ap.add_argument('--stream-queue',action='store_true',help='Keep update 18 command streaming enabled in both comparison modes')
    ap.add_argument('--async-presentation',action='store_true',help='Keep update 19 deferred presentation enabled in both modes; requires --stream-queue')
    ap.add_argument('--output',default='build/update16-qa');args=ap.parse_args()
    flags=args.flags.split(',')
    endian={name:'little' for name in ('uniform_dispatch_disable','shader_shortcuts_disable','uniform_scope_disable')}
    if 'gpu_projection_cache_disable' in symbols:endian['gpu_projection_cache_disable']='big'
    if 'gpu_light_cache_disable' in symbols:endian['gpu_light_cache_disable']='big'
    if 'shade_clamped_disable' in symbols:endian['shade_clamped_disable']='big'
    if 'lighting_uniform_disable' in symbols:endian['lighting_uniform_disable']='little'
    if 'stereo_reuse_disable' in symbols:endian['stereo_reuse_disable']='little'
    if 'stereo_reuse_bounds_reference' in symbols:endian['stereo_reuse_bounds_reference']='little'
    if 'stereo_reuse_shared_disable' in symbols:endian['stereo_reuse_shared_disable']='little'
    if 'gpu_early_queue_disable' in symbols:endian['gpu_early_queue_disable']='little'
    if 'gpu_stream_queue_disable' in symbols:endian['gpu_stream_queue_disable']='little'
    if 'gpu_async_present_disable' in symbols:endian['gpu_async_present_disable']='little'
    if 'unit_attenuation_disable' in symbols:endian['unit_attenuation_disable']='little'
    if 'unlit_affine_disable' in symbols:endian['unlit_affine_disable']='little'
    if 'mp_render_worker_disable' in symbols:endian['mp_render_worker_disable']='little'
    allowed=set(endian)
    assert flags and set(flags)<=allowed
    assert not args.stream_queue or not set(flags)&{'gpu_early_queue_disable','gpu_stream_queue_disable'},'Cannot hold a compared flag constant'
    assert not args.async_presentation or (args.stream_queue and 'gpu_async_present_disable' not in flags),'Cannot hold a compared flag constant'
    bottom.OUT=ROOT/args.output;bottom.OUT.mkdir(parents=True,exist_ok=True)
    target=bottom.OUT/'render-ab.json';target.unlink(missing_ok=True)
    initial=bottom.snapshot()
    assert initial['scene']==2 and clock_state()['pause_flags'],initial
    frozen=clock_state()['match_frame'];profiles=[];images={}
    set_word('mp_test_stereo_slider',1000)
    for flag in allowed:set_word(flag,0,endian[flag])
    if 'gpu_early_queue_disable' in allowed and 'gpu_early_queue_disable' not in flags:set_word('gpu_early_queue_disable',int('gpu_stream_queue_disable' not in flags))
    if 'gpu_stream_queue_disable' in allowed and 'gpu_stream_queue_disable' not in flags:set_word('gpu_stream_queue_disable',1)
    if args.stream_queue:
        set_word('gpu_early_queue_bytes',16384)
        set_word('gpu_early_queue_disable',0);set_word('gpu_stream_queue_disable',0)
    if 'gpu_light_cache_disable' in allowed and 'gpu_light_cache_disable' not in flags:set_word('gpu_light_cache_disable',1,'big')
    if 'gpu_async_present_disable' in allowed and 'gpu_async_present_disable' not in flags:set_word('gpu_async_present_disable',int(not args.async_presentation))
    if 'stereo_reuse_disable' in allowed and not set(flags)&{'stereo_reuse_disable','stereo_reuse_bounds_reference','stereo_reuse_shared_disable'}:
        set_word('stereo_reuse_disable',1)
    if 'gpu_projection_cache_validate' in symbols:set_word('gpu_projection_cache_validate',0,'big')
    if 'gpu_light_cache_validate' in symbols:set_word('gpu_light_cache_validate',0,'big')
    if 'shade_clamped_validate' in symbols:set_word('shade_clamped_validate',0,'big')
    try:
        for iteration in range(3):
            for reference in (1,0):
                for flag in flags:set_word(flag,reference,endian[flag])
                select.act(frames=45)
                before=counters();start=snapshot(True);time.sleep(3);end=snapshot(False);after=counters()
                assert clock_state()['match_frame']==frozen,'Comparison changed the original match state'
                delta={k:[(a-b)&0xffffffff for a,b in zip(after[k],before[k])] for k in before}
                if 'lighting_uniform_disable' in flags:
                    lit=delta['lighting_uniform_draws'];assert lit[reference]>100 and lit[1-reference]==0,lit
                if 'stereo_reuse_disable' in flags:
                    draws=delta['stereo_reuse_draws'];assert draws[0]>100,draws
                    assert draws[1]==0 if reference else draws[1]>100,draws
                if 'gpu_early_queue_disable' in flags:
                    starts=delta['mp_early_queue_starts'][0]
                    assert starts==0 if reference else starts>10,delta
                if 'gpu_light_cache_disable' in flags:
                    hits=delta['gpu_light_cache_hits'][0];misses=delta['gpu_light_cache_misses'][0]
                    assert hits==misses==0 if reference else hits>1000,delta
                if 'gpu_async_present_disable' in flags:
                    count=delta['mp_early_queue_deferrals'][0]
                    assert count==0 if reference else count>10,delta
                if 'shade_clamped_disable' in flags:
                    count=delta['shade_clamped_vertices'][0]
                    assert count==0 if reference else count>1000,delta
                if 'unit_attenuation_disable' in flags:
                    count=delta['unit_attenuation_vertices'][0]
                    assert count==0 if reference else count>1000,delta
                if 'mp_render_worker_disable' in flags:
                    count=delta['mp_render_worker_jobs'][0]
                    assert count==0 if reference else count>1000,delta
                    assert delta['mp_render_worker_failures']==[0],delta
                assert delta['uniform_dispatch_calls'][0]>100
                if reference and 'shader_shortcuts_disable' in flags:
                    assert delta['shader_boolean_routes'][:2]==[0,0],delta
                else:
                    assert sum(delta['shader_boolean_routes'][:2])>0,delta
                    assert sum(delta['uniform_dispatch_skips'])>100,delta
                profile=dict(reference=bool(reference),iteration=iteration,counters=delta,**summarize(start,end))
                profiles.append(profile)
                (bottom.OUT/'render-ab-progress.json').write_text(json.dumps(profiles,indent=2)+'\n')
                print(json.dumps({'iteration':iteration,'reference':bool(reference),
                    'renders':profile['rendered_frames'],'routes':delta['shader_boolean_routes'],
                    'uniform_checks':delta['uniform_scope_checks'][0],
                    'uniform_writes':delta['uniform_scope_writes'][0],
                    'projection_hits':delta.get('gpu_projection_cache_hits',[0])[0],
                    'projection_misses':delta.get('gpu_projection_cache_misses',[0])[0],
                    'shader_setup_ms':profile['phases']['shader_setup']['estimated_ms_per_render'],
                    'uniform_compare_ms':profile['phases']['uniform_compare']['estimated_ms_per_render'],
                    'gpu_commands_ms':profile['phases']['gpu_commands']['estimated_ms_per_render']}),flush=True)
                if iteration==0:
                    label='reference' if reference else 'optimized'
                    bottom.capture('uniform-'+label)
                    images[reference]=[np.array(Image.open(bottom.OUT/('uniform-'+label+'-top.png'))),np.fromfile(SD/'engine-right.bgr',dtype=np.uint8).copy()]
                    assert images[reference][0].shape==(240,400,3)
                    assert images[reference][1].shape==(400*240*3,)
                    for eye in images[reference]:
                        assert np.count_nonzero(eye)>5000,'Blank eye capture'
                    right=np.rot90(images[reference][1].reshape(400,240,3)[:,:,::-1])
                    Image.fromarray(right).save(bottom.OUT/('uniform-'+label+'-right.png'))
                    assert np.count_nonzero(images[reference][0]!=right)>200,'Both eye captures are identical'
        differences=[int(np.count_nonzero(a!=b)) for a,b in zip(images[0],images[1])]
        if differences!=[0,0]:
            target.write_text(json.dumps(dict(passed=False,flags=flags,initial=initial,profiles=profiles,
                different_channels=differences,elf_sha256=hashlib.sha256(TEST_ELF.read_bytes()).hexdigest(),
                physical_fps_verified=False,failure='Stereo pixel differences'),indent=2)+'\n')
        assert differences==[0,0],('Stereo pixel differences',differences)
        before=counters();set_word('uniform_dispatch_validate',1)
        if 'gpu_projection_cache_validate' in symbols:set_word('gpu_projection_cache_validate',1,'big')
        if 'gpu_light_cache_disable' in flags:set_word('gpu_light_cache_validate',1,'big')
        if 'shade_clamped_disable' in flags:
            set_word('shade_clamped_validate',1,'big');set_word('shade_validate',1,'big')
        select.act(frames=90);after=counters()
        checks=after['uniform_dispatch_checks'][0]-before['uniform_dispatch_checks'][0]
        assert checks>1000 and not bottom.snapshot()['engine_failed']
        projection_checks=after.get('gpu_projection_cache_checks',[0])[0]-before.get('gpu_projection_cache_checks',[0])[0]
        light_checks=after.get('gpu_light_cache_checks',[0])[0]-before.get('gpu_light_cache_checks',[0])[0]
        if 'gpu_light_cache_disable' in flags:assert light_checks>1000
        if 'gpu_projection_cache_disable' in flags:
            assert projection_checks>1000
            for profile in profiles:
                hits=profile['counters']['gpu_projection_cache_hits'][0]
                misses=profile['counters']['gpu_projection_cache_misses'][0]
                assert misses>0 and (hits==0 if profile['reference'] else hits>1000)
        clamped_checks=after.get('shade_clamped_checks',[0])[0]-before.get('shade_clamped_checks',[0])[0]
        material_checks=after.get('shade_checks',[0])[0]-before.get('shade_checks',[0])[0]
        if 'shade_clamped_disable' in flags:assert clamped_checks>1000 and material_checks>1000
        result=dict(passed=True,flags=flags,initial=initial,profiles=profiles,different_channels=differences,
                    elf_sha256=hashlib.sha256(TEST_ELF.read_bytes()).hexdigest(),
                    empty_sdk_upload_checks=checks,projection_checks=projection_checks,light_checks=light_checks,
                    clamped_checks=clamped_checks,material_cache_checks=material_checks,
                    stream_queue_held_enabled=args.stream_queue,physical_fps_verified=False,
                    scope='fixed paused encounter, full-resolution stereo; original match frame unchanged')
        target.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result),flush=True)
    finally:
        set_word('mp_gx_profile',0,'big')
        for flag in allowed|{'uniform_dispatch_validate'}:set_word(flag,1 if flag in ('stereo_reuse_disable','gpu_early_queue_disable','gpu_stream_queue_disable','gpu_light_cache_disable','gpu_async_present_disable') else 0,endian.get(flag,'little'))
        if 'gpu_projection_cache_validate' in symbols:set_word('gpu_projection_cache_validate',0,'big')
        if 'gpu_light_cache_validate' in symbols:set_word('gpu_light_cache_validate',0,'big')
        if 'shade_clamped_validate' in symbols:set_word('shade_clamped_validate',0,'big')
        if 'shade_clamped_disable' in flags:set_word('shade_validate',0,'big')


if __name__=='__main__':main()
