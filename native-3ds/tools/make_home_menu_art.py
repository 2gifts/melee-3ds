"""Assemble a private disc icon and a real, animated HOME Menu diorama.

Game imagery and geometry is extracted locally from the user's disc. The
optional disc-label image is supplied locally and is never redistributed.
The CGFX conversion is performed by the separately acquired pycgfx tool.
"""
import argparse,hashlib,json,math,struct,sys
from pathlib import Path
import numpy as np
from PIL import Image,ImageFilter
from banner_assets import ROOT,OUT as ASSETS
from banner_capture_read import read_capture

OUT=ROOT/'build/home-menu/art'

def logo_and_icon(disc_image):
    logo=Image.new('RGBA',(512,256))
    top=Image.open(ASSETS/'title-0023b0.png').resize((432,128),Image.Resampling.LANCZOS)
    lower=Image.open(ASSETS/'title-0028b0.png').resize((300,61),Image.Resampling.LANCZOS)
    logo.alpha_composite(top,(40,18));logo.alpha_composite(lower,(106,150))
    alpha=logo.getchannel('A');outline=alpha.filter(ImageFilter.MaxFilter(9))
    shadow=Image.new('RGBA',logo.size,(17,14,22,255));shadow.putalpha(outline)
    shadow.alpha_composite(logo);shadow.save(OUT/'logo.png')
    im=Image.open(disc_image).convert('RGBA')
    assert min(im.size)>=256 and abs(im.width-im.height)<=2, 'Supply a square disc image, at least 256px'
    # SMDH is RGB565 and has no alpha. Composite before downsampling so the
    # transparent hub/corners cannot turn into an opaque black square.
    icon=Image.new('RGBA',(1024,1024),(238,240,244,255))
    icon.alpha_composite(im.resize((1000,1000),Image.Resampling.LANCZOS),(12,12))
    icon.convert('RGB').save(OUT/'disc-icon-large.png')
    icon.convert('RGB').resize((48,48),Image.Resampling.LANCZOS).save(OUT/'icon.png')
    (OUT/'icon-source.json').write_text(json.dumps(dict(source_sha256=hashlib.sha256(disc_image.read_bytes()).hexdigest(),source_size=im.size,output_size=[48,48],alpha_background=[238,240,244]),indent=2)+'\n')

