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
#ifndef RENDER_BASELINE
static bool fail_render_calloc;
static bool fail_render_malloc;
static void *renderer_malloc(size_t bytes) {
    return fail_render_malloc ? NULL : malloc(bytes);
}
static void *renderer_calloc(size_t count, size_t bytes) {
    return fail_render_calloc ? NULL : calloc(count, bytes);
}
#define calloc renderer_calloc
#define malloc renderer_malloc
#endif
/* Freeze wall time across all render paths belonging to the same frame. */
#define clock_port_now_unix renderer_test_now
#ifdef RENDER_BASELINE
#include "render-baseline.c"
#else
#include "../common/render.c"
#endif
#undef clock_port_now_unix
#ifndef RENDER_BASELINE
#undef calloc
#undef malloc
#endif

static int64_t frozen_unix = INT64_C(1792803600);
int64_t renderer_test_now(void) { return frozen_unix; }

static uint16_t frame[320 * 240], tiled[320 * 240], fish_frame[320 * 240];
static uint16_t retained[320 * 240];
static uint8_t icon_bank[128 * 1024];
static size_t icon_bytes;
#ifdef RENDER_EMBEDDED_ICONS
#include "icons-check.h"
static bool embedded_raw_reference;
#endif

const icon_t *icons_resolve(const icon_t *icon) {
#ifdef RENDER_EMBEDDED_ICONS
    if (!embedded_raw_reference) return icon;
    size_t offset = icon_bytes;
    for (unsigned k = 0; k < sizeof embedded_icons / sizeof embedded_icons[0]; k++)
        if (embedded_icons[k].icon == icon) { offset = embedded_icons[k].offset; break; }
#else
    size_t offset = icon->offset;
#endif
    static icon_t view;
    size_t pixels = (size_t)icon->w * icon->h;
    assert(offset <= icon_bytes && pixels * 3 <= icon_bytes - offset);
    view = *icon;
    view.offset = (uint32_t)offset;
    view.rgb = (const uint16_t *)(icon_bank + offset);
    view.a = icon_bank + offset + pixels * 2;
    return &view;
}
#ifdef RENDER_EMBEDDED_ICONS
static void check_embedded_icons(void) {
    const uint16_t odd_rgb[] = {2, 0x1234, 0xabcd, 0x0110};
    icon_t odd = {3, 1, odd_rgb, NULL, UINT32_C(0x40000000)};
    assert(icon_rgb_at(&odd, 0) == 0x1234);
    assert(icon_rgb_at(&odd, 1) == 0xabcd && icon_rgb_at(&odd, 2) == 0xabcd);
    uint16_t nibble_colors[1 + 16 + 4] = {16};
    for (int i = 0; i < 16; i++) nibble_colors[i + 1] = (uint16_t)(i * 251u);
    for (int i = 0; i < 4; i++)
        nibble_colors[17 + i] = (uint16_t)(i * 4 | (i * 4 + 1) << 4 |
                                                       (i * 4 + 2) << 8 | (i * 4 + 3) << 12);
    icon_t nibble_palette = {4, 4, nibble_colors, NULL, UINT32_C(0x40000000)};
    for (int i = 0; i < 16; i++) assert(icon_rgb_at(&nibble_palette, i) == (uint16_t)(i * 251u));
    uint16_t all_colors[1 + 256 + 128] = {256};
    for (int i = 0; i < 256; i++) all_colors[i + 1] = (uint16_t)(i * 251u);
    for (int i = 0; i < 128; i++) all_colors[257 + i] = (uint16_t)(i * 2 | (i * 2 + 1) << 8);
    icon_t full_palette = {16, 16, all_colors, NULL, UINT32_C(0x40000000)};
    for (int i = 0; i < 256; i++) assert(icon_rgb_at(&full_palette, i) == (uint16_t)(i * 251u));
    for (unsigned k = 0; k < sizeof embedded_icons / sizeof embedded_icons[0]; k++) {
        const icon_t *ic = embedded_icons[k].icon;
        unsigned n = ic->w * ic->h, offset = embedded_icons[k].offset;
        assert(offset + n * 3 <= icon_bytes);
        icon_alpha_t cursor = {ic->a, 0, 0};
        for (unsigned i = 0; i < n; i++) {
            assert(icon_rgb_at(ic, i) == (uint16_t)(icon_bank[offset + i * 2] | icon_bank[offset + i * 2 + 1] << 8));
            assert(icon_alpha_at(ic, &cursor, i) == icon_bank[offset + n * 2 + i]);
            assert(icon_alpha_at(ic, &cursor, i) == icon_bank[offset + n * 2 + i]);
        }
    }
    fprintf(stderr, "PASS: all embedded RGB/alpha pixels match the original bank, including repeated samples\n");
    for (int normal = 0; normal < 2; normal++) {
        tank_resolution(normal);
        for (unsigned k = 0; k < sizeof embedded_icons / sizeof embedded_icons[0]; k++)
            for (int mode = 0; mode < 10; mode++) {
                if (mode >= 4 && k >= 9) continue;
                for (int reference = 0; reference < 2; reference++) {
                    embedded_raw_reference = reference != 0;
                    uint16_t *pixels = reference ? tiled : frame;
                    memset(pixels, 0x4d, sizeof frame);
                    ctx_t c = ctx_full(pixels, TANK_W, .73f);
                    const icon_t *ic = embedded_icons[k].icon;
                    if (mode == 0) blit_icon(&c, -3, 6, ic, 173);
                    else if (mode == 1) blit_icon_locked(&c, 19, 22, ic);
                    else if (mode < 4) blit_icon_scaled(&c, -3, 6, ic, 2, mode == 2);
                    else {
                        static const int sizes[] = {19, 48, 64};
                        render_decor_thumbnail(pixels, TANK_W, 5, 7, sizes[(mode - 4) / 2], k, mode % 2);
                    }
                }
                assert(!memcmp(frame, tiled, sizeof frame));
            }
    }
    embedded_raw_reference = false;
    fprintf(stderr, "PASS: compressed/raw icon blits, locked/integer-scale art and resampled thumbnails match at both scales\n");
}
#endif

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
static void check_packed_spans(void) {
    uint16_t pixels[326];
    for (int offset = 0; offset < 4; offset++)
        for (int count = 0; count <= 320; count++) {
            for (int i = 0; i < 326; i++) pixels[i] = 0xa53c;
            uint16_t color = (uint16_t)(count * 503 + offset);
            fill565_span(pixels + offset, count, color);
            for (int i = 0; i < 326; i++)
                assert(pixels[i] == (i >= offset && i < offset + count ? color : 0xa53c));
        }
    fprintf(stderr, "PASS: packed RGB565 stores preserve every pixel and guard at both halfword alignments, lengths 0-320\n");
    for (int normal = 0; normal < 2; normal++) {
        tank_resolution(normal != 0);
        for (int y = 0; y < TANK_H; y += 7) for (int offset = 0; offset < 2; offset++) {
            for (int i = 0; i < TANK_W * TANK_H; i++) frame[i] = tiled[i] = (uint16_t)(i * 37);
            ctx_t c = ctx_full(frame, TANK_W, .73f);
            src_t color = src_color(0xa6c871, c.dim);
            int x0 = offset, x1 = TANK_W - 3;
            uint16_t expected = color.v;
            int a = vig_alpha((x0 + x1) >> 1, y);
            if (a) px_darken(&expected, a);
            for (int x = x0; x <= x1; x++) tiled[y * TANK_W + x] = expected;
            span_final(&c, x0, x1, y, &color, 255);
            assert(!memcmp(frame, tiled, (size_t)TANK_W * TANK_H * sizeof *frame));
        }
    }
    fprintf(stderr, "PASS: final opaque spans preserve scalar dimming/vignette pixels and row guards at both scales\n");
}

