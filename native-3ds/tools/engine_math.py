"""Select the Dolphin SDK's portable C math implementations for ARM."""
import re
from build import ROOT,UPSTREAM

def generate():
    functions={}
    for filename in ('mtx.c','mtxvec.c','vec.c','mtx44.c'):
        source=(UPSTREAM/'extern/dolphin/src/dolphin/mtx'/filename).read_text()
        for m in re.finditer(r'^(?:void|u32|f32)\s+((?:C_|MTX|VEC)\w+)\s*\([^;]+?\)\s*\{',source,re.M):
            start=m.start();cursor=m.end();depth=1
            while depth:
                if source[cursor]=='{':depth+=1
                elif source[cursor]=='}':depth-=1
                cursor+=1
            code=source[start:cursor]
            if re.search(r'\basm\b',code):continue
            functions[m[1]]=code
    result=['/* Generated from the pinned Dolphin SDK portable C routines. */',
            '#include <dolphin/mtx.h>','#include <math.h>',
            '#include <sysdolphin/baselib/debug.h>',
            '#define FALSE 0',
            '#define ASSERTMSGLINE(line,cond,msg) HSD_ASSERTMSG(line,cond,msg)']
    result.extend(functions.values())
    headers=(UPSTREAM/'extern/dolphin/include/dolphin/mtx.h').read_text()
    for name in functions:
        if not name.startswith('C_'):continue
        paired='PS'+name[2:]
        if re.search(r'\b'+paired+r'\s*\(',headers):
            result.append('__typeof('+name+') '+paired+' __attribute__((alias("'+name+'")));')
    result.append('void PSMTXTrans(Mtx m,float x,float y,float z){C_MTXIdentity(m);m[0][3]=x;m[1][3]=y;m[2][3]=z;}')
    dest=ROOT/'build/generated/dolphin_math.c'
    dest.parent.mkdir(parents=True,exist_ok=True);dest.write_text('\n\n'.join(result)+'\n')
    return dest
