"""Compile escaped glyph literals and compare emitted bytes, including headers."""
import subprocess
from build import ROOT,local_clang
from engine_encoding import shift_jis_literals

directory=ROOT/'build/encoding-test';directory.mkdir(exist_ok=True)
source='''// Ｍ comment stays Unicode
#include "glyph.h"
#include <assert.h>
#include <string.h>
const char *name="Ｍａｒｉｏ";
const char *edge="Ａ1f\\n\\\"\\\\";
const char *hyphen="－";
int main(void){
    const unsigned char mario[]={0x82,0x6c,0x82,0x81,0x82,0x92,0x82,0x89,0x82,0x8f,0};
    const unsigned char edge_expected[]={0x82,0x60,'1','f','\\n','"','\\\\',0};
    assert(sizeof(KEY)==3 && (unsigned char)KEY[0]==0x82 && (unsigned char)KEY[1]==0xa0);
    assert(!memcmp(name,mario,sizeof(mario)));
    assert(!memcmp(edge,edge_expected,sizeof(edge_expected)));
    assert((unsigned char)hyphen[0]==0x81 && (unsigned char)hyphen[1]==0x7c);
}
'''
(directory/'glyph.h').write_text('#define KEY "あ"\n',encoding='utf-8')
(directory/'encoding.c').write_text(source,encoding='utf-8')
pp=directory/'encoding.i';exe=directory/'encoding.exe'
subprocess.run([local_clang(),'-E',str(directory/'encoding.c'),'-o',str(pp)],check=True)
pp.write_text(shift_jis_literals(pp.read_text(encoding='utf-8')),encoding='utf-8')
assert shift_jis_literals('// Ｍ\n"Ｍ"')=='// Ｍ\n"\\202\\154"'
assert shift_jis_literals('L"Ｍ" u8"Ｍ"')=='L"Ｍ" u8"Ｍ"'
subprocess.run([local_clang(),str(pp),'-o',str(exe)],check=True)
subprocess.run([str(exe)],check=True)
print('Execution charset: names, header macros, full-width hyphen, trailing digits, C escapes and comments passed')
