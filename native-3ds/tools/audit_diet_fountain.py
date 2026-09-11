"""Compare gameplay data and skeletons before adopting Diet's Fountain visuals.

Only consumes the local, already patched copy of the user's disc. Relocated
addresses are excluded; pointed-to contents and tree structure are compared.
"""
import copy,hashlib,json,struct
from pathlib import Path
from assets import ROOT,hsd_inventory,u32

class Archive:
    def __init__(self,path):
        self.raw=path.read_bytes();self.inv=hsd_inventory(self.raw)
        self.b=self.raw[32:32+u32(self.raw,4)]
        self.roots={r['name']:r['offset'] for r in self.inv['roots']}
    def word(self,o):return u32(self.b,o)
    def blob(self,o,n):
        assert o>=0 and n>=0 and o+n<=len(self.b),(o,n)
        return self.b[o:o+n].hex()
    def joint(self,o,ancestors=()):
        if not o:return None
        assert o not in ancestors
        # Render bits can differ, but topology and all transforms must match.
        return {'transform':list(struct.unpack('>9f',self.b[o+20:o+56])),'instance':bool(self.word(o+4)&0x1000),
                'matrix':self.blob(self.word(o+56),48) if self.word(o+56) else None,
                'robj':bool(self.word(o+60)),
                'child':self.joint(self.word(o+8),ancestors+(o,)),
                'next':self.joint(self.word(o+12),ancestors+(o,))}
    def fobj(self,o):
        if not o:return None
        return [self.blob(o+4,12),self.blob(self.word(o+16),self.word(o+4)),self.fobj(self.word(o))]
    def aobj(self,o):
        if not o:return None
        return [self.blob(o,8),self.word(o+12),self.fobj(self.word(o+8))]
    def anim(self,o):
        if not o:return None
        assert not self.word(o+12),'Uninspected RObj animation'
        return [self.word(o+16),self.aobj(self.word(o+8)),self.anim(self.word(o)),self.anim(self.word(o+4))]
    def animations(self,o):
        result=[]
        if o:
            while self.word(o):
                result.append(self.anim(self.word(o)));o+=4
                assert len(result)<100
        return result
    def gameplay(self):
        c=self.roots['coll_data'];g=self.roots['grGroundParam'];m=self.roots['map_head']
        result={'collision_fields':self.blob(c+4,4)+self.blob(c+12,24)+self.blob(c+40,8),
                'collision_arrays':[self.blob(self.word(c+p),self.word(c+n)*s) for p,n,s in [(0,4,8),(8,12,16),(36,40,40)]],
                'ground':self.blob(g,176),'stage_params':self.blob(self.word(g+176),self.word(g+180)*100),
                'platform_params':self.blob(self.roots['yakumono_param'],84)}
        rows=self.word(m+8);count=self.word(m+12)
        result['bindings']=[self.blob(self.word(rows+i*52+32),self.word(rows+i*52+36)*6) for i in range(count)]
        result['models']=[{'joints':self.joint(self.word(rows+i*52)),
                           'animations':self.animations(self.word(rows+i*52+4))} for i in range(count)]
        return result

def main():
    original=Archive(ROOT/'assets/GALE01/files/GrIz.dat')
    diet=Archive(ROOT/'references/diet-melee/files/GrIz.dat')
    a,b=original.gameplay(),diet.gameplay()
    report={'original_sha256':hashlib.sha256(original.raw).hexdigest(),'diet_sha256':hashlib.sha256(diet.raw).hexdigest(),
            'checks':{k:a[k]==b[k] for k in a if k!='models'},
            'models':[{'joints':x['joints']==y['joints'],'animations':x['animations']==y['animations']} for x,y in zip(a['models'],b['models'])],
            'model_count_equal':len(a['models'])==len(b['models'])}
    (ROOT/'build/diet-fountain-detailed-audit.json').write_text(json.dumps(report,indent=2)+'\n')
    (ROOT/'build/diet-fountain-gameplay-data.json').write_text(json.dumps({'original':a,'diet':b},indent=2)+'\n')
    print(json.dumps(report,indent=2))
    assert all(report['checks'].values()) and report['model_count_equal']
    # Model 1 is decoration: grIzumi_801CBE10 only starts its animation;
    # its update/collision callbacks are empty. Model 3's final decorative
    # siblings were removed. All collision bindings (grIz_803E0D60) and
    # grIzumi_801C3FA4 lookups use indices <=6. Compare the first 12 siblings,
    # including every interactive joint, and their complete animation trees.
    assert a['bindings'][1]==b['bindings'][1]==''
    for i in (0,2,4):assert a['models'][i]==b['models'][i],i
    assert a['models'][1]['joints']==b['models'][1]['joints']
    assert len(a['models'][1]['animations'])==1 and not b['models'][1]['animations']
    x,y=copy.deepcopy(a['models'][3]),copy.deepcopy(b['models'][3])
    for model in (x,y):
        joint=model['joints']['child']
        for _ in range(11):joint=joint['next']
        joint['next']=None
        for animation in model['animations']:
            node=animation[2]
            for _ in range(11):node=node[3]
            node[3]=None
    assert x==y,'Fountain interactive skeleton or animation changed'
    report['interactive_joints_and_animations_equal']=True
    report['visual_exceptions']=['Model 1 decorative animation removed','Model 3 decorative siblings after index 11 removed']
    (ROOT/'build/diet-fountain-detailed-audit.json').write_text(json.dumps(report,indent=2)+'\n')
    print('All collision, stage parameters, interactive joints and animations match.')

if __name__=='__main__':main()
