#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "icons.h"
#include "progression.h"
#include "render.h"

static uint8_t icon_bank[128 * 1024];
static size_t icon_bytes;
const icon_t *icons_resolve(const icon_t *icon) {
    static icon_t view;
    size_t pixels = (size_t)icon->w * icon->h;
    assert(icon->offset <= icon_bytes && pixels * 3 <= icon_bytes - icon->offset);
    view = *icon;
    view.rgb = (const uint16_t *)(icon_bank + icon->offset);
    view.a = icon_bank + icon->offset + pixels * 2;
    return &view;
}

static goal_t hold_goal(const tank_t *t, int idx, bool request) {
    (void)request;
    return t->fish[idx].goal;
}

static void one_fish(tank_t *t, uint32_t seed, int preset, float bold, float lazy) {
    memset(t, 0, sizeof *t);
    tank_init(t, seed);
    tank_make_fish(t, 0, preset, 0.5f, bold, STAGE_ADULT);
    t->n_fish = 1;
    t->fish[0].lazy = lazy;
    t->fish[0].hunger = 5;
    t->fish[0].energy = 8;
    t->fish[0].stress = 0;
    t->trickle_off = true;
    t->light_override = true;
    t->light_on = true;
}

static void test_rest_arrival_and_turn_continuity(void) {
    for (int variety = 0; variety < FISH_VARIETY_COUNT; variety++) {
        tank_t t;
        one_fish(&t, 712, 0, 0.55f, 0.45f);
        fish_t *f = &t.fish[0];
        f->variety = (uint8_t)variety;
        f->goal.id = GOAL_REST;
        f->goal.urgency = 3;
        tank_rest_spot(&t, 0, &f->x, &f->y);
        f->heading = 0;
        tank_fish_face(f);
        f->speed = f->target_speed = 18;
        tank_tick(&t, 0.04f, hold_goal);
        assert(f->target_speed < 4.2f);

        f->goal.id = GOAL_EXPLORE;
        f->explore_set = true;
        f->explore_x = f->x - 130;
        f->explore_y = f->y;
        f->behind = 0;
        f->heading = 0;
        tank_fish_face(f);
        f->speed = f->target_speed = 20;
        int flips = 0;
        int8_t previous_facing = f->facing;
        float max_step = 0;
        for (int i = 0; i < 250; i++) {
            float x = f->x, y = f->y;
            tank_tick(&t, 0.04f, hold_goal);
            float step = hypotf(f->x - x, f->y - y);
            if (step > max_step) max_step = step;
            if (f->facing != previous_facing) flips++;
            previous_facing = f->facing;
        }
        assert(flips >= 1 && flips <= 2);
        assert(max_step < 2.0f);
    }
}

static void test_rest_step_sizes(void) {
    tank_t fine, coarse;
    one_fish(&fine, 713, 0, 0.55f, 0.45f);
    one_fish(&coarse, 713, 0, 0.55f, 0.45f);
    tank_t *runs[] = {&fine, &coarse};
    const float dt[] = {0.033f, 0.1f};
    for (int run = 0; run < 2; run++) {
        tank_t *t = runs[run];
        fish_t *f = &t->fish[0];
        f->goal.id = GOAL_REST;
        f->goal.urgency = 3;
        tank_rest_spot(t, 0, &f->x, &f->y);
        f->heading = 0;
        tank_fish_face(f);
        f->speed = f->target_speed = 18;
        float max_step = 0;
        int frames = run == 0 ? 15 : 5;
        for (int i = 0; i < frames; i++) {
            float x = f->x, y = f->y;
            tank_tick(t, dt[run], hold_goal);
            float step = hypotf(f->x - x, f->y - y);
            if (step > max_step) max_step = step;
            assert(isfinite(f->speed) && f->speed >= 0);
            assert(isfinite(f->target_speed) && f->target_speed >= 0);
        }
        assert(max_step < 2.0f);
        assert(f->target_speed >= 1.0f && f->target_speed <= 4.2f);
    }
    assert(fabsf(fine.fish[0].speed - coarse.fish[0].speed) < 0.75f);
}

