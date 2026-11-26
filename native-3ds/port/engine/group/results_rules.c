/* Group play on the results screen and after it: the rules (group_rules.h). The scene's side is
 * tools/group_edits/group_results.py and port/engine/group/group.c (mp_group_results_pads, mp_group_results_exit). */
#include <string.h>
#include <group_rules.h>

void gr_results_step(const GrResultsIn *in, GrResultsState *st, GrResultsOut *out)
{
    unsigned want_ready = 0;
    int want_advance = 0, here, over, inj = -1, i;
    int me = in->me;
    memset(out, 0, sizeof *out);
    for (i = 0; i < 4; i++) {
        if ((in->humans >> i & 1) && inj < 0) {
            inj = i;   /* the lobby's presses go to the first Human port */
        }
    }
    if (inj < 0) {
        inj = me;
    }
    /* This console is busy while its results screen is up: the host refuses Start until every console is back on the
     * character select, so the lobby stays there until this screen has seen it. */
    if (in->lob_ok && !(in->lob_busy >> me & 1) && --st->busy_wait <= 0) {
        out->send_busy = 1;
        st->busy_wait = 30;
    }
    /* The lobby counts only in a view that arrived after this console's match ended. Here: the host's results screen
     * is up. Over: the host's screen ended (the lobby is back on the character select), or there is no session. */
    here = in->lob_ok && in->since_end && in->phase == 3;
    over = !in->lob_ok || in->lost || (in->phase == 0 && in->since_end);
    if (over) {
        want_ready = 0xF;
        want_advance = 2;
    } else if (here) {
        want_ready = in->lob_ready;
        want_advance = in->lob_advance;
    }
    out->withhold_mine = in->stage < 3;

    /* this console's press: to the host (kept until the host's results screen is up) */
    if (!over && in->trigger != 0 && ((in->humans >> me & 1) || in->humans == 0)) {
        int advance = 0;
        if (in->stage == 0 && in->canceled && in->humans && (in->trigger & GR_PAD_START)) {
            advance = 1;
        } else if (in->stage == 2) {
            advance = 2;
        }
        if (advance > st->early_advance) {
            st->early_advance = advance;
            out->kept = !here;
        }
        if (here && in->stage == 3 && in->panels_live && (in->trigger & GR_PAD_START)) {
            out->press[out->presses].ready = !(in->lob_ready >> me & 1);
            out->press[out->presses].advance = 0;
            out->presses++;
        }
    }
    if (!over && here && st->early_advance > 0) {
        if (st->early_advance > in->lob_advance) {
            out->press[out->presses].ready = (int) (in->lob_ready >> me & 1);
            out->press[out->presses].advance = st->early_advance;
            out->presses++;
        }
        st->early_advance = 0;
    }

    /* the lobby's state, as presses */
    if (in->stage == 0 && want_advance >= 1 && in->canceled && in->humans) {
        out->inject_start |= 1u << inj;
    } else if (in->stage == 2 && want_advance >= 2) {
        out->inject_a |= 1u << inj;
        out->plug |= 1u << inj;
    } else if (in->stage == 3 && in->panels_live) {
        if (in->humans) {
            for (i = 0; i < 4; i++) {
                if ((in->humans >> i & 1) && (in->shown_ready >> i & 1) != (want_ready >> i & 1)) {
                    out->inject_start |= 1u << i;
                }
            }
        } else if (want_ready & 0xF) {
            for (i = 0; !(want_ready >> i & 1); i++) {
            }
            out->inject_start |= 1u << i;
            out->plug |= 1u << i;
        }
    }
}

/* vanilla's lbTime_8000AF74 on a star count: a + b, kept within 0..255 */
static GrByte stars_add(unsigned a, int b)
{
    if (b > 0) {
        unsigned lo = 255 - (a & 255);
        return (GrByte) (lo > (unsigned) b ? a + (unsigned) b : 255);
    }
    return (GrByte) ((int) (a & 255) > -b ? (int) a + b : 0);
}

void gr_results_stars(const GrByte lobby[4], int lobby_valid, const GrByte pkind[4], const int kos[4], GrByte out[4])
{
    int i;
    for (i = 0; i < 4; i++) {
        out[i] = lobby_valid ? lobby[i] : 0;
        if (pkind[i] == 0) {
            out[i] = stars_add(out[i], kos[i]);
        }
    }
}

int gr_results_auto_hc(int was_auto, int canceled, const GrByte start_hc[4], const GrByte pkind[4], int me, int host,
                       GrAutoHcRule rule, void *match_end, GrByte out[4], unsigned *write)
{
    int i;
    memset(out, 0, 4);
    *write = 0;
    if (!was_auto || canceled) {
        return 0;
    }
    for (i = 0; i < 4; i++) {
        out[i] = start_hc[i] >= 1 && start_hc[i] <= 9 ? start_hc[i] : 5;
    }
    rule(match_end, out);
    for (i = 0; i < 4; i++) {
        if (pkind[i] == 3) {
            out[i] = 0;
            continue;
        }
        if ((pkind[i] == 0 && i == me) || (pkind[i] == 1 && host)) {
            *write |= 1u << i;
        }
    }
    return 1;
}

unsigned gr_results_name_ports(const GrByte slot_type[4])
{
    unsigned ports = 0;
    int i;
    for (i = 0; i < 4; i++) {
        if (slot_type[i] == 0) {
            ports |= 1u << i;
        }
    }
    return ports;
}
