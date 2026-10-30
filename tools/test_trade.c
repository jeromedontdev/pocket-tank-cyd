#define main legacy_profiles_main
#include "test_profiles.c"
#undef main

static void healthy(fish_t *f) {
    f->trust = 10; f->energy = 10; f->hunger = f->stress = 0;
}

/* Three adults of the wanted variety; the last one is a bred, mutated fish. */
static int stock(tank_t *t, int variety) {
    t->sd_balance = 100000;
    while (t->n_fish < 4) assert(progression_market_buy(t, variety));
    for (int i = 0; i < t->n_fish; i++) {
        t->fish[i].variety = (uint8_t)variety; t->fish[i].stage = STAGE_ADULT; healthy(&t->fish[i]);
        t->story.active[i].generation = 0; t->story.active[i].mutation = 0;
    }
    int bred = t->n_fish - 1;
    t->story.active[bred].generation = 2; t->story.active[bred].mutation = 7;
    assert(t->story.version && t->polish.active[bred].id);
    return bred;
}

static void test_contracts(tank_t *t) {
    start(t, GAME_MODE_NORMAL);
    t->care.day = 20000;
    fish_contract_t c, again;
    assert(progression_contract_today(t, &c) && !c.filled);
    assert(c.variety >= 0 && c.variety < FISH_VARIETY_COUNT && c.need >= 0 && c.need < CONTRACT_NEED_COUNT);
    assert(c.client >= 0 && c.client < PROGRESSION_TRADE_CLIENT_COUNT && c.days_left >= 1 && c.days_left <= 3);
    t->care.day = 20000 / 3 * 3 + 2; /* the whole 3-day window keeps one order */
    assert(progression_contract_today(t, &again) && again.variety == c.variety && again.need == c.need &&
           again.client == c.client && again.days_left == 1);
    t->care.day = 20000;
    int bred = stock(t, c.variety);
    assert(progression_contract_fits(t, bred));
    assert(!progression_contract_fits(t, 0)); /* Market stock, never bred. */
    t->story.active[0].generation = 1;
    assert(!progression_contract_fits(t, 0)); /* Bred but no grade/mutation. */
    t->story.active[0].generation = 0;
    int value = progression_fish_value(t, bred);
    int pay = progression_contract_pay(t, bred);
    assert(pay == value * PROGRESSION_CONTRACT_PCT / 100 + PROGRESSION_CONTRACT_BONUS && pay > value);
    t->fish[bred].stage = STAGE_JUV;
    assert(!progression_contract_fits(t, bred) && !progression_contract_pay(t, bred));
    t->fish[bred].stage = STAGE_ADULT;
    fish_sale_t quote;
    assert(progression_contract_quote(t, bred, &quote) && quote.value == pay);
    assert(progression_save(t));
    tank_t before = *t;
    reject_write = true;
    assert(!progression_contract_fill(t, &quote) && !memcmp(t, &before, sizeof before));
    reject_write = false;
    fish_sale_t stale = quote; stale.value++;
    assert(!progression_contract_fill(t, &stale) && !memcmp(t, &before, sizeof before));
    long wallet = t->sd_balance;
    int n = t->n_fish;
    assert(progression_contract_fill(t, &quote));
    assert(t->sd_balance == wallet + pay && t->n_fish == n - 1);
    assert(t->progression.reserved == (uint8_t)(20000 / 3 % 255 + 1));
    assert(progression_contract_today(t, &c) && c.filled);
    assert(!progression_contract_fill(t, &quote)); /* No replay. */
    t->story.active[0].generation = 3; t->story.active[0].mutation = 7;
    assert(!progression_contract_fits(t, 0)); /* One fill per order. */
    char *json = progression_backup_json(t);
    assert(json && strstr(json, "\"contract_done\":"));
    free(json);
    reload(t);
    assert(t->progression.reserved == (uint8_t)(20000 / 3 % 255 + 1) && t->sd_balance == wallet + pay);
    t->care.day = 20000;
    assert(progression_contract_today(t, &c) && c.filled);
    t->care.day = 20000 + 3;
    assert(progression_contract_today(t, &c) && !c.filled);
    printf("contract %s wants %s %d, paid %d over quote %d\n", PROGRESSION_TRADE_CLIENTS[c.client],
           FISH_VARIETY_NAMES[c.variety], c.need, pay, value);
}

