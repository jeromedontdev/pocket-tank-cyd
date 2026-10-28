/* Including the renderer lets the test check private page helpers directly. */
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <limits.h>
#include <string.h>
#include "../common/update.h"
#include "../common/render.c"

static uint16_t pixels[448 * 368];
static uint16_t full_frame[320 * 240];
static uint16_t tile_frame[320 * 240 + 2];
static uint16_t repeated[320 * 240];
static uint16_t icon_bank[128 * 1024];
static size_t icon_bank_bytes;
static tank_t help_tank;

static const char *menu_screen;
static int menu_errors, menu_screens, menu_width;
static struct { int x, y, w, h; char text[128]; } menu_texts[160];
static int menu_text_count;
void menu_test_panel(int w, int h) {
    if (menu_screen && w >= 300 && h >= 100) menu_text_count = 0;
}
void menu_test_text(int x, int y, int scale, const char *text) {
    if (!menu_screen || !*text) return;
    int w = text_w(text, scale), h = scale == 1 ? (7 * PAGE_H + TANK_H - 1) / TANK_H : 7 * scale;
    if (x < 0 || x + w > PAGE_W || y < 0 || y + h > PAGE_H) {
        fprintf(stderr, "MENU %d %s: clipped text (%d,%d %dx%d): %s\n", TANK_W, menu_screen, x, y, w, h, text);
        menu_errors++;
    }
    for (int i = 0; i < menu_text_count; i++) {
        if (x < menu_texts[i].x + menu_texts[i].w && x + w > menu_texts[i].x &&
            y < menu_texts[i].y + menu_texts[i].h && y + h > menu_texts[i].y) {
            fprintf(stderr, "MENU %d %s: text overlap: %s / %s\n", TANK_W, menu_screen, menu_texts[i].text, text);
            menu_errors++;
        }
    }
    assert(menu_text_count < 160);
    int i = menu_text_count++;
    menu_texts[i].x = x; menu_texts[i].y = y; menu_texts[i].w = w; menu_texts[i].h = h;
    snprintf(menu_texts[i].text, sizeof menu_texts[i].text, "%s", text);
}
void menu_test_button(int x, int y, int w, int h, const char *label, int scale) {
    if (!menu_screen) return;
    if (x < 0 || y < 0 || x + w > PAGE_W || y + h > PAGE_H ||
        text_w(label, scale) > w - 8 || 7 * scale > h - 4) {
        fprintf(stderr, "MENU %d %s: button label/bounds (%d,%d %dx%d): %s\n", TANK_W, menu_screen, x, y, w, h, label);
        menu_errors++;
    }
}
static void menu_begin(const char *name) {
    assert(TANK_W == menu_width);
    menu_screen = name; menu_text_count = 0;
}
static void menu_update_tap(int x, int y) {
    update_touch(render_frame_x(x), render_frame_y(y), true);
    update_touch(render_frame_x(x), render_frame_y(y), false);
}
static void menu_network_wait(void) {
    extern void net_sim_advance(float dt);
    for (int i = 0; i < 24; i++) { net_sim_advance(.25f); update_tick(.25f); }
}
static void menu_end(const char *gallery) {
    menu_screens++;
    if (gallery) {
        char path[1024]; snprintf(path, sizeof path, "%s/menu-%s-%d.raw", gallery, menu_screen, TANK_W);
        FILE *file = fopen(path, "wb"); assert(file);
        assert(fwrite(full_frame, 2, (size_t)TANK_W * TANK_H, file) == (size_t)TANK_W * TANK_H);
        assert(!fclose(file));
    }
    menu_screen = NULL;
}
static void check_menu_layout(const char *gallery) {
    menu_width = TANK_W;
    tank_t t; tank_init(&t, 77); tank_new_population(&t);
    t.companion.display_compact = TANK_W == 320;
    t.aquariums.clock_theme = CLOCK_COMPACT;
    t.sd_balance = 100000; t.journal.xp = 20000;
    snprintf(t.fish[0].name, sizeof t.fish[0].name, "WIDEWWW");
    char name[64];
    for (int category = 0; category < 5; category++) {
        for (int p = 0; p < (category == 1 ? 2 : 1); p++) {
            render_life_open(); g_home_category = category; g_home_page = p;
            snprintf(name, sizeof name, "home-%d-%d", category, p); menu_begin(name);
            render_life(&t, full_frame, TANK_W); menu_end(gallery);
        }
    }
    for (int section = 0; section <= 6; section++) {
        if (section == 2 || section == 3) continue;
        render_settings_open(); g_settings_section = section;
        render_backup_status(section == 6 ? "UPDATES: WI-FI OR SD CARD" : "", false);
        snprintf(name, sizeof name, "settings-%d", section); menu_begin(name);
        render_settings(&t, full_frame, TANK_W, 100); menu_end(gallery);
    }
    for (int p = CLOCK_MENU; p <= CLOCK_TIME; p++) {
        render_settings_open(); g_clock_settings = true; g_clock_page = p;
        render_backup_status("", false);
        snprintf(name, sizeof name, "clock-%d", p); menu_begin(name);
        render_settings(&t, full_frame, TANK_W, 100); menu_end(gallery);
    }
    for (int section = COMPANION_SECTION_CARE; section <= COMPANION_SECTION_RPG_HELP; section++) {
        if (section == 17) continue;
        companion_open_section(section);
        snprintf(name, sizeof name, "companion-%d", section); menu_begin(name);
        companion_render(&t, full_frame, TANK_W); menu_end(gallery);
    }
    render_aquariums_open(&t);
    for (int pick = -1; pick <= 1; pick++) {
        g_aquarium_pick = pick;
        snprintf(name, sizeof name, "aquariums-%d", pick); menu_begin(name);
        render_aquariums(&t, full_frame, TANK_W); menu_end(gallery);
    }
    render_bank_open();
    for (int tab = 0; tab < 2; tab++) {
        g_bank_tab = tab;
        snprintf(name, sizeof name, "bank-%d", tab); menu_begin(name);
        render_bank(&t, full_frame, TANK_W); menu_end(gallery);
    }
    for (int tab = 0; tab < 3; tab++) {
        render_journal_open_section(tab);
        snprintf(name, sizeof name, "journal-%d", tab); menu_begin(name);
        render_journal(&t, full_frame, TANK_W); menu_end(gallery);
    }
    uint32_t game_menus[3];
    for (int p = 0; p < 3; p++) {
        for (int game = 0; game < 6; game++) {
            t.games.best[game] = t.games_extra.best[game] = t.companion.game_best[game] = UINT16_MAX;
            t.games.rounds[game] = t.games_extra.rounds[game] = t.companion.game_rounds[game] = UINT16_MAX;
        }
        render_games_open();
        snprintf(name, sizeof name, "games-%d", p); menu_begin(name);
        render_games(&t, full_frame, TANK_W); menu_end(gallery);
        uint32_t hash = 2166136261u;
        for (int i = 0; i < TANK_W * TANK_H; i++) hash = (hash ^ full_frame[i]) * 16777619u;
        for (int i = 0; i < p; i++) assert(hash != game_menus[i]);
        game_menus[p] = hash;
        g_game.page = GAME_RECORDS;
        snprintf(name, sizeof name, "records-%d", p); menu_begin(name);
        render_games(&t, full_frame, TANK_W); menu_end(gallery);
        render_games_tap(&t, render_frame_x(350), render_frame_y(80));
    }
    weather_open(); menu_begin("weather"); weather_render(full_frame, TANK_W, 1); menu_end(gallery);
    menu_begin("updates"); render_updates_page(full_frame, TANK_W); menu_end(gallery);
    render_shop_leave();
    for (int p = 0; p < shp_pages(&t); p++) {
        g_shp_page = p;
        snprintf(name, sizeof name, "shop-%d", p); menu_begin(name);
        render_shop(&t, full_frame, TANK_W); menu_end(gallery);
    }
    for (int slot = 0; slot < DECOR_SLOTS; slot++) {
        g_inventory_item = tank_decor_slot_item(slot);
        snprintf(name, sizeof name, "inventory-item-%d", slot); menu_begin(name);
        render_shop(&t, full_frame, TANK_W); menu_end(gallery);
    }
    render_shop_leave();
    g_shp_modal = SD_SHOP_MUSSEL; menu_begin("mussel");
    render_shop(&t, full_frame, TANK_W); menu_end(gallery);
    render_shop_leave();
    for (int game = 0; game < 18; game++) {
        game_start(game_kinds[game]);
        snprintf(name, sizeof name, "game-help-%d", game); menu_begin(name);
        render_games(&t, full_frame, TANK_W); menu_end(gallery);
        g_game.help = false;
        for (int i = 0; i < 30; i++) render_games_tick(0.05f);
        if (g_game.page != GAME_RESULT) {
            snprintf(name, sizeof name, "game-play-%d", game); menu_begin(name);
            render_games(&t, full_frame, TANK_W); menu_end(gallery);
        }
        game_finish();
        snprintf(name, sizeof name, "game-result-%d", game); menu_begin(name);
        render_games(&t, full_frame, TANK_W); menu_end(gallery);
    }
    for (int section = 0; section < 3; section++) {
        companion_open_section(COMPANION_SECTION_COLLECTIONS);
        for (int p = 0; p < section; p++)
            companion_tap(&t, render_frame_x(100), render_frame_y(300));
        snprintf(name, sizeof name, "discoveries-%d", section); menu_begin(name);
        companion_render(&t, full_frame, TANK_W); menu_end(gallery);
    }
    for (int hint = 0; hint < COMPANION_DISCOVERY_COUNT; hint++) {
        companion_open_section(COMPANION_SECTION_HINTS);
        for (int p = 0; p < hint; p++)
            companion_tap(&t, render_frame_x(300), render_frame_y(250));
        snprintf(name, sizeof name, "hint-%d", hint); menu_begin(name);
        companion_render(&t, full_frame, TANK_W); menu_end(gallery);
    }
    for (int item = 0; item < SD_ITEM_COUNT; item++) {
        companion_open_section(COMPANION_SECTION_OBJECTS);
        for (int p = 0; p < item; p++)
            companion_tap(&t, render_frame_x(300), render_frame_y(270));
        snprintf(name, sizeof name, "object-%d", item); menu_begin(name);
        companion_render(&t, full_frame, TANK_W); menu_end(gallery);
    }
    setup_begin(&t);
    if (setup_page() == SETUP_PG_MODE) {
        menu_begin("mode-selection"); render_setup(&t, full_frame, TANK_W, 0); menu_end(gallery);
        setup_cancel(&t);
        t.profile = (profile_state_t){.marker = 0x50524631u, .version = 1,
            .mode = GAME_MODE_NORMAL, .chosen = 1, .reserved = 2};
        setup_begin(&t);
    }
    for (int p = 0; p < SETUP_PG_N && setup_active(); p++) {
        render_tank(&t, full_frame, TANK_W);
        snprintf(name, sizeof name, "setup-%d", setup_page()); menu_begin(name);
        render_setup(&t, full_frame, TANK_W, 0); menu_end(gallery);
        int pg = setup_page();
        bool top = pg == SETUP_PG_BUBBLES || pg == SETUP_PG_NAME_A || pg == SETUP_PG_NAME_B ||
                   pg == SETUP_PG_LOOK_A || pg == SETUP_PG_LOOK_B;
        int next_x = top ? SETUP_TOP_NEXT_X : pg == SETUP_PG_WELCOME ? SETUP_MID_X : SETUP_NEXT_X;
        setup_activate(&t, setup_hit(render_frame_x(next_x + 10),
                                    render_frame_y((top ? SETUP_TOP_BTN_Y : SETUP_BTN_Y) + 10)));
        assert(!setup_active() || setup_page() != pg);
    }
    setup_cancel(&t);
    setup_begin_customize(&t, 0); render_tank(&t, full_frame, TANK_W); menu_begin("fish-customize");
    render_setup(&t, full_frame, TANK_W, 0); menu_end(gallery); setup_cancel(&t);
    setup_begin_rename(&t, 0); render_tank(&t, full_frame, TANK_W); menu_begin("fish-rename");
    render_setup(&t, full_frame, TANK_W, 0); menu_end(gallery); setup_cancel(&t);
    setup_begin_place(&t, 9); render_tank(&t, full_frame, TANK_W); menu_begin("decor-place");
    render_setup(&t, full_frame, TANK_W, 0); menu_end(gallery); setup_cancel(&t);
    render_settings_open(); g_settings_section = 6;
    render_backup_status("PHOTO SAVED: photos/photo-9999.bmp", false);
    menu_begin("screenshot-status"); render_settings(&t, full_frame, TANK_W, 50); menu_end(gallery);
    int value;
    assert(render_settings_tap(render_frame_x(220), render_frame_y(84), &value) == SET_TAP_PHOTO);
    assert(render_settings_tap(render_frame_x(220), render_frame_y(132), &value) == SET_TAP_UPDATES);
    assert(render_settings_tap(render_frame_x(220), render_frame_y(180), &value) == SET_TAP_TOUCH);
    assert(render_settings_tap(render_frame_x(220), render_frame_y(250), &value) == SET_TAP_NONE);
    assert(render_settings_tap(render_frame_x(220), render_frame_y(284), &value) == SET_TAP_WIPE);
    g_settings_section = 4;
    assert(render_settings_tap(render_frame_x(220), render_frame_y(228), &value) == SET_TAP_NONE);
    menu_begin("wipe-confirm"); render_confirm_reset(full_frame, TANK_W, 1); menu_end(gallery);
    for (int p = 0; p < (JOURNAL_THEME_COUNT + 3) / 4; p++) {
        render_journal_open_section(2); g_journal_theme_page = p;
        snprintf(name, sizeof name, "themes-%d", p); menu_begin(name);
        render_journal(&t, full_frame, TANK_W); menu_end(gallery);
    }
    render_journal_open(); g_journal_daily = false; menu_begin("milestone-goals");
    render_journal(&t, full_frame, TANK_W); menu_end(gallery);
    render_aquariums_open(&t); g_aquarium_naming = true;
    memcpy(g_aquarium_draft, "FIFTEEN LETTERS", 16);
    menu_begin("aquarium-name"); render_aquariums(&t, full_frame, TANK_W); menu_end(gallery);
    t.bank_count = FISH_BANK_CAP;
    for (int i = 0; i < FISH_BANK_CAP; i++) {
        memset(&t.bank[i], 0, sizeof t.bank[i]);
        snprintf(t.bank[i].name, sizeof t.bank[i].name, "WIDEWWW");
        t.bank[i].fish.stage = STAGE_ELDER;
        t.bank[i].body = 0xffb155; t.bank[i].accent = 0xeeeeee;
    }
    for (int p = 0; p < (FISH_BANK_CAP + BANK_PER_PAGE - 1) / BANK_PER_PAGE; p++) {
        render_bank_open(); g_bank_page = p;
        snprintf(name, sizeof name, "bank-full-%d", p); menu_begin(name);
        render_bank(&t, full_frame, TANK_W); menu_end(gallery);
    }
    g_bank_pick = 0; menu_begin("bank-detail");
    render_bank(&t, full_frame, TANK_W); menu_end(gallery);
    for (int pending = -1; pending < 1; pending++) {
        g_bank_pending = pending;
        snprintf(name, sizeof name, "bank-confirm-%d", pending); menu_begin(name);
        render_bank(&t, full_frame, TANK_W); menu_end(gallery);
    }
    t.profile.mode = GAME_MODE_CREATIVE;
    t.creative = (creative_state_t){0x43525631u, 1, 1, 0};
    render_aquariums_open(&t); g_aquarium_creative = true;
    menu_begin("creative-fish"); render_aquariums(&t, full_frame, TANK_W); menu_end(gallery);
    g_creative_remove = 0;
    menu_begin("creative-remove"); render_aquariums(&t, full_frame, TANK_W); menu_end(gallery);
    t.profile.mode = GAME_MODE_HARD; memset(&t.creative, 0, sizeof t.creative);
    t.n_fish = 0; render_settings_open_care();
    menu_begin("hard-recovery"); render_settings(&t, full_frame, TANK_W, 50); menu_end(gallery);
    t.n_fish = 2; t.profile.mode = GAME_MODE_NORMAL;
    setup_begin_birth(&t, 0);
    for (int p = 0; p < SETUP_BIRTH_PAGES && setup_active(); p++) {
        render_tank(&t, full_frame, TANK_W);
        snprintf(name, sizeof name, "birth-%d", setup_page()); menu_begin(name);
        render_setup(&t, full_frame, TANK_W, 0); menu_end(gallery);
        int pg = setup_page(), x = pg == SETUP_PG_BORN ? SETUP_MID_X :
            pg == SETUP_PG_NAME_NEW ? SETUP_TOP_NEXT_X : SETUP_NEXT_X;
        setup_activate(&t, setup_hit(render_frame_x(x + 10),
            render_frame_y((pg == SETUP_PG_NAME_NEW ? SETUP_TOP_BTN_Y : SETUP_BTN_Y) + 10)));
    }
    setup_cancel(&t);
    net_port_creds_forget();
    update_wifi_begin(); menu_begin("wifi-scan-busy");
    render_update(full_frame, TANK_W, 0); menu_end(gallery);
    menu_network_wait(); assert(update_page() == UPD_PG_SCAN);
    menu_begin("wifi-networks"); render_update(full_frame, TANK_W, 0); menu_end(gallery);
    menu_update_tap(160, 74); assert(update_page() == UPD_PG_PASSWORD);
    menu_begin("wifi-keyboard"); render_update(full_frame, TANK_W, 0); menu_end(gallery);
    for (int i = 0; i < 63; i++) menu_update_tap(33, 127);
    menu_begin("wifi-keyboard-full"); render_update(full_frame, TANK_W, .5f); menu_end(gallery);
    assert(strlen(update_typed()) == 63);
    assert(net_port_creds_set("Long-Synthetic-Network-Name-12345", "wrongpass"));
    update_wifi_begin(); menu_begin("wifi-saved");
    render_update(full_frame, TANK_W, 0); menu_end(gallery);
    menu_update_tap(110, 265); menu_network_wait();
    assert(!strcmp(update_message_title(), "NOT ACCEPTED"));
    menu_begin("wifi-rejected"); render_update(full_frame, TANK_W, 0); menu_end(gallery);
    menu_update_tap(330, 265);
    assert(update_page() == UPD_PG_PASSWORD);
    menu_begin("wifi-retype-long-name"); render_update(full_frame, TANK_W, 0); menu_end(gallery);
    net_port_creds_forget();
}