static void test_food_dive_and_personality(void) {
    tank_t near, bold, gentle;
    one_fish(&near, 19, 1, 0.5f, 0.4f);
    fish_t *f = &near.fish[0];
    f->goal.id = GOAL_SEEK_FOOD;
    f->goal.urgency = 3;
    f->x = 150;
    f->y = TANK_SAND_TOP - tank_fish_clearance(f) - 40;
    f->heading = 0;
    tank_fish_face(f);
    near.food[0].alive = true;
    near.food[0].x = f->x + 40;
    near.food[0].y = TANK_SAND_TOP;
    near.food[0].age = 0;
    near.food[0].floor_s = 1;
    tank_tick(&near, 0.5f, hold_goal);
    assert(f->heading > 0.57f); /* close approach bends into the pellet */

    one_fish(&bold, 29, 2, 0.9f, 0.0f);
    one_fish(&gentle, 29, 2, 0.1f, 1.0f);
    tank_t *pair[] = {&bold, &gentle};
    for (int i = 0; i < 2; i++) {
        tank_t *t = pair[i];
        fish_t *fish = &t->fish[0];
        fish->goal.id = GOAL_SEEK_FOOD;
        fish->goal.urgency = 3;
        fish->x = 90;
        fish->y = 90;
        fish->heading = 0;
        tank_fish_face(fish);
        t->food[0].alive = true;
        t->food[0].x = 185;
        t->food[0].y = 100;
        t->food[0].floor_s = 1;
    }
    tank_tick(&bold, 0.04f, hold_goal);
    tank_tick(&gentle, 0.04f, hold_goal);
    assert(bold.fish[0].target_speed > gentle.fish[0].target_speed + 8);
}

static void test_sand_target_offsets(void) {
    const float sizes[] = {0.35f, 0.7f, 1.3f};
    const float offsets[] = {-0.51f, -0.49f, -0.0001f, 0, 0.0001f, 0.49f, 0.51f};
    for (int variety = 0; variety < FISH_VARIETY_COUNT; variety++)
        for (int stage = STAGE_FRY; stage <= STAGE_ELDER; stage++)
            for (unsigned offset = 0; offset < sizeof offsets / sizeof offsets[0]; offset++) {
                tank_t t;
                one_fish(&t, 197, 0, 0.5f, 0.4f);
                fish_t *f = &t.fish[0];
                f->variety = (uint8_t)variety;
                f->stage = (stage_t)stage;
                f->size = sizes[stage == STAGE_FRY ? 0 : stage == STAGE_JUV ? 1 : 2];
                f->heading = 0;
                f->speed = f->target_speed = 0;
                tank_fish_face(f);
                f->x = TANK_W * 0.5f;
                f->y = TANK_SAND_TOP - tank_fish_clearance(f) + offsets[offset];
                f->hunger = 8;
                f->goal.id = GOAL_SEEK_FOOD;
                t.food[0].alive = true;
                t.food[0].x = f->x;
                t.food[0].y = FOOD_FLOOR_Y;
                t.food[0].floor_s = 1;
                int eaten = f->eaten;
                tank_tick(&t, 0.01f, NULL);
                assert(!t.food[0].alive && f->eaten == eaten + 1);
                assert(t.food[0].y == FOOD_FLOOR_Y);
                assert(isfinite(f->speed) && f->speed >= 0);
                assert(f->y + tank_fish_clearance(f) <= TANK_SAND_TOP + 0.001f);
            }
}

static void test_close_dive_threshold(void) {
    const float offsets[] = {-0.51f, -0.49f, -0.0001f, 0, 0.0001f, 0.49f, 0.51f, 20};
    for (unsigned i = 0; i < sizeof offsets / sizeof offsets[0]; i++) {
        tank_t t;
        one_fish(&t, 198, 0, 0.5f, 0.4f);
        fish_t *f = &t.fish[0];
        f->heading = 0;
        f->speed = f->target_speed = 0;
        tank_fish_face(f);
        f->x = TANK_W * 0.5f;
        float target_y = TANK_SAND_TOP - tank_fish_clearance(f);
        f->y = target_y - offsets[i];
        f->goal.id = GOAL_SEEK_FOOD;
        f->turn_rate = 100;
        t.food[0].alive = true;
        t.food[0].x = f->x + 20;
        t.food[0].y = FOOD_FLOOR_Y;
        t.food[0].floor_s = 1;
        float dy = target_y - f->y;
        float vertical = 0.72f;
        if (dy > 0.5f)
            vertical += (1 - 0.72f) * (1 - hypotf(20, dy) / 80);
        float expected = atan2f(dy * vertical, 20);
        tank_tick(&t, 0.01f, NULL);
        /* The glass can reflect a small dive after a pose-clearance change. */
        if (fabsf(fabsf(f->heading) - fabsf(expected)) >= 0.000001f)
            fprintf(stderr, "close dive %dx%d offset=%g expected=%g actual=%g\n",
                    TANK_W, TANK_H, offsets[i], expected, f->heading);
        assert(fabsf(fabsf(f->heading) - fabsf(expected)) < 0.000001f);
        if (offsets[i] == 20) assert(fabsf(f->heading) > 0.7f);
    }

    tank_t t;
    one_fish(&t, 199, 0, 0.5f, 0.4f);
    fish_t *f = &t.fish[0];
    f->heading = 0;
    f->speed = f->target_speed = 0;
    tank_fish_face(f);
    f->x = 100;
    f->y = 70;
    f->goal.id = GOAL_SEEK_FOOD;
    f->turn_rate = 100;
    t.food[0].alive = true;
    t.food[0].x = f->x + 20;
    t.food[0].y = f->y + 40;
    tank_tick(&t, 0.01f, NULL);
    assert(f->heading > 0.9f && t.food[0].y < TANK_SAND_TOP);
}

