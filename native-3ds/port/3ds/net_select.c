/* The engine's online bridge (port/include/slippi_net_bridge.h) serves two backends: Slippi Direct
 * (port/3ds/slippi) and group play over local wireless (port/3ds/group). This is the only place that chooses: a group
 * session, once open (the Melee -> Group menu, or opponent=@host / @join in the engine's config), takes every call
 * until it closes; otherwise they go to Slippi. game_bridge.S maps mp_platform_slippi_net_X to mp_native_net_X here. */
#include "group/group_session.h"

int slippi_net_start(const char *opponent_code);
int slippi_net_status(void);
const char *slippi_net_error(void);
void slippi_net_poll(void);
void slippi_net_stop(void);
int slippi_net_local_index(void);
void slippi_net_set_selections(int char_id, int color, int stage_id, int stage_selected);
int slippi_net_remote_ready(void);
int slippi_net_match_block(unsigned char *out);
void slippi_net_new_game(void);
int slippi_net_send_inputs(int frame, int delay, int finalized_frame, unsigned checksum, const unsigned char *local_pad12,
                           unsigned char *remote_pad12_out);
int slippi_net_remote_checksum(int *unused);
int slippi_net_remote_checksum_frame(void);
unsigned slippi_net_remote_checksum_value(void);
int slippi_net_ping_ms(void);
int slippi_net_time_offset_us(void);

#define GROUP group_session_open()

int mp_native_net_start(const char *code)
{
    if (code && code[0] == '@') return group_net_start(code);   /* @host / @join: local wireless, no account */
    return slippi_net_start(code);
}
int mp_native_net_status(void) { return GROUP ? group_net_status() : slippi_net_status(); }
const char *mp_native_net_error(void) { return GROUP ? group_net_error() : slippi_net_error(); }
void mp_native_net_poll(void) { if (GROUP) group_net_poll(); else slippi_net_poll(); }
void mp_native_net_stop(void) { if (GROUP) group_net_stop(); else slippi_net_stop(); }
int mp_native_net_local_index(void) { return GROUP ? group_net_local_index() : slippi_net_local_index(); }
void mp_native_net_set_selections(int char_id, int color, int stage_id, int stage_selected)
{
    if (GROUP) group_net_set_selections(char_id, color, stage_id, stage_selected);
    else slippi_net_set_selections(char_id, color, stage_id, stage_selected);
}
int mp_native_net_remote_ready(void) { return GROUP ? group_net_remote_ready() : slippi_net_remote_ready(); }
int mp_native_net_match_block(unsigned char *out) { return GROUP ? group_net_match_block(out) : slippi_net_match_block(out); }
void mp_native_net_new_game(void) { if (GROUP) group_net_new_game(); else slippi_net_new_game(); }
int mp_native_net_send_inputs(int frame, int delay, int finalized_frame, unsigned checksum, const unsigned char *local_pad12,
                              unsigned char *out)
{
    if (GROUP) return group_net_send_inputs(frame, delay, finalized_frame, checksum, local_pad12, out);
    return slippi_net_send_inputs(frame, delay, finalized_frame, checksum, local_pad12, out);
}
int mp_native_net_remote_checksum(int *unused) { return GROUP ? group_net_remote_checksum(unused) : slippi_net_remote_checksum(unused); }
int mp_native_net_remote_checksum_frame(void) { return GROUP ? group_net_remote_checksum_frame() : slippi_net_remote_checksum_frame(); }
unsigned mp_native_net_remote_checksum_value(void) { return GROUP ? group_net_remote_checksum_value() : slippi_net_remote_checksum_value(); }
int mp_native_net_ping_ms(void) { return GROUP ? group_net_ping_ms() : slippi_net_ping_ms(); }
int mp_native_net_time_offset_us(void) { return GROUP ? group_net_time_offset_us() : slippi_net_time_offset_us(); }
