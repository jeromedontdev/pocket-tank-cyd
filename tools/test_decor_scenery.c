/* Scenery (items 36..41): ownership lives in polish.scenery_unlocks; since 2.4 the pieces
 * are movable (decor slots 36.., x/depth/mirror in the schema-20 fixed tail) but never stored. */
#include "progression.h"
#include "render.h"
#include "companion.h"
#include "cJSON.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void test_decor_scenery_reject_write(bool reject);
size_t test_decor_scenery_reload(tank_t *t, uint32_t seed);

static int test_decor_scenery_changed(const uint16_t *a, const uint16_t *b) {
    int changed = 0;
    for (int px = 0; px < TANK_W * TANK_H; px++) changed += a[px] != b[px];
    return changed;
}

static void test_decor_scenery_catalog(void) {
    assert(SD_ITEM_COUNT == 63 && SD_SHOP_MUSSEL == 42 && SD_SCENERY_FIRST == 36 && SD_SCENERY_N == 6 && SD_SCENERY_MASK == 0x3f);
    assert(DECOR_SLOTS == 50 && DECOR_FIXED_SLOT0 == 36 && DECOR_LEGACY_SLOTS == 16 && WEAR_COUNT == 13);
    int prices = 0;
    for (int item = SD_SCENERY_FIRST; item < SD_SCENERY_END; item++) {
        assert(SD_IS_SCENERY(item) && SD_IS_FIXED_SCENERY(item));
        assert(SD_ITEMS[item].bit == 0 && SD_ITEMS[item].price > 0);
        assert(!tank_decor_placeable(item) && tank_decor_movable(item) && tank_ornament_index(item) == -1);
        assert(tank_decor_slot(item) == DECOR_FIXED_SLOT0 + tank_fixed_index(item) && tank_decor_slot_item(tank_decor_slot(item)) == item);
        assert(!strcmp(SD_ITEMS[item].words2, "SCENERY YOU CAN MOVE"));
        prices += SD_ITEMS[item].price;
    }
    assert(prices == 530);
    assert(!SD_IS_SCENERY(35) && !SD_IS_SCENERY(SD_ITEM_COUNT) && !SD_IS_FIXED_SCENERY(30));
}

static void test_decor_scenery_buy_sell(void) {
    tank_t t, before;
    tank_init(&t, 4242);
    assert(progression_reset(&t, 4242) && !progression_mode_select(&t, GAME_MODE_NORMAL));
    progression_setup_done(&t);
    t.polish.scenery_unlocks = 0;
    uint32_t sd = t.sd_unlocks;
    uint8_t stat = t.aquariums.stat_upgrades;
    for (int item = SD_SCENERY_FIRST; item < SD_SCENERY_END; item++) {
        int price = SD_ITEMS[item].price;
        uint8_t flag = (uint8_t)(1u << (item - SD_SCENERY_FIRST));
        t.sd_balance = price - 1;
        assert(!progression_buy(&t, item) && !progression_item_owned(&t, item));
        t.sd_balance = price;
        before = t;
        test_decor_scenery_reject_write(true);
        assert(!progression_buy(&t, item) && !memcmp(&before, &t, sizeof t));
        test_decor_scenery_reject_write(false);
        assert(progression_shop_notice() && !strcmp(progression_shop_notice(), "SAVE FAILED - NOT BOUGHT"));
        assert(progression_buy(&t, item) && !progression_shop_notice());
        assert(t.sd_balance == 0 && (t.polish.scenery_unlocks & flag) && progression_item_owned(&t, item));
        assert(!progression_buy(&t, item));
        before = t;
        test_decor_scenery_reject_write(true);
        assert(!progression_sell(&t, item) && !memcmp(&before, &t, sizeof t));
        test_decor_scenery_reject_write(false);
        assert(progression_shop_notice() && !strcmp(progression_shop_notice(), "SAVE FAILED - NOT SOLD"));
        progression_shop_notice_clear(); assert(!progression_shop_notice());
        assert(progression_sell(&t, item) && !(t.polish.scenery_unlocks & flag));
        assert(t.sd_balance == progression_sell_value(item) && progression_sell_value(item) == price / 5);
        assert(!progression_sell(&t, item));
        t.sd_balance = price;
        assert(progression_buy(&t, item));
    }
    /* Only the new byte changes: no decor slot, sd_unlocks bit or stat upgrade is consumed. */
    assert(t.polish.scenery_unlocks == SD_SCENERY_MASK && t.sd_unlocks == sd && t.aquariums.stat_upgrades == stat);
    for (int item = SD_SCENERY_FIRST; item < SD_SCENERY_END; item++) assert(tank_decor_owned(&t, item));
    t.sd_balance = INT32_MAX;
    assert(!progression_sell_value_for(&t, SD_SCENERY_FIRST));
    assert(progression_sell(&t, SD_SCENERY_FIRST) && t.sd_balance == INT32_MAX);
    assert(progression_buy(&t, SD_SCENERY_FIRST) && t.sd_balance == INT32_MAX);
    char *json = progression_backup_json(&t); assert(json);
    cJSON *root = cJSON_Parse(json); free(json); assert(root);
    cJSON *polish = cJSON_GetObjectItemCaseSensitive(root, "polish");
    assert(polish && cJSON_GetObjectItemCaseSensitive(polish, "scenery_unlocks")->valuedouble == SD_SCENERY_MASK);
    cJSON_Delete(root);
}

