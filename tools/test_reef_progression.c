/* Reuse the profile harness's in-memory persistence ports, not its scenarios. */
#define main legacy_profiles_main
#include "test_profiles.c"
#undef main
#include <math.h>

static void test_reef_daily(tank_t *t) {
    start(t, GAME_MODE_NORMAL);
    progression_care_refresh(t);
    assert(CARE_COINS[0] + CARE_COINS[1] + CARE_COINS[2] == 125);
    assert(CARE_XP[0] + CARE_XP[1] + CARE_XP[2] == 0);
    assert(!strcmp(CARE_TASK_NAMES[2], "QUIET COMPANY"));
    t->reef_currents++;
    progression_care_refresh(t);
    assert(!(t->care.ready & 4));
    t->reef_visits++;
    progression_care_refresh(t);
    assert(!(t->care.ready & 4));
    t->night = false; t->ui_cover = false;
    t->fish[0].hunger = t->fish[0].stress = 0;
    progression_tick(t, CARE_VIEW_SECONDS);
    assert(t->care.ready & 4);
    int balance = t->sd_balance;
    uint32_t xp = t->journal.xp;
    reject_write = true;
    assert(!progression_care_claim(t, 2));
    assert(t->sd_balance == balance && t->journal.xp == xp && !(t->care.claimed & 4));
    reject_write = false;
    assert(progression_care_claim(t, 2));
    assert(t->sd_balance == balance + 65 && t->journal.xp == xp);
    assert(!progression_care_claim(t, 2));
    t->reef_flow_s = 9; t->reef_flow_cooldown = 20;
    assert(progression_save(t));
    reload(t);
    assert(t->reef_currents == 1 && t->reef_visits == 1);
    assert(!t->reef_flow_s && !t->reef_flow_cooldown && (t->care.claimed & 4));
    now += 86400;
    progression_care_refresh(t);
    assert(!t->care.ready && !t->care.claimed);
    assert(t->reef.current_base == 1 && t->reef.visit_base == 1);
    t->night = false; t->ui_cover = false;
    t->fish[0].hunger = t->fish[0].stress = 0;
    progression_tick(t, CARE_VIEW_SECONDS);
    assert(t->care.ready & 4); /* The accessible quiet-view alternative remains. */
}

static void test_paid_progression(tank_t *t) {
    start(t, GAME_MODE_NORMAL);
    assert(progression_food_tier(t) == PROGRESSION_FOOD_BASE);
    assert(progression_food_growth_multiplier(t) == 1.0f);
    progression_time_scale = 1.0f;
    /* Favorite decor (x0.9 / x1.1) and hybrid vigor (+1% each) scale every growth step. */
    float g = progression_fish_growth_factor(t, 0);
    assert(g >= 0.9f - 1e-4f && g <= 1.1f * 1.1f + 1e-4f);
    float age = progression_age_s(t, 0);
    progression_tick(t, 2);
    assert(fabsf(progression_age_s(t, 0) - age - (2.0f) * g) < 0.001f);
    age = progression_age_s(t, 0);
    progression_slept(t, 2);
    assert(fabsf(progression_age_s(t, 0) - age - (2.0f * SLEEP_GROWTH_FRAC) * g) < 0.001f);
    assert(progression_food_price(PROGRESSION_FOOD_PLUS) == 250);
    assert(progression_food_price(PROGRESSION_FOOD_PREMIUM) == 750);
    assert(!progression_food_buy(t, PROGRESSION_FOOD_PREMIUM));
    t->sd_balance = 1000;
    progression_state_t state = t->progression;
    reject_write = true;
    assert(!progression_food_buy(t, PROGRESSION_FOOD_PLUS));
    assert(t->sd_balance == 1000 && !memcmp(&t->progression, &state, sizeof state));
    reject_write = false;
    assert(progression_food_buy(t, PROGRESSION_FOOD_PLUS));
    assert(t->sd_balance == 750 && progression_food_tier(t) == PROGRESSION_FOOD_PLUS);
    assert(progression_food_growth_multiplier(t) == 1.25f);
    age = progression_age_s(t, 0);
    progression_tick(t, 2);
    assert(fabsf(progression_age_s(t, 0) - age - (2.5f) * g) < 0.001f);
    age = progression_age_s(t, 0);
    progression_slept(t, 2);
    assert(fabsf(progression_age_s(t, 0) - age - (0.625f) * g) < 0.001f);
    reject_write = true;
    assert(!progression_food_buy(t, PROGRESSION_FOOD_PREMIUM));
    assert(t->sd_balance == 750 && progression_food_tier(t) == PROGRESSION_FOOD_PLUS);
    reject_write = false;
    assert(progression_food_buy(t, PROGRESSION_FOOD_PREMIUM));
    assert(t->sd_balance == 0 && progression_food_growth_multiplier(t) == 1.5f);
    age = progression_age_s(t, 0);
    progression_tick(t, 2);
    assert(fabsf(progression_age_s(t, 0) - age - (3.0f) * g) < 0.001f);
    age = progression_age_s(t, 0);
    progression_slept(t, 2);
    assert(fabsf(progression_age_s(t, 0) - age - (0.75f) * g) < 0.001f);
    assert(!progression_food_buy(t, PROGRESSION_FOOD_PREMIUM));
    reload(t);
    assert(progression_food_tier(t) == PROGRESSION_FOOD_PREMIUM);
    assert(progression_food_growth_multiplier(t) == 1.5f && t->sd_balance == 0);

    t->sd_balance = AQUARIUM_PRICE;
    assert(!progression_aquarium_move(t, 1));
    assert(progression_food_tier(t) == PROGRESSION_FOOD_PREMIUM && t->sd_balance == 0);
    assert(progression_setup_done(t));

    t->sd_balance = 259;
    assert(!journal_theme_buy(t, 3));
    t->sd_balance = 310;
    assert(journal_theme_price(3) == 260 && !journal_theme_unlocked(t, 3));
    reject_write = true;
    assert(!journal_theme_buy(t, 3) && t->sd_balance == 310);
    reject_write = false;
    assert(journal_theme_buy(t, 3));
    assert(t->sd_balance == 50 && journal_theme_unlocked(t, 3));
    assert(!journal_theme_buy(t, 3));
    assert(journal_set_theme(t, 3));
    assert(!progression_aquarium_move(t, 0));
    assert(progression_food_tier(t) == PROGRESSION_FOOD_PREMIUM);
    assert(journal_theme_unlocked(t, 3));
    assert(journal_set_theme(t, 3));
    reload(t);
    assert(t->journal.theme == 3 && journal_theme_unlocked(t, 3));
}

