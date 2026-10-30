/* progression.h — care, growth, rewards and profile persistence.
 * Current CYD behavior is documented in docs/CYD-FEATURES.md. Platform-agnostic;
 * persistence and wall-clock come in through two tiny port functions.
 *
 *  - Population: a new tank starts with two fry. Healthy, settled adult
 *    pairs of the same variety breed after shared meals, up to POP_CAP.
 *    Bought stock and inherited offspring are traded through the markets.
 *  - Growth: well-fed fish grow (size) and advance fry -> juv -> adult -> elder
 *    with time - every awake second, light on or off (2026-09-14, Strato:
 *    "fish don't stop growing with the light off"), and a slept span at
 *    SLEEP_GROWTH_FRAC of its length; stage changes are silent surprises.
 *  - Trait drift: bold and social creep with experience (hours of pressure).
 *  - Achievements: retired. Old bits and ledgers remain save-compatible,
 *    but no new firsts, growth/trust payouts or achievement modals occur.
 *  - Habits: the tank remembers where you feed and greets you at light-on.
 *  - Ravenous boot: powered off >= 1 h -> fish are starving on return and wait
 *    near the surface (at your usual feeding spot) until the first feeding.
 *    Hard starvation deaths accrue only while running, never offline or from
 *    old age. Normal and Creative do not kill fish.
 *  - Saves: on events (arrival, stage, light change), coalesced to
 *    >= 2 min apart, plus a 10-minute heartbeat; explicit keeper actions save
 *    at once. Each routine save is a visible SPIFFS stall, so keep them rare. */
#ifndef PROGRESSION_H
#define PROGRESSION_H
#include "tank.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* ---- platform ports (sim: file + time(); device: internal store + RTC) ---- */
/* Legacy NVS migration namespace + blob key. Expanded CYD saves use the
 * existing SPIFFS partition through the persistence port. Preserve these
 * names so older saves remain discoverable during upgrades.
 * --selftest-saves pins the spelling. */
#define SAVE_NVS_NS  "tank"
#define SAVE_NVS_KEY "save"
#define JOURNAL_GOAL_N 15
#define JOURNAL_LEVEL_MAX 15
#define JOURNAL_XP_MAX 1000000u
typedef struct { const char *name, *detail; uint8_t metric; uint16_t target, xp, coins; } journal_goal_t;
extern const journal_goal_t JOURNAL_GOALS[JOURNAL_GOAL_N];
#define JOURNAL_THEME_COUNT 8
extern const char *const JOURNAL_THEME_NAMES[JOURNAL_THEME_COUNT];
extern const uint8_t JOURNAL_THEME_LEVELS[JOURNAL_THEME_COUNT];
extern const uint8_t JOURNAL_VARIETY_LEVELS[FISH_VARIETY_COUNT];
extern const uint8_t MARKET_VARIETY_TIERS[FISH_VARIETY_COUNT];
bool journal_variety_unlocked(const tank_t *t, int variety);
int journal_level(const tank_t *t);
/* Historical XP remains in the frozen save field for migration/audit only.
 * The active progression is paid in doubloons and never grants XP. */
uint32_t journal_level_xp(int level);
int journal_theme_price(int theme);
bool journal_theme_buy(tank_t *t, int theme);
typedef enum {
    PROGRESSION_FOOD_BASE = 0,
    PROGRESSION_FOOD_PLUS = 1,
    PROGRESSION_FOOD_PREMIUM = 2,
    PROGRESSION_FOOD_TIER_COUNT = 3
} progression_food_tier_t;
int progression_food_price(unsigned tier);
bool progression_food_buy(tank_t *t, unsigned tier);
unsigned progression_food_tier(const tank_t *t);
float progression_food_growth_multiplier(const tank_t *t);
int journal_active(const tank_t *t, int slot);
unsigned journal_progress(const tank_t *t, int goal);
bool journal_claim(tank_t *t, int goal);
/* -1 bank index deposits; -1 active index withdraws; both indices swap.
 * NULL means saved successfully; otherwise a user-readable error. */
