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
void companion_open_creature(int kind);
enum { COMPANION_SECTION_CARE, COMPANION_SECTION_COLLECTIONS, COMPANION_SECTION_ADVENTURES,
       COMPANION_SECTION_APPEARANCE = 7, COMPANION_SECTION_LIFE = 8,
       COMPANION_SECTION_RECAP = 9, COMPANION_SECTION_IDENTITY = 10,
       COMPANION_SECTION_OBJECTS = 11, COMPANION_SECTION_FAMILY = 15,
       COMPANION_SECTION_VISITORS = 16, COMPANION_SECTION_REWARDS = 18 };
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
bool companion_games_valid(const game_records_t *s);
bool companion_captions(const tank_t *t);
bool companion_lively(const tank_t *t);
/* Favorite activity derived from saved creation personality + preset (a GOAL_*
 * id, -1 if no fish); text adds "(seen!)" only when that fish's milestone exists. */
int  companion_favorite(const tank_t *t, int fish);
void companion_favorite_text(const tank_t *t, int fish, char *text, size_t size);
/* Compact CARE label, at most 25 chars: "LIKES: TANKMATES (NATURE)" / "(SEEN)". */
void companion_favorite_label(const tank_t *t, int fish, char *text, size_t size);
/* Read-only short event/need caption. Empty during ordinary calm swimming. */
void companion_status(const tank_t *t, char *text, size_t size);
enum { MEMORY_ARRIVAL = 1, MEMORY_GROWTH, MEMORY_BUDDY, MEMORY_DISCOVERY,
       MEMORY_ADVENTURE, MEMORY_HELPERS, MEMORY_VISITOR, MEMORY_RPG };
bool companion_rpg_valid(const tank_rpg_t *s, uint32_t next_id);
const char *companion_rpg_chapter_name(const tank_t *t);
const char *companion_rpg_chapter_title(int chapter);
const char *companion_rpg_route(const tank_t *t, int chapter);
const char *companion_rpg_prompt(const tank_t *t); /* at most 115 characters */
void companion_rpg_choice_label(const tank_t *t, int choice, char *text, size_t size);
bool companion_rpg_choose(tank_t *t, int fish, int choice);
void companion_rpg_context(const tank_t *t, int fish, char *text, size_t size);
bool companion_rpg_contributor(const tank_t *t, uint32_t id);
bool companion_rpg_legacy(const tank_t *t, int fish);
const char *companion_rpg_outcome(const tank_t *t);
void companion_polish_init(tank_t *t, bool fresh);   /* also migrates the 1.3 story */
typedef struct { const char *name, *memory, *words; int weight; } companion_visitor_t;
extern const companion_visitor_t COMPANION_VISITORS[VISITOR_SPECIES];
void companion_story_init(tank_t *t);
bool companion_story_valid(const tank_story_t *s, int fish, int bank, bool away, uint32_t next_id);
const char *companion_story_name(const tank_t *t, uint16_t id);          /* NULL = unknown */
const fish_lineage_t *companion_story_line(const tank_t *t, uint16_t id); /* a living fish's line */
int  companion_visitor_pick(tank_t *t);                                  /* records the sighting */
void companion_debug_visit(tank_t *t, int species);   /* serial: a visit on the next tick (-1 = random) */
void companion_debug_season(tank_t *t, int season);   /* serial: force SEASON_* (-1 = the clock) */
int  companion_season_for(int64_t local_unix, uint8_t flags);            /* SEASON_* | SEASON_LIGHTS */
bool companion_set_seasons(tank_t *t, uint8_t flags);                    /* saved; false = unchanged */
void companion_polish_observe(tank_t *t);
bool companion_polish_valid(const tank_polish_t *p, int fish, int bank, bool away);
void companion_memory(tank_t *t, int kind, int fish, int detail);
void companion_care_text(const tank_t *t, int fish, char *text, size_t size);
void companion_memory_text(const tank_memory_t *memory, char *text, size_t size);
const char *companion_subtitle(unsigned value);
const char *companion_emblem(unsigned value);
bool companion_identity(tank_t *t, bool emblem);
bool companion_backup_record(tank_t *t);
void companion_date_text(int64_t when, char *text, size_t size);
void companion_open(void);
void companion_render(const tank_t *t, uint16_t *fb, int stride);
/* Coordinates are FRAME coordinates, like the other public page APIs. */
bool companion_tap(tank_t *t, float frame_x, float frame_y);
#endif