static void test_reef_history(tank_t *t) {
    start(t, GAME_MODE_NORMAL);
    t->algae_colonies = 3; t->trim_px = 7.1f * PX_PER_CM;
    t->trims = 4; t->cells_cleaned = 50;
    t->sd_colonies_paid = 2; t->sd_inches_paid = 3;
    t->journal.claimed = 2;
    t->tank_ms_bits |= TMS_FIRST_TRIM | TMS_FIRST_CLEANING;
    t->care.ready = t->care.claimed = 4;
    t->reef_currents = 2; t->reef_visits = 5;
    assert(journal_progress(t, 1) == 3 && journal_progress(t, 5) == 7);
    t->reef_currents = 8; t->reef_visits = 10;
    assert(journal_progress(t, 1) == 5 && journal_progress(t, 5) == 10);
    journal_tick(t);
    assert(progression_save(t));
    reload(t);
    assert(t->algae_colonies == 3 && t->trim_px == 7.1f * PX_PER_CM);
    assert(t->trims == 4 && t->cells_cleaned == 50);
    assert(t->sd_colonies_paid == 2 && t->sd_inches_paid == 3);
    assert(t->journal.claimed == 2 && t->care.claimed == 4);
    assert((t->tank_ms_bits & (TMS_FIRST_TRIM | TMS_FIRST_CLEANING)) ==
           (TMS_FIRST_TRIM | TMS_FIRST_CLEANING));
    fry_req_t requirements[FRY_REQ_MAX]; bool staged;
    int n = progression_next_fry(t, requirements, &staged);
    bool refuge = false;
    for (int i = 0; i < n; i++) {
        assert(requirements[i].kind != FRY_REQ_GLASS);
        if (requirements[i].kind == FRY_REQ_GRASS) {
            refuge = true;
            assert(requirements[i].met && !strcmp(requirements[i].title, "REFUGE"));
        }
    }
    assert(refuge);
    for (int i = 0; i < t->n_fish; i++) {
        t->fish[i].stage = STAGE_ADULT; t->fish[i].variety = FISH_CLASSIC;
        t->fish[i].trust = 5; t->fish[i].energy = 8; t->fish[i].hunger = t->fish[i].stress = 0;
    }
    progression_force_arrival(t);
    assert(t->n_fish == 3 && t->fish[2].x > 0 && t->fish[2].x < TANK_W);
}