static void check_frame_clock(void) {
    for (int normal = 0; normal < 2; normal++) for (int theme = CLOCK_OFF; theme < CLOCK_THEME_COUNT; theme++)
        for (int hidden = 0; hidden < 2; hidden++) {
            tank_t t = {0};
            tank_init(&t, 42);
            tank_resolution(normal != 0);
            t.companion.display_compact = normal != 0;
            t.clock_requested = true;
            t.aquariums.clock_theme = (uint8_t)theme;
            t.aquariums.clock_flags = hidden ? CLOCK_HIDE_PANEL : 0;
            render_clock_fixed_unix = frozen_unix;
            for (int i = 0; i < TANK_W * TANK_H; i++) frame[i] = tiled[i] = (uint16_t)(i * 13);
            render_work_caches(true);
            render_clock_overlay(&t, frame, TANK_W, frozen_unix);
            render_clock_layout_t expected, actual;
            render_clock_layout(&t, &expected);
            render_frame_begin(&t);
            render_clock_fixed_unix++;
            assert(render_clock_now() == frozen_unix);
            render_clock_layout(&t, &actual);
            assert(!memcmp(&expected, &actual, sizeof actual));
            for (int y = 0; y < TANK_H; y += 40) {
                render_tile(y, TANK_H - y < 40 ? TANK_H - y : 40);
                render_clock_overlay(&t, tiled + y * TANK_W, TANK_W, render_clock_now());
            }
            render_tile(0, 0);
            render_frame_end();
            assert(render_clock_now() == frozen_unix + 1);
            assert(!memcmp(frame, tiled, (size_t)TANK_W * TANK_H * 2));
            render_clock_fixed_unix = 0;
        }
    render_work_caches(false);
    tank_t t = {0};
    tank_init(&t, 42);
    t.clock_requested = true; t.aquariums.clock_theme = CLOCK_ANALOG;
    render_clock_fixed_unix = frozen_unix;
    memset(frame, 0x5c, sizeof frame); memset(tiled, 0x5c, sizeof tiled);
    render_clock_overlay(&t, frame, TANK_W, frozen_unix);
    render_work_caches(true);
    fail_render_malloc = true;
    render_frame_begin(&t);
    assert(!g_frame_clock_key && !g_frame_has_clock);
    render_clock_overlay(&t, tiled, TANK_W, frozen_unix);
    render_frame_end();
    fail_render_malloc = false;
    assert(!memcmp(frame, tiled, (size_t)TANK_W * TANK_H * 2));
    render_work_caches(false); render_work_caches(true);
    render_frame_begin(&t);
    assert(g_frame_clock_key && render_work_cache_bytes() == sizeof *g_frame_clock_key);
    render_frame_end();
    render_work_caches(false);
    assert(!g_frame_clock_key && !render_work_cache_bytes());
    render_clock_fixed_unix = 0;
    fprintf(stderr, "PASS: frame-scoped clock metadata preserves all themes, panel modes, scales and exact tiled pixels across a wall-clock second boundary\n");
    fprintf(stderr, "PASS: optional frame metadata allocation failure draws exact live pixels, retries after cache lease, accounts for bytes and releases all memory for TLS/SD\n");
}

