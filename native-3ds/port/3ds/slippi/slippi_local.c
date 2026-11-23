/* Local wireless play: the Slippi P2P packets (slippi_p2p.c) over a direct
 * console-to-console link instead of ENet. No account, no matchmaking server.
 * config.ini: opponent=@host (this console creates the game, player 1) or
 * opponent=@join (finds a host nearby, player 2). The link itself is in
 * slippi_local_io.c. All functions run with the global lock held. */
#include "slippi_internal.h"
#include "slippi_local.h"
#include <stdio.h>
#include <string.h>

#define SELECTIONS_RESEND_US 200000

void slippi_local_start(int host)
{
    slippi_state *g = &g_slippi;
    char err[160] = "";
    slippi_local_close();
    slippi_p2p_clear();
    memset(&g->match, 0, sizeof(g->match));
    g->match.is_host = g->local_host = host;
    g->match.local_index = host ? 0 : 1;
    g->match.player_count = 2;
    g->local_sel.player_idx = (uint8_t)g->match.local_index;
    g->remote_sel.player_idx = (uint8_t)(host ? 1 : 0);
    memset(g->stats, 0, sizeof(g->stats));
    g->error[0] = '\0';
    sp_log("local: %s", host ? "creating a game" : "looking for a game");
    if (slippi_io_open(host, g->cfg.local_port, err, sizeof(err)) != 0) {
        slippi_fail("%s", err[0] ? err : "Local wireless is unavailable");
        return;
    }
    snprintf(g->user.display_name, sizeof(g->user.display_name), "%s", slippi_io_local_name());
    slippi_set_status(SLIPPI_STATUS_SEARCHING);
}

void slippi_local_send(const uint8_t *data, size_t len)
{
    if (g_slippi.status != SLIPPI_STATUS_CONNECTED) return;
    /* Test hook: pads are dropped by the caller; also lose acks and selections. */
    if (g_slippi.cfg.test_drop_pct && data[0] != SLIPPI_MSG_PAD && (int)(sp_random() % 100) < g_slippi.cfg.test_drop_pct) return;
    slippi_io_send(data, len);
}

void slippi_local_close(void)
{
    slippi_io_close();
}

void slippi_local_service(void)
{
    slippi_state *g = &g_slippi;
    if (g->status != SLIPPI_STATUS_SEARCHING && g->status != SLIPPI_STATUS_CONNECTED) return;
    char peer[64] = "", err[160] = "";
    int st = slippi_io_state(peer, sizeof(peer), err, sizeof(err));
    if (st == SLIPPI_IO_FAILED) {
        slippi_fail("%s", err[0] ? err : "Local wireless failed");
        return;
    }
    if (g->status == SLIPPI_STATUS_SEARCHING) {
        if (st == SLIPPI_IO_CONNECTED) slippi_p2p_local_connected(peer);
        return;
    }
    if (st == SLIPPI_IO_PEER_LEFT) {
        slippi_set_status(SLIPPI_STATUS_DISCONNECTED);
        snprintf(g->error, sizeof(g->error), "Opponent disconnected");
        sp_log("local: the other console left");
        return;
    }
    uint8_t buf[1500];
    for (int i = 0; i < 64; ++i) {
        int n = slippi_io_recv(buf, sizeof(buf));
        if (n <= 0) break;
        slippi_p2p_on_data(buf, (size_t)n);
    }
    /* Selections were reliable under ENet; here a lost datagram would stall the
     * match block for good, so they repeat until the peer's pads show it has them. */
    uint64_t now = sp_time_us();
    if (g->sel_resend_pending && now - g->sel_resend_us >= SELECTIONS_RESEND_US) {
        g->sel_resend_us = now;
        slippi_p2p_resend_selections();
    }
}