static void seed_retired(tank_t *t, bool paid) {
    t->sd_balance = 100; t->sd_earned = 200;
    for (int item = 30; item <= 31; item++) {
        t->sd_unlocks |= SD_ITEMS[item].bit;
        t->profile.owned_items |= UINT64_C(1) << item;
        if (paid) t->profile.paid_items |= UINT64_C(1) << item;
    }
    memset(t->algae, 255, sizeof t->algae);
    assert(progression_save(t));
    saved_size = 6424; /* Real schema-20 prefix: absent Reef Rhythm state. */
}

static void test_retired_upgrades(tank_t *t) {
    start(t, GAME_MODE_NORMAL);
    seed_retired(t, true);
    reload(t);
    assert(t->sd_balance == 168 && t->sd_earned == 200);
    assert(!t->reef_currents && !t->reef_visits);
    for (size_t i = 0; i < sizeof t->algae; i++) assert(!((uint8_t *)t->algae)[i]);
    assert(!(t->sd_unlocks & (SD_ITEM_FILTER | SD_ITEM_GARDEN)));
    assert(!(t->profile.owned_items & (UINT64_C(3) << 30)));
    reject_write = true; assert(!progression_save(t)); reject_write = false;
    reload(t); assert(t->sd_balance == 168); /* Retry, not cumulative credit. */
    assert(progression_save(t));
    reload(t); assert(t->sd_balance == 168);
    assert(progression_shop_count(t) == SD_CATALOG_COUNT - 8); /* retired + Creature Market stock */
    for (int row = 0; row < progression_shop_count(t); row++) {
        int item = progression_shop_item(t, row);
        assert(item != 8 && item != 30 && item != 31 && item != 1 && item != 5 && item != 6 && item != 7 && item != 42);
    }
    assert(!progression_buy(t, 30) && !progression_buy(t, 31));
    start(t, GAME_MODE_NORMAL); seed_retired(t, false);
    reload(t); assert(t->sd_balance == 100);
    start(t, GAME_MODE_CREATIVE); seed_retired(t, true);
    reload(t); assert(t->sd_balance == 100);
}

static void test_reef_json_and_prefixes(tank_t *t) {
    start(t, GAME_MODE_NORMAL);
    t->reef_currents = 9; t->reef_visits = 12;
    progression_care_refresh(t);
    assert(progression_save(t) && saved_size == 6476 && AQUARIUM_SAVE_BYTES == 6476);
    unsigned char complete[6476]; memcpy(complete, saved, sizeof complete);
    for (size_t partial = 6425; partial < 6464; partial++) {
        memcpy(saved, complete, sizeof complete); saved_size = partial;
        reload(t);
        assert(!progression_storage_ready() && !progression_save(t));
        assert(saved_size == partial && !memcmp(saved, complete, partial));
    }
    for (size_t partial = 6465; partial < sizeof complete; partial++) {
        if (partial == 6472) continue;
        memcpy(saved, complete, sizeof complete); saved_size = partial;
        reload(t);
        assert(!progression_storage_ready() && !progression_save(t));
        assert(saved_size == partial && !memcmp(saved, complete, partial));
    }
    memcpy(saved, complete, sizeof complete); saved_size = 6464;
    reload(t); /* Exact schema-21 prefix migrates without a payout. */
    assert(progression_storage_ready() && t->sd_balance == 0);
    t->journal.xp = journal_level_xp(10);
    t->journal.theme = 4;
    t->sd_balance = 1234;
    assert(progression_save(t));
    saved_size = 6464;
    reload(t);
    assert(progression_storage_ready() && t->sd_balance == 1234);
    assert(t->progression.food_tier == PROGRESSION_FOOD_BASE);
    assert(journal_theme_unlocked(t, 4));
    unsigned expected_varieties = 0, expected_themes = 0;
    for (int i = 0; i < FISH_VARIETY_COUNT; i++)
        if (10 >= JOURNAL_VARIETY_LEVELS[i]) expected_varieties |= 1u << i;
    for (int i = 0; i < JOURNAL_THEME_COUNT; i++)
        if (10 >= JOURNAL_THEME_LEVELS[i] || i == 4) expected_themes |= 1u << i;
    assert(t->progression.variety_unlocks == expected_varieties);
    assert(t->progression.theme_unlocks == expected_themes);
    reload(t);
    assert(t->sd_balance == 1234 && journal_theme_unlocked(t, 4));
    assert(saved_size == 6464);
    memcpy(saved, complete, sizeof complete); saved_size = sizeof complete; reload(t);
    char *json = progression_backup_json(t), error[160];
    assert(json && strstr(json, "\"schema\":23") && strstr(json, "\"reef\":{") &&
           strstr(json, "\"progression\":{"));
    assert(progression_restore_json(json, strlen(json), false, error, sizeof error));
    cJSON *root = cJSON_Parse(json); assert(root); free(json);
    cJSON *reef = cJSON_GetObjectItemCaseSensitive(root, "reef");
    cJSON_SetNumberValue(cJSON_GetObjectItemCaseSensitive(reef, "current_base"), 10);
    json = cJSON_PrintUnformatted(root);
    assert(!progression_restore_json(json, strlen(json), false, error, sizeof error)); free(json);
    cJSON_SetNumberValue(cJSON_GetObjectItemCaseSensitive(root, "schema"), 20);
    cJSON_DeleteItemFromObjectCaseSensitive(root, "reef");
    cJSON_DeleteItemFromObjectCaseSensitive(root, "creatures");
    cJSON_DeleteItemFromObjectCaseSensitive(root, "progression");
    cJSON_DeleteItemFromObjectCaseSensitive(root, "foliage");
    json = cJSON_PrintUnformatted(root); cJSON_Delete(root);
    assert(progression_restore_json(json, strlen(json), false, error, sizeof error)); free(json);

    json = progression_backup_json(t); assert(json);
    root = cJSON_Parse(json); free(json); assert(root);
    cJSON_SetNumberValue(cJSON_GetObjectItemCaseSensitive(root, "schema"), 21);
    cJSON_DeleteItemFromObjectCaseSensitive(root, "progression");
    cJSON_DeleteItemFromObjectCaseSensitive(root, "foliage");
    json = cJSON_PrintUnformatted(root); cJSON_Delete(root);
    assert(json && progression_restore_json(json, strlen(json), false, error, sizeof error));
    free(json);
}

