"""Build and run focused native fish movement and interaction regressions."""
import argparse
from pathlib import Path
import shutil
import subprocess
import sys


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default="cc", help="C compiler executable (including zig)")
    args = parser.parse_args()
    root = Path(__file__).resolve().parent.parent
    common = root / "common"
    cjson = root / "firmware" / "managed_components" / "espressif__cjson" / "cJSON"
    build = root / "firmware" / "build_fish_behavior_native"
    if build.exists():
        parser.error(f"build path already exists; preserve it and remove it before running: {build}")
    build.mkdir()
    try:
        subprocess.run([sys.executable, str(root / "tools" / "pack_cyd_icons.py"),
                        str(common / "icons.c"), str(build)], check=True)
        command = [args.cc]
        if Path(args.cc).stem.lower() == "zig":
            command.append("cc")
        command += ["-std=c11", "-O1", "-g", "-Wall", "-Wextra", "-Werror", "-include", "math.h",
                    "-I" + str(common), "-I" + str(cjson), "-I" + str(build),
                    str(root / "tools" / "test_fish_behavior.c")]
        command += [str(common / name) for name in (
            "tank.c", "progression.c", "advisor.c", "render.c", "games.c", "setup.c",
            "notice.c", "update.c", "aquarium_store.c", "companion.c", "companion_ui.c",
            "weather.c")]
        command += [str(root / "sim" / name) for name in (
            "persist_port_sim.c", "net_port_sim.c", "weather_port_sim.c")]
        command += [str(build / "icons_sd.c"), str(cjson / "cJSON.c"),
                    "-o", str(build / "test-fish-behavior.exe")]
        if sys.platform != "win32":
            command += ["-pthread", "-lm"]
        subprocess.run(command, check=True)
        subprocess.run([str(build / "test-fish-behavior.exe"), str(build / "icons.bin")],
                       check=True, cwd=root)
    finally:
        shutil.rmtree(build)


if __name__ == "__main__":
    main()
