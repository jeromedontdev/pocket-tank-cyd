/* Including the renderer lets the test check private page helpers directly. */
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <limits.h>
#include <string.h>
#include "../common/render.c"

static uint16_t pixels[448 * 368];
static uint16_t full_frame[320 * 240];
static uint16_t tile_frame[320 * 240 + 2];
static uint16_t repeated[320 * 240];
static uint16_t icon_bank[128 * 1024];
static size_t icon_bank_bytes;
static tank_t help_tank;

static void check_game_help(void) {
    for (int game = 0; game < 18; game++) for (int paused = 0; paused < 2; paused++) {
        memset(&g_game, 0, sizeof g_game);
        g_game.page = g_game.kind = game_kinds[game];
        g_game.help = !paused;
        g_game.paused = paused != 0;
        render_tile(0, 0);
        render_games(&help_tank, full_frame, TANK_W);
        g_game.score = 999;
        g_game.hp = 5;
        g_game.wave = 5;
        g_game.moves = 22;
        g_game.left = 30;
        g_game.travel = 17;
        memset(g_game.board, 2, sizeof g_game.board);
        render_games(&help_tank, repeated, TANK_W);
        assert(!memcmp(full_frame, repeated, (size_t)TANK_W * TANK_H * sizeof *full_frame));
        /* Outside the unchanged panel, only the header/footer may be visible. */
        for (int row = 46 * TANK_H / PAGE_H; row < 339 * TANK_H / PAGE_H; row++) {
            for (int col = 0; col < TANK_W; col++) {
                int px = col * PAGE_W / TANK_W, py = row * PAGE_H / TANK_H;
                if (px < 22 || px > 425 || py < 92 || py > 330) {
                    if (full_frame[row * TANK_W + col] != rgb565(MSP_INK, 1))
                        fprintf(stderr, "Help leak game %d paused %d at %d,%d (page %d,%d)\n",
                                game, paused, col, row, px, py);
                    assert(full_frame[row * TANK_W + col] == rgb565(MSP_INK, 1));
                }
            }
        }
        for (int row = 0; row < TANK_H; row += 17) {
            int height = TANK_H - row < 17 ? TANK_H - row : 17;
            render_tile(row, height);
            render_games(&help_tank, tile_frame, TANK_W);
            assert(!memcmp(tile_frame, full_frame + row * TANK_W,
                           (size_t)TANK_W * height * sizeof *full_frame));
        }
        render_tile(0, 0);
    }
}

