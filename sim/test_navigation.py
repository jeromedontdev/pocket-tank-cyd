"""Focused fish/main-menu navigation and stable appearance-order regressions."""
import argparse
import ctypes
import json
import os
from pathlib import Path
import shutil
import sys

from run import backup, build, load
from test_creative import choose_first_run


def exercise(lib):
    def tap(x, y):
        x, y = x * lib.sim_width() / 448, y * lib.sim_height() / 368
        lib.sim_touch(x, y, 1)
        lib.sim_touch(x, y, 0)

    def frame():
        return ctypes.string_at(lib.sim_frame(0), lib.sim_width() * lib.sim_height() * 2)

    def state():
        snapshot = json.loads(backup(lib))
        snapshot["tank"]["saved_unix"] = 0
        return snapshot

    def select_fish():
        lib.sim_action(0)
        for _ in range(3):
            for y in range(60, 310, 35):
                for x in range(40, 430, 35):
                    tap(x, y)
                    if lib.sim_page() == 9 and lib.sim_companion_section() == 33:
                        return
                    lib.sim_action(0)
        raise AssertionError("A live fish must be selectable")

    order = [lib.sim_variety_at(rank) for rank in range(10)]
    # Market tier order: the Tetra starter first, the jellyfish last.
    assert order == [3, 2, 8, 0, 1, 9, 4, 5, 7, 6], order
    lib.sim_card_menu_hit.argtypes = [ctypes.c_float, ctypes.c_float]

    for width in (320, 240):
        if lib.sim_width() != width:
            lib.sim_action(2); tap(100, 100); tap(200, 140)
        assert lib.sim_width() == width
        lib.sim_action(1)
        main = frame()
        before = state()
        tap(100, 100)
        hub = frame()
        assert lib.sim_page() == 9 and hub != main, "Main > Fish opens the Fish hub"
        tap(100, 100)
        assert lib.sim_companion_section() == 33, "Fish hub > Summary"
        tap(380, 24)
        assert frame() == hub, "Summary Back returns to the Fish hub"
        tap(330, 100)
        assert lib.sim_setup_page() >= 0, "Rename takes over the whole page"
        tap(86, 61)
        assert lib.sim_setup_page() == -1 and frame() == hub, "Cancel rename returns to the Fish hub"
        pages = [hub]
        for pos, section in (((100, 180), 15), ((330, 180), 19), ((100, 260), 8), ((330, 260), 4)):
            tap(*pos)
            assert lib.sim_companion_section() == section, (pos, lib.sim_companion_section())
            pages.append(frame())
            tap(380, 24)
            assert frame() == hub
        assert len(set(pages)) == len(pages), "Family, Genes, Life and Wardrobe are distinct"
        tap(370, 332)
        assert lib.sim_page() == 6
        tap(392, 19)
        assert frame() == hub, "Fish Bank closes back to the Fish hub"
        tap(380, 24)
        assert frame() == main, (lib.sim_page(), lib.sim_companion_section())
        tap(330, 260)
        assert lib.sim_companion_section() == 35, "Help lives in the main menu"
        tap(380, 24)
        assert frame() == main
        tap(100, 260)
        assert lib.sim_page() == 8, "Main Casino opens its lobby"
        tap(380, 24)
        assert lib.sim_page() == 9 and frame() == main, "Casino Back returns to Main"
        tap(330, 100); tank_menu = frame(); tap(330, 180)
        assert lib.sim_page() != 9, "Themes occupies the removed tile, with no interior hole"
        tap(392, 19)
        assert frame() == tank_menu
        tap(100, 180)
        assert lib.sim_companion_section() == 34, "Permanent food upgrades are in Tank"
        tap(380, 24)
        assert frame() == tank_menu
        tap(380, 24)
        assert frame() == main
        assert state() == before, "Browsing must not change saves, IDs or equipment"

        select_fish()
        assert lib.sim_selected() == -1, "No live-tank fish stats overlay remains"
        assert lib.sim_companion_section() == 33, "Tapping fish opens its summary directly"
        frame()
        before = state()
        tap(370, 102)
        assert lib.sim_companion_section() == 33, "Next fish stays in useful summary"
        tap(380, 24)
        assert lib.sim_page() == 0, "Direct fish Summary closes to aquarium in one tap"
        after = state()
        changed = {key: (before[key], after[key]) for key in before if before[key] != after[key]}
        assert not changed, ("Summary navigation must not feed, spend or equip", changed)
        lib.sim_action(8)
        tap(294, 24)
        assert lib.sim_page() == 9 and frame() == main, "Menu opens Main in one tap"
        tap(380, 24)
        assert lib.sim_page() == 0

        # A drag off a card control must neither navigate nor reach gameplay.
        lib.sim_touch(70 * width / 448, 295 * lib.sim_height() / 368, 1)
        lib.sim_frame(0.01)
        lib.sim_touch(220 * width / 448, 295 * lib.sim_height() / 368, 0)
        assert lib.sim_page() == 0
        for x, y, expected in ((70, 80, 0), (70, 295, 0), (70, 333, 0),
                               (70, 315, 0), (172, 315, 0), (70, 360, 0)):
            fx, fy = x * width / 448, y * lib.sim_height() / 368
            assert lib.sim_card_menu_hit(fx, fy) == expected, (width, x, y)
        lib.sim_action(8)
        tap(380, 24)
        assert lib.sim_page() == 0
        lib.sim_action(1); tap(100, 180)
        assert lib.sim_companion_section() == 26, "Main Market opens stock"
        tap(224, 100)
        assert lib.sim_companion_section() == 31
        tap(370, 100)
        assert lib.sim_companion_section() == 27
        tap(380, 24); tap(380, 24); tap(380, 24)
        lib.sim_action(1); tap(330, 180)
        assert lib.sim_page() == 8, "Main Games opens one unified list"
        for _ in range(3):
            tap(350, 80)
        games_menu = frame()
        tap(120, 140)
        assert lib.sim_companion_section() == 2, "Stories share the Games list"
        tap(380, 24)
        assert lib.sim_page() == 8 and frame() == games_menu
        tap(330, 140)
        assert lib.sim_companion_section() == 20, "The Lost Current shares the Games list"
        tap(380, 24)
        assert lib.sim_page() == 8 and frame() == games_menu
        tap(350, 80)
        tap(380, 24)
        assert lib.sim_page() != 8
    print("PASS: 320/240 unified Fish hub/Tank/Market/Games/Casino, direct fish summary, "
          "Back history, wardrobe/family/genes/bank, food/Currents, inert legacy card controls")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default="cc")
    parser.add_argument("--life", action="store_true", help="Also run existing fish-life UI integration")
    args = parser.parse_args()
    output = Path(__file__).resolve().parents[1] / ".navigation-test"
    if output.exists():
        raise RuntimeError(f"Refusing to overwrite existing test output: {output}")
    output.mkdir()
    lib = None
    try:
        os.environ["POCKET_TANK_SAVE"] = str(output / "navigation.sav")
        os.environ["POCKET_TANK_WIFI"] = str(output / "wifi.txt")
        lib = load(build(args.cc, output, defines=("POP_CAP=5",)), (output / "icons.bin").read_bytes())
        choose_first_run(lib)
        exercise(lib)
        if args.life:
            from test_life import exercise as exercise_life
            fixture = json.loads(backup(lib))
            fixture["tank"].update(setup_pending=0, newborn_p1=0)
            data = json.dumps(fixture).encode()
            assert lib.sim_restore(data, len(data))
            exercise_life(lib, backup)
    finally:
        if lib is not None and sys.platform == "win32":
            import _ctypes
            _ctypes.FreeLibrary(lib._handle)
        shutil.rmtree(output)


if __name__ == "__main__":
    main()
