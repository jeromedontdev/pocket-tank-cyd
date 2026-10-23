#ifndef POCKET_TANK_COMPANION_H
#define POCKET_TANK_COMPANION_H
#include "tank.h"
#include <stddef.h>

#define COMPANION_DISCOVERY_COUNT 12
#define COMPANION_COLLECTION_COUNT 3
#define COMPANION_ADVENTURE_COUNT 3
#define COMPANION_VERSION 5
extern const char *const WEAR_NAMES[WEAR_COUNT];
extern const uint8_t WEAR_LEVELS[WEAR_COUNT], WEAR_PRICES[WEAR_COUNT];
bool companion_wearable(tank_t *t, int fish, int wearable);
bool companion_colors(tank_t *t, bool wearable, int target, uint8_t colors);
bool companion_compact(tank_t *t, bool compact);
void companion_open_section(int section);
enum { COMPANION_SECTION_CARE, COMPANION_SECTION_COLLECTIONS, COMPANION_SECTION_ADVENTURES, COMPANION_SECTION_APPEARANCE = 7 };
extern const char *const COMPANION_DISCOVERIES[COMPANION_DISCOVERY_COUNT];
extern const char *const COMPANION_COLLECTIONS[COMPANION_COLLECTION_COUNT];
extern const char *const COMPANION_ADVENTURES[COMPANION_ADVENTURE_COUNT];
extern const uint16_t COMPANION_ADVENTURE_SECONDS[COMPANION_ADVENTURE_COUNT];

void companion_init(tank_t *t);
/* Zero/missing tails migrate to Classic with captions OFF. */
bool companion_valid(const companion_state_t *s);
void companion_tick(tank_t *t, float dt);
bool companion_set_options(tank_t *t, bool cozy, bool captions, bool lively);
bool companion_greet(tank_t *t, int fish);
bool companion_trick(tank_t *t, int fish, int trick);
bool companion_collection_ready(const tank_t *t, int collection);
bool companion_collection_claim(tank_t *t, int collection);
bool companion_cosmetic(tank_t *t, int cosmetic);
bool companion_set_decor(tank_t *t, bool show);
void companion_render_decor(const tank_t *t, uint16_t *fb, int stride);
/* Departures reserve a tank slot and pause care/aging. Synced elapsed time is
 * used when available; otherwise powered-on time advances the adventure. */
unsigned companion_adventure_total(const companion_state_t *s, int adventure);
bool companion_adventure_start(tank_t *t, int adventure, int fish);
bool companion_adventure_choice(tank_t *t, int choice); /* 0 arch, 1 bubbles; before departure */
bool companion_adventure_claim(tank_t *t);
bool companion_game_claim(tank_t *t, int game, int score, int moves, int dollars, bool relaxed);
bool companion_captions(const tank_t *t);
bool companion_lively(const tank_t *t);
/* Favorite activity derived from saved creation personality + preset (a GOAL_*
 * id, -1 if no fish); text adds "(seen!)" only when that fish's milestone exists. */
int  companion_favorite(const tank_t *t, int fish);
void companion_favorite_text(const tank_t *t, int fish, char *text, size_t size);
/* Compact CARE label, at most 25 chars: "LIKES: TANKMATES (NATURE)" / "(SEEN)". */
void companion_favorite_label(const tank_t *t, int fish, char *text, size_t size);
/* Read-only caption; rotates every 6 powered-on seconds through the latest
 * discovery and each fish's favorite / current activity (followed tankmate). */
void companion_status(const tank_t *t, char *text, size_t size);
void companion_open(void);
void companion_render(const tank_t *t, uint16_t *fb, int stride);
/* Coordinates are FRAME coordinates, like the other public page APIs. */
bool companion_tap(tank_t *t, float frame_x, float frame_y);
#endif
