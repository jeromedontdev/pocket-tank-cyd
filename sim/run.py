"""Run the actual CYD gameplay/renderer on the desktop using Python's Tk window."""
import argparse
import ctypes
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parent.parent
SHOP_PRICES = [40, 80, 150, 100, 240, 180, 120, 220, 75, 120, 60, 60, 80, 80,
               300, 300, 200, 60, 100, 80, 160, 120, 40, 70, 90, 100, 110, 140,
               80, 60, 180, 160, 160, 180, 200, 240, 50, 110, 130, 70, 90, 80]


def build(compiler, output, *, defines=()):
    subprocess.run([sys.executable, str(ROOT / "tools" / "pack_cyd_icons.py"),
                    str(ROOT / "common" / "icons.c"), str(output)], check=True)
    cjson = ROOT / "firmware" / "managed_components" / "espressif__cjson" / "cJSON"
    library = output / ("tank.dll" if sys.platform == "win32" else "tank.so")
    command = [compiler]
    if Path(compiler).stem.lower() == "zig":
        command.append("cc")
    command += ["-shared", "-O1", "-std=c11", "-Wall", "-Wextra", "-Werror",
                "-I" + str(ROOT / "common"), "-I" + str(cjson)]
    command += ["-D" + define for define in defines]
    sources = [ROOT / "sim" / name for name in ("main.c", "persist_port_sim.c", "net_port_sim.c", "weather_port_sim.c")]
    sources += [ROOT / "common" / name for name in
                ("tank.c", "advisor.c", "render.c", "games.c", "progression.c", "setup.c", "notice.c", "update.c", "aquarium_store.c",
                 "companion.c", "companion_ui.c", "weather.c")]
    command += [*(str(path) for path in sources), str(cjson / "cJSON.c"),
                str(output / "icons_sd.c"), "-o", str(library)]
    if sys.platform != "win32":
        command += ["-fPIC", "-pthread", "-lm"]
    subprocess.run(command, check=True)
    return library


def load(path, icons):
    lib = ctypes.CDLL(str(path))
    lib.sim_init.argtypes = [ctypes.c_void_p, ctypes.c_size_t]
    lib.sim_frame.argtypes = [ctypes.c_float]
    lib.sim_frame.restype = ctypes.POINTER(ctypes.c_uint16)
    lib.sim_touch.argtypes = [ctypes.c_float, ctypes.c_float, ctypes.c_int]
    lib.sim_action.argtypes = [ctypes.c_int]
    lib.sim_backup.restype = ctypes.c_void_p
    lib.sim_free.argtypes = [ctypes.c_void_p]
    lib.sim_error.restype = ctypes.c_char_p
    lib.sim_network_message.restype = ctypes.c_char_p
    lib.sim_restore.argtypes = [ctypes.c_char_p, ctypes.c_size_t]
    if not lib.sim_init(icons, len(icons)):
        raise RuntimeError("Simulator could not initialize")
    return lib


def backup(lib):
    pointer = lib.sim_backup()
    if not pointer:
        raise RuntimeError("Could not serialize simulator tank")
    try:
        return ctypes.string_at(pointer)
    finally:
        lib.sim_free(pointer)


