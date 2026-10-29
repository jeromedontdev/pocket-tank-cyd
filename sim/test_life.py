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
        lib.sim_action(7)

    def care():
        home(); tap(100, 336); tap(100, 100)

    lib.sim_action(1)
    frame(); tap(392, 326)  # Acknowledge existing firsts before testing read-only navigation.
    lib.sim_action(1)
    keeper = frame()
    original = json.loads(backup(lib))
    tap(375, 356)
    assert lib.sim_page() == 1, "Removed Names & Looks HOME must not remain a hidden target"
    home()
    assert lib.sim_page() == 9
    hub = frame()
    assert hub != keeper
    tap(100, 100)
    assert lib.sim_page() == 1, "Names & Looks must be on the first Home screen"
    tap(392, 326)
    assert frame() == hub, "Direct Names & Looks Back must return to Home"
    tap(100, 336)
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
    lib.sim_action(2); tap(100, 260)
    tap(200, 248)
    changed_care = json.loads(backup(lib))
    assert changed_care["companion"]["cozy"] != original["companion"]["cozy"]
    changed_care["tank"]["saved_unix"] = after["tank"]["saved_unix"]
    assert changed_care["tank"] == after["tank"], "Changing care mode must not fabricate care rewards"
    care(); tap(210, 124)
    hello = json.loads(backup(lib))
    assert int.from_bytes(bytes.fromhex(hello["companion"]["active"])[4:6], "little") == 5
    tap(210, 124)
    assert json.loads(backup(lib))["companion"] == hello["companion"], "Hello cooldown must prevent repeated bond farming"
    for _ in range(124):
        lib.sim_frame(0.25)
    tap(210, 124)
    second = json.loads(backup(lib))
    assert int.from_bytes(bytes.fromhex(second["companion"]["active"])[4:6], "little") == 10, "Hello cooldown must expire while CARE remains open"
    tap(60, 258)
    trick = json.loads(backup(lib))
    assert bytes.fromhex(trick["companion"]["active"])[8] & 1, "DART must unlock and persist at bond 10"
    tap(380, 24); tap(380, 24); tap(100, 180)
    tap(100, 100)
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
    home(); tap(100, 180); tap(100, 100)
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
    home(); tap(100, 336); tap(100, 180)
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
    home(); tap(100, 336); tap(100, 180)
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
    tap(380, 24); tap(380, 24); tap(380, 24); tap(330, 100); tap(330, 180)
    before_creatures = json.loads(backup(lib))
    tap(220, 234); tap(365, 100)
    assert json.loads(backup(lib))["companion"] == before_creatures["companion"], "Switching creatures discards an unsaved preview"
    for _ in range(5):
        tap(220, 234); tap(220, 268); tap(100, 309); tap(365, 100)
    creatures = json.loads(backup(lib))
    assert creatures["companion"]["creature_colors"] == [0x11] * 4
    assert creatures["companion"]["mussel_colors"] == 0x11
    assert creatures["tank"]["sd_balance"] == 955
    tap(330, 309)
    assert json.loads(backup(lib))["companion"] == creatures["companion"], "NATURAL must remain a preview until saved"
    tap(100, 309)
    reset = json.loads(backup(lib))
    assert (reset["companion"]["creature_colors"] + [reset["companion"]["mussel_colors"]]).count(0) == 1
    assert reset["tank"]["journal_xp"] == wardrobe["tank"]["journal_xp"], "Cosmetics must not grant XP"
    lib.sim_action(2); tap(100, 100)
    os.environ["POCKET_TANK_SAVE"] = str(Path(save_path).parent / "missing-scale" / "save.sav")
    tap(200, 140)
    assert lib.sim_width() == reset["width"]
    assert json.loads(backup(lib))["companion"]["display_compact"] == reset["companion"]["display_compact"]
    os.environ["POCKET_TANK_SAVE"] = save_path
    for width in ((240, 320) if reset["width"] == 320 else (320, 240)):
        tap(200, 140)
        scaled = json.loads(backup(lib))
        assert lib.sim_width() == scaled["width"] == width and lib.sim_height() == width * 3 // 4
        assert scaled["companion"]["display_compact"] == (width == 320)
        assert scaled["tank"]["sd_balance"] == reset["tank"]["sd_balance"]
        data = json.dumps(scaled).encode()
        assert lib.sim_restore(data, len(data)) and lib.sim_width() == width
        home()
        home_frame = frame()
        guide_before = json.loads(backup(lib))
        tap(330, 336)
        guide_pages = [frame()]
        for _ in range(2):
            tap(330, 326)
            guide_pages.append(frame())
        assert len(set(guide_pages)) == 3, "All symbol guide pages must be distinct and reachable"
        tap(330, 326)
        assert frame() == guide_pages[0], "Symbol guide NEXT wraps"
        tap(100, 326)
        assert frame() == guide_pages[2], "Symbol guide PREVIOUS wraps"
        tap(380, 24)
        assert frame() == home_frame, "Symbol guide Back must return Home"
        guide_after = json.loads(backup(lib))
        guide_after["tank"]["saved_unix"] = guide_before["tank"]["saved_unix"]
        assert guide_after == guide_before, "Guide navigation must not feed, spend, save or change tools"
        home(); tap(100, 336); tap(100, 180)
        wardrobe_view = frame()
        tap(60, 180)
        colors_view = frame()
        tap(365, 100)
        assert frame() == colors_view, "Removed duplicate BACK must not remain an invisible hit target"
        color_state = json.loads(backup(lib))["companion"]
        tap(220, 234)
        assert frame() != colors_view, "Gear color changes remain previews"
        tap(380, 24)
        assert frame() == wardrobe_view, "The sole header BACK returns exactly one level without saving"
        assert json.loads(backup(lib))["companion"] == color_state
        tap(60, 180)
        assert frame() == colors_view, "Reopening gear colors discards unsaved draft colors"
        tap(380, 24); tap(380, 24)
        assert lib.sim_page() == 9, "Wardrobe BACK returns to the Fish category"
        for category, child, destination in (
                ((100, 336), (330, 100), 6),
                ((100, 100), None, 1),
                ((330, 100), (100, 100), 3),
                ((330, 100), (330, 100), 7),
                ((330, 180), (100, 100), 5),
                ((100, 260), None, 8),
                ((330, 260), None, 2)):
            home(); tap(*category)
            if child:
                parent = frame(); tap(*child)
            assert lib.sim_page() == destination
            frame(); tap(392, 326 if destination in (1, 3) else 19)
            assert lib.sim_page() == 9, (width, category, destination, "Back must return Home/category")
            if child:
                assert frame() == parent, "Back must return to the category actually used"
        home(); tap(100, 336); tap(330, 180)
        assert lib.sim_page() == 1
        for point in ((70, 356), (170, 356), (275, 356), (375, 356), (70, 326), (220, 326)):
            tap(*point)
            assert lib.sim_page() == 1, "Removed Keeper shortcuts must not remain invisible hit targets"
        tap(392, 326)
        assert lib.sim_page() == 9, "Keeper opened from Fish must return to Fish"
        home(); tap(330, 180)
        collections = frame()
        tap(100, 180)
        assert lib.sim_page() == 9 and frame() != collections, "Display Rewards must have its own entry"
        tap(380, 24); assert frame() == collections
        rewards = copy.deepcopy(scaled)
        rewards["companion"].update(discoveries=0xc0, collections=0, cosmetic=0, decor=0)
        data = json.dumps(rewards).encode(); assert lib.sim_restore(data, len(data))
        home(); tap(330, 180); tap(330, 100)
        album = frame(); tap(100, 138); assert frame() != album
        tap(380, 24); assert frame() == album, "Hint Back must return to Discoveries"
        tap(380, 24); tap(100, 180); tap(150, 178)
        assert json.loads(backup(lib))["companion"]["collections"] == 2, "Friendship CLAIM must use the displayed row"
        assert json.loads(backup(lib))["companion"]["cosmetic"] == 2
        tap(330, 257)
        assert json.loads(backup(lib))["companion"]["cosmetic"] == 0, "ORIGINAL is always available"
        tap(330, 257)
        assert json.loads(backup(lib))["companion"]["cosmetic"] == 2, "FRAME cycles only unlocked rewards"
        tap(100, 257)
        assert json.loads(backup(lib))["companion"]["decor"] == 1, "TROPHIES changes the sand display"
        frames_fixture = copy.deepcopy(rewards)
        frames_fixture["companion"].update(discoveries=0x7ff, collections=7, cosmetic=0)
        data = json.dumps(frames_fixture).encode(); assert lib.sim_restore(data, len(data))
        home(); tap(330, 180); tap(100, 180)
        heights = []
        for _ in range(4):
            pixels = lib.sim_frame(0)
            screen_height = lib.sim_height()
            rows = []
            for y in range(246 * screen_height // 368, 272 * screen_height // 368):
                for x in range(237 * width // 448, 432 * width // 448):
                    pixel = pixels[y * width + x]
                    if pixel >> 11 >= 24 and (pixel >> 5 & 63) >= 45:
                        rows.append(y)
                        break
            assert rows, "Frame option lettering must remain visible"
            heights.append(rows[-1] - rows[0] + 1)
            tap(330, 257)
        assert len(set(heights)) == 1, ("Frame font must stay consistent across all four choices", heights)
        data = json.dumps(scaled).encode(); assert lib.sim_restore(data, len(data))
        lib.sim_action(2); tap(100, 260)
        options_before = json.loads(backup(lib))
        tap(200, 324)
        assert json.loads(backup(lib))["companion"]["lively"] == options_before["companion"]["lively"], "Clock effects no longer belong in Care"
        os.environ["POCKET_TANK_SAVE"] = str(Path(save_path).parent / "missing-care" / "save.sav")
        tap(200, 248)
        assert json.loads(backup(lib))["companion"]["cozy"] == options_before["companion"]["cozy"], "Failed Comfort save rolls back"
        os.environ["POCKET_TANK_SAVE"] = save_path
        care()
        care_frame = frame()
        tap(100, 320); help_frame = frame(); assert help_frame != care_frame
        tap(380, 24); assert frame() == care_frame, "Help has one Back, returning to interactions"
        polish_before = json.loads(backup(lib))
        home(); tap(100, 336); tap(100, 260)
        life_frame = frame()
        assert life_frame != care_frame, "Fish Life opens from the Fish category, not Say Hello"
        for x, y in ((100, 272), (330, 272), (60, 320), (220, 320), (370, 320)):
            tap(x, y)
            assert frame() == life_frame, "Removed nested fish links must be inert"
        tap(380, 24)
        fish_menu = frame()
        tap(360, 336)
        more_fish = frame()
        assert more_fish != fish_menu
        for x, y in ((100, 100), (330, 100), (100, 180)):
            tap(x, y)
            assert frame() != more_fish, "Genes, Firsts and Keepsakes have their own entries"
            tap(380, 24)
            assert frame() == more_fish, "Each independent fish page returns to the menu"
        polish_after = json.loads(backup(lib))
        polish_after["tank"]["saved_unix"] = polish_before["tank"]["saved_unix"]
        assert polish_after == polish_before, "Browsing biographies/family/genes must not change the aquarium"
        home(); tap(330, 100); tap(330, 100); tap(300, 338); tap(150, 138)
        identity = json.loads(backup(lib))
        assert identity["polish"]["subtitle"] == (polish_before["polish"]["subtitle"] + 1) % 5
        save_path = os.environ["POCKET_TANK_SAVE"]
        os.environ["POCKET_TANK_SAVE"] = str(Path(save_path).parent / "missing-identity" / "save.sav")
        tap(150, 180)
        assert json.loads(backup(lib))["polish"] == identity["polish"], "Failed emblem save must roll back"
        os.environ["POCKET_TANK_SAVE"] = save_path
        tap(380, 24)
        assert lib.sim_page() == 7, "Identity Back returns to Aquariums"
        lib.sim_action(2); tap(330, 100)
        clock_frame = frame()
        tap(100, 180)
        assert lib.sim_network_page() == 0 and frame() != clock_frame, "EFFECTS opens clock-only options, never Wi-Fi"
        tap(200, 276)
        assert json.loads(backup(lib))["companion"]["captions"] != identity["companion"]["captions"]
        effects_before = json.loads(backup(lib))
        tap(200, 228)
        assert json.loads(backup(lib))["companion"]["lively"] != effects_before["companion"]["lively"]
        os.environ["POCKET_TANK_SAVE"] = str(Path(save_path).parent / "missing-effects" / "save.sav")
        tap(200, 228)
        assert json.loads(backup(lib))["companion"]["lively"] != effects_before["companion"]["lively"], "Failed sparkle save must roll back"
        os.environ["POCKET_TANK_SAVE"] = save_path
        tap(380, 24)
        settings_pixels = width * lib.sim_height() * 2 * 336 // 368
        assert frame()[:settings_pixels] == clock_frame[:settings_pixels], "Effects Back returns to Clock settings"
        home(); tap(330, 180)
        collections_frame = frame()
        tap(330, 180)
        visitors_frame = frame()
        for x in (60, 220, 370):
            tap(x, 320)
            assert frame() == visitors_frame, "Visitor Log must not expose unrelated tank record tabs"
        tap(380, 24); assert frame() == collections_frame
        for kind, card in enumerate((98, 99, 97, 96)):
            fixture = copy.deepcopy(scaled)
            fixture["tank"]["sd_unlocks"] = 1 << (5, 1, 6, 7)[kind]
            fixture["tank"].update(shrimp_n=10, snail_x=190, snail_y=160, urchin_x=190)
            fixture["tank"].update(clock_theme=0, setup_pending=0, newborn_p1=0)
            fixture["tank"]["tank_ms_bits"] = 0xffff
            data = json.dumps(fixture).encode()
            assert lib.sim_restore(data, len(data))
            for _ in range(120):
                point = lib.sim_creature_target(card)
                if point >= 0:
                    lib.sim_touch(point % width, point // width, 1)
                    lib.sim_touch(point % width, point // width, 0)
                    if lib.sim_selected() == card:
                        break
                lib.sim_frame(.1)
            assert point >= 0, (width, kind, "Creature must be reachable without fish overlap")
            assert lib.sim_selected() == card, (width, kind, "Pending milestone notices must dismiss before inspection")
            frame()
            tap(224, 300)
            assert lib.sim_page() == 9, (width, kind, lib.sim_selected(), "Creature COLORS shortcut must open the correct editor")
            colors_before = json.loads(backup(lib))["companion"]["creature_colors"]
            tap(220, 234); tap(100, 309)
            colors_after = json.loads(backup(lib))["companion"]["creature_colors"]
            assert colors_before[kind] != colors_after[kind]
            assert all(colors_before[i] == colors_after[i] for i in range(4) if i != kind)
            tap(380, 24)
            assert lib.sim_page() == 9, "Colors Back must return to Tank category"
        lib.sim_action(2); tap(100, 100)
    encoded = json.dumps(original).encode()
    assert lib.sim_restore(encoded, len(encoded))
    print("PASS: LIFE/wardrobe/color-page routing, five creature and accessory primary/detail previews, explicit saves/Natural reset, free colors, purchase/equip/remove, rollback, bonds and real adventures")
