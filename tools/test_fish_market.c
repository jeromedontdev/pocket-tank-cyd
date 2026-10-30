#define main legacy_profiles_main
#include "test_profiles.c"
#undef main

static void healthy(fish_t *f) {
    f->trust = 10; f->energy = 10; f->hunger = f->stress = 0;
}

static void test_market_prices(tank_t *t) {
    int last = 0;
    for (int variety = 0; variety < FISH_VARIETY_COUNT; variety++) {
        start(t, GAME_MODE_NORMAL);
        t->sd_balance = 10000;
        fish_t preview;
        uint32_t rng = t->rng;
        assert(progression_market_preview(t, variety, &preview) && t->rng == rng);
        int price = progression_market_price(t, variety);
        assert(price > last); last = price;
        fish_market_offer_t offer;
        progression_market_offer(t, variety, &offer);
        assert(offer.price == price && offer.body == preview.color && offer.pattern == preview.pattern);
        assert(progression_market_buy(t, variety));
        int i = t->n_fish - 1;
        fish_t *f = &t->fish[i];
        assert(f->stage == STAGE_ADULT && f->variety == variety);
        assert(f->color == preview.color && f->fin == preview.fin && f->accent == preview.accent);
        assert(f->pattern == preview.pattern && f->bold == preview.bold && f->sociable == preview.sociable);
        assert(f->trust == preview.trust && f->energy == preview.energy &&
               f->hunger == preview.hunger && f->stress == preview.stress);
        assert(t->sd_balance == 10000 - price);
        assert(t->sd_paid_fish[i] == (SD_PAID_JUV | SD_PAID_ADULT));
        int bought_value = progression_fish_value(t, i);
        healthy(f);
        int max_value = progression_fish_value(t, i);
        assert(max_value >= bought_value && price == max_value + 40);
        f->trust = f->energy = 0; f->hunger = f->stress = 10;
        int minimum = progression_fish_value(t, i);
        assert(max_value > minimum && minimum > 0);
        healthy(f);
        fish_sale_t quote;
        assert(progression_fish_sale_quote(t, i, &quote));
        assert(t->n_fish == 3 && t->sd_balance == 10000 - price); /* Quote never sells. */
        assert(progression_fish_sale_confirm(t, &quote));
        assert(t->n_fish == 2 && t->sd_balance == 9960); /* Even perfect adult resale loses 40. */
        assert(!progression_fish_sale_confirm(t, &quote)); /* No replay. */
        assert(!progression_fish_sale_quote(t, 0, &quote)); /* Keep a pair. */
        assert(progression_save(t));
        reload(t); assert(t->n_fish == 2 && t->sd_balance == 9960);
    }
}

static void test_market_transactions(tank_t *t) {
    start(t, GAME_MODE_NORMAL); t->sd_balance = 1000;
    tank_t before = *t;
    reject_write = true;
    assert(!progression_market_buy(t, FISH_SHARK));
    assert(!memcmp(t, &before, sizeof before));
    reject_write = false;
    assert(progression_market_buy(t, FISH_SHARK));
    int i = t->n_fish - 1;
    float bold = t->fish[i].bold, soc = t->fish[i].sociable, lazy = t->fish[i].lazy;
    assert(progression_save(t)); reload(t);
    assert(t->fish[i].variety == FISH_SHARK && t->fish[i].bold == bold &&
           t->fish[i].sociable == soc && t->fish[i].lazy == lazy);
    fish_sale_t quote;
    assert(progression_fish_sale_quote(t, i, &quote));
    before = *t;
    reject_write = true;
    assert(!progression_fish_sale_confirm(t, &quote));
    assert(!memcmp(t, &before, sizeof before));
    reject_write = false;
    t->fish[i].trust = 0;
    assert(!progression_fish_sale_confirm(t, &quote)); /* Price changed; review again. */
    assert(progression_fish_sale_quote(t, i, &quote));
    assert(!progression_bank_move(t, -1, 0));
    assert(!progression_fish_sale_confirm(t, &quote)); /* Slots/identities moved. */
    before = *t;
    assert(!progression_market_buy(t, -1) && !progression_market_buy(t, FISH_VARIETY_COUNT));
    assert(!memcmp(t, &before, sizeof before));
    t->sd_balance = 0; before = *t;
    assert(!progression_market_buy(t, 0) && !memcmp(t, &before, sizeof before));
    start(t, GAME_MODE_CREATIVE); t->sd_balance = 1000;
    before = *t;
    assert(!progression_market_buy(t, 0) && !memcmp(t, &before, sizeof before));
    start(t, GAME_MODE_HARD); t->sd_balance = 10000;
    while (t->n_fish < POP_CAP - 1) assert(progression_market_buy(t, 0));
    t->companion.away = 1; before = *t; /* Traveler reserves the last place. */
    assert(!progression_market_buy(t, 0) && !memcmp(t, &before, sizeof before));
    t->companion.away = 0;
    assert(progression_market_buy(t, 0));
    before = *t;
    assert(!progression_market_buy(t, 0) && !memcmp(t, &before, sizeof before));
}