def window(lib):
    import tkinter as tk
    from tkinter import filedialog, messagebox
    app = tk.Tk()
    app.title("Pocket Tank CYD - desktop preview")
    toolbar = tk.Frame(app)
    toolbar.pack()
    for label, action in (("Tank", 0), ("Keeper", 1), ("Settings", 2), ("Shop", 3),
                          ("Feed", 4), ("+25 dollars", 5), ("Light", 6)):
        tk.Button(toolbar, text=label, command=lambda a=action: lib.sim_action(a)).pack(side="left")
    panel = tk.Label(app)
    panel.pack()
    tk.Label(app, text="Mouse = touch/drag/hold. Preview only; no real Wi-Fi or hardware emulation.").pack()
    colors = []
    last_brightness = -1

    def touch(event, down):
        lib.sim_touch(max(0, min(239, event.x / 3)), max(0, min(179, event.y / 3)), down)

    panel.bind("<ButtonPress-1>", lambda event: touch(event, 1))
    panel.bind("<B1-Motion>", lambda event: touch(event, 1))
    panel.bind("<ButtonRelease-1>", lambda event: touch(event, 0))

    def export():
        name = filedialog.asksaveasfilename(defaultextension=".json", filetypes=[("Tank backup", "*.json")])
        if name:
            try:
                with Path(name).open("xb") as file:
                    file.write(backup(lib))
            except (OSError, RuntimeError) as error:
                messagebox.showerror("Backup failed", str(error))

    def restore():
        name = filedialog.askopenfilename(filetypes=[("Tank backup", "*.json")])
        if name and messagebox.askyesno("Restore tank", "Replace the simulator tank? The device is not affected."):
            try:
                data = Path(name).read_bytes()
                if not lib.sim_restore(data, len(data)):
                    raise ValueError(lib.sim_error().decode())
            except (OSError, ValueError) as error:
                messagebox.showerror("Restore failed", str(error))

    tk.Button(toolbar, text="Export JSON", command=export).pack(side="left")
    tk.Button(toolbar, text="Restore JSON", command=restore).pack(side="left")

    def frame():
        nonlocal colors, last_brightness
        pixels = lib.sim_frame(0.04)
        brightness = lib.sim_display_brightness()
        if brightness != last_brightness:
            colors = [bytes((((p >> 11) & 31) * brightness // 31,
                             ((p >> 5) & 63) * brightness // 63,
                             (p & 31) * brightness // 31)) for p in range(65536)]
            last_brightness = brightness
        width, height = lib.sim_width(), lib.sim_height()
        ppm = f"P6\n{width} {height}\n255\n".encode() + b"".join(colors[pixels[i]] for i in range(width * height))
        panel.image = tk.PhotoImage(data=ppm, format="PPM").zoom(3)
        panel.configure(image=panel.image)
        app.after(40, frame)

    def close():
        if not lib.sim_save():
            messagebox.showerror("Save failed", "Could not write the simulator save.")
            return
        app.destroy()

    app.protocol("WM_DELETE_WINDOW", close)
    frame()
    app.mainloop()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default="cc", help="C compiler (or Zig executable)")
    parser.add_argument("--save", type=Path, default=Path(__file__).parent / "tank.sav")
    parser.add_argument("--smoke", action="store_true", help="Exercise renderer, pages and JSON without opening a window")
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="cyd-simulator-") as temporary:
        output = Path(temporary)
        os.environ["POCKET_TANK_SAVE"] = str(output / "smoke.sav" if args.smoke else args.save.resolve())
        os.environ["POCKET_TANK_WIFI"] = str(output / "fake-wifi.txt")
        lib = load(build(args.cc, output), (output / "icons.bin").read_bytes())
        try:
            if args.smoke:
                from test_creative import choose_first_run
                choose_first_run(lib)
                frames = set()
                for action in range(7):
                    lib.sim_action(action)
                    if action <= 3:
                        assert lib.sim_page() == action
                    for _ in range(5):
                        pixels = lib.sim_frame(0.04)
                        assert any(pixels[i] for i in range(lib.sim_width() * lib.sim_height()))
                    if action <= 3:
                        frames.add(ctypes.string_at(pixels, lib.sim_width() * lib.sim_height() * 2))
                assert len(frames) == 4, "Tank, Keeper, Settings and Shop must render different pages"
                data = backup(lib)
                assert lib.sim_restore(data, len(data))
                assert not lib.sim_restore(b"{}", 2)
                assert lib.sim_save()
                def tap(x, y):
                    lib.sim_touch(x * lib.sim_width() / 448, y * lib.sim_height() / 368, 1)
                    lib.sim_touch(x * lib.sim_width() / 448, y * lib.sim_height() / 368, 0)

                def advance():
                    for _ in range(12):
                        lib.sim_frame(0.25)

                lib.sim_action(2)
                tap(330, 100)  # Clock subpage.
                tap(220, 69); tap(220, 109); tap(220, 149); tap(120, 189); tap(220, 229); tap(380, 270)
                options = json.loads(backup(lib))["tank"]
                assert (options["clock_theme"], options["clock_flags"], options["clock_utc_quarters"], options["clock_layout"], options["clock_delay"]) == (1, 1, 4, 5, 1)
                tap(330, 189)
                assert json.loads(backup(lib))["tank"]["clock_flags"] == 3, "DST must preserve 12-hour format"
                tap(330, 189)
                tap(220, 282); tap(380, 270)
                assert json.loads(backup(lib))["tank"]["clock_utc_quarters"] == 6
                tap(220, 282); tap(380, 270)
                assert json.loads(backup(lib))["tank"]["clock_utc_quarters"] == 7
                tap(220, 282)
                tap(220, 20)  # Appearance subpage.
                tap(220, 80); tap(220, 119); tap(220, 163); tap(220, 207); tap(220, 251)
                assert json.loads(backup(lib))["tank"]["clock_flags"] == 61
                assert json.loads(backup(lib))["tank"]["clock_secondary"] == 1
                tap(224, 300)
                assert lib.sim_clock_active(), "Appearance preview must enter clock mode"
                tap(160, 90)
                lib.sim_action(2); tap(330, 100); tap(220, 20)
                for _ in range(7):
                    tap(220, 80)
                    tap(220, 119)
                tap(220, 163); tap(220, 207); tap(220, 251)
                assert json.loads(backup(lib))["tank"]["clock_flags"] == 1, "Color wrapping/detail toggles must preserve format and DST"
                assert json.loads(backup(lib))["tank"]["clock_secondary"] == 0
                tap(380, 20)
                options = json.loads(backup(lib))["tank"]
                tap(120, 318)
                assert lib.sim_page() == 0 and lib.sim_clock_active(), "SHOW CLOCK must enter clock mode"
                tap(160, 90)
                assert not lib.sim_clock_active() and json.loads(backup(lib))["tank"] == options, "Wake tap must not feed, select, or change settings"
                for _ in range(59):
                    lib.sim_frame(0.25)
                assert not lib.sim_clock_active(), "15-second clock must not activate early"
                lib.sim_frame(0.25)
                assert lib.sim_clock_active(), "15-second clock must activate at the configured delay"
                tap(160, 90)
                lib.sim_action(2); tap(330, 100)
                for _ in range(5):
                    tap(220, 229)
                assert json.loads(backup(lib))["tank"]["clock_delay"] == 6
                tap(120, 318)
                assert lib.sim_clock_active(), "Manual-only clock must support SHOW CLOCK"
                tap(160, 90)
                advance()
                assert not lib.sim_clock_active()
                lib.sim_action(2); tap(330, 100)
                for _ in range(7):
                    tap(220, 69)
                assert json.loads(backup(lib))["tank"]["clock_theme"] == 0
                tap(120, 318)
                assert lib.sim_page() == 2 and not lib.sim_clock_active(), "OFF must disable manual entry too"
                tap(220, 69)
                options = json.loads(backup(lib))["tank"]
                lib.sim_action(2); tap(100, 180)  # Enter Wi-Fi through the actual Settings route.
                advance()
                assert lib.sim_network_page() == 3, "Wi-Fi setup must scan, not fetch firmware"
                tap(160, 150)  # Open CoffeeShop Guest network.
                advance()
                assert lib.sim_network_message() == b"WI-FI CONNECTED"
                assert (output / "fake-wifi.txt").read_text().startswith("CoffeeShop Guest\n")
                advance()
                assert lib.sim_network_message() == b"WI-FI CONNECTED", "Wi-Fi-only flow must never check OTA"
                assert json.loads(backup(lib))["tank"] == options, "Wi-Fi mode must not alter aquarium or clock settings"
                tap(224, 315)  # Back to the clock page.
                assert lib.sim_network_page() == 0 and lib.sim_page() == 2
                lib.sim_action(2); tap(100, 180)
                assert lib.sim_network_message() == b"WI-FI SETTINGS"
                tap(100, 315)  # Forget asks for confirmation.
                assert lib.sim_network_message() == b"FORGET WI-FI?"
                tap(320, 265)  # Keep.
                assert lib.sim_network_message() == b"WI-FI SETTINGS"
                tap(100, 315)
                os.environ["POCKET_TANK_WIFI"] = str(output)  # Cannot erase a nonempty directory as a credential file.
                tap(110, 265)
                assert lib.sim_network_message() == b"FORGET FAILED"
                os.environ["POCKET_TANK_WIFI"] = str(output / "fake-wifi.txt")
                tap(224, 265); tap(110, 265)  # Retry, then confirm forget.
                assert lib.sim_network_message() == b"WI-FI FORGOTTEN" and not (output / "fake-wifi.txt").exists()
                tap(224, 315)
                (output / "fake-wifi.txt").write_text("Fishbowl\nwrongpass\n")
                lib.sim_action(2); tap(100, 180); tap(110, 265); advance()
                assert lib.sim_network_message() == b"NOT ACCEPTED"
                tap(320, 315)  # Error page has three actions, BACK at right.
                assert lib.sim_network_page() == 0
                (output / "fake-wifi.txt").unlink()
                os.environ["POCKET_TANK_WIFI"] = str(output)  # Unwritable credential-file target.
                lib.sim_action(2); tap(100, 180); advance(); tap(160, 150); advance()
                assert lib.sim_network_message() == b"WI-FI SAVE FAILED"
                tap(224, 315)
                os.environ["POCKET_TANK_WIFI"] = str(output / "fake-wifi.txt")
                lib.sim_action(2); tap(100, 180); advance(); tap(160, 74)
                assert lib.sim_network_page() == 4
                tap(380, 315)
                assert lib.sim_network_page() == 4, "An empty secured password must not connect"

                def keyboard_chars():
                    for start, count, y in ((33, 10, 127), (54, 9, 187), (96, 7, 247)):
                        for column in range(count):
                            tap(start + column * 42, y)

                letters = "qwertyuiopasdfghjklzxcvbnm"
                symbols = "1234567890" "@#$%&*-+=" "!?.,:/;"
                more_symbols = "`~^_\\|<>{}" "[]()\"'!?#" "$%&*+-="
                keyboard_chars()
                tap(43, 247)  # CAPS.
                keyboard_chars()
                tap(250, 315)  # SPACE.
                tap(132, 315)  # Separate numbers and symbols.
                keyboard_chars()
                tap(380, 315); advance()
                expected = (letters + letters.upper() + " " + symbols)[:63]
                assert (output / "fake-wifi.txt").read_text() == "Fishbowl\n" + expected + "\n", "QWERTY must enter every letter without paging and enforce the 63-character limit"
                tap(224, 315)
                lib.sim_action(2); tap(100, 180); tap(320, 265); advance(); tap(160, 74)
                for _ in range(63):
                    tap(400, 247)
                tap(132, 315)
                keyboard_chars()
                tap(43, 247)  # Second symbol layer.
                keyboard_chars()
                tap(132, 315)  # ABC.
                tap(54, 187)  # a.
                tap(43, 247); tap(54, 187)  # CAPS, A.
                tap(33, 127); tap(400, 247)  # Q, DEL.
                tap(52, 315)  # BACK must retain the password for the same network.
                assert lib.sim_network_page() == 3
                tap(160, 74)
                tap(380, 315); advance()
                assert (output / "fake-wifi.txt").read_text() == "Fishbowl\n" + symbols + more_symbols + "aA\n", "Symbols, case, DEL and same-network BACK must preserve exact input"
                assert set(symbols + more_symbols) == set("0123456789!\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~"), "Both symbol layers must cover every printable non-letter"
                tap(224, 315)
                tap(380, 20)  # Back to normal Settings.
                print("PASS: themed clock settings and Wi-Fi-only scan/connect, saved network, confirmed forget, wrong password, save failure and aquarium preservation")
                print("PASS: single-page QWERTY, all printable characters, case, space, delete, length limit and retained password")
                card_tank = json.loads(backup(lib))
                card_tank["tank"].update(clock_theme=0, setup_pending=0, tools_locked=1)
                encoded = json.dumps(card_tank).encode()
                assert lib.sim_restore(encoded, len(encoded))
                for y in range(60, 310, 40):
                    for x in range(40, 430, 40):
                        tap(x, y)
                        if lib.sim_selected() >= 0:
                            break
                    if lib.sim_selected() >= 0:
                        break
                assert lib.sim_selected() >= 0, "A fish tap must select a fish"
                first = lib.sim_selected()
                before_cycle = json.loads(backup(lib))
                for step in range(before_cycle["tank"]["n_fish"]):
                    tap(176, 28)
                    assert lib.sim_page() == 0 and lib.sim_selected() == (first + step + 1) % before_cycle["tank"]["n_fish"]
                    current_cycle = json.loads(backup(lib))
                    current_cycle["tank"]["saved_unix"] = before_cycle["tank"]["saved_unix"]
                    assert current_cycle == before_cycle, "Next-fish arrow must not feed or modify the tank"
                lib.sim_touch(176 * lib.sim_width() / 448, 28 * lib.sim_height() / 368, 1)
                lib.sim_touch(206 * lib.sim_width() / 448, 28 * lib.sim_height() / 368, 0)
                assert lib.sim_selected() == first and lib.sim_page() == 0, "Sliding outside the arrow cancels cycling"
                tap(70, 80)
                assert lib.sim_page() == 9, "The fish card MENU must open Home"
                print("PASS: fish-card arrow cycles all fish, wraps, cancels off-target releases and does not feed/open Keeper")
                from test_games import exercise
                exercise(lib, backup, output)
                from test_life import exercise as exercise_life
                exercise_life(lib, backup)
                from test_rpg import exercise as exercise_rpg
                exercise_rpg(lib, backup)
                lib.sim_action(5)
                lib.sim_action(1)
                lib.sim_action(7); tap(330, 180); tap(100, 100)
                assert lib.sim_page() == 5, "Collections Goals entry must open Goals"
                journal_frames = set()
                for category, x, y in ((330, 100, 100), (330, 100, 260), (100, 100, 260)):
                    lib.sim_action(7)
                    tap(330, 180) if category == 330 else tap(330, 100)
                    tap(x, y)
                    journal_frames.add(ctypes.string_at(lib.sim_frame(0.04), lib.sim_width() * lib.sim_height() * 2))
                assert len(journal_frames) == 3, "Goals, album and themes must differ"
                locked = json.loads(backup(lib))
                locked["tank"].update(journal_xp=0, journal_theme=0)
                encoded = json.dumps(locked).encode()
                assert lib.sim_restore(encoded, len(encoded))
                lib.sim_action(7); tap(330, 100); tap(100, 260)
                first_themes = ctypes.string_at(lib.sim_frame(0), lib.sim_width() * lib.sim_height() * 2)
                tap(400, 86)
                more_themes = ctypes.string_at(lib.sim_frame(0), lib.sim_width() * lib.sim_height() * 2)
                assert first_themes != more_themes, "Theme arrows must expose new backdrops"
                tap(100, 124)
                assert json.loads(backup(lib))["tank"]["journal_theme"] == 0, "Locked new themes must not apply"
                unlocked = json.loads(backup(lib))
                unlocked["tank"]["journal_xp"] = 4200
                encoded = json.dumps(unlocked).encode()
                assert lib.sim_restore(encoded, len(encoded))
                lib.sim_action(7); tap(330, 100); tap(100, 260); tap(400, 86)
                for row, theme in enumerate(range(4, 8)):
                    tap(100, 124 + row * 42)
                    assert json.loads(backup(lib))["tank"]["journal_theme"] == theme, "All new backdrops must be selectable on page two"
                tap(350, 86)
                tap(100, 124)
                assert json.loads(backup(lib))["tank"]["journal_theme"] == 0, "Original must remain selectable"
                print("PASS: independent Goals/Album/Themes, paginated themes, locked themes and all four new selections")
                tap(380, 20)
                assert lib.sim_page() == 9
                # Exercise real shop routing, especially IDs above the old MOVE boundary.
                shop_original = json.loads(backup(lib))
                shop_demo = json.loads(backup(lib))
                shop_demo["tank"].update(sd_balance=2530, sd_unlocks=0, sand_style=0, water_style=0, stat_upgrades=0)
                if "polish" in shop_demo:
                    shop_demo["polish"]["scenery_unlocks"] = 0
                data = json.dumps(shop_demo).encode()
                assert lib.sim_restore(data, len(data))
                prices = SHOP_PRICES
                order = sorted(range(len(prices)), key=lambda i: (prices[i], i))
                for item in range(22, len(prices)):
                    lib.sim_action(0)  # Reset shop pagination without altering the save.
                    lib.sim_action(3)
                    row = order.index(item)
                    for _ in range(row // 4):
                        tap(400, 40)
                    tap(150, 98 + (row % 4) * 56 + 16)
                    tap(224, 260)
                    current = json.loads(backup(lib))
                    if item < 32:
                        assert current["tank"]["sd_unlocks"] & (1 << item), item
                    elif item >= 36:
                        assert current["polish"]["scenery_unlocks"] & (1 << (item - 36)), item
                    else:
                        assert current["tank"]["stat_upgrades"] & (1 << (item - 32)), item
                    assert lib.sim_page() == 3, "Fixed scenery/upkeep must not open placement"
                assert current["tank"]["sd_balance"] == 190
                lib.sim_action(0)
                lib.sim_action(3)
                tap(400, 40)
                tap(150, 98 + (order.index(29) % 4) * 56 + 16)  # Duck Float: a 60-sand-dollar surface ornament.
                tap(224, 260)
                assert json.loads(backup(lib))["tank"]["sd_balance"] == 190, "First SELL tap only arms"
                tap(224, 260)
                sold = json.loads(backup(lib))["tank"]
                assert not sold["sd_unlocks"] & (1 << 29) and sold["sd_balance"] == 202
                data = json.dumps(shop_original).encode()
                assert lib.sim_restore(data, len(data))
                print("PASS: twenty shop purchases through price-sorted UI, including all four stat upgrades and six fixed scenery pieces")
                lib.sim_action(1)
                lib.sim_action(7); tap(100, 100); tap(330, 100)
                assert lib.sim_page() == 6, "Fish category must open the fish bank"
                lib.sim_frame(0.04)
                bank_before = json.loads(backup(lib))
                tap(300, 330)
                tap(350, 104)  # The last two fish cannot be deposited.
                assert json.loads(backup(lib))["bank"] == bank_before["bank"]
                assert json.loads(backup(lib))["tank"]["n_fish"] == bank_before["tank"]["n_fish"]
                tap(380, 20)
                assert lib.sim_page() == 9
                lib.sim_action(3)
                tap(250, 326)
                assert lib.sim_page() == 7, "Shop TANKS entry must open aquarium selection"
                lib.sim_frame(0.04)
                tank_before = json.loads(backup(lib))
                def assert_tank_kept():
                    current = json.loads(backup(lib))
                    assert current["bank"] == tank_before["bank"]
                    for key in ("sd_balance", "sd_earned", "sd_unlocks", "names", "n_fish", "fish_variety", "fish_pattern"):
                        assert current["tank"][key] == tank_before["tank"][key], key
                tap(350, 160)
                lib.sim_frame(0.04)
                tap(120, 250)
                assert_tank_kept()
                tap(350, 160)
                tap(300, 250)
                assert lib.sim_page() == 7, "Insufficient sand dollars must not switch tank"
                assert_tank_kept()
                tap(380, 20)
                tap(380, 20)
                assert lib.sim_page() == 3
                rich = json.loads(backup(lib))
                rich["tank"]["sd_balance"] = 1000
                rich["tank"]["setup_pending"] = 0
                rich["tank"]["newborn_p1"] = 0
                data = json.dumps(rich).encode()
                assert lib.sim_restore(data, len(data))
                lib.sim_action(3)
                tap(250, 326)
                tap(350, 160)
                tap(300, 250)
                assert lib.sim_page() == 0, "Confirmed affordable purchase must load a new tank"
                new_tank = json.loads(backup(lib))
                assert new_tank["tank"]["setup_pending"] == 1 and new_tank["tank"]["sd_balance"] == 500
                new_tank["tank"]["setup_pending"] = 0
                data = json.dumps(new_tank).encode()
                assert lib.sim_restore(data, len(data))
                lib.sim_action(3)
                tap(250, 326)
                tap(350, 120)
                tap(300, 250)
                returned = json.loads(backup(lib))
                assert returned["tank"]["sd_balance"] == 500, "Original tank must be charged exactly 500"
                assert returned["tank"]["names"] == rich["tank"]["names"]
                assert returned["bank"] == rich["bank"]
                lib.sim_action(2)
                tap(330, 260)
                before = json.loads(backup(lib))["tank"]
                tap(100, 260)
                tap(146, 216)  # Cancel leaves the tank intact.
                assert json.loads(backup(lib))["tank"]["sd_balance"] == before["sd_balance"]
                tap(100, 260)
                for _ in range(81):
                    lib.sim_frame(0.25)
                assert json.loads(backup(lib))["tank"]["sd_balance"] == before["sd_balance"]
                tap(100, 260)
                lib.sim_touch(146 * lib.sim_width() / 448, 216 * lib.sim_height() / 368, 1)
                lib.sim_touch(302 * lib.sim_width() / 448, 216 * lib.sim_height() / 368, 0)
                assert json.loads(backup(lib))["tank"]["sd_balance"] == before["sd_balance"]
                wipe_balance = json.loads(backup(lib))["tank"]["sd_balance"]
                tap(302, 216)
                fresh = json.loads(backup(lib))["tank"]
                assert fresh["sd_balance"] == 0 and fresh["sd_unlocks"] == 0
                assert lib.sim_page() == 0 and lib.sim_save()
                choose_first_run(lib)
                from test_creative import exercise as exercise_creative
                exercise_creative(lib, backup)
                print("PASS: CYD simulator pages, JSON, wipe cancel/timeout/drag/confirm and fresh setup")
            else:
                args.save.parent.mkdir(parents=True, exist_ok=True)
                window(lib)
        finally:
            # Release the Windows DLL before TemporaryDirectory removes it.
            if sys.platform == "win32":
                import _ctypes
                _ctypes.FreeLibrary(lib._handle)


if __name__ == "__main__":
    main()
