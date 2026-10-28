#define main backup_regression_main
#include "test_tank_backup.c"
#undef main

static void place_tap(tank_t *t, float x, float y) {
    setup_touch(t, x, y, true);
    setup_touch(t, x, y, false);
}

static void place_drag(tank_t *t, float from, float to, float y) {
    setup_touch(t, from, y, true);
    setup_touch(t, to, y, true);
    setup_touch(t, to, y, false);
}

static void test_overlapping_copy_selection_and_rollback(void) {
    tank_t t;
    const int item = 10;
    const int other_item = 11;
    const float start = 120.0f;
    const float tap_y = 165.0f;
    saved_size = 0;
    tank_init(&t, 841);
    boot_enlarged(&t);
    t.sd_unlocks |= SD_ITEMS[item].bit;
    tank_decor_set(&t, item, start, DECOR_Z_BACK);
    t.inventory.extra[0] = (inventory_extra_t){ .item = item + 1, .z = DECOR_Z_BACK, .x = start };
    t.inventory.extra[1] = (inventory_extra_t){ .item = other_item + 1, .z = DECOR_Z_BACK, .x = start };

    setup_begin_place(&t, item);
    place_tap(&t, start, tap_y);
    assert(tank_decor_x(&t, item) == start && t.inventory.extra[0].x == start);
    size_t pixels = (size_t)TANK_W * TANK_H;
    uint16_t *original_preview = calloc(pixels, sizeof *original_preview);
    uint16_t *copy_preview = calloc(pixels, sizeof *copy_preview);
    assert(original_preview && copy_preview);
    render_setup(&t, original_preview, TANK_W, 1.0f);
    place_tap(&t, start, tap_y);
    assert(setup_item() == other_item);
    assert(tank_decor_x(&t, item) == start && t.inventory.extra[0].x == start);
    render_setup(&t, copy_preview, TANK_W, 1.0f);
    assert(memcmp(original_preview, copy_preview, pixels * sizeof *original_preview));
    free(original_preview);
    free(copy_preview);
    place_tap(&t, start, tap_y);
    assert(setup_item() == item);
    place_tap(&t, start, tap_y);
    assert(tank_decor_x(&t, item) == start && t.inventory.extra[0].x == start);
    place_drag(&t, start, 150.0f, tap_y);
    assert(tank_decor_x(&t, item) == start);
    assert(t.inventory.extra[0].x == 150.0f);
    assert(t.inventory.extra[1].x == start);
    setup_cancel(&t);
    assert(tank_decor_x(&t, item) == start && t.inventory.extra[0].x == start && t.inventory.extra[1].x == start);

    setup_begin_extra_place(&t, 0);
    place_drag(&t, start, 155.0f, tap_y);
    test_decor_scenery_reject_write(true);
    setup_activate(&t, SETUP_HIT_NEXT);
    test_decor_scenery_reject_write(false);
    assert(setup_active() && setup_is_place());
    setup_cancel(&t);
    assert(tank_decor_x(&t, item) == start && t.inventory.extra[0].x == start);
    saved_size = 0;
    puts("PASS: overlapping placement-copy selection, drag isolation, and save-failure cancel rollback");
}

static void test_all_placeable_cancel_roundtrip(void) {
    for (int compact = 0; compact < 2; compact++) {
        tank_resolution(compact != 0);
        tank_t t;
        saved_size = 0;
        tank_init(&t, 54);
        boot_enlarged(&t);
        tank_new_population(&t);
        t.sd_unlocks = UINT32_MAX;
        t.polish.scenery_unlocks = SD_SCENERY_MASK;
        tank_set_name(&t, 0, "renamed");
        t.fish[0].color ^= 0xffffff;
        for (int item = 0; item < SD_ITEM_COUNT; item++) {
            if (!tank_decor_movable(item)) continue;
            char *original = progression_backup_json(&t);
            assert(original);
            setup_begin_place(&t, item);
            tank_decor_set(&t, item, TANK_W * .7f, DECOR_Z_BACK);
            tank_decor_shift(&t, item, 1);
            if (item == 3) tank_coral_set_rgb(&t, 0xff0000);
            if (item == 4) tank_cluster_set_scheme(&t, 2);
            char *preview = progression_backup_json(&t);
            assert(preview && !strcmp(preview, original));
            free(preview);
            assert(progression_save(&t));
            setup_cancel(&t);
            char *cancelled = progression_backup_json(&t);
            if (!cancelled || strcmp(cancelled, original)) {
                fprintf(stderr, "Placement cancel changed backup JSON for item %d, compact %d\n", item, compact);
                if (cancelled) {
                    size_t at = 0;
                    while (original[at] && cancelled[at] && original[at] == cancelled[at]) at++;
                    fprintf(stderr, "First difference at %zu: original %.80s / cancelled %.80s\n",
                            at, original + at, cancelled + at);
                }
            }
            assert(cancelled && !strcmp(cancelled, original));
            free(cancelled);
            free(original);
        }
    }
    saved_size = 0;
    tank_resolution(false);
}