static void check_puzzle_games(void) {
    for (int relaxed = 0; relaxed < 2; relaxed++) {
        g_game_relaxed = relaxed != 0;
        for (int trial = 0; trial < 24; trial++) {
            game_start(GAME_LIGHTHOUSE);
            uint32_t rows[16];
            for (int i = 0; i < 16; i++) {
                rows[i] = (uint32_t)g_game.board[i] << 16;
                for (int j = 0; j < 16; j++)
                    if (abs(i % 4 - j % 4) + abs(i / 4 - j / 4) <= 1) rows[i] |= 1u << j;
            }
            int pivots[16], rank = 0;
            for (int col = 0; col < 16; col++) {
                int pivot = rank;
                while (pivot < 16 && !(rows[pivot] & (1u << col))) pivot++;
                if (pivot == 16) continue;
                uint32_t swap = rows[rank]; rows[rank] = rows[pivot]; rows[pivot] = swap;
                for (int i = 0; i < 16; i++) if (i != rank && rows[i] & (1u << col)) rows[i] ^= rows[rank];
                pivots[rank++] = col;
            }
            for (int i = rank; i < 16; i++) assert(!(rows[i] & (1u << 16)));
            for (int i = 0; i < rank; i++) if (rows[i] & (1u << 16)) {
                int cell = pivots[i];
                game_practice_tap(137 + cell % 4 * 56, 121 + cell / 4 * 48);
            }
            assert(g_game.page == GAME_RESULT && g_game.score == 16 && g_game.reward >= 6 && g_game.reward <= 10);
            game_start(GAME_SLIDE);
            assert(!game_slide_solved());
            int inversions = 0;
            for (int i = 0; i < 9; i++) for (int j = i + 1; j < 9; j++)
                if (g_game.board[i] && g_game.board[j] && g_game.board[i] > g_game.board[j]) inversions++;
            assert(!(inversions & 1)); /* Legal shuffles preserve solvability. */
        }
        game_start(GAME_SLIDE);
        for (int i = 0; i < 9; i++) g_game.board[i] = (uint8_t)((i + 1) % 9);
        g_game.board[7] = 0; g_game.board[8] = 8;
        game_practice_tap(157, 129); /* Nonadjacent tiles cannot teleport. */
        assert(!g_game.moves);
        game_practice_tap(285, 257);
        assert(g_game.page == GAME_RESULT && g_game.moves == 1 && g_game.reward == 10);

        game_start(GAME_SONAR);
        assert(g_game.remaining == (relaxed ? 20 : 15));
        int probes = g_game.remaining;
        for (int i = 0; i < 25; i++) if (g_game.board[i] == 1) {
            game_practice_tap(133 + i % 5 * 44, 127 + i / 5 * 36);
            assert(g_game.remaining == --probes);
            game_practice_tap(133 + i % 5 * 44, 127 + i / 5 * 36);
            assert(g_game.remaining == probes);
        }
        assert(g_game.page == GAME_RESULT && g_game.score == 25 && g_game.reward == 8);
        game_start(GAME_SONAR);
        for (int i = 0; i < 25 && g_game.page != GAME_RESULT; i++) if (!g_game.board[i])
            game_practice_tap(133 + i % 5 * 44, 127 + i / 5 * 36);
        assert(g_game.page == GAME_RESULT && g_game.remaining == 0 && !g_game.score);

        game_start(GAME_MERGE);
        memset(g_game.board, 0, sizeof g_game.board);
        for (int i = 0; i < 4; i++) g_game.board[i] = 1;
        game_merge_move(0);
        assert(g_game.board[0] == 2 && g_game.board[1] == 2 && g_game.score == 8);
        game_start(GAME_MERGE);
        memset(g_game.board, 0, sizeof g_game.board);
        g_game.board[0] = 1; g_game.board[1] = 2;
        game_merge_move(0);
        assert(g_game.board[0] == 1 && g_game.board[1] == 2 && !g_game.board[2] && !g_game.moves);
        for (int i = 0; i < 16; i++) g_game.board[i] = (uint8_t)(1 + (i % 4 + i / 4) % 2);
        game_merge_move(0);
        assert(g_game.page == GAME_RESULT && !g_game.reward);
        game_start(GAME_MERGE);
        memset(g_game.board, 0, sizeof g_game.board);
        g_game.board[0] = g_game.board[1] = 10;
        game_merge_move(0);
        assert(g_game.page == GAME_RESULT && g_game.board[0] == 11 && g_game.score == 2048);
        for (int direction = 0; direction < 4; direction++) {
            game_start(GAME_MERGE);
            memset(g_game.board, 0, sizeof g_game.board);
            g_game.board[game_merge_cell(direction, 1, 2)] = 1;
            g_game.board[game_merge_cell(direction, 1, 3)] = 1;
            game_merge_move(direction);
            assert(g_game.board[game_merge_cell(direction, 1, 0)] == 2 && g_game.score == 4);
        }

        game_start(GAME_STACK);
        game_practice_tap(224, 312);
        assert(g_game.remaining == 8 && g_game.rounds == 1);
        while (g_game.page != GAME_RESULT) {
            g_game.px = g_game.board[g_game.rounds * 2];
            game_practice_tap(224, 312);
        }
        assert(g_game.rounds == 12 && g_game.score == 128 && g_game.reward == 9);
        game_start(GAME_STACK);
        g_game.remaining = 1; g_game.board[1] = 5;
        game_practice_tap(224, 312);
        assert(g_game.page == GAME_RESULT && !g_game.score);

        game_start(GAME_ECHO);
        g_game.help = false;
        for (int length = 1; length <= 12; length++) {
            game_practice_tap(138, 149);
            assert(g_game.launched); /* Taps during playback are ignored. */
            for (int n = 0; n < 60 && g_game.launched; n++) render_games_tick(0.25f);
            assert(!g_game.launched);
            for (int i = 0; i < length; i++) {
                int pad = g_game.cards[i];
                game_practice_tap(138 + pad % 2 * 160, 149 + pad / 2 * 90);
            }
            assert(g_game.score == length);
        }
        assert(g_game.page == GAME_RESULT && g_game.reward == 10);
        game_start(GAME_ECHO);
        g_game.launched = false;
        int wrong = (g_game.cards[0] + 1) % 4;
        game_practice_tap(138 + wrong % 2 * 160, 149 + wrong / 2 * 90);
        assert(g_game.page == GAME_RESULT && !g_game.score);
    }
    g_game_relaxed = false;
}

