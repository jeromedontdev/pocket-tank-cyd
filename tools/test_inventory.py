"""Run inventory/migration tests using an existing C compiler (including Zig)."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default="cc")
    parser.add_argument("--all", action="store_true", help="Run the complete backup regression harness")
    args = parser.parse_args()
    root = Path(__file__).resolve().parent.parent
    build = root / ".inventory-test-build"
    build.mkdir(exist_ok=False)
    common = root / "common"
    cjson = root / "firmware" / "managed_components" / "espressif__cjson" / "cJSON"
    try:
        subprocess.run([sys.executable, str(root / "tools" / "pack_cyd_icons.py"),
                        str(common / "icons.c"), str(build)], check=True)
        executable = build / "test-inventory.exe"
        command = [args.cc]
        if Path(args.cc).stem.lower() == "zig":
            command += ["cc"]
        command += ["-std=c11", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
                    "-I" + str(common), "-I" + str(cjson)]
        command += [str(root / "tools" / name) for name in
                    ("test_tank_backup.c", "test_clock_weather.c", "test_decor_scenery.c")]
        command += [str(common / name) for name in (
            "tank.c", "progression.c", "advisor.c", "render.c", "games.c", "setup.c",
            "notice.c", "update.c", "aquarium_store.c", "companion.c", "companion_ui.c", "weather.c")]
        command += [str(build / "icons_sd.c"), str(cjson / "cJSON.c"),
                    str(root / "sim" / "net_port_sim.c"), str(root / "sim" / "weather_port_sim.c"),
                    "-o", str(executable)]
        if sys.platform != "win32":
            command += ["-pthread", "-lm"]
        subprocess.run(command, check=True)
        fixtures = sorted((root / "sim" / "testdata" / "saves").glob("*.sav"))
        environment = dict(os.environ, POCKET_TANK_FAKE_UPDATE="", POCKET_TANK_TEST_SLOTS=str(build),
                           POCKET_TANK_WIFI=str(build / "wifi.txt"),
                           POCKET_TANK_WEATHER=str(build / "weather.bin"))
        subprocess.run([str(executable), *([] if args.all else ["--inventory-only"]),
                        *(str(p) for p in fixtures)], check=True, env=environment)
    finally:
        shutil.rmtree(build)


if __name__ == "__main__":
    main()
