/* Slippi Direct menu flow, native side: the online CSS session (what
 * Slippi's HandleInputsOnCSS / LoadCSSText do), connect-code entry with
 * history, and the view the bottom screen draws. The engine's online mode
 * (port/engine/slippi/online_mode.c) calls in once per CSS frame. */
#ifndef SLIPPI_UI_H
#define SLIPPI_UI_H

/* slippi_ui_css flags */
#define SLIPPI_UI_LOCKED 1      /* locked in: no unselect or colour change */
#define SLIPPI_UI_START 2       /* both locked in: take the match block */
#define SLIPPI_UI_SSS 4         /* the loser picks the stage */

/* slippi_ui_event */
enum {
    SLIPPI_UI_EV_CSS_ENTER = 1,
    SLIPPI_UI_EV_LEAVE = 2,      /* backed out of the online CSS to the menus */
    SLIPPI_UI_EV_STAGE = 3,      /* arg = stage | alt << 16, or -1 for B */
    SLIPPI_UI_EV_RESULT = 4,     /* arg = 1 won, 0 lost (or no contest by us) */
    SLIPPI_UI_EV_MATCH = 5,      /* the match scene starts */
};

enum { SLIPPI_PHASE_IDLE, SLIPPI_PHASE_SEARCH, SLIPPI_PHASE_CONNECTED, SLIPPI_PHASE_ERROR };
enum { SLIPPI_PAGE_NONE, SLIPPI_PAGE_CSS, SLIPPI_PAGE_CODE, SLIPPI_PAGE_MATCH };

#define SLIPPI_CODE_MAX 8
#define SLIPPI_HISTORY_MAX 16
#define SLIPPI_KEYS 42

typedef struct {
    int page, phase, net_status;
    int ready;               /* a character is chosen on the CSS */
    int locked, need_stage, chose_stage, won_last;
    int hold_z;              /* frames Z is held while connected */
    int ping_ms, delay;
    int spinner;             /* 0/1, flips every 15 frames */
    char user_name[32], user_code[16];
    char target[16];         /* the code being searched for */
    char opponent_name[32], opponent_code[16];
    char error[128];
    /* code entry */
    char typed[SLIPPI_CODE_MAX + 1];
    char suggestion[SLIPPI_CODE_MAX + 1];
    int key;                 /* highlighted key */
    int history_count, history_index;
    unsigned serial;         /* changes whenever anything shown changes */
} SlippiUiView;

const SlippiUiView* slippi_ui_view(void);
extern const char slippi_ui_keys[SLIPPI_KEYS + 1];

/* Engine (through the bridge). */
int slippi_ui_css(int packed, int trigger, int held);   /* packed = ready | ckind << 8 | color << 16 */
void slippi_ui_event(int event, int arg);
int slippi_ui_remote(void);                              /* char | color << 8, or -1 */
int slippi_ui_text(int line, char *out, int len);       /* top-screen CSS text */

/* Native pad (services.c, engine thread): the GameCube buttons the game
 * gets after the session's filter; *zero_sticks is set while the keyboard
 * takes the controls. */
unsigned slippi_ui_pad(unsigned keys_down, unsigned gc_buttons, int *zero_sticks);

/* Bottom-screen actions (bottom_draw.c's touch handler). */
void slippi_ui_press_start(void);
void slippi_ui_cancel(void);          /* cancel search / clear error / disconnect */
void slippi_ui_type(int key);         /* index into slippi_ui_keys */
void slippi_ui_erase(void);
void slippi_ui_history(int step);     /* +1 older, -1 newer */
void slippi_ui_use_suggestion(void);
void slippi_ui_confirm(void);
void slippi_ui_close_code(void);

#endif