const char *progression_bank_move(tank_t *t, int bank, int active);
#define AQUARIUM_CAP 8
#define AQUARIUM_PRICE 500
#define AQUARIUM_SAVE_BYTES (6032 + sizeof(inventory_state_t) + sizeof(mussel_state_t) + sizeof(game_income_state_t) + 4 + sizeof(decor_content_t) + sizeof(decor_fixed_t) + sizeof(reef_state_t) + sizeof(creature_state_t) + sizeof(progression_state_t) + sizeof(foliage_state_t))
#define MUSSEL_SECONDS 600
#define MUSSEL_DAILY_LIMIT 15
bool progression_mussel_collect(tank_t *t);
bool progression_mussel_healthy(const tank_t *t);
#define CARE_TASK_COUNT 3
#define CARE_GAME_LIMIT 5
#define CARE_VIEW_SECONDS 300
#define CARE_CALM_SECONDS 60
extern const char *const CARE_TASK_NAMES[CARE_TASK_COUNT];
extern const char *const CARE_TASK_DETAILS[CARE_TASK_COUNT];
extern const uint8_t CARE_COINS[CARE_TASK_COUNT], CARE_XP[CARE_TASK_COUNT];
void progression_care_refresh(tank_t *t);
bool progression_care_claim(tank_t *t, int task);
void progression_care_greet(tank_t *t);
int progression_game_payout(const tank_t *t, int game, int dollars);
game_mode_t progression_mode(const tank_t *t);
bool progression_mode_chosen(const tank_t *t);
/* False after blocked/corrupt load; never treat that as an empty new profile. */
bool progression_storage_ready(void);
bool progression_hard(const tank_t *t);
bool progression_item_unlocked(const tank_t *t, int item);
const char *progression_mode_select(tank_t *t, int mode);
/* Six running hours at hunger >=9.5; feeding below 8.5 resets the timer.
 * Recovery is a free founding pair only when this Hard aquarium is empty. */
#define HARD_STARVATION_S (6.0f * 60.0f * 60.0f)
const char *progression_hard_rescue(tank_t *t);
const char *progression_hard_status(const tank_t *t);
bool persist_port_blocked(void);
bool progression_creative(const tank_t *t);
#define CREATIVE_FISH_CAP 5
/* Compatibility entry point: first-run selection only. */
const char *progression_creative_enter(tank_t *t);
const char *progression_creative_add_fish(tank_t *t, int variety);
/* Keeps two residents, matching the normal save/Bank population invariant. */
const char *progression_creative_remove_fish(tank_t *t, int fish);
bool progression_adventure_depart(tank_t *t, int adventure, int fish);
bool progression_adventure_return(tank_t *t);
/* target == count buys and enters a new tank; otherwise switches to an owned tank.
 * Success loads the tank in memory after its atomic internal-save commit. */
const char *progression_aquarium_move(tank_t *t, int target);
const char *progression_aquarium_rename(tank_t *t, const char *name);
bool progression_aquarium_label(const tank_t *t, int slot, char name[16]);
typedef struct {
    uint8_t fish, away, bank, shrimp, helpers, upgrades, theme;
    uint8_t subtitle, emblem;
    bool creative;
    int64_t last_visit;
} aquarium_summary_t;
bool progression_aquarium_summary(const tank_t *t, int slot, aquarium_summary_t *summary);
bool aquarium_port_ready(void);
void aquarium_port_id(uint32_t id[2]);
bool aquarium_port_read(const uint32_t id[2], int slot, void *data, size_t size);
/* Zero-fill absent legacy tails and report the verified payload length, not capacity. */
bool aquarium_port_read_sized(const uint32_t id[2], int slot, void *data, size_t size, size_t *got);
bool aquarium_port_write(const uint32_t id[2], int slot, const void *data, size_t size);
bool journal_theme_unlocked(const tank_t *t, int theme);
bool journal_set_theme(tank_t *t, int theme);
void journal_tick(tank_t *t);
bool    persist_port_load(void *buf, size_t max, size_t *got); /* the saved blob, whatever length an older
                                                            * build wrote (*got = it); a NEWER build's longer one
                                                            * loads its first max bytes (*got = max); false = nothing */
