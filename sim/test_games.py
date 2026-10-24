"""Play all twelve games through public touch/frame APIs, using rendered cues."""
import ctypes
import json
import os


def exercise(lib, backup, output):
    def tap(x, y):
        lib.sim_touch(x * lib.sim_width() / 448, y * lib.sim_height() / 368, 1)
        lib.sim_touch(x * lib.sim_width() / 448, y * lib.sim_height() / 368, 0)

    def frame(dt=0):
        return list(lib.sim_frame(dt)[:lib.sim_width() * lib.sim_height()])

    def rgb(color):
        return ((color >> 19) << 11) | (((color >> 10) & 63) << 5) | ((color >> 3) & 31)

    def cue(pixels, x, y, color, radius=3):
        nx, ny = round(x * lib.sim_width() / 448), round(y * lib.sim_height() / 368)
        return any(pixels[j * lib.sim_width() + i] == rgb(color)
                   for j in range(max(0, ny - radius), min(lib.sim_height(), ny + radius + 1))
                   for i in range(max(0, nx - radius), min(lib.sim_width(), nx + radius + 1)))

    def snapshot():
        data = json.loads(backup(lib))
        data["tank"].pop("saved_unix", None)
        data["companion"].pop("powered_s", None)
        return data

    original = json.loads(backup(lib))
    screens = set()
    pause_footer = None
    for game in range(6):
        lib.sim_action(7)
        tap(100, 260)
        assert lib.sim_page() == 8
        menu = snapshot()
        frame()
        for _ in range(4):
            frame(0.25)
        assert snapshot() == menu, "The games menu must pause aquarium care"
        tap(110 + game % 2 * 204, 140 + game // 2 * 64)
        screens.add(ctypes.string_at(lib.sim_frame(0), lib.sim_width() * lib.sim_height() * 2))
        footer = frame()[round(166 * lib.sim_height() / 180) * lib.sim_width():]
        if pause_footer is None:
            pause_footer = footer
        assert footer == pause_footer, ("Every game must retain the complete pause footer", game)
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
        expected["companion"] = claimed["companion"]
        assert claimed == expected, "Only coins, Journal XP and companion game achievements may change"
        assert claimed["companion"]["game_rounds"][game] >= 1
        tap(224, 226)
        assert snapshot() == claimed, "Claims must be single-use"
        tap(224, 290)  # Replay.
        assert snapshot() == claimed
        tap(380, 20)  # Game -> menu -> Home.
        tap(380, 20)
        assert lib.sim_page() == 9
    assert len(screens) == 6, "Every game needs its own actual-renderer screen"
    lib.sim_action(7)
    tap(100, 260)
    tap(110, 140)
    no_score = snapshot()
    for _ in range(120):
        frame(0.25)
    tap(224, 226)
    assert snapshot() == no_score, "An unsuccessful round must not pay coins or XP"
    tap(380, 20)
    tap(380, 20)
    assert lib.sim_page() == 9, "A zero-reward round needs no discard confirmation"
    lib.sim_action(0)
    frame(0.25)
    assert snapshot()["tank"]["clock"] > no_score["tank"]["clock"], "Aquarium simulation must resume on exit"
    lib.sim_action(7)
    tap(100, 260)
    tap(80, 330)  # Relaxed: timed games now finish explicitly.
    tap(110, 140)
    relaxed_before = snapshot()
    for _ in range(124):
        frame(0.25)
    assert snapshot() == relaxed_before, "Relaxed play must pause care beyond the challenge timer"
    tap(350, 70)  # Finish an untimed round.
    tap(380, 20)
    tap(380, 20)
    assert lib.sim_page() == 9
    lib.sim_action(7)
    tap(100, 260)
    tap(80, 330)  # Return to challenge mode for subsequent sessions.
    tap(300, 330)
    records = frame()
    assert any(records), "Persistent game records page must render"
    tap(380, 20)
    tap(380, 20)
    # Second page: six different mechanics, each using only actual touch/render APIs.
    old_six = snapshot()["companion"]
    new_screens = set()
    for game in range(6):
        lib.sim_action(7)
        tap(100, 260)
        if game == 0:
            tap(350, 80)
        tap(110 + game % 2 * 204, 140 + game // 2 * 64)
        new_screens.add(ctypes.string_at(lib.sim_frame(0), lib.sim_width() * lib.sim_height() * 2))
        before = snapshot()
        for _ in range(8):
            frame(0.25)
        assert snapshot() == before, "Instructions must pause both game and aquarium"
        tap(224, 302)  # Dismiss HOW TO PLAY.
        initial = frame()
        tap(280, 20)  # Explicit pause.
        paused = frame()
        for _ in range(12):
            frame(0.25)
            tap(180, 200)
        assert frame() == paused, "Pause must freeze game physics and ignore board touches"
        tap(280, 20)
        assert frame() == initial, "Resume must retain the exact game position"
        if game == 0:  # Defender: aim at visible invaders, leading their descent.
            for _ in range(1100):
                pixels = frame(0.25)
                target = next(((x, y) for y in range(112, 280, 12) for x in range(36, 416, 12)
                               if cue(pixels, x, y, 0xffdc73, 1) or cue(pixels, x, y, 0xff8173, 1)), None)
                if target:
                    tap(target[0], min(280, target[1] + 12))
        elif game == 1:  # Runner: follow the actual pearl lane in each scrolling gate.
            for _ in range(350):
                pixels = frame(0.25)
                target = next((i for i in range(5) if any(cue(pixels, 62 + i * 80, y, 0xffdc73, 2)
                                                          for y in range(112, 292, 6))), None)
                if target is not None:
                    tap(62 + target * 80, 304)
        elif game == 2:  # Breaker: track the real white ball and move underneath it.
            tap(224, 260)
            for _ in range(2500):
                pixels = frame(0.125)
                w, h = lib.sim_width(), lib.sim_height()
                points = [(i % w, i // w) for i in range(round(106 * h / 368) * w, round(312 * h / 368) * w)
                          if pixels[i] == rgb(0xffffff)]
                if points:
                    x = sum(p[0] for p in points) / len(points) * 448 / w
                    tap(x + (18 if (_ // 100) % 2 else -18), 310)
            tap(350, 70)  # Bank the earned brick score without requiring a full clear.
        elif game == 3:  # Tactics: BFS a route to flags; attack enemies as encountered.
            pos = 22
            for _ in range(22):
                pixels = frame()
                board = []
                for i in range(25):
                    x, y = 102 + i % 5 * 50, 95 + i // 5 * 40
                    board.append(3 if cue(pixels, x, y, 0x785b6a, 1) else
                                 4 if cue(pixels, x, y, 0xffdc73, 1) else
                                 2 if cue(pixels, x, y, 0xff8173, 1) else 0)
                queue = [(pos, [])]
                seen = {pos}
                route = None
                while queue:
                    cell, path = queue.pop(0)
                    if board[cell] == 4:
                        route = path
                        break
                    for j in range(25):
                        if j not in seen and board[j] != 3 and abs(j % 5 - cell % 5) + abs(j // 5 - cell // 5) == 1:
                            seen.add(j)
                            queue.append((j, path + [j]))
                if not route:
                    break
                cell = route[0]
                tap(122 + cell % 5 * 50, 110 + cell // 5 * 40)
                if board[cell] != 2:
                    pos = cell
        elif game == 4:  # Architect: a mixed habitat, then remove and replace one tile.
            cells = [i for i in range(25) if i not in (6, 18)][:12]
            for n, cell in enumerate(cells):
                tap(80 if n % 2 == 0 else 220, 312)
                tap(122 + cell % 5 * 50, 110 + cell // 5 * 40)
            cell = cells[-1]
            tap(122 + cell % 5 * 50, 110 + cell // 5 * 40)
            tap(122 + cell % 5 * 50, 110 + cell // 5 * 40)
            tap(360, 312)
        else:  # Expedition: complete two routes then deliberately bank loot safely.
            tap(80, 200)
            tap(80, 200)
            tap(224, 312)
        assert snapshot() == before, "New games must never mutate live care or fish"
        tap(380, 20)
        assert snapshot() == before, "First BACK must protect an unclaimed second-page reward"
        save_path = os.environ["POCKET_TANK_SAVE"]
        os.environ["POCKET_TANK_SAVE"] = str(output / "missing" / "game-two.sav")
        tap(224, 226)
        assert snapshot() == before, "Second-page records and rewards must roll back on failure"
        os.environ["POCKET_TANK_SAVE"] = save_path
        tap(224, 226)
        claimed = snapshot()
        dollars = claimed["tank"]["sd_balance"] - before["tank"]["sd_balance"]
        assert 1 <= dollars <= 10, ("New game must reach a meaningful payable result", game, dollars)
        assert claimed["games"]["rounds"][game] == before["games"]["rounds"][game] + 1
        assert claimed["games"]["best"][game] >= 4
        assert claimed["companion"] == old_six, "Original six records must remain byte-for-byte unchanged"
        tap(224, 226)
        assert snapshot() == claimed, "Repeated claims must not pay twice"
        tap(380, 20)
        tap(380, 20)
        encoded = backup(lib)
        assert lib.sim_restore(encoded, len(encoded)), "Current-schema records must round-trip"
        assert snapshot()["games"] == claimed["games"]
    assert len(new_screens) == 6, "Second-page games require six distinct instruction screens"
    saved_games = snapshot()["games"]
    saved_path = os.environ["POCKET_TANK_SAVE"]
    binary = open(saved_path, "rb").read()
    assert len(binary) == 5912, "Game records and RPG must retain their append-only save tails"
    icons = (output / "icons.bin").read_bytes()
    assert lib.sim_init(icons, len(icons))
    assert snapshot()["games"] == saved_games, "All six new records must survive a binary reload"
    assert snapshot()["companion"] == old_six
    with open(saved_path, "wb") as stream:
        stream.write(binary[:5848])
    assert lib.sim_init(icons, len(icons))
    assert snapshot()["companion"] == old_six, "A pre-expansion binary save must keep original records"
    assert snapshot()["games"]["best"] == [0] * 6
    with open(saved_path, "wb") as stream:
        stream.write(binary)
    assert lib.sim_init(icons, len(icons))
    assert snapshot()["games"] == saved_games
    for mutate in (
        lambda g: g.update(version=2),
        lambda g: g.update(version=0),
        lambda g: g.update(medals=[4] * 6),
        lambda g: g.update(rounds=[1] * 5),
    ):
        malformed = json.loads(backup(lib))
        mutate(malformed["games"])
        encoded = json.dumps(malformed).encode()
        before = snapshot()
        assert not lib.sim_restore(encoded, len(encoded)), "Malformed records must be rejected atomically"
        assert snapshot() == before
    # A genuine pre-expansion backup migrates without touching its six existing records.
    legacy = json.loads(backup(lib))
    legacy["schema"] = 12
    legacy.pop("games")
    legacy.pop("rpg", None)
    encoded = json.dumps(legacy).encode()
    assert lib.sim_restore(encoded, len(encoded))
    migrated = snapshot()
    assert migrated["companion"] == old_six
    assert migrated["games"]["best"] == [0] * 6
    assert migrated["games"]["rounds"] == [0] * 6
    lib.sim_action(7)
    tap(100, 260)
    tap(350, 80)  # Restore first menu page for later tests.
    tap(380, 20)
    encoded = json.dumps(original).encode()
    assert lib.sim_restore(encoded, len(encoded))
    print("PASS: twelve touch games, pause, aquarium isolation, transactional records/rewards, retries, single claims and schema-12 migration")
