/* progression.h — care, growth, rewards and profile persistence.
 * Current CYD behavior is documented in docs/CYD-FEATURES.md. Platform-agnostic;
 * persistence and wall-clock come in through two tiny port functions.
 *
 *  - Population: a new tank is two contrasting fry; the 3rd..POP_CAP-th
 *    fish ARRIVE as fry when care milestones are met (trust, feedings, a
 *    calm hold, a raised fry, drift). An arrival is staged when earned; a
 *    few seconds later the parents court down in the nursery grass and the
 *    fry is born there, in front of the keeper (2026-09-24 - it used to wait
 *    for the next light-on). A tank put to sleep first has it at the wake.
 *  - Growth: well-fed fish grow (size) and advance fry -> juv -> adult -> elder
 *    with time - every awake second, light on or off (2026-09-14, Strato:
 *    "fish don't stop growing with the light off"), and a slept span at
 *    SLEEP_GROWTH_FRAC of its length; stage changes are silent surprises.
 *  - Trait drift: bold and social creep with experience (hours of pressure).
 *  - Milestones: firsts are DETECTED from what the fish actually did (the
 *    rules-driven behavior), per fish and per tank; render shows them.
 *  - Habits: the tank remembers where you feed and greets you at light-on.
 *  - Ravenous boot: powered off >= 1 h -> fish are starving on return and wait
 *    near the surface (at your usual feeding spot) until the first feeding.
 *    Hard starvation deaths accrue only while running, never offline or from
 *    old age. Normal and Creative do not kill fish.
 *  - Saves: on events (arrival, stage, milestone, light change), coalesced to
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
bool journal_variety_unlocked(const tank_t *t, int variety);
int journal_level(const tank_t *t);
uint32_t journal_level_xp(int level);
int journal_active(const tank_t *t, int slot);
unsigned journal_progress(const tank_t *t, int goal);
bool journal_claim(tank_t *t, int goal);
/* -1 bank index deposits; -1 active index withdraws; both indices swap.
 * NULL means saved successfully; otherwise a user-readable error. */
const char *progression_bank_move(tank_t *t, int bank, int active);
#define AQUARIUM_CAP 8
#define AQUARIUM_PRICE 500
#define AQUARIUM_SAVE_BYTES (6032 + sizeof(inventory_state_t) + sizeof(mussel_state_t) + sizeof(game_income_state_t) + 4 + sizeof(decor_content_t) + sizeof(decor_fixed_t))
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
 * (now_unix - saved_unix), capped at PROGRESSION_SLEEP_CAP_S. Hunger, energy,
 * grass and algae land where a night of drowse would have put them; a long
 * enough night ends in ravenous begging by itself. Returns the hours
 * simulated, or -1 when no clock was available (restore only). */
#define PROGRESSION_SLEEP_CAP_S (7 * 24 * 3600)
/* how much of a slept span the fish grow through (2026-09-14, Strato: "an
 * eight-hour sleep should be worth 2 hours of growth") */
#define SLEEP_GROWTH_FRAC 0.25f
float progression_wake(tank_t *t, int64_t now_unix);
/* live through one stretch of device sleep: tank_tick_sleep, growth at
 * SLEEP_GROWTH_FRAC, and the tank's "first full night's sleep" milestone once
 * a single stretch reaches FULL_NIGHT_S (progression_wake's path; the director's
 * `sleep H` stages the same). */
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
 * (not at once), then the courting pair swims down into the nursery grass;
 * after SPAWN_DANCE_S of circling there together the fry is born between
 * them. Lit or dark. The wait and the dance hold while a page covers the
 * tank (tank_t.ui_cover) or no bed is tall enough to be born in. */
#define SPAWN_WAIT_MIN_S  8.0f
#define SPAWN_WAIT_MAX_S 20.0f   /* + the swim down: the courtship starts within ~30 s */
#define SPAWN_DANCE_S    10.0f
/* the device woke (a deep-sleep boot, or the grace's quick wake): a staged
 * fry that missed its live birth is born now, in the grass */
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
 * while lit; grass regrows by itself; the glass is wiped by a drag). */
#define FRY_TIP_LINES 5
const char *const *progression_fry_tip(int kind);