static void test_decor_scenery_reload_paths(void) {
    tank_t t, loaded;
    /* The binary save keeps a partial mask through a real reboot. */
    test_decor_scenery_reload(&t, 31);
    assert(progression_reset(&t, 31) && !progression_mode_select(&t, GAME_MODE_NORMAL));
    progression_setup_done(&t);
    t.polish.scenery_unlocks = 0;
    t.sd_balance = SD_ITEMS[SD_SCENERY_GLASS].price + SD_ITEMS[SD_SCENERY_TEAPOT].price;
    assert(progression_buy(&t, SD_SCENERY_GLASS) && progression_buy(&t, SD_SCENERY_TEAPOT) && t.sd_balance == 0);
    assert(test_decor_scenery_reload(&loaded, 5) == AQUARIUM_SAVE_BYTES);
    assert(loaded.polish.scenery_unlocks == 0x21 && loaded.sd_balance == 0);
    uint32_t teapot_since = t.polish.decor_since[32 + SD_SCENERY_TEAPOT - SD_SCENERY_FIRST];
    assert(teapot_since && t.polish.decor_since[32] && !t.polish.decor_since[32 + SD_SCENERY_TIKI - SD_SCENERY_FIRST]);
    assert(loaded.polish.decor_since[32 + SD_SCENERY_TEAPOT - SD_SCENERY_FIRST] == teapot_since);
    assert(progression_item_owned(&loaded, SD_SCENERY_GLASS) && progression_item_owned(&loaded, SD_SCENERY_TEAPOT));
    assert(!progression_item_owned(&loaded, SD_SCENERY_TIKI));
    /* A rejected sale after reload leaves the persisted bytes owning the piece. */
    test_decor_scenery_reject_write(true);
    assert(!progression_sell(&loaded, SD_SCENERY_GLASS));
    test_decor_scenery_reject_write(false);
    assert(test_decor_scenery_reload(&loaded, 6) && loaded.polish.scenery_unlocks == 0x21);
    assert(progression_sell(&loaded, SD_SCENERY_GLASS));
    assert(test_decor_scenery_reload(&loaded, 7) && loaded.polish.scenery_unlocks == 0x20);
    assert(loaded.sd_balance == progression_sell_value(SD_SCENERY_GLASS));
    /* JSON backup -> committed restore -> reboot round-trips the mask. */
    char *json = progression_backup_json(&loaded); assert(json);
    char error[128];
    loaded.polish.scenery_unlocks = SD_SCENERY_MASK; assert(progression_save(&loaded));
    assert(progression_restore_json(json, strlen(json), true, error, sizeof error));
    assert(test_decor_scenery_reload(&t, 8) && t.polish.scenery_unlocks == 0x20);
    assert(t.polish.decor_since[32 + SD_SCENERY_TEAPOT - SD_SCENERY_FIRST] == teapot_since);
    /* An out-of-range mask in a backup is refused, leaving the saved tank intact. */
    cJSON *root = cJSON_Parse(json); free(json); assert(root);
    cJSON *polish = cJSON_GetObjectItemCaseSensitive(root, "polish"); assert(polish);
    cJSON_ReplaceItemInObjectCaseSensitive(polish, "scenery_unlocks", cJSON_CreateNumber(64));
    char *bad = cJSON_PrintUnformatted(root); cJSON_Delete(root); assert(bad);
    assert(!progression_restore_json(bad, strlen(bad), true, error, sizeof error)); free(bad);
    assert(test_decor_scenery_reload(&t, 9) && t.polish.scenery_unlocks == 0x20);
}

