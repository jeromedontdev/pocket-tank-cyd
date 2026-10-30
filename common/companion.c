#include "companion.h"
#include "weather.h"
#include "progression.h"
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

const char *const COMPANION_DISCOVERIES[] = {
    "Bubble watcher", "Little explorer", "Sheltered nap", "Swimming together",
    "A playful dart", "Reef visitor", "A hello from you", "A learned trick",
    "Lantern shell postcard", "Pearl bookmark postcard", "Star stone postcard", "Growing up"
};
const char *const COMPANION_COLLECTIONS[] = {"Nature frame", "Friendship frame", "Story frame"};
const char *const COMPANION_ADVENTURES[] = {"Lantern lagoon", "Pearl library", "Starlight cove"};
const uint16_t COMPANION_ADVENTURE_SECONDS[] = {7200, 21600, 43200};
unsigned companion_adventure_total(const companion_state_t *s, int adventure) {
    static const unsigned legacy[] = {120, 300, 600};
    return s->version < 2 ? legacy[adventure] : COMPANION_ADVENTURE_SECONDS[adventure];
}
static const uint32_t collection_masks[] = {0x3fu, 0xc0u, 0x700u};
static const uint16_t trick_bonds[] = {10, 25, 50};
const char *const WEAR_NAMES[WEAR_COUNT] = {"NONE", "TOP HAT", "PARTY HAT", "CROWN", "TIE", "BOWTIE", "BANDANA",
    "SAILOR CAP", "PIRATE HAT", "BERET", "SUN HAT", "VIKING HELM", "SCARF"};
const uint8_t WEAR_PRICES[WEAR_COUNT] = {0, 45, 20, 100, 35, 30, 20, 40, 65, 30, 45, 80, 25};

const companion_visitor_t COMPANION_VISITORS[VISITOR_SPECIES] = {
    {"DRIFTER", "An unknown drifter", "A pale bell that drifts past on its own errand.", 40},
    {"OLD TURTLE", "An old sea turtle", "Slow, scarred and unhurried. It has seen many reefs.", 22},
    {"GHOST FISH", "A ghost fish", "Almost glass. You see the reef through it.", 15},
    {"NIGHT MANTA", "A small manta", "Wide wings, a lazy glide, gone before you blink.", 15},
    {"LANTERN SQUID", "A lantern squid", "A tiny light that blinks in the open water.", 8},
};
/* 1.3 story: the family tree, departed names, the visitor log and the seasons. */
static bool line_empty(const fish_lineage_t *l) { fish_lineage_t zero = {0}; return !memcmp(l, &zero, sizeof zero); }
void companion_story_init(tank_t *t) {
    tank_story_t *s = &t->story;
    if (s->version) return;
    memset(s, 0, sizeof *s);
    s->version = 1;
    s->seen[VISITOR_DRIFTER] = t->polish.visits > 0xffff ? 0xffff : (uint16_t)t->polish.visits;
    for (int i = 0; i < t->n_fish; i++) {          /* the slot links an older save kept for this tank's fry */
        const fish_t *f = &t->fish[i];
        int pa = f->parent_a, pb = f->parent_b;
        if (pa < 0 || pa >= t->n_fish || pa == i) continue;
        uint32_t id_a = t->polish.active[pa].id;
        uint32_t id_b = pb >= 0 && pb < t->n_fish && pb != i && pb != pa ?
            t->polish.active[pb].id : 0;
        s->active[i].parent[0] = id_a <= 0xffff ? (uint16_t)id_a : 0;
        s->active[i].parent[1] = id_b <= 0xffff ? (uint16_t)id_b : 0;
        s->active[i].generation = 1;
    }
    /* every fish already here gets hidden genes: carried markings, colours and traits */
    for (int i = 0; i < t->n_fish; i++) tank_seed_line(&s->active[i], t->polish.active[i].id ^ t->fish[i].color);
    for (int i = 0; i < t->bank_count; i++) tank_seed_line(&s->bank[i], t->polish.bank[i].id ^ t->bank[i].body);
    if (t->companion.away) tank_seed_line(&s->traveler, t->polish.traveler.id ^ t->companion.traveler.body);
    tank_sync_traits(t);
}
bool companion_story_valid(const tank_story_t *s, int fish, int bank, bool away, uint32_t next_id) {
    if (!s->version) {
        tank_story_t empty = {0};
        return !memcmp(s, &empty, sizeof empty);
    }
    if (s->version != 1 || (s->flags & ~3u) || s->last_species >= VISITOR_SPECIES || s->pad || s->statue > FISH_VARIETY_COUNT ||
        (s->statue && ((s->collection >> (3 * (s->statue - 1))) & 7u) != 7u) ||
        (s->collection >> (3 * FISH_VARIETY_COUNT))) return false;
    for (int i = 0; i < N_FISH_MAX + FISH_BANK_CAP + 1; i++) {
        const fish_lineage_t *l = i < N_FISH_MAX ? &s->active[i] :
            i < N_FISH_MAX + FISH_BANK_CAP ? &s->bank[i - N_FISH_MAX] : &s->traveler;
        bool present = i < N_FISH_MAX ? i < fish : i < N_FISH_MAX + FISH_BANK_CAP ? i - N_FISH_MAX < bank : away;
        if (!present ? !line_empty(l) : (l->flags || l->parent[0] >= next_id || l->parent[1] >= next_id ||
                                        (l->parent[0] && l->parent[0] == l->parent[1]) ||
                                        l->pattern2 > FISH_PATTERN_COUNT || l->color2 > FISH_COLOR_N ||
                                        (l->mutation & ~7u))) return false;
    }
    for (int i = 0; i < STORY_NAME_BOOK; i++) {
        const ancestor_name_t *a = &s->names[i];
        if (!memchr(a->name, 0, sizeof a->name) || a->id >= next_id || (!a->id && a->name[0])) return false;
    }
    return true;
}
const char *companion_story_name(const tank_t *t, uint16_t id) {
    if (!id) return NULL;
    for (int i = 0; i < t->n_fish; i++) if (t->polish.active[i].id == id) return t->fish[i].name;
    for (int i = 0; i < t->bank_count; i++) if (t->polish.bank[i].id == id) return t->bank[i].name;
    if (t->companion.away && t->polish.traveler.id == id) return t->companion.traveler.name;
    for (int i = 0; i < STORY_NAME_BOOK; i++) if (t->story.names[i].id == id) return t->story.names[i].name;
    return NULL;
}
const fish_lineage_t *companion_story_line(const tank_t *t, uint16_t id) {
    if (!id) return NULL;
    for (int i = 0; i < t->n_fish; i++) if (t->polish.active[i].id == id) return &t->story.active[i];
    for (int i = 0; i < t->bank_count; i++) if (t->polish.bank[i].id == id) return &t->story.bank[i];
    if (t->companion.away && t->polish.traveler.id == id) return &t->story.traveler;
    return NULL;
}
static int s_force_species = -1, s_force_season = -1;   /* serial debug only */
static void season_tick(tank_t *t);
void companion_debug_visit(tank_t *t, int species) {
    s_force_species = species >= 0 && species < VISITOR_SPECIES ? species : -1;
    t->polish.next_visit_s = t->polish.viewed_s;
}
void companion_debug_season(tank_t *t, int season) {
    s_force_season = season >= 0 && season <= (SEASON_WINTER | SEASON_LIGHTS) ? season : -1;
    season_tick(t);
}
int companion_visitor_pick(tank_t *t) {
    tank_story_t *s = &t->story;
    uint32_t h = (t->rng ^ (t->polish.visits * 0x85ebca6bu)) * 0x9e3779b1u;
    h ^= h >> 15;
    int total = 0, sp = VISITOR_DRIFTER;
    for (int i = 0; i < VISITOR_SPECIES; i++) total += COMPANION_VISITORS[i].weight;
    int roll = (int)(h % (uint32_t)total);
    if (t->polish.visits > 1)                      /* the first sighting is always the drifter */
        for (int i = 0; i < VISITOR_SPECIES; i++) { if (roll < COMPANION_VISITORS[i].weight) { sp = i; break; } roll -= COMPANION_VISITORS[i].weight; }
    if (s_force_species >= 0) { sp = s_force_species; s_force_species = -1; }
    if (s->version) {
        if (s->seen[sp] < 0xffff) s->seen[sp]++;
        if (!s->first_seen_s[sp]) s->first_seen_s[sp] = t->companion.powered_s == UINT32_MAX ? UINT32_MAX : t->companion.powered_s + 1;
        s->last_species = (uint8_t)sp;
    }
    t->visitor_species = (uint8_t)sp;
    return sp;
}
/* Northern seasons by month (meteorological); SOUTH swaps them. The holiday
 * lights hang from mid-December to New Year's Day either way. */