static void check_puzzle_rewards(void) {
    for (int game = 12; game < 18; game++) {
        assert(!companion_game_reward(game, 0, 0));
        assert(!companion_game_reward(game, -1, 10));
        assert(!companion_game_reward(game, 10, -1));
        game_start(game_kinds[game]);
        game_finish();
        assert(!g_game.reward);
    }
    assert(!companion_game_reward(12, 15, 1));
    assert(!companion_game_reward(12, 16, 0));
    assert(companion_game_reward(12, 16, 30) == 6);
    assert(companion_game_reward(12, 16, 16) == 8);
    assert(companion_game_reward(12, 16, 8) == 10);
    assert(!companion_game_reward(13, 7, 1));
    assert(companion_game_reward(13, 8, 100) == 6);
    assert(companion_game_reward(13, 8, 40) == 8);
    assert(companion_game_reward(13, 8, 20) == 10);
    assert(!companion_game_reward(14, 5, 1));
    assert(companion_game_reward(14, 10, 2) == 2);
    assert(!companion_game_reward(15, 127, 10));
    assert(companion_game_reward(15, 128, 10) == 1);
    assert(!companion_game_reward(16, 29, 10));
    assert(companion_game_reward(16, 30, 10) == 2);
    assert(!companion_game_reward(17, 2, 10));
    assert(companion_game_reward(17, 3, 10) == 1);
    for (int game = 14; game < 18; game++)
        assert(companion_game_reward(game, INT_MAX, INT_MAX) == 10);
}

static void check_creative_games(void) {
    profile_state_t profile = help_tank.profile;
    help_tank.profile = (profile_state_t){.marker = 0x50524631u, .version = 1,
        .mode = GAME_MODE_CREATIVE, .chosen = 1, .reserved = 2};
    help_tank.creative = (creative_state_t){0x43525631u, 1, 1, 0};
    assert(progression_creative(&help_tank));
    tank_t before = help_tank;
    for (int game = 0; game < 18; game++) {
        int score = game == 12 ? 16 : game == 13 ? 8 : 40;
        int reward = game >= 12 ? companion_game_reward(game, score, 12) : 10;
        assert(!companion_game_claim(&help_tank, game, score, 12, reward, false));
        assert(!memcmp(&help_tank, &before, sizeof before));
        game_start(game_kinds[game]);
        g_game.help = false;
        render_games_tick(0.25f);
        render_games_tap(&help_tank, 224.0f * TANK_W / PAGE_W, 180.0f * TANK_H / PAGE_H);
        render_games(&help_tank, full_frame, TANK_W);
        assert(!memcmp(&help_tank, &before, sizeof before));
        g_game.score = 40; g_game.moves = 12;
        game_finish();
        render_games(&help_tank, full_frame, TANK_W);
        render_games_tap(&help_tank, 224.0f * TANK_W / PAGE_W, 226.0f * TANK_H / PAGE_H);
        assert(!memcmp(&help_tank, &before, sizeof before));
        assert(!g_game.claimed && !g_game.save_failed && !g_game.discard);
        render_games_tap(&help_tank, 380.0f * TANK_W / PAGE_W, 20.0f * TANK_H / PAGE_H);
        assert(g_game.page == GAME_MENU);
    }
    memset(&help_tank.creative, 0, sizeof help_tank.creative);
    help_tank.profile = profile;
}

