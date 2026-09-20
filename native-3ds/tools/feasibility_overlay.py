"""Instrumentation of selected sources in isolated feasibility builds only."""
from build import ROOT

def adapt(original,selected,out):
    if original.name not in ('gobj.c','gm_1A45.c','gx.c','audio.c'):return selected
    s=selected.read_text(encoding='utf-8')
    def once(old,new):
        nonlocal s
        assert s.count(old)==1,(original,old,s.count(old));s=s.replace(old,new)
    if original.name=='gobj.c':
        once('proc->on_invoke(proc->gobj);','mp_probe_proc(proc->gobj,(void(*)(void*))proc->on_invoke);')
        once('cur->render_cb(cur, i);','mp_probe_render(cur,i);')
        once('cur->render_cb(cur, 0);','mp_probe_render(cur,0);')
    elif original.name=='gm_1A45.c':
        once('    mp_bottom_scene_begin(info ? info->scene_kind : 255);','    mp_bottom_scene_begin(info ? info->scene_kind : 255);\n    mp_probe_scene(info ? info->scene_kind : 255);')
        once('            HSD_PerfSetStartTime();','            MP_PROBE_START(mp_sim_probe,4,gm_801A4D34,0);\n            HSD_PerfSetStartTime();')
        once('            HSD_PerfSetCPUTime();','            HSD_PerfSetCPUTime();\n            MP_PROBE_END(mp_sim_probe);')
        once('        GXInvalidateVtxCache();','        MP_PROBE_START(mp_render_probe,5,gm_801A4D34,0);\n        GXInvalidateVtxCache();')
        once('        HSD_PerfSetDrawTime();','        HSD_PerfSetDrawTime();\n        MP_PROBE_END(mp_render_probe);')
    elif original.name=='gx.c':
        once('gpu_vertices=flat_vertices=affine_vertices=slow_vertices=0;if(done_callback)',
             'mp_probe_checkpoint();gpu_vertices=flat_vertices=affine_vertices=slow_vertices=0;if(done_callback)')
        once('    GXTexObj*t=&textures[active_texture&7];draw.image=',
             '    if(mp_probe_drop(1))return;\n    GXTexObj*t=&textures[active_texture&7];draw.image=')
        once('    while(p<end){unsigned op=*p++;',
             '    if(mp_probe_drop(2)){flush();return;}\n    while(p<end){unsigned op=*p++;')
        once('        while(n--){vertex(p);p+=vertex_bytes;}remaining=0;',
             '        MP_PROBE_START(mp_decode_probe,3,vertex,(gpu_shading?1:0)|(layer_active?2:0));\n        while(n--){vertex(p);p+=vertex_bytes;}remaining=0;MP_PROBE_END(mp_decode_probe);')
        # Scoped wrappers preserve every original call and handle all returns.
        wrappers=[('void','GXCallDisplayList','void*list,u32 nbytes','list,nbytes'),
                  ('void','GXBegin','GXPrimitive type,GXVtxFmt fmt,u16 nverts','type,fmt,nverts'),
                  ('void','prepare_material','void',''),
                  ('int','prepare_gpu_shading','void',''),
                  ('GeometryCache*','geometry_find','const void*list,unsigned bytes,const GeometryKey*k','list,bytes,k')]
        import re
        for ret,name,params,args in wrappers:
            # Use original declaration's return type, whitespace and parameters.
            pattern=r'(?m)^(static )?([^\n{};]*?\b'+name+r')\(([^\n]*)\)\s*\{'
            matches=list(re.finditer(pattern,s));assert len(matches)==1,(name,len(matches))
            m=matches[0];decl=m.group(0)[:-1].strip();sig=decl.replace(name,name+'_probe_impl',1)
            actual_return=m.group(2).split(name)[0].strip()
            assert actual_return==ret,(name,actual_return,ret)
            body=f'MP_PROBE_START(t,3,{name},0);'
            body+=(f'{ret} result=' if ret!='void' else '')+f'{name}_probe_impl({args});MP_PROBE_END(t);'
            if ret!='void':body+='return result;'
            replacement=sig+';\n'+decl+'{'+body+'}\n'+sig+'{'
            s=s[:m.start()]+replacement+s[m.end():]
    elif original.name=='audio.c':
        once('u32 profile_start=mp_platform_ticks();','MP_PROBE_START(mp_audio_probe,6,mp_audio_poll_at,0);u32 profile_start=mp_platform_ticks();')
        once('mp_profile_audio+=mp_platform_ticks()-profile_start;','mp_profile_audio+=mp_platform_ticks()-profile_start;MP_PROBE_END(mp_audio_probe);')
    s='#include "'+str(ROOT/'port/engine/feasibility.h').replace('\\','/')+'"\n'+s
    path=out/'feasibility-overlays'/original.relative_to(ROOT);path.parent.mkdir(parents=True,exist_ok=True)
    path.write_text(s,encoding='utf-8');return path
