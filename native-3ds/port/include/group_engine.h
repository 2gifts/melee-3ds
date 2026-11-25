/* Group play, engine side (port/engine/group/group.c, and the patches in tools/group_edits/).
 *
 * A group session reuses Slippi online's lockstep driver (port/engine/slippi/online.c) and its scene table
 * (online_mode.c, the GM_HANYU_CSS mode); those call the hooks below, and everything specific to group play is here.
 * In a group the scene table runs the vanilla four-door character select (the lobby), the stage select, the match (and a
 * sudden death after a tie) and the results screen, then comes back to the character select. */
#ifndef GROUP_ENGINE_H
#define GROUP_ENGINE_H

struct GameModeState;
struct StartMeleeData;

/* A group session is open and the HANYU scene is running: the character select is the lobby. */
int mp_group_mode(void);

/* online_mode.c's scene table, in group play (state ids passed in: they are that file's). */
void mp_group_css_enter(struct GameModeState* state);
void mp_group_css_exit(struct GameModeState* state, int sss_state);
void mp_group_sss_enter(struct GameModeState* state);
void mp_group_sss_exit(struct GameModeState* state, int css_state, int vs_state);
void mp_group_vs_enter(struct GameModeState* state);
void mp_group_vs_exit(struct GameModeState* state, int results_state, int sudden_state);
void mp_group_sudden_exit(struct GameModeState* state, int results_state);
void mp_group_results_enter(struct GameModeState* state);
void mp_group_results_exit(struct GameModeState* state, int css_state);

/* online.c's match. mp_group_check_block runs first, while `data` is still what vanilla built (the host logs where
 * its block differs from it). */
void mp_group_check_block(const struct StartMeleeData* vanilla, const unsigned char* block);
void mp_group_match_begin(struct StartMeleeData* data, int local_index, const unsigned char* block);
void mp_group_match_exit(void);
/* Each frame's CPU mask from the host's bundle (bit n: port n's input is not coming). A port that was Human in the
 * match block becomes a level 3 CPU the frame its bit appears, on every console alike. Returns 1 when the bit is this
 * console's own (the host has given up on us): the match is over here. `normal` is 0 when the frame came without a
 * bundle (a disconnect), so the mask means nothing. */
int mp_group_frame_mask(unsigned mask, int frame, int normal, int local_index, const unsigned char* block);
/* online_hud.c: the name over port n's damage: 1 and `name` filled for a group's console, 0 for none, -1 when this is
 * not a group match (Slippi's names apply). */
int mp_group_hud_name(int port, char* name, int size);

/* The results screen patch (tools/group_edits/group_results.py, gmresultplayer.c): its state this frame, and the pads it
 * reads instead of the real ones in a group (see group.c). */
typedef struct MpGroupResults {
    int stage;                 /* ResultsData.x1: 0 wait, 1 intro, 2 waiting for a press, 3 the players' panels, 4 end */
    int panels_live;           /* the panels take Start (x0_23 == 2) */
    unsigned char ready;       /* bit n: port n's player is ready (player_data[n].x0_0) */
} MpGroupResults;
/* The name the results screen shows under port's window instead of its fighter's: the game text of the name of the
 * console that played that port, as the match began (a player who dropped to a CPU keeps it; a CPU door has none).
 * 1 with `out` filled, 0 to keep vanilla's label. */
int mp_group_result_name(int port, char* out, int len);
struct HSD_PadStatus;
struct MatchEnd;
void mp_group_results_pads(const MpGroupResults* r, const struct MatchEnd* end, struct HSD_PadStatus* copy,
                           struct HSD_PadStatus* master);

/* The stage select patch (tools/group_edits/group_css.py, mnstagesel.c). */
int mp_group_sss_remote_exit(void);  /* the stage select ended because of another console (read and clear) */
int mp_group_sss_sent(void);         /* this console already sent its pick (read and clear) */

/* The character and stage selects' camera (mn_8022BA1C, which turns it by the first C-stick that is tilted, in port
 * order): their camera proc is mp_group_camera_proc. In a group it runs mn_8022BA1C over every console's C-stick, by
 * port (this console's own live, the others' as they last came), so every console turns its camera alike; anywhere else
 * it is mn_8022BA1C. mp_group_camera_stick replaces port's stick inside it (it does nothing outside a group). */
struct HSD_GObj;
void mp_group_camera_proc(struct HSD_GObj* gobj);
void mp_group_camera_stick(int port, float* x, float* y);

/* Menus outside the CSS that read one port's buttons (name entry from the lobby): in a group, this console's own port
 * reads pad 0, its only pad; any other port is returned as it is. */
int mp_group_menu_pad(int port);

/* The name tag patch (ifnametag.c): a remote player's tag is its text from the lobby, never a local save id. */
int mp_group_tag_off(int slot);
char* mp_group_tag_text(int slot);

#endif