static void test_remove_to_storage(void) {
    for (int normal = 0; normal < 2; normal++)
        for (int item = 0; item < SD_ITEM_COUNT; item++) {
            if (!tank_decor_placeable(item)) continue;
            tank_t t;
            saved_size = 0;
            tank_init(&t, 96);
            boot_enlarged(&t);
            tank_resolution(normal != 0);
            t.companion.display_compact = normal != 0;
            t.sd_unlocks |= SD_ITEMS[item].bit;
            if (SD_IS_NEW_DECOR(item)) t.content.owned |= 1u << (item - SD_DECOR_FIRST);
            t.profile.owned_items |= UINT64_C(1) << item;
            t.profile.paid_items |= UINT64_C(1) << item;
            tank_decor_set(&t, item, TANK_W * .5f, DECOR_Z_BACK);
            int wallet = t.sd_balance;
            setup_begin_place(&t, item);
            setup_activate(&t, SETUP_HIT_REMOVE);
            assert(setup_active() && progression_item_owned(&t, item));
            setup_activate(&t, SETUP_HIT_BACK);
            assert(!setup_active() && progression_item_owned(&t, item));
            setup_begin_place(&t, item);
            setup_activate(&t, SETUP_HIT_REMOVE);
            test_decor_scenery_reject_write(true);
            setup_activate(&t, SETUP_HIT_REMOVE);
            test_decor_scenery_reject_write(false);
            assert(setup_is_place() && progression_item_owned(&t, item));
            assert(!progression_inventory_stored(&t, item) && t.sd_balance == wallet);
            setup_activate(&t, SETUP_HIT_REMOVE);
            setup_activate(&t, SETUP_HIT_REMOVE);
            assert(!setup_active() && !progression_item_owned(&t, item));
            assert(progression_inventory_stored(&t, item) == 1 && t.sd_balance == wallet);
            tank_t loaded;
            tank_init(&loaded, 98);
            boot_enlarged(&loaded);
            assert(!progression_item_owned(&loaded, item));
            assert(progression_inventory_stored(&loaded, item) == 1 && loaded.sd_balance == wallet);
            assert(progression_inventory_place(&loaded, item));
            assert(progression_item_owned(&loaded, item) && !progression_inventory_stored(&loaded, item));
        }

    tank_t t;
    saved_size = 0;
    tank_init(&t, 97);
    boot_enlarged(&t);
    t.inventory.version = 1;
    t.inventory.extra[0] = (inventory_extra_t){.item = 11, .paid = 1, .x = 120, .z = DECOR_Z_FRONT};
    t.profile.owned_items |= UINT64_C(1) << 10;
    assert(!progression_item_owned(&t, 10));
    assert(setup_begin_place_at(&t, 120, TANK_BOT - 20));
    assert(setup_item() == 10);
    setup_activate(&t, SETUP_HIT_REMOVE);
    setup_activate(&t, SETUP_HIT_REMOVE);
    assert(!setup_active() && !t.inventory.extra[0].item);
    assert(progression_inventory_stored(&t, 10) == 1);
    assert(progression_inventory_place(&t, 10));
    assert(progression_item_owned(&t, 10));

    t.inventory.stored[5] = DECOR_INVENTORY_CAP;
    setup_begin_place(&t, 10);
    setup_activate(&t, SETUP_HIT_REMOVE);
    setup_activate(&t, SETUP_HIT_REMOVE);
    assert(setup_is_place() && progression_item_owned(&t, 10));
    assert(!strcmp(progression_shop_notice(), "DECORATION STORAGE FULL"));
    setup_cancel(&t);
    assert(!setup_begin_place_at(&t, 1, 1));
    saved_size = 0;
    puts("PASS: confirmed movement removal stores every decoration without refund; cancel/write failure/full storage preserve ownership; extra-only holding works");
}

