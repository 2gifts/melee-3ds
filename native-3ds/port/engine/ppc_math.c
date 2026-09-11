#include "ppc_math.h"
#include "native.h"
#include <placeholder.h>
#include <ppc_math_vectors.h>

/* Keep integer classification opaque to floating-point optimization. ARM's
 * flush-to-zero mode must not turn a subnormal bit pattern into zero when
 * the compiler recognizes a double-to-integer bitcast and its zero test. */
__attribute__((noinline)) static uint64_t estimate_word(uint64_t bits){
    return mp_frsqrte_bits(bits);
}
/* Keep the estimate in the game ABI. No SDK/libm call or endian transition. */
double mp_frsqrte(double value){
    union {double d;uint64_t u;} result={.d=value};
    result.u=estimate_word(result.u);
    return result.d;
}

/* The development bridge checks the actual double-precision BE8 call ABI.
 * Unreferenced fixture code/data are discarded from the release image. */
unsigned mp_ppc_math_self_test(void){
    unsigned i;
    for(i=0;i<sizeof(mp_ppc_vectors)/sizeof(*mp_ppc_vectors);++i){
        union {uint64_t u;double d;} input={.u=mp_ppc_vectors[i][0]},output;
        output.d=__frsqrte(input.d);
        if(output.u!=mp_ppc_vectors[i][1])mp_platform_panic("PowerPC reciprocal-square-root mismatch");
    }
    return i;
}
