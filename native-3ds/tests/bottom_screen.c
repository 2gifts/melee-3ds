#include "../port/3ds/bottom.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static struct { unsigned before[8];uint16_t pixels[320*240];unsigned after[8]; } image;
static void save(const char* name,const MPBottomState* state,int fps,int guide){
    mp_bottom_draw(image.pixels,state,60,fps,0,guide);
    for(int i=0;i<8;++i)assert(image.before[i]==0xdeadbeef&&image.after[i]==0xdeadbeef);
    char path[200];snprintf(path,sizeof(path),"build/bottom-host-%s.rgb565",name);
    FILE* f=fopen(path,"wb");assert(f);assert(fwrite(image.pixels,1,sizeof(image.pixels),f)==sizeof(image.pixels));fclose(f);
}
int main(void){
    unsigned char font[287*512];FILE* f=fopen("build/bottom-font.bin","rb");assert(f);assert(fread(font,1,sizeof(font),f)==sizeof(font));fclose(f);
    assert(mp_bottom_art_init(font,"assets/GALE01/files/MnSlChr.usd","assets/GALE01/files/IfAll.usd"));
    for(int i=0;i<8;++i)image.before[i]=image.after[i]=0xdeadbeef;
    assert(mp_bottom_hit(0,214)==0&&mp_bottom_hit(0,215)==MP_BOTTOM_FPS);
    assert(mp_bottom_hit(103,239)==MP_BOTTOM_FPS&&mp_bottom_hit(104,215)==MP_BOTTOM_VIEW);
    assert(mp_bottom_hit(214,239)==MP_BOTTOM_VIEW&&mp_bottom_hit(215,215)==MP_BOTTOM_GUIDE);
    assert(mp_bottom_hit(320,215)==0&&mp_bottom_hit(0,240)==0);
    MPBottomState s={0};s.stock_mode=1;s.rule_stocks=4;s.rule_minutes=8;s.timer=1;s.seconds=423;
    for(unsigned i=0;i<4;++i){s.players[i]=(MPBottomPlayer){i?1:0,i*7,0,i,4,17+i*52};}
    s.scene=2;save("four-player",&s,1,0);
    s.players[2].kind=s.players[3].kind=3;s.players[0].character=2;s.players[1].character=18;
    save("two-player",&s,0,0);
    s.players[0].stocks=0;save("eliminated",&s,1,0);
    s.scene=8;s.players[0].stocks=4;save("css",&s,0,0);
    s.scene=0;save("title",&s,0,0);
    s.scene=1;save("menu",&s,0,0);
    s.scene=9;save("stage",&s,0,0);
    save("controls",&s,0,1);
    s.scene=4;s.stock_mode=0;save("training",&s,1,0);
    s.scene=2;s.stamina=1;save("stamina",&s,0,0);
    s.stamina=0;s.players[0].character=19;save("sheik",&s,0,0);
    mp_bottom_art_exit();puts("Bottom layout bounds and touch targets passed");return 0;
}
