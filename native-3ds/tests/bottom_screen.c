#include "../port/3ds/bottom.h"
#include "bottom_screen_stubs.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static struct { unsigned before[8];uint16_t pixels[320*240];unsigned after[8]; } image;
static void save(const char* name,const MPBottomState* state,int fps,int guide){
    mp_bottom_draw(image.pixels,state,60,fps,0,0,guide);
    for(int i=0;i<8;++i)assert(image.before[i]==0xdeadbeef&&image.after[i]==0xdeadbeef);
    char path[200];snprintf(path,sizeof(path),"build/bottom-host-%s.rgb565",name);
    FILE* f=fopen(path,"wb");assert(f);assert(fwrite(image.pixels,1,sizeof(image.pixels),f)==sizeof(image.pixels));fclose(f);
}
/* The group pages with names in kana and accented letters, stock icon badges in every state, the emblem, the
 * busy reasons, the waiting room's stocks, and Zelda / Sheik following the host's live status. */
#define KANA10 "\xE3\x82\x86\xE3\x81\x86\xE3\x81\x8D\xE3\x82\x86\xE3\x81\x86\xE3\x81\x8D\xE3\x82\x86\xE3\x81\x86\xE3\x81\x8D\xE3\x82\x86"
static lobby_slot door(int kind,int fighter,int color,int chosen){lobby_slot d;memset(&d,0,sizeof d);d.kind=(uint8_t)kind;d.fighter=(uint8_t)fighter;d.color=(uint8_t)color;d.chosen=(uint8_t)chosen;d.level=3;return d;}
extern uint16_t mp_group_badge_drawn[4];
extern char mp_group_strip_text[4][24];
static void group_pages(void){
    MPBottomState s={0};s.scene=1;
    /* the Available Games list (3 rows, 48 px heads): a Mii head, the emblem, a busy "IN MATCH" row, a full game, Teams
     * colours; nine games over pages; the head before it is ready */
    GroupView* v=&bottom_stub_view;
    extern unsigned char mp_group_row_head[GROUP_ROWS];
    const char* hosts[9]={"Alice","Zo\xC3\xAB",KANA10,"Anna","Bob","\xE3\x82\x86\xE3\x81\x86\xE3\x81\x8D","Carol","Dave","Eve"};
    memset(v,0,sizeof *v);v->page=GROUP_PAGE_LIST;v->my_has_mii=1;v->my_mii[0]=1;v->my_mii_crc=0x4242;   /* our own head on Host */
    extern unsigned char mp_group_host_head;
    for(int i=0;i<9;++i){group_game* r=&v->row[i];snprintf(r->host,sizeof r->host,"%s",hosts[i]);r->players=1+i%4;r->max=4;r->key=(uint32_t)i;
        r->has_mii=i%2==0;r->mii_crc=(uint16_t)(0x1000+i);r->mii[0]=1;
        for(int p=0;p<4;++p){r->beacon.slot[p].kind=LOBBY_EMPTY;}
        r->beacon.slot[0].kind=LOBBY_HUMAN;r->beacon.slot[0].fighter=(uint8_t)(i*3%26);r->beacon.slot[0].chosen=1;}
    /* row 1: Human, CPU, choosing, closed; row 2: in a match (Sheik live); row 3: full, Teams on (red, blue, green, red CPU) */
    group_game* r=&v->row[1];r->beacon.slot[0].fighter=9;r->beacon.slot[0].costume=1;
    r->beacon.slot[1].kind=LOBBY_CPU;r->beacon.slot[1].fighter=25;r->beacon.slot[1].costume=2;r->beacon.slot[1].chosen=1;
    r->beacon.slot[2].kind=LOBBY_HUMAN;r->beacon.slot[2].chosen=0;
    r=&v->row[2];r->beacon.phase=LOBBY_MATCH;for(int p=0;p<4;++p){r->beacon.slot[p].kind=LOBBY_HUMAN;r->beacon.slot[p].fighter=(uint8_t)(p*6);r->beacon.slot[p].costume=(uint8_t)p;r->beacon.slot[p].chosen=1;}
    r->beacon.slot[0].fighter=19;
    r=&v->row[3];r->players=4;r->beacon.teams=1;
    for(int p=0;p<4;++p){r->beacon.slot[p].kind=p==3?LOBBY_CPU:LOBBY_HUMAN;r->beacon.slot[p].fighter=(uint8_t)(p*5+1);r->beacon.slot[p].chosen=1;r->beacon.slot[p].team=(uint8_t)(p==3?0:p);}
    v->count=3;v->pages=1;bottom_stub_mii_ready=1;
    save("group-list",&s,0,0);
    assert(mp_group_row_head[0]==1&&mp_group_row_head[1]==2&&mp_group_row_head[2]==1);   /* a Mii head, the emblem, a head */
    assert(mp_group_badge_drawn[0]==20);                                                   /* the IN MATCH row's Sheik (19 + 1) */
    bottom_stub_mii_ready=0;save("group-list-waiting",&s,0,0);assert(mp_group_row_head[0]==2);   /* the emblem until the head is ready */
    bottom_stub_mii_ready=1;
    /* the full game with Teams on page 2 of two */
    v->count=4;v->pages=2;v->list_page=1;save("group-list-teams",&s,0,0);assert(mp_group_row_head[0]==2&&mp_group_row_head[1]==0);
    /* nine games: the last page; a tap on its first row joins the game drawn there; the page buttons */
    v->count=9;v->pages=3;v->list_page=2;save("group-list-last-page",&s,0,0);
    assert(mp_group_row_head[0]==1);
    bottom_stub_joined=-1;mp_bottom_online_touch(150,65+10);assert(bottom_stub_joined==0);
    bottom_stub_joined=-1;mp_bottom_online_touch(150,65+49+10);assert(bottom_stub_joined==1);
    bottom_stub_turned=0;mp_bottom_online_touch(170,45);assert(bottom_stub_turned==-1);
    mp_bottom_online_touch(240,45);assert(bottom_stub_turned==0);
    v->list_page=0;save("group-list-page-1",&s,0,0);
    snprintf(v->error,sizeof v->error,"That game is full (4 consoles)");save("group-list-error",&s,0,0);
    assert(mp_group_host_head==1);
    v->error[0]=0;v->count=0;v->pages=1;v->list_page=0;save("group-list-empty",&s,0,0);
    v->my_has_mii=0;save("group-list-no-mii",&s,0,0);assert(mp_group_host_head==0);   /* no Mii: "HOST GAME" alone */
    v->my_has_mii=1;bottom_stub_mii_ready=0;save("group-list-own-pending",&s,0,0);assert(mp_group_host_head==0);   /* the place kept */
    bottom_stub_mii_ready=1;
    /* the strip: a full lobby with a CPU, a closed door and an empty port; names in kana; busy reasons */
    v->page=GROUP_PAGE_NONE;
    GroupStrip* g=&bottom_stub_strip;memset(g,0,sizeof *g);bottom_stub_strip_on=1;g->active=1;g->me=1;g->phase=LOBBY_CSS;g->rules.stocks=4;
    const char* names_[4]={"Anna",KANA10,"Zo\xC3\xAB \xC3\x85ngstr",""};
    for(int i=0;i<3;++i){g->port[i].present=1;g->port[i].signal=3-i;snprintf(g->port[i].name,sizeof g->port[i].name,"%s",names_[i]);}
    g->port[0].door=door(0,8,0,1);g->port[1].door=door(0,0,0,0);g->port[1].busy_why=LOBBY_BUSY_TAGS;g->port[2].door=door(2,0,0,0);
    g->port[3].door=door(1,13,1,1);
    s.scene=8;save("group-strip",&s,0,0);
    assert(!strcmp(mp_group_strip_text[1],"CHOOSING A NAME TAG")&&!strcmp(mp_group_strip_text[2],"CLOSED")&&!strcmp(mp_group_strip_text[3],"CPU"));
    g->rules.stocks=1;save("group-strip-1-stock",&s,0,0);g->rules.stocks=4;   /* "1 STOCK", not "1 STOCKS" */
    g->port[1].busy_why=LOBBY_BUSY_NAMING;save("group-strip-naming",&s,0,0);assert(!strcmp(mp_group_strip_text[1],"ENTERING A NAME"));
    g->port[1].busy_why=LOBBY_BUSY_RULES;save("group-strip-rules",&s,0,0);assert(!strcmp(mp_group_strip_text[1],"SETTING THE RULES"));
    g->port[1].busy_why=LOBBY_BUSY_SLIDER;g->port[1].door=door(0,0,0,1);save("group-strip-slider",&s,0,0);assert(!strcmp(mp_group_strip_text[1],"CHOSEN"));
    /* the waiting room: 4, 1, 0 and 7 stocks; a time match; Zelda with the live fighter 19 (Sheik) and 18 (Zelda) */
    g->watching=1;g->phase=LOBBY_MATCH;g->status.valid=1;g->status.timer=1;g->status.seconds=125;
    unsigned st[4]={4,1,0,7};
    for(int i=0;i<4;++i){g->port[i].door=door(i==3?1:0,i?i*5:18,0,1);g->status.port[i].kind=(uint8_t)(i==3);g->status.port[i].stocks=(uint8_t)st[i];
        g->status.port[i].damage=(uint16_t)(i*47);g->status.port[i].fighter=(uint8_t)(i?i*5:19);}
    g->port[2].present=1;
    snprintf(g->port[0].player,sizeof g->port[0].player,"%s","Anna");snprintf(g->port[1].player,sizeof g->port[1].player,"%s",KANA10);
    snprintf(g->port[2].player,sizeof g->port[2].player,"%s","Left Early");   /* a console that played port 3 and left */
    save("group-watch-sheik",&s,0,0);assert(mp_group_badge_drawn[0]==20);
    g->status.port[0].fighter=18;save("group-watch-zelda",&s,0,0);assert(mp_group_badge_drawn[0]==19);
    g->rules.time_mode=1;save("group-watch-time",&s,0,0);
    g->phase=LOBBY_RESULTS;save("group-watch-results",&s,0,0);g->phase=LOBBY_MATCH;   /* no timer on the results */
    /* Teams: each badge and human accent in its team's colour (port 2 on red, 3 on green); the late joiner on port 4,
     * where a CPU plays, does not lend the CPU its name (the row says CPU) */
    g->rules.time_mode=0;g->rules.teams=1;g->port[0].door.team=1;g->port[1].door.team=0;g->port[2].door.team=2;g->port[3].door.team=0;
    g->port[3].present=1;snprintf(g->port[3].name,sizeof g->port[3].name,"%s","Joiner");g->port[3].player[0]=0;g->me=3;
    save("group-watch-teams",&s,0,0);
    g->rules.teams=0;g->me=1;g->port[3].present=0;
    /* the HOME notice over the strip */
    g->watching=0;g->phase=LOBBY_CSS;bottom_stub_home_note=1;save("group-home-note",&s,0,0);bottom_stub_home_note=0;
    bottom_stub_strip_on=0;
}
int main(void){
    unsigned char font[287*512];FILE* f=fopen("build/bottom-font.bin","rb");assert(f);assert(fread(font,1,sizeof(font),f)==sizeof(font));fclose(f);
    assert(mp_bottom_art_init(font,"assets/GALE01/files/MnSlChr.usd","assets/GALE01/files/IfAll.usd"));
    for(int i=0;i<8;++i)image.before[i]=image.after[i]=0xdeadbeef;
    assert(mp_bottom_hit(0,214)==0&&mp_bottom_hit(0,215)==MP_BOTTOM_FPS);
    assert(mp_bottom_hit(79,239)==MP_BOTTOM_FPS&&mp_bottom_hit(80,215)==MP_BOTTOM_RATE);
    assert(mp_bottom_hit(159,239)==MP_BOTTOM_RATE&&mp_bottom_hit(160,215)==MP_BOTTOM_VIEW);
    assert(mp_bottom_hit(239,239)==MP_BOTTOM_VIEW&&mp_bottom_hit(240,215)==MP_BOTTOM_GUIDE);
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
    for(unsigned m=0;m<34;++m){char name[40];s.menu=m;snprintf(name,sizeof(name),"menu-%02u",m);save(name,&s,0,0);}
    s.scene=9;save("stage",&s,0,0);
    save("controls",&s,0,1);
    s.scene=4;s.stock_mode=0;save("training",&s,1,0);
    s.scene=2;s.stamina=1;save("stamina",&s,0,0);
    s.stamina=0;s.players[0].character=19;save("sheik",&s,0,0);
    group_pages();
    mp_bottom_art_exit();puts("Bottom layout bounds and touch targets passed");return 0;
}