static int inventory_touch(const tank_t *t, float x, float y) {
    return render_shop_tap(t, x * TANK_W / PAGE_W, y * TANK_H / PAGE_H);
}
static void check_inventory_ui(void) {
    static bool booted;
    bool normal = TANK_W == 320;
    tank_t t; memset(&t, 0, sizeof t); tank_init(&t, 42);
    if (!booted) { progression_boot(&t); booted = true; }
    tank_resolution(normal);
    tank_init(&t, 42); tank_new_population(&t);
    t.companion.display_compact = TANK_W == 320;
    t.sd_balance = 100000;
    t.journal.xp = 20000;
    float resting_x = 0, x, y, crawl; int moving_seconds = 0;
    for (int second = 0; second < 360; second++) {
        t.clock = (float)second;
        mussel_pose(&t, &x, &y, &crawl);
        assert(x >= 20 && x <= TANK_W - 20 && y >= TANK_BOT - 12 && y <= TANK_BOT - 10);
        if (!second) resting_x = x;
        if (second <= 344) assert(x == resting_x && crawl == 0);
        if (crawl) moving_seconds++;
    }
    assert(moving_seconds == 15);
    t.clock = 359.999f; mussel_pose(&t, &x, &y, &crawl);
    float arrival = x;
    t.clock = 360; mussel_pose(&t, &x, &y, &crawl);
    assert(fabsf(arrival - x) < .01f && crawl == 0);
    t.clock = 0;
    render_shop_leave();
    assert(inventory_touch(&t, 180, 77) == SHOP_TAP_NONE && g_inventory_item == -1);
    assert(!progression_inventory_buy(&t, 9));
    assert(progression_buy(&t, 9));
    int row = 0;
    while (shp_item(&t, row) != 9) { row++; assert(row < shp_rows(&t)); }
    g_shp_page = row / SHP_PER_PAGE;
    assert(inventory_touch(&t, 120, SHP_ROW_Y0 + row % SHP_PER_PAGE * SHP_ROW_DY + 10) == SHOP_TAP_KEPT);
    assert(g_inventory_item == 9);
    tank_t before_removed_controls = t;
    assert(inventory_touch(&t, 344, 194) == SHOP_TAP_KEPT);
    assert(inventory_touch(&t, 120, 230) == SHOP_TAP_KEPT);
    assert(inventory_touch(&t, 280, 230) == SHOP_TAP_KEPT);
    assert(!memcmp(&t, &before_removed_controls, sizeof t));
    int action = inventory_touch(&t, 280, 150);
    assert(action == INV_BUY && render_shop_inventory_action(&t, action));
    action = inventory_touch(&t, 120, 150);
    assert(action == INV_PLACE && render_shop_inventory_action(&t, action));
    assert(setup_is_place() && progression_inventory_placed(&t, 9) == 2);
    int copy = -1;
    for (int i = 0; i < DECOR_EXTRA_CAP; i++) if (t.inventory.extra[i].item == 10) copy = i;
    assert(copy >= 0);
    inventory_extra_t before = t.inventory.extra[copy];
    t.inventory.extra[copy].x += 8;
    setup_cancel(&t);
    assert(!memcmp(&before, &t.inventory.extra[copy], sizeof before));
    setup_begin_extra_place(&t, copy);
    setup_activate(&t, SETUP_HIT_REMOVE);
    assert(setup_is_place() && progression_inventory_placed(&t, 9) == 2);
    setup_activate(&t, SETUP_HIT_REMOVE);
    assert(!setup_active());
    assert(progression_inventory_stored(&t, 9) == 1 && progression_inventory_placed(&t, 9) == 1);
    assert(inventory_touch(&t, 220, 276) == SHOP_TAP_KEPT);
    action = inventory_touch(&t, 220, 276);
    assert(action == INV_SELL && render_shop_inventory_action(&t, action));
    assert(progression_inventory_stored(&t, 9) == 0);
    assert(inventory_touch(&t, 220, 276) == SHOP_TAP_KEPT && g_shp_sell_armed);
    tank_t before_back = t;
    assert(inventory_touch(&t, 344, 64) == SHOP_TAP_KEPT);
    assert(g_inventory_item == -1 && !g_shp_sell_armed && g_shp_modal == -1);
    assert(!memcmp(&t, &before_back, sizeof t));
    assert(inventory_touch(&t, 120, SHP_ROW_Y0 + row % SHP_PER_PAGE * SHP_ROW_DY + 10) == SHOP_TAP_KEPT);
    assert(g_inventory_item == 9 && !g_shp_sell_armed);
    render_shop_leave();
    assert(inventory_touch(&t, 350, 77) == SHOP_TAP_KEPT && g_shp_modal == -1);
    row = 0;
    while (shp_item(&t, row) != SD_SHOP_MUSSEL) { row++; assert(row < shp_rows(&t)); }
    g_shp_page = row / SHP_PER_PAGE;
    assert(inventory_touch(&t, 120, SHP_ROW_Y0 + row % SHP_PER_PAGE * SHP_ROW_DY + 10) == SHOP_TAP_KEPT);
    assert(g_shp_modal == SD_SHOP_MUSSEL);
    action = inventory_touch(&t, SHP_MODAL_X + SHP_MODAL_W / 2,
        SHP_MODAL_Y + SHP_MODAL_H - 12 - MSP_HOW_H + 10);
    assert(action == SHOP_TAP_BUY + SD_SHOP_MUSSEL);
    assert(progression_buy(&t, action - SHOP_TAP_BUY));
    assert(progression_item_owned(&t, SD_SHOP_MUSSEL));
    t.mussel.generated = t.mussel.bank = 3;
    int wallet = t.sd_balance; uint32_t xp = t.journal.xp;
    progression_tick(&t, 1);
    assert(t.sd_balance == wallet + 3 && t.mussel.bank == 0 && t.journal.xp == xp);
    for (int i = 0; i < t.n_fish; i++) { t.fish[i].hunger = 2; t.fish[i].stress = 0; }
    assert(progression_mussel_healthy(&t));
    assert(!progression_mussel_healthy(NULL));
    t.fish[t.n_fish - 1].hunger = 8.5f;
    assert(!progression_mussel_healthy(&t));
    t.fish[t.n_fish - 1].hunger = 2;
    t.fish[t.n_fish - 1].stress = 7;
    assert(!progression_mussel_healthy(&t));
    t.fish[t.n_fish - 1].stress = NAN;
    assert(!progression_mussel_healthy(&t));
    t.fish[t.n_fish - 1].stress = 0;
    t.fish[t.n_fish - 1].hunger = NAN;
    assert(!progression_mussel_healthy(&t));
    t.fish[t.n_fish - 1].hunger = 2;
    render_tile(0, 0);
    for (int state = 0; state < 8; state++) {
        g_shp_modal = state >= 2 ? SD_SHOP_MUSSEL : -1;
        t.mussel.owned = state != 3;
        t.mussel.generated = state == 4 ? MUSSEL_DAILY_LIMIT : 3;
        t.mussel.bank = state == 5 ? MUSSEL_DAILY_LIMIT : 0;
        t.fish[0].hunger = state == 6 ? 8.5f : 2;
        int residents = t.n_fish;
        if (state == 7) t.n_fish = 0;
        g_inventory_item = state ? -1 : 9;
        render_tile(0, 0);
        render_shop(&t, full_frame, TANK_W);
        for (int y = 0; y < TANK_H; y++) {
            render_tile(y, 1); render_shop(&t, tile_frame + 1, TANK_W);
            assert(!memcmp(tile_frame + 1, full_frame + y * TANK_W, TANK_W * 2));
        }
        render_tile(0, 0);
        t.n_fish = residents;
    }
    render_shop_leave();
    puts("Shop: first purchase, Buy/Place, removed-control dead zones, movement-screen removal to storage, confirmed sale, Back disarms sale, mussel payout and tiled UI parity.");
}