static void check_practice_render(void) {
    for (int game = 12; game < 18; game++) {
        game_start(game_kinds[game]);
        g_game.help = false;
        render_games_tick(0.25f);
        unsigned char before[sizeof g_game];
        memcpy(before, &g_game, sizeof before);
        render_tile(0, 0);
        render_games(&help_tank, full_frame, TANK_W);
        render_games(&help_tank, repeated, TANK_W);
        assert(!memcmp(full_frame, repeated, (size_t)TANK_W * TANK_H * sizeof *full_frame));
        for (int row = 0; row < TANK_H; row += 17) {
            int height = TANK_H - row < 17 ? TANK_H - row : 17;
            render_tile(row, height);
            render_games(&help_tank, tile_frame, TANK_W);
            assert(!memcmp(tile_frame, full_frame + row * TANK_W, (size_t)TANK_W * height * sizeof *full_frame));
        }
        assert(!memcmp(before, &g_game, sizeof before));
        render_tile(0, 0);
    }
}

const icon_t *icons_resolve(const icon_t *icon) {
    static icon_t view;
    size_t count = (size_t)icon->w * icon->h;
    assert(icon->offset <= icon_bank_bytes);
    assert(count * 3 <= icon_bank_bytes - icon->offset);
    view = *icon;
    view.rgb = (const uint16_t *)((const uint8_t *)icon_bank + icon->offset);
    view.a = (const uint8_t *)view.rgb + count * 2;
    return &view;
}

static void draw_wrapper(uint16_t *fb, int x, int y, int kind, int item, bool lit, int size) {
    if (kind) render_decor_thumbnail(fb, TANK_W, x, y, size, item, lit);
    else render_visitor_portrait(fb, TANK_W, x, y, item, lit);
}

static void check_wrapper(int x, int y, int kind, int item, bool lit, int size) {
    const uint16_t background = 0x1843, guard = 0xc59a;
    int count = TANK_W * TANK_H;
    for (int p = 0; p < count; p++) full_frame[p] = repeated[p] = background;
    render_tile(0, 0);
    draw_wrapper(full_frame, x, y, kind, item, lit, size);
    draw_wrapper(repeated, x, y, kind, item, lit, size);
    assert(!memcmp(full_frame, repeated, (size_t)count * sizeof *full_frame));
    int width = kind ? size : 40, height = kind ? size : 24;
    int left = x * TANK_W / PAGE_W, top = y * TANK_H / PAGE_H;
    int right = (x + width - 1) * TANK_W / PAGE_W;
    int bottom = (y + height - 1) * TANK_H / PAGE_H;
    for (int row = 0; row < TANK_H; row++) for (int col = 0; col < TANK_W; col++)
        if (col < left || col > right || row < top || row > bottom)
            assert(full_frame[row * TANK_W + col] == background);
    for (int tile_height = 1; tile_height <= 17; tile_height += 16) {
        for (int row = 0; row < TANK_H; row += tile_height) {
            int height_now = TANK_H - row < tile_height ? TANK_H - row : tile_height;
            int length = TANK_W * height_now;
            for (int p = 0; p < length + 2; p++) tile_frame[p] = guard;
            for (int p = 1; p <= length; p++) tile_frame[p] = background;
            render_tile(row, height_now);
            draw_wrapper(tile_frame + 1, x, y, kind, item, lit, size);
            assert(tile_frame[0] == guard && tile_frame[length + 1] == guard);
            assert(!memcmp(tile_frame + 1, full_frame + row * TANK_W,
                           (size_t)length * sizeof *full_frame));
        }
    }
    render_tile(0, 0);
}

static void check_box(int x, int y, int width, int height) {
    unsigned drawn = 0;
    for (int row = 0; row < 368; row++) for (int col = 0; col < 448; col++) {
        if (!pixels[row * 448 + col]) continue;
        assert(col >= x && col < x + width);
        assert(row >= y && row < y + height);
        drawn++;
    }
    assert(drawn > 0);
}

static void draw_page_asset(uint16_t *fb, int x, int y, int kind, int item) {
    ctx_t c = ctx_page(fb, TANK_W);
    if (kind == 0) page_stamp(&c, x, y, item, MSP_TEAL);
    else if (kind == 1) game_thumb(&c, x, y, item);
    else page_medal(&c, x, y, (unsigned)item);
}

