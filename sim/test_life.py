"""Exercise fish-life features through the unified FISH/TANK/MARKET/GAMES menus."""
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

    def state():
        return json.loads(backup(lib))

    def restore(fixture):
        data = json.dumps(fixture).encode()
        assert lib.sim_restore(data, len(data)), lib.sim_error()

    def main():
        lib.sim_action(7)
        assert lib.sim_page() == 9 and lib.sim_companion_section() == -1

    def fish(child):
        main(); tap(100, 100); tap(*child)

    def care_guide():
        main(); tap(330, 260)
        assert lib.sim_companion_section() == 35, "Help lives in the main menu"

    def stories():
        main(); tap(330, 180)
        while lib.sim_games_menu_page() != 3:
            tap(350, 80)
        tap(120, 140)
        assert lib.sim_companion_section() == 2, "Stories share the Games list"

    def wardrobe():
        fish((330, 260))
        assert lib.sim_companion_section() == 4, "Wardrobe lives in the Fish hub"

    save_path = os.environ["POCKET_TANK_SAVE"]

    def failing(name):
        os.environ["POCKET_TANK_SAVE"] = str(Path(save_path).parent / f"missing-{name}" / "save.sav")

    def saving():
        os.environ["POCKET_TANK_SAVE"] = save_path

    lib.sim_action(1)
    hub = frame()
    original = state()
    for point in ((375, 356), (70, 356), (170, 356), (275, 356), (330, 336), (100, 336)):
        tap(*point)
        assert lib.sim_page() == 9 and lib.sim_companion_section() == -1 and frame() == hub, \
            (point, "Retired Names & Looks, firsts, trophies and footer shortcuts must not remain hidden targets")
    tap(100, 100)
    fish_hub = frame()
    assert lib.sim_companion_section() == -1 and fish_hub != hub, "FISH opens the Fish hub"
    tap(100, 100)
    assert lib.sim_companion_section() == 33, "Fish hub opens the unified fish summary"
    tap(380, 24)
    assert frame() == fish_hub, "Summary Back must return to the Fish hub"
    tap(380, 24)
    assert frame() == hub, "Fish hub Back must return to Main"
    tap(330, 260)
    assert lib.sim_companion_section() == 35
    tap(380, 24)
    assert frame() == hub, "Help Back must return to Main"
    tap(380, 24)
    assert lib.sim_page() == 0, "Main Back returns to the aquarium"
    after = state()
    after["tank"]["saved_unix"] = original["tank"]["saved_unix"]
    changed = [key for key in original if after[key] != original[key]]
    assert after == original, ("Browsing fish menus must not mutate aquarium/progression", changed)

    lib.sim_action(2); tap(100, 260)
    tap(200, 248)
    changed_care = state()
    assert changed_care["companion"]["cozy"] != original["companion"]["cozy"]
    changed_care["tank"]["saved_unix"] = after["tank"]["saved_unix"]
    assert changed_care["tank"] == after["tank"], "Changing care mode must not fabricate care rewards"

    care_guide()
    before_guide = state()
    for point in ((210, 124), (60, 258), (100, 320)):
        tap(*point)
    assert state()["companion"] == before_guide["companion"], \
        "Retired greeting and trick targets must not grant bond or rewards"

    stories()
    tap(230, 258)
    assert state()["companion"]["adventure"] == 0, "Departure requires confirmation"
    tap(230, 258)
    launched = state()
    assert launched["companion"]["adventure"] == 1
    assert launched["tank"]["n_fish"] == original["tank"]["n_fish"] - 1, "The selected fish must actually leave"
    assert launched["companion"]["away"] == 1
    assert lib.sim_save()
    launched["companion"]["adventure_clock"] -= 7201
    restore(launched)
    stories()
    lib.sim_frame(0.25)
    done = state()
    assert done["companion"]["adventure_done"] == 1, "The two-hour story must finish after real elapsed time"
    assert done["companion"]["away"] == 0 and done["tank"]["n_fish"] == original["tank"]["n_fish"], \
        "Fish returns automatically before the postcard is collected"
    failing("story")
    tap(230, 258)
    failed = state()
    assert failed["companion"] == done["companion"]
    assert failed["tank"]["sd_balance"] == done["tank"]["sd_balance"], "Failed story claims must not grant doubloons"
    saving()
    tap(230, 258)
    claimed = state()
    assert claimed["companion"]["adventure_claimed"] == 1
    assert claimed["tank"]["sd_balance"] == done["tank"]["sd_balance"] + 10
    tap(230, 258)
    assert state()["tank"]["sd_balance"] == claimed["tank"]["sd_balance"], "Story postcards must be single-use"

    dressing = copy.deepcopy(original)
    dressing["tank"].update(journal_xp=0, sd_balance=1000)
    restore(dressing)
    wardrobe()
    before = state()
    wardrobe_frame = frame()
    tap(405, 234)
    assert frame() != wardrobe_frame, "Wardrobe arrow must preview a different accessory"
    assert state()["companion"] == before["companion"], "Preview must not equip/buy"
    tap(45, 234)
    failing("wardrobe")
    tap(100, 300)
    failed = state()
    assert failed["tank"]["sd_balance"] == 1000 and not bytes.fromhex(failed["companion"]["active"])[9]
    saving()
    tap(100, 300)
    dressed = state()
    assert dressed["tank"]["sd_balance"] == 955, "Accessories are bought with doubloons, no XP gate"
    assert bytes.fromhex(dressed["companion"]["active"])[9:11] == bytes([1, 1])
    tap(330, 300)
    undressed = state()
    assert bytes.fromhex(undressed["companion"]["active"])[9:11] == bytes([0, 1])
    tap(100, 300)
    assert state()["tank"]["sd_balance"] == 955, "Re-equipping owned items must be free"
    tap(365, 100)
    assert state()["companion"] == dressed["companion"], "NEXT FISH must not move ownership"
    tap(365, 100); tap(60, 180)
    assert lib.sim_companion_section() == 6, "Accessory colors open from Wardrobe"
    before_colors = state()
    natural = frame()
    tap(220, 234); tap(220, 268)
    assert frame() != natural, "Accessory color controls must update the actual preview"
    assert state()["companion"] == before_colors["companion"], "Color preview must not save early"
    failing("colors")
    tap(100, 309)
    assert state()["companion"] == before_colors["companion"], "Failed color saves must roll back"
    saving()
    tap(100, 309)
    colored = state()
    assert bytes.fromhex(colored["companion"]["active"])[11] == 0x11
    assert colored["tank"]["sd_balance"] == 955, "Accessory fabric colors stay free"
    for key in ("fish_variety", "fish_pattern", "fish"):
        assert colored["tank"].get(key) == before_colors["tank"].get(key), (key, "Gear colors never edit fish looks")
    assert colored["companion"]["creature_colors"] == before_colors["companion"]["creature_colors"]
    assert colored["companion"]["mussel_colors"] == before_colors["companion"]["mussel_colors"]
    tap(380, 24); tap(380, 24); tap(380, 24)
    assert lib.sim_page() == 9 and lib.sim_companion_section() == -1, "Wardrobe Back returns via Summary to Main"

    lib.sim_action(2); tap(100, 100)
    failing("scale")
    tap(200, 140)
    assert lib.sim_width() == colored["width"]
    assert state()["companion"]["display_compact"] == colored["companion"]["display_compact"]
    saving()
    for width in ((240, 320) if colored["width"] == 320 else (320, 240)):
        tap(200, 140)
        scaled = state()
        assert lib.sim_width() == scaled["width"] == width and lib.sim_height() == width * 3 // 4
        assert scaled["companion"]["display_compact"] == (width == 320)
        assert scaled["tank"]["sd_balance"] == colored["tank"]["sd_balance"]
        restore(scaled)
        assert lib.sim_width() == width

        wardrobe()
        wardrobe_view = frame()
        tap(60, 180)
        colors_view = frame()
        tap(365, 100)
        assert frame() == colors_view, "Removed duplicate BACK must not remain an invisible hit target"
        color_state = state()["companion"]
        tap(220, 234)
        assert frame() != colors_view, "Gear color changes remain previews"
        tap(380, 24)
        assert frame() == wardrobe_view, "The sole header BACK returns exactly one level without saving"
        assert state()["companion"] == color_state
        tap(60, 180)
        assert frame() == colors_view, "Reopening gear colors discards unsaved draft colors"
        tap(380, 24); tap(380, 24); tap(380, 24)
        assert lib.sim_page() == 9 and lib.sim_companion_section() == -1

        # Category -> child -> Back returns to the exact category screen used.
        for category, child, page, section, back in (
                ((100, 100), (100, 100), 9, 33, (380, 24)),
                ((100, 100), (100, 180), 9, 15, (380, 24)),
                ((100, 100), (330, 180), 9, 19, (380, 24)),
                ((100, 100), (100, 260), 9, 8, (380, 24)),
                ((100, 100), (330, 260), 9, 4, (380, 24)),
                ((100, 100), (370, 332), 6, -1, (392, 19)),
                ((330, 100), (100, 100), 7, -1, (390, 24)),
                ((330, 100), (330, 100), 3, -1, (392, 326)),
                ((330, 100), (100, 180), 9, 34, (380, 24)),
                ((330, 100), (330, 180), 5, -1, (392, 19)),
                ((330, 100), (100, 260), 9, 7, (380, 24)),
                ((100, 180), (100, 100), 9, 26, (380, 24)),
                ((100, 180), (330, 100), 9, 29, (380, 24)),
                ((330, 260), (60, 120), 9, 36, (380, 24))):
            main(); tap(*category)
            if category == (100, 180):
                assert lib.sim_companion_section() == 26, "MARKET opens stock first"
                tap(380, 24)
            parent = frame()
            tap(*child)
            assert (lib.sim_page(), lib.sim_companion_section()) == (page, section), \
                (width, category, child, lib.sim_page(), lib.sim_companion_section())
            frame(); tap(*back)
            assert lib.sim_page() == 9 and frame() == parent, (width, category, child, "Back returns to the category used")
        main(); tap(400, 334)
        assert lib.sim_page() == 2, "The SETTINGS cog stays on Main"
        main(); tap(100, 260)
        assert lib.sim_page() == 8, "CASINO opens from Main"
        main(); tap(330, 180)
        assert lib.sim_page() == 8, "GAMES opens the single list directly"
        for category, empty in (((330, 100), ((330, 260),)),
                                ((100, 180), ((330, 260),))):
            main(); tap(*category)
            if category == (100, 180):
                tap(380, 24)
            parent = frame()
            for point in empty:
                tap(*point)
                assert lib.sim_page() == 9 and lib.sim_companion_section() == -1 and frame() == parent, \
                    (width, category, point, "Retired collections/trophies/firsts entries must stay inert")

        polish_before = state()
        fish((100, 260))
        life_frame = frame()
        assert lib.sim_companion_section() == 8, "Fish life opens from the Fish hub"
        for x, y in ((100, 272), (330, 272), (60, 320), (220, 320), (370, 320)):
            tap(x, y)
            assert frame() == life_frame, "Removed nested fish links must be inert"
        tap(380, 24)
        assert lib.sim_page() == 9 and lib.sim_companion_section() == -1, "Life Back returns to the Fish hub"
        polish_after = state()
        polish_after["tank"]["saved_unix"] = polish_before["tank"]["saved_unix"]
        assert polish_after == polish_before, "Browsing summary/life/family/genes must not change the aquarium"

        main(); tap(330, 100); tap(100, 100)
        assert lib.sim_page() == 7, "TANK SUMMARY owns tank identity"
        tap(316, 339); tap(150, 166)
        identity = state()
        assert identity["polish"]["subtitle"] == (polish_before["polish"]["subtitle"] + 1) % 13
        failing("identity")
        tap(150, 206)
        assert state()["polish"] == identity["polish"], "Failed emblem save must roll back"
        saving()
        identity_frame = frame()
        name_before = state()
        tap(150, 130)
        assert frame() != identity_frame, "Tank rename opens QWERTY from Name & Identity"
        tap(392, 19)
        assert lib.sim_page() == 7 and state() == name_before, "Cancel tank naming returns to identity unchanged"
        tap(380, 24)
        assert lib.sim_page() == 7, "Identity Back returns to Tank Summary"
        tap(316, 339); tap(294, 24)
        assert lib.sim_page() == 9 and lib.sim_companion_section() == -1, "Identity Menu reaches Main directly"

        lib.sim_action(2); tap(330, 100)
        clock_frame = frame()
        tap(100, 180)
        assert lib.sim_network_page() == 0 and frame() != clock_frame, "EFFECTS opens clock-only options, never Wi-Fi"
        tap(200, 276)
        assert state()["companion"]["captions"] != identity["companion"]["captions"]
        effects_before = state()
        tap(200, 228)
        assert state()["companion"]["lively"] != effects_before["companion"]["lively"]
        failing("effects")
        tap(200, 228)
        assert state()["companion"]["lively"] != effects_before["companion"]["lively"], "Failed sparkle save must roll back"
        saving()
        tap(380, 24)
        settings_pixels = width * lib.sim_height() * 2 * 336 // 368
        assert frame()[:settings_pixels] == clock_frame[:settings_pixels], "Effects Back returns to Clock settings"
        lib.sim_action(2); tap(100, 260)
        options_before = state()
        tap(200, 324)
        assert state()["companion"]["lively"] == options_before["companion"]["lively"], "Clock effects no longer belong in Care"
        failing("care")
        tap(200, 248)
        assert state()["companion"]["cozy"] == options_before["companion"]["cozy"], "Failed Comfort save rolls back"
        saving()

        for kind, card in enumerate((98, 99, 97, 96)):
            fixture = copy.deepcopy(scaled)
            fixture["tank"]["sd_unlocks"] = 1 << (5, 1, 6, 7)[kind]
            fixture["tank"].update(shrimp_n=10, snail_x=190, snail_y=160, urchin_x=190)
            fixture["tank"].update(clock_theme=0, setup_pending=0, newborn_p1=0)
            fixture["tank"]["tank_ms_bits"] = 0xffff
            for species in ("snail", "urchin", "seahorse"):
                fixture["creatures"][f"{species}_adults"] = 0
                fixture["creatures"][f"{species}_offspring"] = 0
                fixture["creatures"][f"{species}_seconds"] = 0
            fixture["creatures"]["paid_adults"] = [0, 0, 0]
            if kind:
                fixture["creatures"][f"{('snail', 'urchin', 'seahorse')[kind - 1]}_adults"] = 1
            # Creature ownership fixtures are independent saves, not livestock rewinds.
            os.environ["POCKET_TANK_SAVE"] = str(Path(save_path).parent / f"life-creature-{width}-{kind}.sav")
            restore(fixture)
            for _ in range(120):
                point = lib.sim_creature_target(card)
                if point >= 0:
                    lib.sim_touch(point % width, point // width, 1)
                    lib.sim_touch(point % width, point // width, 0)
                    if lib.sim_selected() == card:
                        break
                lib.sim_frame(.1)
            assert point >= 0, (width, kind, "Creature must be reachable without fish overlap")
            assert lib.sim_selected() == card, (width, kind, "Pending notices must dismiss before inspection")
            frame()
            colors_before = state()["companion"]
            for x, y in ((224, 300), (224, 280), (224, 320)):
                tap(x, y)
                assert lib.sim_page() == 0, (width, kind, "Creature cards have no COLORS shortcut")
                if lib.sim_selected() != card:
                    break
            after_card = state()["companion"]
            assert after_card["creature_colors"] == colors_before["creature_colors"]
            assert after_card["mussel_colors"] == colors_before["mussel_colors"]
        saving()
        lib.sim_action(2); tap(100, 100)
    os.environ["POCKET_TANK_SAVE"] = str(Path(save_path).parent / "life-original.sav")
    restore(original)
    os.environ["POCKET_TANK_SAVE"] = save_path
    print("PASS: unified FISH hub/TANK/MARKET/GAMES/CASINO/HELP life routes, Stories, paid wardrobe, "
          "gear-only colors, identity in Tank Summary, inert retired targets, no creature color routes, 320/240")