bool    persist_port_save(const void *buf, size_t len);
bool    persist_port_erase(void);                          /* EVERY saved tank, parked copies included */
int64_t clock_port_now_unix(void);                         /* 0 if unknown */
/* the release (version.h PT_RELEASE_NUM) of the build that wrote the save
 * the last load read - 0 for a save from before release numbers, or none */
uint32_t progression_loaded_release(void);
int64_t  progression_loaded_unix(void);                    /* the loaded save's wall-clock stamp, 0 = none (a clockless board seeds its clock from it) */
const char *version_port_string(void);                     /* the build's git describe (device: the app
                                                            * descriptor; sim: PT_VERSION) - the settings page */

/* call after tank_init: restores the saved tank (or creates a new population)
 * and lives through the time since the save was written - since 2026-09-16
 * the same as progression_wake with the port's clock (the cold-boot ravenous
 * rule is gone: a cell that dies in the night is a power-off, and that
 * morning deserves its night too). Returns the hours lived through, -1 for
 * none. */
float progression_boot(tank_t *t);
/* the device waking from DEEP sleep (2026-09-14): restore the save, then live
 * through the dark stretch since it was written - tank_tick_sleep for
 * (now_unix - saved_unix), capped at PROGRESSION_SLEEP_CAP_S. Offline time
 * gives quiet growth and rest, never upkeep or neglect penalties. Returns the hours
 * simulated, or -1 when no clock was available (restore only). */
#define PROGRESSION_SLEEP_CAP_S (7 * 24 * 3600)
/* how much of a slept span the fish grow through (2026-09-14, Strato: "an
 * eight-hour sleep should be worth 2 hours of growth") */
#define SLEEP_GROWTH_FRAC 0.25f
float progression_wake(tank_t *t, int64_t now_unix);
/* Live through device sleep with tank_tick_sleep and growth at
 * SLEEP_GROWTH_FRAC. FULL_NIGHT_S remains a historical duration constant. */
#define FULL_NIGHT_S (6 * 3600.0f)
void progression_slept(tank_t *t, float seconds);
/* the settings page changed something that rides in the save (the light's
 * idle time / auto-off): an event save follows, coalesced like the others */
void progression_settings_changed(void);
/* call every frame after tank_tick */
void progression_tick(tank_t *t, float dt);
/* call on light-off / shutdown (autosaves on events + heartbeat anyway) */
/* Save the current tank. False means the port rejected the write; the state
 * remains dirty so the periodic saver will retry. */
bool progression_save(tank_t *t);
/* Caller frees the JSON string. Import validates fully before committing NVS;
 * restart after success so all runtime state is rebuilt through the normal loader. */
char *progression_backup_json(const tank_t *t);
bool progression_restore_json(const char *json, size_t len, bool commit, char *error, size_t error_size);
bool progression_backup_preview(const char *json, size_t len, char name[16], uint32_t *release);

/* sim/debug: multiply time (aging, drift) - `./fishsim --fast 60` */
extern float progression_time_scale;
/* debug: stage and show an arrival now (sim key R); no-op at the cap */
void progression_force_arrival(tank_t *t);
/* debug: stage an arrival as if its last gate had just closed - the
 * spawning follows (or progression_force_arrival, at once) */
void progression_stage_arrival(tank_t *t);
/* the spawning (2026-09-24, Strato: "new fry comes at next light on" felt
 * unintuitive - there's no real need to turn the light on or off): once an
 * arrival is staged, SPAWN_WAIT_MIN_S..SPAWN_WAIT_MAX_S awake seconds pass
 * (not at once), then the courting pair swims down into a sheltered refuge;
 * after SPAWN_DANCE_S of circling there together the fry is born between
 * them. Lit or dark. The wait and the dance hold while a page covers the
 * tank (tank_t.ui_cover) or no bed is tall enough to be born in. */
