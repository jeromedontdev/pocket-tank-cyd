"""Profile synthetic five-fish scenes and stress full/tile rendering without hardware or real saves."""
import argparse
import json
import os
import re
from pathlib import Path
import subprocess
import sys
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", required=True)
    parser.add_argument("--frames", type=int, default=120)
    parser.add_argument("--seconds", type=int, default=30)
    baseline = parser.add_mutually_exclusive_group()
    baseline.add_argument("--baseline", help="Git revision supplying the reference renderer")
    baseline.add_argument("--baseline-file", type=Path, help="Preserved reference renderer from a local build")
    parser.add_argument("--rounded-edges", action="store_true", help="Also exercise the non-CYD vignette paths")
    parser.add_argument("--out", type=Path, help="Write JSON benchmark records")
    parser.add_argument("--embedded-icons", action="store_true", help="Verify compressed embedded icons against the raw bank")
    args = parser.parse_args()
    if args.frames < 1 or args.seconds < 0:
        parser.error("frames must be positive and seconds nonnegative")
    if args.embedded_icons and (args.baseline or args.baseline_file):
        parser.error("embedded icon checks require the current renderer; collect baseline timings without --embedded-icons")
    root = Path(__file__).resolve().parent.parent
    common = root / "common"
    cjson = root / "firmware" / "managed_components" / "espressif__cjson" / "cJSON"
    with tempfile.TemporaryDirectory(prefix="cyd-renderer-stress-") as temporary:
        build = Path(temporary)
        subprocess.run([sys.executable, str(root / "tools" / "pack_cyd_icons.py"),
                        str(common / "icons.c"), str(build)], check=True)
        command = [args.cc]
        if Path(args.cc).stem.lower() == "zig":
            command.append("cc")
        command += ["-std=c11", "-O2", "-g", "-Wall", "-Wextra", "-Werror",
                    "-DPOP_CAP=5", "-DCYD_FLAT_EDGES=" + ("0" if args.rounded_edges else "1"),
                    "-I" + str(common), "-I" + str(cjson), "-I" + str(build)]
        if args.embedded_icons:
            command.append("-DRENDER_EMBEDDED_ICONS")
            entries = re.findall(r"const icon_t (\w+) = \{ \d+, \d+, 0, 0, (\d+) \};",
                                 (build / "icons_sd.c").read_text())
            (build / "icons-check.h").write_text(
                "static const struct { const icon_t *icon; unsigned offset; } embedded_icons[] = {\n" +
                "".join(f"{{&{name}, {offset}}},\n" for name, offset in entries) + "};\n")
        if args.baseline or args.baseline_file:
            (build / "render-baseline.c").write_bytes(
                args.baseline_file.read_bytes() if args.baseline_file else subprocess.check_output(
                    ["git", "-C", str(root), "show", args.baseline + ":common/render.c"]))
            command.append("-DRENDER_BASELINE")
        command.append(str(root / "tools" / "test_renderer_stress.c"))
        command += [str(common / name) for name in (
            "tank.c", "progression.c", "advisor.c", "games.c", "setup.c", "notice.c",
            "update.c", "aquarium_store.c", "companion.c", "companion_ui.c", "weather.c")]
        command += [str(root / "sim" / name) for name in (
            "persist_port_sim.c", "net_port_sim.c", "weather_port_sim.c")]
        executable = build / "renderer-stress.exe"
        command += [str(common / "icons.c" if args.embedded_icons else build / "icons_sd.c"),
                    str(cjson / "cJSON.c"), "-o", str(executable)]
        if sys.platform != "win32":
            command += ["-pthread", "-lm"]
        subprocess.run(command, check=True)
        environment = dict(os.environ, POCKET_TANK_SAVE=str(build / "synthetic.sav"),
                           POCKET_TANK_WIFI=str(build / "wifi.txt"),
                           POCKET_TANK_WEATHER=str(build / "weather.bin"))
        result = subprocess.run([str(executable), str(build / "icons.bin"), str(args.frames),
                                 str(args.seconds)], env=environment,
                                stdout=subprocess.PIPE, text=True)
        records = [json.loads(line) for line in result.stdout.splitlines()]
        if args.out:
            args.out.write_text(json.dumps(records, indent=2) + "\n", encoding="utf8")
        result.check_returncode()
        assert len(records) == 12
        print(json.dumps(records, indent=2))


if __name__ == "__main__":
    main()
