"""Evaluate stereo eligibility on captured game geometry using the C helper."""
import argparse, ctypes as ct, hashlib, json, struct, subprocess
from pathlib import Path
import numpy as np
from build import ROOT,local_clang


def library(out):
    source=out/'bounds-wrapper.c'
    source.write_text('#include "stereo_bounds.h"\n'
        '__declspec(dllexport) int inside(const MPGPUVertex*v,unsigned n,const MPGPUUniforms*u,float scale,float bias,float limit){\n'
        'MPStereoBounds b;mp_stereo_bounds_build(&b,v,n);return mp_stereo_bounds_inside(&b,u->value,u->value+60,scale,bias,limit);}\n')
    dll=out/'bounds.dll'
    subprocess.run([str(local_clang()),'-O2','-shared','-Wall','-Wextra','-Werror','-I'+str(ROOT/'port/3ds'),str(source),'-o',str(dll)],check=True)
    lib=ct.CDLL(str(dll.resolve()));lib.inside.argtypes=[ct.c_void_p,ct.c_uint,ct.c_void_p,ct.c_float,ct.c_float,ct.c_float];lib.inside.restype=ct.c_int
    return lib


def main():
    ap=argparse.ArgumentParser();ap.add_argument('capture',type=Path);args=ap.parse_args()
    out=args.capture/'analysis';out.mkdir(exist_ok=True);lib=library(out)
    manifest=json.loads((args.capture/'manifest.json').read_text());records=[]
    for entry in manifest['batches']:
        def blob(key):
            r=entry[key];raw=(args.capture/r['file']).read_bytes();assert hashlib.sha256(raw).hexdigest()==r['sha256'];return raw
        d=struct.unpack('>34I',blob('draw'));vertices=np.frombuffer(blob('vertices'),dtype='>f4').astype('<f4').reshape(-1,14)
        # convert_vertices rotates only CPU clip-space marker vertices.
        cpu=vertices[:,13]<0;oldx=vertices[cpu,0].copy();vertices[cpu,0]=vertices[cpu,1];vertices[cpu,1]=-oldx
        kind='cpu'
        uniform=np.zeros(386,dtype='<f4')
        if 'uniforms' in entry:
            raw=blob('uniforms');uniform=np.frombuffer(raw,dtype='>u4').astype('<u4').view('<f4')
            enabled=uniform[91*4]!=0 or uniform[92*4]!=0
            constant=int.from_bytes(raw[1540:1544],'big')!=0
            kind='flat' if constant else 'lit' if enabled else 'unlit'
        convergence=struct.unpack('>f',blob('draw')[132:136])[0]
        width=320 if d[31]==320 else 400;scale=12/width if convergence>0 else 0;bias=-scale*convergence
        is_inside=bool(lib.inside(vertices.ctypes.data,len(vertices),uniform.ctypes.data,scale,bias,.75 if width==320 else 1))
        # Restrict only the first experiment's supported render-state layout.
        # Unsupported paths retain the original renderer; never drop a draw.
        full_scissor=d[24:28]==(0,0,640,480)
        supported=kind=='lit' and bool(d[23]) and not d[28] and not d[32] and full_scissor
        if is_inside:
            # Independent float64 per-vertex check against actual homogeneous
            # clip positions, without using the bounding-box arithmetic.
            u=uniform[:380].reshape(95,4).astype(np.float64);p=vertices[:,:4].astype(np.float64);clip=np.zeros_like(p)
            for i,v in enumerate(vertices):
                if v[13]<0:clip[i]=p[i]
                else:
                    row=int(v[13]);world=np.r_[u[row:row+3]@p[i],1.];clip[i]=u[60:64]@world
            for eye in (-1,1):
                y=clip[:,1]+eye*(scale*clip[:,3]+bias);w=clip[:,3]
                assert np.all((w>0)&(abs(clip[:,0])<w)&(clip[:,2]<0)&(clip[:,2]>-w)&(abs(y)<(.75 if width==320 else 1)*w)),entry['index']
        records.append(dict(index=entry['index'],vertices=len(vertices),indices=d[22],kind=kind,inside=is_inside,supported=supported,
            eligible=is_inside and supported,geometry_id=d[23],points=bool(d[28]),layer=bool(d[32]),full_scissor=full_scissor))
    total=sum(r['vertices'] for r in records);eligible=sum(r['vertices'] for r in records if r['eligible'])
    result=dict(batches=len(records),vertices=total,eligible_vertices=eligible,eligible_vertex_fraction=eligible/total,
        eligible_batches=sum(r['eligible'] for r in records),inside_batches=sum(r['inside'] for r in records),
        by_kind={kind:dict(batches=sum(r['kind']==kind for r in records),vertices=sum(r['vertices'] for r in records if r['kind']==kind)) for kind in ('cpu','flat','unlit','lit')},
        records=records,physical_fps_verified=False,
        bounds_source_sha256=hashlib.sha256((ROOT/'port/3ds/stereo_bounds.h').read_bytes()).hexdigest())
    (out/'result.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps({k:v for k,v in result.items() if k!='records'},indent=2))


if __name__=='__main__':main()