#define SPAWN_WAIT_MIN_S  8.0f
#define SPAWN_WAIT_MAX_S 20.0f   /* + the swim down: the courtship starts within ~30 s */
#define SPAWN_DANCE_S    10.0f
/* the device woke (a deep-sleep boot, or the grace's quick wake): a staged
 * fry that missed its live birth is born now, by its refuge */
void progression_woke(tank_t *t);
bool progression_arrival_pending(void);
float progression_age_s(const tank_t *t, int idx);        /* grown seconds */
/* director/debug: put a fish's tended clock at `seconds` and apply the stage
 * and size that go with it now (the silent surprise, on cue) */
void progression_set_age(tank_t *t, int idx, float seconds);
/* director/debug: the new-tank path on a tank_init'd tank - two fry, first
 * milestones, nothing tended yet. Saves over the current save on the next
 * heartbeat: stash it first (firmware director: `stash`). */
void progression_fresh(tank_t *t);
/* Settings > WIPE, then confirm: every tank save is erased, parked copies too.
 * tank is re-initialised with `seed`, the new-tank path runs and the fresh
 * pair is saved at once, so a reboot lands on them. Two fry, clean glass,
 * the default garden, nothing tended yet. False reports an erase/save failure;
 * an erase failure leaves the live tank unchanged. SD files are not erased. */
/* True means the durable wipe committed. A failed first fresh-save write is
 * retried and reported by progression_hard_status, not a failed/rolled-back wipe. */
bool progression_reset(tank_t *t, uint32_t seed);
/* first-run setup (setup.c, 2026-09-13): a tank born through
 * progression_fresh - a fresh install, a reset - is PENDING setup until the
 * keeper walks the welcome / names / colours flow; the flag rides in the
 * save, so a reboot mid-setup re-opens it. progression_setup_done clears it
 * and saves the names and looks at once. Tanks saved before the flag read as
 * done (nobody's running tank gets the tutorial after an update). */
bool progression_setup_pending(void);
bool progression_setup_done(tank_t *t);
/* the birth flow (setup.c, 2026-09-14): every arrival is owed a visit - the
 * announcement, a name, the family page - until the keeper walks it.
 * progression_newborn is the slot still owed (-1 = none); it rides in the
 * save, so a reboot mid-flow brings it back. progression_newborn_done clears
 * it and saves the name at once. */
int  progression_newborn(void);
bool progression_newborn_done(tank_t *t);
/* milestones page (2026-09-13): the keeper closed it - everything earned so
 * far counts as seen (badges earned later wear a "new" ring until the next
 * look). Rides in the save. */
void progression_ack_milestones(tank_t *t);
/* the new-fry checklist (2026-09-14, Strato: once the pair starts to grow,
 * the first thing a keeper asks is "how do I get a new fry?" - and the
 * milestones page said nothing). progression_next_fry fills one line per
 * gate of the NEXT arrival: the care gates arrival_conditions counts (the
 * same code, so the list can never disagree with the rule - the population's
 * gates, then the GLASS gate every arrival needs: no fry is conceived in a
 * dirty tank, film on more than ALGAE_DIRTY of the glass, 2026-09-16) plus
 * the nursery bed every arrival needs. Each line: a kind (the renderer
 * picks the art), a title, the words (what to do - a plain sentence over two lines; Strato:
 * "all fish must have a minimum six out of 10 trust score", not a hint), a
 * progress phrase (where it stands), a 0..1 fraction and whether it is met. Returns the count, 0 at the
 * population cap. *staged = every gate is met and the fry is on its way
 * (the spawning). Strings fit the pixel font: <= 25 chars at scale 2. */
