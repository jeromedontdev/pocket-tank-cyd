"""Capture the real CYD renderer with an isolated, staged demonstration tank."""
import argparse
import copy
import ctypes
import importlib.util
import json
import os
import re
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import time
import zlib

ROOT = Path(__file__).resolve().parent.parent


def png(path, pixels, scale, width=240, height=180):
    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))

    rows = bytearray()
    for y in range(height):
        row = bytearray()
        for x in range(width):
            p = pixels[y * width + x]
            rgb = bytes((((p >> 11) & 31) * 255 // 31, ((p >> 5) & 63) * 255 // 63,
                         (p & 31) * 255 // 31))
            row.extend(rgb * scale)
        rows.extend((b"\0" + row) * scale)
    path.write_bytes(b"\x89PNG\r\n\x1a\n" +
                     chunk(b"IHDR", struct.pack(">IIBBBBB", width * scale, height * scale, 8, 2, 0, 0, 0)) +
                     chunk(b"IDAT", zlib.compress(rows, 9)) + chunk(b"IEND", b""))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", required=True, help="GCC/Clang-compatible compiler or Zig executable")
    parser.add_argument("--out", type=Path, default=ROOT / "docs" / "media" / "cyd-features")
    parser.add_argument("--width", type=int, choices=(240, 320),
                        help="Capture one scale; by default capture both in separate subdirectories")
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    if args.width is None:
        for width in (320, 240):
            subprocess.run([sys.executable, str(Path(__file__).resolve()), "--cc", args.cc,
                            "--out", str(args.out / str(width)), "--width", str(width)], check=True)
        return
    spec = importlib.util.spec_from_file_location("cyd_sim", ROOT / "sim" / "run.py")
    sim = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(sim)
    names = []
    records = []
    old_env = {key: os.environ.get(key) for key in
               ("POCKET_TANK_SAVE", "POCKET_TANK_WIFI", "POCKET_TANK_WEATHER", "POCKET_TANK_BRIGHTNESS",
                "POCKET_TANK_WEATHER_SEARCH_FIXTURE", "POCKET_TANK_WEATHER_CURRENT_FIXTURE")}
    try:
        with tempfile.TemporaryDirectory(prefix="build_cyd_gallery_", dir=ROOT / "firmware") as temporary:
            output = Path(temporary)
            os.environ["POCKET_TANK_SAVE"] = str(output / "demo.sav")
            os.environ["POCKET_TANK_WIFI"] = str(output / "demo-wifi.txt")
            os.environ["POCKET_TANK_WEATHER"] = str(output / "demo-weather.bin")
            os.environ["POCKET_TANK_BRIGHTNESS"] = str(output / "demo-brightness.bin")
            for suffix, name, fixture in (
                    ("SEARCH", "cities.json", {"results": [{"name": "Paris", "admin1": "Ile-de-France",
                        "country": "France", "latitude": 48.85, "longitude": 2.35}]}),
                    ("CURRENT", "current.json", {"current": {"temperature_2m": 25,
                        "weather_code": 0, "time": 1792760400}, "current_units": {"temperature_2m": "\u00b0C"}})):
                path = output / name
                path.write_text(json.dumps(fixture), encoding="utf-8")
                os.environ["POCKET_TANK_WEATHER_" + suffix + "_FIXTURE"] = str(path)
            library = sim.build(args.cc, output, defines=("POP_CAP=5", "CYD_FLAT_EDGES=1"))
            lib = sim.load(library, (output / "icons.bin").read_bytes())
            try:
                sys.path.insert(0, str(ROOT / "sim"))
                from test_creative import choose_first_run

                def tap(x, y):
                    width, height = lib.sim_width(), lib.sim_height()
                    lib.sim_touch(x * width / 448, y * height / 368, 1)
                    lib.sim_touch(x * width / 448, y * height / 368, 0)

                def home():
                    lib.sim_action(7)

                def games_page(n):
                    for _ in range(4):
                        if lib.sim_games_menu_page() == n:
                            return
                        tap(350, 80)
                    raise RuntimeError("Could not reach Games page %d" % n)

                def capture(name):
                    width, height = lib.sim_width(), lib.sim_height()
                    pixels = list(lib.sim_frame(0.04)[:width * height])
                    if not any(pixels):
                        raise RuntimeError("Empty emulator frame: " + name)
                    if not name.startswith("first-run-") and width != args.width:
                        raise RuntimeError(f"{name}: expected {args.width}px, got {width}px")
                    expected = {
                        "fish-life": 8, "fish-summary": 33, "fish-summary-next": 33,
                        "food-upgrades": 34, "food-flakes": 34, "food-bloodworms": 34, "family-tree": 15,
                        "fish-genes": 19, "help": 35, "help-2": 35, "help-topic": 36, "help-topic-2": 36,
                        "fish-wardrobe": 4, "wearable-colors": 6,
                        "rpg-story": 20, "rpg-journal": 21, "rpg-help": 22,
                        "fish-market": 26, "fish-residents": 31, "fish-nursery": 27, "fish-sale": 28,
                        "creature-market": 29, "creature-sale": 30, "trade-contract": 37, "trade-auction": 38,
                        "rpg-journal-complete": 21,
                        "rpg-fish-life": 8, "adventure-menu": 2,
                        "adventure-pearl-library": 2, "adventure-starlight-cove": 2,
                        "adventure-bubble-route": 2, "adventure-going": 2,
                        "adventure-ready": 2, "adventure-postcard": 2,
                    }.get(name)
                    if name.startswith("creature-market-"):
                        expected = 29
                    if expected is not None and (lib.sim_page() != 9 or lib.sim_companion_section() != expected):
                        raise RuntimeError(f"{name}: expected companion {expected}; "
                                           f"got page {lib.sim_page()} section {lib.sim_companion_section()}")
                    pages = {"tank-identity": 7, "bank-stored": 6,
                             "bank-details": 6, "bank-swap-selection": 6,
                             "bank-swap-confirmation": 6, "bank-swap-saved": 6, "my-fish": 9, "ground-cover": 0}
                    if name in pages and lib.sim_page() != pages[name]:
                        raise RuntimeError(f"{name}: expected page {pages[name]}, got {lib.sim_page()}")
                    if name.startswith("foliage-shop-") and lib.sim_page() != 3:
                        raise RuntimeError(f"{name}: expected Decor shop")
                    png(args.out / (name + ".png"), pixels, 3, width, height)
                    names.append(name)
                    records.append(dict(file=name + ".png", render_width=width, render_height=height,
                                        png_width=width * 3, png_height=height * 3,
                                        page=lib.sim_page(), section=lib.sim_companion_section()))
                    print(f"{width}: {name} page={lib.sim_page()} section={lib.sim_companion_section()}", flush=True)

                def weather_wait():
                    for _ in range(200):
                        lib.sim_frame(0.04)
                        time.sleep(0.002)

                def restore(snapshot, preserve_life=False):
                    snapshot = copy.deepcopy(snapshot)
                    if snapshot.get("schema") in range(11, 24) and not preserve_life:
                        # Staged rosters replace identities; migrate their life records honestly.
                        snapshot["schema"] = 10
                        snapshot.pop("polish")
                        snapshot.pop("story", None)
                        snapshot.pop("games", None)
                        snapshot.pop("rpg", None)
                        snapshot.pop("creative", None)
                        snapshot.pop("profile", None)
                        snapshot.pop("games_extra", None)
                        snapshot.pop("care", None)
                        snapshot.pop("inventory", None)
                        snapshot.pop("mussel", None)
                        snapshot.pop("game_income", None)
                        snapshot.pop("content", None)
                        snapshot.pop("fixed", None)
                        snapshot.pop("reef", None)
                        snapshot.pop("creatures", None)
                        snapshot.pop("progression", None)
                        snapshot.pop("foliage", None)
                    data = json.dumps(snapshot, separators=(",", ":")).encode()
                    # Every showcase is an independent synthetic estate. Use the
                    # real wipe flow rather than bypassing stock anti-replay rules.
                    lib.sim_action(2)
                    tap(330, 260); tap(100, 284); tap(302, 216)
                    if json.loads(sim.backup(lib))["tank"]["sd_balance"] != 0:
                        raise RuntimeError("Could not reset isolated gallery estate")
                    choose_first_run(lib, snapshot.get("profile", {}).get("mode", 0))
                    if lib.sim_width() != args.width:
                        lib.sim_action(2); tap(100, 100); tap(200, 140)
                    assert lib.sim_width() == args.width
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

                capture("first-run-mode")
                choose_first_run(lib)
                if lib.sim_width() != args.width:
                    lib.sim_action(2); tap(100, 100); tap(200, 140); lib.sim_action(0)
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
                for name, owned, fixed in (
                        ("tank-crafted-ornaments", (9, 10, 18, 20), 0),
                        ("tank-crafted-seabed", (22, 23, 25, 26, 27), 0),
                        ("tank-crafted-reef", (), 63),
                        ("tank-crafted-floats", (13, 14, 21, 24, 28, 29), 0),
                        ("tank-crafted-plants", (0, 17, 19), 0)):
                    showcase = copy.deepcopy(demo)
                    showcase["tank"].update(n_fish=2, journal_theme=0,
                        sd_unlocks=sum(1 << item for item in owned), chest_x=122,
                        ornament_x=[42, 95, 202, 216],
                        extra_x=[x * args.width / 320 for x in (38, 80, 190, 253, 273, 219)],
                        plant_x=152, plant_z1=2)
                    for key in ("veg_growth", "veg_h", "veg_h3"):
                        base, spread = (0.22, 0.018) if name.endswith("plants") else (0.08, 0.008)
                        showcase["tank"][key] = [base + (i * 7 % 11) * spread
                                                 for i in range(len(showcase["tank"][key]))]
                    if name.endswith("floats"):
                        showcase["tank"]["ornament_x"][3] = 70
                        showcase["tank"]["extra_x"][5] = 250 * args.width / 320
                        showcase["tank"]["sand_style"] = 1
                    restore(showcase)
                    if fixed:
                        showcase = json.loads(sim.backup(lib))
                        showcase["polish"]["scenery_unlocks"] = fixed
                        restore(showcase, preserve_life=True)
                    scene(name)
                restore(demo)
                polish = json.loads(sim.backup(lib))
                polish["polish"].update(scenery_unlocks=63, subtitle=3, emblem=3)
                restore(polish, preserve_life=True)
                scene("tank-fixed-scenery")
                polish = json.loads(sim.backup(lib))
                polish["polish"]["next_visit_s"] = polish["polish"]["viewed_s"] + 1
                restore(polish, preserve_life=True)
                scene("tank-rare-visitor")
                visitor_demo = json.loads(sim.backup(lib))
                visitor_demo["tank"].update(journal_theme=0, sd_unlocks=0,
                                            veg_growth=[0] * len(visitor_demo["tank"]["veg_growth"]))
                visitor_demo["polish"]["scenery_unlocks"] = 0
                lib.sim_visit.argtypes = [ctypes.c_int]
                for species, name in enumerate(("drifter", "old-turtle", "ghost-fish", "night-manta", "lantern-squid")):
                    restore(visitor_demo, preserve_life=True)
                    lib.sim_visit(species)
                    scene("visitor-" + name)
                    for _ in range(25):
                        lib.sim_frame(0.04)
                    capture("visitor-" + name + "-alternate")
                restore(polish, preserve_life=True)
                home(); tap(100, 100); tap(100, 260)
                capture("fish-life")
                home(); tap(100, 100); capture("my-fish")
                home(); tap(100, 180); capture("fish-market")
                tap(370, 100); capture("fish-nursery")
                tap(224, 100); capture("fish-residents")
                tap(224, 288); capture("fish-sale")
                tap(100, 288); tap(224, 340); capture("creature-market")
                tap(224, 260); tap(100, 310); capture("creature-sale")
                tap(100, 288)
                for kind, species in enumerate(("shrimp", "snail", "urchin", "seahorse", "mussel")):
                    if kind:
                        tap(400, 116)
                    if not lib.sim_creature_adults(kind):
                        tap(224, 260)
                    capture("creature-market-" + species)
                home(); tap(100, 180); tap(380, 24); capture("market-hub")
                tap(330, 180); capture("trade-contract")
                tap(380, 24); tap(100, 260); capture("trade-auction")
                home(); tap(330, 100); tap(100, 100); tap(300, 338); capture("tank-identity")
                # A staged three-generation family with inherited genes and a departed ancestor.
                family = json.loads(sim.backup(lib))
                pol, story = family["polish"], family["story"]
                ids = [struct.unpack_from("<I", bytes.fromhex(pol["active"]), i * 20)[0] for i in range(5)]
                marlo = pol["next_id"]; pol["next_id"] += 1
                lines = bytearray.fromhex(story["active"])

                def lineage(i, parents, generation, pattern2=0, color2=0, traits=0, mutation=0):
                    struct.pack_into("<HHBBBBBB", lines, i * 10, *parents, generation, 0, pattern2, color2, traits, mutation)

                lineage(0, (ids[1], marlo), 2, traits=0x88, mutation=1)   # Nori: gold eye, a new tiger marking
                lineage(1, (ids[2], ids[3]), 1, pattern2=6, traits=0x91)   # Pearl: shimmer, carries speckle
                lineage(2, (0, 0), 0, color2=5, traits=0x10)
                lineage(3, (0, 0), 0, traits=0x80)
                lineage(4, (ids[1], marlo), 2, traits=0x13)                # Azure: shimmer + painted fins
                story["active"] = lines.hex()
                book = bytearray.fromhex(story["names"])
                struct.pack_into("<H8s", book, 0, marlo, b"Marlo")
                story["names"] = book.hex()
                story.update(births=3, seen=[3, 1, 0, 2, 0], first_seen_s=[3600, 90000, 0, 40000, 0], last_species=3)
                family["tank"]["fish_pattern"][0] = 5
                family["tank"]["fish_pattern"][4] = 6
                restore(family, preserve_life=True)
                home(); tap(100, 100); tap(100, 180)
                capture("family-tree")
                home(); tap(100, 100); tap(330, 180); capture("fish-genes")
                restore(demo)
                for theme, name in ((2, "tank-coral"), (3, "tank-deep-glow"),
                                    (4, "tank-ruins"), (5, "tank-sunset"),
                                    (6, "tank-ice"), (7, "tank-moonlit")):
                    themed = copy.deepcopy(demo)
                    themed["tank"].update(journal_theme=theme, journal_xp=4200)
                    restore(themed)
                    # Same staged layout makes backdrop differences easy to compare.
                    scene(name)
                rainbow = copy.deepcopy(demo)
                rainbow["tank"].update(journal_theme=0, sand_style=4, water_style=5)
                restore(rainbow)
                scene("tank-rainbow")
                for clock_theme, name in ((1, "clock-compact"), (2, "clock-digital"), (3, "clock-analog"), (4, "clock-neon"),
                                          (5, "clock-flip"), (6, "clock-binary"), (7, "clock-orbit")):
                    clock_tank = copy.deepcopy(demo)
                    clock_tank["tank"].update(clock_theme=clock_theme, clock_flags=1, clock_utc_quarters=0)
                    restore(clock_tank)
                    for _ in range(300):
                        lib.sim_frame(0.04)
                    lib.sim_action(2)
                    tap(330, 100)
                    tap(330, 260)
                    capture(name)
                custom = copy.deepcopy(demo)
                custom["tank"].update(clock_theme=2, clock_flags=(4 << 5) | 28 | 1)
                restore(custom)
                for _ in range(300):
                    lib.sim_frame(0.04)
                lib.sim_action(2); tap(330, 100); tap(330, 260)
                capture("clock-custom")
                two_color = copy.deepcopy(demo)
                two_color["tank"].update(clock_theme=7, clock_flags=(2 << 5) | 1, clock_secondary=4)
                restore(two_color)
                for _ in range(300):
                    lib.sim_frame(0.04)
                lib.sim_action(2); tap(330, 100); tap(330, 260)
                capture("clock-two-color")
                restore(clock_tank)
                lib.sim_action(2)
                tap(330, 100)
                capture("clock-settings")
                tap(220, 20)
                capture("clock-style")
                tap(380, 20)
                lib.sim_action(2); tap(100, 180)
                for _ in range(12):
                    lib.sim_frame(0.25)
                capture("wifi-networks")
                tap(160, 74)
                capture("wifi-password")
                tap(52, 315)  # Keyboard BACK.
                tap(160, 150)  # Demonstration open network, simulator only.
                for _ in range(12):
                    lib.sim_frame(0.25)
                capture("wifi-connected")
                tap(224, 315)
                tap(380, 20)
                restore(demo)
                for frame in range(300):
                    if frame % 15 == 0:
                        tap(4, 4)
                    lib.sim_frame(0.04)
                for y in range(60, 310, 40):
                    for x in range(40, 430, 40):
                        lib.sim_action(0)
                        tap(x, y)
                        if lib.sim_companion_section() == 33:
                            break
                    if lib.sim_companion_section() == 33:
                        break
                if lib.sim_companion_section() != 33:
                    raise RuntimeError("Could not open a fish summary")
                capture("fish-summary")
                tap(370, 102)
                capture("fish-summary-next")
                for kind, card, name in ((1, 99, "snail-card"), (5, 98, "shrimp-card"),
                                         (6, 97, "urchin-card"), (7, 96, "seahorse-card")):
                    creature = copy.deepcopy(demo)
                    creature["tank"]["sd_unlocks"] = 1 << kind
                    creature["tank"].update(shrimp_n=10, snail_x=190, snail_y=160, urchin_x=190,
                                            clock_theme=0, setup_pending=0, newborn_p1=0, tank_ms_bits=0xffff)
                    restore(creature)
                    for _ in range(200):
                        lib.sim_frame(.1)
                    width = lib.sim_width()
                    # A story notice that pops up meanwhile takes the first tap, so retry
                    # after the tap-burst window from the creature's current position.
                    for _ in range(3):
                        for _ in range(120):
                            point = lib.sim_creature_target(card)
                            if point >= 0:
                                break
                            lib.sim_frame(.1)
                        if point < 0:
                            raise RuntimeError(f"Could not find a touch target for {name}")
                        lib.sim_touch(point % width, point // width, 1)
                        lib.sim_touch(point % width, point // width, 0)
                        if lib.sim_selected() == card:
                            break
                        for _ in range(8):
                            lib.sim_frame(.1)
                    if lib.sim_selected() != card:
                        raise RuntimeError(f"Selection not reproducible for {name}")
                    capture(name)
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
                capture("main-menu")
                restore(demo)
                home(); tap(330, 100); tap(330, 180)
                capture("journal-themes")
                tap(400, 86)
                capture("journal-themes-more")
                starter = copy.deepcopy(demo)
                starter["tank"].update(journal_xp=0, journal_initialized=0, journal_claimed=0, journal_seen=1,
                                       journal_grown=1, journal_theme=0,
                                       fish_variety=[0] * len(demo["tank"]["fish_variety"]))
                restore(starter)
                home(); tap(100, 180)
                capture("market-starter-stock")
                restore(demo)
                lib.sim_action(1)
                lib.sim_action(7); tap(330, 180); games_page(0)
                capture("games-menu")
                for i, name in enumerate(("bubble-pop", "pearl-match", "fish-slalom", "treasure-cast", "feed-frenzy", "reef-puzzle")):
                    tap(110 + i % 2 * 204, 140 + i // 2 * 64)
                    for _ in range(5):
                        lib.sim_frame(0.04)
                    capture("game-" + name)
                    tap(380, 20)
                tap(110, 140)
                for _ in range(119):
                    lib.sim_frame(0.25)
                    for y in range(110, 320, 22):
                        for x in range(32, 416, 22):
                            tap(x, y)
                lib.sim_frame(0.25)
                capture("game-rewards")
                tap(224, 226)
                capture("game-rewards-saved")
                tap(380, 20)
                tap(300, 330)
                capture("game-records")
                tap(380, 20)
                tap(80, 330)
                capture("games-relaxed")
                tap(80, 330)
                home(); tap(330, 180); games_page(1)
                capture("games-page-two")
                for i, name in enumerate(("reef-defender", "current-runner", "pearl-breaker",
                                          "tide-tactics", "reef-architect", "deep-expedition")):
                    tap(100 + (i % 2) * 204, 140 + (i // 2) * 64)
                    capture("game-" + name + "-help")
                    tap(220, 324)
                    capture("game-" + name)
                    tap(380, 20)
                tap(350, 80)
                capture("games-page-three")
                for i, name in enumerate(("lighthouse", "shell-slide", "sonar-sweep",
                                          "tide-2048", "harbor-stack", "echo-reef")):
                    tap(100 + i % 2 * 204, 140 + i // 2 * 64)
                    capture("game-" + name + "-help")
                    tap(220, 324)
                    capture("game-" + name)
                    tap(380, 20)
                lib.sim_action(1)
                lib.sim_action(7)
                capture("desk-companion")
                tap(100, 100)
                capture("home-fish")
                home(); tap(100, 260); capture("casino")
                tap(100, 140); capture("casino-races")
                tap(100, 160); tap(350, 285)
                for _ in range(16):
                    lib.sim_frame(0.1)
                capture("casino-race-run")
                for _ in range(40):
                    lib.sim_frame(0.1)
                capture("casino-race-result")
                tap(390, 24); tap(320, 140); capture("casino-reels")
                tap(350, 285)
                for _ in range(30):
                    lib.sim_frame(0.1)
                capture("casino-reels-result")
                for name, x, y in (("hilo", 100, 190), ("shells", 320, 190),
                                   ("dive", 100, 266), ("wheel", 320, 266)):
                    tap(390, 24); tap(x, y); capture("casino-" + name)
                    tap(350, 285)
                    if name == "dive":
                        tap(330, 234)
                    for _ in range(30):
                        lib.sim_frame(0.1)
                    capture("casino-" + name + "-result")
                home(); tap(100, 180); tap(380, 24); tap(100, 180); capture("collection")
                home(); tap(330, 260); capture("help")
                tap(370, 338); capture("help-2")
                tap(200, 100); capture("help-topic")
                tap(370, 338); capture("help-topic-2")
                home(); tap(100, 100); tap(330, 260)
                capture("fish-wardrobe")
                tap(60, 180)
                for _ in range(3):
                    tap(220, 234)
                tap(220, 268)
                capture("wearable-colors")
                home(); tap(330, 100)
                capture("home-tank")
                lib.sim_action(0); scene("ground-cover")
                for mask, name in ((1, "anubias"), (2, "moss"), (4, "pearlweed"), (8, "kelp"), (15, "mixed")):
                    cover = json.loads(sim.backup(lib))
                    cover["foliage"] = dict(version=1, owned=15, planted=mask, reserved=0)
                    data = json.dumps(cover, separators=(",", ":")).encode()
                    assert lib.sim_restore(data, len(data)), lib.sim_error().decode()
                    lib.sim_action(0); scene("ground-cover-" + name)
                home(); tap(330, 100)
                tap(100, 180); capture("food-upgrades")
                tap(224, 276); capture("food-flakes")
                tap(224, 328); capture("food-bloodworms")
                home(); tap(330, 180); games_page(3); capture("games-stories"); tap(120, 140)
                capture("adventure-menu")
                tap(70, 258); capture("adventure-pearl-library")
                tap(70, 258); capture("adventure-starlight-cove")
                tap(70, 258)
                tap(370, 300); capture("adventure-bubble-route")
                tap(370, 300)
                tap(230, 258)
                tap(230, 258)
                capture("adventure-going")
                arriving = json.loads(sim.backup(lib))
                arriving["companion"]["adventure_clock"] -= 7201
                restore(arriving)
                lib.sim_frame(0.25)
                lib.sim_action(1)
                lib.sim_action(7)
                tap(330, 180)
                games_page(3); tap(120, 140)
                capture("adventure-ready")
                tap(230, 258)
                capture("adventure-postcard")
                before_story = json.loads(sim.backup(lib))
                home(); tap(330, 180); games_page(3); tap(330, 140)
                capture("rpg-story")
                tap(70, 132); capture("rpg-journal")
                tap(100, 324); capture("rpg-help")
                tap(380, 24); tap(380, 24)
                for encounter in range(18):
                    if encounter % 3 == 0:
                        capture("rpg-chapter-" + str(encounter // 3 + 1))
                    tap(200, 246 + encounter % 3 * 36)
                    if encounter == 0:
                        capture("rpg-encounter-result")
                    tap(200, 320)
                capture("rpg-finale")
                tap(70, 132); capture("rpg-journal-complete")
                home(); tap(100, 100); tap(100, 260)
                capture("rpg-fish-life")
                restore(before_story, preserve_life=True)
                if lib.sim_width() != args.width:
                    lib.sim_action(2); tap(100, 100); tap(200, 140)
                home(); tap(330, 180); tap(100, 180)
                capture("rpg-enlarged")
                for encounter in range(18):
                    if encounter % 3 == 0:
                        capture("rpg-chapter-" + str(encounter // 3 + 1) + "-enlarged")
                    tap(200, 246 + encounter % 3 * 36)
                    tap(200, 320)
                restore(before_story, preserve_life=True)
                lib.sim_action(2)
                tap(330, 100)
                tap(100, 260)
                capture("weather-settings")
                tap(100, 68)
                weather_wait()
                tap(140, 232)
                for x, y in ((418, 150), (52, 196), (160, 150), (332, 150), (95, 196), (320, 340)):
                    tap(x, y)
                weather_wait()
                tap(150, 65)
                weather_wait()
                capture("weather-city")
                tap(390, 20)
                tap(390, 20)
                lib.sim_action(0)
                scene("tank-no-retired-trophies")
                clock_collection = json.loads(sim.backup(lib))
                for theme, name in ((2, "digital"), (3, "analog"), (5, "flip")):
                    jumbo = copy.deepcopy(clock_collection)
                    jumbo["tank"].update(clock_theme=theme, clock_layout=3, clock_flags=0)
                    jumbo["companion"].update(captions=0, cosmetic=0)
                    restore(jumbo)
                    lib.sim_action(2); tap(330, 100); tap(100, 318)
                    capture("clock-jumbo-" + name)
                clock_collection["tank"].update(clock_theme=3, clock_layout=0, clock_delay=1)
                restore(clock_collection)
                for _ in range(64):
                    lib.sim_frame(0.25)
                if not lib.sim_clock_active():
                    raise RuntimeError("Collection clock did not activate")
                capture("clock-collection")
                sparkles = copy.deepcopy(clock_collection)
                sparkles["companion"].update(lively=1, captions=0)
                restore(sparkles)
                for _ in range(64):
                    lib.sim_frame(0.25)
                capture("clock-sparkles")
                clock_collection["tank"]["clock_theme"] = 5
                restore(clock_collection)
                for _ in range(64):
                    lib.sim_frame(0.25)
                capture("clock-weather")
                restore(demo)
                lib.sim_action(2)
                capture("settings")
                tap(100, 100)
                capture("settings-display")
                lib.sim_action(2); tap(100, 260)
                capture("settings-care")
                lib.sim_action(2); tap(330, 100); tap(100, 180)
                capture("clock-effects")
                lib.sim_action(0)
                scene("tank-normal")
                restore(demo)
                lib.sim_action(2); tap(330, 260)
                capture("settings-device")
                tap(200, 120)
                capture("sd-updates")
                lib.sim_action(2)
                tap(330, 260); tap(100, 284)
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
                price_defines = dict(re.findall(r"#define (SD_PRICE_\w+)\s+(\d+)",
                                               (ROOT / "common" / "progression.h").read_text()))
                catalog = re.search(r"const sd_item_t SD_ITEMS\[[^\]]+\] = \{([\s\S]*?)\n\};",
                                    (ROOT / "common" / "progression.c").read_text()).group(1)
                prices = [int(price_defines.get(price, price)) for price in re.findall(
                    r'\{\s*[^,]+,\s*"[^"]*",\s*"[^"]*",\s*"[^"]*",\s*(\w+)\s*\}', catalog)]
                order = sorted((i for i in range(len(prices)) if i not in (8, 30, 31)),
                               key=lambda i: (prices[i], i))
                for item, name in ((63, "anubias"), (64, "moss"), (65, "pearlweed"), (66, "kelp")):
                    lib.sim_action(0); lib.sim_action(3)
                    for _ in range(order.index(item) // 4):
                        tap(400, 40)
                    tap(150, 114 + (order.index(item) % 4) * 56)
                    capture("foliage-shop-" + name)
                lib.sim_action(0); lib.sim_action(3)
                for _ in range(order.index(32) // 4):
                    tap(400, 40)
                capture("shop-comfort")
                tap(150, 114 + (order.index(32) % 4) * 56)
                capture("calm-current")
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
                lib.sim_action(7); tap(100, 100); tap(370, 342)
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
                home(); tap(330, 100); tap(100, 100)
                if lib.sim_page() != 7:
                    raise RuntimeError("Aquarium navigation failed")
                capture("aquariums")
                tap(350, 160)
                capture("aquarium-purchase")
                tap(300, 250)
                if lib.sim_page() != 0:
                    raise RuntimeError("Aquarium purchase failed")
                if lib.sim_width() != args.width:
                    lib.sim_action(2); tap(100, 100); tap(200, 140); lib.sim_action(0)
                capture("aquarium-welcome")
                fresh = json.loads(sim.backup(lib))
                fresh["tank"]["setup_pending"] = 0
                fresh_data = json.dumps(fresh).encode()
                if not lib.sim_restore(fresh_data, len(fresh_data)):
                    raise RuntimeError(lib.sim_error().decode())
                lib.sim_action(0)
                home(); tap(330, 100); tap(100, 100)
                capture("aquariums-owned")
                tap(350, 160)
                capture("aquarium-summary")
                tap(380, 24)
                tap(300, 338)
                tap(120, 130)
                for _ in "TANK 2":
                    tap(359, 341)
                for x, y in ((170, 205), (134, 205), (134, 205), (188, 239),
                             (157, 341), (224, 239), (80, 239), (170, 205),
                             (152, 239), (134, 205), (296, 273)):
                    tap(x, y)
                capture("aquarium-name")
                tap(328, 107)
                renamed = json.loads(sim.backup(lib))
                saved_name = bytes.fromhex(renamed["polish"]["tank_name"]).split(b"\0", 1)[0].decode("ascii")
                if saved_name != "REEF GARDEN":
                    raise RuntimeError(f"Aquarium keyboard at {args.width}px saved {saved_name!r}, expected 'REEF GARDEN'")
                tap(380, 24)
                capture("aquariums-named")
                tap(350, 120)
                capture("aquarium-switch")
                tap(120, 250)
                lib.sim_action(2); tap(330, 260); tap(100, 284); tap(302, 216)
                tap(200, 154); capture("first-run-creative")
                tap(200, 300); lib.sim_action(0)
                if lib.sim_mode() != 1:
                    raise RuntimeError("First-run Creative selection failed")
                for width, suffix in ((args.width, ""),):
                    if lib.sim_width() != width:
                        lib.sim_action(2); tap(100, 100); tap(200, 140)
                    home(); tap(330, 100); tap(100, 100); tap(200, 308)
                    if width == 320:
                        for _ in range(3):
                            tap(135, 240); tap(310, 240)
                    capture("creative-fish" + suffix)
                    lib.sim_action(0); scene("creative-tank" + suffix)
                lib.sim_action(2); capture("settings-creative-mode")
                tap(330, 260); tap(100, 284); tap(302, 216)
                tap(200, 210); capture("first-run-hard")
                tap(200, 300); lib.sim_action(0)
                if lib.sim_width() != args.width:
                    lib.sim_action(2); tap(100, 100); tap(200, 140)
                lib.sim_action(2); tap(100, 260); capture("hard-care")
                hard = json.loads(sim.backup(lib))
                hard["tank"]["setup_pending"] = 0
                hard["fish"][0]["hunger"] = 10
                hard["profile"]["starvation_s"][0] = 5 * 3600
                restore(hard, preserve_life=True)
                capture("hard-starvation-warning")
                for i in range(hard["tank"]["n_fish"]):
                    hard["fish"][i]["hunger"] = 10
                    hard["profile"]["starvation_s"][i] = 6 * 3600
                restore(hard, preserve_life=True)
                lib.sim_frame(0.04)
                if json.loads(sim.backup(lib))["tank"]["n_fish"] != 0:
                    raise RuntimeError("Hard extinction demonstration did not reach recovery")
                home(); capture("hard-recovery-home")
                tap(100, 100); capture("hard-recovery")
                tap(200, 308)
                if json.loads(sim.backup(lib))["tank"]["n_fish"] != 2:
                    raise RuntimeError("Hard free recovery did not add the rescue pair")
                scene("hard-rescued")
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
    (args.out / "gallery.json").write_text(json.dumps(records, indent=2) + "\n", encoding="utf-8")
    print(", ".join(names))


if __name__ == "__main__":
    main()
