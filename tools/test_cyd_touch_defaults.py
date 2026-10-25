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
static int TANK_W = 240, TANK_H = 180, s_bias_y;
static bool s_inverted;
"""

TEST = r"""
int main(void) {
    float x, y;
    cal_point(40, 20, &x, &y);
    assert(x == 40);
    assert(y == (PT_BOARD_ELEGOO ? 159 : 20));
    cal_point(239, 179, &x, &y);
    assert(x == 239 && y == (PT_BOARD_ELEGOO ? 0 : 179));
    cal_point(0, 0, &x, &y);
    assert(x == 0 && y == (PT_BOARD_ELEGOO ? 179 : 0));
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
    with tempfile.TemporaryDirectory(prefix="cyd-touch-defaults-") as folder:
        folder = Path(folder)
        test = folder / "test.c"
        test.write_text(HARNESS + defaults + transform + TEST)
        command = [args.cc]
        if Path(args.cc).stem.lower() == "zig":
            command.append("cc")
        for elegoo in (0, 1):
            executable = folder / f"test-{elegoo}.exe"
            subprocess.run(command + ["-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
                                      f"-DPT_BOARD_ELEGOO={elegoo}", str(test), "-o",
                                      str(executable)], check=True)
            subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    main()
