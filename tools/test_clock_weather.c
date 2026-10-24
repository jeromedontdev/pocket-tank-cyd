#include "render.h"
#include "weather.h"
#undef NDEBUG
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void test_clock_weather(void) {
    tank_t tank;
    tank_init(&tank, 42);
    tank_new_population(&tank);
    tank.companion.lively = 0;
    tank.companion.cosmetic = 3;
    tank.fish[0].hunger = 8;
    weather_config_t config = {0};
    config.version = 1; config.located = 1; config.fahrenheit = 1;
    strcpy(config.city.name, "Saint-Remy-en-Bouzemont-Saint-Genest");
    const char *path = getenv("POCKET_TANK_WEATHER");
    assert(path);
    int STRIDE = TANK_W + 3, PIXELS = STRIDE * TANK_H;
    uint16_t *frame = malloc((PIXELS + 2) * sizeof *frame);
    uint16_t *cached = malloc((PIXELS + 2) * sizeof *cached);
    assert(frame && cached);
    for (int weather = 0; weather < 2; weather++) for (int activity = 0; activity < 2; activity++) {
        config.enabled = (uint8_t)weather;
        FILE *file = fopen(path, "wb"); assert(file);
        assert(fwrite(&config, 1, sizeof config, file) == sizeof config && !fclose(file));
        weather_init();
        tank.companion.captions = (uint8_t)activity;
        for (int theme = CLOCK_COMPACT; theme < CLOCK_THEME_COUNT; theme++)
            for (int size = 0; size < CLOCK_SIZE_COUNT; size++)
                for (int position = 0; position < CLOCK_POSITION_COUNT; position++)
                    for (int hidden = 0; hidden < 8; hidden++) for (int valid = 0; valid < 2; valid++) {
                        tank.aquariums.clock_theme = (uint8_t)theme;
                        tank.aquariums.clock_layout = clock_layout_set_size((uint8_t)(position << 2), size);
                        tank.aquariums.clock_flags = (uint8_t)(hidden << 2);
                        render_clock_layout_t l; render_clock_layout(&tank, &l);
                        assert(l.rows == weather * (size == CLOCK_JUMBO ? 2 : 3) + activity);
                        assert(l.x >= 6 && l.y >= 6 && l.x + l.w <= TANK_W - 6 && l.y + l.h <= TANK_H - 6);
                        assert(l.face_x >= l.x && l.face_y == l.y && l.face_x + l.face_w <= l.x + l.w);
                        if (l.rows) {
                            static const int widths[] = {220, 172, 172, 0, 172};
                            assert(l.w == (size == CLOCK_JUMBO ? TANK_W - 12 : widths[size]));
                            assert(l.caption_scale == 1 && l.caption_pitch == 12);
                            assert(l.caption_h == l.rows * 12 + 4);
                            assert(l.caption_y == l.face_y + l.face_h + 4);
                            assert(l.caption_y + l.caption_h == l.y + l.h);
                            assert(l.caption_y + 4 + (l.rows - 1) * l.caption_pitch + 7 * l.caption_scale < l.y + l.h);
                        }
                        if (position == CLOCK_TOP_LEFT || position == CLOCK_BOTTOM_LEFT) assert(l.x == 6);
                        if (position == CLOCK_TOP_RIGHT || position == CLOCK_BOTTOM_RIGHT) assert(l.x + l.w == TANK_W - 6);
                        if (position == CLOCK_TOP_LEFT || position == CLOCK_TOP_RIGHT) assert(l.y == 6);
                        if (position == CLOCK_BOTTOM_LEFT || position == CLOCK_BOTTOM_RIGHT) assert(l.y + l.h == TANK_H - 6);
                        memset(frame, 0, (PIXELS + 2) * sizeof *frame);
                        frame[0] = frame[PIXELS + 1] = 0xbeef;
                        render_clock_overlay(&tank, frame + 1, STRIDE, valid ? INT64_C(1735689600) : 0);
                        for (int y = 0; y < TANK_H; y++) for (int x = 0; x < STRIDE; x++)
                            if (x < l.x || x >= l.x + l.w || y < l.y || y >= l.y + l.h)
                                assert(!frame[1 + y * STRIDE + x]);
                        if (l.rows) {
                            assert(frame[1 + l.caption_y * STRIDE + l.x] != 0);
                            assert(frame[1 + (l.caption_y + l.caption_h - 1) * STRIDE + l.x + l.w - 1] != 0);
                        }
                        assert(frame[0] == 0xbeef && frame[PIXELS + 1] == 0xbeef);
                        memset(cached, 0, (PIXELS + 2) * sizeof *cached);
                        cached[0] = cached[PIXELS + 1] = 0xbeef;
                        render_work_caches(true);
                        render_clock_overlay(&tank, cached + 1, STRIDE, valid ? INT64_C(1735689600) : 0);
                        if (memcmp(frame, cached, (PIXELS + 2) * sizeof *frame)) {
                            fprintf(stderr, "First clock cache mismatch theme %d size %d position %d flags %d valid %d\n", theme, size, position, hidden, valid);
                            assert(false);
                        }
                        for (int i = 0; i < PIXELS; i++) frame[i + 1] = cached[i + 1] = (uint16_t)(i * 13 + 0x5138);
                        render_work_caches(false);
                        render_clock_overlay(&tank, frame + 1, STRIDE, valid ? INT64_C(1735689600) : 0);
                        render_work_caches(true);
                        for (int y = 0; y < TANK_H; y += 60) {
                            render_tile(y, 60);
                            render_clock_overlay(&tank, cached + 1 + y * STRIDE, STRIDE, valid ? INT64_C(1735689600) : 0);
                        }
                        render_tile(0, 0);
                        if (memcmp(frame, cached, (PIXELS + 2) * sizeof *frame)) {
                            fprintf(stderr, "Clock cache mismatch theme %d size %d position %d flags %d valid %d\n", theme, size, position, hidden, valid);
                            assert(false);
                        }
                        render_work_caches(false);
                    }
    }
    size_t case_pixels = (size_t)STRIDE * TANK_H;
    uint16_t *expected = malloc(24 * case_pixels * sizeof *expected); assert(expected);
    for (int pass = 0; pass < 2; pass++) {
        render_work_caches(pass != 0);
        for (int step = 0; step < 24; step++) {
            tank.aquariums.clock_theme = (uint8_t)(CLOCK_COMPACT + step % (CLOCK_THEME_COUNT - 1));
            tank.aquariums.clock_layout = clock_layout_set_size((uint8_t)((step % CLOCK_POSITION_COUNT) << 2), step % CLOCK_SIZE_COUNT);
            tank.aquariums.clock_flags = step % 2 ? CLOCK_HIDE_SECONDS : CLOCK_12H;
            tank.aquariums.clock_utc_quarters = (int8_t)(step - 12);
            tank.aquariums.clock_delay = (uint8_t)((step % 8) << CLOCK_SECONDARY_SHIFT);
            tank.companion.cosmetic = (uint8_t)(step % 4);
            tank.companion.captions = (uint8_t)(step % 2);
            for (int repeat = 0; repeat < (pass ? 2 : 1); repeat++) {
                for (size_t p = 0; p < case_pixels; p++) frame[p + 1] = (uint16_t)(p * 31 + step * 19);
                int64_t now = INT64_C(1735689590) + step * 7;
                render_clock_overlay(&tank, frame + 1, STRIDE, now);
                if (!pass) memcpy(expected + step * case_pixels, frame + 1, case_pixels * sizeof *frame);
                else assert(!memcmp(expected + step * case_pixels, frame + 1, case_pixels * sizeof *frame));
            }
        }
    }
    render_work_caches(false);
    int original_width = TANK_W;
    config.enabled = 1;
    strcpy(config.city.name, "A ABCDEFGHIJKLMNOPQRSTUVWXY Z QRSTUVWX");
    FILE *file = fopen(path, "wb"); assert(file);
    assert(fwrite(&config, 1, sizeof config, file) == sizeof config && !fclose(file));
    weather_init(); tank.companion.captions = 1;
    for (int compact = 0; compact < 2; compact++) {
        tank_resolution(compact != 0);
        for (int theme = CLOCK_COMPACT; theme < CLOCK_THEME_COUNT; theme++)
            for (int size = 0; size < CLOCK_SIZE_COUNT; size++)
                for (int position = 0; position < CLOCK_POSITION_COUNT; position++) {
                    tank.aquariums.clock_theme = (uint8_t)theme;
                    tank.aquariums.clock_layout = clock_layout_set_size((uint8_t)(position << 2), size);
                    render_clock_layout_t l; render_clock_layout(&tank, &l);
                    assert(l.rows >= (size == CLOCK_JUMBO ? 2 : 3) && l.rows <= 5);
                    assert(l.x >= 6 && l.y >= 6 && l.x + l.w <= TANK_W - 6 && l.y + l.h <= TANK_H - 6);
                }
        tank.clock = 600;
        tank.clock_idle_s = 600;
        tank.aquariums.clock_delay = 1;
        tank.aquariums.clock_theme = CLOCK_COMPACT;
        tank.aquariums.clock_layout = 0;
        assert(render_clock_active(&tank));
        size_t n = (size_t)TANK_W * TANK_H;
        uint16_t *plain = malloc(n * sizeof *plain), *sparkles = malloc(n * sizeof *sparkles);
        assert(plain && sparkles);
        render_clock_layout_t layout; render_clock_layout(&tank, &layout);
        tank.companion.lively = 0; render_tank(&tank, plain, TANK_W);
        tank.companion.lively = 1; render_tank(&tank, sparkles, TANK_W);
        int changed = 0, bright = 0;
        for (int y = 0; y < TANK_H; y++) for (int x = 0; x < TANK_W; x++) {
            int p = y * TANK_W + x;
            if (plain[p] == sparkles[p]) continue;
            assert(x < layout.x || x >= layout.x + layout.w || y < layout.y || y >= layout.y + layout.h);
            changed++;
            if ((sparkles[p] >> 11) >= 20 && ((sparkles[p] >> 5) & 63) >= 40) bright++;
        }
        assert(changed >= 64 && bright >= 32);
        tank.clock += .5f;
        render_tank(&tank, plain, TANK_W);
        assert(memcmp(plain, sparkles, n * sizeof *plain));
        free(plain); free(sparkles);
        tank.companion.lively = 0;
    }
    config.enabled = 0;
    file = fopen(path, "wb"); assert(file);
    assert(fwrite(&config, 1, sizeof config, file) == sizeof config && !fclose(file));
    weather_init();
    tank.companion.captions = tank.companion.cosmetic = 0;
    tank.aquariums.clock_theme = CLOCK_COMPACT;
    tank.aquariums.clock_flags = CLOCK_HIDE_PANEL | CLOCK_HIDE_DATE | CLOCK_HIDE_SECONDS;
    tank.aquariums.clock_utc_quarters = tank.aquariums.clock_delay = 0;
    for (int compact = 0; compact < 2; compact++) {
        tank_resolution(compact != 0);
        size_t n = (size_t)TANK_W * TANK_H;
        tank.aquariums.clock_theme = CLOCK_COMPACT;
        tank.aquariums.clock_flags = CLOCK_HIDE_PANEL | CLOCK_HIDE_DATE | CLOCK_HIDE_SECONDS;
        tank.aquariums.clock_delay = 0;
        uint16_t *pixels = calloc(n, sizeof *pixels); assert(pixels);
        int large_height = 0;
        for (int jumbo = 0; jumbo < 2; jumbo++) {
            tank.aquariums.clock_layout = jumbo ? CLOCK_JUMBO : CLOCK_LARGE;
            memset(pixels, 0, n * sizeof *pixels);
            render_clock_overlay(&tank, pixels, TANK_W, INT64_C(1735689600));
            int left = TANK_W, right = 0, top = TANK_H, bottom = 0;
            for (int y = 0; y < TANK_H; y++) for (int x = 0; x < TANK_W; x++)
                if (pixels[y * TANK_W + x] == 0xffff) {
                    if (x < left) left = x;
                    if (x > right) right = x;
                    if (y < top) top = y;
                    if (y > bottom) bottom = y;
                }
            if (!jumbo) large_height = bottom - top + 1;
            else {
                assert(right - left + 1 >= TANK_W * 85 / 100);
                assert(bottom - top + 1 >= TANK_H * 70 / 100);
                assert(bottom - top + 1 >= large_height * 3);
            }
        }
        free(pixels);
        size_t bytes = n * sizeof(uint16_t);
        uint16_t *themes = calloc(CLOCK_THEME_COUNT - 1, bytes); assert(themes);
        tank.aquariums.clock_layout = CLOCK_JUMBO;
        tank.aquariums.clock_flags = CLOCK_HIDE_PANEL | (1 << CLOCK_COLOR_SHIFT);
        tank.aquariums.clock_delay = 1 << CLOCK_SECONDARY_SHIFT;
        for (int theme = CLOCK_COMPACT; theme < CLOCK_THEME_COUNT; theme++) {
            tank.aquariums.clock_theme = (uint8_t)theme;
            uint16_t *image = themes + (theme - 1) * n;
            render_clock_overlay(&tank, image, TANK_W, INT64_C(1735745662));
            for (int previous = CLOCK_COMPACT; previous < theme; previous++)
                assert(memcmp(image, themes + (previous - 1) * n, bytes));
        }
        free(themes);
    }
    tank_resolution(original_width == 320);
    config.enabled = 1;
    file = fopen(path, "wb"); assert(file);
    assert(fwrite(&config, 1, sizeof config, file) == sizeof config && !fclose(file));
    weather_init(); tank.companion.captions = 1;
    int enlarged_details = 0, enlarged_captions = 0;
    for (int compact = 0; compact < 2; compact++) {
        tank_resolution(compact != 0);
        int stride = TANK_W + 3;
        size_t n = (size_t)stride * TANK_H;
        uint16_t *pixels = calloc(n, sizeof *pixels), *reference = calloc(n, sizeof *reference);
        assert(pixels && reference);
        for (int theme = CLOCK_COMPACT; theme < CLOCK_THEME_COUNT; theme++)
            for (int size = 0; size < CLOCK_SIZE_COUNT; size++)
                for (int position = 0; position < CLOCK_POSITION_COUNT; position++)
                    for (int detail = 0; detail < CLOCK_TEXT_SIZE_COUNT; detail++)
                        for (int caption = 0; caption < CLOCK_TEXT_SIZE_COUNT; caption++)
                            for (int valid = 0; valid < 2; valid++) {
                                tank.aquariums.clock_theme = (uint8_t)theme;
                                tank.aquariums.clock_layout = clock_layout_set_size((uint8_t)((position << 2) | (detail << CLOCK_DETAIL_SHIFT)), size);
                                tank.aquariums.clock_delay = (uint8_t)((caption << CLOCK_CAPTION_SHIFT) | (4 << CLOCK_SECONDARY_SHIFT));
                                tank.aquariums.clock_flags = CLOCK_12H | CLOCK_HIDE_PANEL | (1 << CLOCK_COLOR_SHIFT);
                                render_clock_layout_t l; render_clock_layout(&tank, &l);
                                assert(l.x >= 6 && l.y >= 6 && l.x + l.w <= TANK_W - 6 && l.y + l.h <= TANK_H - 6);
                                assert(l.detail_scale >= 1 && l.detail_scale <= detail + 1);
                                assert(l.caption_scale >= 1 && l.caption_scale <= caption + 1);
                                assert(l.rows > 0 && l.rows <= 9);
                                assert(l.caption_y >= l.face_y + l.face_h + 4);
                                assert(l.caption_pitch >= 7 * l.caption_scale + 1);
                                assert(l.caption_y + 4 + (l.rows - 1) * l.caption_pitch + 7 * l.caption_scale < l.y + l.h);
                                enlarged_details += l.detail_scale > 1;
                                enlarged_captions += l.caption_scale > 1;
                                memset(pixels, 0, n * sizeof *pixels);
                                render_work_caches(false);
                                render_clock_overlay(&tank, pixels, stride, valid ? INT64_C(1735745662) : 0);
                                for (int y = 0; y < TANK_H; y++) for (int x = 0; x < stride; x++) {
                                    if (x < l.x || x >= l.x + l.w || y < l.y || y >= l.y + l.h) assert(!pixels[y * stride + x]);
                                    if (pixels[y * stride + x] == 0xffff && y >= l.face_y + l.face_h - 28 * l.detail_scale &&
                                        y < l.face_y + l.face_h && theme != CLOCK_ANALOG)
                                        assert(false); /* White main-time pixels must not intrude into the cyan detail rows. */
                                }
                                memset(reference, 0, n * sizeof *reference);
                                render_work_caches(true);
                                for (int y = 0; y < TANK_H; y += 60) {
                                    render_tile(y, 60);
                                    render_clock_overlay(&tank, reference + y * stride, stride, valid ? INT64_C(1735745662) : 0);
                                }
                                render_tile(0, 0); render_work_caches(false);
                                assert(!memcmp(pixels, reference, n * sizeof *pixels));
                            }
        free(pixels); free(reference);
    }
    assert(enlarged_details > 0 && enlarged_captions > 0);
    tank_resolution(original_width == 320);
    free(expected); free(cached); free(frame); remove(path); weather_init();
    puts("PASS: 7 distinct Jumbo themes, 5 clock sizes, 3 detail/caption sizes, both resolutions, 5 positions, long wrapped captions, no time/detail overlap, live/tiled cache equality and stride guards");
}