static void test_decor_scenery_journal(void) {
    tank_t t;
    tank_init(&t, 77);
    t.sd_unlocks = 0; t.polish.scenery_unlocks = 0;
    int goal = -1;
    for (int g = 0; g < JOURNAL_GOAL_N; g++)
        if (!strcmp(JOURNAL_GOALS[g].name, "A PERSONAL REEF")) goal = g;
    assert(goal >= 0);
    unsigned base = journal_progress(&t, goal);
    t.polish.scenery_unlocks = 0x5;
    assert(journal_progress(&t, goal) == base + 2);
}

static void test_decor_scenery_render(void) {
    uint16_t *base = calloc(TANK_W * TANK_H, sizeof *base);
    uint16_t *art = calloc(TANK_W * TANK_H, sizeof *art);
    uint16_t *scene = calloc(TANK_W * TANK_H, sizeof *scene);
    uint32_t *dirty = calloc(RENDER_DIRTY_WORDS, sizeof *dirty);
    void *scratch = calloc(1, render_decor_scratch_size());
    assert(base && art && scene && dirty && scratch);
    render_set_decor_scratch(scratch); render_set_scene_cache(scene); render_set_dirty_mask(dirty);
    tank_t t;
    tank_init(&t, 17);
    t.polish.scenery_unlocks = 0;
    t.content.owned = 0;
    render_tank(&t, base, TANK_W);
    for (int item = SD_SCENERY_FIRST; item < SD_SCENERY_END; item++) {
        t.polish.scenery_unlocks = (uint8_t)(1u << (item - SD_SCENERY_FIRST));
        render_tank(&t, art, TANK_W);
        assert(test_decor_scenery_changed(base, art) > 40); /* The purchase rebakes the cached backdrop. */
        t.polish.scenery_unlocks = 0;
        render_tank(&t, art, TANK_W);
        assert(!memcmp(base, art, TANK_W * TANK_H * sizeof *base)); /* Selling leaves no cached art. */
        memset(art, 0, TANK_W * TANK_H * sizeof *art);
        render_ornament_preview(art, TANK_W, item, 220, 180);
        int painted = 0;
        for (int px = 0; px < TANK_W * TANK_H; px++) painted += art[px] != 0;
        assert(painted > 40);
    }
    for (int item = SD_DECOR_FIRST; item < SD_ITEM_COUNT; item++) {
        int index = item - SD_DECOR_FIRST;
        assert(tank_ornament_w(item) == 40 && tank_ornament_h(item) == 40);
        t.content.owned = UINT32_C(1) << index;
        tank_decor_set(&t, item, 34 + index * (TANK_W - 68) / (DECOR_NEW_COUNT - 1),
                       index & 1 ? DECOR_Z_FRONT : DECOR_Z_BACK);
        render_tank(&t, art, TANK_W);
        assert(test_decor_scenery_changed(base, art) > 40);
        t.content.owned = 0;
        render_tank(&t, art, TANK_W);
        assert(!memcmp(base, art, TANK_W * TANK_H * sizeof *base));
    }
    /* All six together still bake into the one cached backdrop. */
    t.polish.scenery_unlocks = SD_SCENERY_MASK;
    t.content.owned = (1u << DECOR_NEW_COUNT) - 1;
    for (int item = SD_DECOR_FIRST; item < SD_ITEM_COUNT; item++)
        tank_decor_set(&t, item, 44 + (item % 6) * (TANK_W - 88) / 5,
                       item & 1 ? DECOR_Z_FRONT : DECOR_Z_BACK);
    render_tank(&t, art, TANK_W);
    assert(test_decor_scenery_changed(base, art) > 6 * 40);
    render_set_decor_scratch(NULL); render_set_scene_cache(NULL); render_set_dirty_mask(NULL);
    free(base); free(art); free(scene); free(dirty); free(scratch);
}

