/* Internal state of the Slippi network client (not part of the engine API). */
#ifndef SLIPPI_INTERNAL_H
#define SLIPPI_INTERNAL_H
#include <stdint.h>
#include <enet/enet.h>
#include "slippi_net.h"
#include "slippi_plat.h"

/* Protocol constants (Dolphin NetPlayProto.h / SlippiNetplay.h). */
#define SLIPPI_MSG_PAD 0x80
#define SLIPPI_MSG_PAD_ACK 0x81
#define SLIPPI_MSG_MATCH_SELECTIONS 0x82
#define SLIPPI_MSG_CONN_SELECTED 0x83
#define SLIPPI_MSG_CHAT 0x84
#define SLIPPI_MSG_COMPLETE_STEP 0x85
#define SLIPPI_MSG_SYNCED_STATE 0x86
#define SLIPPI_PAD_DATA_SIZE 8
#define SLIPPI_PAD_HEADER 14
#define SLIPPI_MAX_PAD_FRAMES_IN 128     /* receiver rejects inputs_to_copy > 128 */
#define SLIPPI_LOCAL_BACKLOG 128         /* sender keeps frames >= newest - 128 */
#define SLIPPI_LOCKSTEP_INTERVAL 30      /* time-offset sample ring */
#define SLIPPI_FRAME_US 16683
#define SLIPPI_CHAT_DISABLED 0x10
#define SLIPPI_MM_PORT_DEFAULT 43113
#define SLIPPI_MM_HOST_PROD "mm.slippi.gg"
#define SLIPPI_APP_VERSION_DEFAULT "3.6.4"  /* Ishiiruka netplay SLIPPI_REV_STR */
#define SLIPPI_P2P_TIMEOUT_MS 8000
#define SLIPPI_MM_CONNECT_TIMEOUT_MS 10000  /* Dolphin: 20 x 500 ms service */
#define SLIPPI_MM_CREATE_TIMEOUT_MS 5000

#define REMOTE_RING 512
#define LOCAL_RING 256
#define ACK_TIMER_RING 512
#define MAX_PEERS 8

typedef struct {
    char uid[64];
    char play_key[128];
    char connect_code[32];
    char display_name[64];
    char latest_version[32];
    int loaded;
} slippi_user;

typedef struct {
    char opponent[32];
    int delay;
    char mm_host[128];
    int mm_port;
    int local_port;           /* 0 = random 41000..50999 like Dolphin */
    char lan_ip[32];          /* forced LAN IP (Dolphin Slippi.LanIP) */
    char app_version[32];
    int code_fullwidth;       /* 1: send the opponent code as the game's full-width Shift-JIS */
    int net_thread;
    int net_thread_core;
    int net_thread_interval_us;
    int chat_enabled;
    int auto_resend;
    int log_packets;
    int send_checksum;        /* 0: checksum_frame/checksum forced to 0 (PC never checks) */
    int test_drop_pct;        /* tests: drop this % of outgoing pad packets */
    int mm_retries;           /* re-ticket after a failed P2P connection; -1 = forever */
    /* self-test */
    int selftest;
    int selftest_frames;
    int selftest_character;
    int selftest_allow_real_mm;
    int selftest_exit;
} slippi_config;

typedef struct {
    uint8_t character_id, character_color, is_character_selected, player_idx;
    uint16_t stage_id;
    uint8_t is_stage_selected;
    uint32_t rng_offset;
    uint8_t team_id, alt_stage_mode;
} slippi_selections;

typedef struct {
    int32_t frame;
    uint64_t time_us;
} slippi_timing;

typedef struct {
    ENetPeer *peer;
    int disconnected;
} slippi_conn;

enum mm_state {
    MM_NONE = 0, MM_RESOLVE, MM_CONNECT, MM_WAIT_CREATE, MM_SEARCH, MM_DONE, MM_ERROR
};

typedef struct {
    /* matchmaking result */
    char match_id[96];
    int local_index;            /* port - 1 */
    int is_host;
    int player_count;
    char remote_name[64], remote_code[32], remote_uid[64];
    char remote_addr[64];       /* "ip:port" chosen for P2P */
    uint8_t stages[16];
    int stage_count;
    uint32_t items;
    /* Each player's quick-chat messages (chatMessages, 16 each; custom or the
     * defaults), in Slippi's order: page Up, Left, Right, Down; each page Up,
     * Left, Right, Down. Empty when the server sent none. */
    char local_chat[16][32], remote_chat[16][32];
} slippi_match;

