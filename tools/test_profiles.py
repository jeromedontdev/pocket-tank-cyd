"""Native permanent-mode, shared-progress, Hard starvation and dual-copy tests."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default="cc")
    args = parser.parse_args()
    root = Path(__file__).resolve().parent.parent
    build = root / "firmware" / "build_profiles_native"
    build.mkdir(exist_ok=True)
    common = root / "common"
    cjson = root / "firmware" / "managed_components" / "espressif__cjson" / "cJSON"
    compiler = [args.cc]
    if Path(args.cc).stem.lower() == "zig":
        compiler += ["cc"]
    options = ["-std=c11", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
               "-I" + str(common), "-I" + str(cjson)]
    try:
        core = build / "test-profiles.exe"
        command = compiler + options + [str(root / "tools" / "test_profiles.c")]
        command += [str(common / name) for name in ("tank.c", "progression.c", "companion.c")]
        command += [str(cjson / "cJSON.c"), "-o", str(core)]
        if sys.platform != "win32":
            command += ["-pthread", "-lm"]
        subprocess.run(command, check=True)
        subprocess.run([str(core)], check=True, cwd=root)
        store = build / "test-profile-store.exe"
        command = compiler + options + ["-DTEST_PROFILE_STORE",
                   str(root / "tools" / "test_profiles.c"), str(root / "sim" / "persist_port_sim.c"),
                   str(common / "aquarium_store.c"), "-o", str(store)]
        subprocess.run(command, check=True)
        subprocess.run([str(store)], check=True, cwd=root,
                       env=dict(os.environ, POCKET_TANK_SAVE=str(build / "profile.sav")))
    finally:
        shutil.rmtree(build)


if __name__ == "__main__":
    main()
