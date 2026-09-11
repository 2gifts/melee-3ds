"""Read original Peach turnip material/image bindings without changing assets."""
import json, struct
from audit_diet_fountain import Archive
from assets import ROOT


def main():
    archive=Archive(ROOT/'assets/GALE01/files/PlPe.dat'); word=archive.word
    article=word(word(archive.roots['ftDataPeach']+0x48)+4)
    root=word(word(article+0x10)); materials=[]; animations=[]
    def image_desc(offset):
        if not offset:return None
        w,h=struct.unpack_from('>HH',archive.b,offset+4)
        return {'descriptor':offset,'pixels':word(offset),'width':w,'height':h,'format':word(offset+8)}
    def joint(offset):
        if not offset:return
        dobj=word(offset+16)
        while dobj:
            mobj=word(dobj+8);tobj=word(mobj+8) if mobj else 0;textures=[]
            while tobj:
                textures.append({'descriptor':tobj,'id':word(tobj+8),'source':word(tobj+12),
                    'flags':hex(word(tobj+64)),'image':image_desc(word(tobj+76)),
                    'palette':word(tobj+80),'tev':word(tobj+88)})
                tobj=word(tobj+4)
            materials.append({'joint':offset,'joint_flags':hex(word(offset+4)),'dobj':dobj,
                'material':mobj,'render_mode':hex(word(mobj+4)) if mobj else None,'textures':textures})
            dobj=word(dobj+4)
        joint(word(offset+8));joint(word(offset+12))
    def matanimjoint(offset):
        if not offset:return
        mat=word(offset+8)
        while mat:
            tex=word(mat+8)
            while tex:
                count=struct.unpack_from('>H',archive.b,tex+20)[0]
                table=word(tex+12)
                animations.append({'texanim':tex,'id':word(tex+4),'aobj':word(tex+8),
                    'images':[image_desc(word(table+4*i)) for i in range(count)]})
                tex=word(tex)
            mat=word(mat)
        matanimjoint(word(offset));matanimjoint(word(offset+4))
    joint(root);matanimjoint(word(word(article+12)+4))
    result={'article':article,'joint':root,'materials':materials,'texture_animations':animations}
    (ROOT/'build/turnip-assets.json').write_text(json.dumps(result,indent=2))
    print(json.dumps(result,indent=2))


if __name__=='__main__':main()
