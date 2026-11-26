/* Group play's menus around the lobby: the rules (group_rules.h). The scenes' side is tools/group_edits/group_menu.py
 * and group_lobby_screen.py; the Solo / Group pages themselves are port/3ds/group/group_menu.c. */
#include <group_rules.h>

int gr_menu_return_row(int from_lobby, int solo_back)
{
    return from_lobby ? 1 : solo_back ? 0 : -1;
}

int gr_choose_step(int poll, int listing)
{
    if (poll == 2) {
        return GR_CHOOSE_LOBBY;   /* a game picked, even under a notice: straight in */
    }
    return listing ? GR_CHOOSE_STAY : GR_CHOOSE_BACK;
}

int gr_choose_opens_list(int listing, int reopened)
{
    return !listing && !reopened;
}
