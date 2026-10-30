"""Exercise the actual CYD settings-action handoff with a native compiler."""
import argparse
from pathlib import Path
import subprocess
import tempfile


HARNESS = r"""
#undef NDEBUG
#include <assert.h>
#include <stdio.h>
#include "render.h"
#define ESP_LOGI(...) ((void)0)
static bool s_set, s_ms, s_life, s_back;
static bool s_shop, s_upd;
static int s_sel, s_set_what, s_set_val, result, result_value;
static int calibrated, confirmed, update_reset, dismissed, synced, primed;
static void touch_port_dismiss(void) { dismissed++; }
static void setup_cancel(tank_t *t) { (void)t; }
static void notice_sync(tank_t *t) { (void)t; synced++; }
void render_fb_primed(const uint16_t *fb, unsigned epoch) { (void)fb; (void)epoch; primed++; }
static bool progression_setup_pending(void) { return false; }
static void setup_begin(tank_t *t) { (void)t; }
int render_settings_touch(tank_t *t, float x, float y, bool down, int *value) {
    (void)t; (void)x; (void)y;
    *value = result_value;
    return down ? SET_TAP_NONE : result;
}
static void touch_port_calibrate(void) { calibrated++; }
static void touch_port_confirm_open(void) { confirmed++; }
static void sd_port_update_reset(void) { update_reset++; }
"""

TEST = r"""
int main(void) {
    tank_t tank = {0};
    const int forwarded[] = {
        SET_TAP_BRIGHT, SET_TAP_BRIGHT_PREVIEW, SET_TAP_LIGHT, SET_TAP_IDLE,
        SET_TAP_FEED, SET_TAP_BACKUP, SET_TAP_RESTORE, SET_TAP_SAND,
        SET_TAP_WATER, SET_TAP_WIFI, SET_TAP_ROOM, SET_TAP_LED,
        SET_TAP_AUTO_BACKUP, SET_TAP_PHOTO, SET_TAP_DETAIL
    };
    for (unsigned i = 0; i < sizeof forwarded / sizeof *forwarded; i++) {
        result = forwarded[i]; result_value = (int)i + 1;
        s_set_what = s_set_val = 0;
        settings_touch(&tank, 100, 100, true);
        assert(!s_set_what && !s_set_val);
        settings_touch(&tank, 100, 100, false);
        int value = 0;
        assert(touch_port_take_setting(&value) == result && value == result_value);
        assert(touch_port_take_setting(&value) == SET_TAP_NONE);
    }
    const int local[] = {SET_TAP_NONE, SET_TAP_CLOSE, SET_TAP_CLOCK_SHOW,
                         SET_TAP_TOUCH, SET_TAP_WIPE, SET_TAP_UPDATES, SET_TAP_RECOVERED};
    for (unsigned i = 0; i < sizeof local / sizeof *local; i++) {
        result = local[i];
        settings_touch(&tank, 100, 100, false);
        int value;
        assert(touch_port_take_setting(&value) == SET_TAP_NONE);
    }
    assert(calibrated == 1 && confirmed == 1 && update_reset == 1);
    assert(dismissed == 1 && synced == 1 && primed == 1);
    puts("PASS: CYD settings forwards every device action once and retains local actions");
}
"""


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default="cc")
    args = parser.parse_args()
    root = Path(__file__).resolve().parent.parent
    source = (root / "firmware" / "main" / "touch_port_xpt2046.c").read_text()
    handler = source[source.index("static void settings_touch("):
                     source.index("void touch_port_life_tap(")]
    start = source.index("int  touch_port_take_setting(")
    take = source[start:source.index("\n", start)]
    with tempfile.TemporaryDirectory(prefix="cyd-settings-test-") as folder:
        folder = Path(folder)
        test = folder / "test.c"
        test.write_text(HARNESS + handler + take + TEST)
        executable = folder / "test.exe"
        command = [args.cc]
        if Path(args.cc).stem.lower() == "zig":
            command.append("cc")
        subprocess.run(command + ["-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
                                  "-I" + str(root / "common"), str(test),
                                  "-o", str(executable)], check=True)
        subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    main()
