"""Stress the actual source-snapshot functions with a tracked host allocator.

Functions are extracted from gx.c, not reimplemented. The small cache owner
stub supplies deterministic eviction, allocation failure and alias cases.
Live ARM tests separately compare cached draws against fresh GX decoding.
"""
import hashlib,json,re,subprocess
from pathlib import Path
from build import ROOT,local_clang

PRE=r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef unsigned char u8;
typedef uint32_t u32;
'''
STATE=r'''
#define GEOMETRY_ENTRIES 8
typedef struct {unsigned list,stamp;GeometrySource*source[7];} GeometryCache;
static GeometryCache geometry_cache[8],*geometry_record;
static GeometrySource*geometry_sources[1024];
static unsigned geometry_bytes,geometry_epoch=1,geometry_budget=4096;
static unsigned geometry_range_share=1,geometry_source_grow=1,geometry_source_growths;
static unsigned geometry_range_shares,geometry_source_compare_all,geometry_block_compare=1;
static unsigned geometry_compare_validate=1,geometry_compare_checks,geometry_source_compares,geometry_source_bytes,geometry_source_reuses;
#define GEOMETRY_BUDGET geometry_budget
static unsigned failures,allocations,live,sequence,checks,fail_after;
static struct {void*pointer;unsigned bytes;} blocks[256];
static void*mp_platform_alloc(unsigned n){
    if(fail_after&&!--fail_after){++failures;return NULL;}
    for(unsigned i=0;i<256;++i)if(!blocks[i].pointer){
        void*p=malloc(n);assert(p);blocks[i].pointer=p;blocks[i].bytes=n;++live;++allocations;return p;
    }
    abort();
}
static void mp_platform_free(void*p){
    for(unsigned i=0;i<256;++i)if(blocks[i].pointer==p){free(p);blocks[i].pointer=NULL;--live;return;}
    abort();
}
#define mp_be_memcmp_block memcmp
#define HSD_Panic(file,line,text) abort()
static void geometry_clear(GeometryCache*);
'''
TEST=r'''
static void geometry_clear(GeometryCache*e){
    for(unsigned i=0;i<7;++i)source_release(e->source[i]);memset(e,0,sizeof(*e));
}
static void graph(void){
    unsigned bytes=0,nodes=0,allocated=0;
    for(unsigned b=0;b<1024;++b)for(GeometrySource*s=geometry_sources[b];s;s=s->next){
        assert(++nodes<100);assert(source_bucket(s->base)==b);unsigned refs=0;
        for(unsigned i=0;i<8;++i)for(unsigned j=0;j<7;++j)refs+=geometry_cache[i].source[j]==s;
        assert(refs==s->refs&&refs);bytes+=sizeof(*s)+s->size+3;
        if(source_valid(s))assert(!memcmp(s->copy,s->source,s->size));
    }
    for(unsigned i=0;i<256;++i)if(blocks[i].pointer)allocated+=blocks[i].bytes;
    assert(bytes==geometry_bytes&&bytes==allocated&&live==nodes*2);++checks;
}
static GeometrySource*acquire(unsigned slot,const void*base,const void*data,unsigned n){
    geometry_clear(&geometry_cache[slot]);geometry_record=&geometry_cache[slot];
    geometry_record->list=1;geometry_record->stamp=++sequence;
    GeometrySource*s=source_acquire(base,data,n);geometry_record->source[0]=s;
    if(!s)geometry_record->list=0;geometry_record=NULL;graph();return s;
}
static void clear_all(void){for(unsigned i=0;i<8;++i)geometry_clear(&geometry_cache[i]);graph();assert(!live&&!geometry_bytes);geometry_budget=4096;fail_after=0;}
int main(void){
    static u8 data[4][2048];
    for(unsigned b=0;b<4;++b)for(unsigned i=0;i<2048;++i)data[b][i]=(u8)(i*73+b*29);
    for(unsigned offset=0;offset<4;++offset){
        GeometrySource*a=acquire(0,data[0],data[0]+offset,17);
        assert(a==acquire(1,data[0],data[0]+offset,513));assert(a->refs==2&&a->size==513);
        assert(a==acquire(2,data[0],data[0]+offset+13,77));assert(a->refs==3);
        data[0][offset+500]^=1;mp_gx_invalidate_sources();assert(!source_valid(a));
        GeometrySource*b=acquire(3,data[0],data[0]+offset,700);assert(b&&b!=a);clear_all();
    }
    geometry_source_grow=0;
    GeometrySource*a=acquire(0,data[0],data[0],17),*b=acquire(1,data[0],data[0],513);
    assert(a!=b&&b==acquire(2,data[0],data[0],400));clear_all();geometry_source_grow=1;
    a=acquire(0,data[0],data[0],17);unsigned bytes=geometry_bytes;
    fail_after=1;assert(!acquire(1,data[0],data[0],513));assert(a->refs==1&&geometry_bytes==bytes);clear_all();
    fail_after=2;assert(!acquire(0,data[0],data[0],33));clear_all();
    a=acquire(0,data[0],data[0],17);assert(a==acquire(1,data[0],data[0],17));
    assert(acquire(2,data[1],data[1],512));geometry_budget=300;
    assert(a==acquire(3,data[0],data[0],128));assert(a->refs==1);
    assert(!geometry_cache[0].list&&!geometry_cache[1].list&&!geometry_cache[2].list);clear_all();
    a=acquire(0,data[0],data[0],17);geometry_budget=130;
    assert(!acquire(1,data[0],data[0],128));clear_all();
    a=acquire(0,data[0],data[0],512);data[0][0]^=1;mp_gx_invalidate_sources();assert(!source_valid(a));clear_all();
    uint32_t random=0x31415926;
    for(unsigned i=0;i<20000;++i){
        random=random*1664525+1013904223;unsigned bank=(random>>16)&3,offset=(random>>20)&3;
        if(!(i%17)){data[bank][(random>>9)&1023]^=0x81;mp_gx_invalidate_sources();}
        if(!(i%31))fail_after=1;
        geometry_budget=512+(random&2047);
        acquire((random>>24)&7,data[bank],data[bank]+offset,1+((random>>3)&1023));
    }
    clear_all();printf("{\"passed\":true,\"graph_checks\":%u,\"allocations\":%u,\"injected_failures\":%u,\"growths\":%u}\n",checks,allocations,failures,geometry_source_growths);
}
'''

def function(source,name):
    start=source.rfind('\n',0,source.index(name+'('))+1
    first=source.index('{',start);depth=1;end=first+1
    while depth:
        depth+=(source[end]=='{')-(source[end]=='}');end+=1
    return source[start:end]+'\n'

def main():
    source=(ROOT/'port/engine/gx.c').read_text(encoding='utf-8')
    typedef=re.search(r'typedef struct GeometrySource \{[^\n]+',source)[0]
    names=('source_bucket','mp_gx_invalidate_sources','source_release','geometry_alloc','source_valid','source_acquire')
    functions='\n'.join(function(source,n) for n in names)
    out=ROOT/'build/update15-sources';out.mkdir(exist_ok=True)
    c=out/'test.c';c.write_text(PRE+typedef+'\n'+STATE+functions+TEST)
    exe=out/'test.exe'
    subprocess.run([local_clang(),'-O2','-Wno-pointer-to-int-cast','-I'+str(ROOT/'port/engine'),str(c),'-o',str(exe)],check=True)
    result=json.loads(subprocess.check_output([str(exe)],text=True))
    result['production_functions_sha256']=hashlib.sha256(functions.encode()).hexdigest()
    (ROOT/'build/update15-qa/source-cache-host.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))

if __name__=='__main__':main()