static void check_motion_cache(void) {
    fish_t f = {0};
    for (int i = 0; i < 100; i++) {
        f.preset = i % 3 ? i : -1;
        f.heading = (i - 50) * .13f; f.speed = i * 1.1f; f.wander = i * .17f;
        f.yaw = i * .01f; f.yaw_tail = -.02f * i;
        f.hunger = i % 11; f.stress = i % 10;
        f.color = 0x123456u * i; f.accent = 0xff9800; f.fin = 0x456789;
        f.traits = i % 2 ? TRAIT_PAINTED_FINS : 0;
        float clock = i * .31f;
        f.swim_phase = clock * 1.7f;
        fish_motion_t m = fish_motion(&f);
        float p = tank_fish_pitch(&f);
        assert(m.cp == cosf(p) && m.sp == sinf(p));
        assert(m.yh == yaw_ease(f.yaw) && m.yt == yaw_ease(f.yaw_tail));
        float pale = f.hunger > 7 ? (f.hunger - 7) / 3.0f * .35f : 0;
        assert(m.body_rgb == mix(mix(f.color, 0xf25b65, f.stress / 10.0f * .22f), 0x7a8a8e, pale));
        assert(m.fin_rgb == mix(mix(f.traits & TRAIT_PAINTED_FINS ? f.accent : f.fin,
                                  0xf25b65, f.stress / 10.0f * .18f), 0x5a6a6e, pale));
        assert(m.tail == sinf(f.swim_phase + f.wander) * (.32f + f.speed * .007f));
        const float *offsets = jelly_strands(clock);
        for (int s = 0; s < JELLY_STRANDS; s++) for (int seg = 1; seg <= JELLY_SEGMENTS; seg++)
            assert(offsets[s * JELLY_SEGMENTS + seg - 1] ==
                   sinf(clock * (s < 6 ? 2.2f : 1.6f) + seg * 0.58f + s * 1.3f) * seg * (s < 6 ? 0.5f : 0.32f));
    }
    const float *ridge = snail_foot_ridge();
    for (int x = -16; x <= 20; x++) assert(ridge[x + 16] == snail_foot_top(x));
    const sh_row_t *rows = sh_body_rows();
    for (int y = 0; y < SH_H; y++) {
        sh_row_t expected = {0};
        expected.body = sh_body(y + .5f, &expected.sx, &expected.w, &expected.tb);
        assert(rows[y].body == expected.body && rows[y].sx == expected.sx &&
               rows[y].w == expected.w && rows[y].tb == expected.tb);
    }
    for (int y = 14; y < 27; y++) for (int x = 0; x < 11; x++) {
        float vx = x + .5f - 5.2f, vy = y + .5f - 20.3f;
        float radius = sqrtf(vx * vx + vy * vy);
        float angle = atan2f(vy, vx) * (180.0f / 3.14159265f);
        if (angle < 0) angle += 360;
        sh_fin_t fin = sh_fin_geometry(x, y);
        assert(fin.radius == radius && fin.angle == angle);
    }
    for (int field = 0; field < 12; field++) {
        f = (fish_t){.color = 0x123456, .fin = 0xabcdef, .accent = 0x654321,
                     .hunger = 9, .stress = 1, .yaw = .2f, .yaw_tail = -.5f, .swim_phase = 1};
        if (field == 7) f.traits = TRAIT_PAINTED_FINS;
        (void)fish_motion(&f);
        switch (field) {
        case 0: f.yaw = -.9f; break;
        case 1: f.yaw_tail = .8f; break;
        case 2: f.hunger = 10; break;
        case 3: f.stress = 9; break;
        case 4: f.color = 0xffffff; break;
        case 5: f.fin = 0xffffff; break;
        case 6: f.traits = TRAIT_PAINTED_FINS; break;
        case 7: f.accent = 0xffffff; break;
        case 8: f.heading = 1; break;
        case 9: f.speed = 23; break;
        case 10: f.swim_phase = 2.5f; break;
        default: f.wander = 1; break;
        }
        fish_motion_t m = fish_motion(&f);
        float pale = (f.hunger - 7) / 3.0f * .35f;
        assert(m.yh == yaw_ease(f.yaw) && m.yt == yaw_ease(f.yaw_tail));
        assert(m.cp == cosf(tank_fish_pitch(&f)) && m.sp == sinf(tank_fish_pitch(&f)));
        assert(m.tail == sinf(f.swim_phase + f.wander) * (.32f + f.speed * .007f));
        assert(m.body_rgb == mix(mix(f.color, 0xf25b65, f.stress / 10.0f * .22f), 0x7a8a8e, pale));
        assert(m.fin_rgb == mix(mix(f.traits & TRAIT_PAINTED_FINS ? f.accent : f.fin,
                                  0xf25b65, f.stress / 10.0f * .18f), 0x5a6a6e, pale));
    }
}
static void check_ornament_art(void) {
    static const int items[] = {9, 10, 11, 12, 13, 16, 17, 18, 19, 20, 21,
        43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62};
    for (int pass = 0; pass < 3; pass++) {
        g_buoy_sway = pass - 1; g_buoy_bob = pass;
        for (unsigned i = 0; i < sizeof items / sizeof items[0]; i++) {
            int item = items[i];
            tank_t t = {0};
            if (pass == 2 && SD_ITEMS[item].bit) t.sd_unlocks = SD_ITEMS[item].bit;
            else {
                t.inventory.extra[0].item = (uint8_t)(item + 1);
                if (!pass) t.inventory.extra[1].item = (uint8_t)(item + 1);
            }
            ornament_select(&t);
            for (int y = 0; y < tank_ornament_h(item); y++)
                for (int x = -1; x <= tank_ornament_w(item); x++)
                    assert(ornament_cached_pixel(item, x, y) == ornament_pixel(item, x, y));
            if (item != 13 && item != 21 && !tank_decor_grand(item)) assert(g_ornament_art && !g_ornament_failed);
            if (g_ornament_art) for (int y = 0; y < tank_ornament_h(item); y++)
                for (int x = 0; x < tank_ornament_w(item); x++)
                    assert(((g_ornament_art->mask[y] >> x) & 1) == (ornament_pixel(item, x, y) != 0));
        }
    }
    render_work_caches(false);
    assert(!g_ornament_art && render_work_cache_bytes() == 0);
    render_work_caches(true);
    tank_t sample = {0};
    sample.inventory.extra[0].item = sample.inventory.extra[1].item = 11;
    ornament_select(&sample);
    fail_render_calloc = true;
    assert(ornament_cached_pixel(10, 12, 8) == ornament_pixel(10, 12, 8));
    assert(!g_ornament_art && g_ornament_failed);
    fail_render_calloc = false;
    render_work_caches(false); render_work_caches(true);
    ornament_select(&sample);
    assert(ornament_cached_pixel(10, 12, 8) == ornament_pixel(10, 12, 8));
    assert(g_ornament_art);
    fprintf(stderr, "PASS: exact indexed ornament sprites/masks for single originals, single extras and repeated copies; animated fallback, bounded lifetime and allocation failure\n");
}
static void check_damage_merge(void) {
    tank_resolution(true);
    memset(g_damage_blocks, 0, sizeof g_damage_blocks);
    g_damage_blocks[1] = g_damage_blocks[2] = 2;
    int cursor = 0, x, y, w, h;
    assert(render_damage_next(&cursor, &x, &y, &w, &h));
    assert(x == 32 && y == 40 && w == 32 && h == 80);
    assert(!render_damage_next(&cursor, &x, &y, &w, &h));
    fprintf(stderr, "PASS: odd-row damage merges without extra pixels or exceeding an 80-row buffer\n");
}
static void check_foreground_art(void) {
    g_pk_lx = -1;
    for (int normal = 0; normal < 2; normal++) {
        tank_resolution(normal != 0);
        for (int item = 9; item < SD_ITEM_COUNT; item++) {
            if (!tank_decor_placeable(item) || item == 13 || item == 21 || tank_decor_grand(item)) continue;
            tank_t t = {0};
            t.inventory.extra[0].item = (uint8_t)(item + 1);
            for (int mode = 0; mode < 8; mode++) {
                int x0 = mode & 1 ? -5 : TANK_W / 2;
                int y0 = TANK_H - tank_ornament_h(item) - 5;
                float dim = mode & 2 ? .45f : 1;
                bool final = (mode & 4) != 0;
                g_scene_occluded = (mode & 1) != 0;
                for (int y = 0; y < TANK_H; y++) {
                    g_hidden_left[y] = 10; g_hidden_right[y] = 20;
                }
                memset(frame, 0x5a, sizeof frame);
                memset(tiled, 0x5a, sizeof tiled);
                bool enabled = g_work_enabled;
                g_work_enabled = false;
                ctx_t live = ctx_full(frame, TANK_W, dim);
                draw_ornament(&live, item, x0, y0, 1, final, true, 0);
                g_work_enabled = enabled;
                render_work_caches(true); ornament_select(&t);
                render_region(7, TANK_W - 19);
                for (int y = 0; y < TANK_H; y += 17) {
                    render_tile(y, TANK_H - y < 17 ? TANK_H - y : 17);
                    ctx_t cached = ctx_full(tiled + y * TANK_W, TANK_W, dim);
                    draw_ornament(&cached, item, x0, y0, 1, final, true, 0);
                }
                g_scene_occluded = false;
                render_tile(0, 0); render_region(0, 0);
                assert(g_ornament_art && g_ornament_art->foreground_valid);
                for (int y = 0; y < TANK_H; y++) for (int x = 0; x < TANK_W; x++)
                    assert(tiled[y * TANK_W + x] == (x >= 7 && x < TANK_W - 12 ? frame[y * TANK_W + x] : 0x5a5a));
            }
        }
    }
    render_work_caches(false); render_work_caches(true);
    tank_t t = {0}; t.inventory.extra[0].item = 11;
    ornament_select(&t);
    assert(ornament_art(10));
    ctx_t c = ctx_full(frame, TANK_W, 1);
    fail_render_malloc = true;
    draw_ornament(&c, 10, 100, 100, 1, true, true, 0);
    fail_render_malloc = false;
    assert(g_ornament_art->foreground_valid);
    assert(sizeof g_ornament_art->foreground == 128);
    render_work_caches(false); render_work_caches(true);
    tank_init(&t, 0x5167);
    t.inventory.extra[0].item = 17;
    t.inventory.extra[0].x = TANK_W / 2;
    t.inventory.extra[0].z = DECOR_Z_FRONT;
    for (int y = 0; y < TANK_H; y += 40) {
        render_tile(y, TANK_H - y < 40 ? TANK_H - y : 40);
        render_tank(&t, tiled + y * TANK_W, TANK_W);
    }
    render_tile(0, 0);
    assert(g_ornament_selected == 16 && g_ornament_art && g_ornament_art->foreground_valid);
    render_work_caches(false);
    assert(!render_work_cache_bytes());
    render_work_caches(true);
    fprintf(stderr, "PASS: 128-byte RGB565 foreground palette preserves exact live colors, transparency, clipped tiles/regions, clock occlusion, night/vignette; full tiled tank uses it without a second allocation\n");
}
static void check_castle_cache(void) {
    for (int normal = 0; normal < 2; normal++) {
        tank_resolution(normal != 0);
        for (int mode = 0; mode < 8; mode++) {
            int cx = mode & 1 ? 12 : TANK_W / 2;
            bool final = (mode & 2) != 0;
            float dim = mode & 4 ? .45f : 1;
            memset(frame, 0x5a, sizeof frame);
            memset(tiled, 0x5a, sizeof tiled);
            render_work_caches(false);
            ctx_t live = ctx_full(frame, TANK_W, dim);
            draw_castle(&live, cx, 1, final);
            render_work_caches(true);
            for (int y = 0; y < TANK_H; y += 17) {
                render_tile(y, TANK_H - y < 17 ? TANK_H - y : 17);
                ctx_t cached = ctx_full(tiled + y * TANK_W, TANK_W, dim);
                draw_castle(&cached, cx, 1, final);
            }
            render_tile(0, 0);
            assert(g_castle_art);
            assert(!memcmp(frame, tiled, sizeof frame));
        }
    }
    render_work_caches(false); render_work_caches(true);
    ctx_t c = ctx_full(frame, TANK_W, 1);
    fail_render_malloc = true;
    draw_castle(&c, TANK_W / 2, 1, false);
    assert(!g_castle_art);
    fail_render_malloc = false;
    render_work_caches(false); render_work_caches(true);
    draw_castle(&c, TANK_W / 2, 1, false);
    assert(g_castle_art && CASTLE_FRONT_BYTES == 9701);
    render_work_caches(false);
    assert(!g_castle_art && !render_work_cache_bytes());
    render_work_caches(true);
    fprintf(stderr, "PASS: lossless castle front matches live day/night, final lighting, edge clipping and 17-row tiles; allocation failure/recovery and release\n");
}
static void check_crown_cache(void) {
    g_clip_k = -1;
    for (int normal = 0; normal < 2; normal++) {
        tank_resolution(normal != 0);
        for (int mode = 0; mode < 12; mode++) {
            int cx = mode & 1 ? 12 : TANK_W / 2;
            float clock = mode * .19f, growth = 1.1f + mode * .4f;
            float dim = mode & 2 ? .45f : 1;
            int live_box[4], cached_box[4];
            memset(frame, 0x5a, sizeof frame);
            memset(tiled, 0x5a, sizeof tiled);
            render_work_caches(false);
            ctx_t live = ctx_full(frame, TANK_W, dim);
            draw_coral_crown(&live, cx, 0x9186bf, growth, clock,
                             live_box, live_box + 1, live_box + 2, live_box + 3);
            draw_cluster_crown(&live, cx, 0, growth, clock,
                               live_box, live_box + 1, live_box + 2, live_box + 3);
            render_work_caches(true);
            for (int y = 0; y < TANK_H; y += 17) {
                render_tile(y, TANK_H - y < 17 ? TANK_H - y : 17);
                ctx_t cached = ctx_full(tiled + y * TANK_W, TANK_W, dim);
                draw_coral_crown(&cached, cx, 0x9186bf, growth, clock,
                                 cached_box, cached_box + 1, cached_box + 2, cached_box + 3);
                draw_cluster_crown(&cached, cx, 0, growth, clock,
                                   cached_box, cached_box + 1, cached_box + 2, cached_box + 3);
                assert(!memcmp(live_box, cached_box, sizeof live_box));
            }
            render_tile(0, 0);
            assert(g_crown_poses && !memcmp(frame, tiled, sizeof frame));
            for (int j = 0; j < 20; j++) assert(crown_pose(cx + j, 100, 9, -2, -1, 15, 0, clock));
            assert(crown_pose(cx, 100, 9, -2, -1, 15, 0, clock + 1));
            assert(g_crown_poses->count == 1 && g_crown_poses->used == 135);
        }
    }
    render_work_caches(false); render_work_caches(true);
    fail_render_calloc = true;
    assert(!crown_pose(100, 100, 9, -2, -1, 15, 0, 1));
    fail_render_calloc = false;
    render_work_caches(false); render_work_caches(true);
    assert(crown_pose(100, 100, 9, -2, -1, 15, 0, 1));
    render_work_caches(false);
    assert(!g_crown_poses && !render_work_cache_bytes());
    render_work_caches(true);
    fprintf(stderr, "PASS: crown pose reuse matches live pixels and bounds across tiles, growth, clocks, day/night and cache overflow/failure/release\n");
}
static void check_backdrop_codec(void) {
    uint16_t *saved = g_backdrop;
    size_t saved_words = g_backdrop_words;
    uint16_t storage[TANK_MAX_W + 8], pixels[TANK_MAX_W];
    g_backdrop = storage + 1;
    for (int normal = 0; normal < 2; normal++) {
        tank_resolution(normal != 0);
        for (int mode = 0; mode < 4; mode++) {
            for (int x = 0; x < TANK_W; x++)
                pixels[x] = mode == 0 ? 0x1234 : mode == 1 ? (uint16_t)x :
                    mode == 2 ? (uint16_t)(x / 2) : (uint16_t)(x < 100 ? 0x4321 : x < 200 ? x : 0x5678);
            for (int capacity = 0; capacity <= TANK_W + 4; capacity++) {
                for (unsigned i = 0; i < sizeof storage / sizeof *storage; i++) storage[i] = 0xa57b;
                g_backdrop_words = (size_t)capacity;
                size_t used = 0;
                bool fits = backdrop_encode(pixels, &used);
                assert(used <= (size_t)capacity);
                assert(storage[0] == 0xa57b && storage[capacity + 1] == 0xa57b);
                if (fits) {
                    assert(backdrop_matches(pixels, 0));
                    pixels[TANK_W / 2] ^= 1;
                    assert(!backdrop_matches(pixels, 0));
                    pixels[TANK_W / 2] ^= 1;
                }
                if (capacity == TANK_W + 4) assert(fits);
                if (capacity == TANK_W + 4) {
                    g_backdrop_row[0] = 0;
                    render_tile(0, 1); render_region(7, TANK_W - 19);
                    ctx_t c = ctx_full(frame, TANK_W, 1);
                    for (int hidden = 0; hidden < 2; hidden++) {
                        for (int x = 0; x < TANK_W; x++) frame[x] = 0xa57b;
                        g_scene_occluded = hidden != 0;
                        g_hidden_left[0] = TANK_W / 3; g_hidden_right[0] = TANK_W * 2 / 3;
                        backdrop_draw_row(&c, frame, TANK_W, 0);
                        for (int x = 0; x < TANK_W; x++) {
                            bool drawn = x >= 7 && x < TANK_W - 12 &&
                                (!hidden || x < g_hidden_left[0] || x >= g_hidden_right[0]);
                            assert(frame[x] == (drawn ? pixels[x] : 0xa57b));
                        }
                    }
                    g_scene_occluded = false;
                    render_tile(0, 0); render_region(0, 0);
                }
            }
        }
    }
    g_backdrop = saved; g_backdrop_words = saved_words; g_backdrop_dim = -1;
    fprintf(stderr, "PASS: hybrid literal/run backdrop codec preserves every pixel, detects changed rows, bounds every capacity and compresses noisy rows to width+1 words\n");
}
static void check_scenery_cadence(void) {
    tank_t t = {0};
    render_work_caches(false); render_work_caches(true);
    fail_render_calloc = true;
    assert(veg_cached_sway(&t, 0, VEG_SEED[3]) == veg_sway(&t, 0, VEG_SEED[3], t.clock));
    assert(!g_veg_poses);
    fail_render_calloc = false;
    render_work_caches(false); render_work_caches(true);
    for (int detail = SCENE_BALANCED; detail <= SCENE_ENHANCED; detail++) {
        if (detail == SCENE_SIMPLE) continue;
        render_device_t device = {0, 0, false, -1, (uint8_t)detail};
        render_set_device(&device);
        float rate = detail == SCENE_ENHANCED ? 10 : 5;
        t.clock = .01f;
        float held = secondary_clock(&t);
        t.clock = .04f;
        assert(secondary_clock(&t) == held);
        t.clock = 1 / rate + .01f;
        assert(secondary_clock(&t) > held);
        float sword_rate = detail == SCENE_ENHANCED ? 4 : 2;
        float initial = veg_clock(&t, 0, VEG_SEED[3]);
        for (int step = 0; step < 20; step++) {
            t.clock = step * .017f;
            for (int i = 0; i < VEG_FRONDS_MAX; i++)
                assert(veg_cached_sway(&t, i, VEG_SEED[3]) ==
                       veg_sway(&t, i, VEG_SEED[3], t.clock));
        }
        assert(initial == veg_clock(&t, .1f / sword_rate, VEG_SEED[3]));
        assert(initial < veg_clock(&t, 2 / sword_rate, VEG_SEED[3]));
        fish_t f = {0}; f.speed = 35; f.yaw = f.yaw_tail = 1;
        f.swim_phase = .01f * (8 + 35 * .055f);
        fish_motion_t a = fish_motion(&f);
        f.swim_phase = .04f * (8 + 35 * .055f);
        fish_motion_t b = fish_motion(&f);
        assert(a.tail != b.tail);
    }
    assert(g_veg_poses);
    render_work_caches(false);
    assert(!g_veg_poses && !render_work_cache_bytes());
    render_work_caches(true);
    fprintf(stderr, "PASS: one sword pose cache preserves independent scenery cadence and allocation fallback; fish tails keep full-rate motion\n");
}
static void check_damage_cost_plan(void) {
    int saved_policy = g_damage_policy;
    tank_resolution(true);
    render_damage_policy(RENDER_DAMAGE_ADAPTIVE);
    render_damage_costs(900, 200);
    for (int i = 0; i < 6; i++) g_damage_blocks[i] = 0x1f;
    damage_select_plan();
    for (int i = 0; i < 6; i++) assert(g_damage_blocks[i] == 0x1f);
    for (int i = 0; i < 6; i++) g_damage_blocks[i] = 0x155;
    render_damage_costs(900, 100000);
    damage_select_plan();
    for (int i = 0; i < 6; i++) assert(g_damage_blocks[i] == 0x1ff);
    for (int i = 0; i < 6; i++) g_damage_blocks[i] = 0x255;
    damage_select_plan();
    for (int i = 0; i < 6; i++) assert(g_damage_blocks[i] == 0x3ff);
    render_damage_costs(900, 4000);
    for (int i = 0; i < 6; i++) g_damage_blocks[i] = 0x155;
    damage_select_plan();
    for (int i = 0; i < 6; i++) assert(g_damage_blocks[i] == 0x1ff);
    memset(g_damage_blocks, 0, sizeof g_damage_blocks);
    g_damage_blocks[2] = 0x201;
    damage_select_plan();
    assert(g_damage_blocks[2] == 0x201);
    render_damage_policy(RENDER_DAMAGE_PARTIAL);
    for (int i = 0; i < 6; i++) g_damage_blocks[i] = 0x155;
    damage_select_plan();
    for (int i = 0; i < 6; i++) assert(g_damage_blocks[i] == 0x155);
    render_damage_policy(RENDER_DAMAGE_FULL);
    damage_select_plan();
    for (int i = 0; i < 6; i++) assert(g_damage_blocks[i] == 0x3ff);
    render_damage_costs(900, 4000);
    render_damage_policy(saved_policy);
    fprintf(stderr, "PASS: contiguous damage stays partial, fragmented bands join when cheaper, sparse distant spans stay separate, and full/partial overrides preserve plans\n");
}
static void check_fish_display_scale(void) {
    for (int normal = 0; normal < 2; normal++) {
        tank_resolution(normal);
        for (int stage = STAGE_FRY; stage <= STAGE_ELDER; stage++)
            for (int variety = 0; variety < 10; variety++) {
                tank_t t = {0};
                t.n_fish = 1; t.clock = 10;
                fish_t *f = &t.fish[0];
                f->x = TANK_W / 2; f->y = TANK_H / 2;
                f->size = .55f + stage * .25f; f->base_size = 1;
                f->stage = stage; f->variety = variety;
                f->color = 0xff8040; f->fin = 0x40aaff; f->accent = 0xffffff;
                f->yaw = f->yaw_tail = .9f;
                f->wearable = 1 + variety % (WEAR_COUNT - 1);
                tank_t original = t;
                memset(frame, 0, sizeof frame); memset(tiled, 0, sizeof tiled);
                ctx_t actual = ctx_full(frame, TANK_W, 1);
                ctx_t expected = ctx_full(tiled, TANK_W, 1);
                draw_fish(&actual, &t, f);
                fish_t smaller = *f;
                smaller.size *= .67f;
                draw_fish_core(&expected, &smaller, t.clock, false, 1);
                assert(!memcmp(frame, tiled, sizeof frame));
                assert(!memcmp(&t, &original, sizeof t));
            }
    }
    fprintf(stderr, "PASS: tank fish draw at exactly 67%% scale across all stages/varieties and both sizes without changing saved fish\n");
}
static void check_fish_turn_visibility(bool normal) {
    tank_resolution(normal);
    for (int stage = STAGE_FRY; stage <= STAGE_ELDER; stage++)
        for (int variety = 0; variety < 10; variety++)
            for (int scenario = 0; scenario < 8; scenario++) {
                fish_t f = {0};
                f.x = TANK_W / 2 + (scenario % 4) * .25f;
                f.y = TANK_H / 2 + (scenario % 3) * .33f;
                static const float stage_sizes[] = {.55f, .78f, 1.04f, 1.23f};
                f.size = .86f * stage_sizes[stage] * TANK_FISH_DISPLAY_SCALE;
                f.stage = stage; f.variety = variety;
                f.color = 0xff8040; f.fin = 0x40aaff; f.accent = 0xffffff;
                f.heading = scenario * .8f; f.speed = scenario % 2 ? 95 : 5;
                float direction = scenario % 2 ? -1 : 1;
                f.yaw_tail = direction;
                for (int pose = 0; pose <= 100; pose++) {
                    f.yaw = direction * (1 - pose * .02f);
                    f.yaw_tail += (f.yaw - f.yaw_tail) * .4f;
                    memset(frame, 0, sizeof frame);
                    ctx_t c = ctx_full(frame, TANK_W, 1);
                    draw_fish_core(&c, &f, 10 + pose * .04f, false, 1);
                    int body = 0;
                    for (int y = (int)f.y - 2; y <= (int)f.y + 2; y++)
                        for (int x = (int)f.x - 3; x <= (int)f.x + 3; x++) {
                            uint16_t pixel = frame[y * TANK_W + x];
                            body += pixel && pixel != rgb565(0xffffff, 1) && pixel != rgb565(0x031015, 1);
                        }
                    if (!body) fprintf(stderr, "Invisible turn body: stage %d variety %d scenario %d yaw %.2f size %.3f\n",
                                       stage, variety, scenario, f.yaw, f.size);
                    assert(body > 0);
                }
            }
    fprintf(stderr, "PASS: colored fish bodies survive both turn directions, tail lag, dives and subpixel positions at every stage/variety\n");
}
static void check_scaled_fish_bounds(bool normal) {
    tank_resolution(normal);
    for (int stage = STAGE_FRY; stage <= STAGE_ELDER; stage++)
        for (int variety = 0; variety < 10; variety++)
            for (int wear = 0; wear < WEAR_COUNT; wear++)
                for (int pose = 0; pose < 16; pose++) {
                    tank_t t = {0}; t.n_fish = 1; t.clock = pose * .13f;
                    fish_t *f = &t.fish[0];
                    f->x = TANK_W / 2 + .37f; f->y = TANK_H / 2 + .61f;
                    f->size = .55f + stage * .35f;
                    f->stage = stage; f->variety = variety; f->wearable = wear;
                    f->pattern = pose % FISH_PATTERN_COUNT; f->traits = pose;
                    f->color = 0xff8040; f->fin = 0x40aaff; f->accent = 0xffffff;
                    f->heading = pose * .4f; f->speed = pose % 2 ? 95 : 5;
                    f->yaw = -1 + pose * (2.0f / 15); f->yaw_tail = -f->yaw;
                    fish_bounds_t b = fish_bounds(&t, f);
                    memset(frame, 0, sizeof frame);
                    ctx_t c = ctx_full(frame, TANK_W, 1);
                    fish_t display = *f; display.size *= TANK_FISH_DISPLAY_SCALE;
                    draw_fish_core(&c, &display, t.clock, false, 1);
                    for (int y = 0; y < TANK_H; y++)
                        for (int x = 0; x < TANK_W; x++) if (frame[y * TANK_W + x]) {
                            assert(x >= floorf(b.left) && x <= ceilf(b.right));
                            assert(y >= floorf(b.top) && y <= ceilf(b.bottom));
                        }
                }
    fprintf(stderr, "PASS: scaled repaint bounds contain every fish/wearable pixel across stages, varieties and dive/bent-turn poses\n");
}
#endif