class Scene:
    def __init__(self):
        self.blob=bytearray();self.views=[];self.accessors=[];self.nodes=[dict(name='Melee diorama',children=[])];self.meshes=[];self.skins=[];self.materials=[];self.images=[];self.textures=[];self.animations=[];self.texture_ids={}
    def data(self,a,kind,component=5126):
        a=np.asarray(a,dtype={5126:'<f4',5123:'<u2',5121:'u1'}[component]);self.blob.extend(b'\0'*((-len(self.blob))%4));off=len(self.blob);self.blob.extend(a.tobytes())
        view=len(self.views);self.views.append(dict(buffer=0,byteOffset=off,byteLength=a.nbytes))
        entry=dict(bufferView=view,componentType=component,count=len(a),type=kind)
        if kind=='VEC3' and len(a):entry.update(min=a.min(0).tolist(),max=a.max(0).tolist())
        self.accessors.append(entry);return len(self.accessors)-1
    def material(self,image=None,name='Vertex color',alpha=False):
        key=name
        if key in self.texture_ids:return self.texture_ids[key]
        mat=dict(name=name,pbrMetallicRoughness=dict(metallicFactor=0,roughnessFactor=1),doubleSided=False)
        if image is not None:
            im=image.copy();im.thumbnail((32,32),Image.Resampling.LANCZOS)
            w=1<<(max(8,im.width)-1).bit_length();h=1<<(max(8,im.height)-1).bit_length()
            if name=='Logo':im=image.resize((256,128),Image.Resampling.LANCZOS)
            else:im=im.resize((w,h),Image.Resampling.LANCZOS)
            filename=f'texture-{len(self.images):02d}.png';im.save(OUT/filename)
            self.images.append(dict(uri=filename));self.textures.append(dict(source=len(self.images)-1,sampler=0))
            mat['pbrMetallicRoughness']['baseColorTexture']=dict(index=len(self.textures)-1)
        if alpha:mat.update(alphaMode='MASK',alphaCutoff=.3)
        index=len(self.materials);self.materials.append(mat);self.texture_ids[key]=index;return index
    def mesh(self,name,pos,norm,uv,color,ix,material,joints=None,weights=None,skin=None,node=None):
        # Collapse repeated GX vertices before encoding seven attribute streams.
        arrays=[np.asarray(x) for x in (pos,norm,uv,color)]
        if joints is not None:arrays += [np.asarray(joints),np.asarray(weights)]
        _,unique,remap=np.unique(np.concatenate(arrays,axis=1),axis=0,return_index=True,return_inverse=True)
        pos,norm,uv,color=[x[unique] for x in arrays[:4]];ix=remap[np.asarray(ix)]
        if joints is not None:joints,weights=[x[unique] for x in arrays[4:]]
        # Match the float RGB vertex streams of the working SM64 banner.
        # Transparency belongs to the texture/material, not a vertex alpha.
        assert np.allclose(color[:,3],1), 'Vertex alpha needs explicit material handling'
        attr=dict(POSITION=self.data(pos,'VEC3'),NORMAL=self.data(norm,'VEC3'),TEXCOORD_0=self.data(uv,'VEC2'),COLOR_0=self.data(color[:,:3],'VEC3'))
        if joints is not None:attr.update(JOINTS_0=self.data(joints,'VEC4',5121),WEIGHTS_0=self.data(weights,'VEC4'))
        primitive=dict(attributes=attr,indices=self.data(ix,'SCALAR',5123),material=material,mode=4)
        if node is not None:
            self.meshes[self.nodes[node]['mesh']]['primitives'].append(primitive)
            return node
        mesh=len(self.meshes);self.meshes.append(dict(name=name,primitives=[primitive]))
        node=len(self.nodes);self.nodes.append(dict(name=name,mesh=mesh))
        if skin is not None:self.nodes[node]['skin']=skin
        self.nodes[0]['children'].append(node);return node
    def animation(self,node,times,transforms):
        trs=[decompose(m) for m in transforms];samplers=[];channels=[];t=self.data(times,'SCALAR')
        for i,(name,kind) in enumerate([('translation','VEC3'),('rotation','VEC4'),('scale','VEC3')]):
            values=np.array([a[i] for a in trs])
            if name=='rotation':
                for j in range(1,len(values)):
                    if values[j]@values[j-1]<0:values[j]*=-1
            # Emit complete TRS tracks, including static scale/rotation, like
            # the hardware-tested animated reference banner.
            channels.append(dict(sampler=len(samplers),target=dict(node=node,path=name)))
            samplers.append(dict(input=t,output=self.data(values,kind),interpolation='LINEAR'))
        if channels:self.animations.append(dict(name=f'Pose {node}',samplers=samplers,channels=channels))
    def save(self):
        camera=len(self.nodes);self.nodes.append(dict(name='Banner Camera',camera=0,translation=[0,1,44.786]))
        children={c for n in self.nodes for c in n.get('children',[])}
        scene=dict(asset=dict(version='2.0',generator='Melee local HOME Menu authoring'),scene=0,scenes=[dict(nodes=[i for i in range(len(self.nodes)) if i not in children])],nodes=self.nodes,meshes=self.meshes,materials=self.materials,images=self.images,textures=self.textures,samplers=[dict(magFilter=9729,minFilter=9729,wrapS=33071,wrapT=33071)],buffers=[dict(uri='scene.bin',byteLength=len(self.blob))],bufferViews=self.views,accessors=self.accessors,animations=self.animations,cameras=[dict(name='Banner Camera',type='perspective',perspective=dict(aspectRatio=5/3,yfov=math.pi/6,znear=26.5,zfar=1000))])
        assert not self.skins, 'HOME banners must use rigid mesh-node animation'
        (OUT/'scene.bin').write_bytes(self.blob);(OUT/'scene.gltf').write_text(json.dumps(scene,separators=(',',':'))+'\n')

def decompose(m):
    scale=np.linalg.norm(m[:3,:3],axis=0);r=m[:3,:3]/scale
    u,_,vt=np.linalg.svd(r);r=u@vt
    if np.linalg.det(r)<0:r[:,2]*=-1;scale[2]*=-1
    q=np.zeros(4);trace=np.trace(r)
    if trace>0:
        v=math.sqrt(trace+1)*2;q[3]=v/4;q[:3]=[(r[2,1]-r[1,2])/v,(r[0,2]-r[2,0])/v,(r[1,0]-r[0,1])/v]
    else:
        i=int(np.argmax(np.diag(r)));j=(i+1)%3;k=(i+2)%3;v=math.sqrt(max(0,1+r[i,i]-r[j,j]-r[k,k]))*2
        q[i]=v/4;q[j]=(r[j,i]+r[i,j])/v;q[k]=(r[k,i]+r[i,k])/v;q[3]=(r[k,j]-r[j,k])/v
    return m[:3,3].tolist(),q.tolist(),scale.tolist()

