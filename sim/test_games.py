"""Play all eighteen games through public touch/frame APIs, using rendered cues."""
import ctypes
import heapq
import itertools
import json
import os
from pathlib import Path
import struct
import zlib


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

    def digit(pixels, x, y, color):
        glyphs = (
            (14, 17, 19, 21, 25, 17, 14), (4, 12, 4, 4, 4, 4, 14),
            (14, 17, 1, 2, 4, 8, 31), (31, 2, 4, 2, 1, 17, 14),
            (2, 6, 10, 18, 31, 2, 2), (31, 16, 30, 1, 1, 17, 14),
            (6, 8, 16, 30, 17, 17, 14), (31, 1, 2, 4, 8, 8, 8),
            (14, 17, 17, 14, 17, 17, 14), (14, 17, 17, 15, 1, 2, 12),
        )
        width, height = lib.sim_width(), lib.sim_height()
        left, top = x * width // 448, y * height // 368
        w, h = max(5, (10 * width + 224) // 448), max(7, (14 * height + 184) // 368)
        actual = tuple(pixels[(top + row) * width + left + col] == rgb(color)
                       for row in range(h) for col in range(w))
        matches = [n for n, glyph in enumerate(glyphs) if actual == tuple(
            all(glyph[(row * 7 + sy) // h] & (16 >> ((col * 5 + sx) // w))
                for sy in range(7) for sx in range(5))
            for row in range(h) for col in range(w))]
        assert len(matches) == 1, ("Displayed digit must be readable", x, y, matches)
        return matches[0]

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
        assert dollars == (1 if before["game_income"]["paid"][game] + before["game_income"]["legacy"] < 5 else 0), ("Per-game daily limit", game, dollars)
        assert claimed["tank"]["sd_earned"] - before["tank"]["sd_earned"] == dollars
        assert claimed["tank"]["journal_xp"] == before["tank"]["journal_xp"]
        expected = json.loads(json.dumps(before))
        for key in ("sd_balance", "sd_earned", "journal_xp"):
            expected["tank"][key] = claimed["tank"][key]
        expected["companion"] = claimed["companion"]
        expected["care"] = claimed["care"]
        expected["game_income"] = claimed["game_income"]
        assert claimed == expected, "Only bounded coins, daily quota and companion game achievements may change"
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
    unsuccessful = snapshot()
    assert unsuccessful["tank"] == no_score["tank"], "An unsuccessful round must not pay coins or XP"
    assert unsuccessful["companion"]["game_rounds"][0] == no_score["companion"]["game_rounds"][0] + 1
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
    tap(224, 226)  # A zero-bonus result can still be saved.
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
        assert dollars == (1 if before["game_income"]["paid"][game + 6] + before["game_income"]["legacy"] < 5 else 0), ("Per-game daily limit", game, dollars)
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
    # Third page has the same transactional rewards and separate saved records.
    extra_screens = set()
    for game in range(6):
        lib.sim_action(7)
        tap(100, 260)
        if game == 0:
            tap(350, 80)
        tap(110 + game % 2 * 204, 140 + game // 2 * 64)
        extra_screens.add(ctypes.string_at(lib.sim_frame(0), lib.sim_width() * lib.sim_height() * 2))
        before = snapshot()
        help_screen = frame()
        for _ in range(12):
            frame(0.25)
        assert frame() == help_screen, "Third-page instructions must freeze the whole game"
        tap(224, 302)
        initial = frame()
        tap(280, 20)
        paused = frame()
        for _ in range(8):
            frame(0.25)
            tap(170, 180)
        assert frame() == paused
        tap(280, 20)
        assert frame() == initial

        def active(pixels):
            return cue(pixels, 418, 76, 0x1c2f36, 1)  # FINISH is absent on results.

        tap(350, 70)  # Finishing without any play must never create farmable coins/XP.
        save_path = os.environ["POCKET_TANK_SAVE"]
        os.environ["POCKET_TANK_SAVE"] = str(output / "missing" / "empty-round.sav")
        tap(224, 226)
        assert snapshot() == before, "Zero-reward result saves must also roll back on failure"
        os.environ["POCKET_TANK_SAVE"] = save_path
        tap(224, 226)
        empty = snapshot()
        assert empty["tank"] == before["tank"], "Finishing an untouched game must award no coins or XP"
        assert empty["games_extra"]["rounds"][game] == before["games_extra"]["rounds"][game] + 1
        assert empty["games_extra"]["best"][game] == before["games_extra"]["best"][game]
        assert empty["games_extra"]["medals"][game] == before["games_extra"]["medals"][game]
        tap(224, 226)
        assert snapshot() == empty, "Saving an empty round repeatedly must not duplicate rounds"
        tap(224, 300)
        tap(224, 302)
        before = snapshot()

        if game == 0:  # Lighthouse: solve the displayed cross-toggle system over GF(2).
            pixels = frame()
            rows = []
            for i in range(16):
                lit = cue(pixels, 118 + i % 4 * 56, 106 + i // 4 * 48, 0xffdc73, 1)
                coefficients = sum(1 << j for j in range(16)
                                   if abs(i % 4 - j % 4) + abs(i // 4 - j // 4) <= 1)
                rows.append(coefficients | (int(lit) << 16))
            pivots = []
            for col in range(16):
                pivot = next((i for i in range(len(pivots), 16) if rows[i] & (1 << col)), None)
                if pivot is None:
                    continue
                rank = len(pivots)
                rows[rank], rows[pivot] = rows[pivot], rows[rank]
                for i in range(16):
                    if i != rank and rows[i] & (1 << col):
                        rows[i] ^= rows[rank]
                pivots.append(col)
            assert all(not row & (1 << 16) for row in rows[len(pivots):]), "Every lighthouse must be solvable"
            for row, cell in zip(rows, pivots):
                if row & (1 << 16):
                    tap(137 + cell % 4 * 56, 121 + cell // 4 * 48)
            assert not active(frame()), "All-dark lamps must finish the round"
        elif game == 1:  # Shell Slide: only tiles adjacent to the visible gap may move.
            pixels = frame()
            blank = next(i for i in range(9)
                         if cue(pixels, 134 + i % 3 * 64, 106 + i // 3 * 64, 0x102831, 1))
            invalid = next(i for i in range(9) if abs(i % 3 - blank % 3) + abs(i // 3 - blank // 3) > 1)
            tap(157 + invalid % 3 * 64, 129 + invalid // 3 * 64)
            assert frame() == pixels, "Nonadjacent sliding tiles must not move"
            valid = next(i for i in range(9) if abs(i % 3 - blank % 3) + abs(i // 3 - blank // 3) == 1)
            tap(157 + valid % 3 * 64, 129 + valid // 3 * 64)
            assert frame() != pixels, "A legal slide must move a real tile into the gap"
            pixels = frame()
            state = tuple(0 if cue(pixels, 134 + i % 3 * 64, 106 + i // 3 * 64, 0x102831, 1)
                          else digit(pixels, 152 + i % 3 * 64, 121 + i // 3 * 64, 0x031015)
                          for i in range(9))
            goal = (1, 2, 3, 4, 5, 6, 7, 8, 0)

            def distance(board):
                return sum(abs(i % 3 - (value - 1) % 3) + abs(i // 3 - (value - 1) // 3)
                           for i, value in enumerate(board) if value)

            queue, costs = [(distance(state), 0, state, [])], {state: 0}
            while queue:
                _, cost, board, route = heapq.heappop(queue)
                if board == goal:
                    break
                if costs[board] != cost:
                    continue
                blank = board.index(0)
                for cell in range(9):
                    if abs(cell % 3 - blank % 3) + abs(cell // 3 - blank // 3) != 1:
                        continue
                    next_board = list(board)
                    next_board[blank], next_board[cell] = next_board[cell], 0
                    next_board = tuple(next_board)
                    if cost + 1 < costs.get(next_board, 1000):
                        costs[next_board] = cost + 1
                        heapq.heappush(queue, (cost + 1 + distance(next_board), cost + 1,
                                               next_board, route + [cell]))
            else:
                raise AssertionError("Every shell slide must be solvable")
            for cell in route:
                tap(157 + cell % 3 * 64, 129 + cell // 3 * 64)
            assert not active(frame()), "Ordering all eight shells must finish"
        elif game == 2:  # Sonar: infer pearl locations from the visible row/column clues.
            pixels = frame()
            rows = [digit(pixels, 88, 120 + i * 36, 0xffdc73) for i in range(5)]
            cols = [digit(pixels, 128 + i * 44, 90, 0xffdc73) for i in range(5)]
            candidates = []
            patterns = [[sum(1 << col for col in cells) for cells in itertools.combinations(range(5), count)]
                        for count in rows]
            for row_masks in itertools.product(*patterns):
                if all(sum(bool(mask & (1 << col)) for mask in row_masks) == cols[col] for col in range(5)):
                    candidates.append(sum(mask << (row * 5) for row, mask in enumerate(row_masks)))
            assert candidates
            probed = set()
            for _ in range(15):
                if not active(frame()):
                    break
                cell = max((i for i in range(25) if i not in probed),
                           key=lambda i: sum(bool(board & (1 << i)) for board in candidates))
                tap(133 + cell % 5 * 44, 127 + cell // 5 * 36)
                probed.add(cell)
                pixels = frame()
                if active(pixels):
                    found = cue(pixels, 119 + cell % 5 * 44, 117 + cell // 5 * 36, 0x1c5145, 1)
                    candidates = [board for board in candidates if bool(board & (1 << cell)) == found]
                    assert candidates
                    tap(133 + cell % 5 * 44, 127 + cell // 5 * 36)
                    assert frame() == pixels, "Repeated probes must not consume another turn"
        elif game == 3:  # Tide 2048: directional moves merge and introduce actual tiles.
            pixels = frame()
            for direction in range(400):
                if not active(frame()):
                    break
                tap(74 + (0, 2, 1, 3)[direction % 4] * 100, 312)
            assert frame() != pixels
        elif game == 4:  # Harbor Stack: time drops against the visible moving bar.
            for layer in range(6):
                for _ in range(200):
                    pixels = frame(0.05)
                    y = 273 - layer * 14
                    if cue(pixels, 108, y, 0xffdc73, 1) and not cue(pixels, 88, y, 0xffdc73, 1):
                        break
                else:
                    raise AssertionError("Moving stack bar must cross the supporting layer")
                tap(224, 312)
                assert active(frame()), "Accurate overlap must preserve the stack"
        else:  # Echo Reef: read flashes, including a dark gap between repeated notes.
            colors = (0xffdc73, 0x73e5ff, 0xffa3cf, 0x89ffb1)
            for length in range(1, 5):
                notes, previous = [], None
                for step in range(int((length * 0.8 + 0.35) / 0.1) + 2):
                    pixels = frame(0 if step == 0 else 0.1)
                    pad = next((i for i, color in enumerate(colors)
                                if cue(pixels, 74 + i % 2 * 160, 120 + i // 2 * 90, color, 1)), None)
                    if pad is not None and pad != previous:
                        notes.append(pad)
                    previous = pad
                assert len(notes) == length, ("Echo must show the complete growing sequence", notes, length)
                for pad in notes:
                    tap(138 + pad % 2 * 160, 149 + pad // 2 * 90)
                assert active(frame()), "Correct echo input must advance rather than end the game"
        if active(frame()):
            tap(350, 70)
        result = frame()
        assert not active(result), "Every game needs an explicit finish/result path"
        assert snapshot() == before, "Playing never changes the aquarium before saving"
        tap(380, 20)
        assert frame() != result, "Unclaimed page-three results need discard protection"
        save_path = os.environ["POCKET_TANK_SAVE"]
        os.environ["POCKET_TANK_SAVE"] = str(output / "missing" / "game-three.sav")
        tap(224, 226)
        assert snapshot() == before, "Page-three save failures must roll back all records and rewards"
        os.environ["POCKET_TANK_SAVE"] = save_path
        tap(224, 226)
        claimed = snapshot()
        dollars = claimed["tank"]["sd_balance"] - before["tank"]["sd_balance"]
        assert dollars == (1 if before["game_income"]["paid"][game + 12] + before["game_income"]["legacy"] < 5 else 0), ("Per-game daily limit", game, dollars)
        assert claimed["tank"]["sd_earned"] - before["tank"]["sd_earned"] == dollars
        assert claimed["tank"]["journal_xp"] == before["tank"]["journal_xp"]
        assert claimed["games_extra"]["rounds"][game] == before["games_extra"]["rounds"][game] + 1
        assert claimed["games_extra"]["best"][game] > 0
        thresholds = ((24, 16, 8), (80, 40, 24), (10, 20, 25),
                      (128, 512, 2048), (30, 80, 120), (3, 7, 12))[game]
        best = claimed["games_extra"]["best"][game]
        medal = sum(best <= target if game < 2 else best >= target for target in thresholds)
        assert claimed["games_extra"]["medals"][game] == medal, "Medals must use the game's actual metric"
        assert claimed["companion"] == old_six
        assert claimed["games"] == before["games"]
        tap(224, 226)
        assert snapshot() == claimed, "Repeated claims cannot pay twice"
        tap(224, 300)
        assert frame() != result
        tap(224, 302)
        tap(350, 70)
        tap(380, 20)
        tap(380, 20)  # Explicitly discard this unsolved retry.
        assert snapshot() == claimed
        tap(380, 20)
        assert lib.sim_page() == 9
        encoded = backup(lib)
        assert lib.sim_restore(encoded, len(encoded)), "Third-page records must round-trip"
        assert snapshot()["games_extra"] == claimed["games_extra"]
    assert len(extra_screens) == 6, "Third-page games require six distinct help screens"
    lib.sim_action(7)
    tap(100, 260)
    tap(300, 330)
    extra_records = frame()
    tap(350, 70)
    assert frame() != extra_records, "Each saved-record page must be distinct"
    tap(350, 70)
    tap(350, 70)  # Cycle all three records pages and retain page three.
    assert frame() == extra_records
    tap(380, 20)
    tap(380, 20)
    saved_games = snapshot()["games"]
    saved_extra = snapshot()["games_extra"]
    saved_path = os.environ["POCKET_TANK_SAVE"]
    if Path(saved_path).exists():
        binary = Path(saved_path).read_bytes()
    else:
        generations = []
        for path in Path(saved_path + ".profiles").glob("*-0.*"):
            raw = path.read_bytes()
            magic, version, sequence, length, crc, header_crc = struct.unpack("<6I", raw[:24])
            assert magic == 0x41515943 and version == 1
            assert zlib.crc32(raw[:20]) == header_crc and zlib.crc32(raw[24:]) == crc
            assert len(raw[24:]) == length
            generations.append((sequence, raw[24:]))
        assert generations, "The active profile must have a verified persistent generation"
        binary = max(generations)[1]
    assert len(binary) > 5920, "Extended game records must use the expanded persistent save"
    icons = (output / "icons.bin").read_bytes()
    assert lib.sim_init(icons, len(icons))
    assert snapshot()["games"] == saved_games, "All six new records must survive a binary reload"
    assert snapshot()["games_extra"] == saved_extra
    assert snapshot()["companion"] == old_six
    legacy_path = str(output / "games-schema15.sav")
    os.environ["POCKET_TANK_SAVE"] = legacy_path
    with open(legacy_path, "wb") as stream:
        stream.write(binary[:5920])
    assert lib.sim_init(icons, len(icons))
    assert snapshot()["games"] == saved_games, "Schema-15 binary records must remain unchanged"
    assert snapshot()["games_extra"]["best"] == [0] * 6
    legacy_path = str(output / "games-schema12.sav")
    os.environ["POCKET_TANK_SAVE"] = legacy_path
    with open(legacy_path, "wb") as stream:
        stream.write(binary[:5848])
    assert lib.sim_init(icons, len(icons))
    assert snapshot()["companion"] == old_six, "A pre-expansion binary save must keep original records"
    assert snapshot()["games"]["best"] == [0] * 6
    os.environ["POCKET_TANK_SAVE"] = saved_path
    assert lib.sim_init(icons, len(icons))
    assert snapshot()["games"] == saved_games
    assert snapshot()["games_extra"] == saved_extra
    for field in ("games", "games_extra"):
        for mutate in (
            lambda g: g.update(version=2),
            lambda g: g.update(version=0),
            lambda g: g.update(medals=[4] * 6),
            lambda g: g.update(rounds=[1] * 5),
        ):
            malformed = json.loads(backup(lib))
            mutate(malformed[field])
            encoded = json.dumps(malformed).encode()
            before = snapshot()
            assert not lib.sim_restore(encoded, len(encoded)), "Malformed records must be rejected atomically"
            assert snapshot() == before
    # A genuine pre-expansion backup migrates without touching its six existing records.
    legacy = json.loads(backup(lib))
    legacy["schema"] = 12
    legacy.pop("games")
    legacy.pop("games_extra")
    legacy.pop("rpg", None)
    legacy.pop("creative", None)
    legacy.pop("profile", None)
    legacy.pop("care", None)
    legacy.pop("inventory", None)
    legacy.pop("mussel", None)
    legacy.pop("game_income", None)
    legacy.pop("content", None)
    encoded = json.dumps(legacy).encode()
    if original.get("profile", {}).get("mode", 0) == 2:
        before = snapshot()
        assert not lib.sim_restore(encoded, len(encoded)), "Legacy Normal backups cannot replace a chosen Hard profile"
        assert snapshot() == before
    else:
        assert lib.sim_restore(encoded, len(encoded))
        migrated = snapshot()
        assert migrated["companion"] == old_six
        assert migrated["games"]["best"] == [0] * 6
        assert migrated["games"]["rounds"] == [0] * 6
        assert migrated["games_extra"]["best"] == [0] * 6
        assert migrated["games_extra"]["rounds"] == [0] * 6
    lib.sim_action(7)
    tap(100, 260)
    tap(350, 80)  # Restore first menu page for later tests.
    tap(380, 20)
    encoded = json.dumps(original).encode()
    assert lib.sim_restore(encoded, len(encoded))
    print("PASS: eighteen rewarded touch games, pause, isolation, transactional records/rewards, retries and migration")
