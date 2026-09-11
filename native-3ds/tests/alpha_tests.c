#include <assert.h>
#include <stdio.h>
#include "../port/3ds/alpha_test.h"
int main(void){
    const unsigned refs[]={0,1,64,127,128,254,255};unsigned simple=0,complex=0;
    for(unsigned a=0;a<8;++a)for(unsigned b=0;b<8;++b)for(unsigned op=0;op<4;++op)
    for(unsigned i=0;i<7;++i)for(unsigned j=0;j<7;++j){
        unsigned packed=a|(refs[i]<<3)|(op<<11)|(b<<13)|(refs[j]<<16),func,ref;
        int reduced=mp_alpha_reduce(packed,&func,&ref);if(reduced)++simple;else ++complex;
        for(unsigned alpha=0;alpha<256;++alpha){
            int value=mp_alpha_compare(alpha,a,refs[i]);
            unsigned second=op==0?(value?b:0):op==1?(value?7:b):op==2?(value?7-b:b):(value?b:7-b);
            assert(mp_alpha_pass(packed,alpha)==mp_alpha_compare(alpha,second,refs[j]));
            if(reduced)assert(mp_alpha_pass(packed,alpha)==mp_alpha_compare(alpha,func,ref));
        }
    }
    printf("Alpha predicates verified: %u single comparisons, %u stencil fallbacks\n",simple,complex);
}
