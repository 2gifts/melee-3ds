#ifndef MP_ALPHA_TEST_H
#define MP_ALPHA_TEST_H
static int mp_alpha_compare(unsigned value,unsigned func,unsigned ref){
    switch(func){case 0:return 0;case 1:return value<ref;case 2:return value==ref;case 3:return value<=ref;case 4:return value>ref;case 5:return value!=ref;case 6:return value>=ref;default:return 1;}
}
static int mp_alpha_pass(unsigned packed,unsigned value){
    int a=mp_alpha_compare(value,packed&7,(packed>>3)&255),b=mp_alpha_compare(value,(packed>>13)&7,(packed>>16)&255);
    switch((packed>>11)&3){case 0:return a&&b;case 1:return a||b;case 2:return a!=b;default:return a==b;}
}
/* Find an exactly equivalent PICA comparison, including GREATER zero. */
static int mp_alpha_reduce(unsigned packed,unsigned*func,unsigned*ref){
    unsigned count=0,first=256,last=0,excluded=0;
    for(unsigned i=0;i<256;++i)if(mp_alpha_pass(packed,i)){if(!count)first=i;last=i;++count;}else excluded=i;
    *ref=0;
    if(!count){*func=0;return 1;}if(count==256){*func=7;return 1;}
    if(count==1){*func=2;*ref=first;return 1;}if(count==255){*func=5;*ref=excluded;return 1;}
    if(count==last-first+1){if(first==0){*func=3;*ref=last;return 1;}if(last==255){*func=6;*ref=first;return 1;}}
    return 0;
}
#endif
