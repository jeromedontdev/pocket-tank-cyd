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
    char expected[80], actual[80];
    static const char *const names[] = {"DART", "BUBBLES", "FOLLOW"};
    static const unsigned thresholds[] = {10, 25, 50};
    for (unsigned bond = 0; bond <= 100; bond++) {
        t.companion.active[0].bond = (uint8_t)bond;
        if (bond >= 50) snprintf(expected, sizeof expected, "ALL TRICKS OPEN.");
        else {
            unsigned j = bond < 10 ? 0 : bond < 25 ? 1 : 2;
            unsigned count = (thresholds[j] - bond + 4) / 5;
            snprintf(expected, sizeof expected, "%s: %u BOND / %u HELLO%s.",
                     names[j], thresholds[j], count, count == 1 ? "" : "S");
        }
        bond_progress_text(&t, &t.companion.active[0], actual, sizeof actual);
        assert(!strcmp(expected, actual));
        draw_feedback(&t, COMPANION_SECTION_CARE);
        assert(strstr(observed, expected));
        assert(strstr(observed, "HELLOS: 30S APART. BOND STAYS."));
        for (int trick = 0; trick < 3; trick++)
            assert(companion_trick(&t, 0, trick) == (bond >= thresholds[trick]));
    }
    t.companion.active[0].bond = 0;
    t.companion.powered_s = 100; t.companion.active[0].next_greet_s = 0;
    assert(companion_greet(&t, 0));
    assert(t.companion.active[0].bond == 5 && t.companion.active[0].next_greet_s == 130);
    companion_open_section(COMPANION_SECTION_CARE);
    assert(!companion_tap(&t, render_frame_x(80), render_frame_y(120)));
    assert(!strcmp(notice, "Hello ready in 30s."));
    t.companion.powered_s = 129;
    assert(!companion_greet(&t, 0));
    assert(!companion_tap(&t, render_frame_x(80), render_frame_y(120)));
    assert(!strcmp(notice, "Hello ready in 1s."));
    t.companion.powered_s = 130;
    assert(!companion_tap(&t, render_frame_x(80), render_frame_y(120)));
    assert(!strcmp(notice, "Hello! +5 bond. Trust unchanged."));
    assert(t.companion.active[0].bond == 10);
    fresh_feedback(&t, compact, GAME_MODE_CREATIVE);
    assert(!t.companion.active[0].bond);
    draw_feedback(&t, COMPANION_SECTION_CARE);
    assert(strstr(observed, "CREATIVE: ALL TRICKS OPEN.") && !strstr(observed, "HELLOS LEFT"));
    for (int trick = 0; trick < 3; trick++) {
        companion_open_section(COMPANION_SECTION_CARE);
        companion_tap(&t, render_frame_x(40 + 146 * trick), render_frame_y(255));
        assert(strstr(notice, "BACK to watch.") && !strstr(notice, "Locked"));
    }
    t.n_fish = 1;
    companion_tap(&t, render_frame_x(340), render_frame_y(255));
    assert(!strcmp(notice, "Follow needs a tankmate at home."));
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
            assert(strstr(observed, "NEXT:") && strstr(observed, "DECOR SETS REST/INTEREST SPOTS."));
        }
        progression_set_age(&t, 0, (float)thresholds[stage]);
        assert(t.fish[0].stage == (stage_t)(stage + 1));
    }
    draw_feedback(&t, COMPANION_SECTION_LIFE);
    life_progress_text(&t, 0, actual, sizeof actual);
    assert(!strcmp(actual, "ELDER: NO NEXT STAGE. AGE CONTINUES WITH TIME."));
    progression_set_age(&t, 0, 0);
    t.night = true;
    progression_tick(&t, 3600);
    assert(progression_age_s(&t, 0) == 3600);
    progression_slept(&t, 3600);
    assert(progression_age_s(&t, 0) == 4500); /* Sleep contributes 25%, not a wall-clock ETA. */
    life_progress_text(&t, 0, actual, sizeof actual);
    assert(!strcmp(actual, "NEXT: JUVENILE IN 1 GROWTH-HOUR; SLEEP IS SLOWER."));
    float age = progression_age_s(&t, 0);
    draw_feedback(&t, COMPANION_SECTION_LIFE);
    assert(progression_age_s(&t, 0) == age); /* Rendering itself never advances time. */
}
int main(void) {
    for (int compact = 0; compact < 2; compact++) {
        check_bonds(compact != 0);
        check_growth(compact != 0);
    }
    puts("PASS: companion bond thresholds/counts/cooldowns, Creative tricks, stage rounding and sleep growth; read-only pages fit both scales.");
    return 0;
}
