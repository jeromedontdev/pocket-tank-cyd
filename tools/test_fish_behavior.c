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

static void test_edge_steering_continuity(void) {
    const float steps[] = {0.033f, 0.067f, 0.1f};
    for (int variety = 0; variety < FISH_VARIETY_COUNT; variety++)
        for (int edge = 0; edge < 4; edge++)
            for (unsigned step = 0; step < sizeof steps / sizeof steps[0]; step++) {
                tank_t outside, inside;
                one_fish(&outside, 712, 1, 0.55f, 0.45f);
                fish_t *f = &outside.fish[0];
                f->variety = (uint8_t)variety;
                f->x = TANK_W * 0.5f;
                f->y = TANK_H * 0.5f;
                f->heading = edge == 0 ? 3.14159265f : 0;
                tank_fish_face(f);
                f->speed = f->target_speed = 0;
                f->wander = 0;
                f->goal.id = GOAL_EXPLORE;
                f->explore_set = true;
                float side = 15 * f->size + 6;
                if (edge == 0) f->x = side - 0.001f;
                if (edge == 1) f->x = TANK_W - side + 0.001f;
                if (edge == 2) f->y = 40 - 0.001f;
                if (edge == 3) f->y = TANK_SAND_TOP - tank_fish_clearance(f) - 4 + 0.001f;
                f->explore_x = edge == 0 ? 0 : TANK_W;
                f->explore_y = edge == 2 ? 0 : edge == 3 ? TANK_H : f->y - 12;
                inside = outside;
                if (edge == 0) inside.fish[0].x += 0.002f;
                if (edge == 1) inside.fish[0].x -= 0.002f;
                if (edge == 2) inside.fish[0].y += 0.002f;
                if (edge == 3) inside.fish[0].y -= 0.002f;
                tank_tick(&outside, steps[step], hold_goal);
                tank_tick(&inside, steps[step], hold_goal);
                assert(fabsf(tank_fish_pitch(f) - tank_fish_pitch(&inside.fish[0])) < 0.001f);
                assert(fabsf(f->y - inside.fish[0].y) < 0.01f);
            }
}

