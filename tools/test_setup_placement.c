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
        tank_set_name(&t, 0, "renamed");
        t.fish[0].color ^= 0xffffff;
        for (int item = 0; item < SD_ITEM_COUNT; item++) {
            if (!tank_decor_placeable(item)) continue;
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

int main(void) {
    test_overlapping_copy_selection_and_rollback();
    test_all_placeable_cancel_roundtrip();
    puts("PASS: all placeable decor previews and saves roll back to the original layout on cancel");
    return 0;
}