static void check_font_coverage(void) {
    static const int sizes[][2] = {{5, 7}, {7, 9}, {8, 10}, {11, 14}, {14, 18}, {23, 31}};
    render_tile(0, 0); render_region(0, 0);
    for (unsigned size = 0; size < sizeof sizes / sizeof *sizes; size++) {
        int w = sizes[size][0], h = sizes[size][1];
        for (const char *ch = FONT_CHARS; *ch; ch++) {
            const uint8_t *g = glyph(*ch);
            memset(full_frame, 0x42, sizeof full_frame);
            ctx_t c = ctx_full(full_frame, TANK_W, 1);
            src_t ink = src_color(0xd9eadf, 1);
            draw_font_glyph(&c, 20, 30, w, h, &ink, g);
            int total = 0, dots = 0;
            for (int row = 0; row < 7; row++) for (int bit = 0; bit < 5; bit++)
                dots += (g[row] >> bit) & 1;
            for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) {
                int coverage = 0;
                /* Independent integral reference: 35 equal-area subcells per output pixel. */
                for (int sy = 0; sy < 7; sy++) for (int sx = 0; sx < 5; sx++)
                    coverage += (g[(y * 7 + sy) / h] >> (4 - (x * 5 + sx) / w)) & 1;
                total += coverage;
                uint16_t expected = 0x4242;
                if (coverage == 35) expected = ink.v;
                else if (coverage) blend565(&expected, &ink, (coverage * 255 + 17) / 35);
                assert(full_frame[(y + 30) * TANK_W + x + 20] == expected);
            }
            assert(total == dots * w * h);
            for (int y = 30; y < 30 + h; y++) {
                memset(tile_frame, 0x42, sizeof tile_frame);
                render_tile(y, 1);
                ctx_t tile = ctx_full(tile_frame + 1, TANK_W, 1);
                tile_frame[0] = tile_frame[TANK_W + 1] = 0xc59a;
                draw_font_glyph(&tile, 20, 30, w, h, &ink, g);
                assert(tile_frame[0] == 0xc59a && tile_frame[TANK_W + 1] == 0xc59a);
                assert(!memcmp(tile_frame + 1, full_frame + y * TANK_W, TANK_W * 2));
            }
            render_tile(0, 0);
        }
    }
    assert(glyph('a') == glyph('A'));
    assert(!glyph(' ') && !glyph(0));
    assert(sizeof FONT5X7 / sizeof *FONT5X7 == strlen(FONT_CHARS));
    assert(render_text_w("MINIMUM", 0) == render_text_w("MINIMUM", 1));
    assert(update_text_w("MINIMUM", 0) == render_text_w("MINIMUM", 1));
    for (int scale = 1; scale <= 6; scale++) {
        const char *text = "MWI 08:34 (100%)";
        render_tile(0, 0); render_region(0, 0);
        memset(full_frame, 0x42, sizeof full_frame);
        render_text(full_frame, TANK_W, 20, 40, scale, 0xffffff, text);
        for (int row = 0; row < TANK_H; row++) {
            memset(tile_frame, 0x42, sizeof tile_frame);
            render_tile(row, 1); render_region(19, 41);
            render_text(tile_frame + 1, TANK_W, 20, 40, scale, 0xffffff, text);
            for (int x = 0; x < TANK_W; x++)
                assert(tile_frame[1 + x] == (x >= 19 && x < 60 ? full_frame[row * TANK_W + x] : 0x4242));
        }
        render_tile(0, 0); render_region(0, 0);
        assert(update_text_w(text, scale) == render_text_w(text, scale));
        memset(repeated, 0x42, sizeof repeated);
        update_text(repeated, TANK_W, 20, 40, scale, 0xffffff, text);
        assert(!memcmp(full_frame, repeated, (size_t)TANK_W * TANK_H * 2));
        for (const char *ch = FONT_CHARS; *ch; ch++) {
            char sample[] = {*ch, 0};
            memset(full_frame, 0x42, sizeof full_frame);
            memset(repeated, 0x42, sizeof repeated);
            render_text(full_frame, TANK_W, 20, 40, scale, 0xffffff, sample);
            update_text(repeated, TANK_W, 20, 40, scale, 0xffffff, sample);
            assert(!memcmp(full_frame, repeated, (size_t)TANK_W * TANK_H * 2));
        }
    }
    render_tile(0, 0); render_region(0, 0);
}

static void check_font_clock_cache(void) {
    tank_t tank = {0};
    static const int themes[] = {CLOCK_COMPACT, CLOCK_NEON, CLOCK_FLIP};
    render_work_caches(true);
    for (unsigned theme = 0; theme < sizeof themes / sizeof *themes; theme++)
        for (int sample = 0; sample < 25; sample++) {
            tank.aquariums.clock_theme = (uint8_t)themes[theme];
            tank.aquariums.clock_layout = clock_layout_set_size(0, CLOCK_JUMBO);
            int64_t now = sample ? INT64_C(1735689600) + (sample - 1) * 3661 : 0;
            memset(full_frame, 0, sizeof full_frame);
            render_clock_overlay(&tank, full_frame, TANK_W, now);
            /* Normal Jumbo Flip's card borders already exceed this budget;
             * retain its live fallback, but never introduce new fallbacks. */
            bool live_flip = TANK_W == 320 && themes[theme] == CLOCK_FLIP;
            if (g_record_overflow && !live_flip)
                fprintf(stderr, "Font clock cache overflow: %dx%d theme %d sample %d\n",
                        TANK_W, TANK_H, themes[theme], sample);
            assert(!g_record_overflow || live_flip);
        }
    render_work_caches(false);
}

static uint16_t replay_background(int index) {
    return (uint16_t)((unsigned)index * 251u + 0x39e7u);
}
static void check_clock_replay_pixels(void) {
    for (unsigned tone = 0; tone < 256; tone++) {
        src_t source = src_color(tone << 16 | ((tone * 73) & 255) << 8 | ((tone * 151) & 255), 1);
        for (unsigned old = 0; old < 65536; old++) {
            uint16_t expected = (uint16_t)old, actual = expected;
            blend565(&expected, &source, 255);
            blend255_span(&actual, 1, &source);
            assert(actual == expected);
        }
    }
    tank_t tank = {0};
    for (int theme = CLOCK_OFF + 1; theme < CLOCK_THEME_COUNT; theme++)
        for (int size = 0; size < CLOCK_SIZE_COUNT; size++)
            for (int flags = 0; flags < 2; flags++) {
                tank.aquariums.clock_theme = (uint8_t)theme;
                tank.aquariums.clock_layout = clock_layout_set_size(0, size);
                tank.aquariums.clock_flags = flags ? CLOCK_HIDE_PANEL | CLOCK_12H : 0;
                int64_t now = INT64_C(1792803600) + theme * 3661;
                for (int i = 0; i < TANK_W * TANK_H; i++)
                    full_frame[i] = repeated[i] = replay_background(i);
                render_work_caches(false);
                clock_overlay_live(&tank, full_frame, TANK_W, now);
                render_work_caches(true);
                render_clock_overlay(&tank, repeated, TANK_W, now);
                assert(!memcmp(full_frame, repeated, (size_t)TANK_W * TANK_H * 2));
                if (!g_record_overflow) for (size_t i = 0; i < g_clock_run_count; i++) {
                    const paint_run_t *run = clock_run(i);
                    for (unsigned band = 0; band < CLOCK_INDEX_BANDS; band++) {
                        bool intersects = run->y < (int)(band + 1) * CLOCK_INDEX_ROWS &&
                            run->y + (run->alpha >> 9) >= (int)band * CLOCK_INDEX_ROWS;
                        bool indexed = (g_clock_runs[i / CLOCK_RUN_PAGE]->bands[band][i % CLOCK_RUN_PAGE / 32] >>
                                        (i % 32)) & 1;
                        assert(indexed == intersects);
                    }
                }
                for (int i = 0; i < TANK_W * TANK_H; i++) repeated[i] = replay_background(i);
                render_region(7, TANK_W - 19);
                for (int y = 0; y < TANK_H; y += 37) {
                    render_tile(y, TANK_H - y < 37 ? TANK_H - y : 37);
                    render_clock_overlay(&tank, repeated + y * TANK_W, TANK_W, now);
                }
                render_tile(0, 0); render_region(0, 0);
                for (int y = 0; y < TANK_H; y++) for (int x = 0; x < TANK_W; x++) {
                    int i = y * TANK_W + x;
                    assert(repeated[i] == (x >= 7 && x < TANK_W - 12 ? full_frame[i] : replay_background(i)));
                }
            }
    render_work_caches(false);
    puts("PASS: cached clock spans match live pixels on nonuniform RGB565 backgrounds, all faces/sizes, transparent panels and clipped tiles/regions");
}