static void check_page_asset(int kind, int item, int x, int y) {
    const uint16_t background = 0x1843, guard = 0xc59a;
    int count = TANK_W * TANK_H;
    int width = kind == 1 ? 40 : kind == 0 ? 24 : 20;
    int height = kind == 1 ? 32 : 24;
    for (int p = 0; p < count; p++) full_frame[p] = repeated[p] = background;
    render_tile(0, 0);
    draw_page_asset(full_frame, x, y, kind, item);
    draw_page_asset(repeated, x, y, kind, item);
    assert(!memcmp(full_frame, repeated, (size_t)count * sizeof *full_frame));
    int left = x * TANK_W / PAGE_W, top = y * TANK_H / PAGE_H;
    int right = (x + width - 1) * TANK_W / PAGE_W;
    int bottom = (y + height - 1) * TANK_H / PAGE_H;
    uint16_t colors[64]; int color_count = 0;
    for (int row = 0; row < TANK_H; row++) for (int col = 0; col < TANK_W; col++) {
        uint16_t pixel = full_frame[row * TANK_W + col];
        if (col < left || col > right || row < top || row > bottom) assert(pixel == background);
        else if (pixel != background) {
            int i = 0;
            while (i < color_count && colors[i] != pixel) i++;
            if (i == color_count) {
                assert(color_count < 64);
                colors[color_count++] = pixel;
            }
        }
    }
    if (x == 100 && y == 100) assert(color_count >= 4);
    for (int tile_height = 1; tile_height <= 17; tile_height += 16) {
        for (int row = 0; row < TANK_H; row += tile_height) {
            int height_now = TANK_H - row < tile_height ? TANK_H - row : tile_height;
            int length = TANK_W * height_now;
            tile_frame[0] = tile_frame[length + 1] = guard;
            for (int p = 1; p <= length; p++) tile_frame[p] = background;
            render_tile(row, height_now);
            draw_page_asset(tile_frame + 1, x, y, kind, item);
            assert(tile_frame[0] == guard && tile_frame[length + 1] == guard);
            assert(!memcmp(tile_frame + 1, full_frame + row * TANK_W,
                           (size_t)length * sizeof *full_frame));
        }
    }
    render_tile(0, 0);
}

