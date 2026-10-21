"""Capture the real CYD renderer with an isolated, staged demonstration tank."""
import argparse
import copy
import ctypes
import importlib.util
import json
import os
from pathlib import Path
import struct
import tempfile
import zlib

ROOT = Path(__file__).resolve().parent.parent


def png(path, pixels, scale):
    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))

    rows = bytearray()
    for y in range(180):
        row = bytearray()
        for x in range(240):
            p = pixels[y * 240 + x]
            rgb = bytes((((p >> 11) & 31) * 255 // 31, ((p >> 5) & 63) * 255 // 63,
                         (p & 31) * 255 // 31))
            row.extend(rgb * scale)
        rows.extend((b"\0" + row) * scale)
    path.write_bytes(b"\x89PNG\r\n\x1a\n" +
                     chunk(b"IHDR", struct.pack(">IIBBBBB", 240 * scale, 180 * scale, 8, 2, 0, 0, 0)) +
                     chunk(b"IDAT", zlib.compress(rows, 9)) + chunk(b"IEND", b""))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", required=True, help="GCC/Clang-compatible compiler or Zig executable")
    parser.add_argument("--out", type=Path, default=ROOT / "docs" / "media" / "cyd-features")
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    spec = importlib.util.spec_from_file_location("cyd_sim", ROOT / "sim" / "run.py")
    sim = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(sim)
    names = []
    old_env = {key: os.environ.get(key) for key in ("POCKET_TANK_SAVE", "POCKET_TANK_WIFI")}
    try:
        with tempfile.TemporaryDirectory(prefix="cyd-gallery-") as temporary:
            output = Path(temporary)
            os.environ["POCKET_TANK_SAVE"] = str(output / "demo.sav")
            os.environ["POCKET_TANK_WIFI"] = str(output / "demo-wifi.txt")
            library = sim.build(args.cc, output, defines=("POP_CAP=5",))
            lib = sim.load(library, (output / "icons.bin").read_bytes())
            try:
                def tap(x, y):
                    lib.sim_touch(x * 240 / 448, y * 180 / 368, 1)
                    lib.sim_touch(x * 240 / 448, y * 180 / 368, 0)

                def capture(name):
                    pixels = list(lib.sim_frame(0.04)[:240 * 180])
                    if not any(pixels):
                        raise RuntimeError("Empty emulator frame: " + name)
                    png(args.out / (name + ".png"), pixels, 3)
                    names.append(name)

                def restore(snapshot):
                    data = json.dumps(snapshot, separators=(",", ":")).encode()
                    if not lib.sim_restore(data, len(data)):
                        raise RuntimeError(lib.sim_error().decode())
                    lib.sim_action(0)

                def scene(name):
                    # Dismiss earned-badge notices and let reward toasts expire.
                    for frame in range(300):
                        if frame % 15 == 0:
                            tap(4, 4)
                        lib.sim_frame(0.04)
                    capture(name)

                demo = json.loads(sim.backup(lib))
                tank = demo["tank"]
                tank.update(setup_pending=0, newborn_p1=0, n_fish=5, sd_balance=1250, sd_earned=1250,
                            journal_xp=2200, journal_claimed=7, journal_seen=1023, journal_grown=1023,
                            journal_initialized=1, journal_theme=1, sand_style=0, water_style=0,
                            player_feedings=18, hold_approaches=2, light_auto=0, light_manual_off=0)
                tank["names"] = ["Nori", "Pearl", "Coral", "Nova", "Azure", ""]
                colors = [0xFF477E, 0x42E8F5, 0xFFB347, 0xC184FF, 0x43D5A0, 0xFFFFFF]
                tank["body"] = colors
                tank["accent"] = [0xFFF478, 0xFF78CF, 0xFFFFFF, 0x46FFDA, 0xFFF478, 0xFFFFFF]
                tank["fish_variety"] = [5, 6, 8, 7, 9, 0]
                tank["fish_pattern"] = [2, 1, 0, 3, 1, 0]
                for fish in demo["fish"]:
                    fish.update(stage=2, age_s=86400, size=1.05, trust=5, hunger=3, energy=7, stress=0)
                # Decorated showcase; all ownership is demo-only.
                tank["sd_unlocks"] = sum(1 << bit for bit in (9, 10, 13, 14, 15, 19))
                tank["chest_x"] = 145
                tank["chest_z1"] = 2
                tank["ornament_x"] = [58, 95, 202, 216]
                tank["ornament_z1"] = [2, 2, 2, 2]
                tank["extra_x"] = [38, 80, 125, 153, 190, 219]
                tank["extra_z1"] = [1, 2, 2, 1, 1, 2]
                restore(demo)
                scene("tank-kelp")
                for theme, name in ((2, "tank-coral"), (3, "tank-deep-glow")):
                    themed = copy.deepcopy(demo)
                    themed["tank"]["journal_theme"] = theme
                    restore(themed)
                    # Same staged layout makes backdrop differences easy to compare.
                    scene(name)
                rainbow = copy.deepcopy(demo)
                rainbow["tank"].update(journal_theme=0, sand_style=4, water_style=5)
                restore(rainbow)
                scene("tank-rainbow")
                scenery = copy.deepcopy(demo)
                scenery["tank"].update(sd_unlocks=sum(1 << i for i in range(22, 30)),
                                       journal_theme=0, sand_style=0, water_style=0)
                restore(scenery)
                scene("tank-new-scenery")
                surface = copy.deepcopy(scenery)
                surface["tank"]["sd_unlocks"] = sum(1 << i for i in (24, 28, 29))
                restore(surface)
                scene("tank-surface-floats")
                restore(demo)
                lib.sim_action(1)
                capture("keeper")
                tap(70, 20)
                lib.sim_frame(0.04)
                tap(220, 250)
                capture("customization")
                # Close the customization preview without changing the staged snapshot.
                restore(demo)
                lib.sim_action(1)
                tap(150, 356)
                if lib.sim_page() != 5:
                    raise RuntimeError("Journal navigation failed")
                capture("journal-goals")
                tap(205, 328)
                capture("journal-album")
                tap(345, 328)
                capture("journal-themes")
                lib.sim_action(2)
                capture("settings")
                tap(70, 326)
                capture("sd-updates")
                lib.sim_action(2)
                tap(56, 18)
                capture("wipe-confirmation")
                tap(140, 216)
                # Unowned shop pages show the actual prices rather than owned controls.
                shop = copy.deepcopy(demo)
                shop["tank"].update(sd_unlocks=0, sand_style=0, water_style=0)
                restore(shop)
                lib.sim_action(3)
                capture("shop")
                for _ in range(3):
                    tap(400, 40)
                capture("shop-decorations")
                tap(400, 40)
                capture("shop-expansion")
                lib.sim_action(0)
                lib.sim_action(3)
                order = sorted(range(len(sim.SHOP_PRICES)), key=lambda i: (sim.SHOP_PRICES[i], i))
                for _ in range(order.index(30) // 4):
                    tap(400, 40)
                capture("shop-upkeep")
                tap(150, 114 + (order.index(30) % 4) * 56)
                capture("bio-filter")
                tap(4, 4)
                lib.sim_action(0)
                lib.sim_action(3)
                for _ in range(order.index(33) // 4):
                    tap(400, 40)
                capture("shop-fish-care")
                for _ in range(order.index(35) // 4 - order.index(33) // 4):
                    tap(400, 40)
                tap(150, 114 + (order.index(35) % 4) * 56)
                capture("training-ring")
                tap(4, 4)
                bank = copy.deepcopy(demo)
                for i in range(20):
                    fish = copy.deepcopy(demo["fish"][i % 5])
                    bank["bank"].append(dict(name=[["Sunny", "Bubbles", "Comet", "Luna", "Ripple"][i % 5]],
                                             body=colors[i % 5], accent=0xFFF478, paid=fish["ms_bits"],
                                             seen=0, drift=0, appearance=(i % 10) | ((i % 4) << 8),
                                             fish=fish))
                restore(bank)
                lib.sim_action(1)
                tap(350, 356)
                if lib.sim_page() != 6:
                    raise RuntimeError("Bank navigation failed")
                capture("bank-stored")
                tap(350, 104)
                capture("bank-details")
                tap(300, 240)
                capture("bank-swap-selection")
                tap(350, 104)
                capture("bank-swap-confirmation")
                tap(300, 240)
                result = json.loads(sim.backup(lib))
                if len(result["bank"]) != 20 or result["tank"]["n_fish"] != 5:
                    raise RuntimeError("Full-bank swap changed population counts")
                capture("bank-swap-saved")
                lib.sim_action(3)
                tap(250, 326)
                if lib.sim_page() != 7:
                    raise RuntimeError("Aquarium navigation failed")
                capture("aquariums")
                tap(350, 160)
                capture("aquarium-purchase")
                tap(300, 250)
                if lib.sim_page() != 0:
                    raise RuntimeError("Aquarium purchase failed")
                capture("aquarium-welcome")
                fresh = json.loads(sim.backup(lib))
                fresh["tank"]["setup_pending"] = 0
                restore(fresh)
                lib.sim_action(3)
                tap(250, 326)
                capture("aquariums-owned")
                tap(80, 338)
                letters = " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-"
                current = "TANK 2".ljust(15)
                target = "REEF GARDEN".ljust(15)
                for i, letter in enumerate(target):
                    tap(46 + (i % 8) * 50, 140 + (i // 8) * 58)
                    for _ in range((letters.index(letter) - letters.index(current[i])) % len(letters)):
                        tap(140, 250)
                capture("aquarium-name")
                tap(350, 250)
                capture("aquariums-named")
                tap(350, 120)
                capture("aquarium-switch")
                tap(120, 250)
            finally:
                if os.name == "nt":
                    import _ctypes
                    _ctypes.FreeLibrary(lib._handle)
    finally:
        for key, value in old_env.items():
            if value is None:
                os.environ.pop(key, None)
            else:
                os.environ[key] = value
    print(f"Captured {len(names)} clean CYD renderer images in {args.out}")
    print(", ".join(names))


if __name__ == "__main__":
    main()
