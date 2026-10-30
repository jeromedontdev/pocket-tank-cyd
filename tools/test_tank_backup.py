"""Build/run the native CYD backup tests with an existing C compiler or Zig."""
import argparse
import os
from pathlib import Path
import subprocess
import sys
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default="cc", help="C compiler executable (including zig)")
    parser.add_argument("--clock-only", action="store_true", help="Run clock/weather bounds and settings only")
    parser.add_argument("--inventory-only", action="store_true", help="Run shop inventory and mussel income regressions")
    parser.add_argument("--reef-care-only", action="store_true", help="Run Reef Rhythm care, Journal and shop regressions")
    parser.add_argument("--backup-validation-only", action="store_true", help="Run JSON validation regressions and native timing")
    parser.add_argument("--flat-edges", action="store_true", help="Test the CYD renderer without cosmetic vignette")
    args = parser.parse_args()
    root = Path(__file__).resolve().parent.parent
    cjson = root / "firmware" / "managed_components" / "espressif__cjson" / "cJSON"
    if not (cjson / "cJSON.c").is_file():
        parser.error("Configure the ESP-IDF firmware first to provide its existing cJSON dependency")
    common = root / "common"
    sources = [common / name for name in (
        "tank.c", "progression.c", "advisor.c", "render.c", "games.c", "setup.c", "notice.c", "update.c", "aquarium_store.c",
        "companion.c", "companion_ui.c", "weather.c"
    )]
    # Metadata is generated deterministically by the same packer as the firmware.
    with tempfile.TemporaryDirectory(prefix=".pocket-tank-tests-", dir=Path.cwd()) as temporary:
        temporary = Path(temporary)
        subprocess.run([
            sys.executable, str(root / "tools" / "pack_cyd_icons.py"),
            str(common / "icons.c"), str(temporary)
        ], check=True)
        executable = temporary / "test-backup.exe"
        command = [args.cc]
        if Path(args.cc).stem.lower() == "zig":
            command.append("cc")
        if args.flat_edges:
            command.append("-DCYD_FLAT_EDGES=1")
        command += [
            "-std=c11", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
            "-I" + str(common), "-I" + str(cjson),
            str(root / "tools" / "test_tank_backup.c"),
            str(root / "tools" / "test_clock_weather.c"),
            str(root / "tools" / "test_decor_scenery.c"),
            *(str(path) for path in sources), str(temporary / "icons_sd.c"),
            str(cjson / "cJSON.c"), str(root / "sim" / "net_port_sim.c"),
            str(root / "sim" / "weather_port_sim.c"), "-o", str(executable)
        ]
        if sys.platform != "win32":
            command += ["-pthread", "-lm"]
        subprocess.run(command, check=True)
        fixtures = sorted((root / "sim" / "testdata" / "saves").glob("*.sav"))
        environment = dict(os.environ, POCKET_TANK_FAKE_UPDATE="", POCKET_TANK_TEST_SLOTS=str(temporary),
                           POCKET_TANK_WIFI=str(temporary / "wifi.txt"),
                           POCKET_TANK_WEATHER=str(temporary / "weather.bin"))
        selected = (["--reef-care-only"] if args.reef_care_only else
                    ["--backup-validation-only"] if args.backup_validation_only else
                    ["--inventory-only"] if args.inventory_only else
                    ["--clock-only"] if args.clock_only else [str(path) for path in fixtures])
        subprocess.run([str(executable), *selected], check=True, env=environment)
        if not args.clock_only and not args.inventory_only and not args.backup_validation_only and not args.reef_care_only:
            subprocess.run([str(executable), "--inventory-only"], check=True, env=environment)


if __name__ == "__main__":
    main()
