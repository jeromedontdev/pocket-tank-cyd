/* games.c - bounded mini-game state, input, rewards and page rendering. */
#include "render_internal.h"
#include "games_internal.h"
#include "companion.h"
#include "progression.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#ifdef RENDER_GAMES_TESTING
#define GAME_LOCAL
#else
#define GAME_LOCAL static
#endif
GAME_LOCAL bool g_game_relaxed;
static int game_index(int kind) {
    if (kind >= GAME_LIGHTHOUSE && kind <= GAME_ECHO) return 12 + kind - GAME_LIGHTHOUSE;
    if (kind >= GAME_DEFENDER && kind <= GAME_EXPEDITION) return 6 + kind - GAME_DEFENDER;
    return kind == GAME_BUBBLES ? 0 : kind == GAME_MATCH ? 1 :
           kind == GAME_SLALOM ? 2 : kind == GAME_CAST ? 3 : kind == GAME_FRENZY ? 4 : 5;
}
GAME_LOCAL game_state_t g_game;
static int g_game_menu;
GAME_LOCAL const int game_kinds[18] = {GAME_BUBBLES, GAME_MATCH, GAME_SLALOM, GAME_CAST, GAME_FRENZY, GAME_REEF,
    GAME_DEFENDER, GAME_RUNNER, GAME_BREAKER, GAME_TACTICS, GAME_ARCHITECT, GAME_EXPEDITION,
    GAME_LIGHTHOUSE, GAME_SLIDE, GAME_SONAR, GAME_MERGE, GAME_STACK, GAME_ECHO};
GAME_LOCAL const char *const game_names[18] = {"BUBBLE POP", "PEARL MATCH", "FISH SLALOM", "TREASURE CAST", "FEED FRENZY", "REEF PUZZLE",
    "REEF DEFENDER", "CURRENT RUNNER", "PEARL BREAKER", "TIDE TACTICS", "REEF ARCHITECT", "DEEP EXPEDITION",
    "LIGHTHOUSE", "SHELL SLIDE", "SONAR SWEEP", "TIDE 2048", "HARBOR STACK", "ECHO REEF"};
GAME_LOCAL const char *const game_titles[18] = {"BUBBLE POP", "PEARL MATCH", "FISH SLALOM", "TREASURE", "FEED FRENZY", "REEF PUZZLE",
    "DEFENDER", "CURRENT RUN", "BREAKER", "TACTICS", "ARCHITECT", "EXPEDITION",
    "LIGHTHOUSE", "SHELL SLIDE", "SONAR SWEEP", "TIDE 2048", "STACK", "ECHO REEF"};
