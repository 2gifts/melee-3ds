/* A small CPU compositor for the touch screen. Original artwork is decoded
 * once, independently of GX textures, GPU state and gameplay allocations. */
#include "bottom.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PW 56
#define PH 78
#define PORTRAITS 118
#define RGB(r,g,b) ((((r)>>3)<<11)|(((g)>>2)<<5)|((b)>>3))
static const uint16_t ink=RGB(230,239,245), muted=RGB(132,159,182), gold=RGB(241,183,63);
static const uint16_t colors[4]={RGB(246,73,75),RGB(70,149,255),RGB(247,194,51),RGB(68,211,127)};
static uint16_t* canvas;
static uint16_t* portraits;
static unsigned char* opacity;
static uint16_t backdrop[320*240];
static unsigned backdrop_ready;
static uint16_t sheik[5][24*24];
static unsigned char sheik_alpha[5][24*24];
static unsigned sheik_ready;
static unsigned char glyphs[95][32*32], glyph_left[95], glyph_width[95];
static const char* names[26]={"CAPTAIN FALCON","DONKEY KONG","FOX","MR. GAME & WATCH","KIRBY","BOWSER","LINK","LUIGI","MARIO","MARTH","MEWTWO","NESS","PEACH","PIKACHU","ICE CLIMBERS","JIGGLYPUFF","SAMUS","YOSHI","ZELDA","SHEIK","FALCO","YOUNG LINK","DR. MARIO","ROY","PICHU","GANONDORF"};
static unsigned be32(const unsigned char* p){return (unsigned)p[0]<<24|(unsigned)p[1]<<16|p[2]<<8|p[3];}
static unsigned be16(const unsigned char* p){return p[0]<<8|p[1];}
static uint16_t blend(uint16_t a,uint16_t b,unsigned alpha){
    unsigned rb=(((a&0xf81f)*alpha+(b&0xf81f)*(32-alpha))>>5)&0xf81f;
    unsigned g=(((a&0x07e0)*alpha+(b&0x07e0)*(32-alpha))>>5)&0x07e0;
    return rb|g;
}
static void pixel(int x,int y,uint16_t color,unsigned alpha){
    if((unsigned)x<320&&(unsigned)y<240){uint16_t* p=canvas+x*240+239-y;*p=alpha>=32?color:blend(color,*p,alpha);}
}
static void rect(int x,int y,int w,int h,uint16_t color){
    int endx=x+w,endy=y+h;if(x<0)x=0;if(y<0)y=0;if(endx>320)endx=320;if(endy>240)endy=240;
    for(int xx=x;xx<endx;++xx){uint16_t* p=canvas+xx*240+240-endy;for(int yy=y;yy<endy;++yy)*p++=color;}
}
static void slash(int x,int y,int w,int h,uint16_t color){for(int yy=0;yy<h;++yy)rect(x+yy/3,y+yy,w,1,color);}
static unsigned glyph_index(unsigned ch){
    if(ch>='0'&&ch<='9')return ch-'0';
    if(ch>='A'&&ch<='Z')return ch-'A'+10;
    if(ch>='a'&&ch<='z')return ch-'a'+36;
    switch(ch){case '%':return 259;case ':':return 233;case '.':return 231;case '-':return 252;
    case '/':return 240;case '+':return 251;case '!':return 236;case '?':return 235;
    case '&':return 261;case '(':return 245;case ')':return 246;case '=':return 254;default:return 227;}
}
void mp_bottom_font_init(const unsigned char* font){
    for(unsigned ch=32;ch<127;++ch){
        unsigned i=ch-32,left=31,right=0;
        const unsigned char* src=font+512*glyph_index(ch);
        for(unsigned y=0;y<32;++y)for(unsigned x=0;x<32;++x){
            unsigned at=((y/8)*4+x/8)*64+(y%8)*8+x%8;
            unsigned a=(src[at/2]>>(at&1?0:4))&15;
            glyphs[i][y*32+x]=a;
            if(a>2){if(x<left)left=x;if(x>right)right=x;}
        }
        glyph_left[i]=left<=right?left:0;glyph_width[i]=left<=right?right-left+1:12;
    }
}
static unsigned advance(unsigned ch,int h){return ch==' '?h/3:(glyph_width[ch-32]*h+31)/32+1;}
static int width(const char* s,int h){int n=0;for(;*s;++s)if((unsigned char)*s>=32&&(unsigned char)*s<127)n+=advance(*s,h);return n;}
static void text(int x,int y,const char* s,int h,uint16_t color){
    unsigned rows[64];for(int yy=0;yy<h;++yy)rows[yy]=(yy*32/h)*32;
    for(;*s;++s){unsigned ch=(unsigned char)*s;if(ch<32||ch>=127)continue;
        if(ch!=' '){unsigned i=ch-32,w=(glyph_width[i]*h+31)/32;
            unsigned columns[64];for(unsigned xx=0;xx<w;++xx)columns[xx]=glyph_left[i]+xx*32/h;
            for(int yy=0;yy<h;++yy)for(unsigned xx=0;xx<w;++xx){
                unsigned sx=columns[xx];
                if(sx<32){unsigned a=glyphs[i][rows[yy]+sx];if(a)pixel(x+xx,y+yy,color,(a*32+7)/15);}
            }
        }x+=advance(ch,h);
    }
}
static void fit(int x,int y,const char* s,int h,int maxw,uint16_t color){while(h>8&&width(s,h)>maxw)--h;text(x,y,s,h,color);}
static void right(int x,int y,const char* s,int h,uint16_t color){text(x-width(s,h),y,s,h,color);}
static void center(int x,int y,const char* s,int h,uint16_t color){text(x-width(s,h)/2,y,s,h,color);}
static unsigned portrait_index(unsigned character,unsigned costume){
    if(character==19)character=18;
    if(character>=26)return 0;
    unsigned c=character>19?character-1:character;
    if(costume<4)return costume*25+c;
    /* The US 1.02 CSS's final images are sparse, unlike animation frames. */
    static const unsigned char fifth[25]={100,101,2,3,102,5,103,7,104,114,10,11,113,13,14,115,105,106,107,19,108,109,117,23,116};
    if(costume==5){if(c==0)return 110;if(c==4)return 111;if(c==17)return 112;}
    return fifth[c];
}
static void load_sheik(const char* path){
    FILE* f=fopen(path,"rb");if(!f)return;
    fseek(f,0,SEEK_END);long size=ftell(f);rewind(f);
    if(size<800000||size>2*1024*1024){fclose(f);return;}
    unsigned char* raw=malloc(size);if(!raw){fclose(f);return;}
    if(fread(raw,1,size,f)!=(size_t)size){free(raw);fclose(f);return;}fclose(f);
    unsigned char* b=raw+32;unsigned n=be32(raw+4);static const unsigned ix[5]={25,51,77,103,114};
    if(n>(unsigned)size-32||n<775156)goto done;
    for(unsigned c=0;c<5;++c){
        unsigned d=be32(b+774116+ix[c]*4),p=be32(b+774636+ix[c]*4);
        if(d+24>n||p+16>n)goto done;
        unsigned im=be32(b+d),pal=be32(b+p);
        if(be16(b+d+4)!=24||be16(b+d+6)!=24||be32(b+d+8)!=8||
            be32(b+p+4)!=2||be16(b+p+12)!=16||im+288>n||pal+32>n)goto done;
        for(unsigned y=0;y<24;++y)for(unsigned x=0;x<24;++x){
            unsigned t=((y/8)*3+x/8)*64+(y%8)*8+x%8;
            unsigned v=be16(b+pal+2*((b[im+t/2]>>(t&1?0:4))&15));
            unsigned r=v&0x8000?((v>>10)&31)*255/31:((v>>8)&15)*17;
            unsigned g=v&0x8000?((v>>5)&31)*255/31:((v>>4)&15)*17;
            unsigned bl=v&0x8000?(v&31)*255/31:(v&15)*17;
            sheik[c][y*24+x]=RGB(r,g,bl);sheik_alpha[c][y*24+x]=v&0x8000?32:(((v>>12)&7)*32+3)/7;
        }
    }sheik_ready=1;
done:free(raw);
}
int mp_bottom_art_init(const unsigned char* font,const char* path,const char* hud_path){
    mp_bottom_font_init(font);
    load_sheik(hud_path);
    FILE* f=fopen(path,"rb");if(!f)return 0;
    fseek(f,0,SEEK_END);long size=ftell(f);rewind(f);
    if(size<3550000||size>4*1024*1024){fclose(f);return 0;}
    unsigned char* raw=malloc(size);
    if(!raw){fclose(f);return 0;}
    if(fread(raw,1,size,f)!=(size_t)size){free(raw);fclose(f);return 0;}fclose(f);
    unsigned char* b=raw+32;unsigned length=be32(raw+4);
    if(length>(unsigned)size-32||3549180+PORTRAITS*4>length){free(raw);return 0;}
    portraits=malloc(PORTRAITS*PW*PH*2);opacity=malloc(PORTRAITS*PW*PH);
    if(!portraits||!opacity){free(raw);mp_bottom_art_exit();return 0;}
    for(unsigned i=0;i<PORTRAITS;++i){
        unsigned desc=be32(b+3549180+i*4);
        if(desc>length-24)goto bad;
        unsigned image=be32(b+desc),palette=image+136*188;
        if(be16(b+desc+4)!=136||be16(b+desc+6)!=188||be32(b+desc+8)!=9||
           image>length||length-image<136*188+528||be32(b+palette+512)!=palette||
           be32(b+palette+516)!=2||be16(b+palette+524)!=256)goto bad;
        for(unsigned y=0;y<PH;++y)for(unsigned x=0;x<PW;++x){
            unsigned rr=0,gg=0,bb=0,aa=0;
            /* Four premultiplied samples avoid dark outlines on transparency. */
            for(unsigned sy=0;sy<2;++sy)for(unsigned sx=0;sx<2;++sx){
                unsigned xx=(2*x+sx)*136/(PW*2),yy=(2*y+sy)*188/(PH*2);
                unsigned t=((yy/4)*17+xx/8)*32+(yy%4)*8+xx%8;
                unsigned v=be16(b+palette+2*b[image+t]);
                unsigned a=v&0x8000?255:(((v>>12)&7)*255+3)/7;
                unsigned r=v&0x8000?((v>>10)&31)*255/31:((v>>8)&15)*17;
                unsigned g=v&0x8000?((v>>5)&31)*255/31:((v>>4)&15)*17;
                unsigned bl=v&0x8000?(v&31)*255/31:(v&15)*17;
                aa+=a;rr+=r*a;gg+=g*a;bb+=bl*a;
            }
            unsigned at=(i*PH+y)*PW+x;
            if(aa){rr/=aa;gg/=aa;bb/=aa;}portraits[at]=RGB(rr,gg,bb);opacity[at]=(aa*32+510)/1020;
        }
    }
    free(raw);return 1;
bad:free(raw);mp_bottom_art_exit();return 0;
}
void mp_bottom_art_exit(void){free(portraits);free(opacity);portraits=NULL;opacity=NULL;}
static void portrait(int x,int y,int w,int h,const MPBottomPlayer* p,int dim){
    if(p->character==19&&sheik_ready){unsigned c=p->costume<5?p->costume:0;
        y+=(h-w)/2;
        unsigned columns[56];for(int xx=0;xx<w;++xx)columns[xx]=xx*24/w;
        for(int yy=0;yy<w;++yy){unsigned row=yy*24/w*24;
            for(int xx=0;xx<w;++xx){unsigned at=row+columns[xx],a=sheik_alpha[c][at];
                if(dim)a=a*10/32;if(a)pixel(x+xx,y+yy,sheik[c][at],a);}}
        return;
    }
    if(!portraits||p->character>=26){center(x+w/2,y+h/2-17,"?",36,muted);return;}
    unsigned index=portrait_index(p->character,p->costume)*PW*PH;
    unsigned columns[56];for(int xx=0;xx<w;++xx)columns[xx]=xx*PW/w;
    for(int yy=0;yy<h;++yy){unsigned row=index+(yy*PH/h)*PW;
        for(int xx=0;xx<w;++xx){unsigned at=row+columns[xx];
            unsigned a=opacity[at];if(dim)a=a*10/32;if(a)pixel(x+xx,y+yy,portraits[at],a);}}
}
static void background(void){
    if(backdrop_ready){memcpy(canvas,backdrop,sizeof(backdrop));return;}
    for(int x=0;x<320;++x)for(int y=0;y<240;++y){
        int glow=(320-x)*(240-y)/10000;canvas[x*240+239-y]=RGB(8+glow,15+glow,26+glow*2);
    }
    for(int x=8;x<320;x+=24)rect(x,31,1,180,RGB(20,32,45));
    for(int y=42;y<212;y+=24)rect(0,y,320,1,RGB(20,32,45));
    /* Melee's off-center circle/cross silhouette, kept below the content. */
    for(int y=0;y<190;++y)for(int x=150;x<320;++x){int dx=x-298,dy=y-90,d=dx*dx+dy*dy;
        if((d>7500&&d<8000)||(d<7800&&((x>272&&x<280)||(y>96&&y<102))))pixel(x,y,RGB(35,47,61),24);
    }
    memcpy(backdrop,canvas,sizeof(backdrop));backdrop_ready=1;
}
static void header(const char* title,const char* side){
    rect(0,0,320,29,RGB(10,16,25));slash(0,0,4,29,gold);
    fit(14,3,title,22,215,ink);right(309,10,side,12,gold);rect(0,29,320,2,gold);
}
static void footer(unsigned fps,unsigned show_fps,unsigned expanded,unsigned guide){
    rect(0,214,320,26,RGB(9,15,25));rect(0,214,320,1,RGB(80,98,111));
    char b[32];snprintf(b,sizeof(b),show_fps?"%u FPS":"FPS OFF",fps);
    center(51,220,b,14,show_fps?gold:muted);rect(103,219,1,15,RGB(56,70,88));
    center(160,220,expanded?"VIEW: WIDE":"VIEW: 4:3",13,ink);rect(214,219,1,15,RGB(56,70,88));
    center(269,220,guide?"CLOSE GUIDE":"CONTROLS",13,guide?gold:ink);
}
unsigned mp_bottom_hit(unsigned x,unsigned y){if(x>=320||y<215||y>=240)return 0;return x<104?MP_BOTTOM_FPS:x<215?MP_BOTTOM_VIEW:MP_BOTTOM_GUIDE;}
static void card(int x,int y,int w,int h,const MPBottomPlayer* p,unsigned slot,int battle,int stock,int stamina){
    uint16_t color=colors[p->color%4];int out=battle&&stock&&!p->stocks;
    rect(x,y,w,h,RGB(25,36,51));rect(x,y,3,h,p->kind==3?RGB(66,78,90):color);
    rect(x+3,y,w-3,1,RGB(70,85,103));
    if(p->kind==3){char id[12];snprintf(id,sizeof(id),"P%u",slot+1);text(x+11,y+6,id,14,muted);center(x+w/2,y+33,"CLOSED",17,muted);return;}
    int pw=w>200?56:44,ph=w>200?78:62;
    portrait(x+5,y+h-ph,pw,ph,p,out);
    int tx=x+pw+11;char b[32];snprintf(b,sizeof(b),p->kind==1?"P%u / CPU":"PLAYER %u",slot+1);
    text(tx,y+4,b,11,out?muted:color);
    const char* name=p->character<26?names[p->character]:"RANDOM";
    if(w>200){fit(tx,y+17,name,17,125,out?muted:ink);
        if(battle){snprintf(b,sizeof(b),stamina?"%u HP":"%u%%",p->damage>999?999:p->damage);
            right(x+w-10,y+17,out?"OUT":b,out?29:43,out?muted:p->damage>=100?gold:ink);}
    }else{fit(tx,y+16,name,12,w-pw-17,out?muted:ink);
        if(battle){snprintf(b,sizeof(b),stamina?"%u HP":"%u%%",p->damage>999?999:p->damage);
            fit(tx,y+30,out?"OUT":b,29,w-pw-17,out?muted:p->damage>=100?gold:ink);}
    }
    if(battle){
        if(stock){unsigned count=p->stocks>99?99:p->stocks;
            snprintf(b,sizeof(b),"STOCK %u",count);text(tx,y+h-17,b,11,muted);
            if(w>200)for(unsigned i=0;i<count&&i<8;++i)rect(tx+69+i*11,y+h-13,6,6,color);
        }else text(tx,y+h-17,stamina?"STAMINA":battle==2?"FREE PLAY":"TIME BATTLE",11,muted);
    }else text(tx,y+h-19,p->kind==1?"CPU FIGHTER":"A: SELECT",11,muted);
}
static void rules(const MPBottomState* s,int y){
    char b[70];snprintf(b,sizeof(b),s->stock_mode?"%u STOCK  /  %u MIN":"TIME BATTLE  /  %u MIN",s->stock_mode?s->rule_stocks:s->rule_minutes,s->rule_minutes);
    text(17,y,b,15,ink);text(17,y+20,s->items==255?"ITEMS OFF":"ITEMS ON",12,muted);
}
typedef struct { const char *title,*lead,*line1,*line2,*hint; unsigned rules; } MenuHelp;
/* MenuKind values from the original menu table. Advice describes this
 * screen, not a different mode; only Versus rule screens show VS defaults. */