enum { FRY_REQ_TRUST, FRY_REQ_FEED, FRY_REQ_HOLD, FRY_REQ_GROW, FRY_REQ_CHANGE, FRY_REQ_GRASS, FRY_REQ_GLASS };
#define FRY_REQ_MAX 5
typedef struct {
    int   kind;
    char  title[12];
    char  words[28], words2[28];   /* what to do, a plain sentence over two lines */
    char  progress[28];            /* where it stands */
    float frac;
    bool  met;
} fry_req_t;
int progression_next_fry(const tank_t *t, fry_req_t out[FRY_REQ_MAX], bool *staged);
/* the tip behind a gate (Strato: "if someone reads 'all fish must have a
 * trust of at least six' they may ask OK how do I do that?"): HOW the
 * keeper moves it, in up to FRY_TIP_LINES lines of <= 26 chars, NULL-
 * terminated. Written from the rules in tank.c / progression.c (trust
 * only rises under a resting finger; quick taps cost it; fish age only
 * while lit; the starter rock always provides a free nursery refuge). */
#define FRY_TIP_LINES 5
const char *const *progression_fry_tip(int kind);

/* Births, daily care, games and trading provide income. Stage/trust firsts
 * no longer pay; their constants and ledgers remain for save compatibility. */
#define SD_STAGE_JUV   5
#define SD_STAGE_ADULT 10
#define SD_STAGE_ELDER 25
#define SD_BIRTH       20
#define SD_TRUST       15
#define SD_CHORE_EVERY 100
#define SD_TRIM_CM     250
#define SD_PRICE_PLANT 40
#define SD_PRICE_SNAIL 80
#define SD_PRICE_MUSSEL 120
#define SD_PRICE_CASTLE 150
#define SD_PRICE_CORAL 100
#define SD_PRICE_CLUSTER 240
#define SD_PRICE_SHRIMP 180       /* 2026-09-29, Strato (first 300, then 180) */
#define SD_PRICE_URCHIN 120       /* ornamental sand wanderer */
#define SD_PRICE_SEAHORSE 220     /* 0.3.6, Strato 2026-10-15: between the shrimp (180) and the cluster (240) */
#define SD_PRICE_PUMPKIN 75       /* 0.3.6, Strato 2026-10-17: the seasonal lantern, "affordable" */
typedef struct {
    uint32_t    bit;               /* SD_ITEM_*, zero for separately stored fish-care flags */
    const char *name;              /* shop label, the pixel font */
    const char *words, *words2;    /* what it does, two lines of <= 28 chars */
    int         price;
} sd_item_t;
extern const sd_item_t SD_ITEMS[SD_CATALOG_COUNT];
/* Foliage IDs never enter legacy ownership masks or decoration inventory slots. */
bool progression_foliage_planted(const tank_t *t, int item);
/* Stable IDs stay fixed in saves; only the shop's presentation is price-sorted. */
int progression_shop_item(const tank_t *t, int row);
int progression_shop_count(const tank_t *t);
/* For ground-cover sets, owned/unlocked means purchased, even while stored. */
bool progression_item_owned(const tank_t *t, int item);
/* the last shop save failure ("SAVE FAILED - NOT BOUGHT" / "... NOT SOLD"),
 * after the purchase or sale was rolled back; NULL = none. The shop page
 * shows it until the next tap clears it. */
const char *progression_shop_notice(void);
void progression_shop_notice_clear(void);
#define SD_PRICE_CHEST 120
#define SD_PRICE_SHELL 60
#define SD_PRICE_STARFISH 60
#define SD_PRICE_ANCHOR 80
#define SD_PRICE_BUOY 80
#define SD_PRICE_SAND 300
#define SD_PRICE_LIGHTS 300
/* Ground-cover sets buy-and-plant once, then toggle stored/planted for free.
 * Every action saves transactionally; sets never sell or occupy inventory.
 * Other items: false when the balance is short or it is already owned;
 * true = unlocked, placed in the tank (tank_plant_place / tank_snail_place)
 * and saved at once */
bool progression_buy(tank_t *t, int item);
int progression_item_price_for(const tank_t *t, int item);
/* Retired IDs 8, 30 and 31 remain reserved for old saves; never reassign them.
 * Paid local copies receive the normal sell value once on migration. */
