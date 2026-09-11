"""Prepare conservative optional stage visuals from locally patched disc data."""
import copy,hashlib,json,struct
from audit_diet_stages import StageArchive
from assets import ROOT

PINS={
 'GrNBa.dat':('5b914b8f28c0dc16b1bafad68704e743d495ead41f0b954cca84479d6868a3b7','932220536b6377d48f8e46c7808f7792ebd9585b0dd0ea7ab60899f3452d1f87',8),
 'GrNLa.dat':('2a295a733414fb65a7061a5c092144fb197570682562b87ba5567ff3f08abb02','d602274022139aa24fd0abb0803cbd100a152daf7f3c066756455d1ab3802568',16),
 'GrSt.dat':('1ef0ccc51fc69bf2e06f55377111ec1b67e00438df0597bf6195c3032ae29f83','8b4b45c26d8abee807a4b24a05bb1a9409d0bb397d869aa672370ae1875ba349',36),
 'GrOp.dat':('44ef32a76216c3f47b79c430167e915953c3da0664f121c270be11fd5af2574d','8321b0835f468ed9ce2488505f075b60b9695479e56dee948b0d9366c7f56dcc',52)}

def prepare():
    output=ROOT/'build/visuals';output.mkdir(exist_ok=True)
    reports=[]
    for name,(original_hash,diet_hash,size) in PINS.items():
        original=StageArchive(ROOT/'assets/GALE01/files'/name)
        diet=StageArchive(ROOT/'references/diet-melee/files'/name)
        assert hashlib.sha256(original.raw).hexdigest()==original_hash
        assert hashlib.sha256(diet.raw).hexdigest()==diet_hash
        raw=bytearray(diet.raw)
        if name=='GrNLa.dat':
            from restore_stage_animations import restore_fd
            raw=restore_fd(original,diet)
        if name=='GrSt.dat':
            # Diet changed music and stage selection settings. Preserve every
            # original byte in this non-relocated, eight-row StageParams table.
            a=original.word(original.roots['grGroundParam']+176)
            b=diet.word(diet.roots['grGroundParam']+176)
            count=original.word(original.roots['grGroundParam']+180)
            assert count==diet.word(diet.roots['grGroundParam']+180)==8
            for archive,p in ((original,a),(diet,b)):
                start=32+int.from_bytes(archive.raw[4:8],'big')
                n=int.from_bytes(archive.raw[8:12],'big')
                assert all(not p<=struct.unpack_from('>I',archive.raw,start+i*4)[0]<p+count*100 for i in range(n))
            raw[32+b:32+b+count*100]=original.raw[32+a:32+a+count*100]
        dest=output/name;dest.write_bytes(raw)
        prepared=StageArchive(dest);a=original.gameplay(size);b=prepared.gameplay(size)
        for key in a:
            if key!='models':assert a[key]==b[key],(name,key)
        assert len(a['models'])==len(b['models'])
        exceptions=[]
        if name=='GrSt.dat':
            assert len(a['models'])==4
            for i in (0,2):assert a['models'][i]==b['models'][i],i
            for i in (1,3):
                assert a['models'][i]['animations']==b['models'][i]['animations']
                assert a['models'][i]['bindings']==b['models'][i]['bindings']==''
            # Model 1 only animates scenery (grStory_801E31C0). Model 2 is
            # Randall: its entire skeleton, spline and animations match above.
            x,y=[copy.deepcopy(v['models'][3]['joints']) for v in (a,b)]
            for joint in (x,y):
                node=joint['child']
                for _ in range(4):node=node['next']
                node['next']=None
            assert x==y,'Shy Guy interactive skeleton changed'
            exceptions=['Background scenery transformed/simplified','Decorative siblings after first five Shy Guy joints removed']
        elif name=='GrNBa.dat':
            assert len(a['models'])==7
            for i in (0,2,3,4,5):assert a['models'][i]==b['models'][i],i
            assert a['models'][1]['joints']==b['models'][1]['joints']
            assert len(a['models'][1]['animations'])==len(b['models'][1]['animations'])==1
            assert a['models'][1]['bindings']==b['models'][1]['bindings']==''
            x,y=[copy.deepcopy(v['models'][6]) for v in (a,b)]
            assert y['joints']['child'] is None
            x['joints']['child']=None;assert x==y
            exceptions=['Model 1 scenery animation simplified; its simulation callback is empty','Model 6 decorative children removed; static collision and bindings retained']
        elif name=='GrNLa.dat':
            from restore_stage_animations import audit_fd_wait_controllers
            assert audit_fd_wait_controllers(original,prepared)==66
            for i in [0,*range(2,10)]:assert a['models'][i]==b['models'][i],i
            assert a['models'][1]['bindings']==b['models'][1]['bindings']==''
            exceptions=['Model 1 scenery simplified; its simulation callback is empty','Original joint animations restored for every other model; 66 animation-wait timing sources match','Decorative material animations omitted']
        else:assert a['models']==b['models'],'Dream Land skeleton or animation changed'
        reports.append({'file':name,'bytes':len(raw),'sha256':hashlib.sha256(raw).hexdigest(),
            'original_sha256':original_hash,'diet_input_sha256':diet_hash,
            'collision_ground_settings_gameplay_parameters_equal':True,
            'interactive_skeletons_splines_animations_equal':True,
            'original_stage_params_restored':name=='GrSt.dat','visual_exceptions':exceptions})
    (ROOT/'build/diet-prepared-audit.json').write_text(json.dumps(reports,indent=2)+'\n')
    return reports
if __name__=='__main__':print(json.dumps(prepare(),indent=2))