static void test_fry_transactions(tank_t *t) {
    start(t, GAME_MODE_NORMAL); t->sd_balance = 1000;
    assert(progression_fry_for_sale(t)); /* No first-sale bootstrap deadlock. */
    progression_fry_roll(t);
    int price = progression_fry_price(t, 0);
    tank_t before = *t;
    reject_write = true;
    assert(!progression_buy_fry(t, 0) && !memcmp(t, &before, sizeof before));
    reject_write = false;
    assert(progression_fry_left() == FRY_OFFER_N && progression_newborn() < 0);
    assert(progression_buy_fry(t, 0));
    assert(progression_fry_left() == FRY_OFFER_N - 1 && t->sd_balance == 1000 - price);
    int i = t->n_fish - 1; healthy(&t->fish[i]);
    assert(progression_fish_value(t, i) == price - 20);
    assert(!progression_buy_fry(t, 0) && !progression_market_buy(t, 0));
    assert(progression_newborn_done(t));
    fish_sale_t quote;
    assert(progression_fish_sale_quote(t, i, &quote) && progression_fish_sale_confirm(t, &quote));
    assert(t->sd_balance == 980); /* Perfect fry resale still loses 20. */
}

static void test_breeding(tank_t *t) {
    start(t, GAME_MODE_NORMAL); t->sd_balance = 10000;
    int a, b;
    assert(!progression_breeding_pair(t, &a, &b) && a == -1 && b == -1); /* Fry are not parents. */
    assert(progression_market_buy(t, FISH_SHARK));
    assert(progression_market_buy(t, FISH_SHARK));
    assert(!progression_bank_move(t, -1, 0)); /* First founder safely banked. */
    assert(!progression_bank_move(t, -1, 0)); /* Second founder safely banked. */
    assert(t->n_fish == 2 && t->bank_count == 2);
    assert(progression_breeding_pair(t, &a, &b) && a == 0 && b == 1);
    bank_fish_t bank[2]; memcpy(bank, t->bank, sizeof bank);
    uint64_t owned = t->profile.owned_items, paid = t->profile.paid_items;
    t->fish[1].variety = FISH_CLASSIC;
    assert(!progression_breeding_pair(t, NULL, NULL));
    progression_force_arrival(t); assert(t->n_fish == 2);
    t->fish[1].variety = FISH_SHARK;
    t->fish[1].stage = STAGE_JUV; assert(!progression_breeding_pair(t, NULL, NULL));
    t->fish[1].stage = STAGE_ADULT; t->fish[1].stress = 5;
    assert(!progression_breeding_pair(t, NULL, NULL));
    t->fish[1].stress = 0; t->fish[1].hunger = 7;
    assert(!progression_breeding_pair(t, NULL, NULL));
    t->fish[1].hunger = 2; t->fish[1].energy = 3;
    assert(!progression_breeding_pair(t, NULL, NULL));
    t->fish[1].energy = 8; t->fish[1].trust = 3;
    assert(!progression_breeding_pair(t, NULL, NULL));
    t->fish[1].trust = 4;
    assert(progression_breeding_pair(t, NULL, NULL));
    uint32_t id_a = t->polish.active[0].id, id_b = t->polish.active[1].id;
    t->fish[0].bold = 0.3f; t->fish[1].bold = 0.5f;
    t->fish[0].sociable = 0.5f; t->fish[1].sociable = 0.7f;
    for (int i = 0; i < 2; i++) {
        tank_set_look(t, i, 0x38dcc7, i ? 0xff725c : 0xffd166);
        t->fish[i].pattern = PATTERN_STRIPES;
        t->story.active[i].pattern2 = t->story.active[i].color2 = 0;
        t->story.active[i].traits = TRAIT_GOLD_EYE;
    }
    tank_sync_traits(t);
    t->player_feedings = 12;
    assert(strstr(progression_breeding_status(t), "ON THE WAY"));
    progression_stage_arrival(t);
    progression_force_arrival(t);
    assert(t->n_fish == 3 && t->fish[2].stage == STAGE_FRY && t->fish[2].variety == FISH_SHARK);
    const fish_lineage_t *line = &t->story.active[2];
    assert((line->parent[0] == id_a && line->parent[1] == id_b) ||
           (line->parent[0] == id_b && line->parent[1] == id_a));
    assert(line->generation == 1 && t->story.births == 1);
    assert(t->fish[2].color == 0x38dcc7);
    assert(t->fish[2].accent == 0xff725c || t->fish[2].accent == 0xffd166);
    assert((line->traits & TRAIT_GOLD_EYE) && (t->fish[2].traits & TRAIT_GOLD_EYE));
    assert(t->fish[2].pattern == PATTERN_STRIPES ||
           ((line->mutation & GENE_MUTANT_PATTERN) && line->pattern2 == PATTERN_STRIPES + 1));
    assert(t->fish[2].bold >= 0.25f && t->fish[2].bold <= 0.55f);
    assert(t->fish[2].sociable >= 0.45f && t->fish[2].sociable <= 0.75f);
    assert(!memcmp(bank, t->bank, sizeof bank) && t->profile.owned_items == owned && t->profile.paid_items == paid);
    assert(progression_newborn_done(t));
    assert(strstr(progression_breeding_status(t), "MORE MEALS")); /* Birth resets the earned meal cadence. */
    assert(progression_save(t)); reload(t);
    assert(t->n_fish == 3 && t->fish[2].variety == FISH_SHARK && t->story.active[2].generation == 1);
    assert(!memcmp(bank, t->bank, sizeof bank));
    fish_sale_t quote;
    assert(progression_fish_sale_quote(t, 2, &quote) && progression_fish_sale_confirm(t, &quote));
    assert(t->n_fish == 2 && progression_breeding_pair(t, &a, &b));
}

