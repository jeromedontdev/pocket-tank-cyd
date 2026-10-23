#include "companion.h"
#include "progression.h"
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

const char *const COMPANION_DISCOVERIES[] = {
    "Bubble watcher", "Little explorer", "Seagrass nap", "Swimming together",
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
const char *const WEAR_NAMES[WEAR_COUNT] = {"NONE", "TOP HAT", "PARTY HAT", "CROWN", "TIE", "BOWTIE", "BANDANA"};
const uint8_t WEAR_LEVELS[WEAR_COUNT] = {0, 3, 2, 10, 5, 4, 2};
const uint8_t WEAR_PRICES[WEAR_COUNT] = {0, 45, 20, 100, 35, 30, 20};

void companion_init(tank_t *t) {
    memset(&t->companion, 0, sizeof t->companion);
    t->companion.version = COMPANION_VERSION;
    t->companion.cozy = t->companion.captions = 1;
    t->companion.display_compact = TANK_W == TANK_MAX_W;
    t->companion.lively = 0;
    t->companion.decor = 1;
    t->companion_fraction = 0;
}
bool companion_valid(const companion_state_t *s) {
    if (s->version > COMPANION_VERSION || s->cozy > 1 || s->captions > 1 || s->lively > 1 || s->decor > 1 ||
        (s->discoveries & ~0xfffu) || (s->collections & ~7u) || s->adventure > 3 ||
        (s->adventure_done & ~7u) || (s->adventure_claimed & ~s->adventure_done) ||
        s->cosmetic > 3 || (s->cosmetic && !(s->collections & (1u << (s->cosmetic - 1)))) ||
        s->last_discovery > COMPANION_DISCOVERY_COUNT || s->adventure_choice > 1 ||
        (s->adventure_routes & ~s->adventure_done) || (s->game_challenges & ~63u) || s->featured >= 6 ||
        !memchr(s->portrait_name, 0, sizeof s->portrait_name) || s->portrait_body > 0xffffff ||
        s->portrait_accent > 0xffffff || s->portrait_variety >= FISH_VARIETY_COUNT ||
        s->portrait_pattern >= FISH_PATTERN_COUNT || s->portrait_wearable >= WEAR_COUNT ||
        (s->version < 3 && s->portrait_wearable) ||
        (s->version < 4 && (s->portrait_colors || s->creature_colors[0] || s->creature_colors[1] ||
                           s->creature_colors[2] || s->creature_colors[3])) ||
        s->display_compact > 1 || (s->version < 5 && s->display_compact) ||
        s->color_pad[0] || s->color_pad[1] || s->color_pad[2] ||
        s->away > 1 || (s->away && (!s->adventure || s->version < 2)) ||
        s->travel_pad[0] || s->travel_pad[1] || s->travel_pad[2] ||
        s->adventure_clock < 0 || s->adventure_clock > 4102444800LL ||
        (!s->away && s->adventure_clock)) return false;
    if (!s->adventure && s->adventure_s) return false;
    if (s->adventure) {
        unsigned total = companion_adventure_total(s, s->adventure - 1);
        bool done = (s->adventure_done & (1u << (s->adventure - 1))) != 0;
        if (s->adventure_s > total || (s->adventure_claimed & (1u << (s->adventure - 1))) ||
            done != (!s->away && s->adventure_s == total)) return false;
    }
    if (!s->away) {
        bank_fish_t empty_fish = {0}; companion_bond_t empty_bond = {0};
        if (memcmp(&s->traveler, &empty_fish, sizeof empty_fish) ||
            memcmp(&s->traveler_bond, &empty_bond, sizeof empty_bond)) return false;
    }
    for (int i = 0; i < 3; i++) {
        if ((s->collections & (1u << i)) && (s->discoveries & collection_masks[i]) != collection_masks[i]) return false;
        if ((s->adventure_done & (1u << i)) && !(s->discoveries & (1u << (8 + i)))) return false;
    }
    for (int i = 0; i < N_FISH_MAX + FISH_BANK_CAP + 1; i++) {
        const companion_bond_t *b = i < N_FISH_MAX ? &s->active[i] :
            i < N_FISH_MAX + FISH_BANK_CAP ? &s->bank[i - N_FISH_MAX] : &s->traveler_bond;
        if (b->bond > 100 || (b->tricks & ~7u) ||
            b->wearable >= WEAR_COUNT || (b->wearables & ~63u) || (s->version < 4 && b->wear_colors) ||
            (b->wearable && !(b->wearables & (1u << (b->wearable - 1)))) ||
            (s->version < 3 && (b->wearable || b->wearables)) ||
            (b->next_greet_s > s->powered_s && b->next_greet_s - s->powered_s > 30)) return false;
        for (int j = 0; j < 3; j++) if ((b->tricks & (1u << j)) && b->bond < trick_bonds[j]) return false;
    }
    for (int i = 0; i < 6; i++)
        if (s->game_medals[i] > 3 || ((s->game_challenges & (1u << i)) && s->game_medals[i] != 3)) return false;
    return true;
}
static void discover(tank_t *t, int id) {
    uint32_t bit = 1u << id;
    if (!(t->companion.discoveries & bit)) {
        t->companion.discoveries |= bit; t->companion.last_discovery = (uint8_t)(id + 1);
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
            if (f->goal.id == GOAL_REST && tank_in_grass(t, f->x, f->y)) discover(t, 2);
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
bool companion_wearable(tank_t *t, int fish, int wearable) {
    if (fish < 0 || fish >= t->n_fish || wearable < WEAR_NONE || wearable >= WEAR_COUNT) {
        fprintf(stderr, "Invalid wearable selection\n"); return false;
    }
    companion_bond_t *b = &t->companion.active[fish];
    if (t->companion.version < 2 && t->companion.adventure) {
        fprintf(stderr, "Claim legacy postcard before changing wearables\n"); return false;
    }
    unsigned bit = wearable ? 1u << (wearable - 1) : 0;
    bool buy = wearable && !(b->wearables & bit);
    int price = buy ? WEAR_PRICES[wearable] : 0;
    if (buy && (journal_level(t) < WEAR_LEVELS[wearable] || t->sd_balance < price)) {
        fprintf(stderr, "Wearable locked or insufficient sand dollars\n"); return false;
    }
    companion_state_t old = t->companion;
    t->companion.version = COMPANION_VERSION;
    b->wearable = (uint8_t)wearable; b->wearables |= (uint8_t)bit;
    t->sd_balance -= price;
    if (!commit(t, &old)) {
        t->sd_balance += price;
        fprintf(stderr, "Wearable save failed; purchase/equipment rolled back\n"); return false;
    }
    t->fish[fish].wearable = (uint8_t)wearable;
    return true;
}
bool companion_colors(tank_t *t, bool wearable, int target, uint8_t colors) {
    if (target < 0 || target >= (wearable ? t->n_fish : CREATURE_COUNT) ||
        (t->companion.version < 2 && t->companion.adventure)) {
        fprintf(stderr, "Invalid color target or unclaimed legacy postcard\n"); return false;
    }
    companion_state_t old = t->companion;
    t->companion.version = COMPANION_VERSION;
    if (wearable) t->companion.active[target].wear_colors = colors;
    else t->companion.creature_colors[target] = colors;
    if (!commit(t, &old)) {
        fprintf(stderr, "Color save failed; changes rolled back\n"); return false;
    }
    if (wearable) t->fish[target].wear_colors = colors;
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
    next->companion.version = COMPANION_VERSION; next->companion.display_compact = compact;
    bool ok = progression_save(next);
    if (ok) *t = *next;
    else { tank_resolution(old_w == 320); fprintf(stderr, "Display scale save failed; change rolled back\n"); }
    free(next); return ok;
}
bool companion_set_options(tank_t *t, bool cozy, bool captions, bool lively) {
    companion_state_t old = t->companion;
    t->companion.cozy = cozy; t->companion.captions = captions; t->companion.lively = lively;
    if (!t->companion.adventure) t->companion.version = COMPANION_VERSION;
    return commit(t, &old);
}
bool companion_greet(tank_t *t, int fish) {
    if (fish < 0 || fish >= t->n_fish) return false;
    companion_bond_t *b = &t->companion.active[fish];
    if (b->next_greet_s && t->companion.powered_s < b->next_greet_s) return false;
    companion_state_t old = t->companion;
    b->bond = b->bond > 95 ? 100 : b->bond + 5;
    if (b->greetings < UINT16_MAX) b->greetings++;
    b->next_greet_s = t->companion.powered_s > UINT32_MAX - 30 ? UINT32_MAX : t->companion.powered_s + 30;
    discover(t, 6);
    if (!commit(t, &old)) return false;
    t->greet_timer = 12;
    return true;
}
bool companion_trick(tank_t *t, int fish, int trick) {
    if (fish < 0 || fish >= t->n_fish || trick < 0 || trick > 2 || (trick == 2 && t->n_fish < 2) ||
        t->companion.active[fish].bond < trick_bonds[trick]) return false;
    companion_state_t old = t->companion;
    t->companion.active[fish].tricks |= (uint8_t)(1u << trick);
    discover(t, 7);
    if (!commit(t, &old)) return false;
    static const goal_id_t goals[] = {GOAL_DART_PLAY, GOAL_VISIT_BUBBLES, GOAL_FOLLOW_FRIEND};
    t->fish[fish].goal.id = goals[trick]; t->fish[fish].goal.urgency = 4;
    t->fish[fish].goal_age = 0; t->fish[fish].ask_age = 0;
    t->fish[fish].dart_timer = 0;
    return true;
}
bool companion_collection_ready(const tank_t *t, int collection) {
    return collection >= 0 && collection < 3 &&
        (t->companion.discoveries & collection_masks[collection]) == collection_masks[collection];
}
bool companion_collection_claim(tank_t *t, int collection) {
    if (!companion_collection_ready(t, collection) || (t->companion.collections & (1u << collection))) return false;
    companion_state_t old = t->companion;
    t->companion.collections |= 1u << collection; t->companion.cosmetic = (uint8_t)(collection + 1);
    return commit(t, &old);
}
bool companion_cosmetic(tank_t *t, int cosmetic) {
    if (cosmetic < 0 || cosmetic > 3 || (cosmetic && !(t->companion.collections & (1u << (cosmetic - 1))))) return false;
    companion_state_t old = t->companion; t->companion.cosmetic = (uint8_t)cosmetic;
    return commit(t, &old);
}
bool companion_set_decor(tank_t *t, bool show) {
    companion_state_t old = t->companion; t->companion.decor = show;
    return commit(t, &old);
}
bool companion_adventure_start(tank_t *t, int adventure, int fish) {
    if (adventure < 0 || adventure >= 3 || fish < 0 || fish >= t->n_fish ||
        t->companion.adventure || (t->companion.adventure_claimed & (1u << adventure))) return false;
    return progression_adventure_depart(t, adventure, fish);
}
bool companion_adventure_choice(tank_t *t, int choice) {
    if (t->companion.adventure || choice < 0 || choice > 1) return false;
    companion_state_t old = t->companion; t->companion.adventure_choice = (uint8_t)choice;
    return commit(t, &old);
}
bool companion_adventure_claim(tank_t *t) {
    int a = t->companion.adventure - 1;
    if (a < 0 || a >= 3 || t->companion.away || !(t->companion.adventure_done & (1u << a)) ||
        (t->companion.adventure_claimed & (1u << a))) return false;
    int coins = 10 * (a + 1);
    if (t->sd_balance > INT_MAX - coins || t->sd_earned > INT_MAX - coins) return false;
    companion_state_t old = t->companion; uint32_t xp = t->journal.xp;
    t->companion.adventure_claimed |= (uint8_t)(1u << a);
    t->companion.adventure = 0; t->companion.adventure_s = 0;
    t->sd_balance += coins; t->sd_earned += coins;
    t->journal.xp = xp > JOURNAL_XP_MAX - (unsigned)(coins * 3) ? JOURNAL_XP_MAX : xp + coins * 3;
    if (commit(t, &old)) return true;
    t->sd_balance -= coins; t->sd_earned -= coins; t->journal.xp = xp; return false;
}
bool companion_game_claim(tank_t *t, int game, int score, int moves, int dollars, bool relaxed) {
    if (game < 0 || game >= 6 || score < 0 || moves < 0 || dollars < 1 || dollars > 10) return false;
    companion_state_t old = t->companion;
    if (!relaxed) {
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
    if (progression_game_reward(t, dollars)) return true;
    t->companion = old; return false;
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
    return p % 3u == 0 ? GOAL_VISIT_BUBBLES : p % 3u == 1 ? GOAL_EXPLORE : GOAL_INSPECT_REEF;
}
void companion_favorite_text(const tank_t *t, int fish, char *text, size_t size) {
    static const char *const names[GOAL_COUNT] = {
        [GOAL_VISIT_BUBBLES] = "the bubble column", [GOAL_FOLLOW_FRIEND] = "swimming with tankmates",
        [GOAL_EXPLORE] = "exploring", [GOAL_REST] = "the seagrass", [GOAL_DART_PLAY] = "darting games",
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
static void fish_caption(const tank_t *t, int i, bool favorite, char *text, size_t size) {
    const fish_t *f = &t->fish[i];
    if (favorite) {
        char fav[48];
        companion_favorite_text(t, i, fav, sizeof fav);
        snprintf(text, size, "%s loves %s", f->name, fav);
        return;
    }
    int friend_idx = f->goal.id == GOAL_FOLLOW_FRIEND ? tank_nearest_friend(t, i, NULL) : -1;
    if (friend_idx >= 0) snprintf(text, size, "%s swims with %s", f->name, t->fish[friend_idx].name);
    else snprintf(text, size, "%s: %s", f->name, GOAL_NAMES[f->goal.id >= 0 && f->goal.id < GOAL_COUNT ? f->goal.id : GOAL_REST]);
}
void companion_status(const tank_t *t, char *text, size_t size) {
    if (!size) return;
    const companion_state_t *s = &t->companion;
    if (s->adventure && (s->adventure_done & (1u << (s->adventure - 1))))
        snprintf(text, size, "%s's postcard is ready", s->portrait_name);
    else if (t->greet_timer > 0 && t->n_fish) snprintf(text, size, "A little hello from your fish");
    else if (s->away) snprintf(text, size, "%s is adventuring: %s", s->portrait_name, COMPANION_ADVENTURES[s->adventure - 1]);
    else if (s->adventure) snprintf(text, size, "A story unfolds: %s", COMPANION_ADVENTURES[s->adventure - 1]);
    else if (t->n_fish) {
        /* Rotates every 6 powered-on seconds: the discovery, then each fish's
         * favorite and its current activity (with its followed tankmate). */
        unsigned extra = s->last_discovery ? 1u : 0u, n = extra + 2u * (unsigned)t->n_fish;
        unsigned k = (s->powered_s / 6u) % n;
        if (k < extra) snprintf(text, size, "Found: %s", COMPANION_DISCOVERIES[s->last_discovery - 1]);
        else fish_caption(t, (int)((k - extra) / 2u), ((k - extra) & 1u) == 0, text, size);
    } else if (s->last_discovery) snprintf(text, size, "Found: %s", COMPANION_DISCOVERIES[s->last_discovery - 1]);
    else snprintf(text, size, "A quiet moment together");
}