/* Fixed sand scenery (items 36..41): ownership lives in polish.scenery_unlocks, the pieces
 * are never placeable (no decor slot) and bake into the cached backdrop. */
#include "progression.h"
#include "render.h"
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
    assert(SD_ITEM_COUNT == 42 && SD_SCENERY_FIRST == 36 && SD_SCENERY_N == 6 && SD_SCENERY_MASK == 0x3f);
    assert(DECOR_SLOTS == 16);
    int prices = 0;
    for (int item = SD_SCENERY_FIRST; item < SD_SCENERY_END; item++) {
        assert(SD_IS_SCENERY(item) && SD_IS_FIXED_SCENERY(item));
        assert(SD_ITEMS[item].bit == 0 && SD_ITEMS[item].price > 0);
        assert(!tank_decor_placeable(item) && tank_ornament_index(item) == -1);
        assert(!strcmp(SD_ITEMS[item].words2, "FIXED SPOT ON THE SAND"));
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
    for (int item = SD_SCENERY_FIRST; item < SD_SCENERY_END; item++) assert(!tank_decor_owned(&t, item));
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
    /* All six together still bake into the one cached backdrop. */
    t.polish.scenery_unlocks = SD_SCENERY_MASK;
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

void test_decor_scenery(void) {
    test_decor_scenery_catalog();
    test_decor_scenery_buy_sell();
    test_decor_scenery_reload_paths();
    test_decor_scenery_journal();
    test_decor_scenery_render();
    test_decor_scenery_notice();
    puts("PASS: six fixed sand scenery pieces buy/sell with save-failure rollback and notice, binary/JSON reload, cached backdrop art, no decor slots");
}