static void check_ornament_spans(void) {
    for (int item = 9; item < SD_DECOR_FIRST + DECOR_NEW_COUNT; item++) {
        if (!tank_decor_placeable(item)) continue;
        for (int mode = 0; mode < 8; mode++) {
            bool final = (mode & 1) != 0, lit = (mode & 2) != 0;
            int scale = mode & 4 ? 2 : 1, y0 = TANK_H / 2 - 5, x0 = -3;
            for (int i = 0; i < TANK_W * TANK_H; i++)
                full_frame[i] = repeated[i] = replay_background(i);
            render_tile(TANK_H / 2, 17); render_region(7, 93);
            ctx_t expected = ctx_full(full_frame, TANK_W, mode & 4 ? .45f : 1);
            ctx_t actual = ctx_full(repeated, TANK_W, expected.dim);
            for (int y = 0; y < tank_ornament_h(item); y++)
                for (int x = 0; x < tank_ornament_w(item); x++) {
                    uint32_t rgb = ornament_pixel(item, x, y);
                    if (!rgb) continue;
                    src_t source = src_color(shop_art_color(rgb, lit, 0x173942), expected.dim);
                    for (int row = 0; row < scale; row++) {
                        int left = x0 + x * scale, top = y0 + y * scale + row;
                        if (final) span_final(&expected, left, left + scale - 1, top, &source, 255);
                        else span(&expected, left, left + scale - 1, top, &source, 255);
                    }
                }
            draw_ornament(&actual, item, x0, y0, scale, final, lit, 0x173942);
            assert(!memcmp(full_frame, repeated, (size_t)TANK_W * TANK_H * 2));
            render_tile(0, 0); render_region(0, 0);
        }
    }
    puts("PASS: ornament row spans preserve per-pixel reference artwork, dim/locked colors, final lighting and clipped/scaled geometry");
}

static void check_companion_navigation(void) {
    tank_t tank;
    tank_init(&tank, 77);
    tank_new_population(&tank);
    for (int section = COMPANION_SECTION_CARE; section <= COMPANION_SECTION_RPG_EVENT; section++) {
        if (section == 17) continue;
        companion_open_section(section);
        companion_render(&tank, full_frame, TANK_W);
        memcpy(repeated, full_frame, (size_t)TANK_W * TANK_H * 2);
        render_rect(repeated, TANK_W, 346, 8, 90, 30, 0x264d55);
        render_text(repeated, TANK_W, 354, 15, 2, 0xe7f4dc, "BACK");
        assert(!memcmp(full_frame, repeated, (size_t)TANK_W * TANK_H * 2));
    }
    companion_open_section(COMPANION_SECTION_WARDROBE);
    companion_tap(&tank, render_frame_x(60), render_frame_y(180));
    companion_render(&tank, full_frame, TANK_W);
    memcpy(repeated, full_frame, (size_t)TANK_W * TANK_H * 2);
    render_rect(repeated, TANK_W, 304, 86, 132, 30, 0x102b36);
    assert(!memcmp(full_frame, repeated, (size_t)TANK_W * TANK_H * 2));
    companion_open_section(COMPANION_SECTION_CREATURE_COLORS);
    companion_render(&tank, full_frame, TANK_W);
    memcpy(repeated, full_frame, (size_t)TANK_W * TANK_H * 2);
    render_rect(repeated, TANK_W, 304, 86, 132, 30, 0x264d55);
    render_text(repeated, TANK_W, 312, 93, 2, 0xe7f4dc, "NEXT TYPE");
    assert(!memcmp(full_frame, repeated, (size_t)TANK_W * TANK_H * 2));
}

static void check_content_page_tiles(void) {
    tank_t t; tank_init(&t, 799); tank_new_population(&t);
    t.sd_balance = 100000; t.journal.xp = 20000;
    for (int kind = 0; kind < 3; kind++) {
        int count = kind == 0 ? DECOR_NEW_COUNT : WEAR_COUNT - 1;
        if (kind) companion_open_section(COMPANION_SECTION_WARDROBE);
        for (int item = 0; item < count; item++) {
            if (!kind) {
                render_shop_leave();
                g_shp_modal = SD_DECOR_FIRST + item;
            } else if (kind == 2) {
                companion_tap(&t, render_frame_x(60), render_frame_y(180));
            }
            render_tile(0, 0); render_region(0, 0);
            memset(full_frame, 0, sizeof full_frame);
            if (!kind) render_shop(&t, full_frame, TANK_W);
            else companion_render(&t, full_frame, TANK_W);
            for (int row = 0; row < TANK_H; row += 40) {
                int height = TANK_H - row < 40 ? TANK_H - row : 40;
                int length = TANK_W * height;
                memset(tile_frame, 0, sizeof tile_frame);
                tile_frame[0] = tile_frame[length + 1] = 0x96a5;
                render_tile(row, height);
                if (!kind) render_shop(&t, tile_frame + 1, TANK_W);
                else companion_render(&t, tile_frame + 1, TANK_W);
                assert(tile_frame[0] == 0x96a5 && tile_frame[length + 1] == 0x96a5);
                assert(!memcmp(tile_frame + 1, full_frame + row * TANK_W,
                               (size_t)length * sizeof *full_frame));
            }
            render_tile(0, 0);
            if (kind == 2) companion_tap(&t, render_frame_x(390), render_frame_y(23));
            if (kind) companion_tap(&t, render_frame_x(400), render_frame_y(235));
        }
    }
    render_shop_leave(); render_tile(0, 0); render_region(0, 0);
    puts("PASS: all twenty new shop modals and twelve wardrobe/color pages match exact guarded 40-row rendering");
}

static void check_fish_material(void) {
    const float xs[] = {28, 65, 87, 70, 32, 17};
    const float ys[] = {21, 18, 40, 63, 67, 43};
    render_tile(0, 0);
    memset(full_frame, 0, sizeof full_frame);
    memset(repeated, 0, sizeof repeated);
    ctx_t flat = {.fb = full_frame, .stride = TANK_W, .w = TANK_W, .h = TANK_H, .dim = 1};
    ctx_t shaded = flat; shaded.fb = repeated;
    fill_poly(&flat, xs, ys, 6, 0x659dba);
    fill_fish_material(&shaded, xs, ys, 6, 0x659dba, .3f);
    uint16_t colors[5]; int count = 0;
    for (int i = 0; i < TANK_W * TANK_H; i++) {
        assert((full_frame[i] != 0) == (repeated[i] != 0));
        if (!repeated[i]) continue;
        int k = 0;
        while (k < count && colors[k] != repeated[i]) k++;
        if (k == count) { assert(count < 5); colors[count++] = repeated[i]; }
    }
    assert(count == 5);
    flat.dim = .2f;
    memset(full_frame, 0, sizeof full_frame);
    fill_fish_material(&flat, xs, ys, 6, 0x659dba, .3f);
    for (int i = 0; i < TANK_W * TANK_H; i++) {
        uint16_t dim = full_frame[i], lit = repeated[i];
        assert((dim >> 11) <= (lit >> 11));
        assert(((dim >> 5) & 63) <= ((lit >> 5) & 63));
        assert((dim & 31) <= (lit & 31));
    }
    for (int row = 0; row < TANK_H; row++) {
        memset(tile_frame, 0, sizeof tile_frame);
        ctx_t tile = shaded; tile.fb = tile_frame + 1; tile.oy = row; tile.h = 1;
        tile_frame[0] = tile_frame[TANK_W + 1] = 0xc59a;
        fill_fish_material(&tile, xs, ys, 6, 0x659dba, .3f);
        assert(tile_frame[0] == 0xc59a && tile_frame[TANK_W + 1] == 0xc59a);
        assert(!memcmp(tile_frame + 1, repeated + row * TANK_W, TANK_W * sizeof *repeated));
    }
}

static void capture_typography(const char *directory) {
    render_tile(0, 0); render_region(0, 0);
    ctx_t c = ctx_page(full_frame, TANK_W);
    rect_fill(&c, 0, 0, PAGE_W, PAGE_H, 0x031015);
    draw_text(&c, 18, 14, 3, 0xffffff, "POCKET TANK CYD");
    draw_text(&c, 18, 52, 2, 0x9fd8e2, "NORMAL / ENLARGED");
    draw_text(&c, 18, 80, 1, 0xffffff, "ABCDEFGHIJKLMNOPQRSTUVWXYZ");
    draw_text(&c, 18, 102, 2, 0xffffff, "ABCDEFGHIJKLM");
    draw_text(&c, 18, 126, 2, 0xffffff, "NOPQRSTUVWXYZ");
    draw_text(&c, 18, 154, 2, 0x9fd8e2, "0123456789 12:34 25C");
    draw_text(&c, 18, 182, 1, 0xa4c5ce, "SAND DOLLARS: 1234 / DAILY CARE");
    draw_text(&c, 18, 204, 2, 0xffffff, "WI-FI / SYNC 100%");
    button(&c, 18, 240, 194, 44, 0x1c2f36, 0x9fd8e2, "SETTINGS", 2);
    button(&c, 232, 240, 194, 44, 0x1c2f36, 0x9fd8e2, "BACK", 2);
    draw_text(&c, 18, 304, 3, 0xffffff, "BOLT PIP KELP NORI");
    char path[1024];
    int length = snprintf(path, sizeof path, "%s/typography-%d.raw", directory, TANK_W);
    assert(length > 0 && length < (int)sizeof path);
    FILE *file = fopen(path, "wb"); assert(file);
    assert(fwrite(full_frame, sizeof *full_frame, TANK_W * TANK_H, file) == (size_t)TANK_W * TANK_H);
    assert(!fclose(file));
}

static void capture_fish_materials(const char *directory) {
    for (int accessories = 0; accessories < 2; accessories++) {
        ctx_t c = {.fb = full_frame, .stride = TANK_W, .w = TANK_W, .h = TANK_H, .dim = 1};
        for (int i = 0; i < TANK_W * TANK_H; i++) full_frame[i] = rgb565(0x102b34, 1);
        int count = accessories ? WEAR_COUNT - 1 : FISH_VARIETY_COUNT;
        for (int i = 0; i < count; i++) {
            fish_t f = help_tank.fish[0];
            float scale = TANK_W / 320.0f;
            f.x = (i % 2 ? 245 : 85) * scale;
            f.y = (accessories ? 45 + i / 2 * 72 : 25 + i / 2 * 46) * scale;
            f.size = (accessories ? 1.35f : .92f) * scale;
            f.stage = STAGE_ADULT; f.yaw = f.yaw_tail = 1; f.heading = 0;
            f.color = 0x69b6bf; f.fin = 0xb47faf; f.accent = 0xf0d399;
            f.hunger = f.stress = 0; f.speed = 0; f.traits = 0; f.pattern = PATTERN_PLAIN;
            f.variety = accessories ? FISH_CLASSIC : i;
            f.wearable = accessories ? i + 1 : WEAR_NONE; f.wear_colors = 0;
            draw_fish_core(&c, &f, 0, false, f.size);
            draw_text(&c, (int)(f.x - 58 * scale), (int)(f.y + 20 * scale), 1,
                      0xb6d3ce, accessories ? WEAR_NAMES[i + 1] : FISH_VARIETY_NAMES[i]);
        }
        char path[1024];
        int length = snprintf(path, sizeof path, "%s/fish-%s-%d.raw", directory,
                              accessories ? "accessories" : "bodies", TANK_W);
        assert(length > 0 && length < (int)sizeof path);
        FILE *file = fopen(path, "wb"); assert(file);
        assert(fwrite(full_frame, sizeof *full_frame, TANK_W * TANK_H, file) == (size_t)TANK_W * TANK_H);
        assert(!fclose(file));
    }
}

