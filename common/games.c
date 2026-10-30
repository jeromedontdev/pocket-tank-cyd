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
#define GAME_MENU_PAGES 4                   /* three pages of six games, then the story games */
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
/* ---- Casino: Main Menu > Casino, fixed-odds doubloon games ----
 * Stakes and returns share one wallet transaction. Dive holds its stake until
 * bust or surface; BACK surfaces too, never discarding an unsettled wager. */
enum { CASINO_LOBBY, CASINO_RACES, CASINO_REELS, CASINO_HILO, CASINO_SHELLS, CASINO_DIVE, CASINO_WHEEL };
GAME_LOCAL const int casino_bets[4] = {5, 10, 25, 50};
GAME_LOCAL const int casino_odds[4] = {2, 4, 5, 6};
GAME_LOCAL const int casino_reel_weight[5] = {6, 5, 4, 2, 1};
GAME_LOCAL const int casino_triple_pay[5] = {3, 6, 10, 20, 50};
/* Even tenths keep every advertised Dive payout exact, including a 5 bet. */
GAME_LOCAL const int casino_depth_pay[5] = {14, 16, 20, 26, 34};
static const char *const casino_titles[7] = {"CASINO", "FISH RACES", "REEF REELS", "PEARL HI-LO", "SHELL GAME", "TREASURE DIVE", "TIDE WHEEL"};
static struct {
    bool active, running, shown, diving, pending_end;
    int page, bet, pick, winner, delta, pearl, next, depth, stake, payout;
    float t, finish[4];
    int racer[4];
    uint8_t reels[3];
    char msg[72];
} s_cas;
static void casino_close(void) { memset(&s_cas, 0, sizeof s_cas); }
void render_casino_open(void) { render_games_open(); s_cas.active = true; s_cas.bet = 1; }
bool render_casino_active(void) { return s_cas.active; }
GAME_LOCAL int casino_reel_symbol(uint32_t r) {
    int roll = (int)(r % 18u);
    for (int i = 0; i < 5; i++) { if (roll < casino_reel_weight[i]) return i; roll -= casino_reel_weight[i]; }
    return 0;
}
GAME_LOCAL int casino_reel_pay(const uint8_t r[3]) {
    if (r[0] == r[1] && r[1] == r[2]) return casino_triple_pay[r[0] % 5];
    int pair = r[0] == r[1] || r[0] == r[2] ? r[0] : r[1] == r[2] ? r[1] : -1;
    return pair < 0 ? 0 : pair == 4 ? 2 : 1;
}
GAME_LOCAL int casino_race_winner(uint32_t r) {
    int roll = (int)(r % 10000u), acc = 0;
    for (int i = 0; i < 4; i++) { acc += 8950 / casino_odds[i]; if (roll < acc) return i; }
    return 3;
}
/* Hi-Lo odds follow the visible value, so picking the easier side cannot
 * beat the house. 1222/1300 = 94%; whole-doubloon returns round down. */
