"""Sample separate CPU rendering phases once per sixteen calls."""
import argparse,json,socket,struct,time
from gameplay_test import ROOT,symbols,packet,receive

def snapshot(enabled):
    with socket.create_connection(('127.0.0.1',24689),3) as sock:
        sock.settimeout(5);packet(sock,'?');receive(sock)
        def read(name,n):packet(sock,f'm{symbols[name]:x},{n:x}');return bytes.fromhex(receive(sock))
        def write(name,raw):packet(sock,f'M{symbols[name]:x},{len(raw):x}:'+raw.hex());assert receive(sock)=='OK'
        try:
            write('mp_gx_profile',struct.pack('>I',int(enabled)))
            result={'frame':int.from_bytes(read('engine_frames',4),'little'),'failed':int.from_bytes(read('engine_failed',4),'little')}
            route=read('state_machine',6);result['scene']=[route[0],route[3]]
            result['simulation']=int.from_bytes(read('gm_80479D58',4),'big')
            result['retrace_ticks']=int.from_bytes(read('last_retrace',4),'big')
            result['source_checks']={name:int.from_bytes(read(name,4),'big') for name in
                ('geometry_source_compares','geometry_source_reuses','geometry_source_bytes','geometry_range_skips',
                 'geometry_hits','geometry_misses') if name in symbols}
            result['memory']={name:int.from_bytes(read(name,4),endian) for name,endian in
                [('geometry_bytes','big'),('geometry_budget','big'),('native_geometry_bytes','little'),
                 ('texture_bytes','little'),('__ctru_heap_size','little'),('mp_native_heap_used','little'),
                 ('mp_native_heap_available','little')] if name in symbols}
            if 'native_geometry_work' in symbols:
                result['native_work']=dict(zip(('requests','uncacheable_vertices','misses','cache_converted_vertices',
                    'full_cache_fallbacks','capacity_evictions','eviction_slots_scanned','streamed_vertices'),
                    struct.unpack('<8I',read('native_geometry_work',32))))
            if 'texture_hash_checks' in symbols:
                result['texture_work']={name:int.from_bytes(read(name,8 if name in ('texture_hash_bytes','framebuffer_cpu_pixels') else 4),'little') for name in
                    ('texture_hash_checks','texture_hash_bytes','texture_dirty_calls','texture_dirty_invalidated',
                     'framebuffer_cpu_calls','framebuffer_cpu_pixels','texture_uploads','texture_evictions')}
            for prefix,n,endian in [('mp_gx',3,'>'),('mp_native',4,'<')]:
                for suffix in ('ticks','count'):
                    name=prefix+'_detail_'+suffix
                    result[name]=struct.unpack(endian+str(n)+'I',read(name,n*4))
            return result
        finally:packet(sock,'c');packet(sock,'D');receive(sock)

def summarize(start,end):
    frames=end['frame']-start['frame'];assert frames>0 and not end['failed'],end
    phases={}
    for prefix,names in [('mp_gx',['material','shader_setup','geometry_lookup']),('mp_native',['texture_lookup','uniform_compare','native_geometry','gpu_commands'])]:
        for i,name in enumerate(names):
            ticks=(end[prefix+'_detail_ticks'][i]-start[prefix+'_detail_ticks'][i])&0xffffffff
            count=(end[prefix+'_detail_count'][i]-start[prefix+'_detail_count'][i])&0xffffffff
            phases[name]={'samples':count,'mean_call_us':ticks/40.5/count if count else 0,'estimated_ms_per_render':ticks*16/40500/frames}
    seconds=((end['retrace_ticks']-start['retrace_ticks'])&0xffffffff)/40500000
    source={name:((end['source_checks'][name]-value)&0xffffffff)/frames for name,value in start.get('source_checks',{}).items()}
    native={name:((end['native_work'][name]-value)&0xffffffff)/frames for name,value in start.get('native_work',{}).items()}
    texture={name:((end['texture_work'][name]-value)&((1<<64)-1 if name in ('texture_hash_bytes','framebuffer_cpu_pixels') else 0xffffffff))/frames for name,value in start.get('texture_work',{}).items()}
    return {'rendered_frames':frames,'simulation_updates':end['simulation']-start['simulation'],
            'start_scene':start['scene'],'end_scene':end['scene'],'same_scene':start['scene']==end['scene'],
            'emulated_seconds':seconds,'rates':{'render':frames/seconds,'simulation':(end['simulation']-start['simulation'])/seconds},
            'source_checks_per_render':source,'native_geometry_work_per_render':native,
            'texture_work_per_render':texture,
            'final_memory':end.get('memory',{}),
            'sampling':'one in sixteen calls; CPU phases only','phases':phases}

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--seconds',type=int,default=10);args=ap.parse_args()
    start=snapshot(True)
    try:time.sleep(args.seconds)
    finally:end=snapshot(False)
    result=summarize(start,end)
    (ROOT/'build/render-detail-profile.json').write_text(json.dumps(result,indent=2));print(json.dumps(result,indent=2))
if __name__=='__main__':main()
