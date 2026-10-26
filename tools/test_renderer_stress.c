/* Synthetic five-fish profiling and long-running full/tile equivalence. */
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#undef near
#undef far
#else
#include <time.h>
#endif
#include "advisor.h"
/* Freeze wall time across all render paths belonging to the same frame. */
#define clock_port_now_unix renderer_test_now
#ifdef RENDER_BASELINE
#include "render-baseline.c"
#else
#include "../common/render.c"
#endif
#undef clock_port_now_unix

static int64_t frozen_unix = INT64_C(1792803600);
int64_t renderer_test_now(void) { return frozen_unix; }

static uint16_t frame[320 * 240], tiled[320 * 240], fish_frame[320 * 240];
static uint16_t retained[320 * 240];
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

static int64_t micros(void) {
#ifdef _WIN32
    LARGE_INTEGER now;
    static LARGE_INTEGER frequency;
    QueryPerformanceCounter(&now);
    if (!frequency.QuadPart) QueryPerformanceFrequency(&frequency);
    return now.QuadPart / frequency.QuadPart * INT64_C(1000000)
        + now.QuadPart % frequency.QuadPart * INT64_C(1000000) / frequency.QuadPart;
#else
    struct timespec now;
    timespec_get(&now, TIME_UTC);
    return (int64_t)now.tv_sec * 1000000 + now.tv_nsec / 1000;
#endif
}