int main(int argc, char **argv) {
    assert(argc == 2);
    FILE *icons = fopen(argv[1], "rb");
    assert(icons);
    icon_bank_bytes = fread(icon_bank, 1, sizeof icon_bank, icons);
    assert(!ferror(icons) && feof(icons));
    assert(!fclose(icons));
    ctx_t c = {pixels, 448, 1.0f, 0, 0, 448, 368, false};
    check_puzzle_games();
    check_puzzle_rewards();
    uint32_t previews[18];
    for (int game = 0; game < 18; game++) {
        memset(pixels, 0, sizeof pixels);
        game_thumb(&c, 100, 100, game);
        check_box(100, 100, 40, 32);
        uint32_t hash = 2166136261u;
        for (int row = 100; row < 132; row++) for (int col = 100; col < 140; col++)
            hash = (hash ^ pixels[row * 448 + col]) * 16777619u;
        for (int previous = 0; previous < game; previous++) assert(hash != previews[previous]);
        previews[game] = hash;
        assert(draw_text_wrap(&c, 100, 100, 136, 0xffffff, game_names[game]) <= 2);
        assert(24 + text_w(game_titles[game], 1) < 164);
    }
    uint32_t stamps[STAMP_CLEAN + 1];
    for (int stamp = STAMP_FISH; stamp <= STAMP_CLEAN; stamp++) {
        memset(pixels, 0, sizeof pixels);
        page_stamp(&c, 100, 100, stamp, MSP_TEAL);
        check_box(100, 100, 24, 24);
        uint32_t hash = 2166136261u;
        for (int row = 100; row < 124; row++) for (int col = 100; col < 124; col++)
            hash = (hash ^ pixels[row * 448 + col]) * 16777619u;
        for (int previous = 0; previous < stamp; previous++) assert(hash != stamps[previous]);
        stamps[stamp] = hash;
        memset(pixels, 0, sizeof pixels);
        page_stamp(&c, 100, 100, stamp, MSP_DIM);
        const uint16_t dim = rgb565(MSP_DIM, 1);
        for (int row = 100; row < 124; row++) for (int col = 100; col < 124; col++) {
            uint16_t pixel = pixels[row * 448 + col];
            assert((pixel >> 11) <= (dim >> 11));
            assert(((pixel >> 5) & 63) <= ((dim >> 5) & 63));
            assert((pixel & 31) <= (dim & 31));
        }
    }
    for (int theme = 0; theme < JOURNAL_THEME_COUNT; theme++) {
        for (int unlocked = 0; unlocked < 2; unlocked++) {
            memset(pixels, 0, sizeof pixels);
            page_theme_thumb(&c, 100, 100, theme, unlocked != 0);
            check_box(100, 100, 40, 26);
        }
    }
    for (unsigned medal = 0; medal < 5; medal++) {
        memset(pixels, 0, sizeof pixels);
        page_medal(&c, 100, 100, medal);
        check_box(100, 100, 20, 24);
    }
    for (int goal = 0; goal < JOURNAL_GOAL_N; goal++) {
        const journal_goal_t *g = &JOURNAL_GOALS[goal];
        assert(text_w(g->name, 2) <= 338);
        assert(text_w(g->detail, 1) <= 360);
        char reward[64];
        snprintf(reward, sizeof reward, "%u/%u +%uXP +%uC", g->target, g->target, g->xp, g->coins);
        assert(64 + text_w(reward, 1) < 348); /* CLAIM column stays separate. */
    }
    for (int resolution = 0; resolution < 2; resolution++) {
        tank_resolution(resolution != 0);
        render_life_open();
        for (int category = 0; category < 5; category++) for (int page = 0; page < 2; page++) {
            g_home_category = category; g_home_page = page;
            render_life(&help_tank, full_frame, TANK_W);
            memcpy(repeated, full_frame, (size_t)TANK_W * TANK_H * sizeof *full_frame);
            ctx_t header = ctx_page(repeated, TANK_W);
            button(&header, 326, 8, 112, 36, 0x1c2f36, MSP_TEAL, "BACK", 2);
            assert(!memcmp(full_frame, repeated, (size_t)TANK_W * TANK_H * sizeof *full_frame));
        }
        for (int kind = 0; kind < 3; kind++) {
            int items = kind == 0 ? STAMP_CLEAN + 1 : kind == 1 ? 18 : 4;
            for (int item = 0; item < items; item++) {
                check_page_asset(kind, item, 100, 100);
                check_page_asset(kind, item, -10, -8);
                check_page_asset(kind, item, PAGE_W - 15, PAGE_H - 9);
            }
        }
        check_game_help();
        check_creative_games();
        check_practice_render();
        for (int seen = 0; seen < 2; seen++) {
            for (int species = -1; species <= VISITOR_SPECIES; species++) {
                check_wrapper(100, 100, 0, species, seen != 0, 0);
                check_wrapper(-10, -8, 0, species, seen != 0, 0);
                check_wrapper(PAGE_W - 15, PAGE_H - 9, 0, species, seen != 0, 0);
            }
            for (int item = 0; item < SD_ITEM_COUNT; item++) {
                check_wrapper(100, 100, 1, item, seen != 0, 24);
                check_wrapper(-10, -8, 1, item, seen != 0, 24);
                check_wrapper(PAGE_W - 15, PAGE_H - 9, 1, item, seen != 0, 24);
                check_wrapper(100, 100, 1, item, seen != 0, 1);
                check_wrapper(100, 100, 1, item, seen != 0, 64);
            }
        }
    }
    puts("Page art: distinct games; stamps, themes, medals and goal labels fit.");
    puts("Wrappers: static, clipped and full/tile equivalent at both resolutions.");
    puts("Help/pause: all games mask gameplay and match tiled output at both resolutions.");
    puts("Polished art: distinct stamps, four-plus tones, clipped bounds and guarded 1/17-row tile parity at both resolutions.");
    return 0;
}
