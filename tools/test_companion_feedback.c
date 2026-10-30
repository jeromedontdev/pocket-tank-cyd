/* Reuse the native in-memory persistence ports; only run feedback regressions. */
#define main backup_tests_main
#include "test_tank_backup.c"
#undef main
#include "../common/companion_ui.c"

static uint16_t feedback_frame[320 * 240];
static char observed[8192];
static int observed_count;
static struct { int x, y, w, h; } observed_bounds[160];

void menu_test_panel(int w, int h) { (void)w; (void)h; }
void menu_test_text(int x, int y, int scale, const char *label) {
    if (!*label) return;
    int w = render_text_w(label, scale);
    int h = scale == 1 ? (7 * PAGE_H + TANK_H - 1) / TANK_H : 7 * scale;
    if (!(x >= 0 && y >= 0 && x + w <= PAGE_W && y + h <= PAGE_H))
        fprintf(stderr, "Feedback text bounds: %s (%d,%d %dx%d)\n", label, x, y, w, h);
    assert(x >= 0 && y >= 0 && x + w <= PAGE_W && y + h <= PAGE_H);
    for (int i = 0; i < observed_count; i++)
        assert(!(x < observed_bounds[i].x + observed_bounds[i].w &&
                 x + w > observed_bounds[i].x && y < observed_bounds[i].y + observed_bounds[i].h &&
                 y + h > observed_bounds[i].y));
    assert(observed_count < 160);
    observed_bounds[observed_count].x = x; observed_bounds[observed_count].y = y;
    observed_bounds[observed_count].w = w; observed_bounds[observed_count++].h = h;
    size_t n = strlen(observed);
    assert(n + strlen(label) + 2 < sizeof observed);
    snprintf(observed + n, sizeof observed - n, "%s\n", label);
}
void menu_test_button(int x, int y, int w, int h, const char *label, int scale) {
    assert(x >= 0 && y >= 0 && x + w <= PAGE_W && y + h <= PAGE_H);
    assert(render_text_w(label, scale) <= w - 8);
}
static void draw_feedback(tank_t *t, int section) {
    companion_open_section(section);
    observed[0] = 0; observed_count = 0;
    tank_t before = *t;
    int old_writes = writes;
    companion_render(t, feedback_frame, TANK_W);
    assert(!memcmp(&before, t, sizeof before) && writes == old_writes);
}
static void fresh_feedback(tank_t *t, bool compact, int mode) {
    saved_size = 0;
    tank_init(t, 93);
    progression_boot(t);
    assert(!progression_mode_select(t, mode));
    assert(progression_setup_done(t));
    tank_reframe(t, compact);
    t->companion.display_compact = compact;
}
static void check_bonds(bool compact) {
    tank_t t;
    fresh_feedback(&t, compact, GAME_MODE_NORMAL);
    for (unsigned bond = 0; bond <= 100; bond++) {
        t.companion.active[0].bond = (uint8_t)bond;
        tank_t before = t;
        assert(!companion_greet(&t, 0) && !memcmp(&before, &t, sizeof t));
        assert(!companion_trick(&t, 0, 1));
        draw_feedback(&t, COMPANION_SECTION_CARE);
        assert(companion_current_section() == COMPANION_SECTION_SUMMARY);
        assert(strstr(observed, "FISH SUMMARY") && !strstr(observed, "HELLO"));
        draw_feedback(&t, COMPANION_SECTION_TRICKS);
        assert(companion_current_section() == COMPANION_SECTION_SUMMARY);
        draw_feedback(&t, COMPANION_SECTION_REEF);
        assert(companion_current_section() == COMPANION_SECTION_SUMMARY);
    }
    fresh_feedback(&t, compact, GAME_MODE_CREATIVE);
    tank_t before = t;
    assert(!companion_greet(&t, 0) && !memcmp(&before, &t, sizeof t));
    draw_feedback(&t, COMPANION_SECTION_CARE);
    assert(companion_current_section() == COMPANION_SECTION_SUMMARY);
}
static void check_growth(bool compact) {
    tank_t t;
    fresh_feedback(&t, compact, GAME_MODE_NORMAL);
    char actual[80], expected[80];
    static const char *const names[] = {"JUVENILE", "ADULT", "ELDER"};
    static const unsigned thresholds[] = {STAGE_JUV_AGE, STAGE_ADULT_AGE, STAGE_ELDER_AGE};
    for (int stage = STAGE_FRY; stage < STAGE_ELDER; stage++) {
        const float ages[] = {stage ? thresholds[stage - 1] : 0,
                              thresholds[stage] - 3600.5f, thresholds[stage] - 3600.0f,
                              thresholds[stage] - .5f};
        for (unsigned a = 0; a < sizeof ages / sizeof *ages; a++) {
            progression_set_age(&t, 0, ages[a]);
            assert(t.fish[0].stage == (stage_t)stage);
            unsigned hours = (unsigned)ceilf((thresholds[stage] - ages[a]) / 3600);
            snprintf(expected, sizeof expected, "NEXT: %s IN %u GROWTH-HOUR%s; SLEEP IS SLOWER.",
                     names[stage], hours, hours == 1 ? "" : "S");
            life_progress_text(&t, 0, actual, sizeof actual);
            assert(!strcmp(expected, actual));
            draw_feedback(&t, COMPANION_SECTION_LIFE);
            assert(strstr(observed, "NEXT:") && strstr(observed, "LOVES:") && strstr(observed, "GROWTH x"));
        }
        progression_set_age(&t, 0, (float)thresholds[stage]);
        assert(t.fish[0].stage == (stage_t)(stage + 1));
    }
    draw_feedback(&t, COMPANION_SECTION_LIFE);
    life_progress_text(&t, 0, actual, sizeof actual);
    assert(!strcmp(actual, "ELDER: NO NEXT STAGE. AGE CONTINUES WITH TIME."));
    progression_set_age(&t, 0, 0);
    t.night = true;
    float g = progression_fish_growth_factor(&t, 0);   /* favorite decor and vigor */
    progression_tick(&t, 3600);
    assert(fabsf(progression_age_s(&t, 0) - 3600 * g) < .05f);
    progression_slept(&t, 3600);
    assert(fabsf(progression_age_s(&t, 0) - 4500 * g) < .05f); /* Sleep contributes 25%, not a wall-clock ETA. */
    life_progress_text(&t, 0, actual, sizeof actual);
    assert(!strcmp(actual, "NEXT: JUVENILE IN 1 GROWTH-HOUR; SLEEP IS SLOWER."));
    float age = progression_age_s(&t, 0);
    draw_feedback(&t, COMPANION_SECTION_LIFE);
    assert(progression_age_s(&t, 0) == age); /* Rendering itself never advances time. */
}
static void check_symbols(bool compact) {
    tank_t t;
    fresh_feedback(&t, compact, GAME_MODE_NORMAL);
    tank_t before = t;
    int old_writes = writes;
    companion_open_section(COMPANION_SECTION_SYMBOLS);
    static const char *const expected[] = {
        "STRESS: LESS IS BETTER.", "NEITHER END IS GOOD OR BAD.",
        "SWORD PLANT: A CALM SHELTER."
    };
    for (int p = 0; p < 3; p++) {
        observed[0] = 0; observed_count = 0;
        companion_render(&t, feedback_frame, TANK_W);
        assert(strstr(observed, expected[p]));
        if (!p) {
            assert(strstr(observed, "FOOD / FULLNESS: MORE IS BETTER."));
            assert(strstr(observed, "BOLT / ENERGY: MORE IS BETTER."));
            assert(strstr(observed, "HEART / TRUST: MORE IS BETTER."));
        }
        if (p == 2) {
            assert(strstr(observed, "ARROW: NEXT. BACK: FISH MENU."));
            assert(strstr(observed, "MAIN MENU: TANK, GAMES, SETTINGS."));
            assert(!strstr(observed, "SPONGE") && !strstr(observed, "SCISSORS"));
        }
        assert(!companion_tap(&t, render_frame_x(330), render_frame_y(326)));
    }
    assert(leaf == 0);
    assert(!companion_tap(&t, render_frame_x(100), render_frame_y(326)) && leaf == 2);
    assert(companion_tap(&t, render_frame_x(390), render_frame_y(24)));
    assert(writes == old_writes && !memcmp(&before, &t, sizeof t));
}
static void check_market_fit(bool compact) {
    tank_t t;
    fresh_feedback(&t, compact, GAME_MODE_NORMAL);
    t.sd_balance = 99999;
    memset(t.fish[0].name, 'W', FISH_NAME_MAX); t.fish[0].name[FISH_NAME_MAX] = 0;
    t.fish[0].trust = t.fish[0].energy = t.fish[0].hunger = t.fish[0].stress = 10.f;
    while (t.n_fish < POP_CAP && t.n_fish < N_FISH_MAX) t.fish[t.n_fish++] = t.fish[0];
    assert(progression_market_status(&t));
    for (int stage = STAGE_FRY; stage <= STAGE_ELDER; stage++)
        for (int rank = 0; rank < FISH_VARIETY_COUNT; rank++) {
            t.fish[0].stage = (stage_t)stage; t.fish[0].variety = (uint8_t)companion_variety_at(rank);
            market_rank = rank; selected = 0;
            draw_feedback(&t, COMPANION_SECTION_MARKET);
            for (int i = 0; i < observed_count; i++)
                assert(observed_bounds[i].x + observed_bounds[i].w <= PAGE_W - 12);
        }
    for (int kind = 0; kind < CREATURE_COUNT; kind++) {
        if (kind == CREATURE_MUSSEL) { t.mussel.version = 2; t.mussel.owned = (uint8_t)(1u << t.aquariums.active); t.companion.mussel_colors = 0x5c; }
        else if (kind) t.creatures.stock[kind - 1] = (creature_stock_t){0, 1, 0};
        else { t.sd_unlocks |= SD_ITEM_SHRIMP; t.shrimp_n = SHRIMP_MAX; }
        if (kind != CREATURE_MUSSEL) t.companion.creature_colors[kind] = 0x5c;
        market_creature = kind;
        draw_feedback(&t, COMPANION_SECTION_CREATURE_MARKET);
        for (int i = 0; i < observed_count; i++)
            assert(observed_bounds[i].x + observed_bounds[i].w <= PAGE_W - 12);
    }
}
int main(void) {
    for (int compact = 0; compact < 2; compact++) {
        check_bonds(compact != 0);
        check_growth(compact != 0);
        check_symbols(compact != 0);
        check_market_fit(compact != 0);
    }
    puts("PASS: companion bond thresholds/counts/cooldowns, Creative tricks, stage rounding and sleep growth; read-only and worst-case market pages fit both scales.");
    return 0;
}