static void test_decor_scenery_notice(void) {
    uint16_t *plain = calloc(TANK_W * TANK_H, sizeof *plain);
    uint16_t *noted = calloc(TANK_W * TANK_H, sizeof *noted);
    assert(plain && noted);
    tank_t t;
    tank_init(&t, 9);
    t.polish.scenery_unlocks = 0; t.sd_balance = SD_ITEMS[SD_SCENERY_FIRST].price;
    render_shop_leave(); progression_shop_notice_clear();
    render_shop(&t, plain, TANK_W);
    test_decor_scenery_reject_write(true);
    assert(!progression_buy(&t, SD_SCENERY_FIRST));
    test_decor_scenery_reject_write(false);
    render_shop(&t, noted, TANK_W);
    assert(test_decor_scenery_changed(plain, noted) > 100); /* The keeper sees the failure. */
    progression_shop_notice_clear();
    render_shop(&t, noted, TANK_W);
    assert(!memcmp(plain, noted, TANK_W * TANK_H * sizeof *plain));
    render_shop_leave();
    free(plain); free(noted);
}

static void test_decor_scenery_variants(void) {
    size_t capacity = (size_t)TANK_MAX_W * TANK_MAX_H;
    uint16_t *full = calloc(capacity, sizeof *full), *scene = calloc(capacity, sizeof *scene);
    uint16_t *tile = calloc((size_t)TANK_MAX_W * 17 + 2, sizeof *tile);
    void *scratch = calloc(1, render_decor_scratch_size());
    assert(full && scene && tile && scratch);
    render_set_decor_scratch(scratch);
    render_device_t device = {0};
    for (int normal = 0; normal < 2; normal++) {
        tank_resolution(normal != 0);
        tank_t t;
        tank_init(&t, 27);
        t.companion.display_compact = normal != 0;
        t.n_fish = 0;
        t.sd_unlocks = SD_ITEM_PLANT | SD_ITEM_CHEST;
        for (int item = 10; item <= 12; item++) t.sd_unlocks |= UINT32_C(1) << item;
        for (int item = 16; item <= 20; item++) t.sd_unlocks |= UINT32_C(1) << item;
        for (int item = 22; item <= 27; item++) t.sd_unlocks |= UINT32_C(1) << item;
        t.polish.scenery_unlocks = SD_SCENERY_MASK;
        t.content.owned = (UINT32_C(1) << DECOR_NEW_COUNT) - 1;
        for (int item = SD_DECOR_FIRST; item < SD_ITEM_COUNT; item++)
            tank_decor_set(&t, item, 34 + (item * 37) % (TANK_W - 68),
                           item & 1 ? DECOR_Z_FRONT : DECOR_Z_BACK);
        for (int bed = 0; bed < VEG_BEDS_MAX; bed++)
            for (int i = 0; i < VEG_FRONDS_MAX; i++) t.veg_h[bed][i] = .2f + (i % 7) * .025f;
        for (int detail = SCENE_BALANCED; detail <= SCENE_ENHANCED; detail++)
            for (int night = 0; night < 2; night++) for (int theme = 0; theme < 2; theme++) {
                device.scene_detail = (uint8_t)detail;
                render_set_device(&device);
                t.night = night != 0;
                t.light_on = !night;
                t.journal.theme = theme ? JOURNAL_THEME_COUNT - 1 : 0;
                tank_t before = t;
                render_set_scene_cache(scene);
                render_tile(0, 0); render_region(0, 0);
                render_tank(&t, full, TANK_W);
                assert(!memcmp(&t, &before, sizeof t));
                for (int row = 0; row < TANK_H; row += 17) {
                    int height = TANK_H - row < 17 ? TANK_H - row : 17;
                    size_t count = (size_t)TANK_W * height;
                    tile[0] = tile[count + 1] = 0xa55a;
                    render_tile(row, height);
                    render_tank(&t, tile + 1, TANK_W);
                    assert(tile[0] == 0xa55a && tile[count + 1] == 0xa55a);
                    assert(!memcmp(full + row * TANK_W, tile + 1, count * sizeof *full));
                }
            }
    }
    render_tile(0, 0);
    render_set_scene_cache(NULL); render_set_decor_scratch(NULL);
    device.scene_detail = SCENE_BALANCED; render_set_device(&device);
    tank_resolution(false);
    free(full); free(scene); free(tile); free(scratch);
    puts("PASS: crafted ornaments and sword foliage preserve full/warm-tile equality, day/night, both sizes, two themes and all three scene details without tank mutation");
}