#define SD_PUMPKIN_IN_SHOP 0
bool progression_pumpkin_offered(const tank_t *t);
/* the sale back (2026-09-24, Strato): a placeable piece sells for SD_SELL_PCT
 * of its price (the snail is a permanent resident), goes back to the shop at
 * full price and to its factory state; false = not owned or not for sale.
 * The refund is balance, not earnings; the toast shows it. */
#define SD_SELL_PCT 20
int  progression_sell_value(int item);
/* Refund eligibility is local: free unlocked placements always return zero. */
int  progression_sell_value_for(const tank_t *t, int item);
bool progression_sell(tank_t *t, int item);
/* Stored counts are shared across aquariums; primary/extra placements are local.
 * copy = -1 selects the legacy primary, otherwise an inventory.extra index.
 * Free copies leave storage before paid copies. All mutations save atomically. */
bool progression_inventory_buy(tank_t *t, int item);
bool progression_inventory_store(tank_t *t, int item, int copy);
bool progression_inventory_place(tank_t *t, int item);
bool progression_inventory_sell(tank_t *t, int item);
int progression_inventory_stored(const tank_t *t, int item);
int progression_inventory_placed(const tank_t *t, int item);
bool progression_inventory_duplicate(int item);
/* a fish sold (2026-10-01, Strato: a SELL button on the milestones page's
 * fish card - "the more mature a fish is, the more sand dollars it is
 * worth. once a fish is sold it opens up a slot for a new fry"). The price
 * is its stage's; the dollars are earnings (the toast shows them). The tank
 * always keeps FISH_KEEP_MIN fish - a pair to court - and a fry still owed
 * its welcome holds every sale until it has been named. The fish after it
 * move down a slot (tank_remove_fish), a staged arrival is called off, and
 * the slot is EARNED again: the next fry's MEALS gate waits for
 * SELL_FRY_MEALS meals more than the tank had eaten at the sale (the first
 * fry's dozen), so a grown tank - every other gate long met - does not hand
 * a fry straight back. Saved at once. */
#define SD_FISH_FRY    8
#define SD_FISH_JUV    20
#define SD_FISH_ADULT  40
#define SD_FISH_ELDER  64
#define FISH_KEEP_MIN  2
#define SELL_FRY_MEALS 12
/* The adult type market is always available in Normal/Hard. Its fixed
 * authored stock can form matching pairs without waiting for a refresh.
 * The older three-offer fry shop remains, with saved seed/sold/age fields:
 * each ask is progression_fry_price(), never the retired flat 200 price.
 * Bought fry still receive the naming welcome, no birth reward, and normal
 * later growth rewards. Both purchase paths reserve the traveler's place. */
#define SD_PRICE_FRY   200       /* historical flat price; never charge/display this */
#define FRY_OFFER_N    3
#define FRY_OFFER_S    (24 * 3600.0f)
typedef struct { uint32_t body, accent; bool sold; uint8_t variety, pattern; } fry_offer_t;
bool  progression_fry_for_sale(const tank_t *t);   /* Normal/Hard, no first-sale prerequisite */
bool  progression_fry_room(const tank_t *t);       /* a slot free and no welcome owed */
void  progression_fry_offer(const tank_t *t, int k, fry_offer_t *o);
int   progression_fry_left(void);                  /* fry not yet bought in this offer */
float progression_fry_new_in_s(void);              /* tank seconds until a fresh three */
bool  progression_buy_fry(tank_t *t, int k);
void  progression_fry_roll(tank_t *t);             /* director / tests: a fresh three now */
void  progression_fry_open(tank_t *t);             /* director / tests: the shop's fry, as if a fish was sold */
int  progression_fish_value(const tank_t *t, int fish);      /* what it would fetch now */
int  progression_fish_vigor(const tank_t *t, int fish);      /* hybrid vigor 0..10 */
int  progression_fish_gene_pct(const tank_t *t, int fish);   /* genetic price factor, percent */
int  progression_fish_care_quality(const tank_t *t, int fish); /* 0..40, -1 invalid */
const char *progression_fish_grade(int gene_pct);
/* Bred fish (generation >= 1) of the day's demand variety sell for 25% more;
 * pedigree adds 2% per generation (6% for RARE+ bloodlines), up to five. */
