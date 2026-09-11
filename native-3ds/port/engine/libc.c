#include "native.h"
#include <stdarg.h>
#include <stdint.h>
#include "byte_memory.h"

void *mp_be_memcpy(void *d,const void *s,size_t n) {return mp_memory_copy(d,s,n);}
void *mp_be_memset(void *d,int c,size_t n) {return mp_memory_set(d,c,n);}
void *mp_be_memmove(void*d,const void*s,size_t n) {return mp_memory_move(d,s,n);}
int mp_be_memcmp_reference(const void*a,const void*b,size_t n){return mp_memory_compare(a,b,n);}
int mp_be_memcmp(const void*,const void*,size_t);
int mp_be_memcmp_block(const void*,const void*,size_t);
/* ARM11's multiword loads avoid the dependent scalar loads generated for
 * the C comparison loop. Never read outside the requested range. Mismatches
 * are resolved bytewise, so unsigned-byte ordering also works in BE8. */
__asm__(
".syntax unified\n.arm\n.section .text.mp_be_memcmp,\"ax\",%progbits\n"
".global mp_be_memcmp\n.type mp_be_memcmp,%function\nmp_be_memcmp:\n"
"push {r4-r10,lr}\n"
"eor r3,r0,r1\ntst r3,#3\nbne 4f\n"
"1: cmp r2,#0\nbeq 6f\ntst r0,#3\nbeq 2f\n"
"ldrb r3,[r0],#1\nldrb r4,[r1],#1\nsubs r2,r2,#1\ncmp r3,r4\nbne 5f\nb 1b\n"
"2: cmp r2,#16\nblo 3f\n"
"ldmia r0!,{r3-r6}\nldmia r1!,{r7-r10}\n"
"cmp r3,r7\ncmpeq r4,r8\ncmpeq r5,r9\ncmpeq r6,r10\nbne 7f\n"
"sub r2,r2,#16\nb 2b\n"
"3: cmp r2,#4\nblo 4f\nldr r3,[r0],#4\nldr r4,[r1],#4\ncmp r3,r4\nbne 8f\nsub r2,r2,#4\nb 3b\n"
"4: cmp r2,#0\nbeq 6f\nldrb r3,[r0],#1\nldrb r4,[r1],#1\nsub r2,r2,#1\ncmp r3,r4\nbeq 4b\n"
"5: sub r0,r3,r4\npop {r4-r10,pc}\n"
"6: mov r0,#0\npop {r4-r10,pc}\n"
"7: sub r0,r0,#16\nsub r1,r1,#16\nb 4b\n"
"8: sub r0,r0,#4\nsub r1,r1,#4\nb 4b\n"
".size mp_be_memcmp,.-mp_be_memcmp\n"
);
/* Equal geometry snapshots usually span thousands of bytes. Unroll four
 * bounded 16-byte groups so that loop-control instructions are paid once
 * per 64 bytes. Keep each group's mismatch position and remaining length
 * intact for exact byte ordering and tails; no speculative over-read. */
