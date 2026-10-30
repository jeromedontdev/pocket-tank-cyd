"""Fish-market touch transactions and mode-gated identity editing."""
import argparse
import ctypes
import json
import os
from pathlib import Path
import shutil
import sys

from run import backup, build, load
from test_creative import choose_first_run


def exercise(lib, output, mode):
    fixture_id = 0
    def tap(x, y):
        x, y = x * lib.sim_width() / 448, y * lib.sim_height() / 368
        lib.sim_touch(x, y, 1)
        lib.sim_touch(x, y, 0)

    def snapshot():
        return json.loads(backup(lib))

    def restore(fixture):
        nonlocal fixture_id
        fixture_id += 1
        # Stage an independent save, never rewind the live livestock economy.
        if mode != 1:
            os.environ["POCKET_TANK_SAVE"] = str(output / f"fixture-{mode}-{fixture_id}.sav")
        data = json.dumps(fixture).encode()
        assert lib.sim_restore(data, len(data)), lib.sim_error()

    def frame():
        return ctypes.string_at(lib.sim_frame(0), lib.sim_width() * lib.sim_height() * 2)

    original = snapshot()
    original["tank"].update(setup_pending=0, newborn_p1=0, sd_balance=5000)
    restore(original)
    lib.sim_customize_fish(0)
    assert lib.sim_setup_page() == -1, "Free fish appearance editing is blocked in every mode"
    assert snapshot()["tank"]["fish_variety"] == original["tank"]["fish_variety"]
    lib.sim_begin_setup()
    assert lib.sim_setup_page() == 0, (mode, snapshot()["tank"]["n_fish"], lib.sim_setup_page())
    tap(224, 308)  # Welcome.
    assert lib.sim_setup_page() == 2, "Welcome skips retired bubbler placement"
    tap(80, 60)
    assert lib.sim_setup_page() == 0, "Back skips retired bubbler placement too"
    tap(224, 308)
    assert lib.sim_setup_page() == 2
    tap(360, 60)
    assert lib.sim_setup_page() == 4, "Every mode skips the first free looks editor"
    tap(360, 60)
    assert lib.sim_setup_page() == 6, "Every mode skips the second free looks editor"
    lib.sim_action(0)

    for width in (320, 240):
        restore(original)
        if lib.sim_width() != width:
            lib.sim_action(2); tap(100, 100); tap(200, 140)
        lib.sim_action(7); tap(100, 180)
        if mode == 1:
            assert lib.sim_page() == 9 and lib.sim_companion_section() == 26
            lib.sim_action(9)
            before = snapshot()
            tap(100, 288)
            assert snapshot()["tank"]["n_fish"] == before["tank"]["n_fish"], "Creative trading stays in the sandbox"
            continue
        else:
            assert lib.sim_page() == 9 and lib.sim_companion_section() == 26
        # Creatures: one of each per tank (one shrimp school), colors rolled at purchase, confirmed sales.
        tap(224, 340)
        assert lib.sim_companion_section() == 29
        for kind in (0, 1, 2, 3, 4):
            path = os.environ["POCKET_TANK_SAVE"]
            if lib.sim_creature_adults(kind):
                tap(100, 310); tap(330, 288)
            assert lib.sim_creature_adults(kind) == 0, (mode, width, kind)
            os.environ["POCKET_TANK_SAVE"] = str(output / "absent" / "creature.sav")
            tap(224, 260)
            assert lib.sim_creature_adults(kind) == 0
            os.environ["POCKET_TANK_SAVE"] = path
            tap(224, 260)
            assert lib.sim_creature_adults(kind) == 1, (mode, width, kind)
            balance = snapshot()["tank"]["sd_balance"]
            tap(224, 260)
            assert snapshot()["tank"]["sd_balance"] == balance and lib.sim_creature_adults(kind) == 1, "One per tank"
            tap(100, 310)
            assert lib.sim_companion_section() == 30
            tap(100, 288)
            assert lib.sim_creature_adults(kind) == 1
            tap(100, 310)
            os.environ["POCKET_TANK_SAVE"] = str(output / "absent" / "creature-sale.sav")
            tap(330, 288)
            assert lib.sim_creature_adults(kind) == 1
            assert snapshot()["tank"]["sd_balance"] == balance
            os.environ["POCKET_TANK_SAVE"] = path
            tap(100, 310); tap(330, 288)
            assert lib.sim_creature_adults(kind) == 0
            assert snapshot()["tank"]["sd_balance"] > balance
            tap(100, 310)
            assert lib.sim_companion_section() == 29, "Nothing left to sell"
            tap(400, 116)
        tap(380, 24)
        assert lib.sim_companion_section() == 26, "Creature Market Back returns to Fish Market"
        # Restore fish trading fixture so creature spending cannot alter its assertions.
        restore(original)
        if lib.sim_width() != width:
            lib.sim_action(2); tap(100, 100); tap(200, 140)
        lib.sim_action(9)
        initial = snapshot()
        offer = frame()
        tap(400, 160)
        assert frame() != offer, "Species arrow changes the actual offer"
        tap(48, 160)
        path = os.environ["POCKET_TANK_SAVE"]
        os.environ["POCKET_TANK_SAVE"] = str(output / "absent" / "market.sav")
        tap(100, 288)
        failed = snapshot()
        assert failed["tank"]["n_fish"] == initial["tank"]["n_fish"]
        assert failed["tank"]["sd_balance"] == initial["tank"]["sd_balance"]
        os.environ["POCKET_TANK_SAVE"] = path
        tap(100, 288)
        bought = snapshot()
        assert bought["tank"]["n_fish"] == initial["tank"]["n_fish"] + 1
        assert bought["tank"]["sd_balance"] < initial["tank"]["sd_balance"] or mode == 1
        tap(100, 288)
        paired = snapshot()
        assert paired["tank"]["n_fish"] == initial["tank"]["n_fish"] + 2
        n = initial["tank"]["n_fish"]
        pair = paired["tank"]["fish_variety"][n:n + 2]
        assert pair[0] == pair[1], pair  # Two of the first market tier: a breedable same-variety pair
        tap(370, 100)
        assert lib.sim_companion_section() == 27
        nursery = frame()
        tap(100, 326)
        assert lib.sim_companion_section() == 15
        tap(380, 24)
        assert frame() == nursery, "Family returns to the nursery used to enter"
        tap(380, 24)
        assert lib.sim_companion_section() == 26
        tap(224, 100)
        for _ in range(n):
            tap(400, 160)
        before_sale = snapshot()
        tap(224, 288)
        assert lib.sim_companion_section() == 28
        assert snapshot()["tank"]["n_fish"] == before_sale["tank"]["n_fish"]
        tap(100, 288)
        assert lib.sim_companion_section() == 31
        assert snapshot()["tank"]["sd_balance"] == before_sale["tank"]["sd_balance"]
        tap(224, 288)
        os.environ["POCKET_TANK_SAVE"] = str(output / "absent" / "sale.sav")
        tap(330, 288)
        assert snapshot()["tank"]["n_fish"] == before_sale["tank"]["n_fish"]
        assert snapshot()["tank"]["sd_balance"] == before_sale["tank"]["sd_balance"]
        os.environ["POCKET_TANK_SAVE"] = path
        # Failed confirmation resets selection; select that same adult again.
        for _ in range(n):
            tap(400, 160)
        tap(224, 288); tap(330, 288)
        sold = snapshot()
        assert sold["tank"]["n_fish"] == before_sale["tank"]["n_fish"] - 1
        assert sold["tank"]["sd_balance"] > before_sale["tank"]["sd_balance"] or mode == 1
        balance = sold["tank"]["sd_balance"]
        tap(330, 288)
        assert snapshot()["tank"]["sd_balance"] == balance, "A repeated confirmation cannot sell twice"
        tap(100, 288)  # Dismiss the newly opened quote before choosing stock.
        tap(75, 100)
        for _ in range(5):
            if snapshot()["tank"]["n_fish"] == 5:
                break
            tap(224, 288)
        full = snapshot()
        assert full["tank"]["n_fish"] == 5, (mode, width, lib.sim_companion_section(),
                                           full["tank"]["sd_balance"])
        tap(100, 288)
        assert snapshot()["tank"]["n_fish"] == 5
        assert snapshot()["tank"]["sd_balance"] == full["tank"]["sd_balance"], "Full tank purchase must not charge"
    if mode == 1:
        print("PASS: Creative at 320/240 blocks free fish styling and market purchase")
    else:
        print(f"PASS: market mode {mode}, 320/240, actual offers, pair/nursery, "
              "fish/creature/offspring buy/sale rollback, separate confirmation, capacity, startup/identity gating")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default="cc")
    args = parser.parse_args()
    output = Path(__file__).resolve().parents[1] / ".market-test"
    assert not output.exists()
    output.mkdir()
    try:
        library = build(args.cc, output, defines=("POP_CAP=5",))
        icons = (output / "icons.bin").read_bytes()
        for mode in (0, 2, 1):
            os.environ["POCKET_TANK_SAVE"] = str(output / f"mode-{mode}.sav")
            os.environ["POCKET_TANK_WIFI"] = str(output / "wifi.txt")
            lib = load(library, icons)
            try:
                choose_first_run(lib, mode)
                exercise(lib, output, mode)
            finally:
                if sys.platform == "win32":
                    import _ctypes
                    _ctypes.FreeLibrary(lib._handle)
    finally:
        shutil.rmtree(output)


if __name__ == "__main__":
    main()
