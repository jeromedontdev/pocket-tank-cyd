"""Verify species-aware wardrobe fitting and optionally capture labeled fit sheets."""
import argparse
from pathlib import Path
import shutil
import struct
import subprocess
import sys


def sheets(gallery):
    from PIL import Image, ImageDraw, ImageFont
    varieties = ("CLASSIC", "ANGEL", "GUPPY", "TETRA", "PUFFER",
                 "SHARK", "JELLYFISH", "STINGRAY", "CLOWNFISH", "BETTA")
    wearables = ("TOP HAT", "PARTY HAT", "CROWN", "TIE", "BOWTIE", "BANDANA",
                 "SAILOR CAP", "PIRATE HAT", "BERET", "SUN HAT", "VIKING HELM", "SCARF")
    font = ImageFont.truetype("C:\\Windows\\Fonts\\consola.ttf", 18) if sys.platform == "win32" else ImageFont.load_default()
    for stage, name in enumerate(("fry", "juvenile", "adult", "elder")):
        image = Image.new("RGB", (12 * 352, 10 * 280 + 54), "#10252d")
        draw = ImageDraw.Draw(image)
        draw.text((12, 12), f"ACTUAL RENDERER / {name.upper()} / species x 12 wearables / unavailable = bare fish", font=font, fill="white")
        for variety, species in enumerate(varieties):
            for wear, item in enumerate(wearables, 1):
                raw = gallery / f"fit-{stage}-{variety}-{wear}.raw"
                pixels = struct.unpack("<76800H", raw.read_bytes())
                rgb = bytearray()
                for p in pixels:
                    rgb.extend((((p >> 11) & 31) * 255 // 31, ((p >> 5) & 63) * 255 // 63, (p & 31) * 255 // 31))
                frame = Image.frombytes("RGB", (320, 240), bytes(rgb))
                bounds = frame.getbbox()
                assert bounds and bounds[0] >= 80 and bounds[1] >= 54 and bounds[2] <= 256 and bounds[3] <= 180, bounds
                # Preserve native pixels at 2x nearest-neighbor; no substitute artwork.
                frame = frame.crop((80, 54, 256, 180)).resize((352, 252), Image.Resampling.NEAREST)
                x, y = (wear - 1) * 352, 54 + variety * 280
                image.paste(frame, (x, y + 28))
                blocked = variety == 6 and wear == 12
                draw.rectangle((x, y, x + 351, y + 279), outline="#33525d")
                draw.text((x + 5, y + 3), f"{species}: {item}", font=font, fill="#ffb5a2" if blocked else "#ddf3e8")
                if blocked:
                    draw.text((x + 5, y + 254), "UNAVAILABLE / WRAP FIT", font=font, fill="#ffb5a2")
                raw.unlink()
        image.save(gallery / f"wardrobe-fit-{name}.png")
        # A readable species sheet for inspection without opening a 2880px-wide grid.
        for variety, species in enumerate(varieties):
            row = image.crop((0, 54 + variety * 280, 4224, 54 + (variety + 1) * 280))
            panel = Image.new("RGB", (4 * 352, 3 * 280), "#10252d")
            for item in range(12):
                panel.paste(row.crop((item * 352, 0, (item + 1) * 352, 280)),
                            ((item % 4) * 352, (item // 4) * 280))
            panel.save(gallery / f"species-{species.lower()}-{name}.png")
    small_font = ImageFont.truetype("C:\\Windows\\Fonts\\consola.ttf", 14) if sys.platform == "win32" else font
    for species in varieties:
        detail = Image.new("RGB", (1660, 510), "#10252d")
        draw = ImageDraw.Draw(detail)
        draw.text((8, 5), f"{species} / all 4 stages x all 12 items / enlarged actual pixels for fit inspection", font=font, fill="white")
        for stage, name in enumerate(("fry", "juvenile", "adult", "elder")):
            panel = Image.open(gallery / f"species-{species.lower()}-{name}.png")
            draw.text((4, 55 + stage * 115), name.upper(), font=small_font, fill="white")
            for item, label in enumerate(wearables):
                x, y = item % 4 * 352, item // 4 * 280
                fish = panel.crop((x + 2, y + 30, x + 350, y + 252))
                bounds = fish.getbbox()
                assert bounds
                fish = fish.crop(bounds)
                scale = min(115 / fish.width, 84 / fish.height)
                fish = fish.resize((round(fish.width * scale), round(fish.height * scale)), Image.Resampling.NEAREST)
                x, y = 100 + item * 130, 32 + stage * 115
                detail.paste(fish, (x + (120 - fish.width) // 2, y + 20 + (84 - fish.height) // 2))
                draw.text((x, y), label, font=small_font, fill="#ddf3e8")
        detail.save(gallery / f"detail-{species.lower()}-all-stages.png")
    for raw in gallery.glob("ui-*.raw"):
        rgb = bytearray()
        for p in struct.unpack("<76800H", raw.read_bytes()):
            rgb.extend((((p >> 11) & 31) * 255 // 31, ((p >> 5) & 63) * 255 // 63, (p & 31) * 255 // 31))
        Image.frombytes("RGB", (320, 240), bytes(rgb)).resize((960, 720), Image.Resampling.NEAREST).save(raw.with_suffix(".png"))
        raw.unlink()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default="cc")
    parser.add_argument("--gallery", type=Path)
    args = parser.parse_args()
    root = Path(__file__).resolve().parent.parent
    build = root / "firmware" / "build_wardrobe_native"
    build.mkdir(exist_ok=True)
    common = root / "common"
    cjson = root / "firmware" / "managed_components" / "espressif__cjson" / "cJSON"
    try:
        subprocess.run([sys.executable, str(root / "tools" / "pack_cyd_icons.py"), str(common / "icons.c"), str(build)], check=True)
        command = [args.cc]
        if Path(args.cc).stem.lower() == "zig":
            command.append("cc")
        command += ["-std=c11", "-O1", "-Wall", "-Wextra", "-Werror",
                    "-DRENDER_MENU_TESTING", "-DCYD_FLAT_EDGES=1",
                    "-I" + str(common), "-I" + str(cjson)]
        command += [str(root / "tools" / name) for name in
                    ("test_wardrobe.c", "test_clock_weather.c", "test_decor_scenery.c")]
        command += [str(common / name) for name in
                    ("tank.c", "progression.c", "advisor.c", "render.c", "games.c", "setup.c",
                     "notice.c", "update.c", "aquarium_store.c", "companion.c", "companion_ui.c", "weather.c")]
        command += [str(build / "icons_sd.c"), str(cjson / "cJSON.c"),
                    str(root / "sim" / "net_port_sim.c"), str(root / "sim" / "weather_port_sim.c"),
                    "-o", str(build / "test-wardrobe.exe")]
        if sys.platform != "win32":
            command += ["-pthread", "-lm"]
        subprocess.run(command, check=True)
        run = [str(build / "test-wardrobe.exe")]
        if args.gallery:
            args.gallery.mkdir(parents=True, exist_ok=True)
            run.append(str(args.gallery.resolve()))
        subprocess.run(run, check=True, cwd=root)
        if args.gallery:
            sheets(args.gallery)
    finally:
        shutil.rmtree(build)


if __name__ == "__main__":
    main()