__asm__(
".syntax unified\n.arm\n.section .text.mp_be_memcmp_block,\"ax\",%progbits\n"
".global mp_be_memcmp_block\n.type mp_be_memcmp_block,%function\nmp_be_memcmp_block:\n"
"push {r4-r10,lr}\n"
"eor r3,r0,r1\ntst r3,#3\nbne 4f\n"
"1: cmp r2,#0\nbeq 6f\ntst r0,#3\nbeq 9f\n"
"ldrb r3,[r0],#1\nldrb r4,[r1],#1\nsubs r2,r2,#1\ncmp r3,r4\nbne 5f\nb 1b\n"
"9: cmp r2,#64\nblo 2f\n"
".rept 4\n"
"ldmia r0!,{r3-r6}\nldmia r1!,{r7-r10}\n"
"cmp r3,r7\ncmpeq r4,r8\ncmpeq r5,r9\ncmpeq r6,r10\nbne 7f\nsub r2,r2,#16\n"
".endr\nb 9b\n"
"2: cmp r2,#16\nblo 3f\n"
"ldmia r0!,{r3-r6}\nldmia r1!,{r7-r10}\n"
"cmp r3,r7\ncmpeq r4,r8\ncmpeq r5,r9\ncmpeq r6,r10\nbne 7f\nsub r2,r2,#16\nb 2b\n"
"3: cmp r2,#4\nblo 4f\nldr r3,[r0],#4\nldr r4,[r1],#4\ncmp r3,r4\nbne 8f\nsub r2,r2,#4\nb 3b\n"
"4: cmp r2,#0\nbeq 6f\nldrb r3,[r0],#1\nldrb r4,[r1],#1\nsub r2,r2,#1\ncmp r3,r4\nbeq 4b\n"
"5: sub r0,r3,r4\npop {r4-r10,pc}\n"
"6: mov r0,#0\npop {r4-r10,pc}\n"
"7: sub r0,r0,#16\nsub r1,r1,#16\nb 4b\n"
"8: sub r0,r0,#4\nsub r1,r1,#4\nb 4b\n"
".size mp_be_memcmp_block,.-mp_be_memcmp_block\n"
);
/* Referenced only by the development bridge; removed from physical builds. */
unsigned mp_memory_compare_ticks[3];
unsigned mp_memory_compare_self_test(void){
    unsigned char left[1040],right[1040];unsigned cases=0;
    for(unsigned a=0;a<8;++a)for(unsigned b=0;b<8;++b){
        for(unsigned i=0;i<1024;++i)left[a+i]=right[b+i]=(i*73+19)&255;
        for(unsigned n=0;n<=1024;++n){
            if(mp_be_memcmp(left+a,right+b,n)!=0||mp_be_memcmp_block(left+a,right+b,n)!=0)return 0;++cases;
            if(n)for(unsigned which=0;which<3;++which){unsigned at=which==0?0:which==1?n-1:n/2;unsigned char old=right[b+at];right[b+at]^=0x81;
                int got=mp_be_memcmp(left+a,right+b,n),ref=mp_memory_compare(left+a,right+b,n);
                if(got!=ref||mp_be_memcmp_block(left+a,right+b,n)!=ref)return 0;++cases;right[b+at]=old;
            }
        }
    }
    /* Distinct buffers prevent a self-comparison shortcut from invalidating
     * the benchmark. Keep the same byte pattern across all offsets. */
    for(unsigned i=0;i<1040;++i)left[i]=right[i]=(i*73+19)&255;
    int (*volatile implementations[3])(const void*,const void*,size_t)={mp_be_memcmp,mp_be_memcmp_reference,mp_be_memcmp_block};
    for(unsigned k=0;k<3;++k){unsigned start=mp_platform_ticks();
        for(unsigned i=0;i<16384;++i){unsigned offset=i&3,n=256-offset;
            if(implementations[k](left+offset,right+offset,n))return 0;
        }
        mp_memory_compare_ticks[k]=mp_platform_ticks()-start;
    }
    return cases;
}
void *mp_be_memchr(const void*p,int v,size_t n){const unsigned char*s=p;while(n--){if(*s==(unsigned char)v)return(void*)s;++s;}return NULL;}
size_t mp_be_strlen(const char*s){const char*p=s;while(*p)++p;return p-s;}
int mp_be_strcmp(const char*a,const char*b){while(*a&&*a==*b){++a;++b;}return(unsigned char)*a-(unsigned char)*b;}
int mp_be_strncmp(const char*a,const char*b,size_t n){while(n--){if(*a!=*b||!*a)return(unsigned char)*a-(unsigned char)*b;++a;++b;}return 0;}
char *mp_be_strcpy(char*d,const char*s){char*p=d;while((*p++=*s++));return d;}
char *mp_be_strncpy(char*d,const char*s,size_t n){char*p=d;while(n&&*s){*p++=*s++;--n;}while(n--)*p++=0;return d;}
char *mp_be_strcat(char*d,const char*s){mp_be_strcpy(d+mp_be_strlen(d),s);return d;}
char *mp_be_strchr(const char*s,int c){do{if(*s==(char)c)return(char*)s;}while(*s++);return NULL;}
char *mp_be_strrchr(const char*s,int c){char*r=NULL;do{if(*s==(char)c)r=(char*)s;}while(*s++);return r;}
char *mp_be_strstr(const char*a,const char*b){size_t n=mp_be_strlen(b);for(;*a;++a)if(!mp_be_strncmp(a,b,n))return(char*)a;return n?NULL:(char*)a;}
void *mp_be_malloc(size_t n){return mp_platform_alloc(n);}
void mp_be_free(void*p){mp_platform_free(p);}
void *mp_be_calloc(size_t n,size_t s){if(s&&n>UINT32_MAX/s)return NULL;void*p=mp_be_malloc(n*s);if(p)mp_be_memset(p,0,n*s);return p;}

