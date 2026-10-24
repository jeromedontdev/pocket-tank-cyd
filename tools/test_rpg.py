"""Run The Lost Current's native campaign, persistence and migration tests."""
import argparse
from pathlib import Path
import shutil
import subprocess
import sys


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default="cc")
    args = parser.parse_args()
    root = Path(__file__).resolve().parent.parent
    build = root / "firmware" / "build_rpg_native"
    build.mkdir(exist_ok=True)
    common = root / "common"
    cjson = root / "firmware" / "managed_components" / "espressif__cjson" / "cJSON"
    sources = [common / name for name in (
        "tank.c", "progression.c", "advisor.c", "render.c", "setup.c", "notice.c",
        "update.c", "aquarium_store.c", "companion.c", "weather.c"
    )]
    try:
        subprocess.run([sys.executable, str(root / "tools" / "pack_cyd_icons.py"),
                        str(common / "icons.c"), str(build)], check=True)
        command = [args.cc]
        if Path(args.cc).stem.lower() == "zig":
            command.append("cc")
        command += ["-std=c11", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
                    "-I" + str(common), "-I" + str(cjson)]
        command += [str(root / "tools" / name) for name in
                    ("test_rpg.c", "test_clock_weather.c", "test_decor_scenery.c")]
        command += [str(source) for source in sources]
        command += [str(build / "icons_sd.c"), str(cjson / "cJSON.c"),
                    str(root / "sim" / "net_port_sim.c"),
                    str(root / "sim" / "weather_port_sim.c"),
                    "-o", str(build / "test-rpg.exe")]
        if sys.platform != "win32":
            command += ["-pthread", "-lm"]
        subprocess.run(command, check=True)
        subprocess.run([str(build / "test-rpg.exe")], check=True, cwd=root)
    finally:
        shutil.rmtree(build)


if __name__ == "__main__":
    main()