static void test_market_aquariums(tank_t *t) {
    start(t, GAME_MODE_NORMAL); t->sd_balance = 3000;
    assert(progression_market_buy(t, 2) && progression_market_buy(t, 2));
    fish_sale_t old;
    assert(progression_fish_sale_quote(t, 2, &old));
    assert(!progression_aquarium_move(t, 1));
    progression_setup_done(t);
    assert(progression_market_buy(t, 3) && progression_market_buy(t, 3));
    int a, b;
    assert(progression_breeding_pair(t, &a, &b) && t->fish[a].variety == 3 && t->fish[b].variety == 3);
    assert(!progression_fish_sale_confirm(t, &old)); /* A different tank cannot consume the old quote. */
    int balance = t->sd_balance;
    tank_t before = *t;
    reject_slot_write = true;
    assert(progression_aquarium_move(t, 0) && !memcmp(t, &before, sizeof before));
    reject_slot_write = false;
    assert(!progression_aquarium_move(t, 0));
    assert(t->sd_balance == balance && progression_breeding_pair(t, &a, &b));
    assert(t->fish[a].variety == 2 && t->fish[b].variety == 2);
    assert(!progression_aquarium_move(t, 1));
    assert(t->sd_balance == balance && progression_breeding_pair(t, &a, &b));
    assert(t->fish[a].variety == 3 && t->fish[b].variety == 3);
}

