/* Copyright 2018 Dolphin Emulator Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 * C adaptation of Common::ApproximateReciprocalSquareRoot and its table.
 * Original source, revision and license: vendor/DOLPHIN-README.md.
 */
#ifndef MP_PPC_MATH_H
#define MP_PPC_MATH_H
#include <stdint.h>
static inline uint64_t mp_frsqrte_bits(uint64_t bits){
    static const struct {int32_t base,dec;} table[32]={
    {0x1a7e800, -0x568}, {0x17cb800, -0x4f3}, {0x1552800, -0x48d}, {0x130c000, -0x435},
    {0x10f2000, -0x3e7}, {0x0eff000, -0x3a2}, {0x0d2e000, -0x365}, {0x0b7c000, -0x32e},
    {0x09e5000, -0x2fc}, {0x0867000, -0x2d0}, {0x06ff000, -0x2a8}, {0x05ab800, -0x283},
    {0x046a000, -0x261}, {0x0339800, -0x243}, {0x0218800, -0x226}, {0x0105800, -0x20b},
    {0x3ffa000, -0x7a4}, {0x3c29000, -0x700}, {0x38aa000, -0x670}, {0x3572000, -0x5f2},
    {0x3279000, -0x584}, {0x2fb7000, -0x524}, {0x2d26000, -0x4cc}, {0x2ac0000, -0x47e},
    {0x2881000, -0x43a}, {0x2665000, -0x3fa}, {0x2468000, -0x3c2}, {0x2287000, -0x38e},
    {0x20c1000, -0x35e}, {0x1f12000, -0x332}, {0x1d79000, -0x30a}, {0x1bf4000, -0x2e6},

    };
    uint64_t mantissa=bits&((1ULL<<52)-1),sign=bits&(1ULL<<63);
    int64_t exponent=bits&(0x7ffLL<<52);
    if(!mantissa&&!exponent)return sign|(0x7ffULL<<52);
    if(exponent==(0x7ffLL<<52)){
        if(mantissa)return bits|(1ULL<<51); /* Quiet the original NaN payload. */
        return sign?0x7ff8000000000000ULL:0;
    }
    if(sign)return 0x7ff8000000000000ULL;
    if(!exponent){
        do{exponent-=1LL<<52;mantissa<<=1;}while(!(mantissa&(1ULL<<52)));
        mantissa&=(1ULL<<52)-1;exponent+=1LL<<52;
    }
    uint64_t lsb=exponent&(1LL<<52);
    exponent=((0x3ffLL<<52)-((exponent-(0x3feLL<<52))/2))&(0x7ffLL<<52);
    unsigned i=(unsigned)((lsb|mantissa)>>37);
    return (uint64_t)exponent|((uint64_t)(table[i/2048].base+table[i/2048].dec*(int)(i%2048))<<26);
}
#endif
