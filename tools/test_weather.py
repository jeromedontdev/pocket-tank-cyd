"""Build focused shared-weather/simulator tests without making network requests."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default="cc", help="Existing C compiler (including zig)")
    parser.add_argument("--search-fixture", type=Path, help="Optional saved real geocoding response")
    parser.add_argument("--current-fixture", type=Path, help="Optional saved real current response")
    args = parser.parse_args()
    root = Path(__file__).resolve().parent.parent
    cjson = root / "firmware" / "managed_components" / "espressif__cjson" / "cJSON"
    work = root / ("weather-tests-" + str(os.getpid()))
    work.mkdir()
    try:
        executable = work / ("weather.exe" if sys.platform == "win32" else "weather")
        command = [args.cc]
        if Path(args.cc).stem.lower() == "zig":
            command.append("cc")
        command += [
            "-std=c11", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
            "-I" + str(root / "common"), "-I" + str(cjson),
            str(root / "tools" / "test_weather.c"),
            str(root / "common" / "weather.c"),
            str(root / "sim" / "weather_port_sim.c"),
            str(root / "sim" / "net_port_sim.c"),
            str(cjson / "cJSON.c"), "-o", str(executable),
        ]
        if sys.platform != "win32":
            command += ["-pthread", "-lm"]
        subprocess.run(command, check=True, cwd=root)
        environment = dict(os.environ, POCKET_TANK_WEATHER=str(work / "weather.bin"),
                           POCKET_TANK_WEATHER_SEARCH_FIXTURE=str(work / "search.json"),
                           POCKET_TANK_WEATHER_CURRENT_FIXTURE=str(work / "current.json"),
                           POCKET_TANK_WIFI=str(work / "wifi.txt"))
        fixtures = []
        if args.search_fixture and args.current_fixture:
            fixtures = [str(args.search_fixture.resolve()), str(args.current_fixture.resolve())]
        subprocess.run([str(executable), *fixtures], check=True, env=environment, cwd=work)
        net = (root / "firmware" / "main" / "net_port_esp.c").read_text()
        wrapper = net[net.index("static void background_weather(void) {"):net.index("static void worker(")]
        harness = work / "idle-worker.c"
        harness.write_text(r"""
#include <assert.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <stdio.h>
static atomic_bool s_weather, s_weather_radio, s_foreground, s_background_busy;
static unsigned work_calls, service_calls;
static unsigned priority = 5, priority_changes;
static void vTaskPrioritySet(void *task, unsigned value) {
    assert(!task);
    priority = value; priority_changes++;
}
static bool clear_request, foreground_during_work;
static void background_weather_work(void) {
    assert(atomic_load(&s_background_busy));
    assert(priority == 3);
    work_calls++;
    if (clear_request) s_weather = false;
    if (foreground_during_work) s_foreground = true;
    if (!s_weather) s_weather_radio = false;
}
static void weather_port_esp_service(void) {
    assert(atomic_load(&s_background_busy));
    assert(priority == 3);
    service_calls++;
}
""" + wrapper + r"""
int main(void) {
    for (unsigned i = 0; i < 3600; i++) background_weather();
    assert(!work_calls && !service_calls && !s_background_busy && !priority_changes);
    s_weather = true; background_weather();
    assert(work_calls == 1 && service_calls == 1 && !s_background_busy);
    s_weather = false; s_weather_radio = true; background_weather();
    assert(work_calls == 2 && service_calls == 1 && !s_weather_radio && !s_background_busy);
    s_weather = true; clear_request = true; background_weather();
    assert(work_calls == 3 && service_calls == 1 && !s_background_busy);
    clear_request = false; s_weather = true; foreground_during_work = true; background_weather();
    assert(work_calls == 4 && service_calls == 1 && !s_background_busy);
    assert(priority == 5 && priority_changes == 8);
    puts("PASS: actual ESP weather wrapper ignores 3600 idle wakes; requests, radio cleanup, cancellation and foreground handoff retain busy ownership");
}
""")
        idle = work / "idle-worker.exe"
        compiler = [args.cc] + (["cc"] if Path(args.cc).stem.lower() == "zig" else [])
        subprocess.run(compiler + ["-std=c11", "-Wall", "-Wextra", "-Werror",
                                  str(harness), "-o", str(idle)], check=True)
        subprocess.run([str(idle)], check=True)
    finally:
        shutil.rmtree(work)


if __name__ == "__main__":
    main()