def matrix(d,row=0):
    m=np.eye(4);m[:3]=d['u'][row:row+3];return m

def mesh_key(d):
    # Native geometry IDs can change after visibility/animation invalidation.
    return hashlib.sha256(d['v'][:,[0,1,2,3,8,9,13]].tobytes()+d['indices'].tobytes()).hexdigest()

def build_scene():
    captures=[read_capture(ROOT/f'build/home-menu/capture/banner-{i:04d}.bin') for i in range(45)]
    # Use the original platform as a stable camera reference. No screen-space
    # vertices or HUD are included in the HOME Menu model.
    stages=[];groups=[];reference=[];image_groups=None
    for draws,textures in captures:
        valid=[d for d in draws if np.isfinite(d['positions']).all() and -500<d['positions'][:,2].min() and d['positions'][:,2].max()<-30 and np.ptp(d['positions'][:,2])>.02]
        first=next(i for i,d in enumerate(valid) if np.ptp(d['positions'][:,0])<18 and np.ptp(d['positions'][:,2])<18 and d['positions'][:,1].max()>-15)
        stage=valid[:first];assert len(stage)>=8
        f=[[],[]];current=0;last_x=None
        for d in valid[first:]:
            if image_groups is not None:
                for player,addresses in enumerate(image_groups):
                    if d['d']['image'] in addresses:f[player].append(d)
            else:
                center=d['positions'].mean(0)
                if np.ptp(d['positions'][:,0])>20:continue
                if last_x is not None and abs(center[0]-last_x)>22:current+=1
                if current>1:break
                f[current].append(d);last_x=center[0]
        assert min(map(len,f))>=20,[len(x) for x in f]
        if image_groups is None:
            image_groups=[{d['d']['image'] for d in p} for p in f]
            assert not image_groups[0]&image_groups[1]
        stages.append(stage);groups.append(f);reference.append(matrix(stage[0]))
    s=Scene();stage0=stages[0];base=reference[0]
    allp=np.concatenate([d['positions'] for d in stage0]);center=(allp.min(0)+allp.max(0))/2
    ground=np.percentile(allp[:,1],95)
    # Stage below the logo, with room for stereoscopic depth and spinning.
    root=np.eye(4);root[:3,:3]*=.10;root[:3,3]=[-center[0]*.10,-1.8-ground*.10,-center[2]*.10]
    s.nodes[0]['matrix']=root.T.ravel().tolist()
    metrics=dict(stage_draws=len(stage0),fox_draws=[len(g) for g in groups[0]],animation_samples=0)
    def material(d,textures):
        im=textures.get(d['texture']);key=hashlib.sha256(im.tobytes()).hexdigest()[:12] if im else 'stage-color'
        return s.material(im,key,im is not None and im.getextrema()[3][0]<200)
    def color(d):
        if d['flat']:return np.tile(np.clip(d['u'][84],0,1),(len(d['v']),1))
        c=d['v'][:,4:8].copy();m=d['material'];c=np.where(m>=0,m,c)
        # Texture materials get restrained ambient/directional illumination
        # from the HOME Menu, rather than baking a view-dependent highlight.
        return np.clip(c,0,1)
    stage_node=None
    for i,d in enumerate(stage0):
        p=d['positions'];norm=d['v'][:,10:13]@np.linalg.inv(matrix(d)[:3,:3]);norm/=np.maximum(1e-6,np.linalg.norm(norm,axis=1))[:,None]
        mi=material(d,captures[0][1]);s.materials[mi]['doubleSided']=True
        # The gameplay diet material omits GX multipass surface effects.
        # Give the original platform a compact purple-metal banner finish.
        tint=([.32,.24,.78,1] if i==0 else [.10,.07,.20,1] if i==1 else [.22,.16,.34,1]) if not d['texture'] else [.45,.44,.95,1]
        stage_node=s.mesh('Final Destination',p,norm,d['v'][:,8:10],np.tile(tint,(len(p),1)),d['indices'],mi,node=stage_node)
    # Fill the original top surface beneath its animated GX overlay. The
    # game renders this base through its CPU path, outside the GPU export.
    d=stage0[-1];mi=s.material(name='Platform surface');s.materials[mi]['doubleSided']=True
    p=d['positions'].copy();p[:,1]-=.05
    s.mesh('Final Destination',p,[[0,1,0]]*len(p),d['v'][:,8:10],np.tile([.075,.025,.16,1],(len(p),1)),d['indices'],mi,node=stage_node)
    # Bake complete captured poses, then animate each material part with the
    # same rigid transform. Physical HOME Menu cannot safely animate soft
    # skins. Keeping the whole fighter rigid avoids cracks between envelopes.
    times=[0,.35,.65,.85,1.15,1.6,1.95,2.25,2.5,2.85,3.2]
    for player in range(2):
        frame=16 if player==0 else 24
        normalize=base@np.linalg.inv(reference[frame])
        parts=[d for d in groups[frame][player] if d['d']['geometry_id']]
        pos=np.concatenate([(normalize@np.c_[d['positions'],np.ones(len(d['v']))].T).T[:,:3] for d in parts])
        foot=np.array([np.median(pos[:,0]),pos[:,1].min(),np.median(pos[:,2])])
        desired=np.array([center[0]+(-19 if player==0 else 19),ground,center[2]+22])
        # A short lunge, evade and counterattack loop; original triangle/UV
        # data is retained. This animation belongs only to the HOME diorama.
        motion=([0,0,9,13,4,0,-3,0,3,0,0] if player==0 else [0,0,0,5,7,0,-9,-13,-4,0,0])
        hops=([0,0,1,0,0,0,4,2,0,0,0] if player==0 else [0,0,0,3,4,0,1,0,0,0,0])
        transforms=[]
        for dx,dy in zip(motion,hops):
            transform=np.eye(4);transform[:3,3]=desired+[dx,dy,0]
            transforms.append(transform)
        # One transform/animation per fighter, regardless of material count.
        # Creating a bone for each draw needlessly balloons HOME's matrices.
        node=None
        for j,d in enumerate(parts):
            p=(normalize@np.c_[d['positions'],np.ones(len(d['v']))].T).T[:,:3]
            p=(p-foot)*2.3
            norm=np.empty((len(p),3))
            for row in np.unique(d['v'][:,13].astype(int)):
                mask=d['v'][:,13].astype(int)==row
                norm[mask]=d['v'][mask,10:13]@np.linalg.inv((normalize@matrix(d,int(row)))[:3,:3])
            norm/=np.maximum(1e-6,np.linalg.norm(norm,axis=1))[:,None]
            node=s.mesh(f'Fox {player+1}',p,norm,d['v'][:,8:10],color(d),d['indices'],material(d,captures[frame][1]),node=node)
        s.nodes[node]['translation']=desired.tolist()
        s.animation(node,times,transforms)
    # Logo sits above and slightly in front of the stage. Put it outside the
    # scaled diorama hierarchy so its original aspect ratio stays exact.
    im=Image.open(OUT/'logo.png');mi=s.material(im,'Logo',True)
    n=s.mesh('Melee logo',[[-11.5,1.5,2],[11.5,1.5,2],[11.5,13,2],[-11.5,13,2]],[[0,0,1]]*4,[[0,1],[1,1],[1,0],[0,0]],[[1,1,1,1]]*4,[0,1,2,0,2,3],mi)
    # Y-axis billboarding requires an identity logo node beside the world.
    s.nodes[0]['children'].remove(n)
    from home_banner_atlas import compact_scene
    metrics['atlas'] = compact_scene(s, OUT)
    metrics.update(nodes=len(s.nodes),materials=len(s.materials),skins=len(s.skins),animation_samples=len(times),triangles=sum(s.accessors[p['indices']]['count']//3 for m in s.meshes for p in m['primitives']))
    s.save();(OUT/'art-report.json').write_text(json.dumps(metrics,indent=2)+'\n');print(json.dumps(metrics))

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--disc-image',type=Path,required=True,help='Local square full-color disc artwork (not committed)')
    args=parser.parse_args()
    OUT.mkdir(parents=True,exist_ok=True);logo_and_icon(args.disc_image);build_scene()
