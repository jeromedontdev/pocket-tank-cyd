#ifndef GAMES_INTERNAL_H
#define GAMES_INTERNAL_H

#include "render_internal.h"

enum {
    GAME_MENU, GAME_BUBBLES, GAME_MATCH, GAME_RESULT, GAME_SLALOM, GAME_CAST,
    GAME_FRENZY, GAME_REEF, GAME_RECORDS, GAME_DEFENDER, GAME_RUNNER,
    GAME_BREAKER, GAME_TACTICS, GAME_ARCHITECT, GAME_EXPEDITION,
    GAME_LIGHTHOUSE, GAME_SLIDE, GAME_SONAR, GAME_MERGE, GAME_STACK, GAME_ECHO
};

typedef struct {
    int page, kind, score, moves, pairs, first, second, spawned, reward;
    int fish, lane, target, errors, rounds, discard, paid;
    float left, spawn, reveal;
    float travel;
    bool claimed, save_failed;
    uint8_t cards[12], matched[12], pipes[9];
    struct { float x, y; bool live, gold; } bubbles[6];
    int menu, hp, wave, remaining, selected, oxygen, keys, supplies, depth;
    bool paused, help, launched;
    float px, bx, by, vx, vy, cooldown, invincible;
    uint8_t board[25], choices[3];
    struct { float x, y, vx, vy; int hp; } foes[8], shots[8];
} game_state_t;

#ifdef RENDER_GAMES_TESTING
extern bool g_game_relaxed;
extern game_state_t g_game;
extern const int game_kinds[18];
extern const char *const game_names[18];
extern const char *const game_titles[18];
void game_start(int kind);
void game_finish(void);
bool game_slide_solved(void);
void game_practice_tap(float x, float y);
int game_merge_cell(int direction, int line, int offset);
void game_merge_move(int direction);
void game_thumb(render_ctx_t *c, int x, int y, int game);
extern const int casino_bets[4], casino_odds[4], casino_reel_weight[5], casino_triple_pay[5];
int casino_reel_symbol(uint32_t r);
int casino_reel_pay(const uint8_t r[3]);
int casino_race_winner(uint32_t r);
#endif

#endif