static void test_harbor_garden_content(void) {
    tank_t t, loaded, before;
    tank_init(&t, 343);
    assert(progression_reset(&t, 343) && !progression_mode_select(&t, GAME_MODE_NORMAL));
    progression_setup_done(&t);
    t.sd_balance = 100000;
    uint32_t legacy = t.sd_unlocks;
    uint64_t hashes[DECOR_NEW_COUNT] = {0};
    uint16_t *art = calloc((size_t)TANK_W * TANK_H, sizeof *art); assert(art);
    for (int item = SD_DECOR_FIRST; item < SD_ITEM_COUNT; item++) {
        int i = item - SD_DECOR_FIRST;
        assert(tank_decor_placeable(item) && tank_decor_slot(item) == DECOR_LEGACY_SLOTS + i);
        assert(progression_inventory_duplicate(item) && SD_ITEMS[item].bit == 0);
        before = t;
        test_decor_scenery_reject_write(true);
        assert(!progression_buy(&t, item) && !memcmp(&t, &before, sizeof t));
        test_decor_scenery_reject_write(false);
        assert(progression_buy(&t, item) && progression_item_owned(&t, item));
        tank_decor_set(&t, item, 100 + i * 2, i & 1 ? DECOR_Z_FRONT : DECOR_Z_BACK);
        assert(progression_inventory_buy(&t, item));
        assert(progression_inventory_stored(&t, item) == 1);
        assert(progression_inventory_place(&t, item));
        assert(progression_inventory_placed(&t, item) == 2);
        int copy = -1;
        for (int k = 0; k < DECOR_EXTRA_CAP; k++)
            if (t.inventory.extra[k].item == item + 1) copy = k;
        assert(copy >= 0 && progression_inventory_store(&t, item, copy));
        assert(progression_inventory_sell(&t, item) && progression_inventory_stored(&t, item) == 0);
        before = t;
        test_decor_scenery_reject_write(true);
        assert(!progression_sell(&t, item) && !memcmp(&t, &before, sizeof t));
        test_decor_scenery_reject_write(false);
        memset(art, 0, (size_t)TANK_W * TANK_H * sizeof *art);
        render_ornament_preview(art, TANK_W, item, TANK_W / 2, TANK_H / 2);
        uint64_t hash = UINT64_C(1469598103934665603);
        int ink = 0;
        for (int p = 0; p < TANK_W * TANK_H; p++) {
            ink += art[p] != 0; hash = (hash ^ art[p]) * UINT64_C(1099511628211);
        }
        assert(ink > 40);
        for (int k = 0; k < i; k++) assert(hashes[k] != hash);
        hashes[i] = hash;
    }
    free(art);
    assert(t.sd_unlocks == legacy && t.content.owned == (1u << DECOR_NEW_COUNT) - 1);
    assert(progression_save(&t));
    assert(test_decor_scenery_reload(&loaded, 344) == AQUARIUM_SAVE_BYTES);
    assert(loaded.content.owned == t.content.owned);
    for (int item = SD_DECOR_FIRST; item < SD_ITEM_COUNT; item++) {
        assert(progression_item_owned(&loaded, item));
        assert(tank_decor_x(&loaded, item) == tank_decor_x(&t, item));
        assert(tank_decor_z(&loaded, item) == tank_decor_z(&t, item));
        assert(tank_decor_rank(&loaded, item) == tank_decor_rank(&t, item));
    }
    int ordered[DECOR_SLOTS]; assert(tank_decor_order(&loaded, ordered) >= DECOR_NEW_COUNT);
    loaded.journal.xp = 100000;
    for (int wear = WEAR_SAILOR; wear < WEAR_COUNT; wear++) {
        before = loaded;
        test_decor_scenery_reject_write(true);
        assert(!companion_wearable(&loaded, 0, wear) && !memcmp(&loaded, &before, sizeof loaded));
        test_decor_scenery_reject_write(false);
        assert(companion_wearable(&loaded, 0, wear));
        assert(companion_colors(&loaded, true, 0, 0xab));
        assert(companion_wearables(&loaded.companion.active[0]) & (1u << (wear - 1)));
        assert(companion_valid(&loaded.companion));
        int balance = loaded.sd_balance;
        assert(companion_wearable(&loaded, 0, wear) && loaded.sd_balance == balance);
        for (int variety = 0; variety < FISH_VARIETY_COUNT; variety++)
            for (int part = 0; part < 4; part++) {
                    fish_t f = loaded.fish[0]; f.variety = (uint8_t)variety;
                    float x[8], y[8]; uint32_t color;
                    int n = tank_wearable_polygon(&f, part, x, y, &color);
                    if (variety == FISH_JELLYFISH && wear == WEAR_SCARF) {
                        assert(!n);
                        continue;
                    }
                    if (wear != WEAR_VIKING && wear != WEAR_SCARF && part > 1) {
                        assert(n == 0);
                        continue;
                    }
                    assert(n >= 3 && n <= 8 && color);
                    if (wear == WEAR_VIKING && part == 0) {
                        float min_y = y[0];
                        for (int p = 1; p < n; p++) if (y[p] < min_y) min_y = y[p];
                        assert(n == 7 && min_y <= -15);
                    }
                    if (wear == WEAR_VIKING && (part == 1 || part == 2)) {
                        float min_x = x[0], max_x = x[0], min_y = y[0];
                        for (int p = 1; p < n; p++) {
                            if (x[p] < min_x) min_x = x[p];
                            if (x[p] > max_x) max_x = x[p];
                            if (y[p] < min_y) min_y = y[p];
                        }
                        if (part == 1) assert(min_x <= -4 && max_x <= 1.5f);
                        else assert(max_x >= 11 && min_x > 0);
                        assert(n == 4 && min_y <= -20);
                    }
                    if (wear == WEAR_SCARF) {
                        float min_x = x[0], max_x = x[0], min_y = y[0], max_y = y[0];
                        static const float head_by_type[] = {-8.7f,-13.5f,-8.7f,-7,-12,-7.5f,-15.5f,-7,-10.2f,-8.7f};
                        static const float neck_by_type[] = {8,12,8,5,11,6,0,7,9,8};
                        float head = head_by_type[variety], neck = neck_by_type[variety];
                        for (int p = 1; p < n; p++) {
                            if (x[p] < min_x) min_x = x[p];
                            if (x[p] > max_x) max_x = x[p];
                            if (y[p] < min_y) min_y = y[p];
                            if (y[p] > max_y) max_y = y[p];
                        }
                        unsigned red = color >> 16, green = color >> 8 & 255, blue = color & 255;
                        assert(n == 4 && red >= 0xd0 && red > green + 60 && red > blue + 60);
                        /* The wrap is a solid gill band; knot and tails hang below it. */
                        if (part == 0) assert(min_y <= head + 1 && max_y >= neck && max_x - min_x >= 6 && max_x <= 2);
                        else if (part == 1) assert(min_x <= -9 && max_y >= neck + 4);
                        else if (part == 2) assert(min_x <= -6 && max_y >= neck + 5);
                        else assert(min_y <= neck - 3 && max_y >= neck + 1 && max_x - min_x >= 5);
                    }
                }
        fish_t invalid_wear = loaded.fish[0];
        invalid_wear.wearable = WEAR_VIKING;
        float invalid_x[8], invalid_y[8]; uint32_t invalid_color;
        assert(!tank_wearable_polygon(&invalid_wear, -1, invalid_x, invalid_y, &invalid_color));
    }
    assert((loaded.companion.active[0].tricks & 7) == (t.companion.active[0].tricks & 7));
    unsigned wardrobe = companion_wearables(&loaded.companion.active[0]);
    assert(tank_add_fish(&loaded, -1, -1) >= 0);
    assert(!progression_bank_move(&loaded, -1, 0));
    assert(companion_wearables(&loaded.companion.bank[0]) == wardrobe);
    assert(!progression_bank_move(&loaded, 0, 0));
    assert(loaded.fish[0].wearable == WEAR_SCARF && companion_wearables(&loaded.companion.active[0]) == wardrobe);
    assert(progression_adventure_depart(&loaded, 0, 0));
    assert(companion_wearables(&loaded.companion.traveler_bond) == wardrobe);
    loaded.companion.adventure_s = companion_adventure_total(&loaded.companion, 0);
    assert(progression_adventure_return(&loaded));
    int returning = loaded.n_fish - 1;
    assert(loaded.fish[returning].wearable == WEAR_SCARF &&
           companion_wearables(&loaded.companion.active[returning]) == wardrobe);
    char *json = progression_backup_json(&loaded); assert(json);
    char error[128];
    assert(progression_restore_json(json, strlen(json), true, error, sizeof error));
    assert(test_decor_scenery_reload(&t, 345) == AQUARIUM_SAVE_BYTES);
    assert(t.content.owned == loaded.content.owned && t.fish[returning].wearable == WEAR_SCARF);
    assert(t.companion.active[returning].wear_colors == 0xab &&
           companion_wearables(&t.companion.active[returning]) == wardrobe);
    cJSON *root = cJSON_Parse(json); free(json); assert(root);
    cJSON *content = cJSON_GetObjectItemCaseSensitive(root, "content");
    cJSON_ReplaceItemInObjectCaseSensitive(content, "owned", cJSON_CreateNumber(1u << DECOR_NEW_COUNT));
    char *bad = cJSON_PrintUnformatted(root); cJSON_Delete(root); assert(bad);
    assert(!progression_restore_json(bad, strlen(bad), true, error, sizeof error)); free(bad);
    assert(progression_reset(&t, 346) && !progression_mode_select(&t, GAME_MODE_NORMAL));
    progression_setup_done(&t); t.sd_balance = 10000;
    assert(progression_buy(&t, 62) && progression_inventory_buy(&t, 62));
    char *stale = progression_backup_json(&t); assert(stale);
    assert(!progression_aquarium_move(&t, 1));
    progression_setup_done(&t);
    assert(!progression_item_owned(&t, 62) && progression_inventory_stored(&t, 62) == 1);
    assert(progression_inventory_place(&t, 62) && progression_item_owned(&t, 62));
    assert(!progression_aquarium_move(&t, 0));
    assert(progression_item_owned(&t, 62) && !progression_inventory_stored(&t, 62));
    assert(!progression_restore_json(stale, strlen(stale), true, error, sizeof error));
    free(stale);
    assert(!progression_aquarium_move(&t, 1) && progression_item_owned(&t, 62));
    assert(progression_reset(&t, 347) && !progression_mode_select(&t, GAME_MODE_CREATIVE));
    progression_setup_done(&t);
    int balance = t.sd_balance;
    assert(progression_buy(&t, 62) && progression_inventory_buy(&t, 62));
    assert(t.sd_balance == balance && !t.content.paid[19]);
    assert(progression_inventory_sell(&t, 62) && progression_sell(&t, 62));
    assert(t.sd_balance == balance && !progression_item_owned(&t, 62));
    puts("PASS: 20 unique static decorations, purchases/sales/copy storage, rollback, placement, binary/JSON ownership and six packed/colorable proportional wearables");
}

void test_decor_scenery(void) {
    test_decor_scenery_catalog();
    test_decor_scenery_buy_sell();
    test_decor_scenery_reload_paths();
    test_decor_scenery_journal();
    test_decor_scenery_render();
    test_decor_scenery_notice();
    test_decor_scenery_variants();
    test_harbor_garden_content();
    puts("PASS: six fixed sand scenery pieces buy/sell with save-failure rollback and notice, binary/JSON reload, cached backdrop art, no decor slots");
}