static const MenuHelp menu_help[34]={
    [0]={"MAIN MENU","CHOOSE YOUR MODE","1-P: CHALLENGES AND PRACTICE","VS: BATTLE CPU OPPONENTS","CIRCLE PAD: CHOOSE   A: OPEN",0},
    [1]={"1-P MODE","PLAY AT YOUR OWN PACE","REGULAR MATCH: SOLO ADVENTURES","TRAINING: PRACTICE YOUR MOVES","A: OPEN   B: MAIN MENU",0},
    [2]={"VS MODE","YOUR MELEE RULES","MELEE: FIGHT CPU OPPONENTS","RULES: STOCKS, TIME AND ITEMS","A: OPEN   B: MAIN MENU",1},
    [3]={"TROPHIES","THE MELEE COLLECTION","GALLERY: TAKE A CLOSER LOOK","COLLECTION: VIEW YOUR TROPHIES","A: OPEN   B: MAIN MENU",0},
    [4]={"OPTIONS","MAKE YOURSELF AT HOME","ADJUST SOUND AND GAME SETTINGS","USE VIEW BELOW FOR 4:3 OR WIDE","A: OPEN   B: MAIN MENU",0},
    [5]={"DATA","EXPLORE MELEE","STATS AND CHARACTER ARCHIVES","SOUND TEST: PLAY THE SOUNDTRACK","A: OPEN   B: MAIN MENU",0},
    [6]={"REGULAR MATCH","TAKE ON A SOLO RUN","CLASSIC: A SERIES OF BATTLES","ADVENTURE: EXPLORE THE WORLDS","A: CHOOSE MODE   B: 1-P MODE",0},
    [7]={"EVENT MATCH","A CHALLENGE WITH A TWIST","EACH EVENT HAS ITS OWN OBJECTIVE","READ THE OBJECTIVE ON THE TOP SCREEN","A: CHOOSE EVENT   B: BACK",0},
    [9]={"STADIUM","CHASE A PERSONAL BEST","TARGET TEST AND HOME-RUN CONTEST","MULTI-MAN: SURVIVE THE CROWD","A: CHOOSE EVENT   B: 1-P MODE",0},
    [12]={"SPECIAL MELEE","CHANGE THE WAY YOU PLAY","TRY STAMINA, GIANT OR TINY MELEE","EACH MODE ADDS ITS OWN BATTLE TWIST","A: CHOOSE MODE   B: VS MODE",0},
    [13]={"MATCH RULES","YOUR MELEE RULES","CHOOSE TIME OR STOCK BATTLES","ITEM SWITCH: FREQUENCY AND TYPES","LEFT / RIGHT: CHANGE   B: BACK",1},
    [15]={"MORE RULES","REFINE YOUR MATCH","STOCK TIMER: LIMIT MATCH LENGTH","PAUSE: ALLOW START TO PAUSE","LEFT / RIGHT: CHANGE   B: BACK",0},
    [16]={"ITEM SWITCH","SET THE ITEM POOL","ITEM FREQUENCY IS THE TOP ROW","OFF STOPS RANDOM ITEM SPAWNS","CHOOSE AN ITEM TO ENABLE OR DISABLE IT",0},
    [17]={"RANDOM STAGES","BUILD YOUR RANDOM POOL","CHOOSE THE STAGES RANDOM MAY PICK","MANUAL PICKS STILL WORK","A: TOGGLE STAGE   B: BACK",0},
    [18]={"NAME ENTRY","MAKE YOUR MARK","CREATE OR MANAGE YOUR PLAYER TAGS","NAMES LAST UNTIL YOU CLOSE THE APP","FOLLOW THE TOP-SCREEN PROMPTS",0},
    [19]={"RUMBLE","CONTROLLER FEEDBACK","THE NEW 3DS HAS NO RUMBLE MOTOR","THIS OPTION DOES NOT ADD VIBRATION","B: OPTIONS",0},
    [20]={"SOUND","TUNE YOUR AUDIO","ADJUST MUSIC AND SOUND EFFECTS","USE THE CONSOLE VOLUME FOR OVERALL LEVEL","FOLLOW THE TOP-SCREEN PROMPTS",0},
    [21]={"DISPLAY","PICTURE SETTINGS","MELEE USES THE NATIVE LCD RESOLUTION","TAP VIEW BELOW TO CHANGE SCREEN WIDTH","B: OPTIONS",0},
    [23]={"LANGUAGE","CHOOSE A LANGUAGE","CHANGES THE ORIGINAL GAME'S LANGUAGE","BOTTOM-SCREEN GUIDANCE STAYS IN ENGLISH","FOLLOW THE TOP-SCREEN PROMPTS",0},
    [24]={"ERASE DATA","MANAGE GAME DATA","THIS BUILD HAS NO MEMORY-CARD SAVING","CLOSING THE APP RESETS SESSION SETTINGS","FOLLOW THE TOP-SCREEN PROMPTS",0},
    [25]={"SNAPSHOTS","MELEE MOMENTS","THIS MODE USES GAMECUBE MEMORY CARDS","MEMORY-CARD SAVING IS NOT AVAILABLE","FOLLOW THE TOP-SCREEN PROMPTS",0},
    [26]={"ARCHIVES","MEET THE CAST","EXPLORE THE ORIGINAL CHARACTER ARCHIVES","USE THE TOP SCREEN TO BROWSE","FOLLOW THE TOP-SCREEN PROMPTS",0},
    [28]={"RECORDS","LOOK BACK AT YOUR PLAY","VIEW MATCH STATS AND BONUS RECORDS","RECORDS ARE NOT SAVED BETWEEN LAUNCHES","A: OPEN   B: DATA",0},
    [29]={"SPECIAL MOVIE","THE ORIGINAL SHOWCASE","PLAY THE ORIGINAL MELEE PRESENTATION","DISPLAY CONTROLS REMAIN BELOW","FOLLOW THE TOP-SCREEN PROMPTS",0},
    [30]={"VS RECORDS","MATCH HISTORY","BROWSE YOUR VERSUS STATISTICS","RECORDS ARE NOT SAVED BETWEEN LAUNCHES","FOLLOW THE TOP-SCREEN PROMPTS",0},
    [31]={"BONUS RECORDS","BONUS COLLECTION","BROWSE THE GAME'S BONUS AWARDS","RECORDS ARE NOT SAVED BETWEEN LAUNCHES","FOLLOW THE TOP-SCREEN PROMPTS",0},
    [32]={"MISC. RECORDS","BY THE NUMBERS","VIEW YOUR PLAY STATISTICS","RECORDS ARE NOT SAVED BETWEEN LAUNCHES","FOLLOW THE TOP-SCREEN PROMPTS",0},
    [33]={"MULTI-MAN MELEE","TAKE ON THE CROWD","CHOOSE COUNT, TIME OR ENDLESS","CRUEL MELEE PUTS SURVIVAL TO THE TEST","A: CHOOSE CHALLENGE   B: STADIUM",0}
};
static const char* mode_title(unsigned mode){
    switch(mode){case 3:return "CLASSIC";case 4:return "ADVENTURE";case 5:return "ALL-STAR";
    case 15:return "TARGET TEST";case 28:return "TRAINING";case 32:return "HOME-RUN";
    case 43:return "EVENT";default:return "MELEE";}
}
static void menu_page(const MPBottomState* s){
    static const MenuHelp fallback={"MELEE","READY WHEN YOU ARE","FOLLOW THE PROMPTS ON THE TOP SCREEN","TOUCH CONTROLS ARE AVAILABLE BELOW","VIEW: SCREEN WIDTH   CONTROLS: BUTTON GUIDE",0};
    const MenuHelp* m=s->scene==1&&s->menu<34&&menu_help[s->menu].title?&menu_help[s->menu]:&fallback;
    header(m->title,"3DS");fit(17,44,m->lead,22,286,gold);
    if(m->rules)rules(s,76);
    else{rect(17,82,38,2,gold);text(17,97,"SUPER SMASH BROS. MELEE",12,muted);}
    rect(17,122,286,1,RGB(74,88,102));
    fit(17,138,m->line1,15,286,ink);fit(17,163,m->line2,14,286,muted);
    fit(17,195,m->hint,12,286,gold);
}
static void guide_page(void){
    header("CONTROLS","NEW 3DS");
    static const char* labels[]={"CIRCLE PAD","A / B","X / Y","L / R","ZL / ZR","C STICK","START","SELECT"};
    static const char* actions[]={"MOVE","ATTACK / SPECIAL","JUMP","SHIELD","GRAB","DIRECTIONAL ATTACK","CONFIRM / PAUSE","HOMEBREW LAUNCHER"};
    for(int i=0;i<8;++i){int y=39+i*19;text(15,y,labels[i],13,gold);fit(122,y,actions[i],13,187,ink);}
    text(15,195,"THE CONTROLS GUIDE DOES NOT PAUSE PLAY",11,muted);
}
void mp_bottom_loading(uint16_t* pixels,const char* message){
    canvas=pixels;background();header("SUPER SMASH BROS.","3DS");
    center(160,70,"MELEE",63,ink);rect(60,134,200,2,gold);center(160,152,message,15,gold);
    center(160,207,"NATIVE NINTENDO 3DS PORT",11,muted);
}
void mp_bottom_draw(uint16_t* pixels,const MPBottomState* s,unsigned fps,unsigned show_fps,unsigned expanded,unsigned guide){
    canvas=pixels;background();
    int battle=s->scene==2||s->scene==3||s->scene==4||s->scene==44;
    if(guide)guide_page();
    else if(battle||s->scene==5){
        char timer[24];if(s->timer)snprintf(timer,sizeof(timer),"%u:%02u",s->seconds/60,s->seconds%60);else snprintf(timer,sizeof(timer),"%s",s->scene==4?"FREE PLAY":s->teams?"TEAMS":"FREE FOR ALL");
        header(s->scene==5?"MATCH COMPLETE":s->scene==4?"TRAINING":s->scene==3?"SUDDEN DEATH":"MELEE",s->scene==5?"RESULTS":timer);
        unsigned count=0;for(unsigned i=0;i<4;++i)if(s->players[i].kind!=3)++count;
        if(count<=2){unsigned row=0;for(unsigned i=0;i<4;++i)if(s->players[i].kind!=3)card(8,38+(row++)*86,304,80,&s->players[i],i,s->scene==4?2:1,s->stock_mode,s->stamina);
            if(!count)center(160,100,"GET READY",28,gold);
        }else for(unsigned i=0;i<4;++i)card(8+(i%2)*156,38+(i/2)*86,148,80,&s->players[i],i,1,s->stock_mode,s->stamina);
    }else if(s->scene==8){
        header("SELECT YOUR FIGHTER",mode_title(s->mode));
        for(unsigned i=0;i<4;++i)card(8+(i%2)*156,38+(i/2)*76,148,70,&s->players[i],i,0,0,0);
        center(160,195,"A: SELECT    B: BACK    START: NEXT",12,gold);
    }else if(s->scene==9){
        header("SELECT A STAGE",mode_title(s->mode));
        center(160,47,"SET THE STAGE",28,ink);
        if(s->mode==2)rules(s,84);
        else center(160,88,s->mode==28?"CHOOSE YOUR PRACTICE ARENA":"CHOOSE YOUR BATTLEFIELD",15,gold);
        text(17,133,"CIRCLE PAD: CHOOSE STAGE",15,ink);text(17,157,"A: CONFIRM     B: FIGHTERS",15,gold);
        text(17,191,"DISPLAY OPTIONS ARE ALWAYS BELOW",11,muted);
    }else if(s->scene==0||s->scene==255){
        mp_bottom_loading(pixels,s->scene==0?"PRESS START":"PREPARING MELEE");
        rect(0,190,320,24,RGB(10,18,29));center(160,197,"ALL FIGHTERS UNLOCKED  /  UCF 0.84",11,muted);
    }else if(s->scene==42){
        header("WELCOME TO MELEE","3DS");center(160,49,"READY TO PLAY",29,ink);
        text(19,102,"A: CONTINUE PAST THE CARD PROMPT",14,gold);
        text(19,139,"ALL FIGHTERS ARE ALREADY UNLOCKED",12,ink);
        text(19,161,"SETTINGS LAST UNTIL YOU CLOSE THE APP",11,muted);
    }else menu_page(s);
    footer(fps,show_fps,expanded,guide);
}
