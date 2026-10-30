"""Check compact page illustrations stay inside their reserved layout boxes."""
import argparse
from pathlib import Path
import shutil
import subprocess
import sys
import struct
import os


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default="cc")
    parser.add_argument("--board", choices=("cyd", "elegoo"), default="cyd")
    parser.add_argument("--harbor-only", action="store_true",
                        help="Check replaced upstream fish/helpers/materials, guarded tiles and source icons only")
    parser.add_argument("--build-dir", type=Path, help="Dedicated native build directory for parallel art validation")
    parser.add_argument("--gallery", type=Path, help="Capture audited menus, typography, fish, accessories and aquarium details from the actual renderer")
    parser.add_argument("--content-gallery", type=Path,
                        help="Capture all new tank decorations and wearable comparisons from the actual renderer")
    args = parser.parse_args()
    root = Path(__file__).resolve().parent.parent
    build = args.build_dir.resolve() if args.build_dir else root / "firmware" / "build_page_art_native"
    build.mkdir(parents=True, exist_ok=True)
    common = root / "common"
    cjson = root / "firmware" / "managed_components" / "espressif__cjson" / "cJSON"
    try:
        subprocess.run([sys.executable, str(root / "tools" / "pack_cyd_icons.py"),
                        str(common / "icons.c"), str(build)], check=True)
        command = [args.cc]
        if Path(args.cc).stem.lower() == "zig":
            command.append("cc")
        command += ["-std=c11", "-O1", "-Wall", "-Wextra", "-Werror",
                    "-DRENDER_GAMES_TESTING", "-DRENDER_MENU_TESTING",
                    "-DPOP_CAP=5", "-DCYD_FLAT_EDGES=1",
                    "-I" + str(common), "-I" + str(cjson),
                    str(root / "tools" / "test_page_art.c")]
        if args.board == "elegoo":
            command.append("-DPT_BOARD_ELEGOO=1")
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
        run = [str(build / "test-page-art.exe"), str(build / "icons.bin")]
        if args.gallery or args.content_gallery:
            if args.gallery:
                args.gallery.mkdir(parents=True, exist_ok=True)
            if args.content_gallery:
                args.content_gallery.mkdir(parents=True, exist_ok=True)
            if args.gallery:
                run.append(str(args.gallery.resolve()))
            else:
                run.append("-")
            if args.content_gallery:
                run.append(str(args.content_gallery.resolve()))
        subprocess.run(run,
                       check=True, cwd=root, env=dict(os.environ,
                           POCKET_TANK_SAVE=str(build / "synthetic.sav"),
                           POCKET_TANK_WIFI=str(build / "wifi.txt"),
                           POCKET_TANK_WEATHER=str(build / "weather.bin"),
                           POCKET_TANK_HARBOR_ART="1" if args.harbor_only else "0"))
        galleries = {path.resolve() for path in (args.gallery, args.content_gallery) if path}
        for gallery in galleries:
            from capture_cyd_gallery import png
            for width, height in ((240, 180), (320, 240)):
                for raw in gallery.glob(f"*-{width}.raw"):
                    data = raw.read_bytes()
                    pixels = struct.unpack(f"<{width * height}H", data)
                    png(raw.with_suffix(".png"), pixels, 3, width, height)
                    raw.unlink()
            for raw in gallery.glob("new-decoration-contact-*x*.raw"):
                width, height = map(int, raw.stem.rsplit("-", 1)[1].split("x"))
                pixels = struct.unpack(f"<{width * height}H", raw.read_bytes())
                png(gallery / "new-decoration-contact.png", pixels, 2, width, height)
                raw.unlink()
    finally:
        shutil.rmtree(build)


if __name__ == "__main__":
    main()