int companion_season_for(int64_t local_unix, uint8_t flags) {
    if ((flags & STORY_SEASONS_OFF) || local_unix < 1600000000LL || local_unix > 4102444800LL) return SEASON_NONE;
    int64_t days = local_unix / 86400 + 719468;
    int64_t era = days / 146097, doe = days - era * 146097;
    int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100), mp = (5 * doy + 2) / 153;
    int day = (int)(doy - (153 * mp + 2) / 5 + 1), month = (int)(mp < 10 ? mp + 3 : mp - 9);
    int season = month >= 3 && month <= 5 ? SEASON_SPRING : month >= 6 && month <= 8 ? SEASON_SUMMER :
                 month >= 9 && month <= 11 ? SEASON_AUTUMN : SEASON_WINTER;
    if (flags & STORY_SEASONS_SOUTH) season = (season + 1) % 4 + 1;
    if ((month == 12 && day >= 15) || (month == 1 && day == 1)) season |= SEASON_LIGHTS;
    return season;
}
static void season_tick(tank_t *t) {
    int64_t now = clock_port_now_unix();
    if (now > 0) now += (int64_t)t->aquariums.clock_utc_quarters * 900;
    bool south = false;
    bool located = weather_location_south(&south);
    uint8_t flags = t->story.flags & STORY_SEASONS_OFF;
    if (south) flags |= STORY_SEASONS_SOUTH;
    t->season = s_force_season >= 0 ? (uint8_t)s_force_season :
                located ? (uint8_t)companion_season_for(now, flags) : SEASON_NONE;
}
bool companion_set_seasons(tank_t *t, uint8_t flags) {
    if (!t->story.version || (flags & ~3u)) return false;
    uint8_t old = t->story.flags;
    t->story.flags = flags;
    if (!progression_save(t)) { t->story.flags = old; return false; }
    season_tick(t);
    return true;
}

