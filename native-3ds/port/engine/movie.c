#include <dolphin/thp/thp.h>
#include "native.h"
/* The opening movie is optional. Decoding remains an explicit unsupported
 * operation until the THP decoder is ported; it must never return fake frames. */
void THPInit(void){}
int mp_movie_available(void){return 0;}
s32 THPVideoDecode(void*a,void*b,void*c,void*d,void*e){mp_platform_panic("THP video decoder is not implemented");}
s32 THPDec_8032FD40(THPDec_8032FD40_Data*a,u16 b){mp_platform_panic("THP decoder is not implemented");}
s32 THPDec_8032F8D4(u8*a,THPDec_8032FD40_Data*b){mp_platform_panic("THP decoder is not implemented");}
void THPDec_80331340(s32 a,void*b,void*c,void*d){mp_platform_panic("THP frame output is not implemented");}
void THPDec_803313D0(s32 a,void*b,void*c,void*d,u32 e){mp_platform_panic("THP frame output is not implemented");}
