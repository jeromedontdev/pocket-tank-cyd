"""Exercise the optional story campaign through the same touch UI as the board."""
import copy
import ctypes
import json
import os
from pathlib import Path


def exercise(lib, backup):
    def tap(x, y):
        lib.sim_touch(x * lib.sim_width() / 448, y * lib.sim_height() / 368, 1)
        lib.sim_touch(x * lib.sim_width() / 448, y * lib.sim_height() / 368, 0)

    def frame():
        return ctypes.string_at(lib.sim_frame(0), lib.sim_width() * lib.sim_height() * 2)

    def illustration():
        pixels = frame()
        width, height = lib.sim_width(), lib.sim_height()
        left, right = int(154 * width / 448), int(436 * width / 448)
        top, bottom = int(116 * height / 368), int(162 * height / 368)
        return b"".join(pixels[(row * width + left) * 2:(row * width + right) * 2]
                        for row in range(top, bottom))

    def snapshot():
        data = json.loads(backup(lib))
        data["tank"].pop("saved_unix", None)
        return data

    def restore(data):
        encoded = json.dumps(data).encode()
        assert lib.sim_restore(encoded, len(encoded)), lib.sim_error().decode()

    def open_story():
        lib.sim_action(7)
        tap(100, 180)
        tap(330, 100)

    original = json.loads(backup(lib))
    save_path = os.environ["POCKET_TANK_SAVE"]
    try:
        for width in (240, 320):
            fixture = copy.deepcopy(original)
            restore(fixture)
            if lib.sim_width() != width:
                lib.sim_action(2)
                tap(100, 100)
                tap(200, 140)
            frame()
            fixture = json.loads(backup(lib))
            baseline = snapshot()
            open_story()
            assert lib.sim_width() == width
            opening = frame()
            initial_art = illustration()
            assert len(set(initial_art[index:index + 2] for index in range(0, len(initial_art), 2))) >= 8, \
                "Story vignette must contain palette artwork, not an empty panel"
            assert illustration() == initial_art, "Static artwork does not animate or need background ticks"
            tap(380, 24)
            expeditions = frame()
            assert expeditions != opening, "Story BACK returns to its Adventures parent"
            tap(330, 100)
            assert frame() == opening, "Reopening a story does not advance it"
            browsed = snapshot()
            differences = {key: (baseline[key], browsed[key]) for key in baseline if baseline[key] != browsed[key]}
            assert browsed == baseline, ("Browsing the RPG must not change the aquarium", differences)
            tap(70, 132)
            journal = frame()
            assert journal != opening, "Campaign journal must be accessible"
            tap(100, 324)
            assert frame() != journal, "Story help must explain its optional systems"
            tap(380, 24)
            assert frame() == journal, "Help BACK returns to campaign journal"
            tap(380, 24)
            assert frame() == opening, "Journal BACK returns to the encounter"
            if not fixture["tank"]["n_fish"]:
                continue
            tap(370, 96)
            participant = frame()
            if fixture["tank"]["n_fish"] > 1:
                assert illustration() != initial_art, "The chosen real fish appears in the scene"
            assert snapshot() == baseline, "Choosing a guide must not change the actual fish"
            os.environ["POCKET_TANK_SAVE"] = str(Path(save_path).parent / "missing-rpg" / "save.sav")
            tap(180, 246)
            assert snapshot() == baseline, "Failed encounter saves must roll back progress and rewards"
            assert frame() != participant, "Save failure must be visible"
            os.environ["POCKET_TANK_SAVE"] = save_path
            screens = set()
            chapters = set()
            for encounter in range(18):
                screens.add(frame())
                if encounter % 3 == 0:
                    chapters.add(illustration())
                tap(180, 246 + encounter % 3 * 36)
                event = frame()
                tap(200, 320)
                assert frame() != event, "Saved encounters show a result before continuing"
            completed = snapshot()
            assert len(screens) >= 12, "Campaign must have distinct encounters, not a repeating choice screen"
            assert len(chapters) == 6, "Every chapter has its own illustrated landmark"
            assert completed["rpg"] != baseline["rpg"], "The campaign must persist real progress"
            for field in ("fish", "bank", "story"):
                assert completed[field] == baseline[field], ("Story actions must not alter fish care/genetics", field)
            for field in ("n_fish", "sd_unlocks", "sand_style", "water_style", "clock_theme", "clock_layout"):
                assert completed["tank"][field] == baseline["tank"][field], ("Story must not replace aquarium settings", field)
            assert lib.sim_save()
            saved = json.loads(backup(lib))
            restore(saved)
            open_story()
            resumed = snapshot()
            differences = {key: (completed[key], resumed[key]) for key in completed if completed[key] != resumed[key]}
            assert resumed == completed, ("Completed campaign survives a binary save and JSON reload", differences)
            for _ in range(4):
                tap(180, 246)
            assert snapshot() == completed, "Completed chapters cannot be replayed to farm rewards"
            tap(70, 132)
            assert frame() != opening, "Completed campaign retains its contributor journal"
        print("PASS: optional Lost Current UI, both scales, journal/help/back, guide previews, save rollback, 18 encounters, completion and aquarium isolation")
    finally:
        os.environ["POCKET_TANK_SAVE"] = save_path
        restore(original)