static void fixture(tank_t *t, bool normal, int group) {
    tank_resolution(normal);
    memset(t, 0, sizeof *t);
    tank_init(t, 0x5167);
#ifndef RENDER_BASELINE
    t->foliage = (foliage_state_t){1, (1u << FOLIAGE_COUNT) - 1, (1u << FOLIAGE_COUNT) - 1, 0};
#endif
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
    tank_snail_place(t);
    t->snail_x = TANK_W * .2f;
    if (group) t->snail_heading = 3.14159f;
    t->urchin_x = TANK_W * .7f;
    t->urchin_frond = -1;
    t->urchin_chew = 0;
    tank_seahorse_place(t);
    t->sh_x = TANK_W * .8f; t->sh_y = TANK_H * .7f;
    t->sh_mode = SH_PERCH; t->sh_akind = SH_ANCHOR_SAND; t->sh_land = SH_LAND_S;
    if (group) { t->sh_mode = SH_DRIFT; t->sh_akind = SH_ANCHOR_NONE; t->sh_vx = 18; t->sh_vy = 4; }
    tank_shrimp_place(t, SHRIMP_START);
    for (int i = 0; i < 4; i++) t->companion.creature_colors[i] = (uint8_t)((i + 1) | ((i + 6) << 4));
    if (group) {
        static const uint8_t items[] = {9, 10, 11, 12, 17, 17, 20, 21};
        t->mussel.version = 2; t->mussel.owned = 1;
        t->aquariums.clock_theme = CLOCK_ANALOG; t->clock_requested = true;
        for (int i = 0; i < DECOR_EXTRA_CAP; i++) {
            t->inventory.extra[i].item = items[i] + 1;
            t->inventory.extra[i].x = TANK_W * (i + 1) / (float)(DECOR_EXTRA_CAP + 1);
            t->inventory.extra[i].z = i % 2 ? DECOR_Z_FRONT : DECOR_Z_BACK;
        }
    }
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
#ifndef RENDER_BASELINE
    render_frame_begin(t);
#endif
    for (int y = 0; y < TANK_H; y += height) {
        int rows = TANK_H - y < height ? TANK_H - y : height;
        render_tile(y, rows);
        render_tank(t, fb + y * TANK_W, TANK_W);
    }
    render_tile(0, 0);
#ifndef RENDER_BASELINE
    render_frame_end();
#endif
}

