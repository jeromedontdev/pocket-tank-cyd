"""Play the six games through public touch/frame APIs, using rendered cues."""
import ctypes
import json
import os


def exercise(lib, backup, output):
    def tap(x, y):
        lib.sim_touch(x * 240 / 448, y * 180 / 368, 1)
        lib.sim_touch(x * 240 / 448, y * 180 / 368, 0)

    def frame(dt=0):
        return list(lib.sim_frame(dt)[:240 * 180])

    def rgb(color):
        return ((color >> 19) << 11) | (((color >> 10) & 63) << 5) | ((color >> 3) & 31)

    def cue(pixels, x, y, color, radius=3):
        nx, ny = round(x * 240 / 448), round(y * 180 / 368)
        return any(pixels[j * 240 + i] == rgb(color)
                   for j in range(max(0, ny - radius), min(180, ny + radius + 1))
                   for i in range(max(0, nx - radius), min(240, nx + radius + 1)))

    def snapshot():
        data = json.loads(backup(lib))
        data["tank"].pop("saved_unix", None)
        return data

    original = json.loads(backup(lib))
    screens = set()
    for game in range(6):
        lib.sim_action(1)
        tap(360, 356)
        assert lib.sim_page() == 8
        menu = snapshot()
        menu_footer = frame()[166 * 240:]
        for _ in range(4):
            frame(0.25)
        assert snapshot() == menu, "The games menu must pause aquarium care"
        tap(110 + game % 2 * 204, 140 + game // 2 * 64)
        screens.add(ctypes.string_at(lib.sim_frame(0), 240 * 180 * 2))
        assert frame()[166 * 240:] == menu_footer, ("Game must retain the complete pause footer", game)
        before = snapshot()
        if game == 0:  # Bubble Pop: hit the actual visible ring centers.
            for _ in range(120):
                pixels = frame(0.25)
                for y in range(104, 320, 8):
                    for x in range(32, 424, 8):
                        if cue(pixels, x, y, 0x73e5ff, 1) or cue(pixels, x, y, 0xffdc73, 1):
                            tap(x, y)
        elif game == 1:  # Learn pairs by their displayed pearl colors.
            colors = (0xffdc73, 0x73e5ff, 0xffa3cf, 0x89ffb1, 0xc5adff, 0xff8173)
            learned, matched = {}, set()
            for _ in range(36):
                if len(matched) == 12:
                    break
                first = next(i for i in range(12) if i not in matched)
                tap(76 + first % 4 * 96, 126 + first // 4 * 72)
                pixels = frame()
                identity = next(c for c in range(6) if cue(pixels, 85 + first % 4 * 96, 125 + first // 4 * 72, colors[c], 8))
                learned[first] = identity
                candidates = [i for i, c in learned.items() if i != first and i not in matched and c == identity]
                second = candidates[0] if candidates else next(i for i in range(12) if i != first and i not in matched and i not in learned)
                tap(76 + second % 4 * 96, 126 + second // 4 * 72)
                if len(matched) == 10:
                    matched.update((first, second))
                    break
                pixels = frame()
                other = next(c for c in range(6) if cue(pixels, 85 + second % 4 * 96, 125 + second // 4 * 72, colors[c], 8))
                learned[second] = other
                if identity == other:
                    matched.update((first, second))
                else:
                    for _ in range(4):
                        frame(0.25)
            assert len(matched) == 12
        elif game == 2:  # Slalom: follow each gold open gate.
            lane = 1
            for _ in range(12):
                pixels = frame()
                target = next(i for i in range(3) if cue(pixels, 38 + i * 140, 113, 0xffdc73))
                while lane != target:
                    tap(320 if lane < target else 110, 302)
                    lane += 1 if lane < target else -1
                for _ in range(9):
                    frame(0.25)
        elif game == 3:  # Cast at the moving hook's exact treasure center.
            for _ in range(5):
                pixels = frame()
                target = next(i for i in range(3) if cue(pixels, 96 + i * 128, 244, 0xa97532))
                remaining = (target + 0.5) / 3 / 0.9
                while remaining > 0:
                    dt = min(0.25, remaining)
                    frame(dt)
                    remaining -= dt
                tap(224, 300)
        elif game == 4:  # Feed the requested snack, not the real aquarium.
            for _ in range(120):
                pixels = frame(0.25)
                if _ < 119:
                    target = next(i for i in range(3) if cue(pixels, 88 + i * 136, 256, 0xffdc73))
                    tap(88 + target * 136, 296)
        else:  # Solve the actual randomized plumbing, not a fixed tap sequence.
            pixels = frame()
            offsets = ((0, -31), (31, 0), (0, 31), (-31, 0))
            masks = [sum(1 << d for d, (dx, dy) in enumerate(offsets)
                         if cue(pixels, 145 + i % 3 * 72 + dx, 131 + i // 3 * 72 + dy, 0x73e5ff, 1))
                     for i in range(9)]

            def path(i, incoming, seen):
                for turns in range(4):
                    mask = masks[i]
                    for _ in range(turns):
                        mask = ((mask << 1) | (mask >> 3)) & 15
                    if not mask & (1 << incoming):
                        continue
                    if i == 5 and mask & 2:
                        return [(i, turns)]
                    for d, (dx, dy) in enumerate(((0, -1), (1, 0), (0, 1), (-1, 0))):
                        x, y = i % 3 + dx, i // 3 + dy
                        j = y * 3 + x
                        if d != incoming and mask & (1 << d) and 0 <= x < 3 and 0 <= y < 3 and j not in seen:
                            rest = path(j, (d + 2) % 4, seen | {j})
                            if rest:
                                return [(i, turns)] + rest
                return None

            solution = path(3, 3, {3})
            assert solution, ("Generated reef must have a real inlet-to-outlet solution", masks)
            for i, turns in solution:
                for _ in range(turns):
                    tap(145 + i % 3 * 72, 131 + i // 3 * 72)
        after_play = snapshot()
        assert after_play == before, "Playing must not change fish, care, wallet, goals or XP"
        tap(380, 20)
        tap(224, 290)
        assert snapshot() == before, "First BACK/replay tap must not discard an unclaimed reward"
        # Failed persistence must leave the complete aquarium untouched and allow retry.
        save_path = os.environ["POCKET_TANK_SAVE"]
        os.environ["POCKET_TANK_SAVE"] = str(output / "missing" / "reward.sav")
        tap(224, 226)
        assert snapshot() == before, "Failed reward saves must roll back coins AND XP"
        os.environ["POCKET_TANK_SAVE"] = save_path
        tap(224, 226)
        claimed = snapshot()
        dollars = claimed["tank"]["sd_balance"] - before["tank"]["sd_balance"]
        assert 1 <= dollars <= 10, ("Completed game must offer a bounded reward", game, dollars)
        assert claimed["tank"]["sd_earned"] - before["tank"]["sd_earned"] == dollars
        assert claimed["tank"]["journal_xp"] - before["tank"]["journal_xp"] == dollars * 3
        expected = json.loads(json.dumps(before))
        for key in ("sd_balance", "sd_earned", "journal_xp"):
            expected["tank"][key] = claimed["tank"][key]
        assert claimed == expected, "Only coins and Journal XP may change"
        tap(224, 226)
        assert snapshot() == claimed, "Claims must be single-use"
        tap(224, 290)  # Replay.
        assert snapshot() == claimed
        tap(380, 20)  # Game -> menu -> Keeper.
        tap(380, 20)
        assert lib.sim_page() == 1
    assert len(screens) == 6, "Every game needs its own actual-renderer screen"
    lib.sim_action(1)
    tap(360, 356)
    tap(110, 140)
    no_score = snapshot()
    for _ in range(120):
        frame(0.25)
    tap(224, 226)
    assert snapshot() == no_score, "An unsuccessful round must not pay coins or XP"
    tap(380, 20)
    tap(380, 20)
    assert lib.sim_page() == 1, "A zero-reward round needs no discard confirmation"
    lib.sim_action(0)
    frame(0.25)
    assert snapshot()["tank"]["clock"] > no_score["tank"]["clock"], "Aquarium simulation must resume on exit"
    encoded = json.dumps(original).encode()
    assert lib.sim_restore(encoded, len(encoded))
    print("PASS: all six games played through touch/render cues, pause, real-fish safety, transactional coins/XP, save retry, single claims, replay and exit")