static void test_retired_archive(tank_t *t) {
    start(t, GAME_MODE_NORMAL);
    t->sd_balance = 1500;
    assert(!progression_aquarium_move(t, 1));
    progression_setup_done(t);
    seed_retired(t, true);
    assert(!progression_aquarium_move(t, 0));
    assert(t->sd_balance == 100);
    tank_t before = *t;
    reject_slot_write = true;
    assert(progression_aquarium_move(t, 1));
    assert(!memcmp(&before, t, sizeof before));
    reject_slot_write = false;
    assert(!progression_aquarium_move(t, 1));
    assert(t->sd_balance == 168);
    assert(!progression_aquarium_move(t, 0));
    assert(t->sd_balance == 168);
    assert(!progression_aquarium_move(t, 1));
    assert(t->sd_balance == 168 && !progression_item_owned(t, 30));
    t->reef_currents = 4; t->reef_visits = 7;
    progression_care_refresh(t);
    assert(!progression_aquarium_move(t, 0));
    assert(t->reef_currents == 4 && t->reef_visits == 7);
}

static void test_environment_without_chore_debt(tank_t *t) {
    start(t, GAME_MODE_HARD);
    t->autofeed_off = true; t->companion.cozy = false; t->night = true;
    t->fish[0].hunger = 2;
    t->profile.starvation_s[0] = 17;
    int32_t trims = t->trims, cleaned = t->cells_cleaned;
    progression_slept(t, 14 * 86400);
    assert(t->n_fish == 2 && t->fish[0].hunger == 10);
    assert(t->profile.starvation_s[0] == 17); /* Offline hunger is not online starvation time. */
    assert(tank_veg_beds(t) == 0);
    for (size_t i = 0; i < sizeof t->algae / sizeof t->algae[0]; i++) assert(!t->algae[i]);
    assert(t->trims == trims && t->cells_cleaned == cleaned);
    assert(progression_save(t));
    now += 14 * 86400;
    reload(t);
    assert(t->n_fish == 2 && t->fish[0].hunger == 10 && t->profile.starvation_s[0] == 17);
    assert(t->trims == trims && t->cells_cleaned == cleaned && tank_veg_beds(t) == 0);
    for (size_t i = 0; i < sizeof t->algae / sizeof t->algae[0]; i++) assert(!t->algae[i]);
    progression_tick(t, 1);
    assert(t->profile.starvation_s[0] == 18 && t->n_fish == 2); /* Awake night still counts in Hard. */
}

int main(void) {
    tank_t t;
    test_reef_daily(&t);
    test_reef_history(&t);
    test_retired_upgrades(&t);
    test_paid_progression(&t);
    test_reef_json_and_prefixes(&t);
    test_retired_archive(&t);
    test_environment_without_chore_debt(&t);
    puts("PASS: Doubloons care, paid themes/food tiers, schema22/v21 migration/prefixes/JSON, Reef Rhythm history, refunds/retry and preserved Hard hunger");
    return 0;
}
