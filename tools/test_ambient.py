"""Test CYD light-sensor normalization, filtering and night hysteresis."""
import argparse
from pathlib import Path
import subprocess
import tempfile


HARNESS = r"""
#undef NDEBUG
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "tank.h"
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGE(...) ((void)0)
enum { SCENE_BALANCED, SCENE_SIMPLE, SCENE_ENHANCED, SCENE_DETAIL_COUNT };
enum { ROOM_LIGHT_OFF, ROOM_LIGHT_DIM, ROOM_LIGHT_NIGHT };
enum { BACK_LED_OFF, BACK_LED_GLOW, BACK_LED_ALERTS };
static int s_room, s_led, s_dark_raw, s_lit_raw, raw;
static float s_base, s_base_saved, s_base_save_t;
static float s_raw_avg, s_sample_t, s_dark_for, s_lit_for, s_phase;
static bool s_dark;
static uint8_t s_scene_detail, saved_simple;
static bool fail_save;
static bool nvs_put(const char *key, uint8_t value) {
    assert(!strcmp(key, "simple"));
    if (fail_save) return false;
    saved_simple = value;
    return true;
}
static int64_t s_override_until;
static float led_level;
static int led_r, led_g, led_b;
typedef int nvs_handle_t;
#define ESP_OK 0
#define NVS_READWRITE 1
static int nvs_open(const char *n, int mode, nvs_handle_t *h) {
    (void)n; (void)mode; *h = 1; return 0;
}
static int nvs_set_u16(nvs_handle_t h, const char *k, uint16_t v) {
    (void)h; (void)k; (void)v; return 0;
}
static int nvs_commit(nvs_handle_t h) { (void)h; return 0; }
static void nvs_close(nvs_handle_t h) { (void)h; }
static int ambient_ldr_raw(void) { return raw; }
static int64_t esp_timer_get_time(void) { return 0; }
static void ambient_led(int r, int g, int b) { led_r = r; led_g = g; led_b = b; }
static void led_from_rgb(uint32_t rgb, float level) { (void)rgb; led_level = level; }
static uint32_t saturate(uint32_t rgb) { return rgb; }
static uint32_t render_glow_rgb(const tank_t *t) { (void)t; return 0; }
"""