static void capture_content_raw(const char *directory, const char *name) {
    char path[1024];
    int length = snprintf(path, sizeof path, "%s/%s-%d.raw", directory, name, TANK_W);
    assert(length > 0 && length < (int)sizeof path);
    FILE *file = fopen(path, "wb"); assert(file);
    assert(fwrite(full_frame, sizeof *full_frame, (size_t)TANK_W * TANK_H, file) ==
           (size_t)TANK_W * TANK_H);
    assert(!fclose(file));
}

static void capture_new_content(const char *directory) {
    static const char *const names[DECOR_NEW_COUNT] = {
        "LIGHTHOUSE", "SAILBOAT", "ROPE COIL", "WHEEL", "CARGO CRATE",
        "DIVE BELL", "STONE BRIDGE", "PAGODA", "STONE LANTERN", "BONSAI",
        "TORII", "CRESCENT", "SPLIT GEODE", "AMMONITE", "CERAMIC MUSHROOMS",
        "SANDCASTLE", "ARROW SIGN", "SEABED MAILBOX", "PARCHMENT MAP", "SUBMARINE"
    };
    const uint16_t background = rgb565(0x102b34, 1);
    render_tile(0, 0); render_region(0, 0);
    ctx_t c = {.fb = full_frame, .stride = TANK_W, .dim = 1,
               .ox = 0, .oy = 0, .w = TANK_W, .h = TANK_H, .scaled = false};
    for (int item = SD_DECOR_FIRST; item < SD_ITEM_COUNT; item++) {
        for (int px = 0; px < TANK_W * TANK_H; px++) full_frame[px] = background;
        draw_ornament(&c, item, (TANK_W - 40) / 2, (TANK_H - 40) / 2, 1, false, true, 0);
        char title[40];
        snprintf(title, sizeof title, "%d %s", item, names[item - SD_DECOR_FIRST]);
        draw_text(&c, 4, 4, 1, 0xf1e8ca, title);
        char name[32];
        snprintf(name, sizeof name, "decoration-%02d", item);
        capture_content_raw(directory, name);
    }
    if (TANK_W == 320) {
        enum { CELL_W = 120, CELL_H = 72, SHEET_W = 5 * CELL_W, SHEET_H = 4 * CELL_H };
        static uint16_t sheet[SHEET_W * SHEET_H];
        static const char *const labels[DECOR_NEW_COUNT] = {
            "LIGHTHOUSE", "SAILBOAT", "ROPE COIL", "WHEEL", "CARGO CRATE",
            "DIVE BELL", "STONE BRIDGE", "PAGODA", "STONE LANTERN", "BONSAI",
            "TORII", "CRESCENT", "SPLIT GEODE", "AMMONITE", "MUSHROOMS",
            "SANDCASTLE", "ARROW SIGN", "MAILBOX", "PARCHMENT MAP", "SUBMARINE"
        };
        ctx_t sheet_ctx = {.fb = sheet, .stride = SHEET_W, .dim = 1,
                           .ox = 0, .oy = 0, .w = SHEET_W, .h = SHEET_H, .scaled = false};
        for (int px = 0; px < SHEET_W * SHEET_H; px++) sheet[px] = background;
        for (int i = 0; i < DECOR_NEW_COUNT; i++) {
            /* Each cell: centered ID, centered full name, then the 40x40 ornament. */
            int x = (i % 5) * CELL_W, y = (i / 5) * CELL_H;
            char id[4];
            snprintf(id, sizeof id, "%d", SD_DECOR_FIRST + i);
            const char *lines[2] = {id, labels[i]};
            for (int line = 0; line < 2; line++) {
                int width = text_w(lines[line], 1);
                assert(width <= CELL_W - 4);
                draw_text(&sheet_ctx, x + (CELL_W - width) / 2, y + 2 + line * 10, 1,
                          line ? 0xf1e8ca : 0xf1d89b, lines[line]);
            }
            draw_ornament(&sheet_ctx, SD_DECOR_FIRST + i, x + (CELL_W - 40) / 2, y + 23,
                          1, false, true, 0);
        }
        char path[1024];
        int length = snprintf(path, sizeof path, "%s/new-decoration-contact-%dx%d.raw",
                              directory, SHEET_W, SHEET_H);
        assert(length > 0 && length < (int)sizeof path);
        FILE *file = fopen(path, "wb"); assert(file);
        assert(fwrite(sheet, sizeof *sheet, (size_t)SHEET_W * SHEET_H, file) ==
               (size_t)SHEET_W * SHEET_H);
        assert(!fclose(file));
    }

    static const char *const wearable_names[WEAR_COUNT - WEAR_SAILOR] = {
        "SAILOR", "PIRATE", "BERET", "SUNHAT", "VIKING", "SCARF"
    };
    const float tank_scale = (float)TANK_W / 240.0f;
    for (int readable = 0; readable < 2; readable++) {
        for (int px = 0; px < TANK_W * TANK_H; px++) full_frame[px] = background;
        for (int i = 0; i < WEAR_COUNT - WEAR_SAILOR; i++) {
            int column = i % 3, row = i / 3;
            fish_t f = help_tank.fish[0];
            f.x = (column * 2 + 1) * (float)TANK_W / 6;
            f.y = row ? TANK_H * 0.72f : TANK_H * 0.27f;
            f.size = tank_scale * (readable ? 1.35f : 1.0f);
            f.stage = STAGE_ADULT; f.yaw = f.yaw_tail = 1; f.heading = 0;
            f.color = 0x69b6bf; f.fin = 0xb47faf; f.accent = 0xf0d399;
            f.hunger = f.stress = f.speed = 0; f.traits = 0; f.pattern = PATTERN_PLAIN;
            f.variety = FISH_CLASSIC; f.wearable = WEAR_SAILOR + i; f.wear_colors = 0;
            draw_fish_core(&c, &f, 0, false, f.size);
            int label_y = (int)(f.y + 16 * f.size + 2);
            draw_text(&c, (int)f.x - text_w(wearable_names[i], 1) / 2,
                      label_y, 1, 0xb6d3ce, wearable_names[i]);
        }
        capture_content_raw(directory, readable ? "wearables-readable" : "wearables-tank-scale");
    }
}

static void check_tank_materials(void) {
    static const int items[] = {9, 10, 11, 12, 13, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29,
                               SD_SCENERY_GLASS, SD_SCENERY_BRAIN, SD_SCENERY_TIKI,
                               SD_SCENERY_BOTTLE, SD_SCENERY_ANEMONE, SD_SCENERY_TEAPOT,
                               43, 44, 45, 46, 47, 48, 49, 50, 51, 52,
                               53, 54, 55, 56, 57, 58, 59, 60, 61, 62};
    for (unsigned i = 0; i < sizeof items / sizeof *items; i++) {
        int item = items[i], width = tank_ornament_w(item), height = tank_ornament_h(item);
        uint16_t colors[32];
        int count = 0, painted = 0, transparent = 0;
        for (int y = -1; y <= height; y++) for (int x = -1; x <= width; x++) {
            uint32_t rgb = ornament_pixel(item, x, y);
            assert(rgb == ornament_pixel(item, x, y));
            if (x < 0 || y < 0 || x >= width || y >= height) assert(!rgb);
            else if (!rgb) transparent++;
            else {
                painted++;
                uint16_t color = rgb565(rgb, 1), dark = rgb565(rgb, 0.2f);
                assert((dark >> 11) <= (color >> 11));
                assert(((dark >> 5) & 63) <= ((color >> 5) & 63));
                assert((dark & 31) <= (color & 31));
                int k = 0;
                while (k < count && colors[k] != color) k++;
                if (k == count) {
                    assert(count < 32);
                    colors[count++] = color;
                }
            }
        }
        assert(painted > 40 && transparent > 0);
        assert(count >= (SD_IS_NEW_DECOR(item) ? 7 : 4));
    }
    assert(!ornament_pixel(20, 29, 35)); /* The stone arch is still a real opening. */
    assert(ornament_pixel(9, 23, 23) != ornament_pixel(9, 21, 23)); /* Recessed latch. */
    assert(ornament_pixel(26, 11, 10) != ornament_pixel(26, 12, 10)); /* Hoop rivet. */
    static const struct { int item, x0, y0, x1, y1; } details[] = {
        {43, 19, 8, 18, 8},   /* Glazed lantern and mullion. */
        {44, 24, 13, 24, 14}, /* Sewn sail seam. */
        {45, 20, 17, 20, 18}, /* Braided rope. */
        {46, 20, 18, 23, 18}, /* Brass wheel hub. */
        {47, 6, 12, 7, 12},   /* Iron corner rivet. */
        {48, 20, 18, 20, 21}, /* Porthole reflection. */
        {49, 20, 10, 20, 11}, /* Parapet cap. */
        {50, 20, 17, 21, 17}, /* Recessed balcony lattice. */
        {51, 20, 17, 19, 17}, /* Lantern window frame. */
        {52, 20, 31, 20, 32}, /* Planter lip and soil. */
        {53, 20, 10, 20, 11}, /* Torii plaque border. */
        {54, 6, 18, 9, 18},   /* Crescent bevel. */
        {55, 20, 21, 20, 16}, /* Geode cavity and facets. */
        {56, 17, 19, 20, 20}, /* Fossil spiral and chamber. */
        {57, 17, 9, 20, 9},   /* Glazed mushroom spot. */
        {58, 20, 19, 21, 19}, /* Castle arrow slit. */
        {59, 13, 14, 14, 14}, /* Carved fish eye. */
        {60, 14, 20, 14, 21}, /* Envelope fold. */
        {61, 29, 17, 28, 17}, /* Map compass rose. */
        {62, 10, 24, 10, 25}  /* Submarine window reflection. */
    };
    int lost_details = 0;
    for (unsigned i = 0; i < sizeof details / sizeof *details; i++) {
        uint32_t a = ornament_pixel(details[i].item, details[i].x0, details[i].y0);
        uint32_t b = ornament_pixel(details[i].item, details[i].x1, details[i].y1);
        if (!a || !b || rgb565(a, 1) == rgb565(b, 1)) {
            fprintf(stderr, "Decoration %d lost its defining detail (%06x / %06x)\n", details[i].item, a, b);
            lost_details++;
        }
    }
    assert(!lost_details);
    assert(!ornament_pixel(49, 20, 34));
    assert(!ornament_pixel(53, 20, 26));
    assert(!ornament_pixel(54, 27, 16));
    bool work_enabled = g_work_enabled;
    render_work_caches(true);
    tank_t t = {0};
    for (int item = SD_DECOR_FIRST; item < SD_DECOR_FIRST + DECOR_NEW_COUNT; item++) {
        t.inventory.extra[0].item = t.inventory.extra[1].item = (uint8_t)(item + 1);
        ornament_select(&t);
        const ornament_art_t *art = ornament_art(item);
        assert(art && art->width == 40 && art->height == 40 && !g_ornament_failed);
        for (int y = -1; y <= 40; y++) for (int x = -1; x <= 40; x++)
            assert(ornament_cached_pixel(item, x, y) == ornament_pixel(item, x, y));
    }
    render_work_caches(false);
    render_work_caches(work_enabled);
}

