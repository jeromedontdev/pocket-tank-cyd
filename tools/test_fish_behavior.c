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
        float yaw_lo = f->yaw, yaw_hi = f->yaw;
        for (int i = 0; i < 250; i++) {
            float x = f->x, y = f->y;
            tank_tick(&t, 0.04f, hold_goal);
            float step = hypotf(f->x - x, f->y - y);
            if (step > max_step) max_step = step;
            if (f->facing != previous_facing) flips++;
            previous_facing = f->facing;
            if (f->yaw < yaw_lo) yaw_lo = f->yaw;
            if (f->yaw > yaw_hi) yaw_hi = f->yaw;
        }
        if (variety == FISH_JELLYFISH) {
            /* A jellyfish is radially symmetric: it has no side to present, so
             * it never flips its facing and never rolls its bell. Running it
             * through the powered-fish turn machinery flipped it every ~3 s
             * (17 flips in 48 s measured) and swung the yaw with it, which the
             * keeper read as the bell rocking left and right - and, at the
             * glass, as walking. */
            assert(flips == 0);
            assert(yaw_hi - yaw_lo < 0.001f);
            assert(fabsf(f->yaw - 1.0f) < 0.001f);
        } else {
            assert(flips >= 1 && flips <= 2);
        }
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

    /* The scare is a BOLT, not a porpoise (2026-11). The keeper saw a tapped
     * fish "jump out of the water like a dolphin": the escape was blended 85%
     * with the advisor's goal, so a goal pointing at the floor won the
     * remainder, the fish dived, the hard clamp mirrored its heading and the
     * flee steer re-aimed it at the floor. Measured on the old steer: 8
     * vertical reversals, 13 heading reversals and a 19.8 px single-frame
     * teleport in 10 s of fleeing. Now the escape owns the way and is damped
     * toward the wall's tangent, so the fish runs a line. */
    {
        tank_t t;
        one_fish(&t, 41, 3, 0.5f, 0.4f);
        fish_t *f = &t.fish[0];
        f->x = 120; f->y = 95;
        f->heading = 0; f->speed = f->target_speed = 20;
        f->goal.id = GOAL_EXPLORE;
        tank_fish_face(f);
        for (int i = 0; i < 3; i++) tank_touch_tap(&t, 120, 95);
        assert(t.startled);
        float px = f->x, py = f->y, ldx = 0, ldy = 0, max_step = 0, max_dist = 0;
        int xrev = 0, yrev = 0;
        for (int i = 0; i < 250; i++) {          /* 10 s at 40 ms */
            tank_tick(&t, 0.04f, hold_goal);
            float dx = f->x - px, dy = f->y - py;
            float step = hypotf(dx, dy);
            if (step > max_step) max_step = step;
            float d = tank_dist(f->x, f->y, 120, 95);
            if (d > max_dist) max_dist = d;
            if (dx * ldx < 0 && fabsf(dx) > 0.002f && fabsf(ldx) > 0.002f) xrev++;
            if (dy * ldy < 0 && fabsf(dy) > 0.002f && fabsf(ldy) > 0.002f) yrev++;
            if (fabsf(dx) > 0.002f) ldx = dx;
            if (fabsf(dy) > 0.002f) ldy = dy;
            px = f->x; py = f->y;
        }
        assert(yrev <= 4);
        assert(xrev <= 5);
        assert(max_step < 8.0f);
        /* and it actually bolted away from the tap before the spook wore off */
        assert(max_dist > 60);
    }

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

/* The tail breathes: the caudal web's fan is `spread + tail * 8`, and the
 * breathing term swings `tail` far past the beat's own range, so the fan
 * crosses zero - the web's top and bottom vertices meet and the fin collapses
 * to a sliver. This drives the REAL renderer through a whole breath cycle and
 * measures the drawn web, so removing the floor fails here rather than
 * passing on a restated formula. */