#ifndef RENDER_BASELINE
static void check_retired_environment(void) {
    for (int normal = 0; normal < 2; normal++)
        for (int detail = 0; detail < SCENE_DETAIL_COUNT; detail++)
            for (int night = 0; night < 2; night++) {
                tank_t t;
                fixture(&t, normal != 0, 1);
                render_device_t device = {0, 0, false, -1, (uint8_t)detail};
                render_set_device(&device);
                t.night = t.light_manual_off = night != 0;
                render_tank(&t, frame, TANK_W);
                memset(t.algae, 255, sizeof t.algae);
                for (int b = 0; b < VEG_BEDS; b++) {
                    t.veg_growth[b] = 100;
                    for (int i = 0; i < VEG_FRONDS_MAX; i++) t.veg_h[b][i] = 100;
                }
                tank_t before = t;
                render_tank(&t, fish_frame, TANK_W);
                draw_tiles(&t, tiled, 7);
                assert(!memcmp(frame, fish_frame, (size_t)TANK_W * TANK_H * 2));
                assert(!memcmp(frame, tiled, (size_t)TANK_W * TANK_H * 2));
                assert(!memcmp(&before, &t, sizeof t));
            }
    fprintf(stderr, "PASS: retired raw algae/grass never draw or mutate, full/tile pixel parity at both scales, all details and day/night\n");
}