#define PROGRESSION_DEMAND_PCT 25
int  progression_demand_variety(const tank_t *t);
int  progression_fish_pedigree_pct(const tank_t *t, int fish);
/* Casino and other signed wallet changes; saves, reverts on failure, no-op in Creative. */
bool progression_wallet_settle(tank_t *t, int delta);
bool progression_collection_complete(const tank_t *t, int variety);
bool progression_statue_show(tank_t *t, int variety);   /* -1 hides it */
float progression_fish_growth_factor(const tank_t *t, int fish); /* favorite decor and vigor */
bool progression_fish_sellable(const tank_t *t, int fish);   /* false: the last pair, or a welcome is owed */
bool progression_sell_fish(tank_t *t, int fish);
/* Type premium is 100 + 12*variety percent. Condition adds up to 50% from
 * trust, energy, fullness and calm. Market asks exceed the best same-stage
 * resale by 40 (adult) / 20 (fry), so only breeding/growth can earn a profit. */
typedef struct {
    uint32_t id, roster_gen;
    int fish, value;
} fish_sale_t;
typedef struct {
    uint32_t body, accent;
    uint8_t variety, pattern;
    int price;
} fish_market_offer_t;
int progression_market_price(const tank_t *t, int variety);
void progression_market_offer(const tank_t *t, int variety, fish_market_offer_t *out);
bool progression_market_preview(const tank_t *t, int variety, fish_t *out);
/* NULL means purchasing is allowed; failures also set progression_shop_notice. */
const char *progression_market_status(const tank_t *t);
bool progression_market_buy(tank_t *t, int variety);
/* Ornamental stock: one snail, urchin and seahorse and one shrimp school per
 * aquarium; creatures do not breed. Colors are rolled at purchase and their
 * rarity scales the resale quote (half price x 1.0/1.4/2.0/3.5; a shrimp
 * school also scales with its size). Legacy pairs/nurseries are sold off
 * once at today's quote. Pearl mussels are an income colony, not stock. */
typedef struct {
    uint32_t revision;
    int species, value;
    uint8_t aquarium, offspring;
} creature_sale_t;
enum { CREATURE_RARITY_COUNT = 4 };
extern const char *const CREATURE_RARITY_NAMES[CREATURE_RARITY_COUNT];
int progression_creature_rarity(uint8_t colors);
int progression_creature_rarity_of(const tank_t *t, int species);
int progression_creature_adults(const tank_t *t, int species);
int progression_creature_offspring(const tank_t *t, int species);
int progression_creature_price(const tank_t *t, int species);
int progression_creature_value(const tank_t *t, int species, bool offspring);
int progression_creature_seconds(const tank_t *t, int species);
const char *progression_creature_status(const tank_t *t, int species);
bool progression_creature_buy(tank_t *t, int species);
bool progression_creature_sale_quote(const tank_t *t, int species, bool offspring, creature_sale_t *out);
bool progression_creature_sale_confirm(tank_t *t, const creature_sale_t *quote);
int progression_fry_price(const tank_t *t, int offer);
bool progression_fish_sale_quote(const tank_t *t, int fish, fish_sale_t *out);
bool progression_fish_sale_confirm(tank_t *t, const fish_sale_t *quote);
/* Breeding contracts: every 3 days a visitor orders a bred adult of one
 * unlocked variety that is FINE+, RARE+ or carries a mutation, paying the
 * quote x1.6 + 50 once per window. */
