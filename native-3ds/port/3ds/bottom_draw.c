/* A small CPU compositor for the touch screen. Original artwork is decoded
 * once, independently of GX textures, GPU state and gameplay allocations. */
#include "bottom.h"
#include "controls.h"
#include "name_text.h"
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
/* Every fighter's stock icons (IfAll.usd: 130 entries, 24 x 24); stock_icon() maps fighter x costume to an entry. */
#define STOCK_ENTRIES 130
static uint16_t stock[STOCK_ENTRIES][24*24];
static unsigned char stock_alpha[STOCK_ENTRIES][24*24];
static unsigned stocks_ready;
static unsigned char glyphs[95][32*32], glyph_left[95], glyph_width[95];
/* The whole font (287 glyphs, 4-bit tiles; static game data) for names: kana and the rest (name_text.c) */
static const unsigned char* atlas;
static unsigned char atlas_left[287], atlas_width[287];
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
    case '&':return 261;case '(':return 245;case ')':return 246;case '=':return 254;
    case ',':return 230;case ';':return 234;case '\'':return 243;case '"':return 244;
    case '[':return 247;case ']':return 248;case '#':return 260;case '*':return 262;default:return 227;}
}
static unsigned atlas_alpha(unsigned g,unsigned x,unsigned y){
    unsigned at=((y/8)*4+x/8)*64+(y%8)*8+x%8;return (atlas[512*g+at/2]>>(at&1?0:4))&15;
}
void mp_bottom_font_init(const unsigned char* font){
    atlas=font;
    for(unsigned g=0;g<287;++g){
        unsigned left=31,right=0;
        for(unsigned y=0;y<32;++y)for(unsigned x=0;x<32;++x)if(atlas_alpha(g,x,y)>2){if(x<left)left=x;if(x>right)right=x;}
        atlas_left[g]=left<=right?left:0;atlas_width[g]=left<=right?right-left+1:12;
    }
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
/* Text is UTF-8: ASCII draws from the copied glyphs; any other character goes through the shared name converter
 * (name_text.c: a kana's glyph, an accented letter's base letter, else "？") and draws from the atlas. A "unit" is an
 * ASCII character (32..126) or 0x100 + an atlas index. */
#define MAX_UNITS 96
static int units(const char* s,unsigned* out){
    int n=0;
    while(*s&&n<MAX_UNITS){
        uint32_t cp=name_utf8_next(&s);
        if(cp>=32&&cp<127){out[n++]=cp;continue;}
        if(cp<0x80)continue;
        char b[8];int k=name_char_to_game(cp,b);
        for(int i=0;i<k&&n<MAX_UNITS;){
            unsigned c=(unsigned char)b[i];
            if(c<0x80){if(c>=32&&c<127)out[n++]=c;++i;continue;}
            if(i+1<k){int g=name_sjis_to_atlas(c<<8|(unsigned char)b[i+1]);if(g>=0&&atlas)out[n++]=0x100+g;}
            i+=2;
        }
    }
    return n;
}
static unsigned advance(unsigned u,int h){
    if(u>=0x100)return (atlas_width[u-0x100]*h+31)/32+1;
    return u==' '?h/3:(glyph_width[u-32]*h+31)/32+1;
}
static int width(const char* s,int h){unsigned u[MAX_UNITS];int n=units(s,u),w=0;for(int i=0;i<n;++i)w+=advance(u[i],h);return w;}
static void text(int x,int y,const char* s,int h,uint16_t color){
    unsigned rows[64];for(int yy=0;yy<h;++yy)rows[yy]=(yy*32/h)*32;
    unsigned u[MAX_UNITS];int n=units(s,u);
    for(int k=0;k<n;++k){unsigned ch=u[k];
        if(ch>=0x100){unsigned g=ch-0x100,w=(atlas_width[g]*h+31)/32;
            for(int yy=0;yy<h;++yy)for(unsigned xx=0;xx<w;++xx){
                unsigned sx=atlas_left[g]+xx*32/h,sy=(unsigned)yy*32/h;
                if(sx<32){unsigned a=atlas_alpha(g,sx,sy);if(a)pixel(x+xx,y+yy,color,(a*32+7)/15);}
            }
        }else if(ch!=' '){unsigned i=ch-32,w=(glyph_width[i]*h+31)/32;
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
/* IfAll.usd's stock icon table (tools/dump_stock_icons.py shows it): costumes 0..3 are entries costume * 26 + the
 * fighter's place, which is its external id except that Sheik (19) comes last (25); the fifth and sixth costumes of the
 * fighters that have them follow from 104. -1: no such costume. */
static int stock_icon(unsigned fighter,unsigned costume){
    static const signed char fifth[26]={104,105,-1,-1,106,-1,107,-1,108,125,-1,-1,122,-1,-1,126,109,110,111,114,-1,112,113,128,-1,127};
    static const signed char sixth[26]={115,-1,-1,-1,116,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,117,-1,-1,-1,-1,-1,-1,-1,-1};
    if(fighter>=26)return -1;
    if(costume<4)return (int)costume*26+(fighter<19?(int)fighter:fighter==19?25:(int)fighter-1);
    return costume==4?fifth[fighter]:costume==5?sixth[fighter]:-1;
}
static void load_stocks(const char* path){
    FILE* f=fopen(path,"rb");if(!f)return;
    fseek(f,0,SEEK_END);long size=ftell(f);rewind(f);
    if(size<800000||size>2*1024*1024){fclose(f);return;}
    unsigned char* raw=malloc(size);if(!raw){fclose(f);return;}
    if(fread(raw,1,size,f)!=(size_t)size){free(raw);fclose(f);return;}fclose(f);
    unsigned char* b=raw+32;unsigned n=be32(raw+4);
    if(n>(unsigned)size-32||n<775156)goto done;
    for(unsigned c=0;c<STOCK_ENTRIES;++c){
        unsigned d=be32(b+774116+c*4),p=be32(b+774636+c*4);
        if(d+24>n||p+16>n)continue;
        unsigned im=be32(b+d),pal=be32(b+p);
        if(be16(b+d+4)!=24||be16(b+d+6)!=24||be32(b+d+8)!=8||
            be32(b+p+4)!=2||be16(b+p+12)!=16||im+288>n||pal+32>n)continue;   /* not a stock icon (a few entries) */
        for(unsigned y=0;y<24;++y)for(unsigned x=0;x<24;++x){
            unsigned t=((y/8)*3+x/8)*64+(y%8)*8+x%8;
            unsigned v=be16(b+pal+2*((b[im+t/2]>>(t&1?0:4))&15));
            unsigned r=v&0x8000?((v>>10)&31)*255/31:((v>>8)&15)*17;
            unsigned g=v&0x8000?((v>>5)&31)*255/31:((v>>4)&15)*17;
            unsigned bl=v&0x8000?(v&31)*255/31:(v&15)*17;
            stock[c][y*24+x]=RGB(r,g,bl);stock_alpha[c][y*24+x]=v&0x8000?32:(((v>>12)&7)*32+3)/7;
        }
    }
    /* every mapped fighter and costume decoded: Sheik's five check the reading */
    stocks_ready=1;
    for(unsigned f=0;f<26;++f)for(unsigned c=0;c<6;++c){int e=stock_icon(f,c);
        if(e>=0){unsigned opaque=0;for(unsigned i=0;i<24*24;++i)opaque+=stock_alpha[e][i]>16;if(!opaque)stocks_ready=0;}}
done:free(raw);
}
int mp_bottom_art_init(const unsigned char* font,const char* path,const char* hud_path){
    mp_bottom_font_init(font);
    load_stocks(hud_path);
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
    if(p->character==19&&stocks_ready){unsigned c=(unsigned)stock_icon(19,p->costume<5?p->costume:0);
        y+=(h-w)/2;
        unsigned columns[56];for(int xx=0;xx<w;++xx)columns[xx]=xx*24/w;
        for(int yy=0;yy<w;++yy){unsigned row=yy*24/w*24;
            for(int xx=0;xx<w;++xx){unsigned at=row+columns[xx],a=stock_alpha[c][at];
                if(dim)a=a*10/32;if(a)pixel(x+xx,y+yy,stock[c][at],a);}}
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
/* Four 80-pixel buttons: FPS counter, frame-rate mode, view, controls. */
static void footer(unsigned fps,unsigned show_fps,unsigned expanded,unsigned rate,unsigned guide){
    static const char*const rates[]={"AUTO 60","AUTO 30","30 LOCK","30 LOCK","60 MAX","60 MAX"};
    rect(0,214,320,26,RGB(9,15,25));rect(0,214,320,1,RGB(80,98,111));
    char b[32];snprintf(b,sizeof(b),show_fps?"%u FPS":"FPS OFF",fps);
    center(40,220,b,13,show_fps?gold:muted);rect(79,219,1,15,RGB(56,70,88));
    center(120,220,rates[rate<6?rate:0],13,ink);rect(159,219,1,15,RGB(56,70,88));
    center(200,220,expanded?"WIDE":"4:3",13,ink);rect(239,219,1,15,RGB(56,70,88));
    extern volatile unsigned mp_native_camera_mode,mp_native_camera_control;
    if(mp_native_camera_mode)center(280,220,mp_native_camera_control?"CAMERA ON":"CAMERA",13,mp_native_camera_control?gold:ink);
    else center(280,220,guide?"CLOSE":"CONTROLS",13,guide?gold:ink);
}
unsigned mp_bottom_hit(unsigned x,unsigned y){if(x>=320||y<215||y>=240)return 0;return x<80?MP_BOTTOM_FPS:x<160?MP_BOTTOM_RATE:x<240?MP_BOTTOM_VIEW:MP_BOTTOM_GUIDE;}
/* Slippi Direct matches: the players' display names (bottom_draw.c's online part). */
static const char* card_labels[4];
static void card(int x,int y,int w,int h,const MPBottomPlayer* p,unsigned slot,int battle,int stock,int stamina){
    uint16_t color=colors[p->color%4];int out=battle&&stock&&!p->stocks;
    rect(x,y,w,h,RGB(25,36,51));rect(x,y,3,h,p->kind==3?RGB(66,78,90):color);
    rect(x+3,y,w-3,1,RGB(70,85,103));
    if(p->kind==3){char id[12];snprintf(id,sizeof(id),"P%u",slot+1);text(x+11,y+6,id,14,muted);center(x+w/2,y+33,"CLOSED",17,muted);return;}
    int pw=w>200?56:44,ph=w>200?78:62;
    portrait(x+5,y+h-ph,pw,ph,p,out);
    int tx=x+pw+11;char b[48];snprintf(b,sizeof(b),p->kind==1?"P%u / CPU":"PLAYER %u",slot+1);
    if(card_labels[slot])snprintf(b,sizeof(b),"P%u  %s",slot+1,card_labels[slot]);
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
    [4]={"OPTIONS","MAKE YOURSELF AT HOME","ADJUST SOUND AND GAME SETTINGS","TAP 4:3/WIDE BELOW FOR SCREEN WIDTH","A: OPEN   B: MAIN MENU",0},
    [5]={"DATA","EXPLORE MELEE","RECORDS, SNAPSHOTS AND SECRETS","SOUND TEST: PLAY THE SOUNDTRACK","A: OPEN   B: MAIN MENU",0},
    [6]={"REGULAR MATCH","TAKE ON A SOLO RUN","CLASSIC: A SERIES OF BATTLES","ADVENTURE: EXPLORE THE WORLDS","A: CHOOSE MODE   B: 1-P MODE",0},
    [7]={"EVENT MATCH","A CHALLENGE WITH A TWIST","EACH EVENT HAS ITS OWN OBJECTIVE","READ THE OBJECTIVE ON THE TOP SCREEN","A: CHOOSE EVENT   B: BACK",0},
    [9]={"STADIUM","CHASE A PERSONAL BEST","TARGET TEST AND HOME-RUN CONTEST","MULTI-MAN: SURVIVE THE CROWD","A: CHOOSE EVENT   B: 1-P MODE",0},
    [11]={"MELEE","SOLO OR GROUP","SOLO: PLAY ON THIS CONSOLE","GROUP: PLAY WITH NEARBY 3DS CONSOLES","A: CHOOSE   B: VS MODE",0},
    [12]={"SPECIAL MELEE","CHANGE THE WAY YOU PLAY","TRY STAMINA, GIANT OR TINY MELEE","EACH MODE ADDS ITS OWN BATTLE TWIST","A: CHOOSE MODE   B: VS MODE",0},
    [13]={"MATCH RULES","YOUR MELEE RULES","CHOOSE TIME OR STOCK BATTLES","ITEM SWITCH: FREQUENCY AND TYPES","LEFT / RIGHT: CHANGE   B: BACK",1},
    [15]={"MORE RULES","REFINE YOUR MATCH","STOCK TIMER: LIMIT MATCH LENGTH","PAUSE: ALLOW START TO PAUSE","LEFT / RIGHT: CHANGE   B: BACK",0},
    [16]={"ITEM SWITCH","SET THE ITEM POOL","ITEM FREQUENCY IS THE TOP ROW","OFF STOPS RANDOM ITEM SPAWNS","CHOOSE AN ITEM TO ENABLE OR DISABLE IT",0},
    [17]={"RANDOM STAGES","BUILD YOUR RANDOM POOL","CHOOSE THE STAGES RANDOM MAY PICK","MANUAL PICKS STILL WORK","A: TOGGLE STAGE   B: BACK",0},
    [18]={"NAME ENTRY","MAKE YOUR MARK","CREATE OR MANAGE YOUR PLAYER TAGS","YOUR TAGS ARE SAVED WITH YOUR GAME","FOLLOW THE TOP-SCREEN PROMPTS",0},
    [19]={"RUMBLE","CONTROLLER FEEDBACK","THE NEW 3DS HAS NO RUMBLE MOTOR","THIS OPTION DOES NOT ADD VIBRATION","B: OPTIONS",0},
    [20]={"SOUND","TUNE YOUR AUDIO","ADJUST MUSIC AND SOUND EFFECTS","USE THE CONSOLE VOLUME FOR OVERALL LEVEL","FOLLOW THE TOP-SCREEN PROMPTS",0},
    [21]={"DISPLAY","PICTURE SETTINGS","MELEE USES THE NATIVE LCD RESOLUTION","TAP 4:3/WIDE BELOW TO CHANGE WIDTH","B: OPTIONS",0},
    [23]={"LANGUAGE","CHOOSE A LANGUAGE","CHANGES THE ORIGINAL GAME'S LANGUAGE","BOTTOM-SCREEN GUIDANCE STAYS IN ENGLISH","FOLLOW THE TOP-SCREEN PROMPTS",0},
    [24]={"ERASE DATA","A FRESH START","CLEAR UNLOCKS, SCORES OR TROPHIES","OR START OVER COMPLETELY","FOLLOW THE TOP-SCREEN PROMPTS",0},
    [25]={"SNAPSHOTS","MELEE MOMENTS","BROWSE PHOTOS TAKEN IN CAMERA MODE","PHOTOS ARE KEPT ON THE SD CARD","FOLLOW THE TOP-SCREEN PROMPTS",0},
    [26]={"ARCHIVES","MELEE ON FILM","SPECIAL MOVIE AND HOW TO PLAY","MOVIES DON'T PLAY ON THE 3DS","B: DATA",0},
    [27]={"SOUND TEST","LISTEN IN","MUSIC FROM ACROSS THE SERIES","PLUS VOICES AND SOUND EFFECTS","A: PLAY   B: DATA",0},
    [28]={"RECORDS","LOOK BACK AT YOUR PLAY","VS, BONUS AND MISC. RECORDS","SEE HOW YOUR PLAY STACKS UP","A: OPEN   B: DATA",0},
    [29]={"SPECIAL","SECRETS UNCOVERED","A LOG OF EVERYTHING YOU'VE UNLOCKED","NEW FIGHTERS, STAGES AND MORE","FOLLOW THE TOP-SCREEN PROMPTS",0},
    [30]={"VS RECORDS","MATCH HISTORY","BROWSE YOUR VERSUS STATISTICS","KOS AND FALLS FOR EVERY FIGHTER","FOLLOW THE TOP-SCREEN PROMPTS",0},
    [31]={"BONUS RECORDS","BONUS COLLECTION","BROWSE THE GAME'S BONUS AWARDS","HOW MANY HAVE YOU EARNED?","FOLLOW THE TOP-SCREEN PROMPTS",0},
    [32]={"MISC. RECORDS","BY THE NUMBERS","VIEW YOUR PLAY STATISTICS","PLAY TIME, MATCHES AND MORE","FOLLOW THE TOP-SCREEN PROMPTS",0},
    [33]={"MULTI-MAN MELEE","TAKE ON THE CROWD","CHOOSE COUNT, TIME OR ENDLESS","CRUEL MELEE PUTS SURVIVAL TO THE TEST","A: CHOOSE CHALLENGE   B: STADIUM",0}
};
static const char* mode_title(unsigned mode){
    switch(mode){case 3:return "CLASSIC";case 4:return "ADVENTURE";case 5:return "ALL-STAR";
    case 15:return "TARGET TEST";case 28:return "TRAINING";case 32:return "HOME-RUN";
    case 43:return "EVENT";default:return "MELEE";}
}
static void menu_page(const MPBottomState* s){
    static const MenuHelp fallback={"MELEE","READY WHEN YOU ARE","FOLLOW THE PROMPTS ON THE TOP SCREEN","TOUCH CONTROLS ARE AVAILABLE BELOW","AUTO/30/60: FRAME RATE   CONTROLS: GUIDE",0};
    const MenuHelp* m=s->scene==1&&s->menu<34&&menu_help[s->menu].title?&menu_help[s->menu]:&fallback;
    header(m->title,s->profile?"FRESH SAVE":"3DS");fit(17,44,m->lead,22,286,gold);
    if(m->rules)rules(s,76);
    else if(s->scene==1&&s->fighters){
        /* Save progress: what this save has unlocked so far. */
        char b[48];rect(17,78,38,2,gold);
        snprintf(b,sizeof(b),"FIGHTERS %u/25",s->fighters);text(17,87,b,12,s->fighters==25?gold:ink);
        snprintf(b,sizeof(b),"STAGES %u/29",s->stages);text(165,87,b,12,s->stages==29?gold:ink);
        snprintf(b,sizeof(b),"TROPHIES %u/293",s->trophies);text(17,104,b,12,s->trophies==293?gold:ink);
        snprintf(b,sizeof(b),"EVENTS %u/51",s->events);text(165,104,b,12,s->events==51?gold:ink);
    }
    else{rect(17,82,38,2,gold);text(17,97,"SUPER SMASH BROS. MELEE",12,muted);}
    rect(17,122,286,1,RGB(74,88,102));
    fit(17,138,m->line1,15,286,ink);fit(17,163,m->line2,14,286,muted);
    /* Fresh save: how to unlock the next fighter (vanilla US conditions),
     * in place of the main menu's button hint. */
    static const char*const unlock_hints[11]={
        "NEXT: JIGGLYPUFF - CLEAR CLASSIC WITH ANY FIGHTER",
        "NEXT: DR. MARIO - CLEAR CLASSIC AS MARIO",
        "NEXT: FALCO - CLEAR 100-MAN MELEE",
        "NEXT: YOUNG LINK - CLEAR CLASSIC WITH 10 FIGHTERS",
        "NEXT: PICHU - CLEAR EVENT 37",
        "NEXT: GANONDORF - CLEAR EVENT 29",
        "NEXT: LUIGI - FINISH ADVENTURE 1-1 AT :X2 ON THE TIMER",
        "NEXT: MARTH - CLEAR CLASSIC OR ADVENTURE WITH ALL 14 STARTERS",
        "NEXT: ROY - CLEAR CLASSIC AS MARTH",
        "NEXT: MEWTWO - 20 HOURS OF VS PLAY OR 700 VS MATCHES",
        "NEXT: MR. GAME & WATCH - CLEAR CLASSIC WITH EVERY FIGHTER"};
    if(s->profile&&s->scene==1&&s->menu==0&&s->locked){
        unsigned i=0;while(!(s->locked&(1u<<i)))++i;
        fit(17,195,unlock_hints[i],12,286,gold);
    }else fit(17,195,m->hint,12,286,gold);
}
/* CONTROLS: a GUIDE tab (what each button does, following the custom
 * mapping) and a CUSTOMIZE tab (in-match button mapping, tap jump, reset).
 * Tapping a button there opens an action picker. Touches:
 * mp_bottom_guide_touch; bottom.c resets the page when it closes. */
extern unsigned mp_native_tap_jump(void);
extern void mp_native_set_tap_jump(unsigned);
static unsigned guide_tab,picker=MP_BUTTONS;
#ifdef MP_SMOKE_TEST
unsigned mp_bottom_controls_state; /* tab | (picked button + 1) << 8 */
#endif
static const uint16_t card_color=RGB(25,36,51),line_color=RGB(56,70,88),dark=RGB(10,16,25);
enum{TABS_Y=5,TABS_H=19,GUIDE_X=150,GUIDE_W=64,CUSTOM_X=218,CUSTOM_W=94,
     EDIT_Y=182,EDIT_H=27,CELL_Y=35,CELL_STEP=25,CELL_W=149,CELL_H=22,
     OPTIONS_Y=187,OPTIONS_H=24,TAP_W=222,RESET_X=236,RESET_W=76,
     PANEL_X=18,PANEL_Y=40,PANEL_W=284,PANEL_H=166,PICK_X=30,PICK_Y=72,PICK_W=126,PICK_H=26};
/* Picker order; the last option cancels. */
static const unsigned char picks[7]={MP_ACTION_ATTACK,MP_ACTION_SPECIAL,MP_ACTION_JUMP,MP_ACTION_SHIELD,
    MP_ACTION_GRAB,MP_ACTION_TAUNT,MP_ACTION_NONE};
static int inside(unsigned x,unsigned y,int rx,int ry,int rw,int rh){return (int)x>=rx&&(int)x<rx+rw&&(int)y>=ry&&(int)y<ry+rh;}
static void pill(int x,int y,int w,int h,const char* s,int size,int active){
    rect(x,y,w,h,active?gold:card_color);center(x+w/2,y+(h-size)/2,s,size,active?dark:ink);
}
static void controls_header(void){
    rect(0,0,320,29,dark);slash(0,0,4,29,gold);fit(14,3,"CONTROLS",22,128,ink);
    pill(GUIDE_X,TABS_Y,GUIDE_W,TABS_H,"GUIDE",12,guide_tab==0);
    pill(CUSTOM_X,TABS_Y,CUSTOM_W,TABS_H,"CUSTOMIZE",12,guide_tab==1);
    rect(0,29,320,2,gold);
}
static void cell_position(unsigned button,int* x,int* y){*x=8+(int)(button/6)*155;*y=CELL_Y+(int)(button%6)*CELL_STEP;}
static void guide_tab_page(void){
    /* One row per action, listing the buttons that do it in a match. */
    static const unsigned char rows[6]={MP_ACTION_ATTACK,MP_ACTION_SPECIAL,MP_ACTION_JUMP,MP_ACTION_SHIELD,MP_ACTION_GRAB,MP_ACTION_TAUNT};
    /* Button lists fit a 106-pixel column; actions start at x=126. */
    fit(15,36,"CIRCLE PAD",12,106,gold);text(126,36,"MOVE",12,ink);
    for(int i=0;i<6;++i){
        char list[64];int n=0;list[0]=0;
        for(unsigned b=0;b<MP_BUTTONS;++b)if(mp_native_button_action(b)==rows[i]&&n<(int)sizeof(list)-12)
            n+=snprintf(list+n,sizeof(list)-n,n?" / %s":"%s",mp_button_labels[b]);
        int y=52+i*16;
        fit(15,y,n?list:"NOT SET",12,106,n?gold:muted);text(126,y,mp_action_labels[rows[i]],12,ink);
        if(rows[i]==MP_ACTION_JUMP)right(309,y+1,mp_native_tap_jump()?"TAP JUMP ON":"TAP JUMP OFF",11,muted);
    }
    fit(15,148,"C STICK",12,106,gold);text(126,148,"DIRECTIONAL ATTACK",12,ink);
    fit(15,164,"START / SELECT",12,106,gold);text(126,164,"PAUSE / EXIT GAME",12,ink);
    rect(8,EDIT_Y,304,EDIT_H,card_color);rect(8,EDIT_Y,3,EDIT_H,gold);
    text(18,EDIT_Y+8,"CUSTOMIZE BUTTONS",12,gold);
    right(254,EDIT_Y+9,"IN MATCHES",10,muted);
    pill(262,EDIT_Y+4,44,EDIT_H-8,"EDIT",12,1);
}
static void customize_tab_page(void){
    int custom=0;
    for(unsigned b=0;b<MP_BUTTONS;++b){
        int x,y;cell_position(b,&x,&y);
        unsigned action=mp_native_button_action(b),changed=action!=mp_native_default_action(b);
        custom|=changed;
        rect(x,y,CELL_W,CELL_H,card_color);rect(x,y,3,CELL_H,changed?gold:line_color);
        text(x+10,y+5,mp_button_labels[b],12,gold);
        right(x+CELL_W-8,y+5,mp_action_labels[action],12,action==MP_ACTION_NONE?muted:ink);
    }
    unsigned on=mp_native_tap_jump();
    rect(8,OPTIONS_Y,TAP_W,OPTIONS_H,card_color);rect(8,OPTIONS_Y,3,OPTIONS_H,gold);
    text(18,OPTIONS_Y+6,"TAP JUMP",12,gold);
    pill(8+TAP_W-48,OPTIONS_Y+3,44,OPTIONS_H-6,on?"ON":"OFF",12,on);
    rect(RESET_X,OPTIONS_Y,RESET_W,OPTIONS_H,card_color);
    center(RESET_X+RESET_W/2,OPTIONS_Y+6,"RESET",12,custom?ink:muted);
}
static void picker_panel(void){
    /* Dim the page beneath, between the header and the footer. */
    for(int x=0;x<320;++x)for(int y=31;y<214;++y){uint16_t* at=&canvas[x*240+239-y];*at=blend(*at,RGB(3,6,11),11);} /* 11/32 of the page shows */
    rect(PANEL_X,PANEL_Y,PANEL_W,PANEL_H,RGB(14,22,34));rect(PANEL_X,PANEL_Y,PANEL_W,2,gold);
    char title[32];snprintf(title,sizeof(title),"%s BUTTON",mp_button_labels[picker]);
    text(PICK_X,PANEL_Y+8,title,15,gold);right(PANEL_X+PANEL_W-12,PANEL_Y+11,"IN MATCHES",11,muted);
    unsigned current=mp_native_button_action(picker);
    for(int i=0;i<8;++i){
        int x=PICK_X+(i%2)*134,y=PICK_Y+(i/2)*32;
        if(i<7)pill(x,y,PICK_W,PICK_H,mp_action_labels[picks[i]],13,picks[i]==current);
        else{rect(x,y,PICK_W,PICK_H,dark);center(x+PICK_W/2,y+6,"CANCEL",13,muted);}
    }
}
static void guide_page(void){
    controls_header();
    if(guide_tab)customize_tab_page();else guide_tab_page();
    if(picker<MP_BUTTONS)picker_panel();
}
void mp_bottom_guide_reset(void){guide_tab=0;picker=MP_BUTTONS;
#ifdef MP_SMOKE_TEST
    mp_bottom_controls_state=0;
#endif
}
/* A touch on the open CONTROLS page (not the footer); 1 if it was used. */
unsigned mp_bottom_guide_touch(unsigned x,unsigned y){
    if(y>=214)return 0;
    if(picker<MP_BUTTONS){
        for(int i=0;i<7;++i)if(inside(x,y,PICK_X+(i%2)*134,PICK_Y+(i/2)*32,PICK_W,PICK_H))
            mp_native_set_button_action(picker,picks[i]);
        picker=MP_BUTTONS; /* any tap closes the picker */
    }else if(y<29){
        if(inside(x,y,GUIDE_X,0,GUIDE_W,29))guide_tab=0;
        else if(inside(x,y,CUSTOM_X,0,CUSTOM_W,29))guide_tab=1;
    }else if(!guide_tab){
        if(inside(x,y,8,EDIT_Y,304,EDIT_H))guide_tab=1;
    }else{
        for(unsigned b=0;b<MP_BUTTONS;++b){int cx,cy;cell_position(b,&cx,&cy);if(inside(x,y,cx,cy,CELL_W,CELL_H))picker=b;}
        if(inside(x,y,8,OPTIONS_Y,TAP_W,OPTIONS_H))mp_native_set_tap_jump(!mp_native_tap_jump());
        if(inside(x,y,RESET_X,OPTIONS_Y,RESET_W,OPTIONS_H))mp_native_reset_buttons();
    }
#ifdef MP_SMOKE_TEST
    mp_bottom_controls_state=guide_tab|(picker<MP_BUTTONS?picker+1:0)<<8;
#endif
    return 1;
}
void mp_bottom_loading(uint16_t* pixels,const char* message){
    canvas=pixels;background();header("SUPER SMASH BROS.","3DS");
    center(160,70,"MELEE",63,ink);rect(60,134,200,2,gold);center(160,152,message,15,gold);
    center(160,207,"NATIVE NINTENDO 3DS PORT",11,muted);
}
void mp_bottom_notice(uint16_t* pixels,const char* title,const char* lead,const char* line1,const char* line2,const char* line3,const char* hint){
    canvas=pixels;background();header(title,"3DS");
    fit(17,46,lead,19,286,gold);rect(17,78,38,2,gold);
    fit(17,96,line1,14,286,ink);fit(17,122,line2,12,286,muted);fit(17,140,line3,12,286,muted);
    rect(0,190,320,50,RGB(10,18,29));rect(0,190,320,1,RGB(80,98,111));fit(17,207,hint,14,286,gold);
}
/* ---- Slippi Direct (port/3ds/slippi/slippi_ui.c) ----
 * The online CSS companion: who you are, who you play, Slippi's three status
 * lines and the actions; and the connect-code keyboard. Same idiom as the
 * pages above: header, cards with an accent bar, pills, ink/muted/gold. */
#include "slippi/slippi_ui.h"
#include "group/group_menu.h"
static const uint16_t slippi_wait=RGB(60,188,255),slippi_done=RGB(51,255,47),slippi_red=RGB(255,86,86);
/* Key rectangles: 10-column grid (rows 0-3) and the action row. */
const short slippi_ui_key_rects[SLIPPI_KEYS][4]={
#define K(c,r) {(short)(5+(c)*31),(short)(76+(r)*27),29,24}
    K(0,0),K(1,0),K(2,0),K(3,0),K(4,0),K(5,0),K(6,0),K(7,0),K(8,0),K(9,0),
    K(0,1),K(1,1),K(2,1),K(3,1),K(4,1),K(5,1),K(6,1),K(7,1),K(8,1),K(9,1),
    K(0,2),K(1,2),K(2,2),K(3,2),K(4,2),K(5,2),K(6,2),K(7,2),K(8,2),K(9,2),
    K(0,3),K(1,3),K(2,3),K(3,3),K(4,3),K(5,3),K(6,3),
#undef K
    {222,157,91,24},{5,185,74,25},{83,185,74,25},{161,185,74,25},{239,185,74,25}};
enum{SHEIK_X=242,SHEIK_Y=137,SHEIK_W=70,SHEIK_H=18};
enum{SL_ACTION_X=8,SL_ACTION_Y=187,SL_ACTION_W=190,SL_ACTION_H=23,SL_SIDE_X=204,SL_SIDE_W=108,
     SL_CHAT_X=126,SL_CHAT_W=72,SL_CONNECTED_W=112,CHAT_ROW0=35,CHAT_STEP=21,CHAT_H=19,CHAT_W=148};
static int slippi_online_css_drawn; /* the last online page drawn was the CSS one (not the SSS) */
static int slippi_online_page(const MPBottomState* s){
    const SlippiUiView* v=slippi_ui_view();
    return s->scene==8&&s->mode==8&&(v->page==SLIPPI_PAGE_CSS||v->page==SLIPPI_PAGE_CODE||v->page==SLIPPI_PAGE_CHAT||
                                     v->page==SLIPPI_PAGE_SETTINGS);
}
static void status_line(int y,int done,const char* s,int spinner){
    text(17,y,done?"-":spinner?"x":"+",12,done?slippi_done:slippi_wait);fit(33,y,s,12,270,ink);
}
/* The primary action for the current state, as Slippi's status line 2 / hints. */
static const char* slippi_action(const SlippiUiView* v,int* enabled){
    *enabled=1;
    switch(v->phase){
    case SLIPPI_PHASE_IDLE:*enabled=v->ready;return "ENTER CODE";
    case SLIPPI_PHASE_SEARCH:return "CANCEL";
    case SLIPPI_PHASE_ERROR:return "CLEAR ERROR";
    default:if(v->locked){*enabled=0;return "LOCKED IN";}
        return v->need_stage&&!v->chose_stage?"SELECT STAGE":"LOCK IN";
    }
}
static void slippi_css_page(void){
    const SlippiUiView* v=slippi_ui_view();char b[64];
    static const char* sides[]={"OFFLINE","SEARCHING","CONNECTED","ERROR"};
    header("DIRECT MODE",sides[v->phase&3]);
    /* You */
    rect(8,37,304,46,card_color);rect(8,37,3,46,gold);rect(11,37,301,1,RGB(70,85,103));
    text(19,42,"USER",11,muted);fit(19,56,v->user_name[0]?v->user_name:"NO SLIPPI ACCOUNT",17,180,v->user_name[0]?ink:muted);
    right(304,42,"CONNECT CODE",11,muted);right(304,58,v->user_code[0]?v->user_code:"-",15,gold);
    /* Opponent */
    int connected=v->phase==SLIPPI_PHASE_CONNECTED;
    rect(8,88,304,46,card_color);rect(8,88,3,46,connected?colors[1]:line_color);rect(11,88,301,1,RGB(70,85,103));
    text(19,93,connected?"PLAYING":"OPPONENT",11,muted);
    if(connected){fit(19,107,v->opponent_name[0]?v->opponent_name:v->opponent_code,17,180,ink);
        right(304,93,v->opponent_code,11,gold);snprintf(b,sizeof(b),"PING %d MS",v->ping_ms);right(304,111,b,12,muted);}
    else if(v->phase==SLIPPI_PHASE_SEARCH){fit(19,107,v->target,17,180,ink);right(304,111,"WAITING",12,muted);}
    else fit(19,107,"ENTER THEIR CONNECT CODE",14,280,muted);
    /* Status lines (LoadCSSText.asm) or the error */
    if(v->phase==SLIPPI_PHASE_ERROR){
        text(17,140,"ERROR",12,slippi_red);
        /* Word-wrap the server's message over three lines. */
        const char* e=v->error[0]?v->error:"UNKNOWN ERROR";int y=155;
        while(*e&&y<=185){char line[48];int n=0,last=-1;
            while(e[n]&&n<46){if(e[n]==' ')last=n;line[n]=e[n];n++;line[n]=0;if(width(line,11)>286)break;}
            if(e[n]&&last>0)n=last;line[n]=0;fit(17,y,line,11,286,ink);e+=n;while(*e==' ')e++;y+=15;}
    }else{
        status_line(140,v->ready,v->ready?"CHARACTER SELECTED":"SELECT YOUR CHARACTER",v->spinner);
        if(v->ready&&(v->ckind==18||v->ckind==19)&&!v->locked){
            /* InitSheikSelector: tap to switch the Zelda icon. */
            pill(SHEIK_X,SHEIK_Y,SHEIK_W,SHEIK_H,v->play_zelda?"ZELDA":"SHEIK",12,1);
            right(SHEIK_X-6,SHEIK_Y+4,"TAP:",10,muted);
        }
        if(v->locked)status_line(155,1,"LOCKED IN",0);
        else{const char* what=v->phase==SLIPPI_PHASE_IDLE?"PRESS START TO ENTER CODE":
                v->need_stage&&!v->chose_stage?"PRESS START TO SELECT STAGE":"PRESS START TO LOCK IN";
            status_line(155,0,what,v->spinner);}
        if(v->phase==SLIPPI_PHASE_SEARCH){snprintf(b,sizeof(b),v->net_status==2?"CONNECTING TO %s":"SEARCHING FOR %s",v->target);status_line(170,0,b,v->spinner);}
        else if(connected)status_line(170,0,"WAITING ON OPPONENT",v->spinner);
    }
    /* Actions */
    int enabled;const char* action=slippi_action(v,&enabled);
    int aw=(connected&&v->chat_enabled)||v->phase==SLIPPI_PHASE_IDLE?SL_CONNECTED_W:SL_ACTION_W;
    if(enabled)pill(SL_ACTION_X,SL_ACTION_Y,aw,SL_ACTION_H,action,13,1);
    else{rect(SL_ACTION_X,SL_ACTION_Y,aw,SL_ACTION_H,card_color);center(SL_ACTION_X+aw/2,SL_ACTION_Y+5,action,13,muted);}
    if(connected&&v->chat_enabled)pill(SL_CHAT_X,SL_ACTION_Y,SL_CHAT_W,SL_ACTION_H,"CHAT",13,0);
    if(v->phase==SLIPPI_PHASE_IDLE)pill(SL_CHAT_X,SL_ACTION_Y,SL_CHAT_W,SL_ACTION_H,"SETTINGS",12,0);
    if(connected){
        rect(SL_SIDE_X,SL_ACTION_Y,SL_SIDE_W,SL_ACTION_H,v->hold_z?RGB(90,30,34):dark);
        if(v->hold_z)rect(SL_SIDE_X,SL_ACTION_Y+SL_ACTION_H-3,SL_SIDE_W*v->hold_z/0x30,3,slippi_red);
        center(SL_SIDE_X+SL_SIDE_W/2,SL_ACTION_Y+5,"DISCONNECT",13,ink);
    }else{
        const char* hint=v->phase==SLIPPI_PHASE_SEARCH?"Z: CANCEL":v->phase==SLIPPI_PHASE_ERROR?"Z: CLEAR":"START";
        center(SL_SIDE_X+SL_SIDE_W/2,SL_ACTION_Y+6,hint,11,muted);
    }
}
enum{SET_Y0=38,SET_H=44,SET_STEP=52};
static void slippi_settings_page(void){
    const SlippiUiView* v=slippi_ui_view();char b[16];
    header("ONLINE SETTINGS","BACK");
    int y=SET_Y0;
    rect(8,y,304,SET_H,card_color);rect(8,y,3,SET_H,gold);
    text(19,y+6,"DELAY FRAMES",14,ink);fit(19,y+25,"KEEP AT 2 UNLESS PING IS 120+ MS",10,186,muted);
    pill(214,y+9,26,26,"-",17,0);snprintf(b,sizeof(b),"%d",v->delay);center(258,y+12,b,20,gold);pill(276,y+9,26,26,"+",17,0);
    y+=SET_STEP;
    rect(8,y,304,SET_H,card_color);rect(8,y,3,SET_H,v->chat_enabled?gold:line_color);
    text(19,y+6,"QUICK CHAT",14,ink);fit(19,y+25,"D-PAD MESSAGES ON THE CSS",10,186,muted);
    pill(240,y+9,62,26,v->chat_enabled?"ON":"OFF",14,v->chat_enabled);
    y+=SET_STEP;
    rect(8,y,304,SET_H,card_color);rect(8,y,3,SET_H,line_color);
    text(19,y+6,"RECENT CODES",14,ink);snprintf(b,sizeof(b),"%d SAVED",v->history_count);fit(19,y+25,b,10,190,muted);
    pill(240,y+9,62,26,"CLEAR",13,0);
}
static void chat_rect(int i,int* x,int* y){*x=8+(i/8)*156;*y=CHAT_ROW0+(i%8)*CHAT_STEP;}
static void slippi_chat_page(void){
    const SlippiUiView* v=slippi_ui_view();
    header("QUICK CHAT","BACK");
    for(int i=0;i<16;++i){int x,y;chat_rect(i,&x,&y);char label[48];
        rect(x,y,CHAT_W,CHAT_H,card_color);rect(x,y,3,CHAT_H,v->chat_page==i/4?gold:line_color);
        snprintf(label,sizeof(label),"%s",slippi_ui_chat_message(i));
        for(char* c=label;*c;++c)if(*c>='a'&&*c<='z')*c-=32;
        fit(x+9,y+4,label,12,CHAT_W-14,ink);
    }
}
enum{SSS_FROZEN_X=8,SSS_FROZEN_Y=118,SSS_FROZEN_W=304,SSS_FROZEN_H=40};
static void slippi_sss_page(void){
    const SlippiUiView* v=slippi_ui_view();
    header("SELECT A STAGE","DIRECT");
    rect(8,37,304,72,card_color);rect(8,37,3,72,gold);rect(11,37,301,1,RGB(70,85,103));
    fit(19,44,"YOUR PICK",22,280,ink);
    fit(19,72,"YOU LOST THE LAST GAME, SO YOU CHOOSE",12,280,muted);
    fit(19,88,"THE NEXT STAGE. IT LOCKS YOU IN.",12,280,muted);
    rect(SSS_FROZEN_X,SSS_FROZEN_Y,SSS_FROZEN_W,SSS_FROZEN_H,card_color);rect(SSS_FROZEN_X,SSS_FROZEN_Y,3,SSS_FROZEN_H,v->frozen_stadium?gold:line_color);
    text(19,SSS_FROZEN_Y+6,"FROZEN POKEMON STADIUM",13,ink);text(19,SSS_FROZEN_Y+23,"NO TRANSFORMATIONS   Z OR TAP",11,muted);
    pill(250,SSS_FROZEN_Y+8,54,SSS_FROZEN_H-16,v->frozen_stadium?"ON":"OFF",13,v->frozen_stadium);
    text(17,170,"CIRCLE PAD: CHOOSE   A: PICK",13,gold);
    text(17,190,"B: BACK TO FIGHTERS (NO STAGE)",12,muted);
}
static void slippi_code_page(void){
    const SlippiUiView* v=slippi_ui_view();
    header("CONNECT CODE","DIRECT");
    rect(8,35,304,36,card_color);rect(8,35,3,36,gold);rect(11,35,301,1,RGB(70,85,103));
    int x=19;text(x,42,v->typed,22,ink);x+=width(v->typed,22);
    size_t n=strlen(v->typed);
    if(v->suggestion[0]&&strlen(v->suggestion)>n)text(x,42,v->suggestion+n,22,muted);
    if(n<SLIPPI_CODE_MAX)rect(x+1,64,12,2,v->spinner?gold:card_color);
    right(306,40,v->suggestion[0]?"X: USE":"",10,gold);right(306,54,"L/R: RECENT",10,muted);
    for(int i=0;i<SLIPPI_KEYS;++i){
        const short* r=slippi_ui_key_rects[i];char label[8];const char* s=label;char c=slippi_ui_keys[i];
        switch(c){case '\b':s="ERASE";break;case '\x1b':s="CANCEL";break;case '<':s="L OLDER";break;case '>':s="R NEWER";break;case '\r':s="OK";break;
            default:label[0]=c;label[1]=0;}
        int active=i==v->key,small=c=='\r'||c=='\b'||c=='\x1b'||c=='<'||c=='>';
        if(c=='\x1b'&&!active){rect(r[0],r[1],r[2],r[3],dark);center(r[0]+r[2]/2,r[1]+(r[3]-12)/2,s,12,muted);}
        else pill(r[0],r[1],r[2],r[3],s,small?12:15,active);
    }
}
/* ---- Melee -> Solo / Group (port/3ds/group/group_menu.c) ----
 * Shown on the VS menu once Melee is chosen: Solo or Group, then Host and the games nearby. */
enum{GP_SOLO_X=8,GP_GROUP_X=164,GP_BTN_Y=44,GP_BTN_W=148,GP_BTN_H=120,GP_BACK_X=8,GP_BACK_Y=180,GP_BACK_W=90,GP_BACK_H=24,
     GL_HOST_X=8,GL_HOST_Y=35,GL_HOST_W=220,GL_HOST_H=27,GL_BACK_X=236,GL_BACK_W=76,
     /* with two pages or more, the Host button narrows for the page controls: < 1/2 > */
     GL_HOST_PAGED_W=146,GL_PREV_X=158,GL_NEXT_X=226,GL_PAGE_W=32,GL_BACK_PAGED_X=262,GL_BACK_PAGED_W=50};
/* The Available Games rows, between the Host row and the footer (y 214): GROUP_ROWS (3) rows of 48 px with a 48 px
 * Mii head (owner, 2026-11-25, chosen over 4 rows with 32 px heads). */
typedef struct{int y,step,h,head,name,badge,badge_step,badge_x;}GroupRows;
static const GroupRows group_rows={65,49,48,48,16,24,27,156};
/* The Group list (Host, games nearby) is touch-driven. The Solo/Group choice itself is made on the top-screen menu (menu
 * kind 11) and its bottom page is information only, like the other menus'; the touch buttons only show when the menu files
 * for that menu are missing (the fallback, on the VS menu). */
static int group_page(const MPBottomState* s){
    int page=group_view()->page;
    return s->scene==1&&(page==GROUP_PAGE_LIST||(page==GROUP_PAGE_ASK&&s->menu!=11));
}
static void group_error(const GroupView* v,int y){
    if(v->error[0])fit(14,y,v->error,12,292,slippi_red);
}
static void group_ask_page(const GroupView* v){
    header("MELEE","VS MODE");
    rect(GP_SOLO_X,GP_BTN_Y,GP_BTN_W,GP_BTN_H,card_color);rect(GP_SOLO_X,GP_BTN_Y,3,GP_BTN_H,gold);
    center(GP_SOLO_X+GP_BTN_W/2,GP_BTN_Y+34,"SOLO",24,ink);center(GP_SOLO_X+GP_BTN_W/2,GP_BTN_Y+72,"THIS CONSOLE",11,muted);
    rect(GP_GROUP_X,GP_BTN_Y,GP_BTN_W,GP_BTN_H,card_color);rect(GP_GROUP_X,GP_BTN_Y,3,GP_BTN_H,slippi_wait);
    center(GP_GROUP_X+GP_BTN_W/2,GP_BTN_Y+34,"GROUP",24,ink);center(GP_GROUP_X+GP_BTN_W/2,GP_BTN_Y+72,"NEARBY 3DS",11,muted);
    pill(GP_BACK_X,GP_BACK_Y,GP_BACK_W,GP_BACK_H,"BACK",13,0);
    center(220,GP_BACK_Y+6,"B: BACK",11,muted);
    group_error(v,160+4);
}
/* The fighters in a game's doors, as "FOX, FALCO". */
/* The default Smash emblem (the circle crossed by a vertical and a horizontal bar), d pixels across: Melee's own, the
 * stock-icon-sized emblem in IfAll.usd (entry 118, dark grey), drawn as a shape in `color` so it reads on the dark
 * rows; without the icons, the same procedural mark background() draws behind every page. */
#define EMBLEM_ENTRY 118
static void emblem(int x,int y,int d,uint16_t color){
    if(stocks_ready){
        for(int yy=0;yy<d;++yy)for(int xx=0;xx<d;++xx){unsigned a=stock_alpha[EMBLEM_ENTRY][(yy*24/d)*24+xx*24/d];if(a)pixel(x+xx,y+yy,color,a);}
        return;
    }
    int r=d/2,ring=d/10>0?d/10:1;
    for(int yy=0;yy<d;++yy)for(int xx=0;xx<d;++xx){
        int dx=2*xx-d+1,dy=2*yy-d+1,dd=dx*dx+dy*dy,ro=(2*r)*(2*r),ri=(2*(r-ring))*(2*(r-ring));
        int vbar=xx>=d*6/16&&xx<d*6/16+ring+1,hbar=yy>=d*9/16&&yy<d*9/16+ring;
        if((dd<=ro&&dd>ri)||(dd<=ri&&(vbar||hbar)))pixel(x+xx,y+yy,color,32);
    }
}
/* A stock icon, scaled to size x size (nearest), dimmed when dim. 0 if there is none for that fighter and costume. */
static int stock_draw(int x,int y,int size,unsigned fighter,unsigned costume,int dim){
    int e=stocks_ready?stock_icon(fighter,costume):-1;
    if(e<0&&stocks_ready&&fighter<26)e=stock_icon(fighter,0);   /* a costume this fighter does not have */
    if(e<0)return 0;
    for(int yy=0;yy<size;++yy)for(int xx=0;xx<size;++xx){unsigned at=(yy*24/size)*24+xx*24/size,a=stock_alpha[e][at];
        if(dim)a=a*12/32;if(a)pixel(x+xx,y+yy,stock[e][at],a);}
    return 1;
}
/* A disc of diameter d at (x, y) (its top left). */
static void disc(int x,int y,int d,uint16_t color,unsigned alpha){
    int r2=d*d;for(int yy=0;yy<d;++yy)for(int xx=0;xx<d;++xx){int dx=2*xx-d+1,dy=2*yy-d+1;if(dx*dx+dy*dy<=r2)pixel(x+xx,y+yy,color,alpha);}
}
/* The fighter a badge shows for a port: during a match, the host's live status (Zelda <-> Sheik as she transforms);
 * otherwise the door's pick. */
static void badge_fighter(const lobby_slot* door,const lobby_status* st,int phase,int port,unsigned* fighter,unsigned* costume){
    if((phase==LOBBY_MATCH||phase==LOBBY_RESULTS)&&st->valid&&st->port[port].kind!=3){*fighter=st->port[port].fighter;*costume=st->port[port].costume;return;}
    *fighter=door->fighter;*costume=door->color;
}
/* A port's badge, diameter d: nothing for an empty door; a Human's disc in its port colour, a CPU's grey, with the
 * fighter's stock icon; "?" while no fighter is placed (or the icons are missing). dim: out of stocks. */
uint16_t mp_group_badge_drawn[4];
char mp_group_strip_text[4][24];   /* tests: each strip row's status text, as drawn */
static void mp_group_strip_text_set(int i,const char* s){
    if(i<0||i>=4)return;size_t n=strlen(s);if(n>sizeof(mp_group_strip_text[i])-1)n=sizeof(mp_group_strip_text[i])-1;
    memcpy(mp_group_strip_text[i],s,n);mp_group_strip_text[i][n]=0;
}   /* tests: the fighter each port's last badge showed + 1 (0 none), as read by the Azahar test */
/* A port's colour: its team's under Teams (red, blue, green), else the port's own. */
static uint16_t player_color(int port,const lobby_slot* door,int teams){
    static const uint16_t team_colors[3]={RGB(246,73,75),RGB(70,149,255),RGB(68,211,127)};
    return teams&&door->team<3?team_colors[door->team]:colors[port&3];
}
static void stock_badge(int x,int y,int d,const lobby_slot* door,const lobby_status* st,int phase,int port,int dim,int teams){
    unsigned f,c;
    if(door->kind==LOBBY_EMPTY&&!((phase==LOBBY_MATCH||phase==LOBBY_RESULTS)&&st->valid&&st->port[port].kind!=3))return;
    badge_fighter(door,st,phase,port,&f,&c);
    int cpu=door->kind==LOBBY_CPU||((phase==LOBBY_MATCH||phase==LOBBY_RESULTS)&&st->valid&&st->port[port].kind==1);
    /* the badge's disc: the player's colour muted, as the match draws it behind the percentage (owner, 2026-11-25) */
    disc(x,y,d,cpu?RGB(78,86,96):blend(player_color(port,door,teams),RGB(25,36,51),17),dim?12:32);
    int placed=door->chosen==1||((phase==LOBBY_MATCH||phase==LOBBY_RESULTS)&&st->valid);
    if(!placed||!stock_draw(x+2,y+2,d-4,f,c,dim)){center(x+d/2,y+(d-d*2/3)/2,"?",d*2/3,ink);if(port>=0&&port<4)mp_group_badge_drawn[port]=0;return;}
    if(port>=0&&port<4)mp_group_badge_drawn[port]=(uint16_t)(f+1);
}
/* tests: what each row of the page drew as its head: 1 the host's Mii, 2 the emblem, 0 no row */
unsigned char mp_group_row_head[GROUP_ROWS];
/* The host's Mii head (mp_group_mii_icon: 64 x 64 RGBA, ready once rendered), box-filtered to d x d. 0 if not ready. */
static int mii_icon_draw(uint32_t key,uint16_t crc,const unsigned char* mii,int x,int y,int d){
    const uint32_t* icon=mp_group_mii_icon(key,crc,mii);
    if(!icon)return 0;
    for(int yy=0;yy<d;++yy)for(int xx=0;xx<d;++xx){
        int x0=xx*64/d,x1=(xx+1)*64/d,y0=yy*64/d,y1=(yy+1)*64/d;unsigned rr=0,gg=0,bb=0,aa=0,n=0;
        for(int sy=y0;sy<y1;++sy)for(int sx=x0;sx<x1;++sx){uint32_t p=icon[sy*64+sx];unsigned a=p&255;
            rr+=(p>>24)*a;gg+=(p>>16&255)*a;bb+=(p>>8&255)*a;aa+=a;++n;}
        if(!aa)continue;
        unsigned alpha=(aa/n*32+127)/255;if(!alpha)continue;
        pixel(x+xx,y+yy,RGB(rr/aa,gg/aa,bb/aa),alpha);
    }
    return 1;
}
static int mii_head(const group_game* r,int x,int y,int d){return r->has_mii&&mii_icon_draw(r->key,r->mii_crc,r->mii,x,y,d);}
#define MY_MII_KEY 0xFFFFFFFFu     /* the icon cache's key for this console's own Mii (games' keys never reach it) */
unsigned char mp_group_host_head;  /* tests: 1 when the Host button drew this console's Mii head */
/* "HOST GAME" with this console's own Mii head at its left (owner, 2026-11-25); the text alone without one */
static void host_button(const GroupView* v,int w){
    /* the head (25 px in the 27 px button) and the text are centred together, 4 px apart (owner, 2026-11-25). A valid
     * Mii (its CRC is checked as the list opens) keeps the head's place, so the text never moves: while it renders, and
     * as a gap if its render fails (owner, 2026-11-25); the text centres alone only without a valid Mii. */
    int on=!v->busy,x=GL_HOST_X,y=GL_HOST_Y,h=GL_HOST_H,d=h-2,gap=4;
    rect(x,y,w,h,on?gold:card_color);
    const char* s=v->busy?"PLEASE WAIT":"HOST GAME";
    int tw=width(s,14),with_head=v->my_has_mii&&!v->busy;
    int left=x+(w-(with_head?d+gap:0)-tw)/2;
    mp_group_host_head=with_head&&mii_icon_draw(MY_MII_KEY,v->my_mii_crc,v->my_mii,left,y+1,d);
    if(with_head)left+=d+gap;
    text(left,y+(h-14)/2,s,14,on?dark:ink);
}
/* a page button: a triangle pointing left (dir -1) or right, gold when it can turn */
static void page_button(int x,int y,int w,int h,int dir,int on){
    rect(x,y,w,h,on?gold:card_color);
    for(int k=0;k<7;++k){int len=2*k+1;int cx=x+w/2+(dir<0?k-3:3-k);rect(cx,y+h/2-k,1,len,on?dark:line_color);}
}
static void group_list_page(const GroupView* v){
    const GroupRows* L=&group_rows;int per=GROUP_ROWS,first=v->list_page*per,paged=v->pages>1;
    /* an error takes the header's right side ("SEARCHING") */
    header("GROUP",v->error[0]?"":v->busy?"JOINING":"SEARCHING");
    if(v->error[0]){int h=12;while(h>8&&width(v->error,h)>205)--h;text(309-width(v->error,h),10,v->error,h,slippi_red);}
    host_button(v,paged?GL_HOST_PAGED_W:GL_HOST_W);
    if(paged){char b[16];
        page_button(GL_PREV_X,GL_HOST_Y,GL_PAGE_W,GL_HOST_H,-1,v->list_page>0);
        page_button(GL_NEXT_X,GL_HOST_Y,GL_PAGE_W,GL_HOST_H,1,v->list_page<v->pages-1);
        snprintf(b,sizeof(b),"%d/%d",v->list_page+1,v->pages);center((GL_PREV_X+GL_PAGE_W+GL_NEXT_X)/2,GL_HOST_Y+8,b,12,ink);
        pill(GL_BACK_PAGED_X,GL_HOST_Y,GL_BACK_PAGED_W,GL_HOST_H,"BACK",13,0);
    }else pill(GL_BACK_X,GL_HOST_Y,GL_BACK_W,GL_HOST_H,"BACK",13,0);
    if(!v->count)center(160,L->y+22,"LOOKING FOR GAMES NEARBY...",12,muted);
    memset(mp_group_row_head,0,sizeof(mp_group_row_head));
    for(int k=0;k<per&&first+k<v->count&&first+k<GROUP_KEEP;++k){
        const group_game* r=&v->row[first+k];char b[64];int y=L->y+k*L->step,h=L->h;
        int busy=r->beacon.phase==LOBBY_MATCH||r->beacon.phase==LOBBY_RESULTS;
        rect(8,y,304,h,card_color);rect(8,y,3,h,busy?line_color:slippi_done);
        int full=r->max>0&&r->players>=r->max,cnt=L->name;
        if(full)right(304,y+3,"FULL",cnt,slippi_red);
        else{snprintf(b,sizeof(b),"%d/4",r->players);right(304,y+3,b,cnt,gold);}
        /* the host's Mii head (the emblem until it is drawn, or without one), its name, a badge per port at fixed
         * places (an empty door: a gap) */
        int hx=15,hy=y+(h-L->head)/2,nx=hx+L->head+7;
        if(mii_head(r,hx,hy,L->head))mp_group_row_head[k]=1;
        else{int e=L->head*5/8;emblem(hx+(L->head-e)/2,hy+(L->head-e)/2,e,muted);mp_group_row_head[k]=2;}
        int name_y=busy?y+(h-L->name-10)/2-1:y+(h-L->name)/2-1;
        fit(nx,name_y,r->host[0]?r->host:"3DS",L->name,L->badge_x-nx-4,ink);
        if(busy)text(nx,name_y+L->name+2,"IN MATCH",11,muted);
        if(stocks_ready)for(int p=0;p<LOBBY_SLOTS;++p){
            lobby_slot door;memset(&door,0,sizeof(door));
            door.kind=r->beacon.slot[p].kind;door.fighter=r->beacon.slot[p].fighter;door.color=r->beacon.slot[p].costume;
            door.team=r->beacon.slot[p].team;door.chosen=r->beacon.slot[p].chosen;
            lobby_status none;memset(&none,0,sizeof(none));
            stock_badge(L->badge_x+p*L->badge_step,y+h-L->badge-4,L->badge,&door,&none,r->beacon.phase,p,0,r->beacon.teams);
        }
    }
}
/* The connected-players strip: who is on which port, the host, signal and what each is doing. */
static void group_bars(int x,int y,int level,uint16_t on){
    for(int b=0;b<3;++b){int h=4+b*3;rect(x+b*5,y+10-h,4,h,b<level?on:line_color);}
}
enum{GD_YES_X=34,GD_NO_X=176,GD_Y=120,GD_W=110,GD_H=34};
static void group_dialog_page(void){
    rect(14,70,292,110,card_color);rect(14,70,292,3,gold);
    center(160,84,"END THE SESSION?",20,ink);
    center(160,106,"EVERYONE IS DISCONNECTED",11,muted);
    pill(GD_YES_X,GD_Y,GD_W,GD_H,"END IT",14,1);pill(GD_NO_X,GD_Y,GD_W,GD_H,"STAY",14,0);
}
/* A console that joined during a match: the match as the host reports it, live, until it ends (then the lobby). */
char mp_group_watch_name[4][32];   /* tests: the name each row of the match overview showed ("" for no character) */
static void group_watch_page(const GroupStrip* g){
    char b[64];
    const lobby_status* st=&g->status;
    header("GROUP",g->phase==LOBBY_RESULTS?"RESULTS":"MATCH IN PROGRESS");
    /* no timer once the match is over (the others are on its results screen: the header says so) */
    if(g->phase==LOBBY_RESULTS){}
    else if(!st->valid)center(160,44,"WAITING FOR THE MATCH...",13,muted);
    else if(st->timer){snprintf(b,sizeof(b),"%u:%02u",st->seconds/60,st->seconds%60);center(160,40,b,20,ink);}
    else center(160,44,"NO TIME LIMIT",13,muted);
    for(int i=0;i<LOBBY_SLOTS;++i){
        int y=70+i*30;const lobby_slot* d=&g->port[i].door;int kind=st->valid?st->port[i].kind:(d->kind==LOBBY_EMPTY?3:d->kind);
        /* the accent: the team or port colour for a human player, grey for a CPU, none without a character */
        rect(8,y,304,26,card_color);
        if(kind==0)rect(8,y,4,26,player_color(i,d,g->rules.teams));else if(kind==1)rect(8,y,4,26,RGB(110,120,130));
        if(kind==3){text(20,y+6,"-",13,muted);mp_group_watch_name[i][0]=0;continue;}
        /* the player's name: the console that plays this port, or the one that played it and left; never a console that
         * joined since (this one included) */
        const char* who=g->port[i].player[0]?g->port[i].player:kind==1?"CPU":"";
        snprintf(mp_group_watch_name[i],sizeof(mp_group_watch_name[i]),"%s",who);
        if(stocks_ready){
            /* the live fighter's badge (a Zelda who transforms shows Sheik), the console's name, the stocks left */
            int out=st->valid&&!g->rules.time_mode&&st->port[i].stocks==0;
            stock_badge(16,y+1,24,d,st,g->phase,i,out,g->rules.teams);
            fit(46,y+7,who,13,96,out?muted:ink);
            if(st->valid&&!g->rules.time_mode){
                unsigned f,c,n=st->port[i].stocks;badge_fighter(d,st,g->phase,i,&f,&c);
                if(n<=5)for(unsigned k=0;k<n;++k)stock_draw(148+k*13,y+7,12,f,c,0);
                else{stock_draw(148,y+7,12,f,c,0);snprintf(b,sizeof(b),"x %u",n);text(164,y+7,b,12,gold);}
            }
            if(st->valid){snprintf(b,sizeof(b),"%u%%",st->port[i].damage);right(300,y+5,b,16,st->port[i].stocks||g->rules.time_mode?ink:muted);}
            continue;
        }
        snprintf(b,sizeof(b),"%s%s",kind==1?"CPU ":"",d->chosen==1&&d->fighter<26?names[d->fighter]:"FIGHTER");
        fit(20,y+2,b,13,150,ink);
        fit(20,y+15,who,10,150,muted);
        if(st->valid){
            snprintf(b,sizeof(b),"%u%%",st->port[i].damage);right(300,y+5,b,16,st->port[i].stocks?ink:muted);
            snprintf(b,sizeof(b),"x%u",st->port[i].stocks);right(236,y+7,b,13,gold);
        }
    }
    center(160,192,g->toast[0]?g->toast:"HOLD B TO LEAVE",13,g->toast[0]?slippi_done:gold);
}
static void group_strip_page(const GroupStrip* g,unsigned scene){
    char b[64];
    if(g->watching){group_watch_page(g);if(group_dialog())group_dialog_page();return;}
    header("GROUP",g->phase==LOBBY_MATCH?"IN MATCH":g->phase==LOBBY_RESULTS?"RESULTS":scene==9?"STAGE SELECT":"CHARACTER SELECT");
    for(int i=0;i<LOBBY_SLOTS;++i){
        int y=36+i*38;const lobby_slot* d=&g->port[i].door;
        rect(8,y,304,34,card_color);rect(8,y,4,34,colors[i]);
        if(g->port[i].present){
            fit(20,y+3,g->port[i].name[0]?g->port[i].name:"3DS",15,150,i==g->me?gold:ink);
            /* the signal bars (10 px tall) centred on the badge (24 px at y+5): owner, 2026-11-25; no "HOST" word */
            group_bars(266,y+12,g->port[i].signal,slippi_done);
        }else text(20,y+3,"-",15,muted);
        /* what the port is doing: why it is busy (on the character select), else its state; the fighter itself is the
         * badge (without the icons, its name as before) */
        static const char*const why_text[5]={"","","CHOOSING A NAME TAG","SETTING THE RULES","ENTERING A NAME"};
        int why=g->port[i].busy_why,css=g->phase==LOBBY_CSS;
        if(css&&g->port[i].present&&why>=2&&why<=4)snprintf(b,sizeof(b),"%s",why_text[why]);
        else if(d->kind==LOBBY_EMPTY)snprintf(b,sizeof(b),"%s",g->port[i].present?"CLOSED":"OPEN");
        else if(stocks_ready)snprintf(b,sizeof(b),"%s",g->phase==LOBBY_MATCH||g->phase==LOBBY_RESULTS?(d->kind==LOBBY_CPU?"CPU":"PLAYING"):d->kind==LOBBY_CPU?"CPU":d->chosen==1?"CHOSEN":"CHOOSING");
        else if(g->phase==LOBBY_MATCH||g->phase==LOBBY_RESULTS)snprintf(b,sizeof(b),"%s%s",d->kind==LOBBY_CPU?"CPU ":"",d->chosen==1&&d->fighter<26?names[d->fighter]:"PLAYING");
        else if(d->chosen==1&&d->fighter<26)snprintf(b,sizeof(b),"%s%s",d->kind==LOBBY_CPU?"CPU: ":"",names[d->fighter]);
        else snprintf(b,sizeof(b),"%s",d->kind==LOBBY_CPU?"CPU":"CHOOSING");
        fit(20,y+19,b,11,stocks_ready?200:270,muted);
        if(stocks_ready)stock_badge(236,y+5,24,d,&g->status,g->phase,i,0,g->rules.teams);
        mp_group_strip_text_set(i,b);
    }
    if(g->rules.time_mode)snprintf(b,sizeof(b),"TIME %d MIN",g->rules.minutes);else snprintf(b,sizeof(b),"%d STOCK%s",g->rules.stocks,g->rules.stocks==1?"":"S");
    /* Only the host sets the rules (its CSS Rules banner and Teams switch do nothing on the other consoles); the line
     * says no more than the rules: no "host" wording on the CSS's bottom screen (owner, 2026-11-25) */
    if(g->toast[0])center(160,192,g->toast,13,slippi_done);else center(160,192,b,12,gold);
    if(group_dialog())group_dialog_page();
}
/* A touch on the Solo/Group pages; 1 if used. */
static unsigned group_touch(unsigned x,unsigned y){
    const GroupView* v=group_view();
    group_note_touch(x,y);
    if(v->page==GROUP_PAGE_ASK){
        if(inside(x,y,GP_SOLO_X,GP_BTN_Y,GP_BTN_W,GP_BTN_H))group_pick_solo();
        else if(inside(x,y,GP_GROUP_X,GP_BTN_Y,GP_BTN_W,GP_BTN_H))group_pick_group();
        else if(inside(x,y,GP_BACK_X,GP_BACK_Y,GP_BACK_W,GP_BACK_H))group_back();
        return 1;
    }
    int paged=v->pages>1;
    if(inside(x,y,GL_HOST_X,GL_HOST_Y,paged?GL_HOST_PAGED_W:GL_HOST_W,GL_HOST_H))group_host();
    else if(paged?inside(x,y,GL_BACK_PAGED_X,GL_HOST_Y,GL_BACK_PAGED_W,GL_HOST_H):inside(x,y,GL_BACK_X,GL_HOST_Y,GL_BACK_W,GL_HOST_H))group_back();
    else if(paged&&inside(x,y,GL_PREV_X-2,GL_HOST_Y,GL_PAGE_W+4,GL_HOST_H))group_list_turn(-1);
    else if(paged&&inside(x,y,GL_NEXT_X-2,GL_HOST_Y,GL_PAGE_W+4,GL_HOST_H))group_list_turn(1);
    else{const GroupRows* L=&group_rows;
        for(int i=0;i<GROUP_ROWS;++i)if(inside(x,y,8,L->y+i*L->step,304,L->h)){group_join_row(i);break;}}
    return 1;
}
/* A touch on an online page (not the footer); 1 if used. */
unsigned mp_bottom_online_touch(unsigned x,unsigned y){
    const SlippiUiView* v=slippi_ui_view();
    if(y>=214)return 0;
    if(group_dialog()){
        if(inside(x,y,GD_YES_X,GD_Y,GD_W,GD_H))group_dialog_answer(1);
        else if(inside(x,y,GD_NO_X,GD_Y,GD_W,GD_H))group_dialog_answer(0);
        return 1;
    }
    if(group_view()->page!=GROUP_PAGE_NONE)return group_touch(x,y);
    if(v->page!=SLIPPI_PAGE_CODE&&v->page!=SLIPPI_PAGE_CHAT&&inside(x,y,SSS_FROZEN_X,SSS_FROZEN_Y,SSS_FROZEN_W,SSS_FROZEN_H)&&!slippi_online_css_drawn){slippi_ui_alt(1);return 1;}
    if(v->page==SLIPPI_PAGE_CODE){
        for(int i=0;i<SLIPPI_KEYS;++i){const short* r=slippi_ui_key_rects[i];
            if(inside(x,y,r[0],r[1],r[2],r[3])){slippi_ui_type(i);return 1;}}
        if(inside(x,y,8,35,304,36))slippi_ui_use_suggestion();
        return 1;
    }
    if(v->page==SLIPPI_PAGE_SETTINGS){
        if(inside(x,y,214,SET_Y0+9,26,26))slippi_ui_set_delay(-1);
        else if(inside(x,y,276,SET_Y0+9,26,26))slippi_ui_set_delay(1);
        else if(inside(x,y,8,SET_Y0+SET_STEP,304,SET_H))slippi_ui_toggle_chat();
        else if(inside(x,y,240,SET_Y0+2*SET_STEP+9,62,26))slippi_ui_clear_history();
        else if(!inside(x,y,8,SET_Y0,304,3*SET_STEP))slippi_ui_close_settings();
        return 1;
    }
    if(v->phase==SLIPPI_PHASE_IDLE&&inside(x,y,SL_CHAT_X,SL_ACTION_Y,SL_CHAT_W,SL_ACTION_H)){slippi_ui_open_settings();return 1;}
    if(v->page==SLIPPI_PAGE_CSS&&v->ready&&(v->ckind==18||v->ckind==19)&&!v->locked&&inside(x,y,SHEIK_X,SHEIK_Y,SHEIK_W,SHEIK_H)){slippi_ui_toggle_zelda();return 1;}
    if(v->page==SLIPPI_PAGE_CHAT){
        for(int i=0;i<16;++i){int cx,cy;chat_rect(i,&cx,&cy);if(inside(x,y,cx,cy,CHAT_W,CHAT_H)){slippi_ui_send_chat(i);return 1;}}
        slippi_ui_close_chat();return 1;
    }
    if(v->phase==SLIPPI_PHASE_CONNECTED&&v->chat_enabled&&inside(x,y,SL_CHAT_X,SL_ACTION_Y,SL_CHAT_W,SL_ACTION_H)){slippi_ui_open_chat();return 1;}
    if(inside(x,y,SL_ACTION_X,SL_ACTION_Y,(v->phase==SLIPPI_PHASE_CONNECTED&&v->chat_enabled)||v->phase==SLIPPI_PHASE_IDLE?SL_CONNECTED_W:SL_ACTION_W,SL_ACTION_H)){
        if(v->phase==SLIPPI_PHASE_SEARCH||v->phase==SLIPPI_PHASE_ERROR)slippi_ui_cancel();
        else slippi_ui_press_start();
        return 1;
    }
    if(v->phase==SLIPPI_PHASE_CONNECTED&&inside(x,y,SL_SIDE_X,SL_ACTION_Y,SL_SIDE_W,SL_ACTION_H)){slippi_ui_cancel();return 1;}
    return 1;
}
unsigned mp_bottom_online_active(const MPBottomState* s){return group_dialog()||group_page(s)||slippi_online_page(s)||(s->scene==9&&s->mode==8);}
void mp_bottom_draw(uint16_t* pixels,const MPBottomState* s,unsigned fps,unsigned show_fps,unsigned expanded,unsigned rate,unsigned guide){
    canvas=pixels;background();
    int battle=s->scene==2||s->scene==3||s->scene==4||s->scene==44;
    static char online_labels[2][48];
    for(int i=0;i<4;++i)card_labels[i]=NULL;
    if((battle||s->scene==5)&&s->mode==8){
        const SlippiUiView* v=slippi_ui_view();int me=slippi_ui_local_port();
        if(me>=0){
            snprintf(online_labels[me],sizeof(online_labels[0]),"%s",v->user_name[0]?v->user_name:v->user_code);
            snprintf(online_labels[!me],sizeof(online_labels[0]),"%s  %d MS",v->opponent_name[0]?v->opponent_name:v->opponent_code,v->ping_ms);
            for(int k=0;k<2;++k)for(char* c=online_labels[k];*c;++c)if(*c>='a'&&*c<='z')*c-=32;
            card_labels[0]=online_labels[0];card_labels[1]=online_labels[1];
        }
    }
    static GroupStrip grouped;
    int in_group=(s->scene==8||s->scene==9)&&group_strip(&grouped);
    if(guide)guide_page();
    else if(in_group)group_strip_page(&grouped,s->scene);
    else if(group_page(s)){
        if(group_view()->page==GROUP_PAGE_ASK)group_ask_page(group_view());else group_list_page(group_view());
    }else if(battle||s->scene==5){
        char timer[24];if(s->timer)snprintf(timer,sizeof(timer),"%u:%02u",s->seconds/60,s->seconds%60);else snprintf(timer,sizeof(timer),"%s",s->scene==4?"FREE PLAY":s->teams?"TEAMS":"FREE FOR ALL");
        header(s->scene==5?"MATCH COMPLETE":s->scene==4?"TRAINING":s->scene==3?"SUDDEN DEATH":s->mode==8?"DIRECT":"MELEE",s->scene==5?"RESULTS":timer);
        unsigned count=0;for(unsigned i=0;i<4;++i)if(s->players[i].kind!=3)++count;
        if(count<=2){unsigned row=0;for(unsigned i=0;i<4;++i)if(s->players[i].kind!=3)card(8,38+(row++)*86,304,80,&s->players[i],i,s->scene==4?2:1,s->stock_mode,s->stamina);
            if(!count)center(160,100,"GET READY",28,gold);
        }else for(unsigned i=0;i<4;++i)card(8+(i%2)*156,38+(i/2)*86,148,80,&s->players[i],i,1,s->stock_mode,s->stamina);
    }else if(s->scene==9&&s->mode==8){slippi_online_css_drawn=0;slippi_sss_page();
    }else if(slippi_online_page(s)){
        slippi_online_css_drawn=1;
        if(slippi_ui_view()->page==SLIPPI_PAGE_CODE)slippi_code_page();
        else if(slippi_ui_view()->page==SLIPPI_PAGE_CHAT)slippi_chat_page();
        else if(slippi_ui_view()->page==SLIPPI_PAGE_SETTINGS)slippi_settings_page();else slippi_css_page();
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
        rect(0,190,320,24,RGB(10,18,29));center(160,197,s->profile?"FRESH SAVE  /  UCF 0.84":"ALL FIGHTERS UNLOCKED  /  UCF 0.84",11,muted);
    }else if(s->scene==42){
        header("WELCOME TO MELEE","3DS");center(160,49,"READY TO PLAY",29,ink);
        fit(19,102,"A: CONTINUE PAST THE CARD PROMPT",14,282,gold);
        fit(19,139,s->profile?"UNLOCK FIGHTERS AND STAGES AS YOU PLAY":"EVERY FIGHTER AND STAGE IS UNLOCKED",12,282,ink);
        fit(19,161,"YOUR PROGRESS SAVES TO THE SD CARD",11,282,muted);
    }else menu_page(s);
    /* A refused HOME press during wireless play (group_menu.c): drawn last, over any page. */
    if(group_home_note()){rect(0,188,320,24,RGB(10,18,29));center(160,195,"HOME IS UNAVAILABLE DURING WIRELESS PLAY",10,gold);}
    footer(fps,show_fps,expanded,rate,guide);
}
