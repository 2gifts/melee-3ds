/* Slippi experiment: the network layer as the engine sees it (native side in
 * port/3ds/slippi/, bridged by game_bridge.S). Only scalars and byte buffers
 * cross; multi-byte values inside buffers are big-endian. */
#ifndef MP_SLIPPI_NET_BRIDGE_H
#define MP_SLIPPI_NET_BRIDGE_H

int mp_platform_slippi_net_start(const char* opponent_code);
int mp_platform_slippi_net_status(void); /* 0 idle 1 matchmaking 2 connecting 3 connected 4 failed 5 disconnected */
const char* mp_platform_slippi_net_error(void);
void mp_platform_slippi_net_poll(void);
void mp_platform_slippi_net_stop(void);

int mp_platform_slippi_net_local_index(void);
void mp_platform_slippi_net_set_selections(int char_id, int color, int stage_id, int stage_selected);
int mp_platform_slippi_net_remote_ready(void);
/* 1 when ready: 0x138-byte game info block, then u32 rng offset, u8 local index, u8 delay. */
int mp_platform_slippi_net_match_block(unsigned char* out);
void mp_platform_slippi_net_new_game(void);

/* 1 = sent, remote pad for `frame` in remote_pad12_out; 2 = skip; 3 = disconnected. */
int mp_platform_slippi_net_send_inputs(int frame, int delay, int finalized_frame, unsigned checksum,
                                       const unsigned char* local_pad12, unsigned char* remote_pad12_out);
int mp_platform_slippi_net_remote_checksum_frame(void);
unsigned mp_platform_slippi_net_remote_checksum_value(void);
int mp_platform_slippi_net_ping_ms(void);

#endif
