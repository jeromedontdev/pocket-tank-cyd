"""Exercise fixed first-run modes and Creative editing through real touch APIs."""
import ctypes
import json
import os
from pathlib import Path


def choose_first_run(lib, mode=0):
    def tap(x, y):
        lib.sim_touch(x * lib.sim_width() / 448, y * lib.sim_height() / 368, 1)
        lib.sim_touch(x * lib.sim_width() / 448, y * lib.sim_height() / 368, 0)
    assert lib.sim_setup_page() >= 0 and not lib.sim_mode_chosen()
    tap(200, 98 + mode * 56)
    tap(200, 300)
    assert lib.sim_mode_chosen() and lib.sim_mode() == mode
    lib.sim_action(0)


def exercise(lib, backup):
    original = json.loads(backup(lib))

    def tap(x, y):
        lib.sim_touch(x * lib.sim_width() / 448, y * lib.sim_height() / 368, 1)
        lib.sim_touch(x * lib.sim_width() / 448, y * lib.sim_height() / 368, 0)

    def snapshot():
        return json.loads(backup(lib))

    def frame():
        return ctypes.string_at(lib.sim_frame(0), lib.sim_width() * lib.sim_height() * 2)

    def aquariums():
        lib.sim_action(7); tap(330, 100); tap(100, 100)
        assert lib.sim_page() == 7

    def wipe():
        lib.sim_action(2); tap(330, 260); tap(100, 284); tap(302, 216)
        assert lib.sim_setup_page() >= 0 and not lib.sim_mode_chosen()
        fresh = snapshot()
        assert not fresh["tank"]["sd_balance"] and not fresh["tank"]["journal_xp"]
        return fresh

    aquariums()
    normal_menu = frame()
    tap(200, 308)
    assert frame() == normal_menu and lib.sim_mode() == 0, "Removed Creative tank shortcut must be inert"
    for width in (240, 320):
        wipe()
        choose_first_run(lib, 1)
        if lib.sim_width() != width:
            lib.sim_action(2); tap(100, 100); tap(200, 140)
        lib.sim_frame(0)
        creative = snapshot()
        creative["tank"]["setup_pending"] = 0
        creative["tank"]["newborn_p1"] = 0
        encoded = json.dumps(creative).encode()
        assert lib.sim_restore(encoded, len(encoded)), lib.sim_error().decode()
        aquariums(); tap(200, 308)
        for _ in range(3):
            tap(135, 240); tap(310, 240)
        assert snapshot()["tank"]["n_fish"] == 5, "Creative keeps a five-fish cap"
        tap(310, 240)
        assert snapshot()["tank"]["n_fish"] == 5
        tap(310, 290)
        assert snapshot()["tank"]["n_fish"] == 5, "Removal requires confirmation"
        tap(310, 290)
        assert snapshot()["tank"]["n_fish"] == 4
        tap(380, 24)
        assert lib.sim_page() == 7
        tap(200, 308); tap(310, 240)
        assert snapshot()["tank"]["n_fish"] == 5
        lib.sim_action(7); tap(100, 100); tap(370, 342)
        assert lib.sim_page() == 6
        tap(300, 330); tap(350, 196); tap(300, 240)
        stored = snapshot()
        assert len(stored["bank"]) == 1 and stored["tank"]["n_fish"] == 4
        life = bytes.fromhex(stored["polish"]["bank"])[:20]
        lineage = bytes.fromhex(stored["story"]["bank"])[:10]
        aquariums(); tap(350, 160); tap(300, 250)
        assert snapshot()["bank"] == stored["bank"], "New tanks share the same Bank"
        lib.sim_action(7); tap(100, 100); tap(370, 342)
        tap(350, 104); tap(120, 240); tap(300, 240)
        moved = snapshot()
        assert not moved["bank"] and moved["tank"]["n_fish"] == 3
        assert bytes.fromhex(moved["polish"]["active"])[40:44] == life[:4]
        assert bytes.fromhex(moved["story"]["active"])[20:30] == lineage
        aquariums(); tap(350, 120); tap(300, 250)
        assert not snapshot()["bank"] and snapshot()["tank"]["n_fish"] == 4, "Do not revive a stale SD Bank"
        aquariums(); tap(350, 160); tap(300, 250)
        lib.sim_action(7); tap(100, 100); tap(370, 342)
        tap(300, 330); tap(350, 196); tap(300, 240)
        aquariums(); tap(350, 120); tap(300, 250)
        lib.sim_action(7); tap(100, 100); tap(370, 342)
        tap(350, 104); tap(120, 240); tap(300, 240)
        assert not snapshot()["bank"] and snapshot()["tank"]["n_fish"] == 5
        lib.sim_action(7); tap(100, 100); tap(330, 260); tap(100, 300)
        assert bytes.fromhex(snapshot()["companion"]["active"])[9] != 0, "Creative wardrobe is free"
        appearance = snapshot()
        lib.sim_action(0); lib.sim_customize_fish(0)
        assert lib.sim_setup_page() == -1, "Creative cannot open a free fish appearance editor"
        assert snapshot() == appearance, "Blocked appearance editing cannot change the fish or wallet"
        assert not snapshot()["companion"]["collections"], "Retired rewards cannot fabricate collections"
        encoded = json.dumps(original).encode()
        assert not lib.sim_restore(encoded, len(encoded)), "Normal backup cannot bypass a fixed Creative mode"
        assert lib.sim_mode() == 1
        lib.sim_action(2)
        settings = frame()
        tap(200, 340)
        assert frame() == settings and lib.sim_mode() == 1, "Settings mode footer is read-only"
        assert snapshot()["tank"]["sd_balance"] == creative["tank"]["sd_balance"]
        assert lib.sim_save()
        encoded = backup(lib)
        assert lib.sim_restore(encoded, len(encoded))
        assert lib.sim_mode() == 1 and snapshot()["tank"]["n_fish"] == 5

    wipe()
    save_path = os.environ["POCKET_TANK_SAVE"]
    os.environ["POCKET_TANK_SAVE"] = str(Path(save_path).parent / "missing-mode" / "save.sav")
    tap(200, 210); tap(200, 300)
    assert not lib.sim_mode_chosen(), "Failed mode selection must roll back and stay on the chooser"
    os.environ["POCKET_TANK_SAVE"] = save_path
    tap(200, 300)
    assert lib.sim_mode() == 2 and lib.sim_mode_chosen()
    lib.sim_action(0)
    lib.sim_action(2); tap(100, 260)
    hard = snapshot()
    tap(200, 248)
    assert not snapshot()["companion"]["cozy"], "Hard must not enable Comfort Assist"
    assert snapshot()["tank"]["sd_balance"] == hard["tank"]["sd_balance"]
    wipe(); choose_first_run(lib, 0)
    encoded = json.dumps(original).encode()
    assert lib.sim_restore(encoded, len(encoded)), lib.sim_error().decode()
    print("PASS: fixed first-run modes, global Bank transfers through touch at both scales, no stale SD duplicates, blocked free fish styling, cap, rollback and cross-mode restore protection")
