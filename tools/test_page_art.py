"""Check compact page illustrations stay inside their reserved layout boxes."""
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
    build = root / "firmware" / "build_page_art_native"
    build.mkdir(exist_ok=True)
    common = root / "common"
    cjson = root / "firmware" / "managed_components" / "espressif__cjson" / "cJSON"
    try:
        subprocess.run([sys.executable, str(root / "tools" / "pack_cyd_icons.py"),
                        str(common / "icons.c"), str(build)], check=True)
        command = [args.cc]
        if Path(args.cc).stem.lower() == "zig":
            command.append("cc")
        command += ["-std=c11", "-O1", "-Wall", "-Wextra", "-Werror",
                    "-DRENDER_GAMES_TESTING",
                    "-I" + str(common), "-I" + str(cjson),
                    str(root / "tools" / "test_page_art.c")]
        command += [str(common / name) for name in (
            "tank.c", "progression.c", "advisor.c", "games.c", "setup.c", "notice.c",
            "update.c", "aquarium_store.c", "companion.c", "companion_ui.c", "weather.c")]
        command += [str(root / "sim" / name) for name in (
            "persist_port_sim.c", "net_port_sim.c", "weather_port_sim.c")]
        command += [str(build / "icons_sd.c"), str(cjson / "cJSON.c"),
                    "-o", str(build / "test-page-art.exe")]
        if sys.platform != "win32":
            command += ["-pthread", "-lm"]
        subprocess.run(command, check=True)
        subprocess.run([str(build / "test-page-art.exe"), str(build / "icons.bin")],
                       check=True, cwd=root)
    finally:
        shutil.rmtree(build)


if __name__ == "__main__":
    main()
