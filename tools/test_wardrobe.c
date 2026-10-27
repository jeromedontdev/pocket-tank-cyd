/* Keep the real persistence ports and legacy wardrobe regression coverage. */
#define main backup_tests_main
#include "test_tank_backup.c"
#undef main

static uint16_t wardrobe_frame[320 * 240], wardrobe_reference[320 * 240];
static bool unavailable, no_fit, color_warning;

void menu_test_panel(int w, int h) { (void)w; (void)h; }
void menu_test_text(int x, int y, int scale, const char *label) {
    (void)x; (void)y; (void)scale;
    if (strstr(label, "NO FIT FOR THIS TYPE")) no_fit = true;
    if (strstr(label, "Accessory does not fit")) color_warning = true;
}
void menu_test_button(int x, int y, int w, int h, const char *label, int scale) {
    assert(x >= 0 && y >= 0 && x + w <= PAGE_W && y + h <= PAGE_H);
    assert(render_text_w(label, scale) <= w - 8);
    if (!strcmp(label, "UNAVAILABLE")) unavailable = true;
}
static bool expected_fit(int variety, int wear) {
    return variety != FISH_JELLYFISH || wear != WEAR_SCARF;
}
static void wardrobe_tap(tank_t *t, int x, int y) {
    companion_tap(t, render_frame_x((float)x), render_frame_y((float)y));
}
static void choose_wearable(tank_t *t, int wear) {
    companion_open_section(COMPANION_SECTION_WARDROBE);
    for (int w = WEAR_TOP_HAT; w != wear; w = 1 + w % (WEAR_COUNT - 1))
        wardrobe_tap(t, 400, 230);
}
static void fresh_wardrobe(tank_t *t, int mode) {
    bool compact = TANK_W == 320;
    saved_size = 0;
    tank_init(t, 93);
    progression_boot(t);
    assert(!progression_mode_select(t, mode));
    progression_setup_done(t);
    tank_reframe(t, compact);
    t->companion.display_compact = compact;
    t->sd_balance = 100000;
    t->journal.xp = 100000;
}
static void check_matrix(void) {
    assert(!companion_wearable_fits(-1, WEAR_NONE));
    assert(!companion_wearable_fits(FISH_VARIETY_COUNT, WEAR_SCARF));
    assert(!companion_wearable_fits(FISH_CLASSIC, -1));
    assert(!companion_wearable_fits(FISH_CLASSIC, WEAR_COUNT));
    for (int mode = 0; mode < 2; mode++)
        for (int variety = 0; variety < FISH_VARIETY_COUNT; variety++)
            for (int stage = STAGE_FRY; stage <= STAGE_ELDER; stage++) {
                tank_t t;
                fresh_wardrobe(&t, mode ? GAME_MODE_CREATIVE : GAME_MODE_NORMAL);
                t.fish[0].variety = (uint8_t)variety;
                t.fish[0].stage = (stage_t)stage;
                for (int wear = 1; wear < WEAR_COUNT; wear++) {
                    bool fits = expected_fit(variety, wear);
                    assert(companion_wearable_fits(variety, wear) == fits);
                    int32_t balance = t.sd_balance;
                    companion_state_t before = t.companion;
                    int old_writes = writes;
                    assert(companion_wearable(&t, 0, wear) == fits);
                    if (!fits) {
                        assert(!memcmp(&before, &t.companion, sizeof before));
                        assert(t.sd_balance == balance && writes == old_writes);
                    } else {
                        assert(t.fish[0].wearable == wear);
                        assert(companion_wearables(&t.companion.active[0]) & (1u << (wear - 1)));
                        assert(t.sd_balance == balance - (mode ? 0 : WEAR_PRICES[wear]));
                    }
                    choose_wearable(&t, wear);
                    unavailable = no_fit = false;
                    companion_render(&t, wardrobe_frame, TANK_W);
                    assert(unavailable == !fits && no_fit == !fits);
                    before = t.companion; balance = t.sd_balance; old_writes = writes;
                    wardrobe_tap(&t, 100, 300);
                    assert(t.sd_balance == balance);
                    if (!fits) {
                        assert(!memcmp(&before, &t.companion, sizeof before));
                        assert(writes == old_writes);
                    } else assert(t.fish[0].wearable == wear);
                    wardrobe_tap(&t, 30, 180);
                    color_warning = false;
                    companion_render(&t, wardrobe_frame, TANK_W);
                    assert(color_warning == !fits);
                    assert(companion_wearable(&t, 0, WEAR_NONE));
                }
            }
    puts("PASS: core/UI purchase, equip and color previews: 10 varieties x 4 stages x 12 items, Normal and Creative");
}
static void check_legacy(void) {
    tank_t t, loaded;
    fresh_wardrobe(&t, GAME_MODE_NORMAL);
    t.fish[0].variety = FISH_JELLYFISH;
    companion_bond_t *b = &t.companion.active[0];
    b->wearables = 255; b->tricks = 120; b->wear_colors = 0x9a;
    for (int wear = 1; wear < WEAR_COUNT; wear++) {
        if (expected_fit(FISH_JELLYFISH, wear)) continue;
        b->wearable = t.fish[0].wearable = (uint8_t)wear;
        assert(companion_valid(&t.companion) && progression_save(&t));
        tank_init(&loaded, 94); boot_enlarged(&loaded);
        assert(loaded.fish[0].variety == FISH_JELLYFISH);
        assert(loaded.companion.active[0].wearable == wear);
        assert(companion_wearables(&loaded.companion.active[0]) == 4095);
        assert(loaded.companion.active[0].wear_colors == 0x9a);
        companion_state_t before = loaded.companion;
        int32_t balance = loaded.sd_balance; int old_writes = writes;
        assert(!companion_wearable(&loaded, 0, wear));
        choose_wearable(&loaded, wear); wardrobe_tap(&loaded, 100, 300);
        assert(!memcmp(&before, &loaded.companion, sizeof before));
        assert(balance == loaded.sd_balance && writes == old_writes);
        char *json = progression_backup_json(&loaded); assert(json);
        assert(progression_restore_json(json, strlen(json), true, error, sizeof error)); free(json);
        tank_init(&loaded, 95); boot_enlarged(&loaded);
        assert(loaded.companion.active[0].wearable == wear);
        assert(companion_wearables(&loaded.companion.active[0]) == 4095);
        choose_wearable(&loaded, wear); wardrobe_tap(&loaded, 300, 300);
        assert(loaded.companion.active[0].wearable == WEAR_NONE);
        assert(companion_wearables(&loaded.companion.active[0]) == 4095);
    }
    puts("PASS: incompatible legacy equipment loads/backups intact, cannot re-equip, REMOVE retains all ownership/colors");
}
static void check_legacy_travel(void) {
    tank_t t, loaded;
    fresh_wardrobe(&t, GAME_MODE_NORMAL);
    int fish = add_bank_candidate(&t, 4);
    t.fish[fish].variety = FISH_JELLYFISH;
    t.fish[fish].wearable = WEAR_SCARF;
    companion_bond_t bond = t.companion.active[fish];
    bond.wearables = 255; bond.tricks = 120; bond.wearable = WEAR_SCARF; bond.wear_colors = 0x9a;
    t.companion.active[fish] = bond;
    assert(progression_save(&t));
    assert(!progression_bank_move(&t, -1, fish));
    assert(!memcmp(&bond, &t.companion.bank[0], sizeof bond));
    tank_init(&loaded, 96); boot_enlarged(&loaded);
    assert(!memcmp(&bond, &loaded.companion.bank[0], sizeof bond));
    assert(!progression_bank_move(&loaded, 0, -1));
    fish = loaded.n_fish - 1;
    assert(loaded.fish[fish].variety == FISH_JELLYFISH);
    assert(!memcmp(&bond, &loaded.companion.active[fish], sizeof bond));
    assert(companion_adventure_start(&loaded, 0, fish));
    assert(!memcmp(&bond, &loaded.companion.traveler_bond, sizeof bond));
    assert(loaded.companion.portrait_variety == FISH_JELLYFISH);
    assert(loaded.companion.portrait_wearable == WEAR_SCARF);
    tank_init(&t, 97); boot_enlarged(&t);
    assert(!memcmp(&bond, &t.companion.traveler_bond, sizeof bond));
    test_unix += 7200; companion_tick(&t, .25f);
    assert(!t.companion.away);
    assert(!memcmp(&bond, &t.companion.active[t.n_fish - 1], sizeof bond));
    assert(t.fish[t.n_fish - 1].variety == FISH_JELLYFISH);
    puts("PASS: incompatible legacy ownership/equipment/colors survive Bank, reload, traveler and return unchanged");
}
static void check_legacy_swimming(void) {
    for (int compact = 0; compact < 2; compact++) {
        tank_resolution(compact != 0);
        tank_t t; fresh_wardrobe(&t, GAME_MODE_NORMAL);
        t.n_fish = 1; t.aquariums.clock_theme = CLOCK_OFF;
        fish_t *f = &t.fish[0];
        f->variety = FISH_JELLYFISH;
        f->wear_colors = 0x9a; f->x = TANK_W / 2; f->y = TANK_H / 2;
        for (int stage = 0; stage <= STAGE_ELDER; stage++)
            for (int wear = 1; wear < WEAR_COUNT; wear++) {
                if (expected_fit(FISH_JELLYFISH, wear)) continue;
                f->stage = (stage_t)stage; f->size = .55f + stage * .23f;
                for (int pose = 0; pose < 5; pose++) {
                    f->heading = (pose - 2) * 1.1f; f->speed = 40;
                    f->yaw = pose < 2 ? -1 : pose == 2 ? .18f : 1;
                    f->yaw_tail = f->yaw * .8f;
                    t.clock = pose * .4f;
                    f->wearable = (uint8_t)wear;
                    render_tank(&t, wardrobe_frame, TANK_W);
                    f->wearable = WEAR_NONE;
                    render_tank(&t, wardrobe_reference, TANK_W);
                    assert(!memcmp(wardrobe_frame, wardrobe_reference, (size_t)TANK_W * TANK_H * 2));
                }
            }
    }
    puts("PASS: legacy jellyfish gear invisible in actual swimming renderer at both sizes, four stages and five pitch/yaw/pulse poses");
}
static void write_frame(const char *gallery, const char *name) {
    char path[1024];
#ifdef _WIN32
    snprintf(path, sizeof path, "%s\\%s.raw", gallery, name);
#else
    snprintf(path, sizeof path, "%s/%s.raw", gallery, name);
#endif
    FILE *file = fopen(path, "wb"); assert(file);
    assert(fwrite(wardrobe_frame, 2, (size_t)TANK_W * TANK_H, file) == (size_t)TANK_W * TANK_H);
    assert(!fclose(file));
}
static void check_render(const char *gallery) {
    tank_resolution(true);
    for (int variety = 0; variety < FISH_VARIETY_COUNT; variety++)
        for (int stage = STAGE_FRY; stage <= STAGE_ELDER; stage++)
            for (int wear = 1; wear < WEAR_COUNT; wear++) {
                fish_t f = {0};
                f.variety = (uint8_t)variety; f.stage = (stage_t)stage;
                f.color = 0xe7a650; f.fin = 0xc36c82; f.accent = 0x87d9d3;
                f.wearable = (uint8_t)wear;
                f.size = 1; f.speed = 40; tank_fish_face(&f);
                float x[8], y[8]; uint32_t color;
                assert((tank_wearable_polygon(&f, 0, x, y, &color) > 0) == expected_fit(variety, wear));
                assert(!tank_wearable_polygon(&f, -1, x, y, &color));
                assert(!tank_wearable_polygon(&f, 4, x, y, &color));
                memset(wardrobe_frame, 0, sizeof wardrobe_frame);
                static const float sizes[] = {1.1f, 1.56f, 2.08f, 2.46f};
                render_fish_portrait(wardrobe_frame, TANK_W, 235, 190, sizes[stage], &f, 0.4f);
                f.wearable = WEAR_NONE;
                memset(wardrobe_reference, 0, sizeof wardrobe_reference);
                render_fish_portrait(wardrobe_reference, TANK_W, 235, 190, sizes[stage], &f, 0.4f);
                bool same = !memcmp(wardrobe_frame, wardrobe_reference, sizeof wardrobe_frame);
                assert(same == !expected_fit(variety, wear));
                if (!expected_fit(variety, wear)) {
                    f.wearable = (uint8_t)wear;
                    float top = tank_fish_top_clearance(&f), bottom = tank_fish_clearance(&f);
                    f.wearable = WEAR_NONE;
                    assert(top == tank_fish_top_clearance(&f) && bottom == tank_fish_clearance(&f));
                }
                if (gallery) {
                    char name[80]; snprintf(name, sizeof name, "fit-%d-%d-%d", stage, variety, wear);
                    write_frame(gallery, name);
                }
            }
    puts("PASS: all 480 actual-renderer portraits; legacy incompatibilities pixel-identical to bare fish and add no extents");
    if (gallery) {
        tank_t t; fresh_wardrobe(&t, GAME_MODE_NORMAL);
        t.fish[0].variety = FISH_JELLYFISH;
        t.fish[0].wearable = t.companion.active[0].wearable = WEAR_SCARF;
        t.companion.active[0].tricks = 120; t.companion.active[0].wearables = 255;
        choose_wearable(&t, WEAR_SCARF);
        companion_render(&t, wardrobe_frame, TANK_W);
        write_frame(gallery, "ui-jellyfish-legacy-scarf-unavailable");
        wardrobe_tap(&t, 30, 180);
        companion_render(&t, wardrobe_frame, TANK_W);
        write_frame(gallery, "ui-jellyfish-legacy-scarf-colors");
    }
}
int main(int argc, char **argv) {
    const char *gallery = argc > 1 ? argv[1] : NULL;
    for (int compact = 0; compact < 2; compact++) {
        tank_resolution(compact != 0);
        check_matrix();
    }
    check_legacy();
    check_legacy_travel();
    check_legacy_swimming();
    check_render(gallery);
    test_wearables();
    test_cosmetic_colors();
    test_fish_sand_clearance();
    test_fish_tile_culling();
    puts("PASS: wardrobe fit and existing ownership, colors, sand and tiled-renderer regressions");
    return 0;
}
