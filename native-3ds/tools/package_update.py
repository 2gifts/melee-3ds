"""Verify and zip a replacement executable without recopying game assets."""
import argparse,datetime,hashlib,json,shutil,subprocess,tempfile,zipfile
from pathlib import Path
from package import ROOT,validate_3dsx

def main():
    ap=argparse.ArgumentParser();ap.add_argument('directory',type=Path)
    ap.add_argument('--label',default='renderer optimization 1')
    ap.add_argument('--changes',default='Indexed texture lookup and fewer redundant GPU state uploads.')
    ap.add_argument('--fountain-visuals',action='store_true',help='Include the locally patched and audited Diet Fountain visual archive')
    ap.add_argument('--diet-stages',action='store_true',help='Include every prepared/audited stage plus Fountain')
    args=ap.parse_args()
    directory=args.directory if args.directory.is_absolute() else ROOT/args.directory
    binary=directory/'3ds/melee/melee.3dsx';info=validate_3dsx(binary)
    elf=ROOT/'build/game-release/melee.elf'
    with tempfile.TemporaryDirectory(dir=ROOT/'build',prefix='verify-update-') as temporary:
        repacked=Path(temporary)/'melee.3dsx'
        subprocess.run([str(ROOT/'.toolchain/bin/3dsxtool.exe'),str(elf),str(repacked)],check=True)
        assert repacked.read_bytes()==binary.read_bytes(),'Release ELF does not match the packaged executable'
    symbol_text=subprocess.check_output([str(ROOT/'.toolchain/llvm-mingw-20260908-ucrt-x86_64/bin/llvm-nm.exe'),str(elf)],text=True)
    labels={line.split()[-1] for line in symbol_text.splitlines() if line.split()}
    forbidden={'geometry_planes','mp_bounds_outside_cached','clamped_verify','verify_clamped_paths','audio_decoder_validate','gpu_clamped_disable','geometry_range_dirty_disable','geometry_planes_disable','geometry_planes_validate','geometry_early_disable','geometry_early_validate','audio_decoder_disable','mp_probe_request','mp_probe_mode','mp_probe_rows','shade_program_disable',
               'mp_test_control','mp_test_capture','mp_test_frame_limit','audio_snapshot_pcm','audio_snapshot_request',
               'mp_test_bottom_touch','mp_test_bottom_disabled','mp_bottom_observed','mp_bottom_fps_visible','mp_bottom_guide_visible',
               'mp_test_stereo_slider','stereo_verify','verify_stereo','stereo_order_reference',
               'stereo_eye_switches','stereo_pairs','stereo_attribute_writes',
               'mp_collision_failed_joints','mp_collision_failed_addresses','mp_collision_failed_joints_count',
               'mp_test_async_files','mp_native_async_self_test','mp_async_file_checks',
               'mp_file_trace','mp_test_disc_delay_ms','mp_async_read_disable',
               'framebuffer_range_disable','texture_content_validate','texture_content_checks','texture_hash_checks',
               'texture_dirty_calls','texture_dirty_invalidated','texture_hash_bytes','framebuffer_cpu_calls','framebuffer_cpu_pixels',
               'texture_upload_ticks','texture_upload_max_ticks','efb_rgb565_disable','efb_rgb565_validate','efb_rgb565_checks','efb_rgb565_pixels','efb_rgb565_ticks',
               'texture_lookup_disable','texture_lookup_validate','texture_slot_budget','stream_vertex_budget',
               'texture_repack_disable','texture_repack_validate','texture_repack_checks',
               'texture_repack_format_checks','texture_repack_fast_ticks','texture_repack_reference_ticks',
               'native_geometry_work','native_geometry_linear_scan','native_geometry_lru_validate',
               'native_geometry_lru_checks','native_geometry_check_lru','native_geometry_validate',
               'native_geometry_checks','native_geometry_check',
               'layered_draws','layered_verify','verify_layered_material','fixture_rgba8',
               'shield_draws','shield_verify','verify_shield_material',
               'raster_state_disable','raster_state_verify','verify_raster_state',
               'mp_test_stall','mp_test_memory_compare','mp_game_memory_check','mp_memory_compare_self_test',
               'mp_test_rotation_check','mp_game_rotation_check','mp_rotation_self_test',
               'mp_test_ppc_math_check','mp_game_ppc_math_check','mp_ppc_math_self_test','mp_ppc_vectors','mp_position_probe',
               'texture_bind_disable','texture_bind_hits','texture_bind_updates',
               'draw_packet_disable','draw_packet_validate','draw_packet_checks','draw_packet_draws',
               'gpu_full_heap_flush','gpu_vblank_wait','command_byte_budget',
               'gpu_pipeline_disable','shader_shortcuts_disable','palette_upload_disable',
               'gpu_early_queue_disable','gpu_early_queue_bytes','gpu_stream_queue_disable','affine_identity_disable','affine_verify',
               'shader_verify','verify_shader_paths',
               'uniform_scope_disable','uniform_scope_draws','uniform_scope_checks','uniform_scope_writes',
               'uniform_dispatch_disable','uniform_dispatch_validate','uniform_dispatch_checks',
               'shader_boolean_checks','shader_boolean_routes','audio_flush_disable',
               'audio_service_flush','audio_flush_calls','audio_flush_ticks','audio_flush_last_result',
               'mp_render_bench_request','mp_renderer_benchmark_frame','mp_render_command_bytes',
               'stereo_reuse_disable','stereo_reuse_bounds_reference','stereo_reuse_shared_disable','stereo_reuse_parameters_reference','stereo_reuse_draws','stereo_reuse_vertices','stereo_reuse_checks','mp_stereo_reuse_shader',
               'lighting_uniform_disable','lighting_uniform_checks','lighting_uniform_draws','lighting_verify','lighting_verify_page',
               'mp_light_reference_base','mp_light_reference_dual','mp_light_reference_stereo','mp_light_reference_dual_stereo',
               'gpu_projection_cache_disable','gpu_projection_cache_validate',
               'gpu_light_cache_disable','gpu_light_cache_validate',
               'gpu_async_present_disable','unlit_affine_disable','unlit_affine_draws','unlit_affine_verify',
               'efb_rgb5a3_disable','efb_rgb5a3_validate','efb_rgb5a3_checks','efb_rgb5a3_pixels','efb_rgb5a3_ticks',
               'efb_discard_disable','efb_discard_verify','verify_efb_discard','discarded_image','discarded_image_bytes',
               'unit_attenuation_disable','lighting_unit_verify',
               'shade_clamped_disable','shade_clamped_validate','shade_clamped_checks',
               'mp_render_worker_disable','mp_render_worker_test_failure','mp_render_worker_async',
               'mp_render_worker_source_cache_disable','mp_render_worker_source_cache_validate','mp_render_worker_geometry_borrow',
               'results_capture_disable','results_capture_verify','verify_results_capture','capture_fixture_quad',
               '__ubsan_handle_type_mismatch_v1'}
    assert not labels&forbidden,sorted(labels&forbidden)
    from test_be8_image import check
    image_check=check(elf.with_name('melee-linked.elf'),elf,binary)
    visuals=[];extra_files=[]
    if args.fountain_visuals or args.diet_stages:
        from audit_diet_fountain import main as audit
        audit()
        source=ROOT/'references/diet-melee/files/GrIz.dat'
        digest=hashlib.sha256(source.read_bytes()).hexdigest()
        assert digest=='913134d58c804f44b9fe3dc41b981e50a076e6b27c3c58583fa74095ce22f61d'
        dest=directory/'3ds/melee/visuals/GrIz.dat';dest.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(source,dest)
        visuals=[{'path':'3ds/melee/visuals/GrIz.dat','bytes':source.stat().st_size,'sha256':digest,
                  'source':'Diet Melee Classic Linux patcher 1.0.2 applied locally to the user-owned US 1.02 ISO',
                  'source_url':'https://diet.melee.tv/download/','original_disc_files_modified':False}]
        extra_files.append(dest)
    if args.diet_stages:
        from prepare_diet_stages import prepare
        for stage in prepare():
            source=ROOT/'build/visuals'/stage['file'];dest=directory/'3ds/melee/visuals'/stage['file']
            shutil.copyfile(source,dest);extra_files.append(dest)
            visuals.append({'path':dest.relative_to(directory).as_posix(),'bytes':stage['bytes'],'sha256':stage['sha256'],
                'source':'Diet Melee Classic 1.0.2 patched locally; native-port corrections preserve original stage settings/control data',
                'source_url':'https://diet.melee.tv/download/','original_disc_files_modified':False,'audit':stage})
    for source,name in [(ROOT/'docs/THIRD_PARTY_NOTICES.md','THIRD_PARTY_NOTICES.md'),
                        (ROOT/'references/slippi-ssbm-asm/LICENSE','SLIPPI-LICENSE.txt'),
                        (ROOT/'port/3ds/vendor/CITRO3D-LICENSE.txt','CITRO3D-LICENSE.txt'),
                        (ROOT/'port/engine/vendor/DOLPHIN-LICENSE.txt','DOLPHIN-LICENSE.txt')]:
        dest=directory/name;shutil.copyfile(source,dest);extra_files.append(dest)
    manifest=json.loads((ROOT/'assets/GALE01/manifest.json').read_text())
    report={'built_utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'binary':info,
            'label':args.label,'game_id':manifest['game_id'],'main_dol_sha1':manifest['main_dol_sha1'],
            'upstream_commit':json.loads((ROOT/'upstream.lock.json').read_text())['commit'],
            'hardware_target':['New Nintendo 3DS','New Nintendo 3DS XL','New Nintendo 2DS XL'],
            'asset_files_included':len(visuals),'optional_visual_assets':visuals,'uses_existing_assets':'/3ds/melee/files',
            'physical_hardware_verified':False,'smoke_controls_and_fixtures_absent':True,'image_validation':image_check}
    (directory/'build.json').write_text(json.dumps(report,indent=2)+'\n')
    (directory/'README.txt').write_text(f'''Melee native New 3DS alpha - {args.label}

This is an update to the native-alpha package.

When ready, copy 3ds/melee/melee.3dsx to the same path on the SD card,
replacing the old executable. Keep the existing /3ds/melee/files folder.
Keep a backup of the previously installed executable for rollback.
Launch melee.3dsx through Homebrew Launcher as before.

Changes: {args.changes}
Original disc files and save files remain unchanged.
Controls and the original Training/Versus menu paths are unchanged.
The target includes the smaller New 3DS and New 3DS XL.
Physical console speed and audio still need your hardware results.
The port remains unfinished; saving and multiplayer are unavailable.

The first line of /3ds/melee/game.log identifies this update.
''')
    if 'mp_native_bottom_frame' in labels:
        with (directory/'README.txt').open('a') as readme:
            readme.write('''
The bottom screen shows the live roster, damage and stocks for up to four
fighters, with matching menu guidance and a completed-match summary.
Tap FPS OFF at the lower left to show FPS; tap again to hide it.
Tap VIEW to switch between 4:3 and expanded gameplay and menu models.
START pauses Versus by default; More Rules can disable it.
Tap CONTROLS for the guide, then CLOSE GUIDE to return. The guide does not
pause the match. FPS starts hidden each launch; game logs stay on the SD.
''')
    if 'mp_game_set_stereo' in labels:
        with (directory/'README.txt').open('a') as readme:
            if 'mp_display_ribbon_callback' in labels:
                readme.write('''
Raise the 3D slider for stereo gameplay and menu models; fully down selects 2D.
Maximum depth is 50% stronger than update 8. The CSS Ready to Fight ribbon
sits in front of the screen; ordinary HUD/text overlays remain flat.
The default remains centered 4:3. Hold ZL + ZR and press SELECT to toggle
expanded gameplay and menu models without stretching. Stereo adds GPU drawing work; use 2D
for the highest available frame rate. Stable physical 60 FPS remains open.
''')
            else:
                readme.write('''
Raise the 3D slider for stereoscopic gameplay; fully down selects 2D.
Menus and HUD remain flat. The default remains centered 4:3.
Hold ZL + ZR and press SELECT to toggle expanded gameplay without stretching.
Stereo adds GPU drawing work; use 2D for the highest available frame rate.
Stable physical 60 FPS and physical stereo presentation remain unverified.
''')
    if args.diet_stages:
        with (directory/'README.txt').open('a') as readme:
            readme.write('''
Optional scenery covers Fountain of Dreams, Yoshi's Story, Battlefield,
Final Destination and Dream Land. Copy the complete visuals directory.
Original stage files remain under files/. Renaming an optional visual file
while the app is closed restores that stage's original scenery.
All stages use the shared off-screen rejection and reduced shadow work.
The other stages remain selectable and keep their original archives.

All characters/stages and More Rules unlock on launch. Versus defaults to
4 stocks, 8 minutes, items off, team attack on and pause on. Random uses
the six singles stages. Rules can be changed in the menus for this session.
UCF 0.84's native input fixes are enabled. There is no online mode.
No-card prompts are skipped; memory-card persistence remains unavailable.

Credits: Diet Melee https://diet.melee.tv/
UCF by Altimor, with the Slippi 0.84 integration as reference:
https://github.com/AltimorTASDK/ucf
https://github.com/project-slippi/slippi-ssbm-asm
''')
    elif visuals:
        with (directory/'README.txt').open('a') as readme:
            readme.write('''
This local package also includes /3ds/melee/visuals/GrIz.dat: simplified
Fountain scenery from Diet Melee Classic, patched from your own ISO.
Copy the visuals folder along with the executable. To restore the original
Fountain scenery, rename or remove this optional GrIz.dat file while the
game is closed. Leave /3ds/melee/files/GrIz.dat intact.
The other Diet stage files and its modified game executable are not used.
Credits and upstream patch: https://diet.melee.tv/
''')
    archive=directory.with_suffix('.zip')
    files=[directory/'README.txt',directory/'build.json',binary,*extra_files]
    with zipfile.ZipFile(archive,'w',compression=zipfile.ZIP_DEFLATED) as out:
        for path in files:out.write(path,path.relative_to(directory).as_posix())
    with zipfile.ZipFile(archive) as check:
        assert check.testzip() is None
        assert check.read('3ds/melee/melee.3dsx')==binary.read_bytes()
        for path in extra_files:assert check.read(path.relative_to(directory).as_posix())==path.read_bytes()
    print(json.dumps({'archive':str(archive),'binary':info,'physical_hardware_verified':False},indent=2))
if __name__=='__main__':main()