static int expected_creature_quote(const tank_t *t, int species) {
    static const int pct[CREATURE_RARITY_COUNT] = {100, 140, 200, 350};
    int r = progression_creature_rarity_of(t, species);
    assert(r >= 0 && r < CREATURE_RARITY_COUNT);
    int v = progression_creature_price(t, species) / 2 * pct[r] / 100;
    if (species == CREATURE_SHRIMP) v = v * (50 + 50 * t->shrimp_n / SHRIMP_MAX) / 100;
    return v;
}

static void test_creature_market(tank_t *t) {
    const int modes[] = {GAME_MODE_NORMAL, GAME_MODE_HARD};
    const int items[] = {5, 1, 6, 7, SD_SHOP_MUSSEL};
    assert(progression_creature_rarity(0) == 0 && progression_creature_rarity(0x03) == 1 &&
           progression_creature_rarity(0x33) == 1 && progression_creature_rarity(0x53) == 2 &&
           progression_creature_rarity(0x5c) == 3);
    for (int m = 0; m < 2; m++)
        for (int species = CREATURE_SHRIMP; species <= CREATURE_MUSSEL; species++) {
            start(t, modes[m]); t->sd_balance = 10000;
            int item = items[species];
            int price = progression_creature_price(t, species);
            assert(price == SD_ITEMS[item].price && price > 0);
            assert(!companion_creature_colors(&t->companion, species));
            t->profile.owned_items |= UINT64_C(1) << item; /* Historical unlock is not a free animal. */
            assert(progression_item_price_for(t, item) == price);
            tank_t before = *t;
            reject_write = true;
            assert(!progression_creature_buy(t, species) && !memcmp(t, &before, sizeof before));
            reject_write = false;
            assert(progression_creature_buy(t, species));
            assert(progression_creature_adults(t, species) == 1 && t->sd_balance == 10000 - price);
            before = *t;
            assert(!progression_creature_buy(t, species) && !progression_buy(t, item)); /* One per tank. */
            assert(t->sd_balance == before.sd_balance && progression_creature_adults(t, species) == 1);
            assert(!progression_creature_offspring(t, species) && !progression_creature_seconds(t, species));
            for (int k = 0; k < 4000; k++) progression_tick(t, 1);
            assert(progression_creature_adults(t, species) == 1 && !progression_creature_offspring(t, species));
            if (species != CREATURE_SHRIMP)
                assert(!progression_inventory_buy(t, item) && !progression_inventory_store(t, item, -1) &&
                       !progression_sell(t, item));
            creature_sale_t sale;
            assert(!progression_creature_sale_quote(t, species, true, &sale)); /* No young to sell. */
            assert(progression_creature_sale_quote(t, species, false, &sale));
            assert(sale.value == expected_creature_quote(t, species) && sale.value > 0);
            before = *t; reject_write = true;
            assert(!progression_creature_sale_confirm(t, &sale) && !memcmp(t, &before, sizeof before));
            reject_write = false;
            char *old = progression_backup_json(t); assert(old);
            int balance = t->sd_balance;
            assert(progression_creature_sale_confirm(t, &sale) && t->sd_balance == balance + sale.value);
            assert(!progression_creature_sale_confirm(t, &sale));
            assert(!progression_creature_adults(t, species) && !companion_creature_colors(&t->companion, species));
            assert(!(t->profile.paid_items & (UINT64_C(1) << item)));
            char error[160];
            assert(!progression_restore_json(old, strlen(old), true, error, sizeof error)); /* No sold-stock resurrection. */
            free(old);
            balance = t->sd_balance;
            assert(progression_buy(t, item) && t->sd_balance == balance - price); /* No free replacement. */
            assert(progression_creature_adults(t, species) == 1);
        }
    /* Colors roll at purchase with a lottery-like spread and a losing average flip. */
    start(t, GAME_MODE_NORMAL); t->sd_balance = 2000000000;
    int seen[CREATURE_RARITY_COUNT] = {0}; long spent = 0, back = 0;
    for (int k = 0; k < 400; k++) {
        assert(progression_creature_buy(t, CREATURE_SEAHORSE));
        spent += progression_creature_price(t, CREATURE_SEAHORSE);
        seen[progression_creature_rarity_of(t, CREATURE_SEAHORSE)]++;
        creature_sale_t sale;
        assert(progression_creature_sale_quote(t, CREATURE_SEAHORSE, false, &sale));
        assert(progression_creature_sale_confirm(t, &sale)); back += sale.value;
    }
    for (int r = 0; r < CREATURE_RARITY_COUNT; r++) assert(seen[r] > 0);
    assert(seen[0] > seen[3] && back < spent && back * 2 > spent);
    /* Legacy pairs and nurseries retire once, refunding today's quotes. */
    start(t, GAME_MODE_NORMAL); t->sd_balance = 1000;
    assert(progression_creature_buy(t, CREATURE_SNAIL));
    t->creatures.stock[0].adults = 2; t->creatures.paid_adults[0] = 2;
    t->creatures.stock[0].offspring = 3; t->creatures.stock[0].active_s = 10;
    int balance = t->sd_balance, snail = progression_creature_price(t, CREATURE_SNAIL);
    int refund = expected_creature_quote(t, CREATURE_SNAIL) + 3 * (snail / 5);
    progression_tick(t, 1);
    assert(t->creatures.stock[0].adults == 1 && t->creatures.paid_adults[0] == 1);
    assert(!t->creatures.stock[0].offspring && !t->creatures.stock[0].active_s);
    assert(t->sd_balance == balance + refund);
    progression_tick(t, 1);
    assert(t->sd_balance == balance + refund);
    start(t, GAME_MODE_NORMAL);
    tank_t poor = *t;
    assert(!progression_creature_buy(t, CREATURE_SNAIL) && !memcmp(t, &poor, sizeof poor));
    t->sd_unlocks |= SD_ITEM_SNAIL; /* An unpaid legacy resident survives, but is not a cash refund. */
    assert(progression_creature_adults(t, CREATURE_SNAIL) == 1);
    creature_sale_t quote;
    assert(progression_creature_sale_quote(t, CREATURE_SNAIL, false, &quote) && quote.value == 0);
    assert(progression_creature_sale_confirm(t, &quote));
    assert(!progression_creature_adults(t, CREATURE_MUSSEL) && !progression_creature_buy(t, CREATURE_MUSSEL));
    start(t, GAME_MODE_CREATIVE);
    assert(!progression_creature_buy(t, CREATURE_SNAIL));
    assert(progression_buy(t, 1) && progression_creature_adults(t, CREATURE_SNAIL) == 1);
    assert(!progression_creature_sale_quote(t, CREATURE_SNAIL, false, &quote));
    const int shop_item[] = {5, 1, 6, 7, SD_SHOP_MUSSEL};
    for (int species = CREATURE_SHRIMP; species < CREATURE_COUNT; species++) {
        start(t, GAME_MODE_CREATIVE);
        assert(progression_buy(t, shop_item[species]));
        assert(!companion_colors(t, false, species, 0x31)); /* Colors come from purchase, never a picker. */
    }
}
static void test_creature_formats_and_aquariums(tank_t *t) {
    start(t, GAME_MODE_NORMAL); t->sd_balance = 4000;
    t->sd_unlocks |= SD_ITEM_SNAIL;
    t->profile.paid_items |= UINT64_C(1) << 1; /* Paid legacy resident, not a fictional pair. */
    t->profile.owned_items |= UINT64_C(1) << 1;
    assert(progression_save(t));
    saved_size = 6424;
    progression_wake(t, 0);
    assert(progression_creature_adults(t, CREATURE_SNAIL) == 1 && t->creatures.paid_adults[0] == 1);
    assert(!progression_creature_offspring(t, CREATURE_SNAIL));
    assert(progression_save(t) && saved_size == AQUARIUM_SAVE_BYTES);
    char *base = progression_backup_json(t); assert(base);
    const char *fields[] = {"snail_adults", "snail_offspring", "snail_seconds", "version", "revision"};
    const double invalid[] = {3, CREATURE_NURSERY_CAP + 1, 1, 0, 0.5};
    char error[160];
    for (size_t i = 0; i < sizeof fields / sizeof fields[0]; i++) {
        cJSON *root = cJSON_Parse(base); assert(root);
        cJSON *c = cJSON_GetObjectItemCaseSensitive(root, "creatures");
        cJSON_SetNumberValue(cJSON_GetObjectItemCaseSensitive(c, fields[i]), invalid[i]);
        char *bad = cJSON_PrintUnformatted(root); assert(bad);
        assert(!progression_restore_json(bad, strlen(bad), false, error, sizeof error));
        free(bad); cJSON_Delete(root);
    }
    cJSON *root = cJSON_Parse(base);
    cJSON *paid = cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(root, "creatures"), "paid_adults");
    cJSON_SetNumberValue(cJSON_GetArrayItem(paid, 0), 2); /* Cannot refund more paid adults than exist. */
    char *bad = cJSON_PrintUnformatted(root);
    assert(!progression_restore_json(bad, strlen(bad), false, error, sizeof error));
    free(bad); cJSON_Delete(root);
    start(t, GAME_MODE_NORMAL); /* A real first recovery into a pristine single aquarium remains possible. */
    assert(progression_restore_json(base, strlen(base), true, error, sizeof error));
    progression_wake(t, 0);
    assert(progression_creature_adults(t, CREATURE_SNAIL) == 1 && t->creatures.paid_adults[0] == 1);
    free(base);
    assert(!progression_creature_buy(t, CREATURE_SNAIL)); /* Already one per tank. */
    assert(progression_save(t));
    assert(!progression_aquarium_move(t, 1)); progression_setup_done(t);
    assert(!progression_creature_adults(t, CREATURE_SNAIL)); /* Global ownership is not local stock. */
    assert(progression_item_price_for(t, 1) == SD_ITEMS[1].price);
    assert(progression_creature_buy(t, CREATURE_URCHIN) && !progression_creature_buy(t, CREATURE_URCHIN));
    creature_sale_t second_quote;
    assert(progression_creature_sale_quote(t, CREATURE_URCHIN, false, &second_quote));
    int wallet = t->sd_balance;
    creature_state_t second = t->creatures;
    tank_t before = *t;
    reject_slot_write = true;
    assert(progression_aquarium_move(t, 0) && !memcmp(t, &before, sizeof before));
    reject_slot_write = false;
    assert(!progression_aquarium_move(t, 0));
    assert(t->sd_balance == wallet && progression_creature_adults(t, CREATURE_SNAIL) == 1);
    assert(!progression_creature_adults(t, CREATURE_URCHIN));
    assert(!progression_creature_sale_confirm(t, &second_quote)); /* Quotes cannot cross aquarium identity. */
    assert(!progression_aquarium_move(t, 1));
    assert(t->sd_balance == wallet && !memcmp(&t->creatures, &second, sizeof second));
}

int main(void) {
    tank_t t;
    test_market_prices(&t);
    test_market_transactions(&t);
    test_fry_transactions(&t);
    test_breeding(&t);
    test_market_aquariums(&t);
    test_creature_market(&t);
    test_creature_formats_and_aquariums(&t);
    puts("PASS: fish/three-species creature markets, truthful previews, no instant resale profit, atomic failures, confirmed sales, inherited fish genes, one-per-tank creatures with rolled colors, legacy breeding retired, legacy paid/free migration, schema validation, Bank/travel and aquarium isolation");
    return 0;
}