static void draw_test_effect(ctx_t *c, int kind, int pose) {
    static const int widths[] = {20, 28, 22, 40, 12}, heights[] = {15, 15, 10, 24, 19};
    if (kind < VISITOR_SPECIES) {
        theme_detail_t d = {.x = 32, .y = 40, .w = widths[kind], .h = heights[kind],
                            .kind = 4 + kind, .pose = pose};
        theme_detail_draw(c, &d);
    } else if (kind == 5 || kind == 6) {
        bubble_t b = {.x = 40.3f, .y = 48.7f, .column = kind == 6};
        draw_water_bubble(c, &b);
    } else draw_food_pellet(c, 40.3f, 48.7f);
}

static void check_aquarium_effects(void) {
    static const int widths[] = {20, 28, 22, 40, 12}, heights[] = {15, 15, 10, 24, 19};
    for (int kind = 0; kind < 8; kind++) for (int pose = 0; pose < 2; pose++)
        for (int night = 0; night < 2; night++) {
            render_tile(0, 0);
            memset(full_frame, 0x26, sizeof full_frame);
            ctx_t c = {.fb = full_frame, .stride = TANK_W, .w = TANK_W, .h = TANK_H, .dim = night ? .45f : 1};
            draw_test_effect(&c, kind, pose);
            int changed = 0;
            for (int y = 0; y < TANK_H; y++) for (int x = 0; x < TANK_W; x++)
                if (full_frame[y * TANK_W + x] != 0x2626) {
                    changed++;
                    if (kind < VISITOR_SPECIES)
                        assert(x >= 32 && x < 32 + widths[kind] && y >= 40 && y < 40 + heights[kind]);
                    else assert(x >= 35 && x <= 45 && y >= 43 && y <= 53);
                }
            assert(changed >= 4);
            for (int row = 0; row < TANK_H; row++) {
                memset(tile_frame, 0x26, sizeof tile_frame);
                ctx_t tile = c; tile.fb = tile_frame + 1; tile.oy = row; tile.h = 1;
                tile_frame[0] = tile_frame[TANK_W + 1] = 0xc59a;
                draw_test_effect(&tile, kind, pose);
                assert(tile_frame[0] == 0xc59a && tile_frame[TANK_W + 1] == 0xc59a);
                assert(!memcmp(tile_frame + 1, full_frame + row * TANK_W, TANK_W * sizeof *full_frame));
            }
        }
    render_tile(0, 0);
    tank_t t = help_tank;
    t.tool = TOOL_SPONGE; t.drag_has_prev = true; t.drag_dist = 100;
    t.drag_px = 40; t.drag_py = 50; t.wipe_sounded = false; t.slash_cut = false;
    memset(full_frame, 0, sizeof full_frame);
    draw_care_stroke(&t, full_frame, TANK_W);
    for (int i = 0; i < TANK_W * TANK_H; i++) assert(!full_frame[i]);
    t.wipe_sounded = true;
    assert(care_stroke_visible(&t));
    tank_t before = t;
    draw_care_stroke(&t, full_frame, TANK_W);
    assert(!memcmp(&t, &before, sizeof t));
    assert(full_frame[47 * TANK_W + 40]);
    t.drag_has_prev = false; assert(!care_stroke_visible(&t));
    t.drag_has_prev = true; t.tool = TOOL_SCISSORS;
    assert(!care_stroke_visible(&t));
    t.slash_cut = true; assert(care_stroke_visible(&t));
    t.tool = TOOL_HAND; assert(!care_stroke_visible(&t));
}

static void capture_aquarium_details(const char *directory) {
    static const char *const names[] = {"DRIFTER", "SEA TURTLE", "GHOST FISH", "MANTA", "LANTERN SQUID"};
    static const int widths[] = {20, 28, 22, 40, 12}, heights[] = {15, 15, 10, 24, 19};
    ctx_t c = {.fb = full_frame, .stride = TANK_W, .w = TANK_W, .h = TANK_H, .dim = 1};
    for (int i = 0; i < TANK_W * TANK_H; i++) full_frame[i] = rgb565(0x102b34, 1);
    for (int kind = 0; kind < VISITOR_SPECIES; kind++) {
        for (int pose = 0; pose < 2; pose++) {
            theme_detail_t d = {.x = 20 + pose * 66, .y = 6 + kind * 29,
                .w = widths[kind], .h = heights[kind], .kind = 4 + kind, .pose = pose};
            theme_detail_draw(&c, &d);
        }
        draw_text(&c, 140, 12 + kind * 29, 1, 0xb6d3ce, names[kind]);
    }
    draw_food_pellet(&c, 25, 155);
    bubble_t b = {.x = 45, .y = 155, .column = true};
    draw_water_bubble(&c, &b);
    b.x = 60; b.column = false; draw_water_bubble(&c, &b);
    tank_t t = help_tank;
    t.drag_has_prev = true; t.drag_dist = 100; t.drag_px = 85; t.drag_py = 155;
    t.tool = TOOL_SPONGE; t.wipe_sounded = true;
    draw_care_stroke(&t, full_frame, TANK_W);
    t.drag_px = 105; t.tool = TOOL_SCISSORS; t.slash_cut = true;
    draw_care_stroke(&t, full_frame, TANK_W);
    draw_text(&c, 140, 152, 1, 0xb6d3ce, "CARE DETAILS");
    char path[1024];
    int length = snprintf(path, sizeof path, "%s/aquarium-details-%d.raw", directory, TANK_W);
    assert(length > 0 && length < (int)sizeof path);
    FILE *file = fopen(path, "wb"); assert(file);
    assert(fwrite(full_frame, sizeof *full_frame, TANK_W * TANK_H, file) == (size_t)TANK_W * TANK_H);
    assert(!fclose(file));
}

static void check_care_scene_cache(void) {
    size_t bytes = (size_t)TANK_W * TANK_H * sizeof(uint16_t);
    uint16_t *scene = calloc(1, bytes), *baseline = malloc(bytes);
    uint32_t *dirty = calloc(RENDER_DIRTY_WORDS, sizeof *dirty);
    assert(scene && baseline && dirty);
    for (int night = 0; night < 2; night++) {
        tank_t t = help_tank;
        t.night = night != 0; t.n_fish = 0; t.clock_requested = false;
        t.aquariums.clock_theme = CLOCK_OFF; t.tool = TOOL_SPONGE;
        t.drag_has_prev = false;
        render_tile(0, 0); render_region(0, 0);
        render_set_scene_cache(scene); render_set_dirty_mask(dirty);
        render_tank(&t, full_frame, TANK_W);
        memcpy(baseline, full_frame, bytes);
        for (int frame = 0; frame < 10; frame++) {
            unsigned epoch; const uint16_t *background = render_scene_buf(&epoch);
            for (int y = 0; y < TANK_H; y++) for (int x = 0; x < TANK_W; x++)
                if (dirty[y * DIRTY_WORDS_PER_ROW + (x >> 5)] & (1u << (x & 31)))
                    full_frame[y * TANK_W + x] = background[y * TANK_W + x];
            render_fb_primed(full_frame, epoch);
            t.tool = frame < 5 ? TOOL_SPONGE : TOOL_SCISSORS;
            t.drag_has_prev = frame > 0 && frame < 9; t.drag_dist = 100;
            t.drag_px = 40 + frame * 10; t.drag_py = 70 + frame * 3;
            t.wipe_sounded = frame >= 2; t.slash_cut = frame >= 6;
            render_tank(&t, full_frame, TANK_W);
            memcpy(repeated, baseline, bytes);
            render_set_dirty_mask(NULL);
            draw_care_stroke(&t, repeated, TANK_W);
            render_set_dirty_mask(dirty);
            assert(!memcmp(full_frame, repeated, bytes));
        }
    }
    render_set_scene_cache(NULL); render_set_dirty_mask(NULL);
    free(scene); free(baseline); free(dirty);
}

static void check_game_help(void) {
    for (int game = 0; game < 18; game++) for (int paused = 0; paused < 2; paused++) {
        memset(&g_game, 0, sizeof g_game);
        g_game.page = g_game.kind = game_kinds[game];
        g_game.help = !paused;
        g_game.paused = paused != 0;
        render_tile(0, 0);
        render_games(&help_tank, full_frame, TANK_W);
        g_game.score = 999;
        g_game.hp = 5;
        g_game.wave = 5;
        g_game.moves = 22;
        g_game.left = 30;
        g_game.travel = 17;
        memset(g_game.board, 2, sizeof g_game.board);
        render_games(&help_tank, repeated, TANK_W);
        assert(!memcmp(full_frame, repeated, (size_t)TANK_W * TANK_H * sizeof *full_frame));
        /* Outside the unchanged panel, only the header/footer may be visible. */
        for (int row = 46 * TANK_H / PAGE_H; row < 339 * TANK_H / PAGE_H; row++) {
            for (int col = 0; col < TANK_W; col++) {
                int px = col * PAGE_W / TANK_W, py = row * PAGE_H / TANK_H;
                if (px < 22 || px > 425 || py < 92 || py > 330) {
                    if (full_frame[row * TANK_W + col] != rgb565(MSP_INK, 1))
                        fprintf(stderr, "Help leak game %d paused %d at %d,%d (page %d,%d)\n",
                                game, paused, col, row, px, py);
                    assert(full_frame[row * TANK_W + col] == rgb565(MSP_INK, 1));
                }
            }
        }
        for (int row = 0; row < TANK_H; row += 17) {
            int height = TANK_H - row < 17 ? TANK_H - row : 17;
            render_tile(row, height);
            render_games(&help_tank, tile_frame, TANK_W);
            assert(!memcmp(tile_frame, full_frame + row * TANK_W,
                           (size_t)TANK_W * height * sizeof *full_frame));
        }
        render_tile(0, 0);
    }
}

