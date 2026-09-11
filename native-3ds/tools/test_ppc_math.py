"""Compare the C PowerPC estimate with pinned Dolphin code and test vectors."""
import argparse,hashlib,json,subprocess,urllib.request
from pathlib import Path
from build import ROOT,local_clang


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--fetch-reference',action='store_true',help='Download hash-verified source files from the pinned Dolphin commit')
    args=ap.parse_args()
    ref=ROOT/'references/dolphin-float'
    fixture=json.loads((ROOT/'tests/fixtures/frsqrte.json').read_text())
    if args.fetch_reference:
        ref.mkdir(parents=True,exist_ok=True)
        paths={'COPYING':'COPYING','FloatUtils.cpp':'Source/Core/Common/FloatUtils.cpp',
               'FloatUtilsTest.cpp':'Source/UnitTests/Common/FloatUtilsTest.cpp',
               'TestValues.h':'Source/UnitTests/Common/TestValues.h'}
        for name,sha in fixture['source_sha256'].items():
            if (ref/name).exists() and hashlib.sha256((ref/name).read_bytes()).hexdigest()==sha:continue
            url='https://raw.githubusercontent.com/dolphin-emu/dolphin/'+fixture['commit']+'/'+paths[name]
            data=urllib.request.urlopen(url,timeout=30).read()
            assert hashlib.sha256(data).hexdigest()==sha,name
            (ref/name).write_bytes(data)
    for name,sha in fixture['source_sha256'].items():
        if not (ref/name).exists():ap.error('Missing pinned source; run with --fetch-reference')
        assert hashlib.sha256((ref/name).read_bytes()).hexdigest()==sha,name
    text=(ref/'FloatUtils.cpp').read_text()
    begin=text.index('const std::array<BaseAndDec, 32> frsqrte_expected')
    end=text.index('const std::array<BaseAndDec, 32> fres_expected',begin)
    # Preserve the reference function and table verbatim. Only provide its
    # dependency types/quiet-NaN helper in this standalone comparison runner.
    source='''#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include <cassert>
#include <cstdio>
#include "ppc_math.h"
using u64=uint64_t;using s64=int64_t;using u32=uint32_t;
struct BaseAndDec {int m_base,m_dec;};
double MakeQuiet(double value){return std::bit_cast<double>(std::bit_cast<u64>(value)|(1ULL<<51));}
'''+text[begin:end]+'''
int main(){
const u64 known[][2]={
'''+','.join('{0x'+a+'ULL,0x'+b+'ULL}' for a,b in fixture['vectors'])+'''};
uint64_t checked=0,seed=0x123456789abcdef0ULL;
for(auto pair:known){assert(mp_frsqrte_bits(pair[0])==pair[1]);++checked;}
for(unsigned i=0;i<1000000;++i){
    seed=seed*6364136223846793005ULL+1442695040888963407ULL;
    u64 expected=std::bit_cast<u64>(ApproximateReciprocalSquareRoot(std::bit_cast<double>(seed)));
    assert(mp_frsqrte_bits(seed)==expected);++checked;
}
for(unsigned parity=0;parity<2;++parity)for(unsigned i=0;i<32768;++i){
    u64 bits=((1022ULL+parity)<<52)|((u64)i<<37);
    assert(mp_frsqrte_bits(bits)==std::bit_cast<u64>(ApproximateReciprocalSquareRoot(std::bit_cast<double>(bits))));++checked;
}
// The original Newton refinement must converge from a reciprocal estimate.
for(double x: {0.01,1.0,4.0,100.0,10000.0,1000000.0}){
    double y=std::bit_cast<double>(mp_frsqrte_bits(std::bit_cast<u64>(x)));
    for(int step=0;step<3;++step)y=0.5*y*(3.0-y*y*x);
    assert(std::abs(x*y-std::sqrt(x))<=std::sqrt(x)*1e-14);
}
printf("{\\"passed\\":true,\\"exact_comparisons\\":%llu,\\"refinement_cases\\":6}\\n",(unsigned long long)checked);
}
'''
    directory=ROOT/'build/update14-math';directory.mkdir(exist_ok=True)
    cpp=directory/'compare.cpp';cpp.write_text(source)
    exe=directory/'compare.exe'
    subprocess.run([str(Path(local_clang()).with_name('clang++.exe')),'-std=c++20','-O2',
        '-ffp-contract=off','-I'+str(ROOT/'port/engine'),str(cpp),'-o',str(exe)],check=True)
    result=subprocess.check_output([str(exe)],text=True)
    (ROOT/'build/update14-qa').mkdir(parents=True,exist_ok=True)
    (ROOT/'build/update14-qa/ppc-math-host.json').write_text(result)
    print(result.strip())


if __name__=='__main__':main()