TEST = r"""
static void reset(void) {
    s_room = ROOM_LIGHT_NIGHT; s_led = BACK_LED_OFF;
    s_base = s_base_saved = 23; s_raw_avg = 250;
    s_sample_t = s_dark_for = s_lit_for = s_base_save_t = 0;
    s_dark_raw = s_lit_raw = 0; s_dark = false;
    raw = 250;
}
static void run(tank_t *t, float seconds, float dt) {
    for (int i = 0; i < (int)lroundf(seconds / dt); i++) ambient_tick(t, dt);
}
int main(void) {
    tank_t tank = {0};
    assert(ambient_scene_detail() == SCENE_BALANCED);
    assert(ambient_set_scene_detail(SCENE_SIMPLE) && saved_simple == SCENE_SIMPLE && ambient_scene_detail() == SCENE_SIMPLE);
    fail_save = true;
    assert(!ambient_set_scene_detail(SCENE_ENHANCED) && saved_simple == SCENE_SIMPLE && ambient_scene_detail() == SCENE_SIMPLE);
    fail_save = false;
    assert(ambient_set_scene_detail(SCENE_ENHANCED) && saved_simple == SCENE_ENHANCED && ambient_scene_detail() == SCENE_ENHANCED);
    assert(!ambient_set_scene_detail(-1) && !ambient_set_scene_detail(SCENE_DETAIL_COUNT));
    assert(ambient_set_scene_detail(SCENE_BALANCED) && saved_simple == SCENE_BALANCED);
    reset();
    assert(ambient_room_pct() >= 85);
    assert(ambient_brightness_pct(100) >= 70);
    raw = 350;
    run(&tank, 2, 0.25f);
    assert(ambient_brightness_pct(100) >= 65 && !ambient_force_night());
    reset();
    raw = 1500;
    run(&tank, 1, 0.25f);
    assert(ambient_brightness_pct(100) >= 30 && ambient_brightness_pct(100) <= 40 && !ambient_force_night());
    raw = 250;
    run(&tank, 30, 0.25f);
    assert(ambient_brightness_pct(100) >= 70 && !ambient_force_night());
    raw = 1500;
    run(&tank, 25, 0.25f);
    assert(!ambient_force_night());
    run(&tank, 35, 0.25f);
    assert(ambient_force_night() && ambient_brightness_pct(100) == 2);
    raw = 250;
    run(&tank, 40, 0.25f);
    assert(!ambient_force_night());
    s_room = ROOM_LIGHT_OFF;
    assert(ambient_brightness_pct(60) == 60 && !ambient_force_night());
    s_room = ROOM_LIGHT_DIM; s_dark = true;
    assert(!ambient_force_night());
    s_dark_raw = 1500; s_lit_raw = 250; s_raw_avg = 875;
    assert(ambient_room_pct() == 50);
    reset(); raw = 1500; s_raw_avg = 1500;
    run(&tank, 29, 1);
    assert(!ambient_force_night());
    run(&tank, 1, 1);
    assert(ambient_force_night());
    s_room = ROOM_LIGHT_DIM;
    s_dark_raw = 1500; s_lit_raw = 250;
    int previous = 0;
    for (int room = 0; room <= 100; room++) {
        s_raw_avg = 1500 - room * 12.5f;
        int screen = ambient_brightness_pct(100);
        assert(screen >= previous && screen <= 100);
        previous = screen;
        for (int mode = BACK_LED_GLOW; mode <= BACK_LED_ALERTS; mode++)
            for (int signal = 0; signal < 3; signal++)
                for (int night = 0; night < 2; night++) {
                    tank.night = night; tank.ravenous = signal == 1;
                    tank.visitor_left = signal == 2 ? 10 : 0;
                    s_led = mode; s_phase = 0;
                    ambient_tick(&tank, 0);
                    float dimmed = led_level;
                    s_room = ROOM_LIGHT_OFF;
                    ambient_tick(&tank, 0);
                    float full = led_level;
                    s_room = ROOM_LIGHT_DIM;
                    assert(fabsf(dimmed - full * room_light_gain()) < .00001f);
                }
        if (room == 0) assert(screen == 2 && ambient_brightness_pct(10) == 1);
        if (room == 25) assert(screen == 8 && ambient_brightness_pct(60) == 5);
        if (room == 50) assert(screen == 27 && ambient_brightness_pct(60) == 16);
        if (room == 100) assert(screen == 100 && ambient_brightness_pct(60) == 60);
    }
    s_raw_avg = -1;
    assert(ambient_brightness_pct(60) == 60 && room_light_gain() == 1);
    s_led = BACK_LED_OFF;
    ambient_tick(&tank, 0);
    assert(!led_r && !led_g && !led_b);
    puts("PASS: shared quadratic screen/LED dimming, exact low-light thresholds, monotonic curve, glow/alerts/day/night, OFF/unavailable bypass, filtered shadows and night hysteresis");
}
"""


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default="cc")
    args = parser.parse_args()
    root = Path(__file__).resolve().parent.parent
    firmware = root / "firmware" / "main"
    source = (firmware / "ambient.c").read_text()
    pins = (firmware / "board_pins.h").read_text()
    floor = next(line for line in pins.splitlines()
                 if line.startswith("#define CYD_LDR_MIN_BASE"))
    policy = source[source.index("#define LDR_DARK_RATIO"):
                    source.index("bool ambient_room_dark(")]
    scene = source[source.index("int ambient_scene_detail("):
                   source.index("bool ambient_set_room_mode(")]
    filtering = source[source.index("static void learn_base("):
                       source.index("static void led_from_rgb(")]
    tick = source[source.index("void ambient_tick("):source.index("void ambient_log(")]
    with tempfile.TemporaryDirectory(prefix="cyd-ambient-test-") as folder:
        folder = Path(folder)
        test = folder / "test.c"
        test.write_text(HARNESS + floor + "\n" + scene + policy + filtering + tick + TEST)
        executable = folder / "test.exe"
        command = [args.cc]
        if Path(args.cc).stem.lower() == "zig":
            command.append("cc")
        subprocess.run(command + ["-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
                                  "-I" + str(root / "common"), str(test),
                                  "-o", str(executable), "-lm"], check=True)
        subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    main()