typedef struct {
    int status;                 /* slippi_status_code */
    char error[160];
    int initialized;

    slippi_config cfg;
    slippi_user user;

    /* search */
    char search_code[32];       /* ASCII opponent code */
    uint8_t search_code_sjis[18];
    int search_code_len;
    int retries;

    /* ENet: one host for matchmaking and then P2P (same local port) */
    ENetHost *host;
    uint16_t host_port;
    ENetPeer *mm_peer;
    ENetAddress mm_addr;
    int mm_state;
    uint64_t mm_deadline_us;
    uint64_t mm_last_log_us;

    slippi_match match;

    /* P2P (SlippiNetplayClient) */
    ENetAddress remote_addr;
    slippi_conn conns[MAX_PEERS];   /* every ENet peer for the remote player (de-dup) */
    ENetPeer *primary;              /* m_server[0] */
    int p2p_connected;              /* connections[0] */
    uint64_t p2p_deadline_us;
    uint32_t disconnect_reason;

    slippi_selections local_sel, remote_sel;
    int remote_sel_count;

    int has_game_started;
    /* local pad queue: ring, newest at local_head */
    struct { int32_t frame, checksum_frame; uint32_t checksum; uint8_t pad[SLIPPI_PAD_DATA_SIZE]; } local[LOCAL_RING];
    int local_count;                /* entries, newest first from local_head */
    int local_head;
    int32_t last_queued_frame;
    uint64_t last_send_us;

    /* remote pads keyed by frame */
    struct { int32_t frame; uint8_t pad[SLIPPI_PAD_DATA_SIZE]; } remote[REMOTE_RING];
    int32_t remote_head_frame;      /* newest queued remote frame, 0 = empty queue */
    int32_t remote_checksum_frame;
    uint32_t remote_checksum;

    int32_t last_frame_acked;
    slippi_timing last_frame_timing;
    slippi_timing ack_timers[ACK_TIMER_RING];
    int ack_first, ack_count;
    uint64_t ping_us;
    int32_t offsets[SLIPPI_LOCKSTEP_INTERVAL];
    int offset_count, offset_idx;

    int chat_received;
    uint64_t last_receive_us;

    unsigned stats[SLIPPI_STAT_COUNT];
} slippi_state;

extern slippi_state g_slippi;

/* config.c */
void slippi_config_defaults(slippi_config *cfg);
int slippi_config_load(slippi_config *cfg, const char *path);
int slippi_user_load(slippi_user *user, const char *path, char *err, int errlen);
int slippi_code_to_sjis(const char *ascii, int fullwidth, uint8_t out[18]);
void slippi_sjis_to_ascii(const uint8_t *in, int len, char *out, int outlen);
int slippi_parse_addr(const char *text, ENetAddress *addr);
void slippi_format_ip(uint32_t host, char *out, int len);

/* mm.c */
void slippi_mm_start(void);
void slippi_mm_service_event(ENetEvent *ev);
void slippi_mm_tick(void);
void slippi_mm_close(void);

/* p2p.c */
void slippi_p2p_start(void);
void slippi_p2p_event(ENetEvent *ev);
void slippi_p2p_tick(void);
void slippi_p2p_reset_game(void);
void slippi_p2p_send_selections(void);
void slippi_p2p_send_pads(void);
void slippi_p2p_send_chat(int id);
int32_t slippi_p2p_time_offset(void);
void slippi_p2p_disconnect(void);
void slippi_p2p_clear(void);

/* net.c (lock held) */
int slippi_send_pad_locked(int frame, const unsigned char *pad, int checksum_frame, unsigned checksum);
int slippi_remote_pad_locked(int frame, unsigned char *out);
void slippi_start_game_locked(void);
void slippi_fail(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void slippi_set_status(int status);
int slippi_create_host(void);
void slippi_destroy_host(void);
/* P2P events that arrive while this side still waits for the mm server's
 * match reply (one host serves both; the opponent can hear first). */
void slippi_early_events_replay(void);
void slippi_early_events_clear(void);

/* bridge.c: native side of port/include/slippi_net_bridge.h */
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

#endif