static void check_puzzle_games(void) {
    for (int relaxed = 0; relaxed < 2; relaxed++) {
        g_game_relaxed = relaxed != 0;
        for (int trial = 0; trial < 24; trial++) {
            game_start(GAME_LIGHTHOUSE);
            uint32_t rows[16];
            for (int i = 0; i < 16; i++) {
                rows[i] = (uint32_t)g_game.board[i] << 16;
                for (int j = 0; j < 16; j++)
                    if (abs(i % 4 - j % 4) + abs(i / 4 - j / 4) <= 1) rows[i] |= 1u << j;
            }
            int pivots[16], rank = 0;
            for (int col = 0; col < 16; col++) {
                int pivot = rank;
                while (pivot < 16 && !(rows[pivot] & (1u << col))) pivot++;
                if (pivot == 16) continue;
                uint32_t swap = rows[rank]; rows[rank] = rows[pivot]; rows[pivot] = swap;
                for (int i = 0; i < 16; i++) if (i != rank && rows[i] & (1u << col)) rows[i] ^= rows[rank];
                pivots[rank++] = col;
            }
            for (int i = rank; i < 16; i++) assert(!(rows[i] & (1u << 16)));
            for (int i = 0; i < rank; i++) if (rows[i] & (1u << 16)) {
                int cell = pivots[i];
                game_practice_tap(137 + cell % 4 * 56, 121 + cell / 4 * 48);
            }
            assert(g_game.page == GAME_RESULT && g_game.score == 16 && g_game.reward >= 6 && g_game.reward <= 10);
            game_start(GAME_SLIDE);
            assert(!game_slide_solved());
            int inversions = 0;
            for (int i = 0; i < 9; i++) for (int j = i + 1; j < 9; j++)
                if (g_game.board[i] && g_game.board[j] && g_game.board[i] > g_game.board[j]) inversions++;
            assert(!(inversions & 1)); /* Legal shuffles preserve solvability. */
        }
        game_start(GAME_SLIDE);
        for (int i = 0; i < 9; i++) g_game.board[i] = (uint8_t)((i + 1) % 9);
        g_game.board[7] = 0; g_game.board[8] = 8;
        game_practice_tap(157, 129); /* Nonadjacent tiles cannot teleport. */
        assert(!g_game.moves);
        game_practice_tap(285, 257);
        assert(g_game.page == GAME_RESULT && g_game.moves == 1 && g_game.reward == 10);

        game_start(GAME_SONAR);
        assert(g_game.remaining == (relaxed ? 20 : 15));
        int probes = g_game.remaining;
        for (int i = 0; i < 25; i++) if (g_game.board[i] == 1) {
            game_practice_tap(133 + i % 5 * 44, 127 + i / 5 * 36);
            assert(g_game.remaining == --probes);
            game_practice_tap(133 + i % 5 * 44, 127 + i / 5 * 36);
            assert(g_game.remaining == probes);
        }
        assert(g_game.page == GAME_RESULT && g_game.score == 25 && g_game.reward == 8);
        game_start(GAME_SONAR);
        for (int i = 0; i < 25 && g_game.page != GAME_RESULT; i++) if (!g_game.board[i])
            game_practice_tap(133 + i % 5 * 44, 127 + i / 5 * 36);
        assert(g_game.page == GAME_RESULT && g_game.remaining == 0 && !g_game.score);

        game_start(GAME_MERGE);
        memset(g_game.board, 0, sizeof g_game.board);
        for (int i = 0; i < 4; i++) g_game.board[i] = 1;
        game_merge_move(0);
        assert(g_game.board[0] == 2 && g_game.board[1] == 2 && g_game.score == 8);
        game_start(GAME_MERGE);
        memset(g_game.board, 0, sizeof g_game.board);
        g_game.board[0] = 1; g_game.board[1] = 2;
        game_merge_move(0);
        assert(g_game.board[0] == 1 && g_game.board[1] == 2 && !g_game.board[2] && !g_game.moves);
        for (int i = 0; i < 16; i++) g_game.board[i] = (uint8_t)(1 + (i % 4 + i / 4) % 2);
        game_merge_move(0);
        assert(g_game.page == GAME_RESULT && !g_game.reward);
        game_start(GAME_MERGE);
        memset(g_game.board, 0, sizeof g_game.board);
        g_game.board[0] = g_game.board[1] = 10;
        game_merge_move(0);
        assert(g_game.page == GAME_RESULT && g_game.board[0] == 11 && g_game.score == 2048);
        for (int direction = 0; direction < 4; direction++) {
            game_start(GAME_MERGE);
            memset(g_game.board, 0, sizeof g_game.board);
            g_game.board[game_merge_cell(direction, 1, 2)] = 1;
            g_game.board[game_merge_cell(direction, 1, 3)] = 1;
            game_merge_move(direction);
            assert(g_game.board[game_merge_cell(direction, 1, 0)] == 2 && g_game.score == 4);
        }

        game_start(GAME_STACK);
        game_practice_tap(224, 312);
        assert(g_game.remaining == 8 && g_game.rounds == 1);
        while (g_game.page != GAME_RESULT) {
            g_game.px = g_game.board[g_game.rounds * 2];
            game_practice_tap(224, 312);
        }
        assert(g_game.rounds == 12 && g_game.score == 128 && g_game.reward == 9);
        game_start(GAME_STACK);
        g_game.remaining = 1; g_game.board[1] = 5;
        game_practice_tap(224, 312);
        assert(g_game.page == GAME_RESULT && !g_game.score);

        game_start(GAME_ECHO);
        g_game.help = false;
        for (int length = 1; length <= 12; length++) {
            game_practice_tap(138, 149);
            assert(g_game.launched); /* Taps during playback are ignored. */
            for (int n = 0; n < 60 && g_game.launched; n++) render_games_tick(0.25f);
            assert(!g_game.launched);
            for (int i = 0; i < length; i++) {
                int pad = g_game.cards[i];
                game_practice_tap(138 + pad % 2 * 160, 149 + pad / 2 * 90);
            }
            assert(g_game.score == length);
        }
        assert(g_game.page == GAME_RESULT && g_game.reward == 10);
        game_start(GAME_ECHO);
        g_game.launched = false;
        int wrong = (g_game.cards[0] + 1) % 4;
        game_practice_tap(138 + wrong % 2 * 160, 149 + wrong / 2 * 90);
        assert(g_game.page == GAME_RESULT && !g_game.score);
    }
    g_game_relaxed = false;
}

static void check_puzzle_rewards(void) {
    for (int game = 12; game < 18; game++) {
        assert(!companion_game_reward(game, 0, 0));
        assert(!companion_game_reward(game, -1, 10));
        assert(!companion_game_reward(game, 10, -1));
        game_start(game_kinds[game]);
        game_finish();
        assert(!g_game.reward);
    }
    assert(!companion_game_reward(12, 15, 1));
    assert(!companion_game_reward(12, 16, 0));
    assert(companion_game_reward(12, 16, 30) == 6);
    assert(companion_game_reward(12, 16, 16) == 8);
    assert(companion_game_reward(12, 16, 8) == 10);
    assert(!companion_game_reward(13, 7, 1));
    assert(companion_game_reward(13, 8, 100) == 6);
    assert(companion_game_reward(13, 8, 40) == 8);
    assert(companion_game_reward(13, 8, 20) == 10);
    assert(!companion_game_reward(14, 5, 1));
    assert(companion_game_reward(14, 10, 2) == 2);
    assert(!companion_game_reward(15, 127, 10));
    assert(companion_game_reward(15, 128, 10) == 1);
    assert(!companion_game_reward(16, 29, 10));
    assert(companion_game_reward(16, 30, 10) == 2);
    assert(!companion_game_reward(17, 2, 10));
    assert(companion_game_reward(17, 3, 10) == 1);
    for (int game = 14; game < 18; game++)
        assert(companion_game_reward(game, INT_MAX, INT_MAX) == 10);
}

static void check_creative_games(void) {
    profile_state_t profile = help_tank.profile;
    help_tank.profile = (profile_state_t){.marker = 0x50524631u, .version = 1,
        .mode = GAME_MODE_CREATIVE, .chosen = 1, .reserved = 2};
    help_tank.creative = (creative_state_t){0x43525631u, 1, 1, 0};
    assert(progression_creative(&help_tank));
    tank_t before = help_tank;
    for (int game = 0; game < 18; game++) {
        int score = game == 12 ? 16 : game == 13 ? 8 : 40;
        int reward = game >= 12 ? companion_game_reward(game, score, 12) : 10;
        assert(!companion_game_claim(&help_tank, game, score, 12, reward, false));
        assert(!memcmp(&help_tank, &before, sizeof before));
        game_start(game_kinds[game]);
        g_game.help = false;
        render_games_tick(0.25f);
        render_games_tap(&help_tank, 224.0f * TANK_W / PAGE_W, 180.0f * TANK_H / PAGE_H);
        render_games(&help_tank, full_frame, TANK_W);
        assert(!memcmp(&help_tank, &before, sizeof before));
        g_game.score = 40; g_game.moves = 12;
        game_finish();
        render_games(&help_tank, full_frame, TANK_W);
        render_games_tap(&help_tank, 224.0f * TANK_W / PAGE_W, 226.0f * TANK_H / PAGE_H);
        assert(!memcmp(&help_tank, &before, sizeof before));
        assert(!g_game.claimed && !g_game.save_failed && !g_game.discard);
        render_games_tap(&help_tank, 380.0f * TANK_W / PAGE_W, 20.0f * TANK_H / PAGE_H);
        assert(g_game.page == GAME_MENU);
    }
    memset(&help_tank.creative, 0, sizeof help_tank.creative);
    help_tank.profile = profile;
}

static void check_practice_render(void) {
    for (int game = 12; game < 18; game++) {
        game_start(game_kinds[game]);
        g_game.help = false;
        render_games_tick(0.25f);
        unsigned char before[sizeof g_game];
        memcpy(before, &g_game, sizeof before);
        render_tile(0, 0);
        render_games(&help_tank, full_frame, TANK_W);
        render_games(&help_tank, repeated, TANK_W);
        assert(!memcmp(full_frame, repeated, (size_t)TANK_W * TANK_H * sizeof *full_frame));
        for (int row = 0; row < TANK_H; row += 17) {
            int height = TANK_H - row < 17 ? TANK_H - row : 17;
            render_tile(row, height);
            render_games(&help_tank, tile_frame, TANK_W);
            assert(!memcmp(tile_frame, full_frame + row * TANK_W, (size_t)TANK_W * height * sizeof *full_frame));
        }
        assert(!memcmp(before, &g_game, sizeof before));
        render_tile(0, 0);
    }
}

const icon_t *icons_resolve(const icon_t *icon) {
    static icon_t view;
    size_t count = (size_t)icon->w * icon->h;
    assert(icon->offset <= icon_bank_bytes);
    assert(count * 3 <= icon_bank_bytes - icon->offset);
    view = *icon;
    view.rgb = (const uint16_t *)((const uint8_t *)icon_bank + icon->offset);
    view.a = (const uint8_t *)view.rgb + count * 2;
    return &view;
}

static void draw_wrapper(uint16_t *fb, int x, int y, int kind, int item, bool lit, int size) {
    if (kind) render_decor_thumbnail(fb, TANK_W, x, y, size, item, lit);
    else render_visitor_portrait(fb, TANK_W, x, y, item, lit);
}

static void check_wrapper(int x, int y, int kind, int item, bool lit, int size) {
    const uint16_t background = 0x1843, guard = 0xc59a;
    int count = TANK_W * TANK_H;
    for (int p = 0; p < count; p++) full_frame[p] = repeated[p] = background;
    render_tile(0, 0);
    draw_wrapper(full_frame, x, y, kind, item, lit, size);
    draw_wrapper(repeated, x, y, kind, item, lit, size);
    assert(!memcmp(full_frame, repeated, (size_t)count * sizeof *full_frame));
    int width = kind ? size : 40, height = kind ? size : 24;
    int left = x * TANK_W / PAGE_W, top = y * TANK_H / PAGE_H;
    int right = (x + width - 1) * TANK_W / PAGE_W;
    int bottom = (y + height - 1) * TANK_H / PAGE_H;
    for (int row = 0; row < TANK_H; row++) for (int col = 0; col < TANK_W; col++)
        if (col < left || col > right || row < top || row > bottom) {
            if (full_frame[row * TANK_W + col] != background)
                fprintf(stderr, "Thumbnail overflow: kind %d item %d size %d at %d,%d, pixel %d,%d\n",
                        kind, item, size, x, y, col, row);
            assert(full_frame[row * TANK_W + col] == background);
        }
    for (int tile_height = 1; tile_height <= 17; tile_height += 16) {
        for (int row = 0; row < TANK_H; row += tile_height) {
            int height_now = TANK_H - row < tile_height ? TANK_H - row : tile_height;
            int length = TANK_W * height_now;
            for (int p = 0; p < length + 2; p++) tile_frame[p] = guard;
            for (int p = 1; p <= length; p++) tile_frame[p] = background;
            render_tile(row, height_now);
            draw_wrapper(tile_frame + 1, x, y, kind, item, lit, size);
            assert(tile_frame[0] == guard && tile_frame[length + 1] == guard);
            assert(!memcmp(tile_frame + 1, full_frame + row * TANK_W,
                           (size_t)length * sizeof *full_frame));
        }
    }
    render_tile(0, 0);
}