static void test_tap_and_enrichment_reactions(void) {
    tank_t shy, bold, plain, enriched;
    one_fish(&shy, 41, 3, 0.1f, 0.4f);
    one_fish(&bold, 41, 3, 0.9f, 0.4f);
    shy.fish[0].x = bold.fish[0].x = 120;
    shy.fish[0].y = bold.fish[0].y = 95;
    for (int i = 0; i < 3; i++) {
        tank_touch_tap(&shy, 120, 95);
        tank_touch_tap(&bold, 120, 95);
    }
    assert(shy.startled && bold.startled);
    assert(shy.fish[0].stress > bold.fish[0].stress);
    assert(shy.fish[0].trust < bold.fish[0].trust);

    one_fish(&plain, 51, 4, 0.5f, 0.3f);
    one_fish(&enriched, 51, 4, 0.5f, 0.3f);
    plain.fish[0].goal.id = enriched.fish[0].goal.id = GOAL_EXPLORE;
    plain.fish[0].curiosity = enriched.fish[0].curiosity = 5;
    enriched.aquariums.stat_upgrades |= STAT_ENRICHMENT;
    tank_tick(&plain, 1.0f, hold_goal);
    tank_tick(&enriched, 1.0f, hold_goal);
    assert(enriched.fish[0].curiosity > plain.fish[0].curiosity);
    assert(fabsf((enriched.fish[0].curiosity - plain.fish[0].curiosity) - 0.02f) < 0.002f);
}

static void test_species_edges_and_render_purity(void) {
    tank_t t;
    uint16_t framebuffer[TANK_W * TANK_H];
    for (int variety = 0; variety < FISH_VARIETY_COUNT; variety++) {
        one_fish(&t, (uint32_t)(100 + variety), variety % tank_roster_count(), 0.5f, 0.5f);
        fish_t *f = &t.fish[0];
        f->variety = (uint8_t)variety;
        f->x = 16 * f->size;
        f->y = tank_fish_top_clearance(f) + 1;
        f->heading = 3.8f;
        tank_fish_face(f);
        f->speed = f->target_speed = 45;
        f->goal.id = GOAL_EXPLORE;
        for (int i = 0; i < 80; i++) {
            tank_tick(&t, 0.04f, hold_goal);
            assert(f->x >= 15 * f->size && f->x <= TANK_W - 15 * f->size);
            assert(f->y >= tank_fish_top_clearance(f) - 0.01f);
            assert(f->y <= TANK_SAND_TOP - tank_fish_clearance(f) + 0.01f);
        }
    }
    one_fish(&t, 601, 0, 0.6f, 0.35f);
    tank_t before = t;
    render_tank(&t, framebuffer, TANK_W);
    assert(memcmp(&before, &t, sizeof t) == 0);
}

int main(int argc, char **argv) {
    assert(argc == 2);
    FILE *icons = fopen(argv[1], "rb");
    assert(icons);
    icon_bytes = fread(icon_bank, 1, sizeof icon_bank, icons);
    assert(!ferror(icons) && fclose(icons) == 0);
    for (int enlarged = 0; enlarged <= 1; enlarged++) {
        tank_resolution(enlarged != 0);
        test_rest_arrival_and_turn_continuity();
        test_rest_step_sizes();
        test_food_dive_and_personality();
        test_sand_target_offsets();
        test_close_dive_threshold();
        test_tap_and_enrichment_reactions();
        test_species_edges_and_render_purity();
    }
    puts("PASS: normal/enlarged resolutions, rest arrival/step sizes, smooth committed turns, food dives/personality, sand target offsets/dive threshold, tap sensitivity, enrichment, ten-variety edges, render purity");
    return 0;
}