const char *companion_subtitle(unsigned value) {
    static const char *const names[COMPANION_SUBTITLE_COUNT] = {
        "", "A little ocean", "A quiet refuge", "A reef of friends", "Small wonders live here",
        "Where the tide rests", "Lanterns under glass", "A harbor for small fins", "Moonlit shallows",
        "The breeding grounds", "A garden of currents", "Pearls and patience", "Home of the tidekeepers"};
    return names[value < COMPANION_SUBTITLE_COUNT ? value : 0];
}
const char *companion_emblem(unsigned value) {
    static const char *const names[COMPANION_EMBLEM_COUNT] = {
        "REEF", "SHELL", "PEARL", "STAR", "ANCHOR", "CRYSTAL", "BEACON", "HELM", "BELL", "LANTERN", "GATE", "MOON"};
    return names[value < COMPANION_EMBLEM_COUNT ? value : 0];
}
void companion_memory(tank_t *t, int kind, int fish, int detail) {
    tank_polish_t *p = &t->polish;
    memmove(p->recap + 1, p->recap, (TANK_RECAP_CAP - 1) * sizeof p->recap[0]);
    tank_memory_t *m = &p->recap[0];
    memset(m, 0, sizeof *m);
    m->at_s = t->companion.powered_s; m->kind = (uint8_t)kind; m->detail = (uint8_t)detail;
    if (fish >= 0 && fish < t->n_fish) snprintf(m->name, sizeof m->name, "%s", t->fish[fish].name);
    if (p->recap_count < TANK_RECAP_CAP) p->recap_count++;
    progression_settings_changed();
}
void companion_memory_text(const tank_memory_t *m, char *text, size_t size) {
    const char *who = m->name[0] ? m->name : "A fish";
    switch (m->kind) {
        case MEMORY_ARRIVAL: snprintf(text, size, "%s joined the aquarium.", who); break;
        case MEMORY_GROWTH: snprintf(text, size, "%s reached a new life stage.", who); break;
        case MEMORY_BUDDY: snprintf(text, size, "%s made a familiar friend.", who); break;
        case MEMORY_DISCOVERY: snprintf(text, size, "Discovered: %s.", COMPANION_DISCOVERIES[m->detail]); break;
        case MEMORY_ADVENTURE: snprintf(text, size, "%s brought home a keepsake.", who); break;
        case MEMORY_HELPERS: snprintf(text, size, "Helpers quietly tended the tank."); break;
        case MEMORY_VISITOR: snprintf(text, size, "%s visited the reef.", m->detail ? COMPANION_VISITORS[m->detail % VISITOR_SPECIES].memory : "An unknown drifter"); break;
        case MEMORY_RPG: snprintf(text, size, "%s helped restore current %u.", who, (unsigned)m->detail + 1); break;
        case MEMORY_DEATH: snprintf(text, size, "%s died after prolonged starvation.", who); break;
        default: snprintf(text, size, "No observations yet."); break;
    }
}
static void life_init(tank_t *t, fish_life_t *life, const fish_t *fish, bool arrived) {
    if (life->id) return;
    tank_polish_t *p = &t->polish;
    if (!p->next_id) p->next_id = 1;
    life->id = p->next_id++;
    life->arrived_s = arrived ? t->companion.powered_s : UINT32_MAX;
    if (fish) { life->stage = (uint8_t)fish->stage; life->milestones = fish->ms_bits; }
}
void companion_polish_init(tank_t *t, bool fresh) {
    tank_polish_t *p = &t->polish;
    bool migrate = !p->version;
    if (migrate) {
        memset(p, 0, sizeof *p);
        p->version = 1; p->next_id = 1;
        p->next_visit_s = 21600 + t->rng % 21601;
        if (fresh) p->founded_unix = clock_port_now_unix();
        p->decor_owned = t->sd_unlocks;
        p->helper_meals = t->shrimp_eaten;
    }
    for (int i = 0; i < t->n_fish; i++) life_init(t, &p->active[i], &t->fish[i], fresh);
    for (int i = 0; i < t->bank_count; i++) {
        life_init(t, &p->bank[i], NULL, false);
        p->bank[i].stage = t->bank[i].fish.stage;
        p->bank[i].milestones = t->bank[i].fish.ms_bits;
    }
    if (t->companion.away) {
        life_init(t, &p->traveler, NULL, false);
        p->traveler.stage = t->companion.traveler.fish.stage;
        p->traveler.milestones = t->companion.traveler.fish.ms_bits;
    }
    companion_story_init(t);
    tank_sync_traits(t);
    season_tick(t);
    for (int i = 0; i < t->n_fish; i++) for (int j = 0; j < t->n_fish; j++) {
        if (i != j && p->active[i].buddy == p->active[j].id && p->active[i].friend_s >= 30) {
            t->fish[i].familiar_id[j] = t->fish[j].social_id;
            t->fish[i].familiar[j] = p->active[i].friend_s;
        }
    }
}
bool companion_polish_valid(const tank_polish_t *p, int fish, int bank, bool away) {
    if (!p->version) {
        tank_polish_t empty = {0};
        return !memcmp(p, &empty, sizeof empty);
    }
    if (p->version != 1 || !p->next_id || p->subtitle >= COMPANION_SUBTITLE_COUNT || p->emblem >= COMPANION_EMBLEM_COUNT ||
        p->recap_count > TANK_RECAP_CAP || (p->scenery_unlocks & ~63u) ||
        p->founded_unix < 0 || p->founded_unix > 4102444800LL ||
        p->backup_unix < 0 || p->backup_unix > 4102444800LL ||
        !memchr(p->tank_name, 0, sizeof p->tank_name)) {
        fprintf(stderr, "Invalid polish header (version %u, next identity %lu)\n", p->version, (unsigned long)p->next_id); return false;
    }
    uint32_t ids[N_FISH_MAX + FISH_BANK_CAP + 1]; int count = 0;
    for (int i = 0; i < N_FISH_MAX + FISH_BANK_CAP + 1; i++) {
        const fish_life_t *f = i < N_FISH_MAX ? &p->active[i] :
            i < N_FISH_MAX + FISH_BANK_CAP ? &p->bank[i - N_FISH_MAX] : &p->traveler;
        bool present = i < N_FISH_MAX ? i < fish :
            i < N_FISH_MAX + FISH_BANK_CAP ? i - N_FISH_MAX < bank : away;
        if (!present) {
            fish_life_t empty = {0};
            if (memcmp(f, &empty, sizeof empty)) {
                fprintf(stderr, "Unexpected life record in empty slot %d\n", i); return false;
            }
            continue;
        }
        if (!f->id || f->id >= p->next_id || f->buddy >= p->next_id || f->buddy == f->id ||
            f->friend_s > 3600 || f->stage > STAGE_ELDER || (f->adventures & ~7u)) {
            fprintf(stderr, "Invalid fish life %d (identity %lu, buddy %lu, next %lu, familiarity %u)\n",
                    i, (unsigned long)f->id, (unsigned long)f->buddy, (unsigned long)p->next_id, f->friend_s); return false;
        }
        for (int j = 0; j < count; j++) if (ids[j] == f->id) return false;
        ids[count++] = f->id;
    }
    for (int i = 0; i < TANK_RECAP_CAP; i++) {
        const tank_memory_t *m = &p->recap[i];
        if (m->pad || !memchr(m->name, 0, sizeof m->name) ||
            (i < p->recap_count && (m->kind < MEMORY_ARRIVAL || m->kind > MEMORY_DEATH ||
             (m->kind == MEMORY_DISCOVERY && m->detail >= COMPANION_DISCOVERY_COUNT) ||
             (m->kind == MEMORY_VISITOR && m->detail >= VISITOR_SPECIES) ||
             (m->kind == MEMORY_RPG && m->detail >= 6)))) return false;
    }
    return true;
}
void companion_care_text(const tank_t *t, int i, char *text, size_t size) {
    if (i < 0 || i >= t->n_fish) { snprintf(text, size, "No fish at home; check Bank or traveler."); return; }
    const fish_t *f = &t->fish[i];
    if (f->hunger >= 7) snprintf(text, size, "Hungry: a little food would help.");
    else if (f->stress >= 6) snprintf(text, size, "Unsettled: quiet viewing helps.");
    else if (f->energy <= 3) snprintf(text, size, "Tired: taking a quiet breather.");
    else if (f->goal.id == GOAL_REST) {
        int spot = tank_favorite_decor(t, i);
        snprintf(text, size, "Resting near %s.", spot >= 0 ? SD_ITEMS[spot].name : "the reef");
    } else if (f->goal.id == GOAL_FOLLOW_FRIEND) {
        int buddy = tank_familiar_friend(t, i);
        snprintf(text, size, "Keeping close to %s.", buddy >= 0 ? t->fish[buddy].name : "a tankmate");
    } else if (f->goal.id == GOAL_INSPECT_REEF) snprintf(text, size, "Investigating a favorite reef spot.");
    else if (f->goal.id == GOAL_VISIT_BUBBLES) snprintf(text, size, "Watching the bubble column.");
    else if (f->goal.id == GOAL_DART_PLAY) snprintf(text, size, "Playful; enjoying a little dart.");
    else snprintf(text, size, "Content; exploring at its own pace.");
}
bool companion_identity(tank_t *t, bool emblem) {
    uint8_t old = emblem ? t->polish.emblem : t->polish.subtitle;
    if (emblem) t->polish.emblem = (old + 1) % COMPANION_EMBLEM_COUNT;
    else t->polish.subtitle = (old + 1) % COMPANION_SUBTITLE_COUNT;
    if (progression_save(t)) return true;
    if (emblem) t->polish.emblem = old; else t->polish.subtitle = old;
    return false;
}
bool companion_backup_record(tank_t *t) {
    int64_t now = clock_port_now_unix(), old = t->polish.backup_unix;
    uint32_t count = t->polish.backups;
    t->polish.backup_unix = now > 0 ? now : 0;
    if (t->polish.backups < UINT32_MAX) t->polish.backups++;
    if (progression_save(t)) return true;
    t->polish.backup_unix = old; t->polish.backups = count; return false;
}
void companion_date_text(int64_t when, char *text, size_t size) {
    time_t raw = (time_t)when;
    const struct tm *date = when > 0 ? gmtime(&raw) : NULL;
    if (!date) snprintf(text, size, "UNKNOWN");
    else snprintf(text, size, "%04d-%02d-%02d UTC", date->tm_year + 1900, date->tm_mon + 1, date->tm_mday);
}
static void polish_tick(tank_t *t, uint32_t seconds) {
    tank_polish_t *p = &t->polish;
    if (!p->version) companion_polish_init(t, false);
    for (int i = 0; i < t->n_fish; i++) {
        fish_life_t *life = &p->active[i]; const fish_t *f = &t->fish[i];
        if (!life->id) { life_init(t, life, f, true); companion_memory(t, MEMORY_ARRIVAL, i, 0); }
        if (f->stage > life->stage) {
            life->stage = (uint8_t)f->stage; companion_memory(t, MEMORY_GROWTH, i, f->stage);
        }
        life->milestones |= f->ms_bits;
        int buddy = tank_familiar_friend(t, i);
        if (buddy >= 0 && p->active[buddy].id) {
            uint16_t familiarity = f->familiar[buddy] > 3600 ? 3600 : f->familiar[buddy];
            if (life->buddy != p->active[buddy].id) {
                life->buddy = p->active[buddy].id; life->friend_s = familiarity;
                companion_memory(t, MEMORY_BUDDY, i, 0);
            } else if (familiarity > life->friend_s) {
                if (familiarity / 60 != life->friend_s / 60) progression_settings_changed();
                life->friend_s = familiarity;
            }
        }
    }
    uint32_t added = t->sd_unlocks & ~p->decor_owned;
    for (int i = 0; i < 32; i++) if (added & (1u << i))
        p->decor_since[i] = t->companion.powered_s == UINT32_MAX ? UINT32_MAX : t->companion.powered_s + 1;
    if (added || (p->decor_owned & ~t->sd_unlocks)) {
        p->decor_owned = t->sd_unlocks; progression_settings_changed();
    }
    if (!seconds || t->ui_cover || !t->light_on) return;
    uint32_t old = p->viewed_s;
    p->viewed_s = UINT32_MAX - old < seconds ? UINT32_MAX : old + seconds;
    if (old / 600 != p->viewed_s / 600) {
        if ((uint32_t)t->shrimp_eaten > p->helper_meals)
            companion_memory(t, MEMORY_HELPERS, -1, 0);
        p->helper_meals = t->shrimp_eaten;
    }
    if (p->viewed_s >= p->next_visit_s && !t->visitor_left && p->next_visit_s != UINT32_MAX) {
        t->visitor_left = 24;
        if (p->visits < UINT32_MAX) p->visits++;
        int species = companion_visitor_pick(t);
        uint32_t wait = 28800 + (t->rng ^ p->visits * 0x9e3779b9u) % 57601;
        p->next_visit_s = UINT32_MAX - p->viewed_s < wait ? UINT32_MAX : p->viewed_s + wait;
        companion_memory(t, MEMORY_VISITOR, -1, species);
    }
}
void companion_polish_observe(tank_t *t) { polish_tick(t, 0); }
void companion_init(tank_t *t) {
    memset(&t->companion, 0, sizeof t->companion);
    memset(&t->games, 0, sizeof t->games);
    memset(&t->polish, 0, sizeof t->polish);
    memset(&t->story, 0, sizeof t->story);
    memset(&t->rpg, 0, sizeof t->rpg);
    memset(&t->creative, 0, sizeof t->creative);
    memset(&t->games_extra, 0, sizeof t->games_extra);
    memset(&t->profile, 0, sizeof t->profile);
    t->visitor_left = 0;
    t->companion.version = COMPANION_VERSION;
    t->companion.cozy = t->companion.captions = 1;
    t->companion.display_compact = TANK_W == TANK_MAX_W;
    t->companion.lively = 0;
    t->companion.decor = 0;
    t->companion_fraction = 0;
}

