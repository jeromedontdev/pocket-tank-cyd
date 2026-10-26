"""Test actual touch defaults and calibrated coordinate transforms for both boards."""
import argparse
from pathlib import Path
import subprocess
import tempfile

HARNESS = r"""
#undef NDEBUG
#include <assert.h>
#include <stdbool.h>
#include <math.h>
#include <stdio.h>
#include "board_hardware.h"
#if PT_RUNTIME_HARDWARE
const board_hardware_t *board_hardware_current(void) {
    return board_hardware_lookup(PT_BOARD_ELEGOO ? BOARD_HARDWARE_ELEGOO : BOARD_HARDWARE_CYD);
}
#endif
static int TANK_W = 240, TANK_H = 180, s_bias_y;
static bool s_inverted;
"""

TEST = r"""
int main(void) {
    cyd_cal_default();
    float x, y;
    cal_point(40, 20, &x, &y);
    assert(x == (PT_BOARD_ELEGOO ? 199 : 40));
    assert(y == 20);
    cal_point(239, 179, &x, &y);
    assert(x == (PT_BOARD_ELEGOO ? 0 : 239) && y == 179);
    cal_point(0, 0, &x, &y);
    assert(x == (PT_BOARD_ELEGOO ? 239 : 0) && y == 0);
    for (int size = 0; size < 2; size++) {
        TANK_W = size ? 320 : 240; TANK_H = size ? 240 : 180;
        cal_view(40, 20, &x, &y);
        float upright_x = x, upright_y = y;
        s_inverted = true;
        cal_view(40, 20, &x, &y);
        assert(fabsf(x - (TANK_W - 1 - upright_x)) < 0.001f);
        assert(fabsf(y - (TANK_H - 1 - upright_y)) < 0.001f);
        s_inverted = false;
    }
    /* Previously working ELEGOO calibration has negative X and positive Y.
       A fresh default must agree in direction, not flip the opposite axis. */
    if (PT_BOARD_ELEGOO) {
        const cyd_cal_t measured = {
            {-1.0133768320f, 0.0146672642f, 246.5437775f},
            {0.0024547693f, 1.0117053986f, -0.0095686167f},
        };
        for (int size = 0; size < 2; size++) {
            TANK_W = size ? 320 : 240; TANK_H = size ? 240 : 180;
            for (int side = 0; side < 2; side++)
                for (int bottom = 0; bottom < 2; bottom++) {
                    float rx = side ? 209 : 30, ry = bottom ? 149 : 30;
                    s_calibration = measured;
                    float measured_x, measured_y;
                    cal_point(rx, ry, &measured_x, &measured_y);
                    cyd_cal_default();
                    cal_point(rx, ry, &x, &y);
                    assert(fabsf(x - measured_x) < 12 * (TANK_W - 1) / 239);
                    assert(fabsf(y - measured_y) < 3 * (TANK_H - 1) / 179);
                }
        }
    }
    TANK_W = 240; TANK_H = 180;
    /* A saved calibration replaces defaults, without changing its input basis. */
    s_calibration = (cyd_cal_t){{0.95f, 0.02f, 3}, {0.01f, -0.96f, 176}};
    cal_point(40, 20, &x, &y);
    assert(fabsf(x - 41.4f) < 0.001f && fabsf(y - 157.2f) < 0.001f);
    TANK_W = 320; TANK_H = 240;
    cal_point(40, 20, &x, &y);
    assert(fabsf(x - 41.4f * 319 / 239) < 0.001f);
    assert(fabsf(y - 157.2f * 239 / 179) < 0.001f);
    puts("PASS: board-specific touch defaults and existing affine calibration at both UI sizes");
}
"""


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default="cc")
    args = parser.parse_args()
    source = (Path(__file__).resolve().parent.parent / "firmware" / "main" / "touch_port_xpt2046.c").read_text()
    defaults = source[source.index("typedef struct { float x[3], y[3]; } cyd_cal_t;"):
                      source.index("static int s_cal_target")]
    start = source.index("static void cal_point(float rx, float ry, float *tx, float *ty) {")
    transform = source[start:source.index("\n}", start) + 2]
    start = source.index("static void cal_view(float rx, float ry, float *tx, float *ty) {")
    view = source[start:source.index("\n}", start) + 2]
    with tempfile.TemporaryDirectory(prefix="cyd-touch-defaults-") as folder:
        folder = Path(folder)
        test = folder / "test.c"
        test.write_text(HARNESS + defaults + transform + view + TEST)
        command = [args.cc]
        if Path(args.cc).stem.lower() == "zig":
            command.append("cc")
        for runtime in (0, 1):
            for elegoo in (0, 1):
                executable = folder / f"test-{runtime}-{elegoo}.exe"
                subprocess.run(command + ["-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
                                          f"-DPT_BOARD_ELEGOO={elegoo}",
                                          f"-DPT_BOARD_UNIFIED={runtime}",
                                          f"-DPT_RUNTIME_HARDWARE={runtime}",
                                          "-I" + str(Path(__file__).resolve().parent.parent / "common"), str(test), "-o",
                                          str(executable)], check=True)
                subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    main()