static void test_layers_and_mirror(void) {
    tank_t t, loaded;
    saved_size = 0;
    tank_init(&t, 905);
    boot_enlarged(&t);
    progression_setup_done(&t);
    const int items[] = {9, 10, 11, 12};
    for (int i = 0; i < 4; i++) t.sd_unlocks |= SD_ITEMS[items[i]].bit;
    tank_decor_set(&t, 10, TANK_W * 0.15f, DECOR_Z_BACK);
    tank_decor_set(&t, 12, TANK_W * 0.15f + 10, DECOR_Z_BACK);   /* overlaps the shell */
    tank_decor_set(&t, 11, TANK_W * 0.5f, DECOR_Z_BACK);         /* far away: never a layer neighbor */
    tank_decor_set(&t, 9, TANK_W * 0.85f, DECOR_Z_BACK);
    int up = tank_decor_neighbor(&t, 10, 1), down = tank_decor_neighbor(&t, 10, -1);
    assert(up == 12 || down == 12);
    assert(up != 11 && down != 11 && up != 9 && down != 9);
    for (int d = -1; d <= 1; d += 2) { int n = tank_decor_neighbor(&t, 11, d); assert(n != 9 && n != 10 && n != 12); }
    int dir = up == 12 ? 1 : -1;
    tank_decor_shift(&t, 10, dir);
    assert(tank_decor_neighbor(&t, 10, -dir) == 12);
    assert((tank_decor_rank(&t, 10) > tank_decor_rank(&t, 12)) == (dir > 0));

    int rank = tank_decor_rank(&t, 10);
    assert(tank_decor_mirrorable(10) && !tank_decor_mirrorable(2) && !tank_decor_mirrorable(3));
    tank_decor_set_mirrored(&t, 10, true);
    assert(tank_decor_mirrored(&t, 10) && tank_decor_rank(&t, 10) == rank);
    assert(tank_decor_neighbor(&t, 10, -dir) == 12);
    assert(progression_save(&t));
    tank_init(&loaded, 1);
    boot_enlarged(&loaded);
    assert(tank_decor_mirrored(&loaded, 10) && !tank_decor_mirrored(&loaded, 12));
    assert(tank_decor_rank(&loaded, 10) == rank);

    char *json = progression_backup_json(&t);
    assert(json);
    tank_decor_set_mirrored(&t, 10, false);
    assert(progression_save(&t));
    char error[96];
    assert(progression_restore_json(json, strlen(json), true, error, sizeof error));
    free(json);
    tank_init(&loaded, 2);
    boot_enlarged(&loaded);
    assert(tank_decor_mirrored(&loaded, 10));

    setup_begin_place(&loaded, 12);
    setup_activate(&loaded, SETUP_HIT_MIRROR);
    assert(tank_decor_mirrored(&loaded, 12));
    setup_cancel(&loaded);
    assert(!tank_decor_mirrored(&loaded, 12) && tank_decor_mirrored(&loaded, 10));
    setup_begin_place(&loaded, 12);
    setup_activate(&loaded, SETUP_HIT_MIRROR);
    setup_activate(&loaded, SETUP_HIT_NEXT);
    assert(!setup_active() && tank_decor_mirrored(&loaded, 12));
    saved_size = 0;
    puts("PASS: layer arrows only reach overlapping pieces; mirror survives save, JSON backup and cancel");
}

static bool place_hit_at(int id, int px, int py) {
    return setup_hit(render_frame_x(px), render_frame_y(py)) == id;
}

/* every piece the shop sells for the tank - sand ornaments, plants, sea glass, the floats - buys,
 * opens its placement page, drags, mirrors, saves with DONE and comes back from a reboot where it was put */