static void check_box(int x, int y, int width, int height) {
    unsigned drawn = 0;
    for (int row = 0; row < 368; row++) for (int col = 0; col < 448; col++) {
        if (!pixels[row * 448 + col]) continue;
        assert(col >= x && col < x + width);
        assert(row >= y && row < y + height);
        drawn++;
    }
    assert(drawn > 0);
}

static void draw_page_asset(uint16_t *fb, int x, int y, int kind, int item) {
    ctx_t c = ctx_page(fb, TANK_W);
    if (kind == 0) page_stamp(&c, x, y, item, MSP_TEAL);
    else if (kind == 1) game_thumb(&c, x, y, item);
    else page_medal(&c, x, y, (unsigned)item);
}

static void check_page_asset(int kind, int item, int x, int y) {
    const uint16_t background = 0x1843, guard = 0xc59a;
    int count = TANK_W * TANK_H;
    int width = kind == 1 ? 40 : kind == 0 ? 24 : 20;
    int height = kind == 1 ? 32 : 24;
    for (int p = 0; p < count; p++) full_frame[p] = repeated[p] = background;
    render_tile(0, 0);
    draw_page_asset(full_frame, x, y, kind, item);
    draw_page_asset(repeated, x, y, kind, item);
    assert(!memcmp(full_frame, repeated, (size_t)count * sizeof *full_frame));
    int left = x * TANK_W / PAGE_W, top = y * TANK_H / PAGE_H;
    int right = (x + width - 1) * TANK_W / PAGE_W;
    int bottom = (y + height - 1) * TANK_H / PAGE_H;
    uint16_t colors[64]; int color_count = 0;
    for (int row = 0; row < TANK_H; row++) for (int col = 0; col < TANK_W; col++) {
        uint16_t pixel = full_frame[row * TANK_W + col];
        if (col < left || col > right || row < top || row > bottom) assert(pixel == background);
        else if (pixel != background) {
            int i = 0;
            while (i < color_count && colors[i] != pixel) i++;
            if (i == color_count) {
                assert(color_count < 64);
                colors[color_count++] = pixel;
            }
        }
    }
    if (x == 100 && y == 100) assert(color_count >= 4);
    for (int tile_height = 1; tile_height <= 17; tile_height += 16) {
        for (int row = 0; row < TANK_H; row += tile_height) {
            int height_now = TANK_H - row < tile_height ? TANK_H - row : tile_height;
            int length = TANK_W * height_now;
            tile_frame[0] = tile_frame[length + 1] = guard;
            for (int p = 1; p <= length; p++) tile_frame[p] = background;
            render_tile(row, height_now);
            draw_page_asset(tile_frame + 1, x, y, kind, item);
            assert(tile_frame[0] == guard && tile_frame[length + 1] == guard);
            assert(!memcmp(tile_frame + 1, full_frame + row * TANK_W,
                           (size_t)length * sizeof *full_frame));
        }
    }
    render_tile(0, 0);
}

static void check_packed_urchin(void) {
    tank_t t; tank_init(&t, 42);
    t.sd_unlocks |= SD_ITEM_URCHIN;
    t.urchin_x = 100;
    t.urchin_frond = 0;
    ctx_t c = {pixels, 448, 1.0f, 0, 0, 448, 368, false};
    render_work_caches(true);
    for (int chewing = 0; chewing < 2; chewing++) {
        t.urchin_chew = chewing;
        float speed = chewing ? 2.6f : .8f;
        for (int pose = 0; pose < 16; pose++) {
            t.clock = pose * (TAU / 16) / speed;
            draw_urchin(&c, &t);
            assert(g_urchin_art);
            ur_pose_t p; urchin_pose(&p, t.clock, chewing != 0);
            const uint8_t *art = g_urchin_art + pose * URCHIN_POSE_BYTES;
            for (int i = 0; i < 37 * 24; i++)
                assert(((art[i / 2] >> ((i & 1) * 4)) & 15) ==
                       urchin_tone(i % 37 - 18 + .5f, i / 37 - 16 + .5f, &p));
        }
    }
    assert(16 * URCHIN_POSE_BYTES == 7104);
    render_work_caches(false);
    assert(!g_urchin_art);
    memset(pixels, 0, sizeof pixels);
    puts("PASS: all 32 packed urchin poses preserve every tone including odd-row nibble boundaries; 7104-byte cache and release");
}
int main(int argc, char **argv) {
    assert(argc >= 2 && argc <= 4);
    const char *gallery = argc > 2 && strcmp(argv[2], "-") ? argv[2] : NULL;
    const char *content_gallery = argc > 3 ? argv[3] : NULL;
    FILE *icons = fopen(argv[1], "rb");
    assert(icons);
    icon_bank_bytes = fread(icon_bank, 1, sizeof icon_bank, icons);
    assert(!ferror(icons) && feof(icons));
    assert(!fclose(icons));
    check_packed_urchin();
    ctx_t c = {pixels, 448, 1.0f, 0, 0, 448, 368, false};
    check_puzzle_games();
    check_puzzle_rewards();
    uint32_t previews[18];
    for (int game = 0; game < 18; game++) {
        memset(pixels, 0, sizeof pixels);
        game_thumb(&c, 100, 100, game);
        check_box(100, 100, 40, 32);
        uint32_t hash = 2166136261u;
        for (int row = 100; row < 132; row++) for (int col = 100; col < 140; col++)
            hash = (hash ^ pixels[row * 448 + col]) * 16777619u;
        for (int previous = 0; previous < game; previous++) assert(hash != previews[previous]);
        previews[game] = hash;
        assert(draw_text_wrap(&c, 100, 100, 136, 0xffffff, game_names[game]) <= 2);
        assert(24 + text_w(game_titles[game], 1) < 164);
    }
    uint32_t stamps[STAMP_CLEAN + 1];
    for (int stamp = STAMP_FISH; stamp <= STAMP_CLEAN; stamp++) {
        memset(pixels, 0, sizeof pixels);
        page_stamp(&c, 100, 100, stamp, MSP_TEAL);
        check_box(100, 100, 24, 24);
        uint32_t hash = 2166136261u;
        for (int row = 100; row < 124; row++) for (int col = 100; col < 124; col++)
            hash = (hash ^ pixels[row * 448 + col]) * 16777619u;
        for (int previous = 0; previous < stamp; previous++) assert(hash != stamps[previous]);
        stamps[stamp] = hash;
        memset(pixels, 0, sizeof pixels);
        page_stamp(&c, 100, 100, stamp, MSP_DIM);
        const uint16_t dim = rgb565(MSP_DIM, 1);
        for (int row = 100; row < 124; row++) for (int col = 100; col < 124; col++) {
            uint16_t pixel = pixels[row * 448 + col];
            assert((pixel >> 11) <= (dim >> 11));
            assert(((pixel >> 5) & 63) <= ((dim >> 5) & 63));
            assert((pixel & 31) <= (dim & 31));
        }
    }
    for (int theme = 0; theme < JOURNAL_THEME_COUNT; theme++) {
        for (int unlocked = 0; unlocked < 2; unlocked++) {
            memset(pixels, 0, sizeof pixels);
            page_theme_thumb(&c, 100, 100, theme, unlocked != 0);
            check_box(100, 100, 40, 26);
        }
    }
    for (unsigned medal = 0; medal < 5; medal++) {
        memset(pixels, 0, sizeof pixels);
        page_medal(&c, 100, 100, medal);
        check_box(100, 100, 20, 24);
    }
    for (int goal = 0; goal < JOURNAL_GOAL_N; goal++) {
        const journal_goal_t *g = &JOURNAL_GOALS[goal];
        assert(text_w(g->name, 2) <= 338);
        assert(text_w(g->detail, 1) <= 360);
        char reward[64];
        snprintf(reward, sizeof reward, "%u/%u +%uXP +%uC", g->target, g->target, g->xp, g->coins);
        assert(64 + text_w(reward, 1) < 348); /* CLAIM column stays separate. */
    }
    check_tank_materials();
    for (int resolution = 0; resolution < 2; resolution++) {
        tank_resolution(resolution != 0);
        check_fish_material();
        check_font_coverage();
        check_font_clock_cache();
        check_clock_replay_pixels();
        check_ornament_spans();
        check_companion_navigation();
        check_content_page_tiles();
        check_inventory_ui();
        assert(TANK_W == (resolution ? 320 : 240));
        check_menu_layout(gallery);
        check_aquarium_effects();
        check_care_scene_cache();
        if (gallery) {
            capture_fish_materials(gallery);
            capture_aquarium_details(gallery);
            capture_typography(gallery);
        }
        if (content_gallery) capture_new_content(content_gallery);
        render_life_open();
        for (int category = 0; category < 5; category++) for (int page = 0; page < 2; page++) {
            g_home_category = category; g_home_page = page;
            render_life(&help_tank, full_frame, TANK_W);
            memcpy(repeated, full_frame, (size_t)TANK_W * TANK_H * sizeof *full_frame);
            ctx_t header = ctx_page(repeated, TANK_W);
            button(&header, 326, 8, 112, 36, 0x1c2f36, MSP_TEAL, "BACK", 2);
            assert(!memcmp(full_frame, repeated, (size_t)TANK_W * TANK_H * sizeof *full_frame));
        }
        for (int kind = 0; kind < 3; kind++) {
            int items = kind == 0 ? STAMP_CLEAN + 1 : kind == 1 ? 18 : 4;
            for (int item = 0; item < items; item++) {
                check_page_asset(kind, item, 100, 100);
                check_page_asset(kind, item, -10, -8);
                check_page_asset(kind, item, PAGE_W - 15, PAGE_H - 9);
            }
        }
        check_game_help();
        check_creative_games();
        check_practice_render();
        for (int seen = 0; seen < 2; seen++) {
            for (int species = -1; species <= VISITOR_SPECIES; species++) {
                check_wrapper(100, 100, 0, species, seen != 0, 0);
                check_wrapper(-10, -8, 0, species, seen != 0, 0);
                check_wrapper(PAGE_W - 15, PAGE_H - 9, 0, species, seen != 0, 0);
            }
            for (int item = 0; item < SD_ITEM_COUNT; item++) {
                check_wrapper(100, 100, 1, item, seen != 0, 24);
                check_wrapper(-10, -8, 1, item, seen != 0, 24);
                check_wrapper(PAGE_W - 15, PAGE_H - 9, 1, item, seen != 0, 24);
                check_wrapper(100, 100, 1, item, seen != 0, 1);
                check_wrapper(100, 100, 1, item, seen != 0, 64);
            }
        }
    }
    assert(menu_errors == 0);
    printf("Page art: distinct games; stamps, themes, medals and %d menu screens fit at both scales.\n", menu_screens);
    puts("Wrappers: static, clipped and full/tile equivalent at both resolutions.");
    puts("Help/pause: all games mask gameplay and match tiled output at both resolutions.");
    puts("Polished art: distinct stamps, four-plus tones, clipped bounds and guarded 1/17-row tile parity at both resolutions.");
    puts("Tank materials: 45 dimensional ornaments, RGB565 contrast, night dimming, original bounds and arch opening.");
    puts("Fish materials: five RGB565 tones with unchanged polygon coverage and guarded one-row tile equality.");
    puts("Aquarium details: five visitors and food/bubbles stay bounded with guarded row parity; care feedback requires a successful stroke.");
    puts("Typography: exact coverage/ink conservation for every UI glyph at native and fractional sizes; guarded row/region parity, identical ASCII/UI metrics at scales 1-6, and no new Jumbo clock cache fallbacks.");
    puts("Navigation: all companion sections share the same header BACK; gear colors has no duplicate button and creature colors retains NEXT TYPE at both resolutions.");
    return 0;
}