/* Logging formatter used before the full newlib ABI bridge is introduced. */
typedef struct {char *p;size_t capacity,count;} Text;
static void emit(Text*t,char c){if(t->count+1<t->capacity)t->p[t->count]=c;++t->count;}
static void number(Text*t,unsigned value,unsigned radix,int width,char fill){char b[33];int n=0;do{b[n++]="0123456789abcdef"[value%radix];value/=radix;}while(value);while(width-->n)emit(t,fill);while(n)emit(t,b[--n]);}
int mp_be_vsnprintf(char*d,size_t cap,const char*f,va_list ap)
{
    Text t={d,cap,0};
    while(*f){if(*f!='%'){emit(&t,*f++);continue;}++f;char fill=' ';int width=0;
        if(*f=='0'){fill='0';++f;}while(*f>='0'&&*f<='9')width=width*10+*f++-'0';
        if(*f=='.'){++f;while(*f>='0'&&*f<='9')++f;}
        while(*f=='l'||*f=='h'||*f=='z')++f;
        char c=*f?*f++:0;
        if(c=='s'){const char*s=va_arg(ap,const char*);if(!s)s="(null)";while(*s)emit(&t,*s++);}
        else if(c=='c')emit(&t,va_arg(ap,int));
        else if(c=='d'||c=='i'){int v=va_arg(ap,int);if(v<0){emit(&t,'-');--width;}number(&t,v<0?0u-(unsigned)v:(unsigned)v,10,width,fill);}
        else if(c=='u'||c=='x'||c=='X'||c=='p')number(&t,va_arg(ap,unsigned),(c=='u')?10:16,width,fill);
        else if(c=='f'||c=='g'||c=='e'){double v=va_arg(ap,double);if(v<0){emit(&t,'-');v=-v;}unsigned whole=(unsigned)v;number(&t,whole,10,0,' ');emit(&t,'.');number(&t,(unsigned)((v-whole)*1000),10,3,'0');}
        else if(c)emit(&t,c);
    }
    if(cap)d[t.count<cap?t.count:cap-1]=0;return t.count;
}
int mp_be_snprintf(char*d,size_t n,const char*f,...){va_list a;va_start(a,f);int r=mp_be_vsnprintf(d,n,f,a);va_end(a);return r;}
int mp_be_vsprintf(char*d,const char*f,va_list a){return mp_be_vsnprintf(d,UINT32_MAX,f,a);}
int mp_be_sprintf(char*d,const char*f,...){va_list a;va_start(a,f);int r=mp_be_vsprintf(d,f,a);va_end(a);return r;}
int mp_be_printf(const char*f,...){char b[1024];va_list a;va_start(a,f);int r=mp_be_vsnprintf(b,sizeof(b),f,a);va_end(a);mp_platform_log(b);return r;}
int mp_be_puts(const char*s){mp_platform_log(s);mp_platform_log("\n");return 0;}
int mp_be_putchar(int c){char s[2]={c,0};mp_platform_log(s);return c;}
void mp_be_exit(int c){(void)c;mp_platform_panic("Engine requested exit");}
void mp_be_abort(void){mp_platform_panic("Engine abort");}
void mp_be___aeabi_memcpy(void*d,const void*s,size_t n){mp_be_memcpy(d,s,n);}
void mp_be___aeabi_memcpy4(void*d,const void*s,size_t n){mp_be_memcpy(d,s,n);}
void mp_be___aeabi_memcpy8(void*d,const void*s,size_t n){mp_be_memcpy(d,s,n);}
void mp_be___aeabi_memmove4(void*d,const void*s,size_t n){mp_be_memmove(d,s,n);}
void mp_be___aeabi_memmove(void*d,const void*s,size_t n){mp_be_memmove(d,s,n);}
void mp_be___aeabi_memmove8(void*d,const void*s,size_t n){mp_be_memmove(d,s,n);}
void mp_be___aeabi_memclr(void*d,size_t n){mp_be_memset(d,0,n);}
void mp_be___aeabi_memclr4(void*d,size_t n){mp_be_memset(d,0,n);}
void mp_be___aeabi_memclr8(void*d,size_t n){mp_be_memset(d,0,n);}
void mp_be___aeabi_memset(void*d,size_t n,int c){mp_be_memset(d,c,n);}
void mp_be___aeabi_memset4(void*d,size_t n,int c){mp_be_memset(d,c,n);}
void mp_be___aeabi_memset8(void*d,size_t n,int c){mp_be_memset(d,c,n);}
unsigned long long __cvt_dbl_usll(double v){return(unsigned long long)v;}
