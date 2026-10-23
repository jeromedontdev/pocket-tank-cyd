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
    finally:
        shutil.rmtree(work)


if __name__ == "__main__":
    main()