#define PROGRESSION_CONTRACT_DAYS 3
#define PROGRESSION_CONTRACT_PCT 160
#define PROGRESSION_CONTRACT_BONUS 50
enum { CONTRACT_NEED_FINE, CONTRACT_NEED_RARE, CONTRACT_NEED_MUTANT, CONTRACT_NEED_COUNT };
#define PROGRESSION_TRADE_CLIENT_COUNT 6
extern const char *const PROGRESSION_TRADE_CLIENTS[PROGRESSION_TRADE_CLIENT_COUNT];
typedef struct { int variety, need, client, days_left; bool filled; } fish_contract_t;
bool progression_contract_today(const tank_t *t, fish_contract_t *out);
bool progression_contract_fits(const tank_t *t, int fish);
int  progression_contract_pay(const tank_t *t, int fish);
bool progression_contract_quote(const tank_t *t, int fish, fish_sale_t *out);
bool progression_contract_fill(tank_t *t, const fish_sale_t *quote);
/* Auction day: Saturdays (unix day % 7 == 2), one RARE+ or 2nd-generation
 * adult a week sells to rival bidders for 95..165% of its quote, committed
 * before the ladder is revealed. */
#define PROGRESSION_AUCTION_WEEKDAY 2
#define PROGRESSION_AUCTION_BIDS 5
#define PROGRESSION_AUCTION_RESERVE_PCT 95
typedef struct {
    int opening, hammer, bids[PROGRESSION_AUCTION_BIDS];
    uint8_t bidder[PROGRESSION_AUCTION_BIDS];
} fish_auction_t;
int  progression_auction_days(const tank_t *t);      /* 0 = today */
bool progression_auction_sold(const tank_t *t);      /* this week's lot is gone */
bool progression_auction_open(const tank_t *t);
bool progression_auction_eligible(const tank_t *t, int fish);
int  progression_auction_reserve(const tank_t *t, int fish);
bool progression_auction_quote(const tank_t *t, int fish, fish_sale_t *out);
bool progression_auction_sell(tank_t *t, const fish_sale_t *quote, fish_auction_t *out);
/* A pair is two local healthy adult/elder fish of the same variety.
 * Banked fish and the traveler never breed or consume a second active slot. */
bool progression_breeding_pair(const tank_t *t, int *a, int *b);
const char *progression_breeding_status(const tank_t *t);
/* dollars awarded since the last call (the toast over the live tank) */
int  progression_sd_take_award(void);
/* director / tests: dollars from nowhere (negative takes them away) */
void progression_sd_grant(tank_t *t, int n);
bool progression_game_reward(tank_t *t, int game, int dollars);
/* the earn table for the shop page: one line per source, "+N WORDS" */
#define SD_EARN_LINES 7
const char *const *progression_sd_earn_lines(void);

/* Active population ceiling. CYD firmware and its smoke simulator use
 * -DPOP_CAP=5; generic native tests may use all six storage slots.
 * Never exceed N_FISH_MAX or enlarge the frozen save arrays. */
#ifndef POP_CAP
#define POP_CAP N_FISH_MAX
#endif

/* stage thresholds, in seconds of grown life: awake time in full (the light
 * makes no difference), sleep at SLEEP_GROWTH_FRAC. A desk companion is
 * glanced at over weeks, so the arc is hours/days, not minutes. */
/* tended seconds to each stage (2026-08-30, halved from 1/6/48 h: a fresh
 * tank now starts as FRY, so the first growth spurt lands in the keeper's
 * first session and elder stays a multi-day achievement). 0.3.5 (2026-10-14,
 * Strato: "all of the age stages should last longer, they happen quite fast
 * now"): four times as long - 2 h / 12 h / 4 days. Breeding now needs two
 * healthy adults of the same type, rather than the youngest resident's age.
 * A fish already past a stage under the old numbers
 * keeps it (apply_stage never takes a stage away). */
#define STAGE_JUV_AGE    (2 * 3600)
#define STAGE_ADULT_AGE  (12 * 3600)
#define STAGE_ELDER_AGE  (96 * 3600)
/* trait drift: one full unit (0.11 of the 0..1 trait) per this many hours of
 * sustained pressure */
#define DRIFT_HOURS      2.0f
/* Historical CHANGE threshold; retained for legacy requirement IDs/data. */
#define DRIFT_CHANGE     0.11f

/* milestone labels (UI / logs); order = bit order in tank.h */
extern const char *const MS_NAMES[MS_FISH_COUNT];
extern const char *const TMS_NAMES[TMS_COUNT];
#endif