static uint32_t g_game_rng = 0x7a5b193du;
static uint32_t game_random(void) {
    g_game_rng ^= g_game_rng << 13; g_game_rng ^= g_game_rng >> 17; g_game_rng ^= g_game_rng << 5;
    return g_game_rng;
}
static void game_lights_toggle(int cell) {
    for (int i = 0; i < 16; i++)
        if (abs(i % 4 - cell % 4) + abs(i / 4 - cell / 4) <= 1) g_game.board[i] ^= 1;
}
GAME_LOCAL bool game_slide_solved(void) {
    for (int i = 0; i < 9; i++) if (g_game.board[i] != (i + 1) % 9) return false;
    return true;
}
static void game_merge_spawn(void) {
    int empty[16], count = 0;
    for (int i = 0; i < 16; i++) if (!g_game.board[i]) empty[count++] = i;
    if (count) g_game.board[empty[game_random() % (unsigned)count]] = game_random() % 10 ? 1 : 2;
}
static void game_practice_start(int kind) {
    if (kind == GAME_LIGHTHOUSE) {
        /* Scrambling by legal toggles guarantees a solution; remember no save state. */
        unsigned mask = 1u | (game_random() & 65535u);
        for (int i = 0; i < 16; i++) if (mask & (1u << i)) game_lights_toggle(i);
        bool lit = false;
        for (int i = 0; i < 16; i++) if (g_game.board[i]) lit = true;
        if (!lit) game_lights_toggle(0);
    } else if (kind == GAME_SLIDE) {
        for (int i = 0; i < 9; i++) g_game.board[i] = (uint8_t)((i + 1) % 9);
        int blank = 8, previous = -1;
        for (int step = 0; step < (g_game_relaxed ? 16 : 40); step++) {
            int choices[4], count = 0;
            for (int i = 0; i < 9; i++) if (i != previous &&
                abs(i % 3 - blank % 3) + abs(i / 3 - blank / 3) == 1) choices[count++] = i;
            int next = choices[game_random() % (unsigned)count];
            g_game.board[blank] = g_game.board[next]; g_game.board[next] = 0;
            previous = blank; blank = next;
        }
        if (game_slide_solved()) { g_game.board[8] = g_game.board[7]; g_game.board[7] = 0; }
    } else if (kind == GAME_SONAR) {
        for (int n = 0; n < 5;) {
            int cell = (int)(game_random() % 25);
            if (!g_game.board[cell]) { g_game.board[cell] = 1; n++; }
        }
        g_game.remaining = g_game_relaxed ? 20 : 15;
    } else if (kind == GAME_MERGE) {
        game_merge_spawn(); game_merge_spawn();
    } else if (kind == GAME_STACK) {
        g_game.board[0] = 4; g_game.board[1] = 16;
        g_game.remaining = 12; g_game.px = 0; g_game.vx = 1;
    } else if (kind == GAME_ECHO) {
        for (int i = 0; i < 12; i++) g_game.cards[i] = (uint8_t)(game_random() % 4);
        g_game.rounds = 1; g_game.launched = true; g_game.travel = 0;
    }
}
void render_games_open(void) {
    memset(&g_game, 0, sizeof g_game); g_game.first = g_game.second = -1;
}
GAME_LOCAL void game_start(int kind) {
    int fish = g_game.fish;
    render_games_open(); g_game.fish = fish; g_game.page = g_game.kind = kind; g_game.left = 30;
    g_game.target = (int)(game_random() % 3); g_game.lane = 1;
    for (int i = 0; i < 12; i++) g_game.cards[i] = (uint8_t)(i / 2);
    for (int i = 11; i > 0; i--) {
        int j = (int)(game_random() % (unsigned)(i + 1));
        uint8_t tmp = g_game.cards[i]; g_game.cards[i] = g_game.cards[j]; g_game.cards[j] = tmp;
    }
    static const uint8_t routes[2][9] = {
        {6, 10, 12, 9, 3, 3, 3, 3, 3},
        {3, 3, 3, 12, 3, 6, 3, 10, 9}
    };
    unsigned route = game_random() % 2;
    for (int i = 0; i < 9; i++) {
        unsigned mask = routes[route][i];
        for (unsigned r = game_random() % 4; r; r--) mask = ((mask << 1) | (mask >> 3)) & 15;
        g_game.pipes[i] = (uint8_t)mask;
    }
    g_game.pipes[3] = 3; /* Closed inlet: a generated puzzle cannot start solved. */
    g_game.hp = 5; g_game.px = 224; g_game.bx = 224; g_game.by = 270;
    g_game.vx = 90; g_game.vy = -130; g_game.selected = 22;
    g_game.oxygen = 20; g_game.keys = 1; g_game.supplies = 2;
    g_game.help = kind >= GAME_DEFENDER;
    if (kind == GAME_BREAKER) {
        g_game.hp = 3;
        for (int i = 0; i < 24; i++) g_game.board[i] = i < 6 ? 2 : 1;
    } else if (kind == GAME_TACTICS) {
        g_game.board[2] = g_game.board[10] = g_game.board[14] = 2;
        g_game.board[6] = g_game.board[18] = 3;
        g_game.board[0] = g_game.board[4] = g_game.board[12] = 4;
    } else if (kind == GAME_ARCHITECT) {
        g_game.board[6] = g_game.board[18] = 3;
        g_game.selected = 1;
    }
    for (int i = 0; i < 3; i++) g_game.choices[i] = (uint8_t)((game_random() + i) % 5);
    if (kind >= GAME_LIGHTHOUSE) game_practice_start(kind);
}
GAME_LOCAL void game_finish(void) {
    g_game.page = GAME_RESULT;
    if (g_game.kind >= GAME_LIGHTHOUSE) {
        g_game.reward = companion_game_reward(game_index(g_game.kind), g_game.score, g_game.moves);
        return;
    }
    g_game.reward = g_game.kind == GAME_MATCH ? (g_game.moves <= 18 ? 8 : 5) :
                   g_game.kind == GAME_REEF ? 6 : g_game.kind == GAME_CAST ? g_game.score :
                   g_game.score / (g_game.kind == GAME_BUBBLES ? 3 : 2);
    if (g_game.reward > 10) g_game.reward = 10;
    if (g_game.kind >= GAME_DEFENDER) {
        g_game.reward = g_game.score / 4;
        if (g_game.reward > 10) g_game.reward = 10;
    }
}
static int game_distance(int a, int b) {
    return abs(a % 5 - b % 5) + abs(a / 5 - b / 5);
}
static void game_architect_score(void) {
    int score = 0;
    for (int i = 0; i < 25; i++) if (g_game.board[i] == 1 || g_game.board[i] == 2) {
        bool water = false, partner = false;
        for (int j = 0; j < 25; j++) if (game_distance(i, j) == 1) {
            if (!g_game.board[j]) water = true;
            if (g_game.board[j] == 3 - g_game.board[i]) partner = true;
        }
        score += 1 + water + partner;
    }
    g_game.score = score;
}
static void game_extra_tick(float dt) {
    if (g_game.cooldown > 0) g_game.cooldown -= dt;
    if (g_game.invincible > 0) g_game.invincible -= dt;
    float speed = g_game_relaxed ? 0.65f : 1;
    if (g_game.page == GAME_DEFENDER) {
        bool live = false;
        for (int i = 0; i < 8; i++) if (g_game.foes[i].hp) live = true;
        if (!live) {
            if (g_game.wave == 5) { game_finish(); return; }
            g_game.wave++;
            for (int i = 0; i < g_game.wave + 2; i++) {
                g_game.foes[i].x = 48 + i * 350.0f / (g_game.wave + 1);
                g_game.foes[i].y = 112 + i % 2 * 20; g_game.foes[i].hp = g_game.wave >= 3 ? 2 : 1;
            }
        }
        for (int i = 0; i < 8; i++) {
            if (g_game.foes[i].hp) {
                g_game.foes[i].y += dt * (12 + g_game.wave * 4) * speed;
                if (g_game.foes[i].y >= 282) { g_game.foes[i].hp = 0; if (--g_game.hp <= 0) { game_finish(); return; } }
            }
            if (g_game.shots[i].hp) {
                g_game.shots[i].x += g_game.shots[i].vx * dt;
                g_game.shots[i].y += g_game.shots[i].vy * dt;
                if (g_game.shots[i].y < 92 || g_game.shots[i].x < 24 || g_game.shots[i].x > 424) g_game.shots[i].hp = 0;
                for (int j = 0; j < 8 && g_game.shots[i].hp; j++) if (g_game.foes[j].hp &&
                    fabsf(g_game.foes[j].x - g_game.shots[i].x) < 19 &&
                    fabsf(g_game.foes[j].y - g_game.shots[i].y) < 22) {
                    g_game.shots[i].hp = 0;
                    if (--g_game.foes[j].hp == 0) g_game.score += 2;
                }
            }
        }
    } else if (g_game.page == GAME_RUNNER) {
        g_game.travel += dt * (72 + g_game.rounds * 2) * speed;
        if (g_game.travel >= 180) {
            if (g_game.lane != g_game.target && g_game.lane != (g_game.target + 1) % 5) g_game.hp--;
            else g_game.score += g_game.lane == g_game.target ? 3 : 1;
            g_game.rounds++; g_game.travel = 0;
            int shift = (int)(game_random() % 3) - 1;
            g_game.target = (g_game.target + shift + 5) % 5;
            if (g_game.hp <= 0 || g_game.rounds == 18) game_finish();
        }
    } else if (g_game.page == GAME_BREAKER && g_game.launched) {
        for (int step = 0; step < 8; step++) {
            float ox = g_game.bx, oy = g_game.by;
            g_game.bx += g_game.vx * dt * speed / 8; g_game.by += g_game.vy * dt * speed / 8;
            if (g_game.bx < 30) { g_game.bx = 30; g_game.vx = fabsf(g_game.vx); }
            if (g_game.bx > 418) { g_game.bx = 418; g_game.vx = -fabsf(g_game.vx); }
            if (g_game.by < 102) { g_game.by = 102; g_game.vy = fabsf(g_game.vy); }
            if (g_game.vy > 0 && oy <= 278 && g_game.by >= 278 && fabsf(g_game.bx - g_game.px) < 49) {
                g_game.by = 277; g_game.vy = -fabsf(g_game.vy);
                g_game.vx = (g_game.bx - g_game.px) * 3;
            }
            for (int i = 0; i < 24; i++) if (g_game.board[i]) {
                int x = 30 + i % 6 * 65, y = 108 + i / 6 * 24;
                if (g_game.bx > x - 5 && g_game.bx < x + 63 && g_game.by > y - 5 && g_game.by < y + 23) {
                    g_game.board[i]--; g_game.score++;
                    if (ox <= x - 5 || ox >= x + 63) g_game.vx = -g_game.vx;
                    else g_game.vy = -g_game.vy;
                    g_game.bx = ox; g_game.by = oy; break;
                }
            }
            bool live = false;
            for (int i = 0; i < 24; i++) if (g_game.board[i]) live = true;
            if (!live) { game_finish(); return; }
            if (g_game.by > 316) {
                g_game.launched = false; g_game.by = 266; g_game.bx = g_game.px; g_game.vy = -130;
                if (--g_game.hp <= 0) game_finish();
                return;
            }
        }
    }
}
static void game_practice_tick(float dt) {
    if (g_game.page == GAME_STACK) {
        float limit = 20.0f - g_game.remaining;
        g_game.px += dt * g_game.vx * (g_game_relaxed ? 5 : 9 + g_game.rounds);
        if (g_game.px < 0) { g_game.px = -g_game.px; g_game.vx = 1; }
        if (g_game.px > limit) { g_game.px = 2 * limit - g_game.px; g_game.vx = -1; }
    } else if (g_game.page == GAME_ECHO && g_game.launched) {
        g_game.travel += dt;
        float beat = g_game_relaxed ? 1.15f : 0.8f;
        if (g_game.travel >= g_game.rounds * beat + 0.35f) {
            g_game.launched = false; g_game.moves = 0;
        }
    }
}
void render_games_tick(float dt) {
    if (!isfinite(dt) || dt <= 0) return;
    if (dt > 0.25f) dt = 0.25f;
    if (g_game.paused || g_game.help) return;
    if (g_game.page >= GAME_LIGHTHOUSE) { game_practice_tick(dt); return; }
    if (g_game.page >= GAME_DEFENDER) { game_extra_tick(dt); return; }
    if (g_game.page == GAME_BUBBLES) {
        if (!g_game_relaxed) g_game.left -= dt;
        if (g_game.left <= 0) { g_game.left = 0; game_finish(); return; }
        g_game.spawn -= dt;
        for (int i = 0; i < 6; i++) {
            if (g_game.bubbles[i].live) {
                g_game.bubbles[i].y -= dt * (g_game.bubbles[i].gold ? 58 : 42);
                if (g_game.bubbles[i].y < 100) g_game.bubbles[i].live = false;
            }
            if (!g_game.bubbles[i].live && g_game.spawn <= 0) {
                g_game.bubbles[i].x = 52 + game_random() % 344;
                g_game.bubbles[i].y = 304; g_game.bubbles[i].live = true;
                g_game.bubbles[i].gold = ++g_game.spawned % 7 == 0;
                g_game.spawn = 0.65f;
            }
        }
    } else if (g_game.page == GAME_SLALOM) {
        g_game.travel += dt * (g_game_relaxed ? 48 : 95);
        if (g_game.travel >= 210) {
            if (g_game.lane == g_game.target) g_game.score += 2; else g_game.errors++;
            g_game.travel = 0; g_game.rounds++; g_game.target = (int)(game_random() % 3);
            if (g_game.rounds == 12) game_finish();
        }
    } else if (g_game.page == GAME_CAST) {
        g_game.travel += dt * (g_game_relaxed ? 0.45f : 0.9f);
        if (g_game.travel >= 2) g_game.travel -= 2;
    } else if (g_game.page == GAME_FRENZY) {
        if (!g_game_relaxed) g_game.left -= dt;
        if (g_game.reveal > 0) g_game.reveal -= dt;
        if (g_game.left <= 0) { g_game.left = 0; game_finish(); }
    } else if (g_game.page == GAME_MATCH && g_game.reveal > 0) {
        g_game.reveal -= dt;
        if (g_game.reveal <= 0) g_game.first = g_game.second = -1;
    }
}
static bool game_reef_connected(void) {
    bool wet[9] = {false};
    if (!(g_game.pipes[3] & 8)) return false;
    wet[3] = true;
    static const int dx[] = {0, 1, 0, -1}, dy[] = {-1, 0, 1, 0};
    for (int pass = 0; pass < 9; pass++) for (int i = 0; i < 9; i++) if (wet[i]) {
        for (int d = 0; d < 4; d++) {
            int x = i % 3 + dx[d], y = i / 3 + dy[d];
            if (x >= 0 && x < 3 && y >= 0 && y < 3 && (g_game.pipes[i] & (1 << d)) &&
                (g_game.pipes[y * 3 + x] & (1 << ((d + 2) % 4)))) wet[y * 3 + x] = true;
        }
    }
    return wet[5] && (g_game.pipes[5] & 2);
}
static void game_extra_tap(float x, float y) {
    if (g_game.page == GAME_DEFENDER && y >= 96 && y < 294 && x >= 24 && x < 424 && g_game.cooldown <= 0) {
        for (int i = 0; i < 8; i++) if (!g_game.shots[i].hp) {
            float dx = x - 224, dy = y - 304, length = sqrtf(dx * dx + dy * dy);
            g_game.shots[i].x = 224; g_game.shots[i].y = 300;
            g_game.shots[i].vx = dx / length * 330; g_game.shots[i].vy = dy / length * 330;
            g_game.shots[i].hp = 1; g_game.cooldown = 0.22f; break;
        }
    } else if (g_game.page == GAME_RUNNER && x >= 24 && x < 424 && y >= 100 && y < 326) {
        g_game.lane = (int)(x - 24) / 80;
    } else if (g_game.page == GAME_BREAKER && x >= 280 && x < 424 && y >= 54 && y < 86) {
        game_finish();
    } else if (g_game.page == GAME_BREAKER && x >= 24 && x < 424 && y >= 200 && y < 328) {
        g_game.px = fmaxf(70, fminf(378, x));
        if (!g_game.launched) { g_game.bx = g_game.px; g_game.launched = true; }
    } else if (g_game.page == GAME_TACTICS) {
        int cell = -1;
        if (x >= 99 && x < 349 && y >= 92 && y < 292) cell = (int)(y - 92) / 40 * 5 + (int)(x - 99) / 50;
        bool wait = x >= 24 && x < 424 && y >= 298 && y < 328;
        if (!wait && (cell < 0 || game_distance(cell, g_game.selected) != 1 || g_game.board[cell] == 3)) return;
        g_game.moves++;
        if (wait) { if (g_game.hp < 5) g_game.hp++; }
        else if (g_game.board[cell] == 2) { g_game.board[cell] = 0; g_game.score += 4; }
        else {
            g_game.selected = cell;
            if (g_game.board[cell] == 4) { g_game.board[cell] = 0; g_game.pairs++; g_game.score += 6; }
        }
        bool moved[25] = {false};
        for (int i = 0; i < 25; i++) if (g_game.board[i] == 2 && !moved[i]) {
            if (game_distance(i, g_game.selected) == 1) { if (!wait) g_game.hp--; }
            else {
                int best = i;
                for (int j = 0; j < 25; j++) if (!g_game.board[j] && j != g_game.selected &&
                    game_distance(i, j) == 1 && game_distance(j, g_game.selected) < game_distance(best, g_game.selected)) best = j;
                if (best != i) { g_game.board[i] = 0; g_game.board[best] = 2; moved[best] = true; }
            }
        }
        if (g_game.pairs == 3) { g_game.score += g_game.hp * 2; game_finish(); }
        else if (g_game.hp <= 0 || g_game.moves >= 22) game_finish();
    } else if (g_game.page == GAME_ARCHITECT) {
        if (y >= 298 && y < 328) {
            if (x >= 24 && x < 150) g_game.selected = 1;
            else if (x >= 160 && x < 286) g_game.selected = 2;
            else if (x >= 296 && x < 424 && g_game.moves == 12) { game_architect_score(); game_finish(); }
        } else if (x >= 99 && x < 349 && y >= 92 && y < 292) {
            int i = (int)(y - 92) / 40 * 5 + (int)(x - 99) / 50;
            if (g_game.board[i] == 3) return;
            if (g_game.board[i]) { g_game.board[i] = 0; g_game.moves--; }
            else if (g_game.moves < 12) {
                int count = 0;
                for (int j = 0; j < 25; j++) if (g_game.board[j] == g_game.selected) count++;
                if (count >= 6) return;
                g_game.board[i] = (uint8_t)g_game.selected; g_game.moves++;
            }
            game_architect_score();
        }
    } else if (g_game.page == GAME_EXPEDITION) {
        if (x >= 24 && x < 424 && y >= 298 && y < 328) {
            if (g_game.depth) { g_game.score += g_game.hp; game_finish(); }
        } else if (x >= 24 && x < 424 && y >= 128 && y < 282) {
            int i = (int)(x - 24) / 136;
            if (i > 2 || x >= 24 + i * 136 + 128) return;
            int route = g_game.choices[i];
            if (route == 3 && !g_game.keys) return;
            if (route == 4 && !g_game.supplies) return;
            g_game.depth++; g_game.oxygen -= route == 2 ? 4 : 2;
            if (route == 0) { g_game.supplies++; g_game.score += 2; }
            if (route == 1) { g_game.keys++; g_game.score += 2; }
            if (route == 2) { g_game.hp -= 2; g_game.score += 9; }
            if (route == 3) { g_game.keys--; g_game.score += 8; }
            if (route == 4) { g_game.supplies--; g_game.oxygen += 5; g_game.hp = g_game.hp < 5 ? g_game.hp + 1 : 5; }
            if (g_game.hp <= 0 || g_game.oxygen <= 0) { g_game.score /= 2; game_finish(); }
            else if (g_game.depth == 8) { g_game.score += 8 + g_game.hp; game_finish(); }
            else {
                for (int j = 0; j < 3; j++) g_game.choices[j] = (uint8_t)((game_random() + j) % 5);
                g_game.choices[0] = (uint8_t)(game_random() % 3); /* Always one unlocked route. */
            }
        }
    }
}
GAME_LOCAL int game_merge_cell(int direction, int line, int offset) {
    return direction == 0 ? line * 4 + offset : direction == 1 ? line * 4 + 3 - offset :
           direction == 2 ? offset * 4 + line : (3 - offset) * 4 + line;
}
GAME_LOCAL void game_merge_move(int direction) {
    uint8_t before[16];
    memcpy(before, g_game.board, sizeof before);
    for (int line = 0; line < 4; line++) {
        uint8_t packed[4] = {0};
        int count = 0;
        for (int n = 0; n < 4; n++) {
            uint8_t value = g_game.board[game_merge_cell(direction, line, n)];
            if (value) packed[count++] = value;
        }
        int out = 0;
        for (int n = 0; n < count; n++) {
            uint8_t value = packed[n];
            if (n + 1 < count && packed[n + 1] == value) {
                value++; n++; g_game.score += 1 << value;
            }
            g_game.board[game_merge_cell(direction, line, out++)] = value;
        }
        while (out < 4) g_game.board[game_merge_cell(direction, line, out++)] = 0;
    }
    if (memcmp(before, g_game.board, sizeof before)) {
        if (g_game.moves < 9999) g_game.moves++;
        game_merge_spawn();
    }
    bool available = false;
    for (int i = 0; i < 16; i++) {
        if (g_game.board[i] >= 11) { game_finish(); return; }
        if (!g_game.board[i] || (i % 4 < 3 && g_game.board[i] == g_game.board[i + 1]) ||
            (i < 12 && g_game.board[i] == g_game.board[i + 4])) available = true;
    }
    if (!available) game_finish();
}
GAME_LOCAL void game_practice_tap(float x, float y) {
    if (x >= 280 && x < 424 && y >= 54 && y < 86) { game_finish(); return; }
    if (g_game.page == GAME_LIGHTHOUSE && x >= 112 && x < 336 && y >= 100 && y < 292) {
        int col = (int)(x - 112) / 56, row = (int)(y - 100) / 48;
        if (x >= 112 + col * 56 + 50 || y >= 100 + row * 48 + 42) return;
        game_lights_toggle(row * 4 + col);
        if (g_game.moves < 9999) g_game.moves++;
        g_game.score = 0;
        for (int i = 0; i < 16; i++) g_game.score += !g_game.board[i];
        if (g_game.score == 16) game_finish();
    } else if (g_game.page == GAME_SLIDE && x >= 128 && x < 320 && y >= 100 && y < 292) {
        int col = (int)(x - 128) / 64, row = (int)(y - 100) / 64, cell = row * 3 + col;
        if (x >= 128 + col * 64 + 58 || y >= 100 + row * 64 + 58 || !g_game.board[cell]) return;
        for (int i = 0; i < 9; i++) if (!g_game.board[i] &&
            abs(i % 3 - col) + abs(i / 3 - row) == 1) {
            g_game.board[i] = g_game.board[cell]; g_game.board[cell] = 0;
            if (g_game.moves < 9999) g_game.moves++;
            if (game_slide_solved()) { g_game.score = 8; game_finish(); }
            break;
        }
    } else if (g_game.page == GAME_SONAR && x >= 114 && x < 334 && y >= 112 && y < 292) {
        int col = (int)(x - 114) / 44, row = (int)(y - 112) / 36, cell = row * 5 + col;
        if (x >= 114 + col * 44 + 38 || y >= 112 + row * 36 + 30 || g_game.board[cell] >= 2) return;
        g_game.board[cell] = g_game.board[cell] ? 2 : 3;
        g_game.remaining--; g_game.moves++;
        if (g_game.board[cell] == 2) { g_game.pairs++; g_game.score += 5; }
        if (g_game.pairs == 5 || !g_game.remaining) game_finish();
    } else if (g_game.page == GAME_MERGE && y >= 298 && y < 330 && x >= 24 && x < 424) {
        int direction = (int)(x - 24) / 100;
        if (x < 24 + direction * 100 + 94) game_merge_move(direction);
    } else if (g_game.page == GAME_STACK && x >= 24 && x < 424 && y >= 298 && y < 330) {
        int left = (int)roundf(g_game.px), right = left + g_game.remaining;
        int previous = g_game.rounds * 2;
        if (left < g_game.board[previous]) left = g_game.board[previous];
        if (right > g_game.board[previous + 1]) right = g_game.board[previous + 1];
        if (right <= left) { game_finish(); return; }
        g_game.rounds++; g_game.remaining = right - left;
        g_game.board[g_game.rounds * 2] = (uint8_t)left;
        if (g_game.rounds < 12) g_game.board[g_game.rounds * 2 + 1] = (uint8_t)right;
        g_game.score = g_game.rounds * 10 + g_game.remaining;
        if (g_game.rounds == 12) { game_finish(); return; }
        g_game.px = g_game.rounds % 2 ? 20 - g_game.remaining : 0;
        g_game.vx = g_game.rounds % 2 ? -1 : 1;
    } else if (g_game.page == GAME_ECHO && !g_game.launched && x >= 64 && x < 384 && y >= 110 && y < 290) {
        int col = (int)(x - 64) / 160, row = (int)(y - 110) / 90;
        if (x >= 64 + col * 160 + 148 || y >= 110 + row * 90 + 78) return;
        if (g_game.cards[g_game.moves] != row * 2 + col) { game_finish(); return; }
        g_game.moves++;
        if (g_game.moves == g_game.rounds) {
            g_game.score = g_game.rounds;
            if (g_game.rounds == 12) { game_finish(); return; }
            g_game.rounds++; g_game.travel = 0; g_game.launched = true;
        }
    }
}
static bool game_result_pending(const tank_t *t) {
    return !progression_creative(t) && !g_game.claimed;
}
bool render_games_tap(tank_t *t, float x, float y) {
    x = render_page_x(x); y = render_page_y(y);
    if (x >= 326 && x < 438 && y >= 8 && y < 44) {
        if (g_game.page == GAME_MENU) return true;
        if (g_game.page == GAME_RESULT && game_result_pending(t) && g_game.discard != 1) {
            g_game.discard = 1; return false;
        }
        render_games_open(); return false;
    }
    if (g_game.page != GAME_MENU && g_game.page != GAME_RECORDS && g_game.page != GAME_RESULT) {
        if (x >= 244 && x < 320 && y >= 8 && y < 44) { g_game.paused = !g_game.paused; return false; }
        if (x >= 164 && x < 236 && y >= 8 && y < 44) { g_game.help = !g_game.help; return false; }
        if (g_game.help && x >= 32 && x < 416 && y >= 280 && y < 328) { g_game.help = false; return false; }
        if (g_game.help || g_game.paused) return false;
        if (g_game.page >= GAME_LIGHTHOUSE) { game_practice_tap(x, y); return false; }
        if (g_game.page >= GAME_DEFENDER) { game_extra_tap(x, y); return false; }
    }
    if (g_game.page == GAME_MENU) {
        if (x >= 280 && x < 424 && y >= 60 && y < 102) { g_game_menu = (g_game_menu + 1) % 3; return false; }
        if (y >= 316 && y < 352) {
            if (x >= 24 && x < 220) g_game_relaxed = !g_game_relaxed;
            else if (x >= 228 && x < 424) g_game.page = GAME_RECORDS;
            return false;
        }
        if (x >= 32 && x < 272 && y >= 60 && y < 102 && t->n_fish)
            g_game.fish = (g_game.fish + 1) % t->n_fish;
        if (x >= 24 && x < 424 && y >= 116 && y < 308) {
            int col = x >= 228, row = (int)(y - 116) / 64;
            if (x >= (col ? 228 : 24) && x < (col ? 424 : 220) && y < 116 + row * 64 + 56)
                game_start(game_kinds[g_game_menu * 6 + row * 2 + col]);
        }
    } else if (g_game.page == GAME_RECORDS && x >= 280 && x < 424 && y >= 54 && y < 86) {
        g_game_menu = (g_game_menu + 1) % 3;
    } else if (g_game_relaxed && (g_game.page == GAME_BUBBLES || g_game.page == GAME_FRENZY) &&
               x >= 280 && x < 424 && y >= 54 && y < 88) {
        game_finish();
    } else if (g_game.page == GAME_BUBBLES) {
        for (int i = 5; i >= 0; i--) {
            float dx = x - g_game.bubbles[i].x, dy = y - g_game.bubbles[i].y;
            if (g_game.bubbles[i].live && dx * dx + dy * dy <= 28 * 28) {
                if (g_game.score < 9996) g_game.score += g_game.bubbles[i].gold ? 3 : 1;
                g_game.bubbles[i].live = false; break;
            }
        }
    } else if (g_game.page == GAME_SLALOM) {
        if (y >= 276 && y < 328) {
            if (x >= 32 && x < 214 && g_game.lane > 0) g_game.lane--;
            else if (x >= 234 && x < 416 && g_game.lane < 2) g_game.lane++;
        }
    } else if (g_game.page == GAME_CAST) {
        if (x >= 32 && x < 416 && y >= 276 && y < 328) {
            float pos = g_game.travel <= 1 ? g_game.travel : 2 - g_game.travel;
            float target = (g_game.target + 0.5f) / 3;
            if (fabsf(pos - target) < 0.12f) g_game.score += 2; else g_game.errors++;
            g_game.rounds++; g_game.travel = 0; g_game.target = (int)(game_random() % 3);
            if (g_game.rounds == 5) game_finish();
        }
    } else if (g_game.page == GAME_FRENZY) {
        if (x >= 24 && x < 424 && y >= 268 && y < 326 && g_game.reveal <= 0) {
            int choice = (int)(x - 24) / 136;
            if (choice < 3 && x < 24 + choice * 136 + 128) {
                if (choice == g_game.target) { if (g_game.score < 9998) g_game.score += 2; }
                else { g_game.errors++; if (g_game.score) g_game.score--; }
                g_game.target = (int)(game_random() % 3);
                g_game.reveal = 0.2f;
                if (t->n_fish) g_game.fish = (g_game.fish + 1) % t->n_fish;
            }
        }
    } else if (g_game.page == GAME_REEF) {
        if (x >= 112 && x < 328 && y >= 98 && y < 314) {
            int col = (int)(x - 112) / 72, row = (int)(y - 98) / 72, i = row * 3 + col;
            if (x < 112 + col * 72 + 66 && y < 98 + row * 72 + 66) {
                unsigned mask = g_game.pipes[i];
                g_game.pipes[i] = (uint8_t)(((mask << 1) | (mask >> 3)) & 15);
                if (g_game.moves < 9999) g_game.moves++;
                if (game_reef_connected()) game_finish();
            }
        }
    } else if (g_game.page == GAME_MATCH && g_game.reveal <= 0) {
        if (x >= 32 && x < 416 && y >= 94 && y < 310) {
            int col = (int)(x - 32) / 96, row = (int)(y - 94) / 72, i = row * 4 + col;
            if (x >= 32 + col * 96 + 88 || y >= 94 + row * 72 + 64 || g_game.matched[i] || i == g_game.first) return false;
            if (g_game.first < 0) g_game.first = i;
            else {
                g_game.second = i; if (g_game.moves < 9999) g_game.moves++;
                if (g_game.cards[i] == g_game.cards[g_game.first]) {
                    g_game.matched[i] = g_game.matched[g_game.first] = 1; g_game.pairs++;
                    g_game.first = g_game.second = -1;
                    if (g_game.pairs == 6) game_finish();
                } else g_game.reveal = 0.85f;
            }
        }
    } else if (g_game.page == GAME_RESULT && x >= 32 && x < 428) {
        if (y >= 196 && y < 240 && game_result_pending(t)) {
            progression_care_refresh(t);
            int paid = progression_game_payout(t, g_game.reward);
            if (companion_game_claim(t, game_index(g_game.kind), g_game.score, g_game.moves, g_game.reward, g_game_relaxed)) {
                g_game.paid = paid;
                g_game.claimed = true; g_game.save_failed = false; g_game.discard = 0;
            }
            else g_game.save_failed = true;
        } else if (y >= 278 && y < 322) {
            if (game_result_pending(t) && g_game.discard != 2) g_game.discard = 2;
            else game_start(g_game.kind);
        }
    }
    return false;
}
static void game_practice_render(render_ctx_t *c) {
    char text[64];
    render_internal_button(c, 280, 54, 144, 32, 0x1c2f36, MSP_TEAL, "FINISH", 1);
    if (g_game.page == GAME_LIGHTHOUSE || g_game.page == GAME_SLIDE || g_game.page == GAME_MERGE) {
        snprintf(text, sizeof text, "MOVES %d", g_game.moves);
        render_internal_text(c, 24, 62, 2, MSP_TEAL, text);
        int side = g_game.page == GAME_SLIDE ? 3 : 4;
        int width = side == 3 ? 64 : 56, height = side == 3 ? 64 : 48;
        int start = side == 3 ? 128 : 112;
        for (int i = 0; i < side * side; i++) {
            int x = start + i % side * width, y = 100 + i / side * height, value = g_game.board[i];
            uint32_t color = g_game.page == GAME_LIGHTHOUSE ? (value ? 0xffdc73 : 0x24404b) :
                value ? (g_game.page == GAME_MERGE && value > 3 ? 0xffa3cf : 0x73e5ff) : 0x102831;
            render_internal_rect_fill(c, x, y, width - 6, height - 6, color);
            if (g_game.page == GAME_LIGHTHOUSE) {
                render_internal_ring(c, x + 25, y + 21, 11, value ? 0xffffff : 0x45616c);
            } else if (value) {
                snprintf(text, sizeof text, "%d", g_game.page == GAME_MERGE ? 1 << value : value);
                render_internal_text(c, x + (width - 6 - render_text_w(text, 2)) / 2, y + (height - 22) / 2, 2, MSP_INK, text);
            }
        }
        if (g_game.page == GAME_MERGE) {
            static const char *const arrows[] = {"< LEFT", "RIGHT >", "UP", "DOWN"};
            for (int i = 0; i < 4; i++) render_internal_button(c, 24 + i * 100, 298, 94, 32, 0x1c2f36, MSP_TEAL, arrows[i], 1);
        } else render_internal_text(c, 48, 306, 1, MSP_TEAL, g_game.page == GAME_LIGHTHOUSE ?
                        "TOGGLE CROSSES. ALL DARK WINS." : "ORDER 1 TO 8; GAP AT THE END.");
    } else if (g_game.page == GAME_SONAR) {
        snprintf(text, sizeof text, "PEARLS %d/5  LEFT %d", g_game.pairs, g_game.remaining);
        render_internal_text(c, 24, 62, 1, MSP_TEAL, text);
        for (int line = 0; line < 5; line++) {
            int row = 0, col = 0;
            for (int i = 0; i < 5; i++) {
                row += g_game.board[line * 5 + i] == 1 || g_game.board[line * 5 + i] == 2;
                col += g_game.board[i * 5 + line] == 1 || g_game.board[i * 5 + line] == 2;
            }
            snprintf(text, sizeof text, "%d", row);
            render_internal_text(c, 88, 120 + line * 36, 2, 0xffdc73, text);
            snprintf(text, sizeof text, "%d", col);
            render_internal_text(c, 128 + line * 44, 90, 2, 0xffdc73, text);
        }
        for (int i = 0; i < 25; i++) {
            int x = 114 + i % 5 * 44, y = 112 + i / 5 * 36, value = g_game.board[i];
            render_internal_rect_fill(c, x, y, 38, 30, value == 2 ? 0x1c5145 : value == 3 ? 0x24404b : 0x173540);
            if (value == 2) render_internal_ring(c, x + 19, y + 15, 9, 0xffdc73);
            if (value == 3) render_internal_text(c, x + 14, y + 7, 2, MSP_TEAL, "X");
        }
        render_internal_text(c, 48, 306, 1, MSP_TEAL, "GOLD: PEARLS IN EACH ROW/COLUMN.");
    } else if (g_game.page == GAME_STACK) {
        snprintf(text, sizeof text, "LAYERS %d/12  WIDTH %d", g_game.rounds, g_game.remaining);
        render_internal_text(c, 24, 88, 2, MSP_TEAL, text);
        for (int i = 0; i <= g_game.rounds; i++) {
            int left = g_game.board[i * 2], right = g_game.board[i * 2 + 1];
            render_internal_rect_fill(c, 24 + left * 20, 282 - i * 14, (right - left) * 20, 11,
                      i % 2 ? 0x73e5ff : 0x89ffb1);
        }
        render_internal_rect_fill(c, 24 + (int)roundf(g_game.px) * 20, 268 - g_game.rounds * 14,
                  g_game.remaining * 20, 11, 0xffdc73);
        render_internal_button(c, 24, 298, 400, 32, 0x1c2f36, MSP_TEAL, "DROP - KEEP THE OVERLAP", 2);
    } else if (g_game.page == GAME_ECHO) {
        snprintf(text, sizeof text, "%s  LENGTH %d/12", g_game.launched ? "WATCH" : "REPEAT", g_game.rounds);
        render_internal_text(c, 24, 88, 2, MSP_TEAL, text);
        float beat = g_game_relaxed ? 1.15f : 0.8f;
        int step = (int)(g_game.travel / beat);
        int lit = g_game.launched && step < g_game.rounds &&
            g_game.travel - step * beat < beat * 0.65f ? g_game.cards[step] : -1;
        static const uint32_t colors[] = {0xffdc73, 0x73e5ff, 0xffa3cf, 0x89ffb1};
        for (int i = 0; i < 4; i++) {
            int x = 64 + i % 2 * 160, y = 110 + i / 2 * 90;
            render_internal_rect_fill(c, x, y, 148, 78, i == lit ? colors[i] : 0x173540);
            render_internal_rect_edge(c, x, y, 148, 78, colors[i]);
            snprintf(text, sizeof text, "%d", i + 1);
            render_internal_text(c, x + 66, y + 25, 3, i == lit ? MSP_INK : colors[i], text);
        }
        snprintf(text, sizeof text, g_game.launched ? "WATCH THE FLASHES; THEN COPY." : "YOUR TURN: %d OF %d", g_game.moves, g_game.rounds);
        render_internal_text(c, 64, 306, 1, MSP_TEAL, text);
    }
}
static void game_extra_render(render_ctx_t *c) {
    char text[80];
    if (g_game.page == GAME_DEFENDER) {
        snprintf(text, sizeof text, "WAVE %d/5  REEF %d  SCORE %d", g_game.wave, g_game.hp, g_game.score);
        render_internal_text(c, 24, 88, 2, MSP_TEAL, text);
        for (int i = 0; i < 8; i++) {
            if (g_game.foes[i].hp) {
                int x = (int)g_game.foes[i].x, y = (int)g_game.foes[i].y;
                render_internal_rect_fill(c, x - 14, y - 9, 28, 18, g_game.foes[i].hp > 1 ? 0xff8173 : 0xffdc73);
                render_internal_rect_fill(c, x - 7, y - 4, 4, 4, MSP_INK);
            }
            if (g_game.shots[i].hp) render_internal_ring(c, (int)g_game.shots[i].x, (int)g_game.shots[i].y, 4, 0x73e5ff);
        }
        render_internal_rect_fill(c, 32, 300, 384, 8, 0x89ffb1); render_internal_ring(c, 224, 300, 14, MSP_TEAL);
        render_internal_text(c, 60, 316, 2, MSP_TEAL, "TAP THE WATER TO AIM AND FIRE");
    } else if (g_game.page == GAME_RUNNER) {
        snprintf(text, sizeof text, "GATES %d/18  HULL %d  SCORE %d", g_game.rounds, g_game.hp, g_game.score);
        render_internal_text(c, 24, 88, 2, MSP_TEAL, text);
        for (int i = 0; i < 5; i++) {
            int x = 24 + i * 80;
            render_internal_rect_fill(c, x, 110, 76, 180, 0x102831);
            int y = 110 + (int)g_game.travel;
            if (i != g_game.target && i != (g_game.target + 1) % 5) render_internal_rect_fill(c, x + 5, y, 66, 14, 0x8b6570);
            else if (i == g_game.target) render_internal_ring(c, x + 38, y + 6, 7, 0xffdc73);
        }
        render_internal_rect_fill(c, 45 + g_game.lane * 80, 278, 32, 12, MSP_TEAL);
        render_internal_text(c, 48, 308, 2, MSP_TEAL, "TAP A LANE - GOLD IS 3 POINTS");
    } else if (g_game.page == GAME_BREAKER) {
        render_internal_button(c, 280, 54, 144, 32, 0x1c2f36, MSP_TEAL, "BANK SCORE", 1);
        snprintf(text, sizeof text, "BALLS %d  SCORE %d", g_game.hp, g_game.score);
        render_internal_text(c, 24, 88, 2, MSP_TEAL, text);
        for (int i = 0; i < 24; i++) if (g_game.board[i])
            render_internal_rect_fill(c, 30 + i % 6 * 65, 108 + i / 6 * 24, 58, 18, g_game.board[i] == 2 ? 0xffdc73 : 0x73e5ff);
        render_internal_rect_fill(c, (int)g_game.px - 44, 284, 88, 8, MSP_TEAL);
        render_internal_ring(c, (int)g_game.bx, (int)g_game.by, 5, 0xffffff);
        render_internal_text(c, 40, 310, 2, MSP_TEAL, g_game.launched ? "TAP BELOW BRICKS TO MOVE PADDLE" : "TAP BELOW BRICKS TO LAUNCH");
    } else if (g_game.page == GAME_TACTICS || g_game.page == GAME_ARCHITECT) {
        if (g_game.page == GAME_TACTICS) snprintf(text, sizeof text, "HP %d  TURN %d/22  FLAGS %d/3", g_game.hp, g_game.moves, g_game.pairs);
        else snprintf(text, sizeof text, "PLACED %d/12  HABITAT SCORE %d", g_game.moves, g_game.score);
        render_internal_text(c, 24, 66, 2, MSP_TEAL, text);
        for (int i = 0; i < 25; i++) {
            int x = 99 + i % 5 * 50, y = 92 + i / 5 * 40, tile = g_game.board[i];
            uint32_t color = tile == 3 ? 0x785b6a : tile == 4 ? 0xffdc73 :
                tile == 2 ? (g_game.page == GAME_TACTICS ? 0xff8173 : 0x89ffb1) : tile == 1 ? 0xffa3cf : 0x102831;
            render_internal_rect_fill(c, x, y, 46, 36, color);
            if (g_game.page == GAME_TACTICS && tile == 2 && game_distance(i, g_game.selected) == 1)
                render_internal_rect_edge(c, x, y, 46, 36, 0xffdc73);
            if (g_game.page == GAME_TACTICS && i == g_game.selected) {
                render_internal_ring(c, x + 23, y + 18, 12, MSP_TEAL); render_internal_text(c, x + 18, y + 10, 2, 0xffffff, "Y");
            }
        }
        if (g_game.page == GAME_TACTICS) render_internal_button(c, 24, 298, 400, 30, 0x1c2f36, MSP_TEAL, "GUARD: BLOCK ATTACKS / HEAL 1", 2);
        else {
            render_internal_button(c, 24, 298, 126, 30, 0x1c2f36, g_game.selected == 1 ? 0xffdc73 : MSP_TEAL, "CORAL", 2);
            render_internal_button(c, 160, 298, 126, 30, 0x1c2f36, g_game.selected == 2 ? 0xffdc73 : MSP_TEAL, "GRASS", 2);
            render_internal_button(c, 296, 298, 128, 30, 0x1c2f36, MSP_TEAL, "FINISH", 2);
        }
    } else {
        static const char *const routes[] = {"SUPPLIES", "KEY CACHE", "DARK TUNNEL", "LOCKED RUIN", "REST REEF"};
        static const char *const costs[] = {"O2 -2", "O2 -2", "O2 -4 HP -2", "O2 -2 KEY -1", "FOOD -1 O2 +3"};
        static const char *const loot[] = {"+FOOD / 2PT", "+KEY / 2PT", "+9 POINTS", "+8 POINTS", "+1 HP"};
        snprintf(text, sizeof text, "DEPTH %d/8  O2 %d  HP %d", g_game.depth, g_game.oxygen, g_game.hp);
        render_internal_text(c, 24, 88, 2, MSP_TEAL, text);
        snprintf(text, sizeof text, "KEYS %d  FOOD %d  LOOT %d", g_game.keys, g_game.supplies, g_game.score);
        render_internal_text(c, 24, 110, 2, MSP_TEAL, text);
        for (int i = 0; i < 3; i++) {
            int route = g_game.choices[i], x = 24 + i * 136;
            bool locked = (route == 3 && !g_game.keys) || (route == 4 && !g_game.supplies);
            render_internal_rect_fill(c, x, 138, 128, 140, 0x1c2f36);
            render_internal_text(c, x + 5, 158, 1, locked ? 0x8b6570 : 0xffdc73, routes[route]);
            render_internal_text(c, x + 5, 196, 1, MSP_TEAL, costs[route]);
            render_internal_text(c, x + 5, 230, 1, MSP_TEAL, locked ? "NEEDS ITEM" : loot[route]);
        }
        render_internal_button(c, 24, 298, 400, 30, 0x1c2f36, MSP_TEAL, "RETURN: BANK LOOT SAFELY", 2);
    }
}
/* Fixed 40x32 previews deliberately share the games' board colors and shapes. */
GAME_LOCAL void game_thumb(render_ctx_t *c, int x, int y, int game) {
    const uint32_t blue = 0x73e5ff, gold = 0xffdc73, pink = 0xffa3cf;
    render_internal_rect_fill(c, x, y, 40, 32, 0x092027);
    if (game == 12) {
        for (int i = 0; i < 16; i++)
            render_internal_rect_fill(c, x + 3 + i % 4 * 9, y + 2 + i / 4 * 7, 6, 5,
                      i == 2 || i == 5 || i == 6 || i == 7 || i == 10 ? gold : 0x24404b);
    } else if (game == 13) {
        for (int i = 0; i < 8; i++) {
            int xx = x + 4 + i % 3 * 11, yy = y + 1 + i / 3 * 10;
            render_internal_rect_fill(c, xx, yy, 9, 8, blue);
            render_internal_rect_fill(c, xx + 3, yy + 2, 2, 4, MSP_INK);
        }
    } else if (game == 14) {
        render_internal_ring(c, x + 20, y + 17, 12, blue);
        render_internal_ring(c, x + 20, y + 17, 6, MSP_TEAL);
        render_internal_rect_fill(c, x + 19, y + 4, 2, 14, gold);
        render_internal_ring(c, x + 27, y + 23, 3, gold);
    } else if (game == 15) {
        render_internal_rect_fill(c, x + 2, y + 9, 11, 15, blue);
        render_internal_rect_fill(c, x + 16, y + 9, 11, 15, blue);
        render_internal_rect_fill(c, x + 29, y + 6, 9, 21, pink);
        render_internal_text(c, x + 5, y + 12, 1, MSP_INK, "2");
        render_internal_text(c, x + 19, y + 12, 1, MSP_INK, "2");
        render_internal_text(c, x + 31, y + 12, 1, MSP_INK, "4");
    } else if (game == 16) {
        for (int i = 0; i < 4; i++)
            render_internal_rect_fill(c, x + 3 + i * 4, y + 25 - i * 7, 29 - i * 5, 5, i == 3 ? gold : blue);
    } else if (game == 17) {
        for (int i = 0; i < 4; i++) {
            int xx = x + 3 + i % 2 * 19, yy = y + 2 + i / 2 * 15;
            render_internal_rect_edge(c, xx, yy, 16, 12, i == 1 ? gold : pink);
            if (i == 1) render_internal_rect_fill(c, xx + 3, yy + 3, 10, 6, gold);
        }
    } else if (game == 0) {
        render_internal_ring(c, x + 12, y + 19, 8, blue);
        render_internal_ring(c, x + 29, y + 9, 6, gold);
        render_internal_rect_fill(c, x + 8, y + 14, 3, 2, 0xffffff);
        render_internal_rect_fill(c, x + 26, y + 6, 2, 2, 0xffffff);
    } else if (game == 1) {
        for (int i = 0; i < 4; i++) {
            int xx = x + 4 + i % 2 * 18, yy = y + 2 + i / 2 * 15;
            render_internal_rect_edge(c, xx, yy, 13, 12, i % 3 ? MSP_TEAL : gold);
            if (i % 3 == 0) render_internal_ring(c, xx + 6, yy + 6, 3, gold);
            else render_internal_rect_fill(c, xx + 4, yy + 3, 5, 6, 0x24505a);
        }
    } else if (game == 2 || game == 7) {
        for (int i = 0; i < 3; i++) {
            render_internal_rect_fill(c, x + 5 + i * 12, y + 2, 2, 27, 0x24505a);
            if (i != 1) render_internal_rect_fill(c, x + 2 + i * 12, y + 10, 8, 5, 0x8b6570);
        }
        if (game == 2) render_internal_page_stamp(c, x + 8, y + 10, STAMP_FISH, MSP_TEAL);
        else {
            render_internal_ring(c, x + 18, y + 11, 4, gold);
            render_internal_rect_fill(c, x + 13, y + 24, 11, 5, MSP_TEAL);
        }
    } else if (game == 3 || game == 11) {
        render_internal_rect_fill(c, x + 5, y + 20, 29, 9, gold);
        render_internal_rect_fill(c, x + 5, y + 19, 29, 3, 0xb37b4b);
        render_internal_rect_fill(c, x + 17, y + 21, 5, 6, 0xffffff);
        if (game == 3) {
            render_internal_rect_fill(c, x + 23, y + 1, 2, 13, blue);
            render_internal_rect_fill(c, x + 18, y + 12, 7, 3, blue);
            render_internal_rect_fill(c, x + 18, y + 9, 2, 4, blue);
        } else {
            render_internal_ring(c, x + 9, y + 8, 5, blue);
            render_internal_rect_fill(c, x + 13, y + 6, 17, 4, blue);
            render_internal_rect_fill(c, x + 25, y + 8, 3, 5, blue);
        }
    } else if (game == 4) {
        render_internal_page_stamp(c, x + 1, y + 1, STAMP_FISH, MSP_TEAL);
        for (int i = 0; i < 3; i++) render_internal_rect_fill(c, x + 28 + i % 2 * 5, y + 7 + i * 7, 4, 4, gold);
    } else if (game == 5) {
        render_internal_rect_fill(c, x + 2, y + 4, 17, 5, blue);
        render_internal_rect_fill(c, x + 14, y + 4, 5, 21, blue);
        render_internal_rect_fill(c, x + 14, y + 20, 24, 5, gold);
        render_internal_ring(c, x + 6, y + 6, 5, MSP_TEAL);
        render_internal_ring(c, x + 34, y + 22, 5, MSP_TEAL);
    } else if (game == 6) {
        for (int i = 0; i < 3; i++) {
            render_internal_rect_fill(c, x + 4 + i * 12, y + 3, 7, 7, i == 1 ? gold : pink);
            render_internal_rect_fill(c, x + 6 + i * 12, y + 10, 3, 3, pink);
        }
        render_internal_rect_fill(c, x + 19, y + 17, 2, 3, blue);
        render_internal_rect_fill(c, x + 15, y + 26, 11, 4, MSP_TEAL);
        render_internal_rect_fill(c, x + 19, y + 22, 3, 5, MSP_TEAL);
    } else if (game == 8) {
        for (int i = 0; i < 8; i++)
            render_internal_rect_fill(c, x + 2 + i % 4 * 9, y + 3 + i / 4 * 7, 7, 5, i % 3 ? blue : gold);
        render_internal_ring(c, x + 23, y + 20, 3, 0xffffff);
        render_internal_rect_fill(c, x + 9, y + 28, 20, 3, MSP_TEAL);
    } else {
        for (int i = 0; i < 9; i++) {
            int xx = x + 5 + i % 3 * 10, yy = y + 2 + i / 3 * 10;
            render_internal_rect_fill(c, xx, yy, 8, 8, 0x24505a);
            if (game == 9 && (i == 0 || i == 8))
                render_internal_rect_fill(c, xx + 2, yy + 2, 4, 4, i ? 0xff8173 : MSP_TEAL);
            if (game == 9 && i == 2) {
                render_internal_rect_fill(c, xx + 2, yy + 1, 2, 6, gold);
                render_internal_rect_fill(c, xx + 4, yy + 1, 3, 3, gold);
            }
            if (game == 10 && i % 3 != 1) {
                render_internal_rect_fill(c, xx + 3, yy + 1, 2, 6, i % 2 ? pink : 0x89ffb1);
                render_internal_rect_fill(c, xx + 1, yy + 3, 6, 2, i % 2 ? pink : 0x89ffb1);
            }
        }
    }
}
void render_games(const tank_t *t, uint16_t *fb, int stride) {
    render_ctx_t c = render_internal_page_ctx(fb, stride);
    render_internal_rect_fill(&c, 0, 0, PAGE_W, PAGE_H, MSP_INK);
    const char *title = g_game.page == GAME_RECORDS ? "GAME RECORDS" : g_game.page == GAME_MENU ? "MINI GAMES" :
        g_game.page == GAME_RESULT ? game_names[game_index(g_game.kind)] : game_titles[game_index(g_game.kind)];
    render_internal_text(&c, 24, 14, g_game.page == GAME_MENU || g_game.page == GAME_RECORDS ? 2 : 1, 0xffffff, title);
    render_internal_button(&c, 326, 8, 112, 36, 0x1c2f36, MSP_TEAL, "BACK", 2);
    char text[64];
    if (g_game.page == GAME_MENU) {
        snprintf(text, sizeof text, "PLAY AS: %s >", t->n_fish ? t->fish[g_game.fish % t->n_fish].name : "GUEST");
        render_internal_button(&c, 32, 60, 240, 42, 0x1c2f36, MSP_TEAL, text, 1);
        render_internal_button(&c, 280, 60, 144, 42, 0x1c2f36, MSP_TEAL, ">", 3);
        static const char *const genres[] = {
            "POP & SCORE", "FIND PAIRS", "SWIM GATES", "CAST & AIM",
            "FEED & TAP", "JOIN PIPES", "AIM & FIRE", "DODGE ROCKS",
            "HIT BRICKS", "TURN BASED", "PLANT REEF", "PICK ROUTE",
            "SWITCH OFF", "SLIDE TILES", "SONAR HUNT", "MERGE TILES",
            "TIME STACK", "COPY NOTES"
        };
        for (int i = 0; i < 6; i++) {
            int x = 24 + i % 2 * 204, y = 116 + i / 2 * 64, game = g_game_menu * 6 + i;
            render_internal_button(&c, x, y, 196, 56, 0x10282e, MSP_DIM, "", 1);
            game_thumb(&c, x + 6, y + 12, game);
            render_internal_text_wrap(&c, x + 54, y + 5, 136, 0xffffff, game_names[game]);
            render_internal_text(&c, x + 54, y + 40, 1, MSP_TEAL, genres[game]);
        }
        render_internal_button(&c, 24, 314, 196, 32, 0x1c2f36, MSP_TEAL, g_game_relaxed ? "RELAXED" : "CHALLENGE", 2);
        render_internal_button(&c, 228, 314, 196, 32, 0x1c2f36, MSP_TEAL, "RECORDS", 2);
    } else if (g_game.page == GAME_RECORDS) {
        render_internal_button(&c, 280, 54, 144, 32, 0x1c2f36, MSP_TEAL, ">", 3);
        static const char *const short_names[] = {"BUBBLES", "MATCH", "SLALOM", "CAST", "FRENZY", "REEF"};
        static const char *const medals[] = {"--", "BRONZE", "SILVER", "GOLD"};
        for (int i = 0; i < 6; i++) {
            int y = 94 + i * 38;
            const game_records_t *records = g_game_menu == 2 ? &t->games_extra : &t->games;
            unsigned medal = g_game_menu ? records->medals[i] : t->companion.game_medals[i];
            render_internal_rect_fill(&c, 24, y - 2, 400, 35, 0x10282e);
            game_thumb(&c, 28, y - 1, g_game_menu * 6 + i);
            render_internal_page_medal(&c, 394, y + 2, medal);
            if (g_game_menu) {
                snprintf(text, sizeof text, "%s  %s", game_names[g_game_menu * 6 + i], medals[medal < 4 ? medal : 0]);
                render_internal_text(&c, 76, y + 1, 1, MSP_TEAL, text);
                snprintf(text, sizeof text, "BEST %u%s  ROUNDS %u", (unsigned)records->best[i],
                         g_game_menu == 2 && i < 2 ? " MOVES" : "", (unsigned)records->rounds[i]);
                render_internal_text(&c, 76, y + 18, 1, 0xffffff, text);
                continue;
            }
            snprintf(text, sizeof text, "%s%s  %s", t->companion.featured == i ? "> " : "",
                     short_names[i], medals[medal < 4 ? medal : 0]);
            render_internal_text(&c, 76, y + 1, 1, MSP_TEAL, text);
            snprintf(text, sizeof text, "BEST %u%s  ROUNDS %u", (unsigned)t->companion.game_best[i],
                     i == 1 || i == 5 ? " MOVES" : "", (unsigned)t->companion.game_rounds[i]);
            render_internal_text(&c, 76, y + 18, 1, 0xffffff, text);
        }
    } else if (g_game.page != GAME_RESULT && (g_game.help || g_game.paused)) {
        /* Help replaces the playfield, including HUD and edge instructions.
         * Drawing a smaller panel over gameplay leaves scaled glyph fragments. */
    } else if (g_game.page >= GAME_LIGHTHOUSE) {
        game_practice_render(&c);
    } else if (g_game.page >= GAME_DEFENDER) {
        game_extra_render(&c);
    } else if (g_game.page == GAME_BUBBLES) {
        if (g_game_relaxed) snprintf(text, sizeof text, "SCORE %d", g_game.score);
        else snprintf(text, sizeof text, "TIME %02d   SCORE %d", (int)ceilf(g_game.left), g_game.score);
        render_internal_text(&c, 32, 62, 2, MSP_TEAL, text);
        for (int i = 0; i < 6; i++) if (g_game.bubbles[i].live) {
            int x = (int)g_game.bubbles[i].x, y = (int)g_game.bubbles[i].y;
            uint32_t color = g_game.bubbles[i].gold ? 0xffdc73 : 0x73e5ff;
            render_internal_ring(&c, x, y, 24, color); render_internal_ring(&c, x, y, 23, color);
            render_internal_rect_fill(&c, x - 10, y - 13, 8, 4, 0xffffff);
            if (g_game.bubbles[i].gold) render_internal_text(&c, x - 5, y - 7, 2, color, "3");
        }
    } else if (g_game.page == GAME_SLALOM || g_game.page == GAME_CAST || g_game.page == GAME_FRENZY) {
        if (g_game_relaxed && g_game.page == GAME_FRENZY) snprintf(text, sizeof text, "SCORE %d", g_game.score);
        else snprintf(text, sizeof text, "SCORE %d   %s %d", g_game.score, g_game.page == GAME_FRENZY ? "TIME" : "ROUND",
                      g_game.page == GAME_FRENZY ? (int)ceilf(g_game.left) : g_game.rounds + 1);
        render_internal_text(&c, 24, 58, 2, MSP_TEAL, text);
        if (g_game.page == GAME_SLALOM) {
            render_internal_text(&c, 24, 82, 2, MSP_TEAL, "SWIM THROUGH THE OPEN GATE");
            for (int lane = 0; lane < 3; lane++) {
                int x = 74 + lane * 140;
                render_internal_rect_fill(&c, x - 46, 112, 92, 148, 0x102831);
                if (lane == g_game.target) {
                    render_internal_rect_edge(&c, x - 36, 112 + (int)(g_game.travel * 0.55f), 72, 12, 0xffdc73);
                    render_internal_rect_fill(&c, x - 3, 115 + (int)(g_game.travel * 0.55f), 6, 6, 0xffdc73);
                }
                else render_internal_rect_fill(&c, x - 44, 112 + (int)(g_game.travel * 0.55f), 88, 12, 0x8b6570);
            }
            if (t->n_fish) render_fish_portrait(fb, stride, 74 + g_game.lane * 140, 235, 1.3f, &t->fish[g_game.fish % t->n_fish], g_game.travel * 0.03f);
            render_internal_button(&c, 32, 276, 182, 52, 0x1c2f36, MSP_TEAL, "< LEFT", 2);
            render_internal_button(&c, 234, 276, 182, 52, 0x1c2f36, MSP_TEAL, "RIGHT >", 2);
        } else if (g_game.page == GAME_CAST) {
            render_internal_text(&c, 24, 86, 2, MSP_TEAL, "STOP THE HOOK OVER THE TREASURE");
            int tx = 32 + (int)((g_game.target + 0.5f) * 384 / 3);
            render_internal_rect_fill(&c, tx - 42, 218, 84, 38, 0xa97532); render_internal_rect_edge(&c, tx - 42, 218, 84, 38, 0xffdc73);
            render_internal_text(&c, tx - 12, 226, 2, 0xffdc73, "$");
            float pos = g_game.travel <= 1 ? g_game.travel : 2 - g_game.travel;
            int hx = 32 + (int)(pos * 384);
            render_internal_rect_fill(&c, hx, 116, 2, 84, MSP_TEAL); render_internal_ring(&c, hx - 5, 201, 6, 0xffffff);
            for (int lane = 0; lane < 3; lane++) if (lane != g_game.target) {
                if (t->n_fish && lane == (g_game.target + 1) % 3)
                    render_fish_portrait(fb, stride, 96 + lane * 128, 234, 0.7f,
                                         &t->fish[g_game.fish % t->n_fish], g_game.travel);
                else render_internal_rect_fill(&c, 64 + lane * 128, 224, 62, 26, 0x785b6a);
            }
            render_internal_button(&c, 32, 276, 384, 52, 0x1c2f36, MSP_TEAL, "CAST!", 3);
        } else {
            static const char *const snacks[] = {"FLAKES", "WORMS", "GREENS"};
            snprintf(text, sizeof text, "WANTS: %s", snacks[g_game.target]);
            render_internal_text(&c, 80, 98, 3, 0xffdc73, text);
            render_internal_rect_fill(&c, 84 + g_game.target * 136, 252, 8, 8, 0xffdc73);
            if (t->n_fish) {
                const fish_t *f = &t->fish[g_game.fish % t->n_fish];
                render_fish_portrait(fb, stride, 224, 182, 2, f, g_game.left);
                render_internal_text(&c, 32, 232, 2, MSP_TEAL, f->name);
            }
            for (int i = 0; i < 3; i++) render_internal_button(&c, 24 + i * 136, 268, 128, 58, 0x1c2f36, MSP_TEAL, snacks[i], 2);
        }
    } else if (g_game.page == GAME_REEF) {
        render_internal_text(&c, 24, 58, 2, MSP_TEAL, "TAP PIPES TO CONNECT IN TO OUT");
        render_internal_text(&c, 32, 191, 2, 0x73e5ff, "IN >"); render_internal_text(&c, 344, 191, 2, 0xffdc73, "> OUT");
        for (int i = 0; i < 9; i++) {
            int x = 112 + i % 3 * 72, y = 98 + i / 3 * 72, mask = g_game.pipes[i];
            render_internal_rect_fill(&c, x, y, 66, 66, 0x1c2f36); render_internal_rect_edge(&c, x, y, 66, 66, MSP_TEAL);
            if (mask & 1) render_internal_rect_fill(&c, x + 27, y + 1, 12, 38, 0x73e5ff);
            if (mask & 2) render_internal_rect_fill(&c, x + 27, y + 27, 38, 12, 0x73e5ff);
            if (mask & 4) render_internal_rect_fill(&c, x + 27, y + 27, 12, 38, 0x73e5ff);
            if (mask & 8) render_internal_rect_fill(&c, x + 1, y + 27, 38, 12, 0x73e5ff);
        }
    } else if (g_game.page == GAME_MATCH) {
        snprintf(text, sizeof text, "PAIRS %d/6   MOVES %d", g_game.pairs, g_game.moves);
        render_internal_text(&c, 32, 62, 2, MSP_TEAL, text);
        static const uint32_t colors[] = {0xffdc73, 0x73e5ff, 0xffa3cf, 0x89ffb1, 0xc5adff, 0xff8173};
        for (int i = 0; i < 12; i++) {
            int x = 32 + i % 4 * 96, y = 94 + i / 4 * 72;
            bool shown = g_game.matched[i] || i == g_game.first || i == g_game.second;
            render_internal_button(&c, x, y, 88, 64, g_game.matched[i] ? 0x124236 : 0x1c2f36, MSP_TEAL, shown ? "" : "?", 3);
            if (shown) {
                uint32_t color = colors[g_game.cards[i]];
                render_internal_ring(&c, x + 22, y + 32, 12, color);
                snprintf(text, sizeof text, "%d", g_game.cards[i] + 1);
                render_internal_text(&c, x + 46, y + 21, 3, color, text);
            }
        }
    } else {
        game_thumb(&c, 32, 72, game_index(g_game.kind));
        render_internal_text(&c, 84, 80, 2, 0xffffff, "ROUND COMPLETE");
        bool puzzle = g_game.kind == GAME_LIGHTHOUSE || g_game.kind == GAME_SLIDE;
        bool solved = g_game.kind == GAME_MATCH || g_game.kind == GAME_REEF || (puzzle && g_game.reward);
        snprintf(text, sizeof text, solved ? "SOLVED IN %d MOVES" : "SCORE: %d", solved ? g_game.moves : g_game.score);
        if (puzzle && !solved) snprintf(text, sizeof text, "UNSOLVED - NO COINS OR XP");
        render_internal_text(&c, 32, 126, 2, MSP_TEAL, text);
        snprintf(text, sizeof text, "WALLET: %ld SAND DOLLARS", (long)t->sd_balance);
        render_internal_text(&c, 32, 164, 2, MSP_TEAL, text);
        int payout = progression_game_payout(t, g_game.reward);
        snprintf(text, sizeof text, g_game.claimed ? "SAVED +%d SAND DOLLARS" :
                 payout ? "SAVE RESULT +%d DOLLAR" : "SAVE RESULT - NO BONUS",
                 g_game.claimed ? g_game.paid : payout);
        if (g_game.kind >= GAME_LIGHTHOUSE && !g_game.reward)
            snprintf(text, sizeof text, g_game.claimed ? "RESULT SAVED - NO REWARD" : "SAVE RESULT - NO REWARD");
        if (progression_creative(t)) snprintf(text, sizeof text, "CREATIVE - PRACTICE ONLY");
        render_internal_button(&c, 32, 196, 396, 44, 0x1c2f36, MSP_TEAL, text, 2);
        render_internal_button(&c, 32, 278, 396, 44, 0x1c2f36, MSP_TEAL, "PLAY AGAIN", 2);
        snprintf(text, sizeof text, "GAMES: %d/5C TODAY - NO XP", t->care.game_paid);
        if (progression_creative(t)) snprintf(text, sizeof text, "NO COINS, XP OR RECORDS");
        render_internal_text(&c, 32, 248, 2, MSP_TEAL, text);
    }
    if (g_game.page != GAME_MENU && g_game.page != GAME_RECORDS && g_game.page != GAME_RESULT) {
        render_internal_button(&c, 244, 8, 76, 36, 0x1c2f36, MSP_TEAL, g_game.paused ? "PLAY" : "PAUSE", 1);
        render_internal_button(&c, 164, 8, 72, 36, 0x1c2f36, MSP_TEAL, "HELP", 1);
        if (g_game.help || g_game.paused) {
            static const char *const help[18][5] = {
                {"Tap rising bubbles.", "Gold bubbles score three.", "Challenge lasts 30 seconds.", "Relaxed: FINISH when ready.", "Games share 5 coins/day; no XP."},
                {"Tap two cards to reveal.", "Match all six pearl pairs.", "Fewer moves earn medals.", "Mismatch hides after a moment.", "No timer."},
                {"Use LEFT / RIGHT to swim.", "Follow the gold open gate.", "Twelve gates per round.", "Each clear gate scores two.", "Relaxed gates move slower."},
                {"Tap CAST over the treasure.", "Watch the moving hook.", "Five casts per round.", "Each accurate cast scores two.", "Relaxed hook moves slower."},
                {"Tap the requested food.", "Wrong choices lose a point.", "Challenge lasts 30 seconds.", "Relaxed: FINISH when ready.", "No real fish are fed."},
                {"Tap pipes to rotate them.", "Connect IN on left to OUT.", "Both pipe ends must meet.", "Fewer moves earn medals.", "No timer."},
                {"Tap water to aim and fire.", "Defend the reef for 5 waves.", "Armored targets take 2 hits.", "Five breaches end the round.", "Each target scores 2."},
                {"Tap any of the five lanes.", "Avoid the scrolling rocks.", "Gold lane: 3, other gap: 1.", "Survive 18 gates; 5 hull.", "Relaxed scrolls more slowly."},
                {"Tap below bricks to launch.", "Tap left/right to move paddle.", "Gold bricks need two hits.", "Paddle edges angle the ball.", "24 bricks / 3 balls. BANK ends."},
                {"Tap a neighboring grid cell.", "Red: attack. Gold: capture.", "Enemies move after your turn.", "Gold outline warns of attack.", "GUARD blocks/heals. 22 turns."},
                {"Place 6 coral and 6 grass.", "Choose species, then tap cell.", "Tap planted cells to remove.", "+1 per tile, +1 by open water,", "+1 by other species. FINISH."},
                {"Choose one of three routes.", "Costs and loot shown on cards.", "Keys open ruins; food heals.", "RETURN banks loot plus hull.", "8 depths. No air/hull: 1/2 loot."},
                {"Tap a lamp: it and its four", "neighbors switch on or off.", "Solve for +1 (daily cap).", "Gold: solve in 8 moves or less.", "FINISH saves; unsolved: no pay."},
                {"Tap a tile beside the gap.", "Slide numbers into order 1-8.", "Solve for +1 (daily cap).", "Gold: solve in 24 moves.", "FINISH saves; unsolved: no pay."},
                {"Find five pearls in the grid.", "Gold: total pearls per row/col.", "15 probes; Relaxed gives 20.", "2+ pearls qualify; five: gold.", "Bonus: +1, shared max 5/day."},
                {"Use arrows to slide all tiles.", "Equal neighbors merge once.", "New tiles appear after moves.", "128+ qualifies; 2048: gold.", "Bonus: +1, shared max 5/day."},
                {"Tap DROP to place the gold bar.", "Only the overlap stays; missed", "edges are lost. Stack 12 layers.", "30+ qualifies; 120: gold.", "Bonus: +1, shared max 5/day."},
                {"Watch the numbered pads flash.", "Repeat the sequence by tapping.", "One new note per round, to 12.", "3+ qualifies; 12: gold.", "Bonus: +1, shared max 5/day."}
            };
            render_internal_rect_fill(&c, 24, 94, 400, 234, MSP_INK);
            render_internal_rect_edge(&c, 24, 94, 400, 234, MSP_TEAL);
            game_thumb(&c, 40, 103, game_index(g_game.kind));
            render_internal_text(&c, 96, 108, 2, 0xffdc73, g_game.paused ? "PAUSED" : "HOW TO PLAY");
            for (int i = 0; i < 5; i++) render_internal_text(&c, 40, 148 + i * 24, 1, MSP_TEAL, help[game_index(g_game.kind)][i]);
            render_internal_button(&c, 32, 280, 384, 44, 0x1c2f36, MSP_TEAL, g_game.paused ? "USE PLAY ABOVE TO RESUME" : "PLAY", 1);
        }
    }
    if (!g_game.help && !g_game.paused && g_game_relaxed && (g_game.page == GAME_BUBBLES || g_game.page == GAME_FRENZY))
        render_internal_button(&c, 280, 54, 144, 34, 0x1c2f36, MSP_TEAL, "FINISH", 2);
    if (g_game.page == GAME_MENU) {
        static const char *const featured[] = {"BUBBLES", "MATCH", "SLALOM", "CAST", "FRENZY", "REEF"};
        if (progression_creative(t)) snprintf(text, sizeof text, "CREATIVE: NO COINS, XP OR RECORDS");
        else if (g_game_menu == 2) snprintf(text, sizeof text, "+1, SHARED 5/DAY - NO XP");
        else snprintf(text, sizeof text, "SUGGESTED: %s - NO EXPIRY", featured[t->companion.featured]);
        render_internal_text(&c, 24, 350, progression_creative(t) ? 1 : 2, MSP_TEAL, text);
    } else render_internal_text(&c, 24, 340, 2,
                    g_game.save_failed ? 0xff8173 : MSP_TEAL,
                    g_game.save_failed ? "SAVE FAILED - TAP CLAIM TO RETRY" :
                    g_game.discard ? "TAP AGAIN TO DISCARD RESULT" :
                    g_game_relaxed ? "RELAXED - NO RECORDS OR MEDALS" : "AQUARIUM CARE PAUSED");
}
