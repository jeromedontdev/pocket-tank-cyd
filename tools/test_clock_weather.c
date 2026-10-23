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
    tank.companion.lively = 0;
    tank.companion.cosmetic = 3;
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
                        tank.aquariums.clock_layout = (uint8_t)((position << 2) | size);
                        tank.aquariums.clock_flags = (uint8_t)(hidden << 2);
                        render_clock_layout_t l; render_clock_layout(&tank, &l);
                        assert(l.rows == weather * 2 + activity);
                        assert(l.x >= 6 && l.y >= 6 && l.x + l.w <= TANK_W - 6 && l.y + l.h <= TANK_H - 6);
                        assert(l.face_x >= l.x && l.face_y == l.y && l.face_x + l.face_w <= l.x + l.w);
                        if (l.rows) {
                            static const int widths[] = {220, 172, 116};
                            static const int scales[] = {2, 1, 1}, pitches[] = {16, 12, 10};
                            assert(l.w == widths[size]);
                            assert(l.caption_scale == scales[size] && l.caption_pitch == pitches[size]);
                            assert(l.caption_h == l.rows * pitches[size] + 4);
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
            tank.aquariums.clock_layout = (uint8_t)((step % CLOCK_POSITION_COUNT) << 2 | step % CLOCK_SIZE_COUNT);
            tank.aquariums.clock_flags = step % 2 ? CLOCK_HIDE_SECONDS : CLOCK_12H;
            tank.aquariums.clock_utc_quarters = (int8_t)(step - 12);
            tank.aquariums.clock_delay = (uint8_t)((step % 7) << 4);
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
    free(expected); free(cached); free(frame); remove(path); weather_init();
    puts("PASS: clock/weather/activity exact bounds: 7 themes, 3 sizes, 5 positions, all detail flags, synced/unsynced, stride guards");
}