GAME_LOCAL int casino_hilo_pay(int stake, int pearl, int higher, int next) {
    int wins = higher ? 13 - pearl : pearl - 1;
    return (higher ? next > pearl : next < pearl) && wins > 0 ? stake * 1222 / (wins * 100) : 0;
}
GAME_LOCAL int casino_shell_pay(int stake, int pick, uint32_t roll) {
    return pick == (int)(roll % 3u) ? stake * 28 / 10 : 0;
}
GAME_LOCAL bool casino_dive_survive(int depth, uint32_t roll) {
    int previous = depth ? casino_depth_pay[depth - 1] * 1000 : 9400;
    return (int)(roll % 10000u) < previous * 10 / casino_depth_pay[depth];
}
GAME_LOCAL int casino_wheel_color(uint32_t roll) {
    int r = (int)(roll % 10000u);
    return r < 4615 ? 0 : r < 9230 ? 1 : 2;
}
GAME_LOCAL int casino_wheel_pay(int stake, int pick, uint32_t roll) {
    return pick == casino_wheel_color(roll) ? stake * (pick == 2 ? 12 : 2) : 0;
}
static void casino_new_field(void) {
    int v = (int)(game_random() % FISH_VARIETY_COUNT);
    static const int step[4] = {0, 2, 5, 7};
    for (int i = 0; i < 4; i++) s_cas.racer[i] = (v + step[i]) % FISH_VARIETY_COUNT;
    s_cas.pick = 0; s_cas.running = false; s_cas.shown = false; s_cas.t = 0;
}
static bool casino_can_bet(tank_t *t, int bet) {
    if (progression_creative(t)) { snprintf(s_cas.msg, sizeof s_cas.msg, "CREATIVE MODE HAS NO BETS."); return false; }
    if (t->sd_balance < bet) { snprintf(s_cas.msg, sizeof s_cas.msg, "YOU NEED %d DOUBLOONS TO BET.", bet); return false; }
    return true;
}
static bool casino_settle(tank_t *t, int stake, int payout) {
    if (!casino_can_bet(t, stake)) return false;
    if (!progression_wallet_settle(t, payout - stake)) { snprintf(s_cas.msg, sizeof s_cas.msg, "SAVE FAILED. TRY AGAIN."); return false; }
    s_cas.delta = payout - stake; s_cas.msg[0] = 0;
    return true;
}
static void casino_result(void) {
    s_cas.running = false; s_cas.shown = true;
    if (s_cas.delta > 0) snprintf(s_cas.msg, sizeof s_cas.msg, "A WIN! +%d DOUBLOONS", s_cas.delta);
    else if (!s_cas.delta) snprintf(s_cas.msg, sizeof s_cas.msg, "YOUR STAKE COMES BACK.");
    else snprintf(s_cas.msg, sizeof s_cas.msg, "NO LUCK. %d DOUBLOONS", s_cas.delta);
}
static bool casino_surface(tank_t *t) {
    if (!s_cas.pending_end) { s_cas.payout = s_cas.stake * casino_depth_pay[s_cas.depth - 1] / 10; s_cas.pending_end = true; }
    if (!casino_settle(t, s_cas.stake, s_cas.payout)) return false;
    s_cas.diving = false; s_cas.pending_end = false; casino_result(); return true;
}
static void casino_tick(float dt) {
    if (!s_cas.running) return;
    s_cas.t += dt;
    float end = 1.9f;
    if (s_cas.page == CASINO_RACES) for (int i = 0; i < 4; i++) if (s_cas.finish[i] + 0.2f > end) end = s_cas.finish[i] + 0.2f;
    if (s_cas.t >= end) casino_result();
}
static bool casino_tap(tank_t *t, float x, float y) {
    if (x >= 326 && x < 438 && y >= 8 && y < 44) {
        if (s_cas.page == CASINO_LOBBY) { casino_close(); return true; }
        /* BACK is a cash-out, not an escape from a live Dive stake. */
        if (s_cas.diving && !casino_surface(t)) return false;
        s_cas.page = CASINO_LOBBY; s_cas.running = false; s_cas.msg[0] = 0; return false;
    }
    if (s_cas.page == CASINO_LOBBY) {
        for (int i = 0; i < 6; i++) {
            int cx = 24 + i % 2 * 204, cy = 84 + i / 2 * 76;
            if (x < cx || x >= cx + 196 || y < cy || y >= cy + 70) continue;
            s_cas.page = i + 1; s_cas.msg[0] = 0; s_cas.shown = false; s_cas.pick = 0;
            s_cas.pearl = 2 + (int)(game_random() % 11u); s_cas.next = 0; s_cas.depth = 0;
            if (s_cas.page == CASINO_RACES) casino_new_field();
            else { s_cas.reels[0] = 0; s_cas.reels[1] = 1; s_cas.reels[2] = 2; }
        }
        return false;
    }
    if (s_cas.running) return false;
    if (s_cas.page == CASINO_DIVE && s_cas.diving && x >= 236 && x < 424 && y >= 214 && y < 252) {
        casino_surface(t); return false;
    }
    if (y >= 268 && y < 302) {
        if (!s_cas.diving) for (int k = 0; k < 4; k++) if (x >= 24 + k * 64 && x < 82 + k * 64) { s_cas.bet = k; return false; }
        if (x >= 290 && x < 424) {
            int bet = casino_bets[s_cas.bet & 3];
            if (!casino_can_bet(t, bet)) return false;
            g_game_rng ^= (uint32_t)(t->clock * 1000.0f) * 2654435761u;
            if (!g_game_rng) g_game_rng = 0x7a5b193du;
            if (s_cas.page == CASINO_DIVE) {
                if (s_cas.pending_end) { casino_surface(t); return false; }
                if (!s_cas.diving) { s_cas.stake = bet; s_cas.depth = 0; s_cas.diving = true; s_cas.shown = false; }
                if (!casino_dive_survive(s_cas.depth, game_random())) {
                    s_cas.payout = 0; s_cas.pending_end = true; casino_surface(t);
                } else {
                    s_cas.depth++; s_cas.msg[0] = 0;
                    if (s_cas.depth == 5) casino_surface(t);
                }
                return false;
            }
            int payout = 0, winner = 0, next = 0;
            uint8_t r[3] = {0};
            if (s_cas.page == CASINO_RACES) {
                if (s_cas.shown) casino_new_field();
                winner = casino_race_winner(game_random());
                payout = winner == s_cas.pick ? bet * casino_odds[s_cas.pick] : 0;
            } else if (s_cas.page == CASINO_REELS) {
                for (int k = 0; k < 3; k++) r[k] = (uint8_t)casino_reel_symbol(game_random());
                payout = bet * casino_reel_pay(r);
            } else if (s_cas.page == CASINO_HILO) {
                next = 1 + (int)(game_random() % 13u);
                payout = casino_hilo_pay(bet, s_cas.pearl, s_cas.pick == 0, next);
            } else if (s_cas.page == CASINO_SHELLS) {
                uint32_t roll = game_random(); winner = (int)(roll % 3u);
                payout = casino_shell_pay(bet, s_cas.pick, roll);
            } else if (s_cas.page == CASINO_WHEEL) {
                uint32_t roll = game_random(); winner = casino_wheel_color(roll);
                payout = casino_wheel_pay(bet, s_cas.pick, roll);
            }
            if (!casino_settle(t, bet, payout)) return false;
            s_cas.winner = winner; s_cas.next = next; memcpy(s_cas.reels, r, 3);
            if (s_cas.page == CASINO_RACES) for (int i = 0; i < 4; i++)
                s_cas.finish[i] = i == winner ? 3.4f : 3.7f + (float)(game_random() % 90) / 100.0f;
            s_cas.running = true; s_cas.shown = false; s_cas.t = 0;
        }
        return false;
    }
    if (s_cas.page == CASINO_RACES && x >= 24 && x < 424 && y >= 84 && y < 260) {
        if (s_cas.shown) casino_new_field();
        s_cas.pick = (int)(y - 84) / 44; if (s_cas.pick > 3) s_cas.pick = 3;
    } else if (s_cas.page == CASINO_HILO && x >= 24 && x < 424 && y >= 208 && y < 252) {
        s_cas.pick = x < 224 ? 0 : 1;
    } else if ((s_cas.page == CASINO_SHELLS || s_cas.page == CASINO_WHEEL) && x >= 24 && x < 424 && y >= 100 && y < 252) {
        s_cas.pick = (int)(x - 24) / 136; if (s_cas.pick > 2) s_cas.pick = 2;
    }
    return false;
}
#ifdef RENDER_GAMES_TESTING
/* Read the revealed outcome independently of the wallet delta. */
int casino_test_return(void) {
    int bet = casino_bets[s_cas.bet];
    switch (s_cas.page) {
    case CASINO_HILO: return casino_hilo_pay(bet, s_cas.pearl, s_cas.pick == 0, s_cas.next);
    case CASINO_SHELLS: return casino_shell_pay(bet, s_cas.pick, (uint32_t)s_cas.winner);
    case CASINO_DIVE: return s_cas.payout;
    case CASINO_WHEEL: return s_cas.pick == s_cas.winner ? bet * (s_cas.pick == 2 ? 12 : 2) : 0;
    default: return 0;
    }
}
#endif
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
    casino_close();
}
int render_games_menu_page(void) { return g_game_menu; }
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
    if (s_cas.active) { casino_tick(dt); return; }
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
/* which tiles the inlet's water reaches; true when it also leaves by OUT */
static bool game_reef_flow(bool wet[9]) {
    for (int i = 0; i < 9; i++) wet[i] = false;
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
static bool game_reef_connected(void) {
    bool wet[9];
    return game_reef_flow(wet);
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
    if (s_cas.active) return casino_tap(t, x, y);
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
        if (x >= 280 && x < 424 && y >= 60 && y < 102) { g_game_menu = (g_game_menu + 1) % GAME_MENU_PAGES; return false; }
        if (y >= 316 && y < 352) {
            if (x >= 24 && x < 220) g_game_relaxed = !g_game_relaxed;
            else if (x >= 228 && x < 424) { g_game.page = GAME_RECORDS; g_game_menu %= 3; }
            return false;
        }
        if (x >= 32 && x < 272 && y >= 60 && y < 102 && t->n_fish)
            g_game.fish = (g_game.fish + 1) % t->n_fish;
        if (x >= 24 && x < 424 && y >= 116 && y < 308) {
            int col = x >= 228, row = (int)(y - 116) / 64;
            if (x >= (col ? 228 : 24) && x < (col ? 424 : 220) && y < 116 + row * 64 + 56) {
                if (g_game_menu < 3) game_start(game_kinds[g_game_menu * 6 + row * 2 + col]);
                else if (row == 0) {             /* the story games open in the companion pages */
                    render_life_story_open(col == 1, g_game.fish);
                    return true;
                }
            }
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
            int paid = progression_game_payout(t, game_index(g_game.kind), g_game.reward);
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
/* ---- game art kit (2026-10) ----
 * The playfields share the aquarium's illustrated look: a lit water column
 * over sand, and shaded props (pearls, shells, rocks, coral, crabs, lanterns,
 * bronze pipes) built from opaque ellipses/polygons, with a few translucent
 * touches. Every page here redraws each frame, so the kit stays at a handful
 * of spans per prop and the backdrop costs what the old flat fill did. */
#define GA_GOLD   0xf2c45a
#define GA_PEARL  0xf1ebdf
#define GA_SAND   0xc9a46c
#define GA_STONE  0x2f4f59
#define GA_SHADOW 0x05161d
#define GA_FLOOR  322
static uint32_t ga_mix(uint32_t a, uint32_t b, int p) {
    int ar = a >> 16 & 255, ag = a >> 8 & 255, ab = a & 255;
    int br = b >> 16 & 255, bg = b >> 8 & 255, bb = b & 255;
    return (uint32_t)(ar + (br - ar) * p / 256) << 16 | (uint32_t)(ag + (bg - ag) * p / 256) << 8 |
           (uint32_t)(ab + (bb - ab) * p / 256);
}
static uint32_t ga_dark(uint32_t c, int p) { return ga_mix(c, GA_SHADOW, p); }
static uint32_t ga_lite(uint32_t c, int p) { return ga_mix(c, 0xfff8ea, p); }
static void ga_fill(render_ctx_t *c, int x, int y, int w, int h, uint32_t rgb) {
    render_internal_rect_fill(c, x, y, w, h, rgb);
}
static void ga_ell(render_ctx_t *c, float x, float y, float rx, float ry, uint32_t rgb) {
    render_internal_ellipse(c, x, y, rx, ry, rgb, 255);
}
static void ga_quad(render_ctx_t *c, float x0, float y0, float x1, float y1,
                    float x2, float y2, float x3, float y3, uint32_t rgb, int alpha) {
    const float xs[4] = {x0, x1, x2, x3}, ys[4] = {y0, y1, y2, y3};
    render_internal_poly(c, xs, ys, 4, rgb, alpha);
}
static void ga_tri(render_ctx_t *c, float x0, float y0, float x1, float y1, float x2, float y2, uint32_t rgb) {
    const float xs[3] = {x0, x1, x2}, ys[3] = {y0, y1, y2};
    render_internal_poly(c, xs, ys, 3, rgb, 255);
}
/* cos/sin of i * pi / 8 for i = 0..16, filled once (the S3's trig is software) */
static float g_ga_cos[17], g_ga_sin[17];
static void ga_trig(void) {
    if (g_ga_cos[0] != 0) return;
    for (int i = 0; i <= 16; i++) { g_ga_cos[i] = cosf(i * 3.14159265f / 8); g_ga_sin[i] = sinf(i * 3.14159265f / 8); }
}
/* water column, light shafts, sand and a dark footer band for the status line */
static uint32_t g_ga_top, g_ga_bottom;
#define GA_BAND_Y(i) (48 + (GA_FLOOR - 48) * (i) / 12)
static uint32_t ga_band(int i) { return ga_mix(g_ga_top, g_ga_bottom, i * 256 / 11); }
/* A translucent tint over the flat water bands, drawn as opaque pre-mixed
   fills: game pages redraw every frame and per-pixel blends cost FPS. */
static void ga_wash(render_ctx_t *c, int x, int y, int w, int h, uint32_t rgb, int alpha) {
    for (int i = 0; i < 12; i++) {
        int y0 = GA_BAND_Y(i) > y ? GA_BAND_Y(i) : y, y1 = GA_BAND_Y(i + 1) < y + h ? GA_BAND_Y(i + 1) : y + h;
        if (y1 > y0) ga_fill(c, x, y0, w, y1 - y0, ga_mix(ga_band(i), rgb, alpha));
    }
}
static void ga_backdrop(render_ctx_t *c, uint32_t top, uint32_t bottom) {
    g_ga_top = top; g_ga_bottom = bottom;
    ga_fill(c, 0, 0, PAGE_W, 48, MSP_INK);
    ga_fill(c, 0, 47, PAGE_W, 1, 0x1f4b57);
    /* Each band is five opaque runs - water, shaft, water, shaft, water - so no
       pixel is written twice; the light shafts drift a step per band. */
    static const int shaft[2][4] = {{70, 112, 196, 128}, {262, 286, 350, 312}};
    for (int i = 0; i < 12; i++) {
        int y0 = GA_BAND_Y(i), h = GA_BAND_Y(i + 1) - y0, x = 0;
        uint32_t band = ga_band(i);
        for (int k = 0; k < 2; k++) {
            const int *sh = shaft[k];
            int l = sh[0] + (sh[3] - sh[0]) * (2 * i + 1) / 24, r = sh[1] + (sh[2] - sh[1]) * (2 * i + 1) / 24;
            ga_fill(c, x, y0, l - x, h, band);
            ga_fill(c, l, y0, r - l, h, ga_mix(band, 0xd8f4f2, k ? 12 : 16));
            x = r;
        }
        ga_fill(c, x, y0, PAGE_W - x, h, band);
    }
    for (int side = 0; side < 2; side++) {       /* kelp at the glass edges: stepped stalks */
        int x = side ? PAGE_W - 12 : 6, s = side ? -1 : 1;
        for (int i = 0; i < 4; i++) {
            int y = GA_FLOOR - i * 46, sway = (i & 1 ? 5 : -3) * s;
            for (int k = 0; k < 3; k++) ga_fill(c, x + sway * k / 2, y - 16 * (k + 1), 6, 16, 0x2f6b44);
            ga_ell(c, x + 3 + sway + 8 * s, y - 26, 8, 4, i & 1 ? 0x3f8552 : 0x356f47);
        }
    }
    ga_fill(c, 0, GA_FLOOR, PAGE_W, 12, ga_dark(GA_SAND, 70));
    for (int i = 0; i < 9; i++) ga_ell(c, 18 + i * 52, GA_FLOOR + 1, 30, 4, GA_SAND);
    for (int i = 0; i < 7; i++) ga_ell(c, 42 + i * 63, GA_FLOOR + 6, 4, 2, ga_dark(GA_SAND, 120));
    ga_fill(c, 0, GA_FLOOR + 12, PAGE_W, PAGE_H - GA_FLOOR - 12, 0x061a21);
}
/* readable status text: a dark glass plate sized to the string */
static void ga_hud(render_ctx_t *c, int x, int y, int scale, uint32_t rgb, const char *s) {
    int w = render_text_w(s, scale);
    render_internal_rect_blend(c, x - 6, y - 4, w + 12, 7 * scale + 8, 0x04141a, 190);
    ga_fill(c, x - 6, y - 4, w + 12, 1, 0x3f7884);
    render_internal_text(c, x, y, scale, rgb, s);
}
/* a bevelled block: dark lip below/right, light lip above/left */
static void ga_block(render_ctx_t *c, int x, int y, int w, int h, uint32_t base) {
    ga_fill(c, x, y, w, h, ga_dark(base, 130));
    ga_fill(c, x, y, w - 2, h - 3, base);
    ga_fill(c, x + 2, y, w - 6, 3, ga_lite(base, 95));
    ga_fill(c, x, y + 2, 3, h - 7, ga_lite(base, 50));
}
/* a recessed socket (an empty board cell) */
static void ga_socket(render_ctx_t *c, int x, int y, int w, int h, uint32_t base) {
    ga_fill(c, x, y, w, h, base);
    ga_fill(c, x, y, w, 3, ga_dark(base, 120));
    ga_fill(c, x, y + 3, 3, h - 3, ga_dark(base, 70));
    ga_fill(c, x + 3, y + h - 2, w - 3, 2, ga_lite(base, 40));
}
static void ga_speckle(render_ctx_t *c, int x, int y, int w, int h, unsigned seed, uint32_t rgb) {
    if (w < 14 || h < 14) return;
    for (int i = 0; i < 4; i++) {
        seed = seed * 1103515245u + 12345u;
        ga_fill(c, x + 4 + (int)(seed >> 8) % (w - 10), y + 5 + (int)(seed >> 18) % (h - 10), 3, 2, rgb);
    }
}
static void ga_pearl(render_ctx_t *c, float x, float y, float r, uint32_t tint) {
    ga_ell(c, x + r * 0.12f, y + r * 0.16f, r, r, ga_dark(tint, 120));
    ga_ell(c, x, y, r * 0.9f, r * 0.9f, tint);
    ga_ell(c, x - r * 0.2f, y - r * 0.2f, r * 0.55f, r * 0.5f, ga_lite(tint, 120));
    ga_ell(c, x - r * 0.36f, y - r * 0.4f, r * 0.22f + 0.6f, r * 0.18f + 0.6f, 0xffffff);
}
static void ga_bubble(render_ctx_t *c, float x, float y, float r, uint32_t tint) {
    render_internal_ellipse(c, x, y, r, r, tint, 46);
    render_internal_annulus(c, x, y, r, r - 3, ga_lite(tint, 110), 170);
    render_internal_ellipse(c, x + r * 0.1f, y + r * 0.62f, r * 0.5f, r * 0.13f, ga_lite(tint, 60), 120);
    render_internal_ellipse(c, x - r * 0.4f, y - r * 0.42f, r * 0.28f, r * 0.18f, 0xffffff, 230);
    render_internal_ellipse(c, x + r * 0.45f, y - r * 0.12f, r * 0.08f + 1, r * 0.14f + 1, 0xffffff, 150);
}
static void ga_rock(render_ctx_t *c, float x, float y, float w, float h, uint32_t base) {
    ga_ell(c, x + w * 0.04f, y + h * 0.1f, w * 0.5f, h * 0.48f, ga_dark(base, 150));
    ga_ell(c, x - w * 0.14f, y - h * 0.02f, w * 0.36f, h * 0.42f, base);
    ga_ell(c, x + w * 0.2f, y + h * 0.02f, w * 0.3f, h * 0.36f, ga_dark(base, 45));
    ga_ell(c, x - w * 0.2f, y - h * 0.18f, w * 0.18f, h * 0.14f, ga_lite(base, 70));
    ga_ell(c, x + w * 0.06f, y - h * 0.34f, w * 0.16f, h * 0.09f + 1, 0x5f8c4f);
}
/* branching coral: stalks with rounded tips */
static void ga_coral(render_ctx_t *c, float x, float y, float s, uint32_t tint) {
    uint32_t dark = ga_dark(tint, 80), tip = ga_lite(tint, 90);
    ga_quad(c, x - 3 * s, y, x + 3 * s, y, x + 2 * s, y - 14 * s, x - 2 * s, y - 14 * s, dark, 255);
    ga_quad(c, x - 1 * s, y - 8 * s, x + 2 * s, y - 10 * s, x - 8 * s, y - 19 * s, x - 10 * s, y - 16 * s, tint, 255);
    ga_quad(c, x + 1 * s, y - 6 * s, x - 1 * s, y - 9 * s, x + 9 * s, y - 17 * s, x + 11 * s, y - 14 * s, tint, 255);
    ga_ell(c, x - 9 * s, y - 18 * s, 2.6f * s, 2.6f * s, tip);
    ga_ell(c, x + 10 * s, y - 16 * s, 2.6f * s, 2.6f * s, tip);
    ga_ell(c, x, y - 15 * s, 2.8f * s, 2.8f * s, tip);
}
static void ga_grass(render_ctx_t *c, float x, float y, float s) {
    static const float lean[] = {-7, -2, 4, 9};
    for (int i = 0; i < 4; i++) {
        float bx = x + (i - 1.5f) * 4 * s;
        ga_quad(c, bx - 1.6f * s, y, bx + 1.6f * s, y, bx + lean[i] * s + 0.6f * s, y - (16 + (i & 1) * 5) * s,
                bx + lean[i] * s - 0.6f * s, y - (16 + (i & 1) * 5) * s, i & 1 ? 0x6fb35c : 0x4f9550, 255);
    }
}
/* a gold buoy (gate post) */
static void ga_buoy(render_ctx_t *c, float x, float y) {
    ga_fill(c, (int)x - 1, (int)y + 8, 3, 12, 0x6b5a3f);
    ga_ell(c, x, y, 8, 9, ga_dark(GA_GOLD, 90));
    ga_ell(c, x - 1, y - 1, 7, 8, GA_GOLD);
    ga_fill(c, (int)x - 7, (int)y - 1, 14, 3, 0xd25a4a);
    ga_ell(c, x - 3, y - 4, 2.4f, 2.2f, 0xfff4cf);
}
static void ga_chest(render_ctx_t *c, float x, float y, float s) {
    render_internal_ellipse(c, x, y, 40 * s, 26 * s, 0xffd77a, 34);
    ga_ell(c, x, y + 14 * s, 30 * s, 5 * s, ga_dark(GA_SAND, 140));
    ga_ell(c, x, y - 4 * s, 24 * s, 12 * s, 0x6e4122);
    ga_ell(c, x, y - 5 * s, 22 * s, 10 * s, 0x93592c);
    ga_fill(c, (int)(x - 24 * s), (int)(y - 2 * s), (int)(48 * s), (int)(16 * s), 0x7a4824);
    ga_fill(c, (int)(x - 24 * s), (int)(y + 6 * s), (int)(48 * s), (int)(2 * s), 0x5a3218);
    ga_fill(c, (int)(x - 24 * s), (int)(y - 3 * s), (int)(48 * s), (int)(3 * s), GA_GOLD);
    ga_fill(c, (int)(x - 17 * s), (int)(y - 14 * s), (int)(4 * s), (int)(28 * s), ga_dark(GA_GOLD, 40));
    ga_fill(c, (int)(x + 13 * s), (int)(y - 14 * s), (int)(4 * s), (int)(28 * s), ga_dark(GA_GOLD, 40));
    ga_fill(c, (int)(x - 4 * s), (int)(y - 4 * s), (int)(8 * s), (int)(9 * s), GA_GOLD);
    ga_fill(c, (int)(x - 1 * s), (int)(y - 1 * s), (int)(2 * s) + 1, (int)(4 * s), 0x3a2410);
    ga_pearl(c, x - 30 * s, y + 10 * s, 4 * s, GA_GOLD);
    ga_pearl(c, x + 29 * s, y + 11 * s, 3.5f * s, GA_GOLD);
    ga_pearl(c, x - 6 * s, y - 15 * s, 3 * s, GA_PEARL);
}
/* fishing line from (x, y0) to a hook whose bend sits at (x, y1) */
static void ga_hook(render_ctx_t *c, int x, int y0, int y1) {
    ga_fill(c, x, y0, 2, y1 - y0 + 1, 0xd5e6e8);
    ga_ell(c, x + 1, y0 + 10, 5, 6, 0xe9e2d8);
    ga_ell(c, x + 1, y0 + 12, 5, 3, 0xd94f43);
    ga_trig();
    for (int i = 0; i <= 8; i += 2)              /* the bend: dots round the lower half */
        ga_ell(c, x - 5 + g_ga_cos[i] * 6, y1 + g_ga_sin[i] * 6, 1.8f, 1.8f, 0xb7c3c6);
    ga_tri(c, x - 11, y1 + 1, x - 10, y1 - 7, x - 7, y1 - 2, 0xb7c3c6);
}
static void ga_crab(render_ctx_t *c, float x, float y, bool armored) {
    uint32_t shell = armored ? 0x9a62b8 : 0xe4654f, dark = ga_dark(shell, 110);
    for (int side = -1; side <= 1; side += 2) {
        for (int i = 0; i < 3; i++)
            ga_quad(c, x + side * 9, y + 1 + i * 3, x + side * 9, y + 3 + i * 3,
                    x + side * (19 + i * 2), y + 7 + i * 4, x + side * (19 + i * 2), y + 5 + i * 4, dark, 255);
        ga_quad(c, x + side * 10, y - 2, x + side * 12, y + 1, x + side * 17, y - 7, x + side * 15, y - 9, dark, 255);
        ga_ell(c, x + side * 18, y - 10, 5.5f, 4.5f, shell);
        ga_tri(c, x + side * 16, y - 15, x + side * 22, y - 16, x + side * 19, y - 10, ga_dark(shell, 30));
        ga_ell(c, x + side * 17, y - 11, 2, 1.6f, ga_lite(shell, 90));
    }
    ga_ell(c, x, y + 1, 14, 9, dark);
    ga_ell(c, x, y, 13, 8, shell);
    ga_ell(c, x - 3, y - 3, 7, 3.5f, ga_lite(shell, 80));
    if (armored) for (int i = -2; i <= 2; i++) ga_tri(c, x + i * 5 - 2, y - 5, x + i * 5 + 2, y - 5, x + i * 5, y - 11, 0xe0c6ef);
    for (int side = -1; side <= 1; side += 2) {
        ga_fill(c, (int)x + side * 4 - 1, (int)y - 12, 2, 6, dark);
        ga_ell(c, x + side * 4, y - 13, 2.6f, 2.6f, 0xffffff);
        ga_ell(c, x + side * 4, y - 13, 1.2f, 1.4f, MSP_INK);
    }
}
/* a little yellow submarine facing right */
static void ga_sub(render_ctx_t *c, float x, float y, float s) {
    ga_ell(c, x - 15 * s, y, 3 * s, 7 * s, 0x6f7a7c);
    ga_ell(c, x, y + 1 * s, 15 * s, 8 * s, ga_dark(GA_GOLD, 110));
    ga_ell(c, x, y, 14 * s, 7 * s, 0xf3c94b);
    ga_ell(c, x - 3 * s, y - 3 * s, 8 * s, 2.5f * s, 0xfff0b0);
    ga_fill(c, (int)(x - 3 * s), (int)(y - 12 * s), (int)(8 * s), (int)(6 * s), 0xd8a93a);
    ga_fill(c, (int)(x + 1 * s), (int)(y - 16 * s), (int)(2 * s) + 1, (int)(5 * s), 0x6f7a7c);
    ga_ell(c, x + 6 * s, y, 3.4f * s, 3.4f * s, 0x6f7a7c);
    ga_ell(c, x + 6 * s, y, 2.4f * s, 2.4f * s, 0x9fe6f4);
}
/* a lantern on a stone tile; lit lanterns glow */
static void ga_lantern(render_ctx_t *c, int x, int y, int w, int h, bool lit) {
    ga_block(c, x, y, w, h, lit ? 0x4b5a4f : 0x24414b);
    float cx = x + w / 2.0f - 1, cy = y + h / 2.0f;
    if (lit) render_internal_ellipse(c, cx, cy, w * 0.46f, h * 0.48f, 0xffd77a, 70);
    ga_fill(c, (int)cx - 8, (int)cy - 13, 16, 4, 0x1b2a2f);
    ga_fill(c, (int)cx - 3, (int)cy - 16, 6, 3, 0x1b2a2f);
    ga_ell(c, cx, cy, 7, 9, lit ? 0xffcf56 : 0x2c4852);
    ga_ell(c, cx, cy + 1, 4, 5, lit ? 0xfff6d2 : 0x37545e);
    ga_fill(c, (int)cx - 1, (int)cy - 9, 2, 18, lit ? 0xd8a43a : 0x1b2a2f);
    ga_fill(c, (int)cx - 8, (int)cy + 9, 16, 4, 0x1b2a2f);
}
/* a scallop shell, hinge at the bottom */
static void ga_shell(render_ctx_t *c, float x, float y, float s, uint32_t tint) {
    float xs[11], ys[11];
    ga_trig();
    for (int pass = 0; pass < 2; pass++) {
        float g = pass ? 1.0f : 1.08f, oy = pass ? 0 : 1.2f * s;
        for (int i = 0; i < 9; i++) {
            xs[i] = x + g_ga_cos[8 + i] * 16 * s * g; ys[i] = y + 2 * s + oy + g_ga_sin[8 + i] * 14 * s * g;
        }
        xs[9] = x + 5 * s; ys[9] = y + 9 * s + oy; xs[10] = x - 5 * s; ys[10] = y + 9 * s + oy;
        render_internal_poly(c, xs, ys, 11, pass ? tint : ga_dark(tint, 110), 255);
    }
    for (int i = -2; i <= 2; i++)
        ga_quad(c, x + i * 1.2f * s - 0.8f, y + 7 * s, x + i * 1.2f * s + 0.8f, y + 7 * s,
                x + i * 6 * s + 1, y - 9 * s, x + i * 6 * s - 1, y - 9 * s, ga_dark(tint, 70), 255);
    ga_ell(c, x - 5 * s, y - 6 * s, 4 * s, 2 * s, ga_lite(tint, 110));
    ga_quad(c, x - 7 * s, y + 6 * s, x + 7 * s, y + 6 * s, x + 5 * s, y + 11 * s, x - 5 * s, y + 11 * s, ga_dark(tint, 40), 255);
}
static void ga_food(render_ctx_t *c, float x, float y, int kind) {
    if (kind == 0) {
        static const float fx[] = {-7, 3, -1, 7}, fy[] = {-3, -6, 4, 3};
        for (int i = 0; i < 4; i++)
            ga_quad(c, x + fx[i] - 4, y + fy[i] - 1, x + fx[i] + 2, y + fy[i] - 4, x + fx[i] + 4, y + fy[i] + 2,
                    x + fx[i] - 2, y + fy[i] + 3, i & 1 ? 0xf08a4b : 0xe4c35a, 255);
    } else if (kind == 1) {
        for (int i = 0; i < 6; i++) ga_ell(c, x - 10 + i * 4, y + (i & 1 ? -2 : 2), 3.2f, 2.8f, i == 5 ? 0xf2a2b4 : 0xd9707f);
        ga_ell(c, x + 10, y - 2, 1, 1, MSP_INK);
    } else {
        ga_quad(c, x - 2, y + 7, x + 1, y + 7, x - 8, y - 8, x - 11, y - 6, 0x4f9550, 255);
        ga_ell(c, x - 5, y - 2, 6, 3.4f, 0x6fb35c);
        ga_ell(c, x + 5, y - 1, 7, 4, 0x5aa253);
        ga_fill(c, (int)x - 1, (int)y - 1, 11, 1, 0x9fd27c);
    }
}
/* a bronze pipe run from (x0,y0) of length/size along one axis; wet carries water */
static void ga_pipe(render_ctx_t *c, int x, int y, int w, int h, bool wet) {
    ga_fill(c, x, y, w, h, 0x5a3c22);
    bool across = w > h;
    if (across) {
        ga_fill(c, x, y + 2, w, h - 4, 0xb07a40);
        ga_fill(c, x, y + 3, w, 2, 0xe2b06c);
        ga_fill(c, x, y + h / 2 - 1, w, 3, wet ? 0x73e5ff : 0x3a2818);
    } else {
        ga_fill(c, x + 2, y, w - 4, h, 0xb07a40);
        ga_fill(c, x + 3, y, 2, h, 0xe2b06c);
        ga_fill(c, x + w / 2 - 1, y, 3, h, wet ? 0x73e5ff : 0x3a2818);
    }
}
static void ga_flag(render_ctx_t *c, float x, float y, float s) {
    ga_ell(c, x, y + 12 * s, 8 * s, 2.5f * s, ga_dark(GA_SAND, 100));
    ga_fill(c, (int)(x - 1), (int)(y - 14 * s), 2, (int)(26 * s), 0xd8d0c0);
    ga_quad(c, x + 1, y - 14 * s, x + 14 * s, y - 10 * s, x + 13 * s, y - 4 * s, x + 1, y - 5 * s, GA_GOLD, 255);
    ga_fill(c, (int)(x + 4 * s), (int)(y - 10 * s), (int)(5 * s), (int)(2 * s), 0xd25a4a);
}
static void ga_key(render_ctx_t *c, float x, float y, float s) {
    render_internal_annulus(c, x - 8 * s, y, 6 * s, 3 * s, GA_GOLD, 255);
    ga_fill(c, (int)(x - 3 * s), (int)(y - 1.5f * s), (int)(17 * s), (int)(3 * s) + 1, GA_GOLD);
    ga_fill(c, (int)(x + 9 * s), (int)(y + 1 * s), (int)(3 * s), (int)(5 * s), GA_GOLD);
    ga_fill(c, (int)(x + 4 * s), (int)(y + 1 * s), (int)(3 * s), (int)(4 * s), GA_GOLD);
    ga_ell(c, x - 10 * s, y - 2 * s, 1.6f * s, 1.4f * s, 0xfff4cf);
}
static void ga_crate(render_ctx_t *c, float x, float y, float s) {
    int w = (int)(28 * s), h = (int)(22 * s), x0 = (int)(x - w / 2), y0 = (int)(y - h / 2);
    ga_block(c, x0, y0, w, h, 0x9a6a3c);
    ga_fill(c, x0 + 2, y0 + h / 2 - 1, w - 4, 2, 0x5e3c1e);
    ga_quad(c, x0 + 3, y0 + 3, x0 + 6, y0 + 3, x0 + w - 3, y0 + h - 4, x0 + w - 6, y0 + h - 4, 0x5e3c1e, 255);
}
/* sea-glass tile colours for 2048 values 2, 4, 8 ... 1024 */
static const uint32_t g_ga_glass[11] = {
    0x0b2630, 0x9fd8e2, 0x7fd3b8, 0xf2c45a, 0xf09a5a, 0xe2786a, 0xd8608e, 0xb27ae0, 0x7f8cf0, 0x5ac3f0, 0xfff0b0
};
static void game_practice_render(render_ctx_t *c) {
    char text[64];
    render_internal_button(c, 280, 54, 144, 32, 0x1c2f36, MSP_TEAL, "FINISH", 1);
    if (g_game.page == GAME_LIGHTHOUSE || g_game.page == GAME_SLIDE || g_game.page == GAME_MERGE) {
        snprintf(text, sizeof text, "MOVES %d", g_game.moves);
        ga_hud(c, 24, 62, 2, MSP_TEAL, text);
        int side = g_game.page == GAME_SLIDE ? 3 : 4;
        int width = side == 3 ? 64 : 56, height = side == 3 ? 64 : 48;
        int start = side == 3 ? 128 : 112;
        ga_block(c, start - 8, 92, side * width + 12, side * height + 12, 0x1d3941);
        for (int i = 0; i < side * side; i++) {
            int x = start + i % side * width, y = 100 + i / side * height, value = g_game.board[i];
            int w = width - 6, h = height - 6;
            if (g_game.page == GAME_LIGHTHOUSE) { ga_lantern(c, x, y, w, h, value); continue; }
            if (!value) { ga_socket(c, x, y, w, h, 0x0a232c); continue; }
            uint32_t base = g_game.page == GAME_SLIDE ? 0xd8c094 : g_ga_glass[value > 10 ? 10 : value];
            ga_block(c, x, y, w, h, base);
            if (g_game.page == GAME_SLIDE) {
                ga_speckle(c, x, y, w, h, (unsigned)value * 7u, ga_dark(base, 40));
                ga_shell(c, x + 11, y + 11, 0.3f, 0xe9a48c);
            } else render_internal_ellipse(c, x + 12, y + 9, 7, 3, 0xffffff, 120);
            snprintf(text, sizeof text, "%d", g_game.page == GAME_MERGE ? 1 << value : value);
            render_internal_text(c, x + (w - render_text_w(text, 2)) / 2, y + (h - 14) / 2, 2, MSP_INK, text);
        }
        if (g_game.page == GAME_MERGE) {
            static const char *const arrows[] = {"< LEFT", "RIGHT >", "UP", "DOWN"};
            for (int i = 0; i < 4; i++) render_internal_button(c, 24 + i * 100, 298, 94, 32, 0x1c2f36, MSP_TEAL, arrows[i], 1);
        } else ga_hud(c, 48, 306, 1, MSP_TEAL, g_game.page == GAME_LIGHTHOUSE ?
                      "TOGGLE CROSSES. ALL DARK WINS." : "ORDER 1 TO 8; GAP AT THE END.");
    } else if (g_game.page == GAME_SONAR) {
        snprintf(text, sizeof text, "PEARLS %d/5  LEFT %d", g_game.pairs, g_game.remaining);
        ga_hud(c, 24, 62, 1, MSP_TEAL, text);
        render_internal_annulus(c, 50, 262, 34, 32, 0x9fe6f4, 70);
        render_internal_annulus(c, 50, 262, 24, 22, 0x9fe6f4, 110);
        ga_sub(c, 50, 264, 0.9f);
        ga_block(c, 108, 106, 232, 192, 0x1a3a44);
        for (int line = 0; line < 5; line++) {
            int row = 0, col = 0;
            for (int i = 0; i < 5; i++) {
                row += g_game.board[line * 5 + i] == 1 || g_game.board[line * 5 + i] == 2;
                col += g_game.board[i * 5 + line] == 1 || g_game.board[i * 5 + line] == 2;
            }
            ga_ell(c, 94, 127 + line * 36, 11, 11, 0x06222b);
            ga_ell(c, 134 + line * 44, 97, 11, 11, 0x06222b);
            snprintf(text, sizeof text, "%d", row);
            render_internal_text(c, 88, 120 + line * 36, 2, 0xffdc73, text);
            snprintf(text, sizeof text, "%d", col);
            render_internal_text(c, 128 + line * 44, 90, 2, 0xffdc73, text);
        }
        for (int i = 0; i < 25; i++) {
            int x = 114 + i % 5 * 44, y = 112 + i / 5 * 36, value = g_game.board[i];
            if (value == 2) {
                ga_socket(c, x, y, 38, 30, 0x184a44);
                ga_ell(c, x + 19, y + 20, 15, 7, 0x6b5a7a);
                ga_ell(c, x + 19, y + 19, 12, 5, 0x2a1e30);
                ga_pearl(c, x + 19, y + 15, 6, GA_PEARL);
            } else if (value == 3) {
                ga_socket(c, x, y, 38, 30, 0x0d2a33);
                ga_ell(c, x + 19, y + 17, 6, 3, 0x2f4f59);
                render_internal_text(c, x + 14, y + 7, 2, 0x5f8a96, "X");
            } else {
                ga_block(c, x, y, 38, 30, 0x2b5560);
                ga_speckle(c, x, y, 38, 30, (unsigned)i * 13u + 5u, 0x406d78);
            }
        }
        ga_hud(c, 48, 306, 1, MSP_TEAL, "GOLD: PEARLS IN EACH ROW/COLUMN.");
    } else if (g_game.page == GAME_STACK) {
        snprintf(text, sizeof text, "LAYERS %d/12  WIDTH %d", g_game.rounds, g_game.remaining);
        ga_hud(c, 24, 88, 2, MSP_TEAL, text);
        int top_l = g_game.board[g_game.rounds * 2], top_r = g_game.board[g_game.rounds * 2 + 1];
        int guide = 268 - g_game.rounds * 14;
        if (guide > 112 && top_r > top_l) {
            render_internal_rect_blend(c, 24 + top_l * 20, 112, 2, guide - 112, 0xd8f4f2, 60);
            render_internal_rect_blend(c, 22 + top_r * 20, 112, 2, guide - 112, 0xd8f4f2, 60);
        }
        for (int i = 0; i <= g_game.rounds; i++) {
            int left = g_game.board[i * 2], right = g_game.board[i * 2 + 1];
            if (right <= left) continue;
            uint32_t base = i % 2 ? 0x7fc6cf : 0xd9b98a;
            ga_block(c, 24 + left * 20, 282 - i * 14, (right - left) * 20, 12, base);
            ga_fill(c, 30 + left * 20, 288 - i * 14, (right - left) * 20 - 14, 1, ga_dark(base, 50));
        }
        int mx = 24 + (int)roundf(g_game.px) * 20, my = 268 - g_game.rounds * 14;
        ga_block(c, mx, my, g_game.remaining * 20, 12, GA_GOLD);
        render_internal_ellipse(c, mx + 12, my + 3, 7, 2, 0xffffff, 170);
        render_internal_button(c, 24, 298, 400, 32, 0x1c2f36, MSP_TEAL, "DROP - KEEP THE OVERLAP", 2);
    } else if (g_game.page == GAME_ECHO) {
        snprintf(text, sizeof text, "%s  LENGTH %d/12", g_game.launched ? "WATCH" : "REPEAT", g_game.rounds);
        ga_hud(c, 24, 88, 2, MSP_TEAL, text);
        float beat = g_game_relaxed ? 1.15f : 0.8f;
        int step = (int)(g_game.travel / beat);
        int lit = g_game.launched && step < g_game.rounds &&
            g_game.travel - step * beat < beat * 0.65f ? g_game.cards[step] : -1;
        static const uint32_t colors[] = {0xffdc73, 0x73e5ff, 0xffa3cf, 0x89ffb1};
        for (int i = 0; i < 4; i++) {
            int x = 64 + i % 2 * 160, y = 110 + i / 2 * 90;
            bool on = i == lit;
            ga_block(c, x, y, 148, 78, on ? ga_mix(colors[i], 0x0b2630, 70) : ga_mix(colors[i], 0x0b2630, 205));
            if (on) render_internal_ellipse(c, x + 52, y + 38, 44, 32, 0xffffff, 90);
            ga_shell(c, x + 52, y + 40, 1.3f, on ? ga_lite(colors[i], 120) : ga_dark(colors[i], 40));
            render_internal_rect_edge(c, x, y, 148, 78, colors[i]);
            snprintf(text, sizeof text, "%d", i + 1);
            render_internal_text(c, x + 100, y + 28, 3, on ? MSP_INK : colors[i], text);
        }
        snprintf(text, sizeof text, g_game.launched ? "WATCH THE FLASHES; THEN COPY." : "YOUR TURN: %d OF %d", g_game.moves, g_game.rounds);
        ga_hud(c, 64, 306, 1, MSP_TEAL, text);
    }
}
static void ga_board_cell(render_ctx_t *c, int x, int y, int tile, bool tactics) {
    ga_socket(c, x, y, 46, 36, tactics ? 0x1f4954 : 0x2c5148);
    if (tile == 3) ga_rock(c, x + 23, y + 20, 40, 28, 0x5b6a6c);
    else if (tile == 4) ga_flag(c, x + 18, y + 19, 1.0f);
    else if (tile == 2 && tactics) ga_crab(c, x + 23, y + 22, false);
    else if (tile == 2) ga_grass(c, x + 23, y + 34, 1.0f);
    else if (tile == 1) ga_coral(c, x + 23, y + 34, 1.15f, 0xe2786a);
}
static void game_extra_render(render_ctx_t *c) {
    char text[80];
    if (g_game.page == GAME_DEFENDER) {
        snprintf(text, sizeof text, "WAVE %d/5  REEF %d  SCORE %d", g_game.wave, g_game.hp, g_game.score);
        ga_hud(c, 24, 88, 2, MSP_TEAL, text);
        static const uint32_t reef[] = {0xe2786a, 0xf0a060, 0xd8608e};
        ga_ell(c, 224, 312, 204, 9, 0x3b4f4a);
        for (int i = 0; i < 12; i++) ga_coral(c, 42 + i * 33 + (i > 5 ? 12 : -12), 312, 0.75f, reef[i % 3]);
        ga_ell(c, 224, 305, 21, 9, 0x6a4f7a);
        ga_ell(c, 224, 302, 19, 7, 0xb08cc0);
        for (int i = -2; i <= 2; i++) ga_fill(c, 223 + i * 7, 298, 2, 9, 0x8a6a9a);
        ga_pearl(c, 224, 294, 6, GA_PEARL);
        for (int i = 0; i < 8; i++) {
            if (g_game.foes[i].hp) ga_crab(c, g_game.foes[i].x, g_game.foes[i].y, g_game.foes[i].hp > 1);
            if (g_game.shots[i].hp) ga_pearl(c, g_game.shots[i].x, g_game.shots[i].y, 4.5f, 0xc8f3ff);
        }
        ga_hud(c, 60, 320, 2, MSP_TEAL, "TAP THE WATER TO AIM AND FIRE");
    } else if (g_game.page == GAME_RUNNER) {
        snprintf(text, sizeof text, "GATES %d/18  HULL %d  SCORE %d", g_game.rounds, g_game.hp, g_game.score);
        ga_hud(c, 24, 88, 2, MSP_TEAL, text);
        for (int i = 0; i < 5; i++) {
            int x = 24 + i * 80;
            ga_wash(c, x, 110, 76, 180, 0x041820, 105);
            render_internal_rect_blend(c, x, 110, 2, 180, 0x9fd8e2, 50);
            int y = 110 + (int)g_game.travel;
            if (i != g_game.target && i != (g_game.target + 1) % 5) {
                ga_rock(c, x + 22, y + 8, 40, 18, 0x56676b);
                ga_rock(c, x + 52, y + 7, 36, 16, 0x4c5c60);
            } else if (i == g_game.target) {
                render_internal_ellipse(c, x + 38, y + 7, 14, 14, 0xffd77a, 50);
                ga_ell(c, x + 39, y + 8, 8, 8, ga_dark(GA_GOLD, 110));
                ga_ell(c, x + 38, y + 7, 7.5f, 7.5f, GA_GOLD);
                render_internal_annulus(c, x + 38, y + 7, 5, 3.5f, 0xb8862e, 255);
                ga_ell(c, x + 35, y + 4, 2, 1.6f, 0xfff4cf);
            }
        }
        ga_sub(c, 61 + g_game.lane * 80, 283, 1.0f);
        ga_hud(c, 48, 308, 2, MSP_TEAL, "TAP A LANE - GOLD IS 3 POINTS");
    } else if (g_game.page == GAME_BREAKER) {
        render_internal_button(c, 280, 54, 144, 32, 0x1c2f36, MSP_TEAL, "BANK SCORE", 1);
        snprintf(text, sizeof text, "BALLS %d  SCORE %d", g_game.hp, g_game.score);
        ga_hud(c, 24, 88, 2, MSP_TEAL, text);
        for (int i = 0; i < 24; i++) if (g_game.board[i]) {
            int x = 30 + i % 6 * 65, y = 108 + i / 6 * 24;
            bool tough = g_game.board[i] == 2;
            uint32_t base = tough ? 0xe0895a : i / 6 % 2 ? 0x5fbccc : 0x6fd0c4;
            ga_block(c, x, y, 58, 18, base);
            if (tough) { ga_fill(c, x + 8, y + 8, 22, 2, GA_GOLD); ga_fill(c, x + 30, y + 10, 16, 2, GA_GOLD); }
            else ga_fill(c, x + 30, y + 7, 18, 2, ga_lite(base, 70));
        }
        int px = (int)g_game.px;
        ga_ell(c, px - 40, 288, 6, 5, 0x5e3c1e);
        ga_ell(c, px + 40, 288, 6, 5, 0x5e3c1e);
        ga_fill(c, px - 40, 284, 80, 9, 0x8a5e36);
        ga_fill(c, px - 36, 285, 62, 2, 0xb8885a);
        ga_fill(c, px - 18, 289, 48, 1, 0x5e3c1e);
        ga_pearl(c, g_game.bx, g_game.by, 6, GA_PEARL);
        ga_hud(c, 40, 310, 2, MSP_TEAL, g_game.launched ? "TAP BELOW BRICKS TO MOVE PADDLE" : "TAP BELOW BRICKS TO LAUNCH");
    } else if (g_game.page == GAME_TACTICS || g_game.page == GAME_ARCHITECT) {
        bool tactics = g_game.page == GAME_TACTICS;
        if (tactics) snprintf(text, sizeof text, "HP %d  TURN %d/22  FLAGS %d/3", g_game.hp, g_game.moves, g_game.pairs);
        else snprintf(text, sizeof text, "PLACED %d/12  HABITAT SCORE %d", g_game.moves, g_game.score);
        ga_hud(c, 24, 66, 2, MSP_TEAL, text);
        ga_block(c, 93, 86, 260, 210, 0x16323b);
        for (int i = 0; i < 25; i++) {
            int x = 99 + i % 5 * 50, y = 92 + i / 5 * 40, tile = g_game.board[i];
            ga_board_cell(c, x, y, tile, tactics);
            if (tactics && tile == 2 && game_distance(i, g_game.selected) == 1) {
                render_internal_rect_edge(c, x, y, 46, 36, 0xffdc73);
                render_internal_rect_edge(c, x + 1, y + 1, 44, 34, 0xffdc73);
            }
            if (tactics && i == g_game.selected) {
                render_internal_annulus(c, x + 23, y + 18, 18, 16, MSP_TEAL, 200);
                ga_sub(c, x + 24, y + 22, 0.85f);
            }
        }
        if (tactics) render_internal_button(c, 24, 298, 400, 30, 0x1c2f36, MSP_TEAL, "GUARD: BLOCK ATTACKS / HEAL 1", 2);
        else {
            render_internal_button(c, 24, 298, 126, 30, 0x1c2f36, g_game.selected == 1 ? 0xffdc73 : MSP_TEAL, "CORAL", 2);
            render_internal_button(c, 160, 298, 126, 30, 0x1c2f36, g_game.selected == 2 ? 0xffdc73 : MSP_TEAL, "GRASS", 2);
            render_internal_button(c, 296, 298, 128, 30, 0x1c2f36, MSP_TEAL, "FINISH", 2);
        }
    } else {
        static const char *const routes[] = {"SUPPLIES", "KEY CACHE", "DARK TUNNEL", "LOCKED RUIN", "REST REEF"};
        static const char *const costs[] = {"O2 -2", "O2 -2", "O2 -4", "O2 -2", "FOOD -1"};
        static const char *const costs2[] = {"", "", "HP -2", "KEY -1", "O2 +3"};
        static const char *const loot[] = {"+FOOD 2PT", "+KEY 2PT", "+9 POINTS", "+8 POINTS", "+1 HP"};
        snprintf(text, sizeof text, "DEPTH %d/8  O2 %d  HP %d", g_game.depth, g_game.oxygen, g_game.hp);
        ga_hud(c, 24, 84, 2, MSP_TEAL, text);
        snprintf(text, sizeof text, "KEYS %d  FOOD %d  LOOT %d", g_game.keys, g_game.supplies, g_game.score);
        ga_hud(c, 24, 110, 2, MSP_TEAL, text);
        for (int i = 0; i < 3; i++) {
            int route = g_game.choices[i], x = 24 + i * 136;
            bool locked = (route == 3 && !g_game.keys) || (route == 4 && !g_game.supplies);
            float cx = x + 64;
            ga_block(c, x, 138, 128, 140, locked ? 0x29343a : 0x1d3d47);
            ga_socket(c, x + 6, 144, 114, 56, 0x0b2a35);
            ga_ell(c, cx, 197, 50, 5, ga_dark(GA_SAND, 90));
            if (route == 0) {
                ga_crate(c, cx - 8, 184, 1.1f);
                ga_crate(c, cx + 20, 189, 0.7f);
            } else if (route == 1) {
                ga_shell(c, cx - 22, 186, 0.6f, 0xd9a0c8);
                ga_key(c, cx + 8, 178, 1.4f);
            } else if (route == 2) {
                ga_ell(c, cx, 180, 42, 18, 0x3b4a4e);
                ga_ell(c, cx - 14, 170, 14, 6, 0x56676b);
                ga_ell(c, cx, 188, 23, 11, 0x020a0d);
                ga_ell(c, cx - 6, 186, 2.2f, 1.5f, 0xffdc73);
                ga_ell(c, cx + 6, 186, 2.2f, 1.5f, 0xffdc73);
            } else if (route == 3) {
                ga_block(c, (int)cx - 30, 154, 60, 8, 0x8a9a96);
                ga_block(c, (int)cx - 26, 162, 12, 34, 0x7a8a86);
                ga_block(c, (int)cx + 14, 162, 12, 34, 0x7a8a86);
                ga_fill(c, (int)cx - 14, 166, 28, 30, 0x07181e);
                render_internal_annulus(c, cx, 177, 6, 3.5f, 0xb7c3c6, 255);
                ga_block(c, (int)cx - 8, 179, 16, 13, locked ? 0x8a8070 : GA_GOLD);
            } else {
                ga_coral(c, cx - 26, 196, 1.0f, 0xe2786a);
                ga_grass(c, cx + 28, 196, 1.0f);
                for (int k = 0; k < 7; k++) ga_ell(c, cx - 9 + k * 3, 182 + (k & 1) * 2, 2, 7, 0xf2a2c4);
                ga_ell(c, cx, 192, 12, 5, 0xc0607e);
            }
            render_internal_text(c, x + 6, 208, 1, locked ? 0x8b6570 : 0xffdc73, routes[route]);
            render_internal_text(c, x + 6, 224, 1, MSP_TEAL, costs[route]);
            render_internal_text(c, x + 6, 242, 1, MSP_TEAL, costs2[route]);
            render_internal_text(c, x + 6, 260, 1, locked ? 0x8b6570 : 0xffffff, locked ? "NEEDS ITEM" : loot[route]);
        }
        render_internal_button(c, 24, 298, 400, 30, 0x1c2f36, MSP_TEAL, "RETURN: BANK LOOT SAFELY", 2);
    }
}
/* Fixed 40x32 previews: miniatures of each playfield's own props. */
GAME_LOCAL void game_thumb(render_ctx_t *c, int x, int y, int game) {
    ga_fill(c, x, y, 40, 32, 0x0a2a34);
    ga_fill(c, x + 1, y + 1, 38, 9, 0x1a5565);
    ga_fill(c, x + 1, y + 10, 38, 9, 0x134654);
    ga_fill(c, x + 1, y + 19, 38, 8, 0x0e3846);
    ga_fill(c, x + 1, y + 27, 38, 4, ga_dark(GA_SAND, 60));
    ga_fill(c, x + 1, y + 27, 38, 1, GA_SAND);
    float X = (float)x, Y = (float)y;
    if (game == 0) {
        ga_bubble(c, X + 13, Y + 17, 8, 0x73e5ff);
        ga_bubble(c, X + 29, Y + 9, 6, GA_GOLD);
    } else if (game == 1) {
        ga_block(c, x + 3, y + 3, 15, 22, 0x2a6070);
        ga_shell(c, X + 10, Y + 13, 0.3f, GA_GOLD);
        ga_socket(c, x + 21, y + 3, 15, 22, 0x14333d);
        ga_pearl(c, X + 28, Y + 14, 4, 0xffa3cf);
    } else if (game == 2) {
        ga_ell(c, X + 6, Y + 8, 3, 3, GA_GOLD);
        ga_ell(c, X + 34, Y + 8, 3, 3, GA_GOLD);
        ga_fill(c, x + 9, y + 8, 22, 1, 0xd5e6e8);
        render_internal_page_stamp(c, x + 8, y + 12, STAMP_FISH, MSP_TEAL);
    } else if (game == 3) {
        ga_chest(c, X + 18, Y + 23, 0.32f);
        ga_fill(c, x + 30, y + 1, 1, 14, 0xd5e6e8);
        ga_ell(c, X + 27, Y + 16, 2, 2, 0xb7c3c6);
    } else if (game == 4) {
        render_internal_page_stamp(c, x + 1, y + 1, STAMP_FISH, MSP_TEAL);
        ga_food(c, X + 28, Y + 21, 0);
    } else if (game == 5) {
        ga_pipe(c, x + 2, y + 5, 20, 9, true);
        ga_pipe(c, x + 15, y + 5, 9, 20, true);
        ga_pipe(c, x + 15, y + 17, 23, 9, true);
    } else if (game == 6) {
        ga_ell(c, X + 12, Y + 9, 7, 4.5f, 0xe4654f);
        ga_ell(c, X + 11, Y + 8, 3, 1.5f, 0xf6a090);
        ga_ell(c, X + 30, Y + 7, 6, 4, 0x9a62b8);
        ga_pearl(c, X + 21, Y + 16, 2.5f, 0xc8f3ff);
        ga_ell(c, X + 20, Y + 25, 8, 3.5f, 0xb08cc0);
        ga_pearl(c, X + 20, Y + 22, 2.5f, GA_PEARL);
    } else if (game == 7) {
        ga_rock(c, X + 8, Y + 9, 14, 8, 0x56676b);
        ga_rock(c, X + 32, Y + 9, 14, 8, 0x56676b);
        ga_ell(c, X + 20, Y + 9, 3.5f, 3.5f, GA_GOLD);
        ga_sub(c, X + 19, Y + 22, 0.5f);
    } else if (game == 8) {
        for (int i = 0; i < 8; i++)
            ga_block(c, x + 2 + i % 4 * 9, y + 3 + i / 4 * 7, 8, 6, i < 4 ? 0xe0895a : 0x5fbccc);
        ga_pearl(c, X + 23, Y + 20, 2.5f, GA_PEARL);
        ga_fill(c, x + 11, y + 25, 18, 3, 0x8a5e36);
    } else if (game == 11) {
        ga_ell(c, X + 20, Y + 19, 16, 10, 0x3b4a4e);
        ga_ell(c, X + 20, Y + 22, 9, 6, 0x020a0d);
        ga_ell(c, X + 17, Y + 21, 1.2f, 1, 0xffdc73);
        ga_ell(c, X + 23, Y + 21, 1.2f, 1, 0xffdc73);
        ga_key(c, X + 21, Y + 6, 0.55f);
    } else if (game == 12) {
        for (int i = 0; i < 9; i++) {
            float lx = X + 8 + i % 3 * 12, ly = Y + 6 + i / 3 * 9;
            bool lit = i == 1 || i == 3 || i == 4 || i == 8;
            if (lit) render_internal_ellipse(c, lx, ly, 6, 5, 0xffd77a, 80);
            ga_ell(c, lx, ly, 2.6f, 3, lit ? 0xffcf56 : 0x2c4852);
        }
    } else if (game == 13) {
        for (int i = 0; i < 8; i++) {
            int xx = x + 4 + i % 3 * 11, yy = y + 1 + i / 3 * 9;
            ga_block(c, xx, yy, 10, 8, 0xd8c094);
            ga_fill(c, xx + 4, yy + 2, 2, 3, 0x5a4a30);
        }
    } else if (game == 14) {
        render_internal_annulus(c, X + 20, Y + 15, 13, 11.5f, 0x73e5ff, 150);
        render_internal_annulus(c, X + 20, Y + 15, 7, 5.5f, 0x73e5ff, 200);
        ga_pearl(c, X + 28, Y + 21, 3, GA_PEARL);
        ga_ell(c, X + 20, Y + 15, 1.6f, 1.6f, 0xe7f3ee);
    } else if (game == 15) {
        ga_block(c, x + 2, y + 6, 11, 17, g_ga_glass[1]);
        ga_block(c, x + 15, y + 6, 11, 17, g_ga_glass[1]);
        ga_block(c, x + 28, y + 4, 10, 21, g_ga_glass[2]);
        render_internal_text(c, x + 5, y + 11, 1, MSP_INK, "2");
        render_internal_text(c, x + 18, y + 11, 1, MSP_INK, "2");
        render_internal_text(c, x + 31, y + 11, 1, MSP_INK, "4");
    } else if (game == 16) {
        for (int i = 0; i < 4; i++)
            ga_block(c, x + 3 + i * 4, y + 21 - i * 6, 30 - i * 5, 6, i == 3 ? GA_GOLD : i % 2 ? 0x7fc6cf : 0xd9b98a);
    } else if (game == 17) {
        static const uint32_t colors[] = {0xffdc73, 0x73e5ff, 0xffa3cf, 0x89ffb1};
        for (int i = 0; i < 4; i++) ga_shell(c, X + 11 + i % 2 * 18, Y + 9 + i / 2 * 12, 0.32f, colors[i]);
    } else {
        for (int i = 0; i < 9; i++) {
            int xx = x + 5 + i % 3 * 10, yy = y + 1 + i / 3 * 9;
            ga_socket(c, xx, yy, 9, 8, game == 9 ? 0x1f4954 : 0x2c5148);
        }
        if (game == 9) {
            ga_flag(c, X + 28, Y + 6, 0.35f);
            ga_ell(c, X + 30, Y + 23, 3.5f, 2.5f, 0xe4654f);
            ga_sub(c, X + 10, Y + 14, 0.32f);
        } else {
            ga_coral(c, X + 10, Y + 9, 0.38f, 0xe2786a);
            ga_grass(c, X + 30, Y + 18, 0.4f);
            ga_coral(c, X + 20, Y + 27, 0.38f, 0xd8608e);
        }
    }
}
static void casino_symbol(render_ctx_t *c, const tank_t *t, uint16_t *fb, int stride, int cx, int cy, int sym, float clock) {
    if (sym == 0) ga_shell(c, (float)cx, (float)cy + 6, 1.4f, 0xf3c7a8);
    else if (sym == 1) {
        float xs[10], ys[10];
        for (int k = 0; k < 10; k++) {
            float a = -1.5708f + (float)k * 0.6283f, r = k & 1 ? 6.5f : 15.0f;
            xs[k] = (float)cx + cosf(a) * r; ys[k] = (float)cy + 1 + sinf(a) * r;
        }
        render_internal_poly(c, xs, ys, 10, 0xffd75e, 255);
        render_internal_ellipse(c, (float)cx - 3, (float)cy - 2, 2.5f, 2.5f, 0xfff6c8, 255);
    }
    else if (sym == 2) {
        fish_t f = {0}; progression_market_preview(t, FISH_TETRA, &f);
        render_fish_portrait(fb, stride, (float)cx, (float)cy, 0.7f, &f, clock);
    } else if (sym == 3) ga_pearl(c, (float)cx, (float)cy, 13, 0xf1ebdf);
    else { render_doubloon(fb, stride, cx - 8, cy - 8); render_internal_annulus(c, (float)cx, (float)cy, 16, 13, GA_GOLD, 255); }
}
static void casino_controls(render_ctx_t *c, const char *go) {
    char text[16];
    for (int k = 0; k < 4; k++) {
        snprintf(text, sizeof text, "%d", casino_bets[k]);
        bool on = s_cas.bet == k;
        render_internal_button(c, 24 + k * 64, 268, 58, 34, on ? 0x3a3112 : 0x1c2f36, on ? GA_GOLD : MSP_DIM, text, 2);
    }
    render_internal_button(c, 290, 268, 134, 34, s_cas.running ? 0x10282e : 0x1c3a2a,
                           s_cas.running ? MSP_DIM : 0x8ed9a5, s_cas.running ? "..." : go, 2);
}
static void casino_footer(render_ctx_t *c) {
    const char *go = s_cas.page == CASINO_RACES ? "RACE" : s_cas.page == CASINO_HILO ? "PLAY" :
        s_cas.page == CASINO_SHELLS ? "SHUFFLE" : s_cas.page == CASINO_DIVE ? (s_cas.pending_end ? "RETRY" : "DIVE") : "SPIN";
    casino_controls(c, go);
    if (s_cas.msg[0]) {
        render_internal_text(c, 24, 318, 1, s_cas.delta > 0 && s_cas.shown ? 0xffe29a : 0xa7c4ba, s_cas.msg);
        return;
    }
    static const char *const hints[7][2] = {
        {"", ""}, {"TAP A LANE, PICK A BET, RACE.", "LONG SHOTS PAY MORE."},
        {"PICK A BET, THEN SPIN.", "ANY PAIR RETURNS THE STAKE."},
        {"HIGH OR LOW? TIES LOSE.", "ODDS FOLLOW THE PEARL VALUE."},
        {"PICK A SHELL. WIN x2.8.", "THE PEARL HIDES IN ONE OF THREE."},
        {"DIVE DEEPER OR SURFACE.", "SHARKS LOSE ALL. BACK CASHES OUT."},
        {"RED / BLUE x2. GOLD x12.", "PICK A COLOR, THEN SPIN."}};
    render_internal_text(c, 24, 314, 1, 0x8c9f9a, hints[s_cas.page][0]);
    render_internal_text(c, 24, 334, 1, 0x8c9f9a, hints[s_cas.page][1]);
}
static void render_casino(const tank_t *t, uint16_t *fb, int stride) {
    render_ctx_t c = render_internal_page_ctx(fb, stride);
    char text[72];
    render_menu_backdrop(fb, stride, 0, 0, PAGE_W, PAGE_H);
    const char *title = casino_titles[s_cas.page];
    render_internal_page_stamp(&c, 22, 12, STAMP_DICE, GA_GOLD);
    render_internal_text(&c, 54, 14, 2, 0xffffff, title);
    render_internal_button(&c, 326, 8, 112, 36, 0x1c2f36, MSP_TEAL, "BACK", 2);
    render_doubloon(fb, stride, 24, 52);
    if (progression_creative(t)) snprintf(text, sizeof text, "CREATIVE MODE - BETS ARE OFF");
    else snprintf(text, sizeof text, "%ld DOUBLOONS", (long)t->sd_balance);
    render_internal_text(&c, 48, 56, 1, 0xffe29a, text);
    if (s_cas.page == CASINO_LOBBY) {
        static const char *const subs[6] = {"PICK A FISH", "MATCH THREE", "HIGH OR LOW", "FIND PEARL", "PUSH LUCK", "PICK COLOR"};
        static const int symbols[6] = {2, 4, 3, 0, 1, 4};
        for (int i = 0; i < 6; i++) {
            int x = 24 + i % 2 * 204, y = 84 + i / 2 * 76;
            render_internal_button(&c, x, y, 196, 70, 0x10282e, GA_GOLD, "", 1);
            render_internal_text(&c, x + 10, y + 7, 1, 0xffffff, casino_titles[i + 1]);
            render_internal_text(&c, x + 10, y + 44, 1, MSP_TEAL, subs[i]);
            casino_symbol(&c, t, fb, stride, x + 172, y + 45, symbols[i], t->clock);
        }
        render_internal_text(&c, 24, 326, 1, 0xa7c4ba, "BETS 5-50. SMALL HOUSE EDGE.");
        return;
    }
    if (s_cas.page == CASINO_RACES) {
        for (int i = 0; i < 4; i++) {
            int y = 84 + i * 44;
            bool pick = s_cas.pick == i, won = s_cas.shown && s_cas.winner == i;
            render_internal_rect_fill(&c, 24, y, 400, 40, pick ? 0x1e2a18 : 0x10282e);
            render_internal_rect_edge(&c, 24, y, 400, 40, won ? 0xffffff : pick ? GA_GOLD : MSP_DIM);
            for (int d = 0; d < 40; d += 8) render_internal_rect_fill(&c, 380, y + d, 4, 4, 0xffffff);
            snprintf(text, sizeof text, "%s%s", pick ? "> " : "", FISH_VARIETY_NAMES[s_cas.racer[i]]);
            render_internal_text(&c, 30, y + 4, 1, pick ? 0xffe29a : 0xe7f4dc, text);
            snprintf(text, sizeof text, "x%d", casino_odds[i]);
            render_internal_text(&c, 392, y + 15, 1, GA_GOLD, text);
            float p = 0;
            if (s_cas.running || s_cas.shown) {
                p = s_cas.t / s_cas.finish[i];
                if (p > 1) p = 1;
                p = p * p * (3 - 2 * p);
                if (p < 1) p += 0.015f * sinf(s_cas.t * 7 + (float)i * 1.7f);
            }
            fish_t f = {0}; progression_market_preview(t, s_cas.racer[i], &f);
            render_fish_portrait(fb, stride, 50 + p * 318, (float)(y + 29), 0.42f, &f, t->clock);
        }
        casino_footer(&c);
        return;
    }
    if (s_cas.page == CASINO_HILO) {
        render_internal_text(&c, 66, 88, 1, MSP_TEAL, "PEARL");
        render_internal_text(&c, 280, 88, 1, MSP_TEAL, "NEXT");
        casino_symbol(&c, t, fb, stride, 112, 132, 3, t->clock);
        casino_symbol(&c, t, fb, stride, 320, 132, 3, t->clock);
        snprintf(text, sizeof text, "%d", s_cas.pearl);
        render_internal_text(&c, 100, 160, 2, GA_GOLD, text);
        if (s_cas.shown) snprintf(text, sizeof text, "%d", s_cas.next);
        else snprintf(text, sizeof text, "%s", s_cas.running ? "..." : "?");
        render_internal_text(&c, 308, 160, 2, GA_GOLD, text);
        for (int i = 0; i < 2; i++) {
            int wins = i ? s_cas.pearl - 1 : 13 - s_cas.pearl;
            int pay = casino_bets[s_cas.bet] * 1222 / (wins * 100);
            snprintf(text, sizeof text, "PAYS %d", pay);
            render_internal_text(&c, 42 + i * 204, 190, 1, MSP_TEAL, text);
            render_internal_button(&c, 24 + i * 204, 214, 196, 38, 0x10282e,
                s_cas.pick == i ? GA_GOLD : MSP_DIM, i ? "LOWER" : "HIGHER", 1);
        }
        casino_footer(&c); return;
    }
    if (s_cas.page == CASINO_SHELLS) {
        render_internal_text(&c, 66, 88, 1, MSP_TEAL, "WHICH SHELL HIDES THE PEARL?");
        for (int i = 0; i < 3; i++) {
            int x = 24 + i * 136, cx = x + 64;
            if (s_cas.running) cx += (int)(sinf(s_cas.t * 12 + i * 2.1f) * 18);
            casino_symbol(&c, t, fb, stride, cx, 142, 0, t->clock);
            if (s_cas.shown && s_cas.winner == i) casino_symbol(&c, t, fb, stride, cx, 182, 3, t->clock);
            snprintf(text, sizeof text, "SHELL %d", i + 1);
            render_internal_button(&c, x, 214, 128, 38, 0x10282e,
                s_cas.pick == i ? GA_GOLD : MSP_DIM, text, 1);
        }
        casino_footer(&c); return;
    }
    if (s_cas.page == CASINO_DIVE) {
        for (int i = 0; i < 5; i++) {
            int x = 24 + i * 82;
            render_internal_rect_fill(&c, x, 94, 72, 54, i < s_cas.depth ? 0x244d50 : 0x10282e);
            render_internal_rect_edge(&c, x, 94, 72, 54, i < s_cas.depth ? GA_GOLD : MSP_DIM);
            snprintf(text, sizeof text, "%d.%d", casino_depth_pay[i] / 10, casino_depth_pay[i] % 10);
            render_internal_text(&c, x + 12, 114, 1, GA_GOLD, text);
        }
        if (s_cas.shown && s_cas.delta < 0) {
            fish_t shark = {0}; progression_market_preview(t, FISH_SHARK, &shark);
            render_fish_portrait(fb, stride, 62, 184, 0.7f, &shark, t->clock);
        } else casino_symbol(&c, t, fb, stride, 62, 184, 3, t->clock);
        int pot = s_cas.depth ? (s_cas.diving ? s_cas.stake : casino_bets[s_cas.bet]) * casino_depth_pay[s_cas.depth - 1] / 10 : 0;
        if (s_cas.shown && s_cas.delta < 0) pot = 0;
        snprintf(text, sizeof text, "POT %d DOUBLOONS", pot);
        render_internal_text(&c, 104, 178, 1, GA_GOLD, text);
        render_internal_text(&c, 24, 226, 1, MSP_TEAL, s_cas.diving ? "STAKE HELD" : "SHARKS AHEAD");
        render_internal_button(&c, 236, 214, 188, 38, 0x10282e,
            s_cas.diving ? GA_GOLD : MSP_DIM, "SURFACE", 1);
        casino_footer(&c); return;
    }
    if (s_cas.page == CASINO_WHEEL) {
        static const uint32_t colors[3] = {0xda6970, 0x5ba9e8, GA_GOLD};
        for (int i = 0; i < 13; i++) {
            float spin = s_cas.running ? s_cas.t * 9 : 0;
            float a = i * 6.283185f / 13 + spin, b = (i + 1) * 6.283185f / 13 + spin;
            float xs[3] = {224, 224 + cosf(a) * 59, 224 + cosf(b) * 59};
            float ys[3] = {146, 146 + sinf(a) * 59, 146 + sinf(b) * 59};
            render_internal_poly(&c, xs, ys, 3, colors[i == 12 ? 2 : i % 2], 255);
        }
        render_internal_annulus(&c, 224, 146, 61, 58, GA_GOLD, 255);
        casino_symbol(&c, t, fb, stride, 224, 146, 4, t->clock);
        static const char *const labels[3] = {"RED x2", "BLUE x2", "GOLD x12"};
        if (s_cas.shown) render_internal_text(&c, 294, 142, 1, colors[s_cas.winner], labels[s_cas.winner]);
        for (int i = 0; i < 3; i++) render_internal_button(&c, 24 + i * 136, 214, 128, 38,
            s_cas.pick == i ? 0x353126 : 0x10282e, s_cas.pick == i ? GA_GOLD : colors[i], labels[i], 1);
        casino_footer(&c); return;
    }
    for (int k = 0; k < 3; k++) {
        int x = 84 + k * 100;
        render_internal_rect_fill(&c, x, 84, 80, 88, 0x05141a);
        render_internal_rect_edge(&c, x, 84, 80, 88, GA_GOLD);
        render_internal_rect_edge(&c, x + 2, 86, 76, 84, 0x6b5420);
        bool spinning = s_cas.running && s_cas.t < 0.6f + 0.4f * (float)k;
        int sym = spinning ? ((int)(s_cas.t * 14) + k * 2) % 5 : s_cas.reels[k];
        casino_symbol(&c, t, fb, stride, x + 40, 128, sym, t->clock);
    }
    static const char *const pays[8] = {"3 SHELLS x3", "3 STARS x6", "3 FISH x10", "3 PEARLS x20",
                                        "3 COINS x50", "COIN PAIR x2", "ANY PAIR x1", "(STAKE BACK)"};
    for (int i = 0; i < 8; i++)
        render_internal_text(&c, 24 + (i % 2) * 212, 184 + (i / 2) * 20, 1, i >= 5 ? 0xa7c4ba : 0xe7f4dc, pays[i]);
    casino_footer(&c);
}
void render_games(const tank_t *t, uint16_t *fb, int stride) {
    if (s_cas.active) { render_casino(t, fb, stride); return; }
    render_ctx_t c = render_internal_page_ctx(fb, stride);
    if (g_game.page == GAME_MENU || g_game.page == GAME_RECORDS) {
        render_menu_backdrop(fb, stride, 0, 0, PAGE_W, PAGE_H);
    } else if (g_game.page != GAME_RESULT && (g_game.help || g_game.paused)) {
        render_internal_rect_fill(&c, 0, 0, PAGE_W, PAGE_H, MSP_INK);  /* help hides the playfield */
    } else {
        int deep = g_game.page == GAME_EXPEDITION ? g_game.depth * 26 : 0;
        ga_backdrop(&c, ga_mix(0x15596b, 0x0a1c2a, deep), ga_mix(0x072431, 0x02080c, deep));
    }
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
        for (int i = 0; i < 6 && g_game_menu < 3; i++) {
            int x = 24 + i % 2 * 204, y = 116 + i / 2 * 64, game = g_game_menu * 6 + i;
            render_internal_button(&c, x, y, 196, 56, 0x10282e, MSP_DIM, "", 1);
            game_thumb(&c, x + 6, y + 12, game);
            render_internal_text_wrap(&c, x + 54, y + 5, 136, 0xffffff, game_names[game]);
            render_internal_text(&c, x + 54, y + 40, 1, MSP_TEAL, genres[game]);
        }
        if (g_game_menu == 3) {
            static const char *const names[] = {"STORIES", "THE LOST CURRENT"};
            static const char *const kinds[] = {"SAFE TRIPS", "STORY RPG"};
            for (int i = 0; i < 2; i++) {
                int x = 24 + i * 204;
                uint32_t accent = i ? 0xffdc73 : MSP_TEAL;
                render_internal_button(&c, x, 116, 196, 56, 0x10282e, MSP_DIM, "", 1);
                render_internal_rect_fill(&c, x + 6, 124, 40, 40, 0x092027);
                render_internal_rect_edge(&c, x + 6, 124, 40, 40, ga_mix(accent, 0x092027, 128));
                render_internal_page_stamp(&c, x + 11, 132, i ? STAMP_BOOK : STAMP_MAP, accent);
                render_internal_text_wrap(&c, x + 54, 121, 136, 0xffffff, names[i]);
                render_internal_text(&c, x + 54, 156, 1, MSP_TEAL, kinds[i]);
            }
            render_internal_text_wrap(&c, 24, 196, 400, 0xa7c4ba,
                                      "Stories send your fish on calm trips. The Lost Current is a chaptered RPG. Both use the fish chosen above.");
        }
        snprintf(text, sizeof text, "PAGE %d/%d", g_game_menu + 1, GAME_MENU_PAGES);
        render_internal_text(&c, 300, 104, 1, MSP_TEAL, text);
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
                snprintf(text, sizeof text, "BEST %u%s RND %u", (unsigned)records->best[i],
                         g_game_menu == 2 && i < 2 ? " MOVES" : "", (unsigned)records->rounds[i]);
                render_internal_text(&c, 76, y + 18, 1, 0xffffff, text);
                continue;
            }
            snprintf(text, sizeof text, "%s%s  %s", t->companion.featured == i ? "> " : "",
                     short_names[i], medals[medal < 4 ? medal : 0]);
            render_internal_text(&c, 76, y + 1, 1, MSP_TEAL, text);
            snprintf(text, sizeof text, "BEST %u%s RND %u", (unsigned)t->companion.game_best[i],
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
        ga_hud(&c, 32, 62, 2, MSP_TEAL, text);
        for (int i = 0; i < 6; i++) if (g_game.bubbles[i].live) {
            int x = (int)g_game.bubbles[i].x, y = (int)g_game.bubbles[i].y;
            bool gold = g_game.bubbles[i].gold;
            ga_bubble(&c, x, y, 24, gold ? GA_GOLD : 0x73e5ff);
            if (gold) render_internal_text(&c, x - 5, y - 7, 2, 0xffdc73, "3");
        }
    } else if (g_game.page == GAME_SLALOM || g_game.page == GAME_CAST || g_game.page == GAME_FRENZY) {
        if (g_game_relaxed && g_game.page == GAME_FRENZY) snprintf(text, sizeof text, "SCORE %d", g_game.score);
        else snprintf(text, sizeof text, "SCORE %d   %s %d", g_game.score, g_game.page == GAME_FRENZY ? "TIME" : "ROUND",
                      g_game.page == GAME_FRENZY ? (int)ceilf(g_game.left) : g_game.rounds + 1);
        ga_hud(&c, 24, 58, 2, MSP_TEAL, text);
        if (g_game.page == GAME_SLALOM) {
            int dy = (int)(g_game.travel * 0.55f);
            for (int lane = 0; lane < 3; lane++) ga_wash(&c, 28 + lane * 140, 112, 92, 148, 0x041820, 90);
            for (int k = 0; k < 2; k++) {                     /* kelp between the lanes */
                float kx = 144 + k * 140;
                for (int i = 0; i < 3; i++) {
                    float y = 260 - i * 50, sway = i & 1 ? 4 : -3;
                    ga_quad(&c, kx - 3, y, kx + 3, y, kx + 3 + sway, y - 52, kx - 3 + sway, y - 52, 0x2f6b44, 255);
                    ga_ell(&c, kx + sway + (i & 1 ? 9 : -9), y - 26, 9, 4, 0x3f8552);
                }
            }
            for (int lane = 0; lane < 3; lane++) {
                int x = 74 + lane * 140, y = 112 + dy;
                if (lane == g_game.target) {
                    for (int i = 0; i < 6; i++) ga_fill(&c, x - 28 + i * 10, y + 5, 6, 2, 0xd5e6e8);
                    ga_buoy(&c, x - 36, y + 4);
                    ga_buoy(&c, x + 36, y + 4);
                } else {
                    ga_rock(&c, x - 22, y + 6, 48, 18, 0x56676b);
                    ga_rock(&c, x + 22, y + 7, 46, 16, 0x4c5c60);
                    ga_coral(&c, x + 6, y + 2, 0.55f, lane ? 0xe2786a : 0xd8608e);
                }
            }
            ga_hud(&c, 24, 82, 2, MSP_TEAL, "SWIM THROUGH THE OPEN GATE");
            if (t->n_fish) render_fish_portrait(fb, stride, 74 + g_game.lane * 140, 235, 1.3f, &t->fish[g_game.fish % t->n_fish], g_game.travel * 0.03f);
            render_internal_button(&c, 32, 276, 182, 52, 0x1c2f36, MSP_TEAL, "< LEFT", 2);
            render_internal_button(&c, 234, 276, 182, 52, 0x1c2f36, MSP_TEAL, "RIGHT >", 2);
        } else if (g_game.page == GAME_CAST) {
            ga_hud(&c, 24, 80, 2, MSP_TEAL, "STOP THE HOOK OVER THE TREASURE");
            int tx = 32 + (int)((g_game.target + 0.5f) * 384 / 3);
            float pos = g_game.travel <= 1 ? g_game.travel : 2 - g_game.travel;
            int hx = 32 + (int)(pos * 384);
            render_internal_rect_blend(&c, 24, 110, 400, 3, 0xd8f4f2, 110);
            ga_quad(&c, hx - 30, 104, hx + 24, 104, hx + 16, 114, hx - 22, 114, 0x6e4122, 255);
            ga_fill(&c, hx - 26, 104, 46, 2, 0xa8743e);
            ga_fill(&c, hx - 12, 97, 14, 7, 0xd8c094);
            ga_hook(&c, hx, 114, 201);
            for (int lane = 0; lane < 3; lane++) if (lane != g_game.target) {
                if (t->n_fish && lane == (g_game.target + 1) % 3)
                    render_fish_portrait(fb, stride, 96 + lane * 128, 234, 0.7f,
                                         &t->fish[g_game.fish % t->n_fish], g_game.travel);
                else {
                    ga_rock(&c, 95 + lane * 128, 240, 62, 26, 0x56676b);
                    ga_grass(&c, 125 + lane * 128, 254, 0.9f);
                }
            }
            ga_chest(&c, tx, 238, 1.0f);
            render_internal_button(&c, 32, 276, 384, 52, 0x1c2f36, MSP_TEAL, "CAST!", 3);
        } else {
            static const char *const snacks[] = {"FLAKES", "WORMS", "GREENS"};
            snprintf(text, sizeof text, "WANTS: %s", snacks[g_game.target]);
            ga_hud(&c, 80, 98, 3, 0xffdc73, text);
            float ax = 88 + g_game.target * 136;
            ga_tri(&c, ax - 9, 250, ax + 9, 250, ax, 262, GA_GOLD);
            if (t->n_fish) {
                const fish_t *f = &t->fish[g_game.fish % t->n_fish];
                ga_bubble(&c, 340, 170, 28, 0x9fd8e2);
                ga_food(&c, 340, 170, g_game.target);
                ga_bubble(&c, 304, 196, 6, 0x9fd8e2);
                ga_bubble(&c, 292, 208, 4, 0x9fd8e2);
                render_fish_portrait(fb, stride, 224, 182, 2, f, g_game.left);
                ga_hud(&c, 32, 232, 2, MSP_TEAL, f->name);
            }
            for (int i = 0; i < 3; i++) {
                int x = 24 + i * 136;
                render_internal_button(&c, x, 268, 128, 58, 0x1c2f36, MSP_TEAL, "", 2);
                ga_food(&c, x + 24, 297, i);
                render_internal_text(&c, x + 46, 290, 2, 0xffffff, snacks[i]);
            }
        }
    } else if (g_game.page == GAME_REEF) {
        ga_hud(&c, 24, 58, 2, MSP_TEAL, "TAP PIPES TO CONNECT IN TO OUT");
        bool wet[9], out = game_reef_flow(wet);
        ga_hud(&c, 32, 191, 2, 0x73e5ff, "IN >"); ga_hud(&c, 350, 191, 2, 0xffdc73, "> OUT");
        ga_block(&c, 104, 90, 230, 230, 0x16323b);
        ga_pipe(&c, 84, 196, 28, 14, true);
        ga_pipe(&c, 334, 196, 10, 14, out);
        for (int i = 0; i < 9; i++) {
            int x = 112 + i % 3 * 72, y = 98 + i / 3 * 72, mask = g_game.pipes[i];
            ga_block(&c, x, y, 66, 66, 0x2c4a52);
            ga_speckle(&c, x, y, 66, 66, (unsigned)i * 31u + 3u, 0x3a5c66);
            if (mask & 1) ga_pipe(&c, x + 26, y + 1, 14, 38, wet[i]);
            if (mask & 2) ga_pipe(&c, x + 26, y + 26, 39, 14, wet[i]);
            if (mask & 4) ga_pipe(&c, x + 26, y + 26, 14, 39, wet[i]);
            if (mask & 8) ga_pipe(&c, x + 1, y + 26, 39, 14, wet[i]);
            if (mask & 1) ga_fill(&c, x + 23, y + 1, 20, 4, 0x8a5a2e);
            if (mask & 2) ga_fill(&c, x + 61, y + 23, 4, 20, 0x8a5a2e);
            if (mask & 4) ga_fill(&c, x + 23, y + 61, 20, 4, 0x8a5a2e);
            if (mask & 8) ga_fill(&c, x + 1, y + 23, 4, 20, 0x8a5a2e);
            ga_ell(&c, x + 33, y + 33, 10, 10, 0x5a3c22);
            ga_ell(&c, x + 33, y + 33, 8, 8, 0xb07a40);
            ga_ell(&c, x + 31, y + 31, 3, 2, 0xe2b06c);
            ga_ell(&c, x + 33, y + 34, 3.5f, 3.5f, wet[i] ? 0x73e5ff : 0x3a2818);
        }
    } else if (g_game.page == GAME_MATCH) {
        snprintf(text, sizeof text, "PAIRS %d/6   MOVES %d", g_game.pairs, g_game.moves);
        ga_hud(&c, 32, 62, 2, MSP_TEAL, text);
        static const uint32_t colors[] = {0xffdc73, 0x73e5ff, 0xffa3cf, 0x89ffb1, 0xc5adff, 0xff8173};
        for (int i = 0; i < 12; i++) {
            int x = 32 + i % 4 * 96, y = 94 + i / 4 * 72;
            bool shown = g_game.matched[i] || i == g_game.first || i == g_game.second;
            if (!shown) {
                ga_block(&c, x, y, 88, 64, 0x2a6070);
                render_internal_rect_edge(&c, x + 5, y + 4, 76, 54, 0x4f8e9c);
                ga_shell(&c, x + 43, y + 30, 1.0f, GA_GOLD);
                continue;
            }
            uint32_t color = colors[g_game.cards[i]];
            ga_socket(&c, x, y, 88, 64, g_game.matched[i] ? 0x124236 : 0x14333d);
            if (g_game.matched[i]) render_internal_rect_edge(&c, x, y, 88, 64, 0x89ffb1);
            ga_ell(&c, x + 28, y + 44, 20, 9, 0x6b5a7a);
            ga_ell(&c, x + 28, y + 42, 16, 6, 0x2a1e30);
            ga_pearl(&c, x + 28, y + 32, 11, color);
            snprintf(text, sizeof text, "%d", g_game.cards[i] + 1);
            render_internal_text(&c, x + 56, y + 21, 3, color, text);
        }
    } else {
        ga_wash(&c, 22, 62, 412, 260, 0x04141a, 200);
        game_thumb(&c, 32, 72, game_index(g_game.kind));
        render_internal_text(&c, 84, 80, 2, 0xffffff, "ROUND COMPLETE");
        bool puzzle = g_game.kind == GAME_LIGHTHOUSE || g_game.kind == GAME_SLIDE;
        bool solved = g_game.kind == GAME_MATCH || g_game.kind == GAME_REEF || (puzzle && g_game.reward);
        snprintf(text, sizeof text, solved ? "SOLVED IN %d MOVES" : "SCORE: %d", solved ? g_game.moves : g_game.score);
        if (puzzle && !solved) snprintf(text, sizeof text, "UNSOLVED - NO REWARD");
        render_internal_text(&c, 32, 126, 2, MSP_TEAL, text);
        snprintf(text, sizeof text, "WALLET: %ld DOUBLOONS", (long)t->sd_balance);
        render_internal_text(&c, 32, 164, 2, MSP_TEAL, text);
        int payout = progression_game_payout(t, game_index(g_game.kind), g_game.reward);
        snprintf(text, sizeof text, g_game.claimed ? "SAVED +%d DOUBLOONS" :
                 payout ? "SAVE RESULT +%d DOUBLOONS" : "SAVE RESULT - NO BONUS",
                 g_game.claimed ? g_game.paid : payout);
        if (g_game.kind >= GAME_LIGHTHOUSE && !g_game.reward)
            snprintf(text, sizeof text, g_game.claimed ? "RESULT SAVED - NO REWARD" : "SAVE RESULT - NO REWARD");
        if (progression_creative(t)) snprintf(text, sizeof text, "CREATIVE - PRACTICE ONLY");
        render_internal_button(&c, 32, 196, 396, 44, 0x1c2f36, MSP_TEAL, text, 2);
        render_internal_button(&c, 32, 278, 396, 44, 0x1c2f36, MSP_TEAL, "PLAY AGAIN", 2);
        snprintf(text, sizeof text, "PAID ROUNDS TODAY: %d/5",
                 t->game_income.paid[game_index(g_game.kind)] + t->game_income.legacy);
        if (progression_creative(t)) snprintf(text, sizeof text, "PRACTICE - NO PAY OR RECORDS");
        render_internal_text(&c, 32, 248, 2, MSP_TEAL, text);
    }
    if (g_game.page != GAME_MENU && g_game.page != GAME_RECORDS && g_game.page != GAME_RESULT) {
        render_internal_button(&c, 244, 8, 76, 36, 0x1c2f36, MSP_TEAL, g_game.paused ? "PLAY" : "PAUSE", 1);
        render_internal_button(&c, 164, 8, 72, 36, 0x1c2f36, MSP_TEAL, "HELP", 1);
        if (g_game.help || g_game.paused) {
            static const char *const help[18][5] = {
                {"Tap rising bubbles.", "Gold bubbles score three.", "Challenge lasts 30 seconds.", "Practice shares this game cap.", "+1 doubloon; max 5/game/day."},
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
                {"Find five pearls in the grid.", "Gold: total pearls per row/col.", "15 probes; Relaxed gives 20.", "2+ pearls qualify; five: gold.", "+1/round; max 5/game/day."},
                {"Use arrows to slide all tiles.", "Equal neighbors merge once.", "New tiles appear after moves.", "128+ qualifies; 2048: gold.", "+1/round; max 5/game/day."},
                {"Tap DROP to place the gold bar.", "Only the overlap stays; missed", "edges are lost. Stack 12 layers.", "30+ qualifies; 120: gold.", "+1/round; max 5/game/day."},
                {"Watch the numbered pads flash.", "Repeat the sequence by tapping.", "One new note per round, to 12.", "3+ qualifies; 12: gold.", "+1/round; max 5/game/day."}
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
        if (progression_creative(t)) snprintf(text, sizeof text, "CREATIVE: PRACTICE ONLY");
        else snprintf(text, sizeof text, "UP TO 12 PER ROUND, 5/GAME/DAY");
        render_internal_text(&c, 24, 350, 1, MSP_TEAL, text);
    } else render_internal_text(&c, 24, 340, 2,
                    g_game.save_failed ? 0xff8173 : MSP_TEAL,
                    g_game.save_failed ? "SAVE FAILED - TAP CLAIM TO RETRY" :
                    g_game.discard ? "TAP AGAIN TO DISCARD RESULT" :
                    g_game_relaxed ? "RELAXED - NO RECORDS OR MEDALS" : "AQUARIUM CARE PAUSED");
}
