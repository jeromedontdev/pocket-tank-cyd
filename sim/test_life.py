"""Exercise the desk-companion entry points through real touch/render APIs."""
import ctypes
import copy
import json
import os
from pathlib import Path


def exercise(lib, backup):
    def tap(x, y):
        width, height = lib.sim_width(), lib.sim_height()
        lib.sim_touch(x * width / 448, y * height / 368, 1)
        lib.sim_touch(x * width / 448, y * height / 368, 0)

    def frame():
        return ctypes.string_at(lib.sim_frame(0), lib.sim_width() * lib.sim_height() * 2)

    def home():
        lib.sim_action(1); tap(375, 356)

    def care():
        home(); tap(100, 100); tap(100, 100)

    lib.sim_action(1)
    keeper = frame()
    original = json.loads(backup(lib))
    tap(375, 356)
    assert lib.sim_page() == 9, "HOME must open without colliding with GAMES"
    hub = frame()
    assert hub != keeper
    tap(100, 100)
    fish_hub = frame()
    tap(100, 100)
    companions = frame()
    assert companions != fish_hub, "Fish care must open from Fish"
    tap(380, 24)
    assert frame() == fish_hub, "Care Back must return to Fish"
    tap(380, 24)
    assert frame() == hub, "Fish Back must return to Home"
    tap(380, 24)
    assert lib.sim_page() == 0, "Home Tank must return to aquarium"
    after = json.loads(backup(lib))
    after["tank"]["saved_unix"] = original["tank"]["saved_unix"]
    changed = [key for key in original if after[key] != original[key]]
    assert after == original, ("Browsing LIFE must not mutate aquarium/progression", changed)
    care()
    tap(100, 100)
    changed_care = json.loads(backup(lib))
    assert changed_care["companion"]["cozy"] != original["companion"]["cozy"]
    changed_care["tank"]["saved_unix"] = after["tank"]["saved_unix"]
    assert changed_care["tank"] == after["tank"], "Changing care mode must not fabricate care rewards"
    tap(210, 234)
    hello = json.loads(backup(lib))
    assert int.from_bytes(bytes.fromhex(hello["companion"]["active"])[4:6], "little") == 5
    tap(210, 234)
    assert json.loads(backup(lib))["companion"] == hello["companion"], "Hello cooldown must prevent repeated bond farming"
    for _ in range(124):
        lib.sim_frame(0.25)
    tap(210, 234)
    second = json.loads(backup(lib))
    assert int.from_bytes(bytes.fromhex(second["companion"]["active"])[4:6], "little") == 10, "Hello cooldown must expire while CARE remains open"
    tap(60, 298)
    trick = json.loads(backup(lib))
    assert bytes.fromhex(trick["companion"]["active"])[8] & 1, "DART must unlock and persist at bond 10"
    tap(380, 24); tap(380, 24); tap(100, 180)
    tap(230, 258)
    assert json.loads(backup(lib))["companion"]["adventure"] == 0, "Departure requires confirmation"
    tap(230, 258)
    launched = json.loads(backup(lib))
    assert launched["companion"]["adventure"] == 1
    assert launched["tank"]["n_fish"] == original["tank"]["n_fish"] - 1, "The selected fish must actually leave"
    assert launched["companion"]["away"] == 1
    assert lib.sim_save()
    launched["companion"]["adventure_clock"] -= 7201
    data = json.dumps(launched).encode()
    assert lib.sim_restore(data, len(data))
    home(); tap(100, 180)
    lib.sim_frame(0.25)
    done = json.loads(backup(lib))
    assert done["companion"]["adventure_done"] == 1, "The two-hour adventure must finish after real elapsed time"
    assert done["companion"]["away"] == 0 and done["tank"]["n_fish"] == original["tank"]["n_fish"], "Fish returns automatically before rewards are claimed"
    save_path = os.environ["POCKET_TANK_SAVE"]
    os.environ["POCKET_TANK_SAVE"] = str(Path(save_path).parent / "missing-story" / "save.sav")
    tap(230, 258)
    failed = json.loads(backup(lib))
    assert failed["companion"] == done["companion"]
    assert failed["tank"]["sd_balance"] == done["tank"]["sd_balance"], "Failed quest claims must not grant coins"
    os.environ["POCKET_TANK_SAVE"] = save_path
    tap(230, 258)
    claimed = json.loads(backup(lib))
    assert claimed["companion"]["adventure_claimed"] == 1
    assert claimed["tank"]["sd_balance"] == done["tank"]["sd_balance"] + 10
    assert claimed["tank"]["journal_xp"] == min(1000000, done["tank"]["journal_xp"] + 30)
    tap(230, 258)
    assert json.loads(backup(lib))["tank"]["sd_balance"] == claimed["tank"]["sd_balance"], "Quest rewards must be single-use"
    wardrobe = copy.deepcopy(original)
    wardrobe["tank"].update(journal_xp=0, sd_balance=1000)
    data = json.dumps(wardrobe).encode()
    assert lib.sim_restore(data, len(data))
    home(); tap(100, 100); tap(100, 180)
    before = json.loads(backup(lib))
    wardrobe_frame = frame()
    tap(405, 234)
    assert frame() != wardrobe_frame, "Wardrobe arrow must preview a different accessory"
    assert json.loads(backup(lib))["companion"] == before["companion"], "Preview must not equip/buy"
    tap(45, 234); tap(100, 300)
    assert json.loads(backup(lib))["tank"]["sd_balance"] == 1000, "Level lock must prevent purchase"
    wardrobe["tank"]["journal_xp"] = 10000
    data = json.dumps(wardrobe).encode()
    assert lib.sim_restore(data, len(data))
    home(); tap(100, 100); tap(100, 180)
    os.environ["POCKET_TANK_SAVE"] = str(Path(save_path).parent / "missing-wardrobe" / "save.sav")
    tap(100, 300)
    failed = json.loads(backup(lib))
    assert failed["tank"]["sd_balance"] == 1000 and not bytes.fromhex(failed["companion"]["active"])[9]
    os.environ["POCKET_TANK_SAVE"] = save_path
    tap(100, 300)
    dressed = json.loads(backup(lib))
    assert dressed["tank"]["sd_balance"] == 955
    assert bytes.fromhex(dressed["companion"]["active"])[9:11] == bytes([1, 1])
    tap(330, 300)
    undressed = json.loads(backup(lib))
    assert bytes.fromhex(undressed["companion"]["active"])[9:11] == bytes([0, 1])
    tap(100, 300)
    assert json.loads(backup(lib))["tank"]["sd_balance"] == 955, "Re-equipping owned items must be free"
    tap(365, 100)
    assert json.loads(backup(lib))["companion"] == dressed["companion"], "NEXT FISH must not move ownership"
    tap(365, 100); tap(60, 180)
    before_colors = json.loads(backup(lib))
    natural = frame()
    tap(220, 234); tap(220, 268)
    assert frame() != natural, "Accessory color controls must update the actual preview"
    assert json.loads(backup(lib))["companion"] == before_colors["companion"], "Color preview must not save early"
    os.environ["POCKET_TANK_SAVE"] = str(Path(save_path).parent / "missing-colors" / "save.sav")
    tap(100, 309)
    assert json.loads(backup(lib))["companion"] == before_colors["companion"], "Failed color saves must roll back"
    os.environ["POCKET_TANK_SAVE"] = save_path
    tap(100, 309)
    colored = json.loads(backup(lib))
    assert bytes.fromhex(colored["companion"]["active"])[11] == 0x11
    assert colored["tank"]["sd_balance"] == 955, "Accessory colors must be free"
    tap(365, 100); tap(380, 24); tap(380, 24); tap(330, 100); tap(330, 180)
    before_creatures = json.loads(backup(lib))
    tap(220, 234); tap(365, 100)
    assert json.loads(backup(lib))["companion"] == before_creatures["companion"], "Switching creatures discards an unsaved preview"
    for _ in range(4):
        tap(220, 234); tap(220, 268); tap(100, 309); tap(365, 100)
    creatures = json.loads(backup(lib))
    assert creatures["companion"]["creature_colors"] == [0x11] * 4
    assert creatures["tank"]["sd_balance"] == 955
    tap(330, 309)
    assert json.loads(backup(lib))["companion"] == creatures["companion"], "NATURAL must remain a preview until saved"
    tap(100, 309)
    reset = json.loads(backup(lib))
    assert reset["companion"]["creature_colors"].count(0) == 1
    assert reset["tank"]["journal_xp"] == wardrobe["tank"]["journal_xp"], "Cosmetics must not grant XP"
    lib.sim_action(2); tap(100, 100)
    os.environ["POCKET_TANK_SAVE"] = str(Path(save_path).parent / "missing-scale" / "save.sav")
    tap(200, 175)
    assert lib.sim_width() == reset["width"]
    assert json.loads(backup(lib))["companion"]["display_compact"] == reset["companion"]["display_compact"]
    os.environ["POCKET_TANK_SAVE"] = save_path
    for width in ((240, 320) if reset["width"] == 320 else (320, 240)):
        tap(200, 175)
        scaled = json.loads(backup(lib))
        assert lib.sim_width() == scaled["width"] == width and lib.sim_height() == width * 3 // 4
        assert scaled["companion"]["display_compact"] == (width == 320)
        assert scaled["tank"]["sd_balance"] == reset["tank"]["sd_balance"]
        data = json.dumps(scaled).encode()
        assert lib.sim_restore(data, len(data)) and lib.sim_width() == width
        for category, child, destination in (
                ((100, 100), (330, 100), 6),
                ((330, 100), (100, 100), 3),
                ((330, 100), (330, 100), 7),
                ((330, 180), (100, 100), 5),
                ((100, 260), None, 8),
                ((330, 260), None, 2)):
            home(); tap(*category)
            if child:
                parent = frame(); tap(*child)
            assert lib.sim_page() == destination
            frame(); tap(392, 326 if destination == 3 else 19)
            assert lib.sim_page() == 9, (width, category, destination, "Back must return Home/category")
            if child:
                assert frame() == parent, "Back must return to the category actually used"
        home(); tap(100, 100); tap(330, 180)
        assert lib.sim_page() == 1
        tap(70, 356)
        assert lib.sim_page() == 5
        tap(392, 19)
        assert lib.sim_page() == 1, "Keeper shortcut must return to Keeper, not skip a parent"
        tap(392, 326)
        assert lib.sim_page() == 9, "Keeper opened from Fish must return to Fish"
        lib.sim_action(2); tap(100, 100)
    encoded = json.dumps(original).encode()
    assert lib.sim_restore(encoded, len(encoded))
    print("PASS: LIFE/wardrobe/color-page routing, four creature and accessory primary/detail previews, explicit saves/Natural reset, free colors, purchase/equip/remove, rollback, bonds and real adventures")