static void test_auction(tank_t *t) {
    start(t, GAME_MODE_NORMAL);
    int64_t saturday = 20000 - (20000 % 7) + PROGRESSION_AUCTION_WEEKDAY;
    t->care.day = saturday - 1;
    assert(progression_auction_days(t) == 1 && !progression_auction_open(t));
    t->care.day = saturday;
    assert(!progression_auction_days(t) && progression_auction_open(t) && !progression_auction_sold(t));
    int bred = stock(t, 2);
    assert(progression_auction_eligible(t, bred) && !progression_auction_eligible(t, 0));
    t->story.active[0].generation = 2;
    assert(progression_auction_eligible(t, 0)); /* 2nd-generation lines qualify. */
    t->story.active[0].generation = 0;
    t->fish[bred].stage = STAGE_JUV;
    assert(!progression_auction_eligible(t, bred));
    t->fish[bred].stage = STAGE_ADULT;
    fish_sale_t quote;
    assert(progression_auction_quote(t, bred, &quote) && quote.value == progression_fish_value(t, bred));
    assert(progression_auction_reserve(t, bred) == quote.value * PROGRESSION_AUCTION_RESERVE_PCT / 100);
    assert(progression_save(t));
    tank_t before = *t;
    fish_auction_t result, replay;
    reject_write = true;
    assert(!progression_auction_sell(t, &quote, &result) && !memcmp(t, &before, sizeof before));
    reject_write = false;
    long wallet = t->sd_balance;
    assert(progression_auction_sell(t, &quote, &result));
    assert(result.hammer >= quote.value * 95 / 100 && result.hammer <= quote.value * 165 / 100);
    assert(result.opening == quote.value * 80 / 100 && result.bids[PROGRESSION_AUCTION_BIDS - 1] == result.hammer);
    for (int i = 0; i < PROGRESSION_AUCTION_BIDS; i++) {
        assert(result.bidder[i] < PROGRESSION_TRADE_CLIENT_COUNT);
        if (i) assert(result.bids[i] >= result.bids[i - 1] && result.bidder[i] != result.bidder[i - 1]);
    }
    assert(t->sd_balance == wallet + result.hammer && progression_auction_sold(t) && !progression_auction_open(t));
    tank_t after = *t;
    *t = before; /* the same lot in the same week always hammers at the same price */
    assert(progression_auction_sell(t, &quote, &replay) && replay.hammer == result.hammer);
    for (int i = 0; i < PROGRESSION_AUCTION_BIDS; i++)
        assert(replay.bids[i] == result.bids[i] && replay.bidder[i] == result.bidder[i]);
    *t = after;
    assert(!progression_auction_quote(t, 0, &quote));
    char *json = progression_backup_json(t);
    assert(json && strstr(json, "\"auction_done\":") && !strstr(json, "\"contract_done\""));
    free(json);
    reload(t);
    t->care.day = saturday;
    assert(progression_auction_sold(t) && !progression_auction_open(t));
    t->care.day = saturday + 1;
    assert(progression_auction_days(t) == 6);
    t->care.day = saturday + 7;
    assert(!progression_auction_sold(t) && progression_auction_open(t));
    printf("auction quote %d hammer %d (%d%%)\n", quote.value, result.hammer,
           (int)((long)result.hammer * 100 / quote.value));
}

static void test_creative_and_hard(tank_t *t) {
    fish_contract_t c;
    start(t, GAME_MODE_CREATIVE);
    t->care.day = 20000 - (20000 % 7) + PROGRESSION_AUCTION_WEEKDAY;
    assert(!progression_contract_today(t, &c) && !progression_auction_open(t));
    start(t, GAME_MODE_HARD);
    t->care.day = 20000;
    assert(progression_contract_today(t, &c));
}

int main(void) {
    tank_t t;
    test_contracts(&t);
    test_auction(&t);
    test_creative_and_hard(&t);
    puts("PASS: breeding contracts (bred-only, grade/mutation, x1.6+50, once per 3-day order) and weekly auctions (eligibility, deterministic 95-165% hammer, once a week) save atomically and persist");
    return 0;
}