static void test_tail_breathing_fan(void) {
    static uint16_t base[320 * 480], frame[320 * 480];   /* the largest framebuffer */
    for (int variety = 0; variety < FISH_VARIETY_COUNT; variety++) {
        if (variety == FISH_JELLYFISH || variety == FISH_STINGRAY) continue;   /* no caudal web */
        for (int grown = 0; grown < 2; grown++) {
            tank_t t;
            memset(&t, 0, sizeof t);
            tank_init(&t, 4242);
            tank_make_fish(&t, 0, 0, 0.5f, 0.5f, STAGE_ADULT);
            t.n_fish = 1;
            fish_t *f = &t.fish[0];
            f->variety = (uint8_t)variety;
            f->size = grown ? 1.23f : 1.0f;
            f->x = TANK_W * 0.5f; f->y = TANK_H * 0.45f;
            f->heading = 0; f->speed = f->target_speed = 40;
            f->hunger = 4; f->stress = 0; f->energy = 8;
            f->goal.id = GOAL_EXPLORE;
            tank_fish_face(f);
            /* the same tank with no fish: water and scenery are not fish pixels */
            tank_t empty = t; empty.n_fish = 0;
            render_tank(&empty, base, TANK_W);
            /* The caudal web sits behind the body: the columns left of
             * x - 9 are the fin alone (the body starts around x - 7). */
            int up = 0, down = 0, span_lo = 9999, span_hi = -1;
            /* The breath is sin(wander * 1.65), so one breath cycle needs
             * wander to sweep 2*pi/1.65 ~= 3.81. 320 samples at 0.0982 covers
             * ~8.25 cycles: enough to land on both the top and the bottom of
             * the breath. A shorter sweep misses the bottom entirely. */
            for (int k = 0; k < 320; k++) {
                f->wander = k * 0.0982f;
                f->swim_phase = 0;
                memset(frame, 0, sizeof frame);
                render_tank(&t, frame, TANK_W);
                int k_up = 0, k_down = 0;
                for (int y = 0; y < TANK_H; y++)
                    for (int x = 0; x <= (int)f->x - 9; x++)
                        if (frame[y * TANK_W + x] != base[y * TANK_W + x]) {
                            int d = y - (int)f->y;
                            if (d < 0 && -d > k_up) k_up = -d;
                            if (d > 0 && d > k_down) k_down = d;
                        }
                if (k_up > up) up = k_up;
                if (k_down > down) down = k_down;
                int span = k_up + k_down;
                if (span < span_lo) span_lo = span;
                if (span > span_hi) span_hi = span;
            }
            /* The web is drawn on BOTH sides of the centre line, and it never
             * collapses to a sliver at the bottom of the breath. */
            assert(up > 2 && down > 2);
            assert(span_lo >= 3);
            /* And it breathes: the web opens and closes over the cycle. The
             * shark's heterocercal web sweeps rather than fans (both of its
             * vertices shift together), so its span is steady by design. */
            if (variety != FISH_SHARK) assert(span_hi - span_lo >= 5);
        }
    }
}
/* Jellyfish float (2026-11): the keeper asked for more bobbing. The old bob
 * was 0.1 px/s - a tenth of a pixel a second, which read as a still image.
 * The drift is now a real rise and sink. It must stay a zero-mean velocity
 * (so the animal never climbs to the surface and sticks there) and it must
 * actually move the animal several pixels over a breath. */
/* Explore must reach the whole tank (2026-11): the keeper reported the school
 * "preferring the bottom and middle". The explore band used absolute 42/40 px
 * insets, which were tuned on the 240 px display; at 180 px they left row 0 a
 * two-pixel band, so every explore target landed in the lower half. With rest
 * at the floor and the reef orbit at the middle, the upper half was
 * unreachable. This samples the real target function at both scales and
 * requires the upper half to be used. */
static void test_explore_reaches_the_upper_half(void) {
    for (int compact = 0; compact < 2; compact++) {
        tank_t t;
        one_fish(&t, 4242, 0, 0.5f, 0.4f);
        tank_resolution(compact != 0);
        fish_t *f = &t.fish[0];
        f->goal.id = GOAL_EXPLORE;
        f->explore_set = false;
        f->hunger = 4; f->energy = 8; f->stress = 0; f->curiosity = 8;
        int upper = 0, lower = 0, top_band = 0;
        float lo = TANK_H, hi = 0;
        for (int i = 0; i < 400; i++) {
            f->explore_set = false;                 /* force a fresh pick */
            tank_tick(&t, 0.04f, hold_goal);
            float y = f->explore_y;
            if (y < lo) lo = y;
            if (y > hi) hi = y;
            if (y < TANK_BOT / 2.0f) upper++; else lower++;
            /* the top quarter of the water column, where the old absolute
               insets left only a two-pixel band at 180 px */
            if (y < TANK_BOT * 0.25f) top_band++;
        }
        /* both halves get used, and the picks span most of the water column */
        fprintf(stderr, "  explore %s: upper=%d lower=%d top=%d span=%.1f lo=%.1f hi=%.1f (TANK_BOT=%.0f)\n",
                compact ? "240" : "320", upper, lower, top_band, hi - lo, lo, hi, (double)TANK_BOT);
        assert(upper > 40 && lower > 40);
        /* The picks must span most of the REACHABLE water column. The target
         * clamp is 32..157 of 180 px, so 69% is the physical maximum; the old
         * absolute insets reached only 42..140 (54%) and left the top of the
         * tank unused, which is the bottom-dwelling the keeper reported. */
        assert(hi - lo > TANK_BOT * 0.65f);
        /* the top quarter is a real destination, not a sliver */
        assert(top_band > 25);
        /* and the very top of the reachable water is used */
        assert(lo < TANK_BOT * 0.20f);
        /* and no pick lands above the glass or below the sand line */
        assert(lo > 0 && hi <= TANK_BOT);
    }
    tank_resolution(false);
}