typedef struct { const char *prompt, *choice[3]; } rpg_encounter_t;
static const char *const rpg_chapters[7] = {
    "THE WHISPERING SHELL", "THE KELP CROSSING", "THE GLASS GARDEN",
    "THE LANTERN SHOAL", "THE ANCESTORS' ARCH", "THE HEART OF THE CURRENT",
    "A CURRENT SHARED"
};
static const rpg_encounter_t rpg_encounters[18] = {
    {"A shell hums with a current nobody remembers. How will your fish answer?",
     {"COURAGE: Answer with a finbeat", "CURIOSITY: Study the shell", "KINDNESS: Listen to its worry"}},
    {"Sand hides the first waymark. A tiny crab guards a loose stone.",
     {"COURAGE: Lift the stone", "CURIOSITY: Trace sand lines", "KINDNESS: Help the little crab"}},
    {"The shell offers a map or a promise. Choose what your journey will carry.",
     {"COURAGE: Carry a beacon", "CURIOSITY: Keep the old map", "KINDNESS: Promise to return"}},
    {"Kelp knots the crossing. A small opening bends away from the marked path.",
     {"COURAGE: Part the fronds", "CURIOSITY: Follow the opening", "KINDNESS: Untangle the roots"}},
    {"A shy snail knows a shortcut, but rushing might miss its quiet directions.",
     {"COURAGE: Mark a direct trail", "CURIOSITY: Read the leaf marks", "KINDNESS: Wait with the snail"}},
    {"Two currents meet at the kelp gate. Decide what the crossing should become.",
     {"COURAGE: Open a clear channel", "CURIOSITY: Map a winding path", "KINDNESS: Make a sheltered way"}},
    {"Glass flowers reflect unfamiliar colours. One reflection moves on its own.",
     {"COURAGE: Greet the reflection", "CURIOSITY: Compare markings", "KINDNESS: Offer it some shade"}},
    {"An old garden wheel has stopped. Its keeper remembers only half a song.",
     {"COURAGE: Turn the wheel", "CURIOSITY: Find missing notes", "KINDNESS: Sing along softly"}},
    {"The garden can shine as a landmark or shelter small travellers.",
     {"COURAGE: Raise a bright marker", "CURIOSITY: Align glass petals", "KINDNESS: Grow a quiet refuge"}},
    {"The lantern shoal has lost its rhythm. Distant visitors leave faint lights.",
     {"COURAGE: Lead a new rhythm", "CURIOSITY: Follow the lights", "KINDNESS: Match the slow fish"}},
    {"A young lantern drifts behind. The safe path is longer than the bright one.",
     {"COURAGE: Guide from the front", "CURIOSITY: Test light trails", "KINDNESS: Stay by the lantern"}},
    {"The shoal asks who its lights should guide when the current returns.",
     {"COURAGE: Light the open sea", "CURIOSITY: Chart distant reefs", "KINDNESS: Welcome wanderers"}},
    {"Names echo beneath the ancestors' arch. Familiar family marks catch the light.",
     {"COURAGE: Add your own story", "CURIOSITY: Read family marks", "KINDNESS: Honour past helpers"}},
    {"An unfinished bridge waits beneath the arch. No ancestor left every answer.",
     {"COURAGE: Place the first stone", "CURIOSITY: Study old journeys", "KINDNESS: Build a rest ledge"}},
    {"The arch offers a legacy: a route for explorers or a homeward welcome.",
     {"COURAGE: Leave a trail banner", "CURIOSITY: Keep the waymarks", "KINDNESS: Keep a welcome light"}},
    {"At the heart, the lost current waits. It needs a direction, not a battle.",
     {"COURAGE: Give it momentum", "CURIOSITY: Find its source", "KINDNESS: Ask where it belongs"}},
    {"Old waymarks join into one circle. Every small contribution has brought you here.",
     {"COURAGE: Join the channels", "CURIOSITY: Balance the flow", "KINDNESS: Gather travellers"}},
    {"The current wakes. What kind of ocean will your companions leave behind?",
     {"COURAGE: An ocean of journeys", "CURIOSITY: An ocean of wonders", "KINDNESS: An ocean of welcome"}}
};
bool companion_rpg_valid(const tank_rpg_t *s, uint32_t next_id) {
    if (!s->version) { tank_rpg_t empty = {0}; return !memcmp(s, &empty, sizeof empty); }
    if (s->version != 1 || s->chapter > 6 || s->step > 2 ||
        (s->chapter == 6 && s->step) || s->outcome > 6 ||
        (s->routes >> s->chapter) || s->participant >= next_id) return false;
    unsigned actions = s->chapter * 3 + s->step, total = 0;
    for (int i = 0; i < 3; i++) {
        if (s->skills[i] > 18) return false;
        total += s->skills[i];
    }
    /* A profile's story survives moving to a different physical family. Its
     * unavailable participant is explicitly unknown, never a reused slot ID. */
    if (total != actions || (!!s->outcome != !!actions) || (!actions && s->participant)) return false;
    for (int i = 0; i < 6; i++) {
        if (s->contributors[i] >= next_id || (i >= s->chapter && s->contributors[i])) return false;
    }
    return true;
}
const char *companion_rpg_chapter_name(const tank_t *t) {
    return rpg_chapters[t->rpg.chapter <= 6 ? t->rpg.chapter : 0];
}
const char *companion_rpg_chapter_title(int chapter) {
    return rpg_chapters[chapter >= 0 && chapter <= 6 ? chapter : 0];
}
const char *companion_rpg_route(const tank_t *t, int chapter) {
    static const char *const open[6] = {
        "BEACON AND MAP", "EXPLORERS' CROSSING", "GLASS LANDMARK",
        "DISCOVERY LIGHTS", "TRAIL OF WAYMARKS", "OCEAN OF JOURNEYS"
    };
    static const char *const welcome[6] = {
        "A PROMISE KEPT", "SHELTERED CROSSING", "GARDEN REFUGE",
        "LIGHTS OF WELCOME", "HOMEWARD LEGACY", "OCEAN OF WELCOME"
    };
    if (chapter < 0 || chapter >= 6 || chapter >= t->rpg.chapter) return "NOT YET WRITTEN";
    if (chapter == 5 && (t->rpg.outcome == 2 || t->rpg.outcome == 5)) return "OCEAN OF WONDERS";
    return t->rpg.routes & (1u << chapter) ? welcome[chapter] : open[chapter];
}
const char *companion_rpg_prompt(const tank_t *t) {
    if (t->rpg.chapter >= 6) {
        if (t->rpg.routes & 32)
            return "The current flows homeward, welcoming wanderers. Your companions have made an ocean of refuge.";
        if (t->rpg.outcome == 2 || t->rpg.outcome == 5)
            return "The current reveals hidden reefs. Your companions have made an ocean of wonders to discover.";
        return "The current opens distant paths. Your companions have made an ocean of journeys and discoveries.";
    }
    if (t->rpg.step == 1 && t->rpg.outcome >= 4)
        return "The first attempt found a gentle detour. A quieter clue now points the way onward.";
    return rpg_encounters[t->rpg.chapter * 3 + t->rpg.step].prompt;
}
void companion_rpg_choice_label(const tank_t *t, int choice, char *text, size_t size) {
    const char *label = "";
    if (choice >= 0 && choice < 3 && t->rpg.chapter < 6 && t->rpg.step < 3)
        label = rpg_encounters[t->rpg.chapter * 3 + t->rpg.step].choice[choice];
    snprintf(text, size, "%s", label);
}
bool companion_rpg_contributor(const tank_t *t, uint32_t id) {
    if (!id || id > UINT16_MAX) return false;
    for (int i = 0; i < t->rpg.chapter && i < 6; i++)
        if (t->rpg.contributors[i] == id) return true;
    return false;
}
bool companion_rpg_legacy(const tank_t *t, int fish) {
    if (fish < 0 || fish >= t->n_fish) return false;
    const fish_lineage_t *line = &t->story.active[fish];
    return companion_rpg_contributor(t, line->parent[0]) ||
           companion_rpg_contributor(t, line->parent[1]);
}
/* Each real-world connection helps, but the selected skill alone always
 * advances the story. Neither fish care nor the RNG is touched. */
