"""Run focused Reef Rhythm progression/save regressions with an existing compiler."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default="cc")
    parser.add_argument("--market-only", action="store_true", help="Run fish market and inherited breeding regressions")
    parser.add_argument("--trade-only", action="store_true", help="Run breeding contract and auction regressions")
    args = parser.parse_args()
    root = Path(__file__).resolve().parent.parent
    build = root / "firmware" / "build_reef_progression_native"
    build.mkdir(exist_ok=False)
    common = root / "common"
    cjson = root / "firmware" / "managed_components" / "espressif__cjson" / "cJSON"
    compiler = [args.cc]
    if Path(args.cc).stem.lower() == "zig":
        compiler.append("cc")
    try:
        executable = build / "test-reef-progression.exe"
        command = compiler + ["-std=c11", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
                              "-I" + str(common), "-I" + str(cjson),
                              str(root / "tools" / ("test_trade.c" if args.trade_only else "test_fish_market.c" if args.market_only else "test_reef_progression.c"))]
        command += [str(common / name) for name in ("tank.c", "progression.c", "companion.c")]
        command += [str(cjson / "cJSON.c"), "-o", str(executable)]
        if sys.platform != "win32":
            command += ["-pthread", "-lm"]
        subprocess.run(command, check=True)
        subprocess.run([str(executable)], check=True, cwd=root)
        migration = build / "test-profile-v21-migration.exe"
        command = compiler + ["-std=c11", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
                              "-I" + str(common), "-I" + str(cjson),
                              str(root / "tools" / "test_profile_v21_migration.c")]
        command += [str(common / name) for name in ("tank.c", "progression.c", "companion.c", "aquarium_store.c")]
        command += [str(root / "sim" / "persist_port_sim.c"), str(cjson / "cJSON.c"), "-o", str(migration)]
        if sys.platform != "win32":
            command += ["-pthread", "-lm"]
        subprocess.run(command, check=True)
        environment = dict(os.environ, POCKET_TANK_SAVE=str(build / "v21-profile.sav"))
        subprocess.run([str(migration)], check=True, cwd=root, env=environment)
    finally:
        shutil.rmtree(build)


if __name__ == "__main__":
    main()