static void test_jellyfish_float(void) {
    tank_t t;
    one_fish(&t, 4242, 0, 0.5f, 0.4f);
    fish_t *f = &t.fish[0];
    f->variety = FISH_JELLYFISH;
    f->x = TANK_W * 0.5f; f->y = TANK_H * 0.45f;
    f->heading = 0; f->speed = f->target_speed = 0;
    f->goal.id = GOAL_REST;
    tank_fish_face(f);
    /* hold the goal so the advisor never steers it away from the float */
    float lo = f->y, hi = f->y, prev = f->y, max_step = 0;
    int reversals = 0; float last_dy = 0;
    for (int i = 0; i < 1200; i++) {          /* 48 s at 40 ms */
        tank_tick(&t, 0.04f, hold_goal);
        if (f->y < lo) lo = f->y;
        if (f->y > hi) hi = f->y;
        float step = fabsf(f->y - prev);
        if (step > max_step) max_step = step;
        float dy = f->y - prev;
        /* a real bob reverses direction repeatedly; a one-way climb does not */
        if (dy * last_dy < 0 && fabsf(dy) > 0.002f && fabsf(last_dy) > 0.002f) reversals++;
        if (fabsf(dy) > 0.002f) last_dy = dy;
        prev = f->y;
    }
    /* it floats: several pixels of rise and sink over the window */
    assert(hi - lo >= 4.0f);
    /* and it never teleports: the bob is a velocity, so a 40 ms frame moves it
     * a fraction of a pixel, not a jump */
    assert(max_step < 1.0f);
    /* the drift is zero-mean, so the animal rises and sinks repeatedly instead
     * of climbing to the surface and staying there */
    assert(reversals >= 4);
}

/* Separation is lateral (2026-11): the keeper saw fish "constantly bobbing up
 * and down" dodging each other. The old steer squashed the vertical term to
 * 0.4 and pushed along the resulting angle, so two fish at the same height
 * shoved each other sideways and two stacked vertically shoved each other up
 * and down. Now a fish steps aside in x and keeps its own y unless the pair is
 * genuinely stacked. */
