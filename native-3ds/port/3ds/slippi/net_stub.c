/* Weak stand-ins for the network layer (port/3ds/slippi/ net code replaces
 * them), so the engine-side driver links before the network lands. */
#define WEAK __attribute__((weak))

WEAK int mp_native_slippi_net_start(const char* code) { (void) code; return -1; }
WEAK int mp_native_slippi_net_status(void) { return 4; }
WEAK const char* mp_native_slippi_net_error(void) { return "network layer not built in"; }
WEAK void mp_native_slippi_net_poll(void) {}
WEAK void mp_native_slippi_net_stop(void) {}
WEAK int mp_native_slippi_net_local_index(void) { return 0; }
WEAK void mp_native_slippi_net_set_selections(int c, int col, int s, int sel) { (void) c; (void) col; (void) s; (void) sel; }
WEAK int mp_native_slippi_net_remote_ready(void) { return 0; }
WEAK int mp_native_slippi_net_match_block(unsigned char* out) { (void) out; return 0; }
WEAK void mp_native_slippi_net_new_game(void) {}
WEAK int mp_native_slippi_net_send_inputs(int f, int d, int ff, unsigned ck, const unsigned char* l, unsigned char* r)
{
    (void) f; (void) d; (void) ff; (void) ck; (void) l; (void) r;
    return 3;
}
WEAK int mp_native_slippi_net_remote_checksum_frame(void) { return 0; }
WEAK unsigned mp_native_slippi_net_remote_checksum_value(void) { return 0; }
WEAK int mp_native_slippi_net_ping_ms(void) { return 0; }