/* Daily care is the repeatable income. Fish stage/birth/full-trust firsts
 * still pay once; frozen chore ledgers remain for save compatibility. */
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
#define SD_PRICE_URCHIN 120       /* 2026-10-02: the grass's snail (the snail is 80, the grass pays 25 per 250 cm) */
#define SD_PRICE_SEAHORSE 220     /* 0.3.6, Strato 2026-10-15: between the shrimp (180) and the cluster (240) */
#define SD_PRICE_PUMPKIN 75       /* 0.3.6, Strato 2026-10-17: the seasonal lantern, "affordable" */
typedef struct {
    uint32_t    bit;               /* SD_ITEM_*, zero for separately stored fish-care flags */
    const char *name;              /* <= 12 chars, the pixel font */
    const char *words, *words2;    /* what it does, two lines of <= 25 chars */
    int         price;
} sd_item_t;
extern const sd_item_t SD_ITEMS[SD_ITEM_COUNT];
/* Stable IDs stay fixed in saves; only the shop's presentation is price-sorted. */
int progression_shop_item(const tank_t *t, int row);
int progression_shop_count(const tank_t *t);
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
/* the shop's sale: false when the balance is short or it is already owned;
 * true = unlocked, placed in the tank (tank_plant_place / tank_snail_place)
 * and saved at once */
bool progression_buy(tank_t *t, int item);
int progression_item_price_for(const tank_t *t, int item);
/* the seasonal pumpkin (0.3.6): sold while SD_PUMPKIN_IN_SHOP - the first
 * release after October 31 sets it to 0 (Strato: no date check, a board
 * keeps selling it until it is updated). One bought stays in its tank, so
 * the shop keeps its row for the keeper who has one. */
#define SD_PUMPKIN_IN_SHOP 1
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
#define SD_FISH_FRY    5
#define SD_FISH_JUV    15
#define SD_FISH_ADULT  30
#define SD_FISH_ELDER  60
#define FISH_KEEP_MIN  2
#define SELL_FRY_MEALS 12
/* FRY FOR SALE (0.3.5, Strato: "when a player sells a fish for the first
 * time it should award a tank milestone badge and also open up fry
 * purchasing in the shop as long as there is at least one open slot"). The
 * first sale earns TMS_FIRST_SALE (a tank that sold before 0.3.5 gets it at
 * its first boot), and from then on the shop's first row is FRY: three fry,
 * each a look of its own, SD_PRICE_FRY apiece. A bought fry leaves a blank
 * where it stood - any number can be bought while slots and dollars last -
 * and nothing comes back until FRY_OFFER_S of tank time (awake or asleep,
 * no wall clock needed: the pendant may have none) brings a fresh three.
 * A bought fry is a newborn without parents: the birth flow welcomes it
 * (announce, name, family), no birth dollars, its stages pay as any fish's.
 * Taking the tank's last slot calls off a staged arrival.
 * The offer is a seed: each fry's look is a hash of it and its place, so
 * the save keeps two bytes for the seed, two for the clock, one for the
 * blanks - in padding 0.4 (main) leaves free. 0.3.x rolls the colors from
 * 0.4's nine (main: LOOK_COLORS), so a bought fry is an exact genome there;
 * 0.4 rolls the form (shape, fins, pattern) from the same hash. */
#define SD_PRICE_FRY   200
#define FRY_OFFER_N    3
#define FRY_OFFER_S    (24 * 3600.0f)
typedef struct { uint32_t body, accent; bool sold; uint8_t variety, pattern; } fry_offer_t;
bool  progression_fry_for_sale(const tank_t *t);   /* the first sale's badge is earned */
bool  progression_fry_room(const tank_t *t);       /* a slot free and no welcome owed */
void  progression_fry_offer(const tank_t *t, int k, fry_offer_t *o);
int   progression_fry_left(void);                  /* fry not yet bought in this offer */
float progression_fry_new_in_s(void);              /* tank seconds until a fresh three */
bool  progression_buy_fry(tank_t *t, int k);
void  progression_fry_roll(tank_t *t);             /* director / tests: a fresh three now */
void  progression_fry_open(tank_t *t);             /* director / tests: the shop's fry, as if a fish was sold */
int  progression_fish_value(const tank_t *t, int fish);      /* what it would fetch now */
bool progression_fish_sellable(const tank_t *t, int fish);   /* false: the last pair, or a welcome is owed */
bool progression_sell_fish(tank_t *t, int fish);
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
 * now"): four times as long - 2 h / 12 h / 4 days. The fry gates read the
 * same numbers (a new fry waits on the youngest growing up), so the tank
 * fills more slowly too. A fish already past a stage under the old numbers
 * keeps it (apply_stage never takes a stage away). */
#define STAGE_JUV_AGE    (2 * 3600)
#define STAGE_ADULT_AGE  (12 * 3600)
#define STAGE_ELDER_AGE  (96 * 3600)
/* trait drift: one full unit (0.11 of the 0..1 trait) per this many hours of
 * sustained pressure */
#define DRIFT_HOURS      2.0f
/* CHANGE gate: this much accumulated drift pressure on the
                                 * youngest fish (~26 lit minutes fed and calm, or following) */
#define DRIFT_CHANGE     0.11f

/* milestone labels (UI / logs); order = bit order in tank.h */
extern const char *const MS_NAMES[MS_FISH_COUNT];
extern const char *const TMS_NAMES[TMS_COUNT];
#endif