static void test_surface_cruising_without_seesaw(void) {
    for (int preset = 0; preset < tank_roster_count(); preset++) {
        tank_t t;
        one_fish(&t, 712, preset, 0.55f, 0.45f);
        fish_t *f = &t.fish[0];
        f->x = TANK_W * 0.35f;
        f->y = 40;
        f->heading = 0;
        tank_fish_face(f);
        f->speed = f->target_speed = 18;
        f->goal.id = GOAL_EXPLORE;
        f->explore_set = true;
        f->explore_x = TANK_W * 0.8f;
        f->explore_y = f->y;
        float previous = tank_fish_pitch(f), delta = 0;
        int rapid_reversals = 0;
        for (int i = 0; i < 250; i++) {
            tank_tick(&t, 0.04f, hold_goal);
            float pitch = tank_fish_pitch(f), next_delta = pitch - previous;
            /* A normal bend may reverse once; a seesaw reverses repeatedly
             * on consecutive frames by more than ~0.6 degrees. */
            if (next_delta * delta < 0 && fabsf(next_delta) > 0.01f && fabsf(delta) > 0.01f)
                rapid_reversals++;
            else rapid_reversals = 0;
            assert(rapid_reversals < 3);
            previous = pitch;
            delta = next_delta;
        }
    }
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
        /* already turning hard its way: the eased turn lands exactly on the target */
        f->turn_vel = expected > 0 ? 1000.0f : expected < 0 ? -1000.0f : 0;
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
    f->turn_vel = 1000;
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

/* Fluid motion (2026-11): the tail beat's phase advances at its own frequency
 * (no jump when the speed changes), the heading's turn rate ramps instead of
 * snapping between full lock and none, and a change of pace is sprung. */
static void test_fluid_motion(void) {
    const float dt = 0.04f;
    for (int preset = 0; preset < tank_roster_count(); preset++) {
        tank_t t;
        one_fish(&t, 900 + preset, preset, 0.55f, 0.45f);
        fish_t *f = &t.fish[0];
        f->x = TANK_W * 0.5f;
        f->y = TANK_H * 0.4f;
        f->heading = 0;
        tank_fish_face(f);
        f->speed = f->target_speed = 18;
        f->goal.id = GOAL_EXPLORE;
        f->explore_set = true;
        float prev_step = 0, prev_dv = 0, max_turn_jerk = 0, max_speed_jerk = 0, top_speed = 0, top_target = 18;
        bool have_step = false;
        for (int i = 0; i < 750; i++) {
            if (i % 75 == 0) {
                /* a fresh destination every 3 s: up, down, across and back */
                f->explore_set = true;
                f->explore_x = (i / 75) % 2 ? TANK_W * 0.15f : TANK_W * 0.85f;
                f->explore_y = TANK_H * ((i / 75) % 3 == 0 ? 0.25f : (i / 75) % 3 == 1 ? 0.55f : 0.4f);
                f->goal.id = GOAL_EXPLORE;
            }
            float heading = f->heading, phase = f->swim_phase, speed = f->speed;
            int8_t facing = f->facing;
            tank_tick(&t, dt, hold_goal);
            float dphase = f->swim_phase - phase;
            if (dphase < 0) dphase += 6.2831853f;
            assert(f->swim_phase >= 0 && f->swim_phase < 6.2831853f);
            assert(fabsf(dphase - dt * (8 + f->speed * 0.055f)) < 0.001f);
            assert(isfinite(f->speed) && f->speed >= 0 && isfinite(f->turn_vel));
            top_speed = fmaxf(top_speed, f->speed);
            top_target = fmaxf(top_target, f->target_speed);
            float step = atan2f(sinf(f->heading - heading), cosf(f->heading - heading));
            bool mirrored = f->facing != facing || fabsf(step) > f->turn_rate * dt * 1.01f;
            if (!mirrored && have_step) {
                float jerk = fabsf(step - prev_step) / (f->turn_rate * dt);
                if (jerk > max_turn_jerk) max_turn_jerk = jerk;
            }
            have_step = !mirrored;
            prev_step = step;
            float dv = f->speed - speed;
            if (i > 0 && fabsf(dv - prev_dv) > max_speed_jerk) max_speed_jerk = fabsf(dv - prev_dv);
            prev_dv = dv;
        }
        /* the old bang-bang turn and first-order pace measured ~1.0 and ~0.6
         * here; the eased ones ~0.45 and ~0.2 */
        assert(max_turn_jerk < 0.7f);
        assert(max_speed_jerk < 0.35f);
        assert(top_speed <= top_target + 0.001f);
    }
}
static void test_reef_rhythm(void) {
    tank_t t, control;
    one_fish(&t, 981, 0, .5f, .5f);
    t.fish[0].goal.id = GOAL_EXPLORE;
    t.fish[0].x = TANK_W * .5f; t.fish[0].y = TANK_H * .4f;
    t.sd_unlocks |= SD_ITEM_SNAIL | SD_ITEM_URCHIN | SD_ITEM_SEAHORSE;
    assert(tank_reef_shelters(&t) == 0 && tank_reef_open_lane(&t) == 1);
    t.inventory.extra[0] = (inventory_extra_t){.item = SD_DECOR_FIRST + 1, .x = TANK_W * .8f};
    assert(tank_reef_shelters(&t) == 1 && tank_reef_open_lane(&t) < 1);
    assert(tank_reef_calm_near(&t, TANK_W * .8f, TANK_BOT - 40));
    memset(&t.inventory.extra[0], 0, sizeof t.inventory.extra[0]);
    assert(!tank_reef_calm_near(&t, t.reef_x, TANK_BOT - 40));
    assert(tank_nursery_bed(&t) >= 0);
    assert(!tank_reef_current(&t, 20, 50, 22, 50));
    assert(!tank_reef_current(&t, 20, 50, 30, TANK_BOT - 20));
    assert(!tank_reef_current(&t, NAN, 50, 150, 50));
    assert(!tank_reef_current(&t, -100, 50, TANK_W - 20, 50));
    assert(!tank_reef_current(&t, 20, 50, TANK_W + 100, 50));
    t.ui_cover = true;
    assert(!tank_reef_current(&t, 20, 50, TANK_W - 20, 50));
    t.ui_cover = false;
    t.night = true;
    assert(!tank_reef_current(&t, 20, 50, TANK_W - 20, 50));
    t.night = false;
    float y = t.fish[0].y;
    control = t;
    assert(!tank_reef_current(&t, 20, y, TANK_W - 20, y));
    assert(!memcmp(&t, &control, sizeof t));
    assert(!tank_reef_current(&t, 20, y, TANK_W - 20, y));
    for (int i = 0; i < 10; i++) tank_tick(&t, .04f, hold_goal);
    assert(t.reef_visits == 0);
    assert(t.reef_flow_s == 0);
    t.fish[0].goal.id = GOAL_VISIT_BUBBLES;
    t.fish[0].at_bubbles = true;
    tank_tick(&t, .04f, hold_goal);
    assert(t.fish[0].goal.id == GOAL_EXPLORE && !t.fish[0].at_bubbles);
    float sword[VEG_FRONDS_MAX];
    t.sd_unlocks |= SD_ITEM_PLANT | SD_ITEM_CASTLE;
    tank_veg_sync(&t);
    memcpy(sword, t.veg_h[3], sizeof sword);
    assert(tank_reef_shelters(&t) >= 2 && tank_reef_open_lane(&t) < 1);
    assert(tank_reef_calm_near(&t, tank_decor_x(&t, 2), TANK_BOT - 40));
    for (int b = 0; b < VEG_BEDS; b++) {
        int n = -1; tank_veg_bed(&t, b, NULL, NULL, NULL, &n);
        assert(n == 0 && tank_veg_frond(&t, b, 0, NULL) == 0);
    }
    memset(t.algae, 180, sizeof t.algae);
    tank_grow_algae(&t, 1000000);
    assert(tank_algae_cover(&t) == 0 && !tank_shrimp_refusing(&t));
    control = t;
    int32_t cleaned = t.cells_cleaned, grazed = t.snail_grazed;
    float trim = t.trim_px, chewed = t.urchin_grazed_px;
    for (int i = 0; i < 9000; i++) {
        if (i % 800 == 0) {
            t.fish[0].hunger = 3; t.fish[0].energy = 8;
            (void)tank_reef_current(&t, 20, y, TANK_W - 20, y);
        }
        tank_tick(&t, .04f, hold_goal);
        assert(isfinite(t.fish[0].x) && isfinite(t.fish[0].heading));
        assert(t.reef_flow_s >= 0 && t.reef_flow_cooldown >= 0);
    }
    assert(!memcmp(sword, t.veg_h[3], sizeof sword));
    assert(!memcmp(control.algae, t.algae, sizeof t.algae));
    assert(t.cells_cleaned == cleaned && t.trim_px == trim &&
        t.snail_grazed == grazed && t.urchin_grazed_px == chewed);
    assert(!tank_urchin_chewing(&t) && t.snail_cell == -1);
    for (int b = 0; b < VEG_BEDS; b++) assert(t.veg_growth[b] == 0);
    tank_tick_sleep(&t, 86400 * 14);
    assert(!memcmp(sword, t.veg_h[3], sizeof sword));
    assert(!t.reef_flow_s && !t.reef_flow_cooldown);
    t.night = false;
    t.reef_currents = UINT32_MAX;
    assert(!tank_reef_current(&t, 20, 50, TANK_W - 20, 50));
    assert(t.reef_currents == UINT32_MAX);
    tank_set_tool(&t, TOOL_SCISSORS);
    assert(t.tool == TOOL_HAND);
    puts("PASS: retired currents cannot mutate state; owned foliage/shelter, 9000-frame ornamental helpers and two-week no-upkeep sleep");
}

static void test_seahorse_open_water(void) {
    tank_t t;
    one_fish(&t, 993, 0, .5f, .5f);
    t.sd_unlocks = SD_ITEM_SEAHORSE;
    tank_seahorse_place(&t);
    assert(t.sh_mode == SH_DRIFT && t.sh_akind == SH_ANCHOR_NONE);
    float lo = t.sh_x, hi = t.sh_x;
    for (int i = 0; i < 9000; i++) {
        tank_tick(&t, .04f, hold_goal);
        assert(t.sh_mode == SH_DRIFT && t.sh_akind == SH_ANCHOR_NONE);
        assert(isfinite(t.sh_x) && isfinite(t.sh_y));
        assert(t.sh_y >= 65 && t.sh_y <= TANK_BOT - 45);
        lo = fminf(lo, t.sh_x); hi = fmaxf(hi, t.sh_x);
    }
    assert(hi - lo > TANK_W * .2f);
    t.light_override = true; t.light_on = false; t.night = true;
    float x = t.sh_x, y = t.sh_y;
    for (int i = 0; i < 100; i++) tank_tick(&t, .04f, hold_goal);
    assert(t.sh_x == x && t.sh_y == y);
    t.light_on = true; t.night = false;
    t.sd_unlocks |= SD_ITEM_PLANT;
    tank_veg_sync(&t);
    for (int i = 0; i < 3000 && t.sh_mode != SH_PERCH; i++)
        tank_tick(&t, .04f, hold_goal);
    assert(t.sh_mode == SH_PERCH && t.sh_akind == SH_ANCHOR_BLADE && t.sh_bed == 3);
    t.sd_unlocks &= ~SD_ITEM_PLANT;
    for (int i = 0; i < 3000; i++) tank_tick(&t, .04f, hold_goal);
    assert(t.sh_mode == SH_DRIFT && t.sh_akind == SH_ANCHOR_NONE);
    assert(t.sh_y < TANK_BOT - 45);
    t.hold_active = true; t.hold_time = 3;
    t.hold_x = TANK_W * .5f; t.hold_y = 90;
    tank_tick(&t, .04f, hold_goal);
    assert(t.sh_mode == SH_HITCH);
    puts("PASS: seahorse cruises upright without anchors, rests in water at night, finds sword, recovers after removal and prioritizes a held finger");
}

static void test_variety_pair_guard(void) {
    tank_t t, before;
    tank_init(&t, 984);
    tank_new_population(&t);
    assert(t.n_fish == 2);
    t.fish[0].variety = FISH_SHARK;
    t.fish[1].variety = FISH_CLASSIC;
    before = t;
    assert(tank_add_fish(&t, 0, 1) == -1 && !memcmp(&t, &before, sizeof t));
    assert(tank_add_fish(&t, 0, 0) == -1 && !memcmp(&t, &before, sizeof t));
    assert(tank_add_fish(&t, -1, 1) == -1 && !memcmp(&t, &before, sizeof t));
    assert(tank_add_fish(&t, 0, t.n_fish) == -1 && !memcmp(&t, &before, sizeof t));
    t.fish[1].variety = FISH_SHARK;
    int slot = tank_add_fish(&t, 0, 1);
    assert(slot == 2 && t.fish[slot].variety == FISH_SHARK);
    int mutated = 0;
    for (uint32_t seed = 1; seed <= 512; seed++) {
        tank_init(&t, seed);
        tank_new_population(&t);
        t.story.version = 1;
        t.fish[0].variety = t.fish[1].variety = FISH_SHARK;
        t.fish[1].color = t.fish[0].color;
        t.story.active[0].color2 = t.story.active[1].color2 = 0;
        uint32_t parent_color = t.fish[0].color;
        slot = tank_add_fish(&t, 0, 1);
        assert(slot == 2 && t.fish[slot].variety == FISH_SHARK);
        if (t.story.active[slot].mutation & GENE_MUTANT_COLOR) {
            assert(t.fish[slot].color != parent_color);
            mutated++;
        } else assert(t.fish[slot].color == parent_color);
    }
    assert(mutated >= 16 && mutated <= 64);
    puts("PASS: direct spawning requires two distinct valid same-variety parents and rejected pairs leave state/RNG untouched");
    puts("PASS: color mutations produce new inherited colors without changing variety or requiring free color editing");
}

int main(int argc, char **argv) {
    assert(argc == 2);
    FILE *icons = fopen(argv[1], "rb");
    assert(icons);
    icon_bytes = fread(icon_bank, 1, sizeof icon_bank, icons);
    assert(!ferror(icons) && fclose(icons) == 0);
    for (int enlarged = 0; enlarged <= 1; enlarged++) {
        tank_resolution(enlarged != 0);
        test_variety_pair_guard();
        test_seahorse_open_water();
        test_rest_arrival_and_turn_continuity();
        test_rest_step_sizes();
        test_edge_steering_continuity();
        test_surface_cruising_without_seesaw();
        test_fluid_motion();
        test_food_dive_and_personality();
        test_sand_target_offsets();
        test_close_dive_threshold();
        test_tap_and_enrichment_reactions();
        test_species_edges_and_render_purity();
        test_reef_rhythm();
    }
    puts("PASS: normal/enlarged resolutions, rest arrival/step sizes, smooth committed turns, continuous four-edge steering and surface cruising without seesaw, continuous tail beat with eased turns and sprung pace, food dives/personality, sand target offsets/dive threshold, tap sensitivity, enrichment, ten-variety edges, render purity");
    return 0;
}
