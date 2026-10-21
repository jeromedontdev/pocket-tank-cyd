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
               80, 60, 180, 160, 160, 180, 200, 240]


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
    sources = [ROOT / "sim" / name for name in ("main.c", "persist_port_sim.c", "net_port_sim.c")]
    sources += [ROOT / "common" / name for name in
                ("tank.c", "advisor.c", "render.c", "progression.c", "setup.c", "notice.c", "update.c", "aquarium_store.c")]
    command += [*(str(path) for path in sources), str(cjson / "cJSON.c"),
                str(output / "icons_sd.c"), "-o", str(library)]
    if sys.platform != "win32":
        command += ["-fPIC", "-lm"]
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
    colors = [bytes((((p >> 11) & 31) * 255 // 31, ((p >> 5) & 63) * 255 // 63,
                     (p & 31) * 255 // 31)) for p in range(65536)]

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
        pixels = lib.sim_frame(0.04)
        ppm = b"P6\n240 180\n255\n" + b"".join(colors[pixels[i]] for i in range(240 * 180))
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
                frames = set()
                for action in range(7):
                    lib.sim_action(action)
                    if action <= 3:
                        assert lib.sim_page() == action
                    for _ in range(5):
                        pixels = lib.sim_frame(0.04)
                        assert any(pixels[i] for i in range(240 * 180))
                    if action <= 3:
                        frames.add(ctypes.string_at(pixels, 240 * 180 * 2))
                assert len(frames) == 4, "Tank, Keeper, Settings and Shop must render different pages"
                data = backup(lib)
                assert lib.sim_restore(data, len(data))
                assert not lib.sim_restore(b"{}", 2)
                assert lib.sim_save()
                def tap(x, y):
                    lib.sim_touch(x * 240 / 448, y * 180 / 368, 1)
                    lib.sim_touch(x * 240 / 448, y * 180 / 368, 0)

                lib.sim_action(5)
                lib.sim_action(1)
                tap(220, 356)
                assert lib.sim_page() == 5, "Keeper Journal entry must open the Journal"
                journal_frames = set()
                for x in (70, 200, 340):
                    tap(x, 328)
                    journal_frames.add(ctypes.string_at(lib.sim_frame(0.04), 240 * 180 * 2))
                assert len(journal_frames) == 3, "Goals, album and themes must differ"
                tap(380, 20)
                assert lib.sim_page() == 1
                # Exercise real shop routing, especially IDs above the old MOVE boundary.
                shop_original = json.loads(backup(lib))
                shop_demo = json.loads(backup(lib))
                shop_demo["tank"].update(sd_balance=2000, sd_unlocks=0, sand_style=0, water_style=0, stat_upgrades=0)
                data = json.dumps(shop_demo).encode()
                assert lib.sim_restore(data, len(data))
                prices = SHOP_PRICES
                order = sorted(range(len(prices)), key=lambda i: (prices[i], i))
                for item in range(22, 36):
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
                    else:
                        assert current["tank"]["stat_upgrades"] & (1 << (item - 32)), item
                    assert lib.sim_page() == 3, "Fixed scenery/upkeep must not open placement"
                assert current["tank"]["sd_balance"] == 190
                lib.sim_action(0)
                lib.sim_action(3)
                tap(400, 40)
                tap(150, 170)  # Duck Float: first 60-sand-dollar surface ornament.
                tap(224, 260)
                assert json.loads(backup(lib))["tank"]["sd_balance"] == 190, "First SELL tap only arms"
                tap(224, 260)
                sold = json.loads(backup(lib))["tank"]
                assert not sold["sd_unlocks"] & (1 << 29) and sold["sd_balance"] == 202
                data = json.dumps(shop_original).encode()
                assert lib.sim_restore(data, len(data))
                print("PASS: fourteen new shop purchases through price-sorted UI, including all four stat upgrades")
                lib.sim_action(1)
                tap(350, 356)
                assert lib.sim_page() == 6, "Keeper must open the fish bank"
                lib.sim_frame(0.04)
                bank_before = json.loads(backup(lib))
                tap(300, 330)
                tap(350, 104)  # The last two fish cannot be deposited.
                assert json.loads(backup(lib))["bank"] == bank_before["bank"]
                assert json.loads(backup(lib))["tank"]["n_fish"] == bank_before["tank"]["n_fish"]
                tap(380, 20)
                assert lib.sim_page() == 1
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
                before = json.loads(backup(lib))["tank"]
                tap(56, 19)
                tap(146, 216)  # Cancel leaves the tank intact.
                assert json.loads(backup(lib))["tank"]["sd_balance"] == before["sd_balance"]
                tap(56, 19)
                for _ in range(81):
                    lib.sim_frame(0.25)
                assert json.loads(backup(lib))["tank"]["sd_balance"] == before["sd_balance"]
                tap(56, 19)
                lib.sim_touch(146 * 240 / 448, 216 * 180 / 368, 1)
                lib.sim_touch(302 * 240 / 448, 216 * 180 / 368, 0)
                assert json.loads(backup(lib))["tank"]["sd_balance"] == before["sd_balance"]
                wipe_balance = json.loads(backup(lib))["tank"]["sd_balance"]
                tap(302, 216)
                fresh = json.loads(backup(lib))["tank"]
                assert fresh["sd_balance"] == wipe_balance and fresh["sd_unlocks"] == 0
                assert lib.sim_page() == 0 and lib.sim_save()
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
