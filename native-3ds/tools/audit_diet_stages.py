"""Audit stage gameplay data independently of relocated archive addresses."""
import copy,hashlib,json,struct
from audit_diet_fountain import Archive
from assets import ROOT

class StageArchive(Archive):
    def joint(self,o,ancestors=()):
        result=super().joint(o,ancestors)
        if o and self.word(o+4)&0x4000:
            s=self.word(o+16);kind=self.b[s];n=struct.unpack_from('>h',self.b,s+2)[0]
            assert kind in (0,1,2,3) and 1<n<10000
            controls=n if kind==0 else 3*n-2 if kind==1 else n+2
            result['spline']={'header':self.blob(s,8),'length':self.blob(s+12,4),
                'controls':self.blob(self.word(s+8),controls*12),
                'segment_lengths':self.blob(self.word(s+16),n*4) if self.word(s+16) else None,
                'segment_polynomials':self.blob(self.word(s+20),(n-1)*20) if self.word(s+20) else None}
        return result
    def aobj(self,o):
        if not o:return None
        # obj_id is a relocated joint descriptor, not a stable integer ID.
        return [self.blob(o,8),self.joint(self.word(o+12)),self.fobj(self.word(o+8))]
    def gameplay(self,yakumono_size):
        c=self.roots['coll_data'];g=self.roots['grGroundParam'];m=self.roots['map_head']
        # Retail coll_data ends at +0x2c: x2C in the decomp is inferred,
        # unused by mpLib, and overlaps the following allocation in GrSt.
        result={'collision_fields':self.blob(c+4,4)+self.blob(c+12,24)+self.blob(c+40,4),
            'collision_arrays':[self.blob(self.word(c+p),self.word(c+n)*s) for p,n,s in [(0,4,8),(8,12,16),(36,40,40)]],
            'ground':self.blob(g,176),'stage_params':self.blob(self.word(g+176),self.word(g+180)*100),
            'yakumono':self.blob(self.roots['yakumono_param'],yakumono_size)}
        if yakumono_size in (8,16):
            sizes=[28,28] if yakumono_size==8 else [24,24,28,28]
            result['yakumono']=[self.blob(self.word(self.roots['yakumono_param']+i*4),n) for i,n in enumerate(sizes)]
        rows=self.word(m+8);count=self.word(m+12)
        result['models']=[{'joints':self.joint(self.word(rows+i*52)),
            'animations':self.animations(self.word(rows+i*52+4)),
            'bindings':self.blob(self.word(rows+i*52+32),self.word(rows+i*52+36)*6)} for i in range(count)]
        return result

def inspect(name,size):
    archives=[StageArchive(ROOT/p/name) for p in ('assets/GALE01/files','references/diet-melee/files')]
    a,b=[x.gameplay(size) for x in archives]
    result={'file':name,'bytes':[len(x.raw) for x in archives],
        'sha256':[hashlib.sha256(x.raw).hexdigest() for x in archives],
        'checks':{k:a[k]==b[k] for k in a if k!='models'},
        'models':[{k:x[k]==y[k] for k in x} for x,y in zip(a['models'],b['models'])],
        'model_counts':[len(x['models']) for x in(a,b)]}
    return a,b,result

def main():
    output=[]
    for name,size in [('GrSt.dat',36),('GrNBa.dat',8),('GrNLa.dat',16),('GrOp.dat',52)]:
        a,b,report=inspect(name,size);output.append(report)
        (ROOT/f'build/diet-audit-{name}-data.json').write_text(json.dumps([a,b],indent=2))
    (ROOT/'build/diet-expanded-audit.json').write_text(json.dumps(output,indent=2))
    print(json.dumps(output,indent=2))
if __name__=='__main__':main()