static unsigned rpg_bonus(const tank_t *t, int fish, int choice) {
    const companion_bond_t *b = &t->companion.active[fish];
    const fish_lineage_t *line = &t->story.active[fish];
    unsigned bonus = b->bond >= 25 || (b->tricks & (1u << choice));
    if (choice == 0) bonus += line->traits != 0 || line->mutation != 0;
    if (choice == 1) {
        bonus += t->sd_unlocks != 0 || t->journal.theme != 0;
        bonus += t->story.seen[VISITOR_GHOST] || t->story.seen[VISITOR_MANTA] || t->story.seen[VISITOR_SQUID];
    }
    if (choice == 2) bonus += t->polish.active[fish].buddy != 0 || companion_rpg_legacy(t, fish);
    bonus += t->polish.active[fish].adventures != 0;
    bonus += t->rpg.chapter && (t->rpg.routes & (1u << (t->rpg.chapter - 1))) && choice == 2;
    return bonus;
}
void companion_rpg_context(const tank_t *t, int fish, char *text, size_t size) {
    const char *context = "Every approach is open. No hurry.";
    if (fish >= 0 && fish < t->n_fish) {
        const companion_bond_t *bond = &t->companion.active[fish];
        const fish_lineage_t *line = &t->story.active[fish];
        if (companion_rpg_legacy(t, fish))
            context = "Family legacy helps your guide.";
        else if (bond->bond >= 25 && (bond->tricks & 7))
            context = "Bonds and tricks help your guide.";
        else if (bond->bond >= 25)
            context = "Your bond helps your guide.";
        else if (bond->tricks & 7)
            context = "Learned tricks help your guide.";
        else if (line->traits || line->mutation)
            context = "Your fish's genes offer courage.";
        else if (t->sd_unlocks || t->journal.theme)
            context = "Your decor offers a clue.";
        else if (t->story.seen[VISITOR_GHOST] || t->story.seen[VISITOR_MANTA] ||
                 t->story.seen[VISITOR_SQUID])
            context = "A past visitor left a clue.";
        else if (t->polish.active[fish].adventures)
            context = "Past adventures help your guide.";
        else if (t->polish.active[fish].buddy)
            context = "A familiar friend inspires kindness.";
        else if (t->rpg.chapter && (t->rpg.routes & (1u << (t->rpg.chapter - 1))))
            context = "Your welcoming route inspires kindness.";
    }
    snprintf(text, size, "%s", context);
}
const char *companion_rpg_outcome(const tank_t *t) {
    static const char *const outcomes[7] = {
        "Your journey is waiting. No fish leaves home.",
        "A brave step opened the way.", "A hidden clue revealed the way.",
        "A new friendship opened the way.", "A gentle detour taught courage.",
        "A second look taught curiosity.", "A helping hand taught kindness."
    };
    return outcomes[t->rpg.outcome <= 6 ? t->rpg.outcome : 0];
}
bool companion_rpg_choose(tank_t *t, int fish, int choice) {
    if (fish < 0 || fish >= t->n_fish || choice < 0 || choice > 2 ||
        t->rpg.chapter >= 6 || !t->polish.active[fish].id ||
        !companion_rpg_valid(&t->rpg, t->polish.next_id)) return false;
    tank_rpg_t old = t->rpg;
    tank_memory_t memories[TANK_RECAP_CAP];
    memcpy(memories, t->polish.recap, sizeof memories);
    uint8_t count = t->polish.recap_count;
    tank_rpg_t *s = &t->rpg;
    unsigned power = s->skills[choice] + rpg_bonus(t, fish, choice);
    s->version = 1;
    s->participant = t->polish.active[fish].id;
    s->outcome = (uint8_t)(choice + 1 + (power < s->chapter + s->step + 1u ? 3 : 0));
    s->skills[choice]++;
    if (++s->step == 3) {
        unsigned chapter = s->chapter;
        /* Never alias a late life identity to an unrelated ancestor. */
        s->contributors[chapter] = s->participant <= UINT16_MAX ? (uint16_t)s->participant : 0;
        if (choice == 2) s->routes |= (uint8_t)(1u << chapter);
        companion_memory(t, MEMORY_RPG, fish, chapter);
        s->chapter++; s->step = 0;
    }
    if (progression_save(t)) return true;
    t->rpg = old;
    memcpy(t->polish.recap, memories, sizeof memories);
    t->polish.recap_count = count;
    return false;
}
bool companion_valid_mode(const companion_state_t *s, bool creative) {
    if (s->version > COMPANION_VERSION || s->cozy > 1 || s->captions > 1 || s->lively > 1 || s->decor > 1 ||
        (s->discoveries & ~0xfffu) || (s->collections & ~7u) || s->adventure > 3 ||
        (s->adventure_done & ~7u) || (s->adventure_claimed & ~s->adventure_done) ||
        s->cosmetic > 3 || (!creative && s->cosmetic && !(s->collections & (1u << (s->cosmetic - 1)))) ||
        s->last_discovery > COMPANION_DISCOVERY_COUNT || s->adventure_choice > 1 ||
        (s->adventure_routes & ~s->adventure_done) || (s->game_challenges & ~63u) || s->featured >= 6 ||
        !memchr(s->portrait_name, 0, sizeof s->portrait_name) || s->portrait_body > 0xffffff ||
        s->portrait_accent > 0xffffff || s->portrait_variety >= FISH_VARIETY_COUNT ||
        s->portrait_pattern >= FISH_PATTERN_COUNT || s->portrait_wearable >= WEAR_COUNT ||
        (s->version < 3 && s->portrait_wearable) ||
        (s->version < 4 && (s->portrait_colors || s->creature_colors[0] || s->creature_colors[1] ||
                           s->creature_colors[2] || s->creature_colors[3])) ||
        s->display_compact > 1 || (s->version < 5 && s->display_compact) ||
        (s->version < 6 && s->mussel_colors) || s->color_pad[0] || s->color_pad[1] ||
        s->away > 1 || (s->away && (!s->adventure || s->version < 2)) ||
        s->travel_pad[0] || s->travel_pad[1] || s->travel_pad[2] ||
        s->adventure_clock < 0 || s->adventure_clock > 4102444800LL ||
        (!s->away && s->adventure_clock)) return false;
    if (!s->adventure && s->adventure_s) return false;
    if (s->adventure) {
        unsigned total = companion_adventure_total(s, s->adventure - 1);
        bool done = (s->adventure_done & (1u << (s->adventure - 1))) != 0;
        if (s->adventure_s > total ||
            (!s->away && ((s->adventure_claimed & (1u << (s->adventure - 1))) ||
                          done != (s->adventure_s == total)))) return false;
    }
    if (!s->away) {
        bank_fish_t empty_fish = {0}; companion_bond_t empty_bond = {0};
        if (memcmp(&s->traveler, &empty_fish, sizeof empty_fish) ||
            memcmp(&s->traveler_bond, &empty_bond, sizeof empty_bond)) return false;
    }
    for (int i = 0; i < 3; i++) {
        if (!creative && (s->collections & (1u << i)) && (s->discoveries & collection_masks[i]) != collection_masks[i]) return false;
        if ((s->adventure_done & (1u << i)) && !(s->discoveries & (1u << (8 + i)))) return false;
    }
    for (int i = 0; i < N_FISH_MAX + FISH_BANK_CAP + 1; i++) {
        const companion_bond_t *b = i < N_FISH_MAX ? &s->active[i] :
            i < N_FISH_MAX + FISH_BANK_CAP ? &s->bank[i - N_FISH_MAX] : &s->traveler_bond;
        if (b->bond > 100 || (b->tricks & 128u) ||
            (s->version < 7 && ((b->tricks & ~7u) || (b->wearables & ~63u))) ||
            b->wearable >= WEAR_COUNT || (s->version < 4 && b->wear_colors) ||
            (b->wearable && !(companion_wearables(b) & (1u << (b->wearable - 1)))) ||
            (s->version < 3 && (b->wearable || b->wearables)) ||
            (b->next_greet_s > s->powered_s && b->next_greet_s - s->powered_s > 30)) return false;
        for (int j = 0; j < 3; j++) if (!creative && (b->tricks & (1u << j)) && b->bond < trick_bonds[j]) return false;
    }
    for (int i = 0; i < 6; i++)
        if (s->game_medals[i] > 3 || ((s->game_challenges & (1u << i)) && s->game_medals[i] != 3)) return false;
    return true;
}
bool companion_valid(const companion_state_t *s) { return companion_valid_mode(s, false); }
static void discover(tank_t *t, int id) {
    uint32_t bit = 1u << id;
    if (!(t->companion.discoveries & bit)) {
        t->companion.discoveries |= bit; t->companion.last_discovery = (uint8_t)(id + 1);
        companion_memory(t, MEMORY_DISCOVERY, -1, id);
        progression_settings_changed();
    }
}
void companion_tick(tank_t *t, float dt) {
    if (!isfinite(dt) || dt <= 0) return;
    companion_state_t *s = &t->companion;
    /* Social cooldowns use powered-on time, independently of adventure time. */
    double elapsed = (double)t->companion_fraction + dt;
    uint32_t seconds = elapsed >= UINT32_MAX ? UINT32_MAX : (uint32_t)elapsed;
    t->companion_fraction = elapsed >= UINT32_MAX ? 0 : (float)(elapsed - seconds);
    if (UINT32_MAX - s->powered_s < seconds) s->powered_s = UINT32_MAX;
    else s->powered_s += seconds;
    if (t->visitor_left > 0 && !t->ui_cover) t->visitor_left = fmaxf(0, t->visitor_left - dt);
    if (seconds) { polish_tick(t, seconds); season_tick(t); }
    if (s->adventure) {
        unsigned total = companion_adventure_total(s, s->adventure - 1);
        uint32_t travel_seconds = seconds;
        if (s->away) {
            int64_t now = clock_port_now_unix();
            if (now > 0 && now <= 4102444800LL) {
                if (s->adventure_clock > 0 && now >= s->adventure_clock) {
                    int64_t delta = now - s->adventure_clock;
                    travel_seconds = delta > UINT32_MAX ? UINT32_MAX : (uint32_t)delta;
                }
                s->adventure_clock = now;
            } else s->adventure_clock = 0;
        }
        unsigned left = total - s->adventure_s;
        s->adventure_s += travel_seconds > left ? left : travel_seconds;
        if (s->adventure_s == total) {
            if (s->away) {
                if ((seconds || travel_seconds) && !progression_adventure_return(t))
                    fprintf(stderr, "Adventure return save failed; fish remains safely stored for retry\n");
            } else {
                s->adventure_done |= (uint8_t)(1u << (s->adventure - 1));
                if (s->adventure_choice) s->adventure_routes |= (uint8_t)(1u << (s->adventure - 1));
                discover(t, 7 + s->adventure);
            }
        }
    }
    if (t->ui_cover) return;
    for (int i = 0; i < t->n_fish; i++) {
        const fish_t *f = &t->fish[i];
        if (f->goal_age >= 2) {
            if (f->goal.id == GOAL_VISIT_BUBBLES && f->at_bubbles) discover(t, 0);
            if (f->goal.id == GOAL_EXPLORE) discover(t, 1);
            if (f->goal.id == GOAL_REST && tank_reef_calm_near(t, f->x, f->y)) discover(t, 2);
            if (f->goal.id == GOAL_FOLLOW_FRIEND && t->n_fish > 1) discover(t, 3);
            if (f->goal.id == GOAL_DART_PLAY) discover(t, 4);
            if (f->goal.id == GOAL_INSPECT_REEF && (f->ms_bits & MS_INSPECTED)) discover(t, 5);
        }
        if (f->ms_bits & (MS_REACHED_JUV | MS_REACHED_ADULT | MS_REACHED_ELDER)) discover(t, 11);
    }
}
static bool commit(tank_t *t, const companion_state_t *old) {
    if (progression_save(t)) return true;
    t->companion = *old;
    return false;
}
bool companion_wearable_fits(int variety, int wearable) {
    if (variety < 0 || variety >= FISH_VARIETY_COUNT ||
        wearable < WEAR_NONE || wearable >= WEAR_COUNT) return false;
    /* Flat front accessories fit the bell; the wraparound scarf does not. */
    return variety != FISH_JELLYFISH || wearable != WEAR_SCARF;
}
bool companion_wearable(tank_t *t, int fish, int wearable) {
    if (fish < 0 || fish >= t->n_fish || wearable < WEAR_NONE || wearable >= WEAR_COUNT) {
        fprintf(stderr, "Invalid wearable selection\n"); return false;
    }
    if (!companion_wearable_fits(t->fish[fish].variety, wearable)) {
        fprintf(stderr, "Wearable does not fit this fish type\n"); return false;
    }
    companion_bond_t *b = &t->companion.active[fish];
    if (t->companion.version < 2 && t->companion.adventure) {
        fprintf(stderr, "Claim legacy postcard before changing wearables\n"); return false;
    }
    unsigned bit = wearable ? 1u << (wearable - 1) : 0;
    bool buy = wearable && !(companion_wearables(b) & bit);
    int price = buy && !progression_creative(t) ? WEAR_PRICES[wearable] : 0;
    if (buy && !progression_creative(t) && t->sd_balance < price) {
        fprintf(stderr, "Not enough Doubloons for wearable\n"); return false;
    }
    companion_state_t old = t->companion;
    t->companion.version = COMPANION_VERSION;
    b->wearable = (uint8_t)wearable; b->wearables |= (uint8_t)bit;
    b->tricks |= (uint8_t)((bit >> 8) << 3);
    t->sd_balance -= price;
    if (!commit(t, &old)) {
        t->sd_balance += price;
        fprintf(stderr, "Wearable save failed; purchase/equipment rolled back\n"); return false;
    }
    t->fish[fish].wearable = (uint8_t)wearable;
    return true;
}
uint8_t companion_creature_colors(const companion_state_t *s, int creature) {
    return creature == CREATURE_MUSSEL ? s->mussel_colors : s->creature_colors[creature];
}
bool companion_colors(tank_t *t, bool wearable, int target, uint8_t colors) {
    if (!t || !wearable) {
        fprintf(stderr, "Creature colors come from acquired stock; free palette edits are disabled\n");
        return false;
    }
    if (target < 0 || target >= t->n_fish ||
        (t->companion.version < 2 && t->companion.adventure)) {
        fprintf(stderr, "Invalid color target or unclaimed legacy postcard\n"); return false;
    }
    companion_state_t old = t->companion;
    t->companion.version = COMPANION_VERSION;
    t->companion.active[target].wear_colors = colors;
    if (!commit(t, &old)) {
        fprintf(stderr, "Color save failed; changes rolled back\n"); return false;
    }
    t->fish[target].wear_colors = colors;
    return true;
}
bool companion_compact(tank_t *t, bool compact) {
    if (compact == !!t->companion.display_compact) return true;
    if (t->companion.version < 2 && t->companion.adventure) {
        fprintf(stderr, "Claim legacy postcard before changing display scale\n"); return false;
    }
    tank_t *next = malloc(sizeof *next);
    if (!next) { fprintf(stderr, "Not enough memory to change display scale\n"); return false; }
    *next = *t;
    int old_w = TANK_W;
    tank_reframe(next, compact);
    next->companion.version = COMPANION_VERSION;
    next->companion.display_compact = compact;
    bool ok = progression_save(next);
    if (ok) *t = *next;
    else {
        tank_resolution(old_w == 320);
        fprintf(stderr, "Display scale save failed; change rolled back\n");
    }
    free(next);
    return ok;
}
bool companion_set_options(tank_t *t, bool cozy, bool captions, bool lively) {
    companion_state_t old = t->companion;
    t->companion.cozy = cozy;
    t->companion.captions = captions;
    t->companion.lively = lively;
    if (!t->companion.adventure) t->companion.version = COMPANION_VERSION;
    return commit(t, &old);
}
bool companion_greet(tank_t *t, int fish) {
    (void)t; (void)fish;
    fprintf(stderr, "Say Hello is retired; no bond or reward changed\n");
    return false;
}
bool companion_trick(tank_t *t, int fish, int trick) {
    if (fish < 0 || fish >= t->n_fish || trick < 0 || trick > 2 || trick == 1 || (trick == 2 && t->n_fish < 2) ||
        (!progression_creative(t) && t->companion.active[fish].bond < trick_bonds[trick])) return false;
    companion_state_t old = t->companion;
    t->companion.active[fish].tricks |= (uint8_t)(1u << trick);
    discover(t, 7);
    if (!commit(t, &old)) return false;
    static const goal_id_t goals[] = {GOAL_DART_PLAY, GOAL_VISIT_BUBBLES, GOAL_FOLLOW_FRIEND};
    t->fish[fish].goal.id = goals[trick];
    t->fish[fish].goal.urgency = 4;
    t->fish[fish].goal_age = 0;
    t->fish[fish].ask_age = 0;
    t->fish[fish].dart_timer = 0;
    return true;
}
bool companion_collection_ready(const tank_t *t, int collection) {
    return collection >= 0 && collection < 3 &&
        (progression_creative(t) || (t->companion.discoveries & collection_masks[collection]) == collection_masks[collection]);
}
bool companion_collection_claim(tank_t *t, int collection) {
    if (!companion_collection_ready(t, collection) || (t->companion.collections & (1u << collection))) return false;
    companion_state_t old = t->companion;
    t->companion.collections |= 1u << collection;
    t->companion.cosmetic = (uint8_t)(collection + 1);
    return commit(t, &old);
}
bool companion_cosmetic(tank_t *t, int cosmetic) {
    if (cosmetic < 0 || cosmetic > 3 || (!progression_creative(t) && cosmetic && !(t->companion.collections & (1u << (cosmetic - 1))))) return false;
    companion_state_t old = t->companion;
    t->companion.cosmetic = (uint8_t)cosmetic;
    return commit(t, &old);
}
bool companion_set_decor(tank_t *t, bool show) {
    companion_state_t old = t->companion;
    t->companion.decor = show;
    return commit(t, &old);
}
bool companion_adventure_start(tank_t *t, int adventure, int fish) {
    if (adventure < 0 || adventure >= 3 || fish < 0 || fish >= t->n_fish ||
        t->companion.adventure || (t->companion.adventure_claimed & (1u << adventure))) return false;
    return progression_adventure_depart(t, adventure, fish);
}
bool companion_adventure_choice(tank_t *t, int choice) {
    if (t->companion.adventure || choice < 0 || choice > 1) return false;
    companion_state_t old = t->companion;
    t->companion.adventure_choice = (uint8_t)choice;
    return commit(t, &old);
}
bool companion_adventure_claim(tank_t *t) {
    int a = t->companion.adventure - 1;
    if (a < 0 || a >= 3 || t->companion.away || !(t->companion.adventure_done & (1u << a)) ||
        (t->companion.adventure_claimed & (1u << a))) return false;
    int coins = progression_creative(t) ? 0 : 10 * (a + 1);
    if (t->sd_balance > INT_MAX - coins || t->sd_earned > INT_MAX - coins) return false;
    companion_state_t old = t->companion;
    uint32_t xp = t->journal.xp;
    t->companion.adventure_claimed |= (uint8_t)(1u << a);
    t->companion.adventure = 0;
    t->companion.adventure_s = 0;
    t->sd_balance += coins;
    t->sd_earned += coins;
    t->journal.xp = xp > JOURNAL_XP_MAX - (unsigned)(coins * 3) ? JOURNAL_XP_MAX : xp + coins * 3;
    if (commit(t, &old)) return true;
    t->sd_balance -= coins;
    t->sd_earned -= coins;
    t->journal.xp = xp;
    return false;
}
bool companion_games_valid(const game_records_t *s) {
    if (s->version > 1) return false;
    for (int i = 0; i < 9; i++) if (s->reserved[i]) return false;
    for (int i = 0; i < 6; i++)
        if (s->medals[i] > 3 || (!s->version && (s->best[i] || s->rounds[i] || s->medals[i]))) return false;
    return true;
}
int companion_game_reward(int game, int score, int moves) {
    if (score < 0 || moves < 0) return 0;
    int dollars = 0;
    switch (game) {
    case 12: if (score == 16 && moves) dollars = moves <= 8 ? 10 : moves <= 16 ? 8 : 6; break;
    case 13: if (score == 8 && moves) dollars = moves <= 20 ? 10 : moves <= 40 ? 8 : 6; break;
    case 14: if (score >= 10) dollars = score / 5 + (score == 25 ? 3 : 0); break;
    case 15: if (score >= 128) dollars = score / 128; break;
    case 16: if (score >= 30) dollars = score / 14; break;
    case 17: if (score >= 3) dollars = score - 2; break;
    default: break;
    }
    return dollars > 10 ? 10 : dollars;
}
bool companion_game_claim(tank_t *t, int game, int score, int moves, int dollars, bool relaxed) {
    if (progression_creative(t) || game < 0 || game >= 18 || score < 0 || moves < 0 ||
        dollars < 0 || dollars > 10 ||
        (game >= 12 && dollars != companion_game_reward(game, score, moves))) return false;
    companion_state_t old = t->companion;
    game_records_t old_games = t->games;
    game_records_t old_extra = t->games_extra;
    if (!relaxed && game >= 12) {
        int index = game - 12;
        bool lower = index < 2;
        unsigned metric = lower ? (unsigned)(dollars ? moves : 0) : (unsigned)score;
        if (metric > UINT16_MAX) metric = UINT16_MAX;
        game_records_t *records = &t->games_extra;
        records->version = 1;
        if ((!lower && metric > records->best[index]) ||
            (lower && metric && (!records->best[index] || metric < records->best[index])))
            records->best[index] = (uint16_t)metric;
        if (records->rounds[index] < UINT16_MAX) records->rounds[index]++;
        static const uint16_t thresholds[6][3] = {
            {24,16,8}, {80,40,24}, {10,20,25}, {128,512,2048}, {30,80,120}, {3,7,12}
        };
        for (int i = 0; i < 3; i++)
            if ((lower ? (metric && metric <= thresholds[index][i]) : metric >= thresholds[index][i]) &&
                records->medals[index] < i + 1) records->medals[index] = (uint8_t)(i + 1);
    } else if (!relaxed && game >= 6) {
        int index = game - 6;
        unsigned metric = (unsigned)score;
        if (metric > UINT16_MAX) metric = UINT16_MAX;
        t->games.version = 1;
        if (metric > t->games.best[index]) t->games.best[index] = (uint16_t)metric;
        if (t->games.rounds[index] < UINT16_MAX) t->games.rounds[index]++;
        static const uint16_t thresholds[6][3] = {{8,20,36},{12,28,44},{6,14,24},{8,18,30},{8,24,36},{10,24,40}};
        for (int i = 0; i < 3; i++) if (metric >= thresholds[index][i] && t->games.medals[index] < i + 1)
            t->games.medals[index] = (uint8_t)(i + 1);
    } else if (!relaxed) {
        bool lower = game == 1 || game == 5;
        unsigned metric = (unsigned)(lower ? moves : score);
        if (metric > UINT16_MAX) metric = UINT16_MAX;
        uint16_t *best = &t->companion.game_best[game];
        if ((!lower && metric > *best) || (lower && metric && (!*best || metric < *best))) *best = (uint16_t)metric;
        if (t->companion.game_rounds[game] < UINT16_MAX) t->companion.game_rounds[game]++;
        static const uint16_t thresholds[6][3] = {{6,15,24},{30,22,16},{8,16,24},{2,6,10},{8,20,40},{50,30,18}};
        for (int i = 0; i < 3; i++) if ((lower ? (metric && metric <= thresholds[game][i]) : metric >= thresholds[game][i]) &&
            t->companion.game_medals[game] < i + 1) t->companion.game_medals[game] = (uint8_t)(i + 1);
        if (t->companion.game_medals[game] == 3) t->companion.game_challenges |= (uint8_t)(1u << game);
        if (t->companion.featured == game) t->companion.featured = (uint8_t)((game + 1) % 6);
    }
    if (dollars ? progression_game_reward(t, game, dollars) : progression_save(t)) return true;
    t->companion = old;
    t->games = old_games;
    t->games_extra = old_extra;
    return false;
}
bool companion_captions(const tank_t *t) { return t->companion.captions != 0; }
bool companion_lively(const tank_t *t) { return t->companion.lively != 0; }
/* Derived only from the fish's saved creation personality and roster preset:
 * no counters, no claim of observed history unless its milestone says so. */