static int64_t tick_test_clock;
static int64_t tick_test_now(void) { tick_test_clock += 7; return tick_test_clock; }
static void check_tick_profile(void) {
    tank_t plain, profiled;
    tank_init(&plain, 0x5167);
    profiled = plain;
    memset(tank_profile_us, 0, sizeof tank_profile_us);
    tank_profile_clock_us = NULL;
    tank_tick(&plain, .04f, advisor_rules);
    tank_profile_clock_us = tick_test_now;
    tick_test_clock = 0;
    tank_tick(&profiled, .04f, advisor_rules);
    tank_profile_clock_us = NULL;
    assert(!memcmp(&plain, &profiled, sizeof plain));
    for (int i = 0; i < 3; i++) assert(tank_profile_us[i] == 7);
    memset(tank_profile_us, 0, sizeof tank_profile_us);
    fprintf(stderr, "PASS: simulation stage profiling preserves exact aquarium state and accounts for all three phases\n");
}
#endif
static void benchmark(int frames) {
#ifndef RENDER_BASELINE
    check_packed_spans();
    check_frame_clock();
    check_tick_profile();
    check_retired_environment();
    check_motion_cache();
    check_castle_cache();
    check_foreground_art();
    check_crown_cache();
    check_backdrop_codec();
    check_ornament_art();
    check_damage_merge();
    check_scenery_cadence();
    check_damage_cost_plan();
    check_fish_display_scale();
    for (int normal = 0; normal < 2; normal++) {
        check_fish_turn_visibility(normal != 0);
        check_scaled_fish_bounds(normal != 0);
    }
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
                memset(render_helper_us, 0, sizeof render_helper_us);
                render_clock_us = micros;
                int64_t start = micros();
                for (int i = 0; i < frames; i++) {
                    pose(&t, i);
                    draw_tiles(&t, tiled, 80);
                }
                int64_t elapsed = micros() - start;
                render_clock_us = NULL;
                static const int samples[] = {0, 59, 119, 239};
                uint64_t hashes[4];
                for (int sample = 0; sample < 4; sample++) {
                    pose(&t, samples[sample]);
                    draw_tiles(&t, tiled, 80);
                    render_tank(&t, frame, TANK_W);
                    assert(!memcmp(frame, tiled, (size_t)TANK_W * TANK_H * 2));
                    hashes[sample] = hash_frame(frame);
                }
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
                double helpers_isolated[3] = {0};
                if (detail != SCENE_SIMPLE) {
                    for (int helper = 0; helper < 3; helper++) {
                        tank_t sample = t;
                        int64_t helper_start = micros();
                        for (int i = 0; i < frames; i++) {
                            sample.clock = 10 + i * .04f;
                            ctx_t c = ctx_full(fish_frame, TANK_W, 1);
                            if (helper == 0) draw_snail(&c, &sample, !group);
                            else if (helper == 1) draw_urchin(&c, &sample);
                            else draw_seahorse(&c, &sample);
                        }
                        helpers_isolated[helper] = (double)(micros() - helper_start) / frames;
                    }
                }
                tank_t damage_sample = t;
                uint64_t damage_pixels = 0;
                unsigned damage_rectangles = 0;
                int damage_frames = frames < 240 ? frames : 240;
                render_damage_begin(&damage_sample, true);
                int64_t damage_start = micros();
                for (int i = 0; i < damage_frames; i++) {
                    pose(&damage_sample, i);
#ifndef RENDER_BASELINE
                    render_frame_begin(&damage_sample);
#endif
                    render_damage_begin(&damage_sample, false);
                    int cursor = 0, x, y, width, height;
                    while (render_damage_next(&cursor, &x, &y, &width, &height)) {
                        int bottom = y + height;
                        for (; y < bottom; y += 40) {
                        height = bottom - y < 40 ? bottom - y : 40;
                        damage_rectangles++;
                        damage_pixels += (uint64_t)width * height;
                        render_tile(y, height);
                        render_region(x, width);
                        render_tank(&damage_sample, fish_frame, TANK_W);
                        }
                    }
                    render_tile(0, 0); render_region(0, 0);
#ifndef RENDER_BASELINE
                    render_frame_end();
#endif
                }
                int64_t damage_elapsed = micros() - damage_start;
                printf("{\"width\":%d,\"detail\":%d,\"group\":%d,\"frames\":%d,"
                       "\"render_us\":%.3f,\"fish_us\":%.3f,\"hash\":\"%016" PRIx64 "\",\"stages_us\":[",
                       TANK_W, detail, group, frames, (double)elapsed / frames,
                       (double)fish_elapsed / frames, hash_frame(frame));
                for (int i = 0; i < 8; i++) printf("%s%.3f", i ? "," : "", (double)render_prof_us[i] / frames);
                printf("],\"helpers_us\":[%.3f,%.3f,%.3f],\"frame_hashes\":[",
                       (double)render_helper_us[0] / frames, (double)render_helper_us[1] / frames,
                       (double)render_helper_us[2] / frames);
                for (int i = 0; i < 4; i++) printf("%s\"%016" PRIx64 "\"", i ? "," : "", hashes[i]);
                printf("],\"helpers_isolated_us\":[%.3f,%.3f,%.3f],"
                       "\"damage_pixels\":%.3f,\"damage_rectangles\":%.3f,\"damage_us\":%.3f}\n",
                       helpers_isolated[0], helpers_isolated[1], helpers_isolated[2],
                       (double)damage_pixels / damage_frames, (double)damage_rectangles / damage_frames,
                       (double)damage_elapsed / damage_frames);
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
        t.progression.food_tier = (uint8_t)(checks % PROGRESSION_FOOD_TIER_COUNT);
        t.foliage = (foliage_state_t){1, (1u << FOLIAGE_COUNT) - 1, checks % (1u << FOLIAGE_COUNT), 0};
        static const float stage_sizes[] = {.55f, .78f, 1.04f, 1.23f};
        for (int i = 0; i < t.n_fish; i++) {
            t.fish[i].stage = (uint8_t)((checks + i) % 4);
            t.fish[i].size = .86f * stage_sizes[t.fish[i].stage];
        }
        static const uint8_t duplicates[] = {9, 10, 11, 12, 13, 16, 20, 21};
        int new_item = SD_DECOR_FIRST + (int)(checks % DECOR_NEW_COUNT);
        t.content.version = 1;
        t.content.owned = 1u << (new_item - SD_DECOR_FIRST);
        tank_decor_set(&t, new_item, TANK_W * .4f, checks % 2 ? DECOR_Z_FRONT : DECOR_Z_BACK);
        if (checks % 3) for (int i = 0; i < DECOR_EXTRA_CAP; i++) {
            t.inventory.extra[i].item = (uint8_t)((i < 2 ? new_item : duplicates[i]) + 1);
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
            t.foliage.planted = (uint8_t)((checks + step) % (1u << FOLIAGE_COUNT));
            if (step == 3) memset(&t.inventory.extra[0], 0, sizeof t.inventory.extra[0]);
            if (step == 4 && t.inventory.extra[1].item) t.inventory.extra[1].x += 12;
            if (step == 2) t.mussel.bank = 1;
            t.mussel_glint = step < 4 ? 1.6f - step * .4f : 0;
            if (step == 5) t.mussel.owned = 0;
            if (step == 5) t.content.owned = 0;
            if (step == 1) {
                tank_touch_drag(&t, TANK_W * .15f, TANK_H * .4f);
                tank_touch_drag(&t, TANK_W * .85f, TANK_H * .4f);
                uint32_t currents = t.reef_currents;
                for (int attempt = 0; attempt < 5; attempt++)
                    assert(!tank_reef_current(&t, 20, 50, TANK_W - 20, 50));
                assert(t.reef_currents == currents);
            }
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
#ifndef RENDER_BASELINE
            render_frame_begin(&t);
#endif
            render_damage_begin(&t, step == 0);
            int cursor = 0, x, y, width, height;
            while (render_damage_next(&cursor, &x, &y, &width, &height)) {
                assert(x >= 0 && width > 0 && x + width <= TANK_W);
                assert(y >= 0 && height > 0 && y + height <= TANK_H);
                int bottom = y + height;
                for (; y < bottom; y += 40) {
                height = bottom - y < 40 ? bottom - y : 40;
                render_tile(y, height);
                render_region(x, width);
                render_tank(&t, fish_frame, TANK_W);
                for (int row = 0; row < height; row++)
                    memcpy(retained + (y + row) * TANK_W + x,
                           fish_frame + row * TANK_W + x, (size_t)width * 2);
                }
            }
            render_tile(0, 0);
            render_region(0, 0);
#ifndef RENDER_BASELINE
            render_frame_end();
#endif
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
            " full/tile/retained frame sets; five fish, current gestures/cooldown spam, eight extra ornaments, both scales, all details, day/night, palettes and clock layouts; no tank mutation\n",
            seconds, checks, checks * 7);
}

int main(int argc, char **argv) {
    assert(argc == 4 || argc == 5);
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
#ifdef RENDER_EMBEDDED_ICONS
    check_embedded_icons();
#endif
    void *decor = calloc(1, render_decor_scratch_size());
    void *mask = calloc(1, render_mask_scratch_size());
    void *backdrop = malloc(12288);
    assert(decor && mask && backdrop);
    render_set_decor_scratch(decor);
    render_set_mask_scratch(mask);
    render_set_backdrop_cache(backdrop, 12288);
    render_work_caches(true);
    render_clock_synchronized(true);
#ifndef RENDER_BASELINE
    render_damage_policy(argc > 4 ? atoi(argv[4]) : RENDER_DAMAGE_ADAPTIVE);
#endif
    benchmark(atoi(argv[2]));
    soak(atoi(argv[3]));
    render_work_caches(false);
    render_set_backdrop_cache(NULL, 0);
    render_set_decor_scratch(NULL);
    render_set_mask_scratch(NULL);
    free(backdrop); free(mask); free(decor);
    return 0;
}