static void test_separation_is_lateral(void) {
    /* Two fish cruising the same horizontal line, one just ahead of the other.
     * The control is the SAME fish swimming the same goal with no neighbour: if
     * separation is lateral only, the pair's y trajectories stay close to the
     * solo one while their x trajectories diverge. A vertical shove would show
     * up as a y divergence. */
    tank_t pair, solo;
    one_fish(&pair, 5150, 0, 0.5f, 0.4f);
    tank_make_fish(&pair, 1, 1, 0.5f, 0.5f, STAGE_ADULT);
    pair.n_fish = 2;
    for (int i = 0; i < 2; i++) {
        fish_t *f = &pair.fish[i];
        f->variety = FISH_CLASSIC;
        f->goal.id = GOAL_EXPLORE;
        f->explore_set = true;
        f->explore_x = TANK_W * 0.85f;
        f->explore_y = TANK_H * 0.45f;
        f->speed = f->target_speed = 18;
        f->hunger = 5; f->energy = 8; f->stress = 0;
        f->x = TANK_W * 0.35f + i * 22; f->y = TANK_H * 0.45f;
        f->heading = 0;
        tank_fish_face(f);
    }
    solo = pair;
    solo.n_fish = 1;
    float max_dx = 0, prev_y = pair.fish[0].y, last_dy = 0;
    int reversals = 0;
    for (int i = 0; i < 60; i++) {
        tank_tick(&pair, 0.04f, hold_goal);
        tank_tick(&solo, 0.04f, hold_goal);
        float dx = fabsf(pair.fish[0].x - solo.fish[0].x);
        if (dx > max_dx) max_dx = dx;
        /* "constant up and down" is repeated y reversals, not one correction */
        float dy = pair.fish[0].y - prev_y;
        if (dy * last_dy < 0 && fabsf(dy) > 0.002f && fabsf(last_dy) > 0.002f) reversals++;
        if (fabsf(dy) > 0.002f) last_dy = dy;
        prev_y = pair.fish[0].y;
    }
    /* the neighbour pushed it aside in x ... */
    assert(max_dx > 4.0f);
    /* ... without porpoising up and down around it. A single correction as the
     * fish steps aside is fine; repeated reversals are the bug the keeper saw. */
    assert(reversals <= 2);
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

/* The sword plant is duplicable: the primary is bed 3 and every extra copy the
 * keeper placed is a bed of its own, with its own x, depth and sway phase. */
static void test_sword_plant_copies(void) {
    tank_t t;
    one_fish(&t, 995, 0, .5f, .5f);
    assert(tank_veg_beds(&t) == VEG_BEDS_MAX - 1);          /* no plant yet: only the three grass beds */
    t.sd_unlocks |= SD_ITEM_PLANT;
    tank_veg_sync(&t);
    assert(tank_veg_beds(&t) == VEG_BEDS_MAX);              /* the primary alone */
    assert(tank_veg_bed_extra(&t, 3) == -1);
    assert(tank_veg_kind(&t, 3) == VEG_KIND_SWORD);
    assert(tank_veg_kind(&t, 0) == VEG_KIND_GRASS);
    float x0, x1, top; int n = 0;
    tank_veg_bed(&t, 3, &x0, &x1, &top, &n);
    assert(n == 4 && x1 > x0 && top < TANK_BOT);
    /* two extra copies, each its own bed */
    t.inventory.extra[0] = (inventory_extra_t){ .item = 1, .z = DECOR_Z_BACK, .rank = DECOR_SLOTS, .x = 60 };
    t.inventory.extra[1] = (inventory_extra_t){ .item = 1, .z = DECOR_Z_FRONT, .rank = DECOR_SLOTS + 1, .x = 300 };
    assert(tank_veg_beds(&t) == VEG_BEDS_MAX + 2);
    assert(tank_veg_bed_extra(&t, 4) == 0 && tank_veg_bed_extra(&t, 5) == 1);
    assert(tank_veg_extra_bed(&t, 0) == 4 && tank_veg_extra_bed(&t, 1) == 5);
    assert(tank_veg_z(&t, 4) == DECOR_Z_BACK && tank_veg_z(&t, 5) == DECOR_Z_FRONT);
    float cx0, cx1; int cn = 0;
    tank_veg_bed(&t, 4, &cx0, &cx1, NULL, &cn);
    assert(cn == 4 && cx0 < x0);                            /* the copy stands where it was planted */
    tank_veg_bed(&t, 5, &cx0, &cx1, NULL, &cn);
    assert(cn == 4 && cx0 > x1);
    /* a copy is a real seahorse perch, and it is the copy's own blade */
    assert(tank_veg_frond(&t, 4, 0, NULL) > 0 && tank_veg_frond(&t, 5, 3, NULL) > 0);
    /* depth ordering follows the copy's own lane, not the primary's */
    t.plant_z = DECOR_Z_MIDDLE;
    assert(tank_veg_above(&t, 4, 2) == (tank_decor_z(&t, 2) > DECOR_Z_BACK));
    /* storing the primary leaves the copies standing */
    t.sd_unlocks &= ~SD_ITEM_PLANT;
    assert(tank_veg_beds(&t) == VEG_BEDS_MAX - 1 + 2);
    assert(tank_veg_bed_extra(&t, 3) == 0 && tank_veg_bed_extra(&t, 4) == 1);
    tank_veg_bed(&t, 3, &cx0, &cx1, NULL, &cn);
    assert(cn == 4);
    /* and removing them all returns to the bare three beds */
    memset(t.inventory.extra, 0, sizeof t.inventory.extra);
    assert(tank_veg_beds(&t) == VEG_BEDS_MAX - 1);
    tank_veg_bed(&t, 3, &cx0, &cx1, NULL, &cn);
    assert(cn == 0);
    puts("PASS: the sword plant is duplicable - each extra copy is its own bed with its own x, depth and perch");
}

/* A sword-plant copy weaves AMONG the fish exactly like the primary, so the
 * placement page must offer all three depths for it - not just BACK/FRONT -
 * and its order arrows must work against the pieces it overlaps. */
static void test_sword_copy_depth_and_order(void) {
    tank_t t;
    one_fish(&t, 996, 0, .5f, .5f);
    t.sd_unlocks |= SD_ITEM_PLANT;
    tank_veg_sync(&t);
    /* the primary offers three depths; so must a copy */
    assert(tank_decor_z_count(0) == DECOR_Z_N);
    assert(tank_decor_copy_z_count(0, -1) == DECOR_Z_N);
    assert(tank_decor_copy_z_count(0, 0) == DECOR_Z_N);
    for (int i = 0; i < DECOR_Z_N; i++) {
        assert(tank_decor_copy_z_at(0, 0, i) == i);
        assert(tank_decor_copy_z_index(0, 0, i) == i);
    }
    /* every other item's copy keeps its two depths */
    assert(tank_decor_copy_z_count(9, 0) == 2);
    assert(tank_decor_copy_z_at(9, 0, 0) == DECOR_Z_BACK);
    assert(tank_decor_copy_z_at(9, 0, 1) == DECOR_Z_FRONT);
    assert(tank_decor_copy_z_index(9, 0, DECOR_Z_MIDDLE) == 1);
    /* a copy can sit AMONG, and the bed reports that lane */
    t.inventory.extra[0] = (inventory_extra_t){ .item = 1, .z = DECOR_Z_MIDDLE, .rank = DECOR_SLOTS, .x = 60 };
    assert(tank_veg_z(&t, 4) == DECOR_Z_MIDDLE);
    /* ordering: a copy passes the pieces it overlaps at its own depth. Only the
       plant weaves AMONG, so the overlapping neighbour here is the primary. */
    tank_decor_set(&t, 0, 60, DECOR_Z_MIDDLE);
    tank_decor_raise(&t, 0);
    t.inventory.extra[0].rank = 1;                               /* the copy starts under the primary */
    int primary_rank = tank_decor_rank(&t, 0);
    assert(primary_rank > 1);
    assert(tank_decor_copy_neighbor(&t, 0, 0, 1) == 0);          /* the primary is in front of the copy */
    assert(tank_decor_copy_neighbor(&t, 0, 0, -1) == -1);        /* nothing behind it */
    assert(tank_decor_copy_shift(&t, 0, 0, 1));
    assert((int)(t.inventory.extra[0].rank & DECOR_RANK_MASK) > primary_rank);
    assert(tank_decor_copy_neighbor(&t, 0, 0, -1) == 0);         /* now the primary is behind it */
    assert(tank_decor_copy_neighbor(&t, 0, 0, 1) == -1);
    /* a copy at FRONT orders against the ordinary pieces there too (the chest
       is narrow enough to sit where the copy does) */
    t.inventory.extra[0].z = DECOR_Z_FRONT;
    t.inventory.extra[0].rank = 1;
    t.sd_unlocks |= SD_ITEM_CHEST;
    tank_decor_set(&t, 9, 60, DECOR_Z_FRONT);
    tank_decor_raise(&t, 9);
    int chest_rank = tank_decor_rank(&t, 9);
    assert(chest_rank > 1);
    assert(tank_decor_copy_neighbor(&t, 0, 0, 1) == 9);
    assert(tank_decor_copy_shift(&t, 0, 0, 1));
    assert((int)(t.inventory.extra[0].rank & DECOR_RANK_MASK) > chest_rank);
    assert(tank_decor_copy_neighbor(&t, 0, 0, -1) == 9);
    t.inventory.extra[0].z = DECOR_Z_MIDDLE;
    /* a copy neighbour is named without colliding with an item id */
    assert(DECOR_IS_COPY_REF(DECOR_COPY_REF + 3) && DECOR_COPY_OF(DECOR_COPY_REF + 3) == 3);
    assert(!DECOR_IS_COPY_REF(0) && !DECOR_IS_COPY_REF(SD_ITEM_COUNT - 1));
    /* MIRROR survives an order change (bit 7 is not a rank) */
    t.inventory.extra[0].rank |= DECOR_RANK_MIRROR;
    assert(tank_decor_copy_shift(&t, 0, 0, -1));
    assert(t.inventory.extra[0].rank & DECOR_RANK_MIRROR);
    puts("PASS: a sword-plant copy offers all three depths and its order arrows work against the pieces it overlaps");
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
        test_sword_plant_copies();
        test_sword_copy_depth_and_order();
        test_variety_pair_guard();
        test_seahorse_open_water();
        test_rest_arrival_and_turn_continuity();
        test_rest_step_sizes();
        test_edge_steering_continuity();
        test_surface_cruising_without_seesaw();
        test_fluid_motion();
        test_tail_breathing_fan();
        test_jellyfish_float();
        test_explore_reaches_the_upper_half();
        test_separation_is_lateral();
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