static uint64_t hash_frame(const uint16_t *fb) {
    uint64_t hash = UINT64_C(14695981039346656037);
    for (int i = 0; i < TANK_W * TANK_H; i++) {
        hash ^= fb[i];
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

#ifndef RENDER_BASELINE
static void check_motion_cache(void) {
    fish_t f = {0};
    for (int i = 0; i < 100; i++) {
        f.preset = i % 3 ? i : -1;
        f.heading = (i - 50) * .13f; f.speed = i * 1.1f; f.wander = i * .17f;
        float clock = i * .31f;
        fish_motion_t m = fish_motion(&f, clock);
        float p = tank_fish_pitch(&f);
        assert(m.cp == cosf(p) && m.sp == sinf(p));
        assert(m.tail == sinf(clock * (8 + f.speed * .055f) + f.wander) * (.32f + f.speed * .007f));
        const float *offsets = jelly_strands(clock);
        for (int s = 0; s < 5; s++) for (int seg = 1; seg <= 6; seg++)
            assert(offsets[s * 6 + seg - 1] == sinf(clock * 3 + seg * .65f + s) * seg * .55f);
    }
}
#endif

static void fixture(tank_t *t, bool normal, int group) {
    tank_resolution(normal);
    memset(t, 0, sizeof *t);
    tank_init(t, 0x5167);
    tank_new_population(t);
    while (t->n_fish < 5) assert(tank_add_fish(t, 0, 1) >= 0);
    t->companion.display_compact = normal;
    t->companion.captions = 0;
    t->sd_unlocks = SD_ITEM_PLANT | SD_ITEM_SHRIMP | SD_ITEM_SNAIL |
        SD_ITEM_URCHIN | SD_ITEM_SEAHORSE | SD_ITEM_LIGHTS | SD_ITEM_SAND |
        SD_ITEM_PEBBLES | SD_ITEM_PEARL | SD_ITEM_SPONGES | SD_ITEM_BARREL;
    t->journal.xp = journal_level_xp(20);
    t->journal.initialized = 1;
    t->journal.theme = (uint8_t)group;
    t->autofeed_off = true;
    t->light_manual_off = false;
    t->light_auto = false;
    t->night = false;
    for (int i = 0; i < 5; i++) {
        fish_t *f = &t->fish[i];
        f->stage = i == 4 ? STAGE_ELDER : STAGE_ADULT;
        f->size = i == 4 ? 1.25f : 1.05f;
        f->variety = (uint8_t)(group * 5 + i);
        f->pattern = (uint8_t)(i % 4);
        f->wearable = (uint8_t)(1 + i % (WEAR_COUNT - 1));
        f->wear_colors = (uint8_t)((i + 1) | (i + 2) << 4);
        f->hunger = 3; f->energy = 7; f->stress = 0; f->trust = 5;
        f->x = TANK_W * (i + 1) / 6.0f;
        f->y = TANK_BOT * (0.25f + i % 3 * 0.22f);
        f->yaw = f->yaw_tail = i % 2 ? -0.8f : 0.9f;
        f->heading = i % 2 ? 3.0f : 0.2f;
        f->speed = 12 + i * 5;
    }
}

static void pose(tank_t *t, int sample) {
    t->clock = 10 + sample * 0.04f;
    frozen_unix = INT64_C(1792803600) + sample;
    for (int i = 0; i < 5; i++) {
        fish_t *f = &t->fish[i];
        float phase = sample * 0.06f + i;
        f->x = TANK_W * (i + 1) / 6.0f + sinf(phase * 0.5f) * 12;
        f->y = TANK_BOT * (0.25f + i % 3 * 0.22f) + cosf(phase) * 9;
        f->yaw = sinf(phase);
        f->yaw_tail = sinf(phase - 0.15f);
        f->heading = phase;
    }
}

static void draw_tiles(const tank_t *t, uint16_t *fb, int height) {
    for (int y = 0; y < TANK_H; y += height) {
        int rows = TANK_H - y < height ? TANK_H - y : height;
        render_tile(y, rows);
        render_tank(t, fb + y * TANK_W, TANK_W);
    }
    render_tile(0, 0);
}

static void benchmark(int frames) {
#ifndef RENDER_BASELINE
    check_motion_cache();
#endif
    for (int normal = 0; normal < 2; normal++)
        for (int detail = 0; detail < SCENE_DETAIL_COUNT; detail++)
            for (int group = 0; group < 2; group++) {
                tank_t t;
                fixture(&t, normal != 0, group);
                render_device_t device = {0, 0, false, -1, (uint8_t)detail};
                render_set_device(&device);
                for (int i = 0; i < 3; i++) draw_tiles(&t, tiled, 80);
                memset(render_prof_us, 0, sizeof render_prof_us);
                render_clock_us = micros;
                int64_t start = micros();
                for (int i = 0; i < frames; i++) {
                    pose(&t, i);
                    draw_tiles(&t, tiled, 80);
                }
                int64_t elapsed = micros() - start;
                render_clock_us = NULL;
                pose(&t, 239);
                draw_tiles(&t, tiled, 80);
                render_tank(&t, frame, TANK_W);
                assert(!memcmp(frame, tiled, (size_t)TANK_W * TANK_H * 2));
                tank_t before = t;
                int64_t fish_start = micros();
                for (int sample = 0; sample < frames; sample++) {
                    memset(fish_frame, 0, sizeof fish_frame);
                    for (int y = 0; y < TANK_H; y += 80) {
                        int rows = TANK_H - y < 80 ? TANK_H - y : 80;
                        render_tile(y, rows);
                        ctx_t c = ctx_full(fish_frame + y * TANK_W, TANK_W, 1);
                        for (int i = 0; i < 5; i++) draw_fish(&c, &t, &t.fish[i]);
                    }
                    render_tile(0, 0);
                }
                int64_t fish_elapsed = micros() - fish_start;
                assert(!memcmp(&t, &before, sizeof t));
                printf("{\"width\":%d,\"detail\":%d,\"group\":%d,\"frames\":%d,"
                       "\"render_us\":%.3f,\"fish_us\":%.3f,\"hash\":\"%016" PRIx64 "\",\"stages_us\":[",
                       TANK_W, detail, group, frames, (double)elapsed / frames,
                       (double)fish_elapsed / frames, hash_frame(frame));
                for (int i = 0; i < 8; i++) printf("%s%.3f", i ? "," : "", (double)render_prof_us[i] / frames);
                puts("]}");
                fflush(stdout);
            }
}

static void check_seahorse_shadow(void) {
    for (int normal = 0; normal < 2; normal++) {
        tank_t t;
        fixture(&t, normal != 0, 0);
        t.sd_unlocks = SD_ITEM_SEAHORSE;
        t.n_fish = 0;
        t.aquariums.clock_theme = CLOCK_OFF;
        t.sh_mode = SH_PERCH;
        t.sh_akind = SH_ANCHOR_POST;
        t.sh_pose = SH_POSE_GRIP;
        t.sh_land = SH_LAND_S;
        t.sh_x = t.sh_ax = TANK_W * .8f;
        t.sh_y = t.sh_ay = TANK_H * .65f;
        t.sh_ahw = 2;
        t.sh_vx = t.sh_vy = 0;
        t.sh_facing = 1;
        t.sh_hidden = false;
        render_device_t device = {0, 0, false, -1, SCENE_BALANCED};
        render_set_device(&device);
        render_tank(&t, frame, TANK_W);
        draw_tiles(&t, tiled, 1);
        assert(!memcmp(frame, tiled, (size_t)TANK_W * TANK_H * 2));
    }
    fprintf(stderr, "PASS: perched seahorse contact shadows cross one-row tile boundaries at both scales\n");
}

static void soak(int seconds) {
    if (!seconds) return;
    check_seahorse_shadow();
    int64_t end = micros() + (int64_t)seconds * 1000000;
    uint64_t checks = 0;
    while (micros() < end) {
        tank_t t;
        fixture(&t, checks % 2 != 0, (int)(checks / 2 % 2));
#ifndef RENDER_BASELINE
        static const uint8_t duplicates[] = {9, 10, 11, 12, 13, 16, 20, 21};
        if (checks % 3) for (int i = 0; i < DECOR_EXTRA_CAP; i++) {
            t.inventory.extra[i].item = (uint8_t)(duplicates[i] + 1);
            t.inventory.extra[i].x = TANK_W * (i + 1) / (float)(DECOR_EXTRA_CAP + 1);
            t.inventory.extra[i].z = i % 2 ? DECOR_Z_FRONT : DECOR_Z_BACK;
        }
        t.mussel.owned = 1;
        t.mussel.bank = checks % 2 ? 1 : 0;
        t.mussel_glint = 1.6f;
#endif
        int detail = (int)(checks / 4 % SCENE_DETAIL_COUNT);
        render_device_t device = {0, 0, false, -1, (uint8_t)detail};
        render_set_device(&device);
        t.night = checks / 12 % 2 != 0;
        t.light_manual_off = t.night;
        t.water_style = (uint8_t)(checks / 24 % WATER_STYLE_COUNT);
        t.sand_style = (uint8_t)(checks / 24 % SAND_STYLE_COUNT);
        t.aquariums.clock_theme = (uint8_t)(checks / 48 % CLOCK_THEME_COUNT);
        t.aquariums.clock_layout = clock_layout_set_size(
            (uint8_t)((checks / 192 % CLOCK_POSITION_COUNT) << 2), (int)(checks / 96 % CLOCK_SIZE_COUNT));
        t.clock_requested = t.aquariums.clock_theme != CLOCK_OFF;
        render_work_caches(checks % 3 != 0);
        render_tank(&t, frame, TANK_W);
        for (int step = 0; step < 7; step++) {
#ifndef RENDER_BASELINE
            if (step == 3) memset(&t.inventory.extra[0], 0, sizeof t.inventory.extra[0]);
            if (step == 4 && t.inventory.extra[1].item) t.inventory.extra[1].x += 12;
            if (step == 2) t.mussel.bank = 1;
            t.mussel_glint = step < 4 ? 1.6f - step * .4f : 0;
            if (step == 5) t.mussel.owned = 0;
#endif
            tank_tick(&t, 0.04f, advisor_rules);
            pose(&t, (int)(checks % 1000) + step);
#ifndef RENDER_BASELINE
            t.clock = (float)(checks % 6 * 360) + (checks % 3 ? 344 + checks % 100 * .16f : 100) + step * .04f;
#endif
            tank_t before = t;
            render_tank(&t, frame, TANK_W);
            render_tank(&t, fish_frame, TANK_W);
            if (memcmp(frame, fish_frame, (size_t)TANK_W * TANK_H * 2)) {
                fprintf(stderr, "Repeated full-frame mismatch: case=%" PRIu64 " step=%d clock=%d layout=%d\n",
                        checks, step, t.aquariums.clock_theme, t.aquariums.clock_layout);
                for (int i = 0; i < TANK_W * TANK_H; i++) if (frame[i] != fish_frame[i]) {
                    fprintf(stderr, "First pixel: %d,%d first=%04x second=%04x\n",
                            i % TANK_W, i / TANK_W, frame[i], fish_frame[i]);
                    break;
                }
                abort();
            }
            draw_tiles(&t, tiled, 1 + (int)(checks % 80));
            if (memcmp(frame, tiled, (size_t)TANK_W * TANK_H * 2)) {
                fprintf(stderr, "Full/tile stress mismatch: case=%" PRIu64 " step=%d width=%d detail=%d\n",
                        checks, step, TANK_W, detail);
                for (int i = 0; i < TANK_W * TANK_H; i++) if (frame[i] != tiled[i]) {
                    fprintf(stderr, "First pixel: %d,%d full=%04x tile=%04x\n",
                            i % TANK_W, i / TANK_W, frame[i], tiled[i]);
                    break;
                }
                abort();
            }
            render_damage_begin(&t, step == 0);
            int cursor = 0, x, y, width, height;
            while (render_damage_next(&cursor, &x, &y, &width, &height)) {
                render_tile(y, height);
                render_region(x, width);
                render_tank(&t, fish_frame, TANK_W);
                for (int row = 0; row < height; row++)
                    memcpy(retained + (y + row) * TANK_W + x,
                           fish_frame + row * TANK_W + x, (size_t)width * 2);
            }
            render_tile(0, 0);
            render_region(0, 0);
            if (memcmp(frame, retained, (size_t)TANK_W * TANK_H * 2)) {
                fprintf(stderr, "Retained-region stress mismatch: case=%" PRIu64 " step=%d\n", checks, step);
                abort();
            }
            assert(!memcmp(&t, &before, sizeof t));
            assert(t.n_fish == 5);
            for (int i = 0; i < 5; i++) assert(isfinite(t.fish[i].x) && isfinite(t.fish[i].y));
        }
        checks++;
        if (checks % 100 == 0) {
            fprintf(stderr, "Stress: %" PRIu64 " cases / %" PRIu64 " five-fish frame pairs\n", checks, checks * 7);
            fflush(stderr);
        }
    }
    fprintf(stderr, "PASS: %d-second stress, %" PRIu64 " cases / %" PRIu64
            " full/tile/retained frame sets; five fish, both scales, all details, day/night, palettes and clock layouts; no tank mutation\n",
            seconds, checks, checks * 7);
}

int main(int argc, char **argv) {
    assert(argc == 4);
#ifndef RENDER_BASELINE
    for (int pass = 0; pass < 3; pass++)
        for (int i = 0; i < 256; i++) {
            uint8_t fraction = (uint8_t)(pass % 2 ? 255 - i : i);
            float expected = sh_tail_hw(fraction / 255.0f);
            assert(sh_pixel_tail_hw(fraction) == expected);
            assert(sh_pixel_tail_hw(fraction) == expected);
        }
#endif
    FILE *icons = fopen(argv[1], "rb"); assert(icons);
    icon_bytes = fread(icon_bank, 1, sizeof icon_bank, icons);
    assert(!ferror(icons) && feof(icons) && !fclose(icons));
    void *decor = calloc(1, render_decor_scratch_size());
    void *mask = calloc(1, render_mask_scratch_size());
    void *backdrop = malloc(12288);
    assert(decor && mask && backdrop);
    render_set_decor_scratch(decor);
    render_set_mask_scratch(mask);
    render_set_backdrop_cache(backdrop, 12288);
    render_work_caches(true);
    render_clock_synchronized(true);
    benchmark(atoi(argv[2]));
    soak(atoi(argv[3]));
    render_work_caches(false);
    render_set_backdrop_cache(NULL, 0);
    render_set_decor_scratch(NULL);
    render_set_mask_scratch(NULL);
    free(backdrop); free(mask); free(decor);
    return 0;
}