int companion_favorite(const tank_t *t, int fish) {
    if (fish < 0 || fish >= t->n_fish) return -1;
    const fish_t *f = &t->fish[fish];
    unsigned p = (unsigned)f->preset;
    if (f->sociable0 >= 0.65f && f->sociable0 >= f->bold0) return GOAL_FOLLOW_FRIEND;
    if (f->bold0 >= 0.65f) return p & 1u ? GOAL_DART_PLAY : GOAL_EXPLORE;
    if (f->bold0 <= 0.35f) return p & 1u ? GOAL_INSPECT_REEF : GOAL_REST;
    return p % 3u == 1 ? GOAL_EXPLORE : GOAL_INSPECT_REEF;
}
void companion_favorite_text(const tank_t *t, int fish, char *text, size_t size) {
    static const char *const names[GOAL_COUNT] = {
        [GOAL_VISIT_BUBBLES] = "the bubble column", [GOAL_FOLLOW_FRIEND] = "swimming with tankmates",
        [GOAL_EXPLORE] = "exploring", [GOAL_REST] = "a quiet refuge", [GOAL_DART_PLAY] = "darting games",
        [GOAL_INSPECT_REEF] = "the reef"
    };
    static const uint32_t seen[GOAL_COUNT] = {
        [GOAL_VISIT_BUBBLES] = MS_FIRST_BUBBLES, [GOAL_FOLLOW_FRIEND] = MS_FIRST_FOLLOW,
        [GOAL_REST] = MS_FIRST_GRASS, [GOAL_DART_PLAY] = MS_FIRST_DART, [GOAL_INSPECT_REEF] = MS_INSPECTED
    };
    if (!size) return;
    int g = companion_favorite(t, fish);
    if (g < 0) { text[0] = 0; return; }
    snprintf(text, size, "%s %s", names[g], seen[g] && (t->fish[fish].ms_bits & seen[g]) ? "(seen!)" : "(by nature)");
}
void companion_favorite_label(const tank_t *t, int fish, char *text, size_t size) {
    static const char *const names[GOAL_COUNT] = {
        [GOAL_VISIT_BUBBLES] = "BUBBLES", [GOAL_FOLLOW_FRIEND] = "TANKMATES", [GOAL_EXPLORE] = "EXPLORING",
        [GOAL_REST] = "SEAGRASS", [GOAL_DART_PLAY] = "DARTING", [GOAL_INSPECT_REEF] = "THE REEF"
    };
    if (!size) return;
    char verbose[48];
    int g = companion_favorite(t, fish);
    if (g < 0) { text[0] = 0; return; }
    companion_favorite_text(t, fish, verbose, sizeof verbose);
    snprintf(text, size, "LIKES: %s %s", names[g], strstr(verbose, "(seen!)") ? "(SEEN)" : "(NATURE)");
}
void companion_status(const tank_t *t, char *text, size_t size) {
    if (!size) return;
    const companion_state_t *s = &t->companion;
    if (s->adventure && (s->adventure_done & (1u << (s->adventure - 1))) &&
        !(s->adventure_claimed & (1u << (s->adventure - 1))))
        snprintf(text, size, "%s: POSTCARD READY", s->portrait_name);
    else if (s->away) snprintf(text, size, "%s: ADVENTURING", s->portrait_name);
    else if (t->visitor_left > 0) snprintf(text, size, "RARE VISITOR PASSING");
    else {
        text[0] = 0;
        for (int i = 0; i < t->n_fish; i++) {
            const fish_t *f = &t->fish[i];
            const char *need = f->hunger >= 8 ? "HUNGRY" : f->stress >= 7 ? "NEEDS CALM" : NULL;
            if (need) { snprintf(text, size, "%s: %s", f->name, need); break; }
        }
    }
}