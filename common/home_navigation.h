#ifndef POCKET_TANK_HOME_NAVIGATION_H
#define POCKET_TANK_HOME_NAVIGATION_H
#include "render.h"

typedef enum {
    HOME_DEST_NONE, HOME_DEST_BANK, HOME_DEST_JOURNAL, HOME_DEST_GAMES,
    HOME_DEST_SETTINGS, HOME_DEST_KEEPER, HOME_DEST_SHOP, HOME_DEST_TANKS
} home_destination_t;

/* Common page initialization; platform input owns overlays and BACK history.
 * Keeping these together prevents simulator/hardware menu routes diverging. */
static inline home_destination_t home_open(tank_t *t, int action) {
    switch (action) {
    case HOME_BANK:
        render_bank_open();
        return HOME_DEST_BANK;
    case HOME_JOURNAL:
        render_journal_open();
        return HOME_DEST_JOURNAL;
    case HOME_ALBUM:
    case HOME_THEMES:
        render_journal_open_section(action == HOME_ALBUM ? 1 : 2);
        return HOME_DEST_JOURNAL;
    case HOME_GAMES:
        render_games_open();
        return HOME_DEST_GAMES;
    case HOME_CASINO:
        render_casino_open();
        return HOME_DEST_GAMES;
    case HOME_SETTINGS:
        render_settings_open();
        return HOME_DEST_SETTINGS;
    case HOME_RECOVERY:
        render_settings_open_care();
        return HOME_DEST_SETTINGS;
    case HOME_KEEPER:
        return HOME_DEST_KEEPER;
    case HOME_SHOP:
        render_shop_leave();
        return HOME_DEST_SHOP;
    case HOME_TANKS:
        render_aquariums_open(t);
        return HOME_DEST_TANKS;
    default:
        return HOME_DEST_NONE;
    }
}
#endif
