"""Verify frame-time percentile bounds, slow-frame thresholds and resets."""
import argparse
from pathlib import Path
import subprocess
import tempfile


HARNESS = r"""
#include <assert.h>
#include <stdio.h>
#include "frame_pacing.h"
int main(void) {
    for (unsigned elapsed = 0; elapsed <= 200000; elapsed++) {
        frame_schedule_t s = {0};
        unsigned delay = frame_schedule_delay_ticks(&s, 1000000, 1000000 + elapsed, 1000);
        assert(delay >= 1);
        if (elapsed < 33334) {
            assert(elapsed + delay * 1000 >= 33334);
            assert(s.next_us == 1066668);
        } else assert(delay == 1 && !s.next_us);
    }
    for (unsigned tick = 1000; tick <= 10000; tick += 9000) {
        frame_schedule_t s = {0};
        int64_t now = 1000000;
        for (int i = 1; i <= 30000; i++) {
            now += 12000 + frame_schedule_delay_ticks(&s, now, now + 12000, tick) * tick;
            int64_t target = 1000000 + (int64_t)i * 33334;
            assert(now >= target && now < target + tick);
        }
        assert(frame_schedule_delay_ticks(&s, now, now + 250000, tick) == 1 && !s.next_us);
        now += 250000 + tick;
        unsigned delay = frame_schedule_delay_ticks(&s, now, now + 12000, tick);
        assert(12000 + delay * tick >= 33334);
    }
    frame_pacing_t p = {0};
    assert(!frame_pacing_percentile(&p, 99));
    for (unsigned i = 1; i <= 200; i++) frame_pacing_add(&p, i * 1000);
    assert(p.frames == 200 && p.total_us == 20100000 && p.max_us == 200000);
    assert(p.over_50 == 150 && p.over_80 == 120 && p.over_100 == 100);
    assert(frame_pacing_percentile(&p, 50) >= 100000 && frame_pacing_percentile(&p, 50) <= 102000);
    assert(frame_pacing_percentile(&p, 95) >= 190000 && frame_pacing_percentile(&p, 95) <= 192000);
    assert(frame_pacing_percentile(&p, 99) >= 198000 && frame_pacing_percentile(&p, 99) <= 200000);
    memset(&p, 0, sizeof p);
    frame_pacing_add(&p, 4000000);
    assert(frame_pacing_percentile(&p, 99) == 4000000);
    memset(&p, 0, sizeof p);
    frame_pacing_add(&p, 50000); frame_pacing_add(&p, 80000); frame_pacing_add(&p, 100000);
    assert(!p.over_100 && p.over_80 == 1 && p.over_50 == 2);
    puts("PASS: absolute 30-Hz deadlines have no rounding drift over 30,000 frames at 1/10-ms ticks; late frames rebase without catch-up; histogram bounds and stalls");
}
"""


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parent.parent
    with tempfile.TemporaryDirectory(prefix="cyd-pacing-test-") as folder:
        folder = Path(folder)
        source = folder / "test.c"
        source.write_text(HARNESS)
        executable = folder / "test.exe"
        command = [args.cc]
        if Path(args.cc).stem.lower() == "zig":
            command.append("cc")
        subprocess.run(command + ["-std=c11", "-Wall", "-Wextra", "-Werror",
                                 "-I" + str(root / "firmware" / "main"), str(source),
                                 "-o", str(executable)], check=True)
        subprocess.run([str(executable)], check=True)
        main_source = (root / "firmware" / "main" / "main.c").read_text()
        services = main_source[main_source.index("static int64_t service_finished("):
                               main_source.index("static void start_tank_task(")]
        source.write_text(r"""
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef struct { int unused; } tank_t;
static tank_t tank;
static int64_t now, touch_duration;
static unsigned warnings, polls;
static const char *last_name;
static long long last_duration;
static int64_t esp_timer_get_time(void) { return now; }
static void touch_port_poll(tank_t *value) {
    assert(value == &tank); polls++; now += touch_duration;
}
static void log_warning(const char *tag, const char *format, const char *name, long long duration) {
    assert(!strcmp(tag, "pacing") && strstr(format, "blocked tank task"));
    warnings++; last_name = name; last_duration = duration;
}
#define ESP_LOGW log_warning
""" + services + r"""
int main(void) {
    now = 19999;
    assert(service_finished("fast", 0) == now && !warnings);
    now = 20000;
    assert(service_finished("threshold", 0) == now && warnings == 1);
    assert(!strcmp(last_name, "threshold") && last_duration == 20000);
    now = 9000000;
    service_finished("SD auto backup", now - 290000);
    assert(warnings == 2 && last_duration == 290000 && !strcmp(last_name, "SD auto backup"));
    touch_duration = 100;
    tank_touch_poll(); assert(polls == 1 && warnings == 2);
    touch_duration = 21000;
    tank_touch_poll();
    assert(polls == 2 && warnings == 3 && last_duration == 21000 && !strcmp(last_name, "touch"));
    puts("PASS: actual slow-service wrapper logs exact durations at 20-ms boundary; every touch poll runs without changing tank ownership");
}
""")
        subprocess.run(command + ["-std=c11", "-Wall", "-Wextra", "-Werror",
                                 str(source), "-o", str(executable)], check=True)
        subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    main()