static void test_every_item_buys_and_moves(void) {
    for (int compact = 0; compact < 2; compact++) {
        tank_resolution(compact != 0);
        tank_t t, loaded;
        saved_size = 0;
        tank_init(&t, 77);
        boot_enlarged(&t);
        progression_setup_done(&t);
        t.polish.scenery_unlocks = 0;
        int moved = 0;
        for (int item = 0; item < SD_ITEM_COUNT; item++) {
            if (!tank_decor_movable(item)) continue;
            if (!progression_item_owned(&t, item)) {
                t.sd_balance = 1000000;
                if (!progression_buy(&t, item)) fprintf(stderr, "Item %d (%s) could not be bought\n", item, SD_ITEMS[item].name);
                assert(progression_item_owned(&t, item));
            }
            assert(tank_decor_owned(&t, item));
            setup_begin_place(&t, item);
            if (!setup_is_place() || setup_item() != item) fprintf(stderr, "Item %d (%s) has no placement page\n", item, SD_ITEMS[item].name);
            assert(setup_active() && setup_is_place() && setup_item() == item);
            bool surface = tank_decor_surface(item);
            assert(place_hit_at(SETUP_HIT_REMOVE, SETUP_TOP_BACK_X + 20, SETUP_TOP_BTN_Y + 20) == tank_decor_placeable(item));
            assert(place_hit_at(SETUP_HIT_MIRROR, PAGE_W / 2, SETUP_MIRROR_Y + 12) == tank_decor_mirrorable(item));
            assert(place_hit_at(SETUP_HIT_BCK, SETUP_ORD_X0 + 18, SETUP_TOP_BTN_Y + 20) == !surface);
            int depth = setup_hit(render_frame_x(PAGE_W / 2), render_frame_y(SETUP_DEPTH_Y + SETUP_DEPTH_H / 2));
            assert((depth >= SETUP_HIT_Z0 && depth < SETUP_HIT_Z0 + DECOR_Z_N) == (tank_decor_z_count(item) >= 2));
            float from = tank_decor_x(&t, item);
            float to = from < TANK_W / 2 ? TANK_W * 0.72f : TANK_W * 0.28f;
            place_drag(&t, from, to, TANK_H - 6);
            float x = tank_decor_x(&t, item);
            if (fabsf(x - from) < 12) fprintf(stderr, "Item %d (%s) did not move: %.1f -> %.1f\n", item, SD_ITEMS[item].name, from, x);
            assert(fabsf(x - from) >= 12);
            bool mirrored = tank_decor_mirrored(&t, item);
            if (tank_decor_mirrorable(item)) { setup_activate(&t, SETUP_HIT_MIRROR); mirrored = !mirrored; }
            assert(tank_decor_mirrored(&t, item) == mirrored);
            int z = tank_decor_z(&t, item);
            setup_activate(&t, SETUP_HIT_NEXT);
            assert(!setup_active());
            tank_init(&loaded, 3);
            boot_enlarged(&loaded);
            if (fabsf(tank_decor_x(&loaded, item) - x) > 1 || tank_decor_mirrored(&loaded, item) != mirrored || tank_decor_z(&loaded, item) != z)
                fprintf(stderr, "Item %d (%s) did not reload: x %.1f/%.1f mirror %d/%d z %d/%d\n", item, SD_ITEMS[item].name,
                        x, tank_decor_x(&loaded, item), mirrored, tank_decor_mirrored(&loaded, item), z, tank_decor_z(&loaded, item));
            assert(fabsf(tank_decor_x(&loaded, item) - x) <= 1);
            assert(tank_decor_mirrored(&loaded, item) == mirrored && tank_decor_z(&loaded, item) == z);
            moved++;
        }
        assert(moved >= 40);
        char *json = progression_backup_json(&t); assert(json);
        char error[96];
        tank_t before = t;
        assert(progression_restore_json(json, strlen(json), true, error, sizeof error));
        free(json);
        tank_init(&loaded, 4);
        boot_enlarged(&loaded);
        for (int item = 0; item < SD_ITEM_COUNT; item++)
            if (tank_decor_movable(item))
                assert(fabsf(tank_decor_x(&loaded, item) - tank_decor_x(&before, item)) <= 1 &&
                       tank_decor_mirrored(&loaded, item) == tank_decor_mirrored(&before, item));
        printf("PASS: all %d movable shop pieces (sand, plants, sea glass, floats) buy, drag, mirror, DONE and reload (%s)\n",
               moved, compact ? "240x320" : "320x480");
    }
    saved_size = 0;
    tank_resolution(false);
}

int main(void) {    test_overlapping_copy_selection_and_rollback();
    test_all_placeable_cancel_roundtrip();
    test_remove_to_storage();
    test_layers_and_mirror();
    test_every_item_buys_and_moves();
    puts("PASS: all placeable decor previews and saves roll back to the original layout on cancel");
    return 0;
}
