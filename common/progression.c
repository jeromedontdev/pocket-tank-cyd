#include "progression.h"
#include "companion.h"
#include "tank_events.h"
#include "version.h"
#include "setup.h"
#include <stddef.h>
#include <string.h>
#include <math.h>
#include <stdio.h>
#include <limits.h>
#include <stdlib.h>

#define SAVE_MAGIC 0x50544b32u   /* "PTK2" (PTK1 saves are 4-fish, pre-population: start fresh) */
#define RAVENOUS_GIVE_UP_S 150.0f  /* begging window before the fish give up */
/* AUTO FEED off (0.3.2): nobody dies, but a fish left starving (hunger at
 * STARVE_HUNGER or worse) in a lit, awake tank for longer than the grace
 * loses trust, slowly, down to a floor a few feedings and holds win back. */
#define STARVE_HUNGER      8.5f
#define STARVE_GRACE_S     300.0f
#define STARVE_TRUST_PER_S (1.0f / 1200.0f)   /* a point of trust per 20 min */
#define STARVE_TRUST_FLOOR 2.0f
#define SAVE_HEARTBEAT_S 600.0f
#define SAVE_MIN_GAP_S   30.0f

const char *const MS_NAMES[MS_FISH_COUNT] = {
    "arrived", "first meal from you", "first hold-approach", "first dart", "first bubbles",
    "(inspected)", "(retired)", "(retired)", "first follow",
    "reached juv", "reached adult", "reached elder",
    "first rest in the seagrass",
};
const char *const TMS_NAMES[TMS_COUNT] = {
    "a pair", "a trio", "a quartet", "a quintet", "a sextet",
    "first full night's sleep", "first play session", "the tank changed someone", "first feeding",
    "first trimming", "first glass cleaning", "a school of ten shrimp",
    "first visit to the reef",
    "first mutation",                    /* 0.4's (main); never set on 0.3.x */
    "first fish sold",
    "first seahorse feeding",            /* 0.3.6 */
};

/* the save keeps 644 film cells - the rectangle's 28 x 23 grid, frozen by the
 * layout lock below. A build with another grid (the bowl's 26 x 22) saves its
 * own cells into the front of the same array: the layout never moves. */
#define ALGAE_SAVE 644
_Static_assert(ALGAE_CAP <= ALGAE_SAVE, "the film grid outgrew the 644 cells the save keeps");

typedef fish_record_t fish_save_t;

typedef struct {
    uint32_t magic;
    int64_t  saved_unix;
    float    clock;
    bool     light_override, light_on, arrival_pending;
    uint8_t  n_fish;
    float    feed_spot_x;
    int32_t  player_feedings, hold_approaches;
    uint32_t tank_ms_bits;
    fish_save_t fish[N_FISH_MAX];
    /* ---- upkeep tail (2026-08-30). Fields only ever APPEND here: boot falls
     * back to loading the prefix above from an older PTK2 save; the zeroed
     * tail then reads as clean glass and default vegetation (0 is not a legal
     * growth - the floor is VEG_NUB - so restore treats it as "keep the
     * fresh-tank default"). ---- */
    float    veg_growth[VEG_BEDS];
    uint8_t  algae[ALGAE_SAVE];
    int32_t  trims, cells_cleaned;
    /* per-frond heights (2026-09-04); an older save (no tail, or zeros)
     * seeds every frond from its bed's veg_growth */
    float    veg_h[VEG_BEDS][VEG_FRONDS_MAX];
    /* identity tail (2026-09-13, first-run setup): the keeper's names and
     * colours per fish - an empty name / a zero colour = the preset's - and
     * whether the setup flow still owes the keeper a visit. Older saves read
     * zeros: preset looks, setup done. */
    uint8_t  setup_pending, pad_id[3];
    char     names[N_FISH_MAX][FISH_NAME_MAX + 1];
    uint32_t body[N_FISH_MAX], accent[N_FISH_MAX];
    float    bubble_x;                   /* the keeper's bubble column (0 = the default spot) */
    /* seen-milestones tail (2026-09-13, the milestones page): what the
     * keeper has already looked at, so a badge earned since wears a ring.
     * Older saves read zeros: everything earned shows as new once. */
    uint32_t ms_seen[N_FISH_MAX], tank_ms_seen;
    /* family tail (2026-09-14, the birth flow): each fish's parents and the
     * arrival still owed its welcome, all as slot + 1 so an older save's
     * zeros read as "none". */
    uint8_t  newborn_p1, parent_p1[N_FISH_MAX][2], pad_fam[3];
    /* drift-pressure tail (2026-09-15, the CHANGE gate): older saves read
     * zeros, and the youngest simply starts earning it from this build on */
    float    drift_acc[N_FISH_MAX];
    /* light settings tail (2026-09-15, the settings page): 0 seconds = the
     * default (older saves); auto 0 = MANUAL, the double-tap (the default) */
    uint16_t light_idle_s; uint8_t light_auto, light_manual_off;   /* auto 0 = MANUAL, the default */
    /* sand dollar tail (2026-09-15, the shop): the balance, the lifetime
     * total, the unlocks, the paid ledger, the two chore counters, the snail's
     * spot and the sword plant's leaves. Older saves read zeros: no dollars,
     * nothing bought, nothing paid - and the ledger then pays what the tank
     * already earned on the first tick, once. */
    int32_t  sd_balance, sd_earned;
    uint32_t sd_unlocks;
    uint32_t sd_paid_fish[N_FISH_MAX];
    int32_t  sd_colonies_paid, sd_inches_paid;
    int32_t  algae_colonies;
    float    trim_px;
    float    snail_x, snail_y;           /* 0 = not placed yet */
    float    veg_h3[VEG_FRONDS_MAX];     /* bed 3: zeros = VEG_START when it is bought */
    /* placement tail (2026-09-16, the placement page): the plant's centre x
     * (0 = the default spot) and its layer + 1 (0 = MIDDLE, an older save) */
    float    plant_x;
    uint8_t  plant_z1, pad_place[3];
    /* the snail's tally (2026-09-16, its card): cells grazed clean, lifetime.
     * Older saves read 0 - it starts counting from this build on. */
    int32_t  snail_grazed;
    /* the castle's spot (2026-09-16): its centre x (0 = the default) and its
     * layer + 1 (0 = FRONT, an older save or one that never placed it) */
    float    castle_x;
    uint8_t  castle_z1, pad_castle[3];
    /* the coral (2026-09-23): its centre x (0 = the default), its layer + 1
     * (0 = MIDDLE, a save that never placed it) and its colour (0 = the
     * palette's first). Older saves read zeros: no coral until it is bought. */
    float    coral_x;
    uint8_t  coral_z1, pad_coral[3];
    uint32_t coral_rgb;
    float    coral_growth;               /* CORAL_START..1 (0 = a save from before it grew: full) */
    /* the reef cluster (2026-09-24): its centre x (0 = the default), its
     * layer + 1, its look, its growth (0 = full). Older saves: no cluster. */
    float    cluster_x;
    uint8_t  cluster_z1, cluster_scheme;
    /* the fry for sale (0.3.5): the offer's seed (0 = none drawn yet). In
     * the cluster's padding, zeros in every older save; free on main. */
    uint16_t fry_seed;
    float    cluster_growth;
    /* the shrimp school (2026-09-29): how many (0 = an older save, or none
     * bought), the pellets eaten toward the next, the seconds before another
     * may join. Positions are not saved: a load puts them in the grass. */
    uint8_t  shrimp_n, shrimp_food;
    uint16_t fry_age_min;                /* the fry offer's age, tank minutes (0.3.5, the shrimp's padding) */
    float    shrimp_cool;
    int32_t  shrimp_eaten;               /* pellets eaten, lifetime (its card): in the tail's padding, so still 1664 B */
    /* the release that wrote it (2026-09-29, version.h PT_RELEASE_NUM:
     * 0x000200 = 0.2.0); 0 = a build from before release numbers */
    uint32_t saved_release;
    /* the meals a tank that sold a fish owes before its next fry (2026-10-01,
     * progression_sell_fish): player_feedings must reach this. 0 = no fish
     * was ever sold (every older save; it sat in the struct's tail padding,
     * which every build wrote as zeros - still 1672 B). */
    int32_t  sale_meals_need;
    /* a worn tank's way up (2026-10-02, the watch: tank_screen_*): settings
     * SCREEN, 1 = TURNED. Older saves (and every other board) read 0: NORMAL. */
    uint8_t  screen_turned;
    /* 0.3.2's two settings, in what was screen_turned's padding (every build
     * wrote it as zeros, so older saves read the defaults; still 1688 B, and
     * nothing moves for 0.4's tails after it): orient - bit 0 = ROTATION
     * locked, bit 1 = the locked way up is turned over (tank_orient);
     * autofeed_off - 1 = AUTO FEED off. */
    uint8_t  orient, autofeed_off, pad_screen;
    /* the urchin (2026-10-02): its x on the floor (0 = not placed yet: it
     * starts by the reef bed) and the grass it has eaten, lifetime, in the
     * trim's px (its card). Older saves read zeros. */
    float    urchin_x;
    float    urchin_grazed_px;
    /* the lights-out notice (2026-10-03): 1 = a double-tap has turned the
     * light off once and the keeper was told how. In the tail's padding,
     * which every build wrote as zeros - still 1688 B; older saves read 0
     * and get the notice once. */
    uint8_t  light_tip_seen;
    /* the toolbox's LOCK (0.3.5): 1 = bare fingers never wipe or cut. In
     * light_tip_seen's padding, which every build wrote as zeros - still
     * 1688 B; older saves read 0, unlocked. */
    uint8_t  tools_locked;
    uint8_t  fry_sold, pad_tip;          /* the fry offer's blanks, bit k = fry k bought (0.3.5) */
    /* RESERVED for the 0.4 line (main): its genes, the tank and fish IDs, the
     * fish prints and the link's away flags live at 1688..1863 there. The
     * 0.3 line writes zeros (a 0.3.6 save reads on main as a tank without
     * genes, as every older save does) so the seahorse below lands where
     * main's tail ends and nothing collides at the merge forward. */
    uint8_t  pad_04[176];
    /* the seahorse (0.3.6): its seconds with the keeper, awake and asleep
     * (its card: days), its hitches and the trust it passed on. Older saves
     * (and main's) read zeros: a fresh count, which is right - they had none. */
    float    sh_s;
    int32_t  sh_hitches;
    float    sh_trust_passed;
    /* its snacks (0.3.6, later the same day): the count, the earning count,
     * the pay ledger, the cooldown. sh_eaten took pad_sh's place (zeros in
     * every 1880 save: a fresh count); the rest grew the save to 1892. */
    int32_t  sh_eaten;
    int32_t  sh_snack_pay, sd_snacks_paid;
    float    sh_snack_cool;
    /* the pumpkin lantern (0.3.6, the seasonal piece): its spot (0 = the
     * default) and its depth + 1 (0 = never placed: BEHIND). Older saves
     * read zeros. */
    float    pumpkin_x;
    uint8_t  pumpkin_z1, pad_pk[3];
    /* the order of the pieces (0.3.6, later the same day): a rank per slot
     * (castle, coral, cluster, pumpkin, plant). Older saves read zeros: the
     * old fixed order. */
    uint8_t  decor_rank[5], pad_rank[3]; /* frozen: the sixth rank belongs in the appended chest tail */
    float    chest_x;
    uint8_t  chest_z1, chest_rank, pad_chest[2];
    uint32_t pad_chest_tail;       /* preserve the original 1920-byte save, including padding */
    float    ornament_x[4];
    uint8_t  ornament_z1[4], ornament_rank[4], sand_style, water_style, pad_ornament[2];
    uint32_t pad_palette_tail;
    float extra_x[6];
    uint8_t extra_z1[6], extra_rank[6], fish_variety[N_FISH_MAX], fish_pattern[N_FISH_MAX];
    journal_state_t journal;
    uint32_t pad_journal;
    uint8_t bank_count, pad_bank[3];
    bank_fish_t bank[FISH_BANK_CAP];
    uint32_t pad_bank_tail;
    aquarium_state_t aquariums;
    companion_state_t companion;
    tank_polish_t polish;
    tank_story_t story;                /* 1.3: family tree, name book, visitor log, seasons */
    game_records_t games;
    tank_rpg_t rpg;
    creative_state_t creative;
    game_records_t games_extra;
    profile_state_t profile;
    care_state_t care;
    inventory_state_t inventory;
    mussel_state_t mussel;
    game_income_state_t game_income;
} save_t;
/* the smallest PTK2 save (pre-upkeep, 2026-08-30): anything shorter is not
 * ours. Every later build wrote sizeof(save_t) of its day - 448, 1112, 1304,
 * 1408, 1440, 1456 ... - and load_save takes ANY such length, so a tail only
 * ever appends and never needs its size recorded here. (Until 2026-09-14 the
 * load walked a fixed list of tail offsets; the seen-masks build's 1440 was
 * 1436 padded to the int64's alignment, no offset matched, and the birth-flow
 * flash replaced a live tank with two fry.) */
#define SAVE_CORE_SIZE   offsetof(save_t, veg_growth)
/* the one field ever added MID-struct: bubble_x (2026-09-14) went in ahead of
 * the seen masks, and the browser installer's first builds (public 09-13 ..
 * 09-14) had already written saves without it - 1432 bytes, the masks
 * starting where bubble_x now sits. load_save slides them into place, so a
 * tank kept since the first install updates clean (its badges stay seen and
 * the bubble column stays put). Never add a field mid-struct again. */
#define SAVE_PRE_BUBBLE_SIZE 1432
_Static_assert(offsetof(save_t, ms_seen) == offsetof(save_t, bubble_x) + sizeof(float),
               "the pre-bubble migration expects the seen masks right after bubble_x");
_Static_assert(offsetof(save_t, bubble_x) + sizeof(((save_t *)0)->ms_seen) + sizeof(uint32_t) == SAVE_PRE_BUBBLE_SIZE,
               "the pre-bubble migration expects the 1432-byte layout's masks to end at 1432");

/* ---- SAVE LAYOUT LOCK (2026-09-29): a new field goes at the END of save_t
 * with its own assert here; never edit an existing line here. ----
 * Every tank on a keeper's board is read back through these offsets after a
 * browser update, so they are frozen: a failing assert means the change just
 * moved (scrambled) every saved tank - put the field at the end instead. The
 * array bounds are in here too (N_FISH_MAX, VEG_BEDS, VEG_FRONDS_MAX,
 * ALGAE_CELLS, FISH_NAME_MAX): growing one shifts everything after it, so a
 * bigger bound needs a new tail, not a bigger array. The numbers hold on both
 * builds: no pointers or longs, int64_t aligns to 8 and bool is 1 byte on the
 * device's xtensa gcc and on the 64-bit sim alike (both compilers measured
 * the same 1656 bytes, 2026-09-29). The sizes older builds wrote, all still
 * loadable: 448, 1112, 1304, 1408, 1432 (pre-bubble), 1440, 1456, 1480, 1608,
 * 1616, 1624, 1640, 1656 - sim/testdata/saves holds them. */
_Static_assert(SAVE_MAGIC == 0x50544b32u, "SAVE LAYOUT LOCK: a new magic = every saved tank starts fresh");
_Static_assert(sizeof SAVE_NVS_NS == 5 && sizeof SAVE_NVS_KEY == 5, "SAVE LAYOUT LOCK: the NVS namespace / key are \"tank\" / \"save\"");
#define SAVE_AT(f, off) _Static_assert(offsetof(save_t, f) == (off), "SAVE LAYOUT LOCK: save_t." #f " moved")
#define FISH_AT(f, off) _Static_assert(offsetof(fish_save_t, f) == (off), "SAVE LAYOUT LOCK: fish_save_t." #f " moved")
FISH_AT(preset, 0); FISH_AT(stage, 1); FISH_AT(pad, 2); FISH_AT(size, 4); FISH_AT(trust, 8);
FISH_AT(bold, 12); FISH_AT(sociable, 16); FISH_AT(bold0, 20); FISH_AT(sociable0, 24);
FISH_AT(hunger, 28); FISH_AT(energy, 32); FISH_AT(stress, 36); FISH_AT(curiosity, 40);
FISH_AT(age_s, 44); FISH_AT(rest_dx, 48); FISH_AT(rest_dy, 52);
FISH_AT(eaten, 56); FISH_AT(eaten_player, 60); FISH_AT(ms_bits, 64);
_Static_assert(sizeof(fish_save_t) == 68, "SAVE LAYOUT LOCK: fish_save_t is frozen (a new per-fish field = a new save_t tail array)");
SAVE_AT(magic, 0); SAVE_AT(saved_unix, 8); SAVE_AT(clock, 16);
SAVE_AT(light_override, 20); SAVE_AT(light_on, 21); SAVE_AT(arrival_pending, 22); SAVE_AT(n_fish, 23);
SAVE_AT(feed_spot_x, 24); SAVE_AT(player_feedings, 28); SAVE_AT(hold_approaches, 32); SAVE_AT(tank_ms_bits, 36);
SAVE_AT(fish, 40);                                             /* 6 x 68 */
SAVE_AT(veg_growth, 448); SAVE_AT(algae, 460); SAVE_AT(trims, 1104); SAVE_AT(cells_cleaned, 1108);   /* upkeep, 08-30 */
SAVE_AT(veg_h, 1112);                                          /* fronds, 09-04 */
SAVE_AT(setup_pending, 1304); SAVE_AT(pad_id, 1305); SAVE_AT(names, 1308);                            /* identity, 09-13 */
SAVE_AT(body, 1356); SAVE_AT(accent, 1380); SAVE_AT(bubble_x, 1404);
SAVE_AT(ms_seen, 1408); SAVE_AT(tank_ms_seen, 1432);           /* seen masks, 09-13 */
SAVE_AT(newborn_p1, 1436); SAVE_AT(parent_p1, 1437); SAVE_AT(pad_fam, 1449);                          /* family, 09-14 */
SAVE_AT(drift_acc, 1452);                                      /* drift, 09-15 */
SAVE_AT(light_idle_s, 1476); SAVE_AT(light_auto, 1478); SAVE_AT(light_manual_off, 1479);              /* light, 09-15 */
SAVE_AT(sd_balance, 1480); SAVE_AT(sd_earned, 1484); SAVE_AT(sd_unlocks, 1488); SAVE_AT(sd_paid_fish, 1492);   /* shop, 09-15 */
SAVE_AT(sd_colonies_paid, 1516); SAVE_AT(sd_inches_paid, 1520); SAVE_AT(algae_colonies, 1524);
SAVE_AT(trim_px, 1528); SAVE_AT(snail_x, 1532); SAVE_AT(snail_y, 1536); SAVE_AT(veg_h3, 1540);
SAVE_AT(plant_x, 1604); SAVE_AT(plant_z1, 1608); SAVE_AT(pad_place, 1609);                            /* placement, 09-16 */
SAVE_AT(snail_grazed, 1612);
SAVE_AT(castle_x, 1616); SAVE_AT(castle_z1, 1620); SAVE_AT(pad_castle, 1621);                         /* castle, 09-16 */
SAVE_AT(coral_x, 1624); SAVE_AT(coral_z1, 1628); SAVE_AT(pad_coral, 1629); SAVE_AT(coral_rgb, 1632);  /* coral, 09-23 */
SAVE_AT(coral_growth, 1636);
SAVE_AT(cluster_x, 1640); SAVE_AT(cluster_z1, 1644); SAVE_AT(cluster_scheme, 1645);                   /* reef cluster, 09-24 */
SAVE_AT(fry_seed, 1646); SAVE_AT(cluster_growth, 1648);                                                 /* (1646: pad_cluster until 0.3.5's fry seed) */
/* (the next field: SAVE_AT(its_name, 1652 or its type's alignment past it);) */
_Static_assert(sizeof(save_t) >= 1656, "SAVE LAYOUT LOCK: save_t only ever grows");
SAVE_AT(shrimp_n, 1652); SAVE_AT(shrimp_food, 1653); SAVE_AT(fry_age_min, 1654); SAVE_AT(shrimp_cool, 1656);   /* shrimp, 09-29 (1654: pad_shrimp until 0.3.5's fry clock) */
SAVE_AT(shrimp_eaten, 1660);
_Static_assert(sizeof(save_t) >= 1664, "SAVE LAYOUT LOCK: save_t only ever grows");
SAVE_AT(saved_release, 1664);                                                                          /* release stamp, 09-29 (0.2.0) */
_Static_assert(sizeof(save_t) >= 1672, "SAVE LAYOUT LOCK: save_t only ever grows");
SAVE_AT(sale_meals_need, 1668);                                                                        /* a fish sold, 10-01 */
_Static_assert(sizeof(save_t) >= 1672, "SAVE LAYOUT LOCK: save_t only ever grows");
SAVE_AT(screen_turned, 1672); SAVE_AT(orient, 1673); SAVE_AT(autofeed_off, 1674); SAVE_AT(pad_screen, 1675);                  /* the watch's way up, 10-02 */
_Static_assert(sizeof(save_t) >= 1680, "SAVE LAYOUT LOCK: save_t only ever grows");
SAVE_AT(urchin_x, 1676); SAVE_AT(urchin_grazed_px, 1680);                                              /* the urchin, 10-02 */
_Static_assert(sizeof(save_t) >= 1688, "SAVE LAYOUT LOCK: save_t only ever grows");
SAVE_AT(light_tip_seen, 1684); SAVE_AT(tools_locked, 1685); SAVE_AT(fry_sold, 1686); SAVE_AT(pad_tip, 1687);   /* the lights-out notice, 10-03; the toolbox's LOCK and the fry's blanks, 0.3.5 */
_Static_assert(sizeof(save_t) >= 1688, "SAVE LAYOUT LOCK: save_t only ever grows");
SAVE_AT(pad_04, 1688);                                                                                 /* main's 0.4 tail, reserved (zeros here) */
SAVE_AT(sh_s, 1864); SAVE_AT(sh_hitches, 1868); SAVE_AT(sh_trust_passed, 1872); SAVE_AT(sh_eaten, 1876);  /* the seahorse, 0.3.6 (1876: pad_sh until its snacks) */
_Static_assert(sizeof(save_t) >= 1880, "SAVE LAYOUT LOCK: save_t only ever grows");
SAVE_AT(sh_snack_pay, 1880); SAVE_AT(sd_snacks_paid, 1884); SAVE_AT(sh_snack_cool, 1888);               /* its snacks, 0.3.6 */
_Static_assert(sizeof(save_t) >= 1896, "SAVE LAYOUT LOCK: save_t only ever grows");   /* 1892 of fields, padded to 8 */
SAVE_AT(pumpkin_x, 1892); SAVE_AT(pumpkin_z1, 1896); SAVE_AT(pad_pk, 1897);                            /* the pumpkin lantern, 0.3.6 */
_Static_assert(sizeof(save_t) >= 1904, "SAVE LAYOUT LOCK: save_t only ever grows");   /* 1900 of fields, padded to 8 */
SAVE_AT(decor_rank, 1900); SAVE_AT(pad_rank, 1905);                                                    /* the pieces' order, 0.3.6 */
_Static_assert(sizeof(save_t) >= 1912, "SAVE LAYOUT LOCK: save_t only ever grows");   /* 1908 of fields, padded to 8 */
SAVE_AT(chest_x, 1908); SAVE_AT(chest_z1, 1912); SAVE_AT(chest_rank, 1913);
SAVE_AT(pad_chest, 1914);
_Static_assert(sizeof(save_t) >= 1920, "SAVE LAYOUT LOCK: save_t only ever grows");
SAVE_AT(pad_chest_tail, 1916);
SAVE_AT(ornament_x, 1920); SAVE_AT(ornament_z1, 1936); SAVE_AT(ornament_rank, 1940);
SAVE_AT(sand_style, 1944); SAVE_AT(water_style, 1945);
SAVE_AT(pad_palette_tail, 1948); SAVE_AT(extra_x, 1952);
SAVE_AT(extra_z1, 1976); SAVE_AT(extra_rank, 1982);
SAVE_AT(fish_variety, 1988); SAVE_AT(fish_pattern, 1994);
SAVE_AT(journal, 2000); SAVE_AT(pad_journal, 2036);
SAVE_AT(bank_count, 2040); SAVE_AT(bank, 2044);
_Static_assert(sizeof(bank_fish_t) == 96, "SAVE LAYOUT LOCK: bank fish");
SAVE_AT(aquariums, 3968);
_Static_assert(sizeof(aquarium_state_t) == 32, "SAVE LAYOUT LOCK: aquarium metadata and name");
SAVE_AT(companion, 4000);
SAVE_AT(polish, 4520);
SAVE_AT(story, 5416);
SAVE_AT(games, 5848);
SAVE_AT(rpg, 5888);
SAVE_AT(creative, 5912);
SAVE_AT(games_extra, 5920);
SAVE_AT(profile, 5960);
SAVE_AT(care, 6008);
SAVE_AT(inventory, 6032);
_Static_assert(offsetof(inventory_state_t, stored) == 0 &&
               offsetof(inventory_state_t, paid) == 16 &&
               offsetof(inventory_state_t, extra) == 32 &&
               offsetof(inventory_state_t, version) == 96 &&
               offsetof(inventory_extra_t, x) == 4, "inventory wire offsets");
SAVE_AT(mussel, 6132);
SAVE_AT(game_income, 6140);
_Static_assert(sizeof(save_t) == 6168, "inventory mussel and game income tail");
_Static_assert(sizeof(save_t) == AQUARIUM_SAVE_BYTES, "SAVE LAYOUT LOCK: companion tail");
/* Historical NVS budget: older saves were one blob in nvs (0x9000, 0x6000 =
 * 6 pages of 4096 B; tools/make_installer.py pins the row). A page is 126
 * entries of 32 B, and NVS keeps one page free for its garbage collection:
 * 5 x 126 = 630 entries usable. A blob costs an entry per 32 B, a header per
 * chunk (a chunk stays on one page) and an index entry, and nvs_set_blob
 * writes the new copy before it erases the old - on top of the director's
 * parked "bk" copy, three at once at worst. At the 5920-byte ceiling,
 * payload + two chunk headers + index costs 188 entries per copy:
 * 3 x 188 + 64 reserved for namespaces, Wi-Fi, settings and history = 628,
 * below 630, retaining the dedicated GC page. Inactive aquariums live on
 * SD, not additional NVS blobs. Current 6032-byte saves exceed this budget
 * and use the existing SPIFFS storage partition with CRC/readback dual copies.
 * NVS is migration input only; the old director backup is disabled.
 * Older SD records zero-fill appended tails after checksum validation. */
#define SAVE_NVS_BUDGET 5920
_Static_assert(offsetof(save_t, games_extra) == SAVE_NVS_BUDGET, "legacy NVS prefix is frozen");

float progression_time_scale = 1.0f;

/* seconds of tended life per fish. A double (0.3.5): a float summed a frame
 * at a time rounds every step once it passes ~36 h (its step is 1/64 s
 * there, 1/32 past ~73 h) - with elder at 96 h it came anywhere from 89 h
 * to 105 h depending on the frame rate. The save keeps a float, once. */
static double s_age[N_FISH_MAX];
static uint32_t s_loaded_release;    /* the release that wrote the save this boot loaded (0 = older / none) */
static int64_t  s_loaded_unix;       /* ... and its wall-clock stamp (0 = none) */
static float s_since_save, s_dirty_since;
static bool  s_dirty;
static bool  s_ravenous;             /* begging/frenzy active until everyone's fed / give-up */
static float s_ravenous_t;           /* seconds spent begging (dash time excluded) */
static int   s_rav_feedings0;        /* player_feedings when the episode began (tank.ravenous_fed) */
static bool  s_gave_up;              /* AUTO FEED off: they begged, nobody came, nothing fell - no more begging
                                        until the keeper feeds (s_gave_feedings0) or someone is no longer starving */
static int   s_gave_feedings0;
static float s_starve_s[N_FISH_MAX]; /* seconds each fish has been starving in a lit tank with AUTO FEED off */
static bool  s_arrival_pending;
static float s_spawn_in = -1;        /* seconds until the spawning starts (-1 = not counting) */
static bool  s_prev_night;
static bool  s_booted;
static bool  s_hard_save_failed, s_hard_departed;
static bool  s_wipe_save_pending;
static bool  s_setup_pending;        /* the first-run flow still owed (setup.c) */
static int   s_newborn = -1;         /* the arrival still owed its birth flow (setup.c), or -1 */
static int   s_sd_pending;           /* dollars awarded and not yet shown (the toast) */
static int32_t s_sale_need;               /* player_feedings the next fry waits for since a fish was sold (0 = none sold) */
static uint16_t s_fry_seed;               /* the fry offer (progression_fry_*): its seed, 0 = none drawn */
static double   s_fry_age;                /* ... its age in tank seconds (a double, as s_age) */
static uint8_t  s_fry_sold;               /* ... bit k = fry k bought */

static float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }
static void mark_dirty(void) { if (!s_dirty) { s_dirty = true; s_dirty_since = 0; } }
enum { JM_MEALS, JM_CLEAN, JM_TRIM, JM_HOLD, JM_TRUST, JM_GROWN, JM_SEEN, JM_DECOR, JM_POP };
const journal_goal_t JOURNAL_GOALS[JOURNAL_GOAL_N] = {
    {"FIRST MEALS", "SERVE MEALS THAT GET EATEN", JM_MEALS, 3, 50, 25},
    {"CLEAR VIEW", "CLEAN ALGAE COLONIES", JM_CLEAN, 5, 50, 25},
    {"MAKE IT YOURS", "OWN A PLACEABLE DECORATION", JM_DECOR, 1, 50, 30},
    {"A CALM HELLO", "LET A FISH APPROACH YOUR HOLD", JM_HOLD, 1, 60, 30},
    {"GROWING TOGETHER", "RECORD A LOOK ON AN ADULT FISH", JM_GROWN, 1, 80, 50},
    {"GREEN THUMB", "TRIM 20 CM OF PLANTS", JM_TRIM, 20, 60, 30},
    {"TRUSTED KEEPER", "REACH TRUST 4 WITH ANY FISH", JM_TRUST, 4, 90, 50},
    {"CREATURE COLLECTOR", "RECORD THREE DIFFERENT LOOKS", JM_SEEN, 3, 70, 40},
    {"REGULAR MEALS", "SERVE FIFTEEN EATEN MEALS", JM_MEALS, 15, 90, 50},
    {"A PERSONAL REEF", "OWN FOUR PLACEABLE DECORATIONS", JM_DECOR, 4, 100, 60},
    {"HELPING HAND", "CLEAN 25 ALGAE COLONIES", JM_CLEAN, 25, 100, 60},
    {"CLOSE FRIEND", "REACH TRUST 6 WITH ANY FISH", JM_TRUST, 6, 130, 80},
    {"NEW GENERATION", "HAVE THREE FISH IN YOUR TANK", JM_POP, 3, 150, 100},
    {"OCEAN ALBUM", "RECORD SEVEN DIFFERENT LOOKS", JM_SEEN, 7, 130, 80},
    {"MASTER KEEPER", "RECORD FIVE UNIQUE ADULT LOOKS", JM_GROWN, 5, 200, 120}
};
const char *const JOURNAL_THEME_NAMES[JOURNAL_THEME_COUNT] = {
    "ORIGINAL", "KELP FOREST", "CORAL REEF", "DEEP GLOW",
    "SUNKEN RUINS", "SUNSET LAGOON", "ICE CAVERN", "MOONLIT REEF"
};
const uint8_t JOURNAL_THEME_LEVELS[JOURNAL_THEME_COUNT] = {1, 3, 6, 10, 5, 8, 12, 15};
const uint8_t JOURNAL_VARIETY_LEVELS[FISH_VARIETY_COUNT] = {1, 6, 2, 3, 10, 12, 15, 14, 4, 8};
#define CREATIVE_MARKER 0x43525631u
#define PROFILE_MARKER 0x50524631u
#if defined(__GNUC__)
__attribute__((weak)) bool persist_port_blocked(void) { return false; }
#endif
static bool profile_valid(const profile_state_t *s) {
    if (s->marker != PROFILE_MARKER || s->version != 1 || s->mode > GAME_MODE_HARD ||
        s->chosen > 1 || (s->reserved & ~(7u | PROFILE_SHARED_BANK)) || (s->chosen && !(s->reserved & (1u << s->mode))) ||
        (s->paid_items & ~s->owned_items) || (s->owned_items >> SD_ITEM_COUNT)) return false;
    for (int i = 0; i < N_FISH_MAX; i++)
        if (!isfinite(s->starvation_s[i]) || s->starvation_s[i] < 0 ||
            s->starvation_s[i] > HARD_STARVATION_S ||
            (s->mode != GAME_MODE_HARD && s->starvation_s[i] != 0)) return false;
    return true;
}
game_mode_t progression_mode(const tank_t *t) {
    return t && t->profile.marker == PROFILE_MARKER && t->profile.mode <= GAME_MODE_HARD ?
        (game_mode_t)t->profile.mode : GAME_MODE_NORMAL;
}
bool progression_mode_chosen(const tank_t *t) {
    return t && t->profile.marker == PROFILE_MARKER && t->profile.chosen == 1;
}
bool progression_hard(const tank_t *t) { return progression_mode(t) == GAME_MODE_HARD; }
static bool creative_valid(const creative_state_t *s) {
    return (!s->marker && !s->version && !s->enabled && !s->reserved) ||
        (s->marker == CREATIVE_MARKER && s->version == 1 && s->enabled == 1 && !s->reserved);
}
bool progression_creative(const tank_t *t) {
    return progression_mode(t) == GAME_MODE_CREATIVE;
}
bool journal_variety_unlocked(const tank_t *t, int variety) {
    if (variety < 0 || variety >= FISH_VARIETY_COUNT) return false;
    if (progression_creative(t)) return true;
    if (journal_level(t) >= JOURNAL_VARIETY_LEVELS[variety] || (t->journal.seen & (1u << variety))) return true;
    for (int i = 0; i < t->n_fish; i++) if (t->fish[i].variety == variety) return true;
    for (int i = 0; i < t->bank_count; i++) if (bank_variety(&t->bank[i]) == variety) return true;
    return false;
}
static unsigned journal_bits(uint32_t bits) { unsigned n = 0; while (bits) { n += bits & 1u; bits >>= 1; } return n; }
uint32_t journal_level_xp(int level) {
    if (level > JOURNAL_LEVEL_MAX) level = JOURNAL_LEVEL_MAX;
    return level <= 1 ? 0 : (uint32_t)(20 * level * (level - 1));
}
int journal_level(const tank_t *t) {
    int level = 1;
    while (level < JOURNAL_LEVEL_MAX && t->journal.xp >= journal_level_xp(level + 1)) level++;
    return level;
}
int journal_active(const tank_t *t, int slot) {
    if (slot < 0 || slot >= 3) return -1;
    for (int i = 0; i < JOURNAL_GOAL_N; i++) if (!(t->journal.claimed & (1u << i)) && slot-- == 0) return i;
    return -1;
}
unsigned journal_progress(const tank_t *t, int goal) {
    if (goal < 0 || goal >= JOURNAL_GOAL_N) return 0;
    unsigned n = 0;
    switch (JOURNAL_GOALS[goal].metric) {
        case JM_MEALS: n = t->player_feedings > 0 ? (unsigned)t->player_feedings : 0; break;
        case JM_CLEAN: n = t->algae_colonies > 0 ? (unsigned)t->algae_colonies : 0; break;
        case JM_TRIM: n = (unsigned)fminf(fmaxf(t->trim_px / PX_PER_CM, 0), 1000000); break;
        case JM_HOLD: n = t->hold_approaches > 0 ? (unsigned)t->hold_approaches : 0; break;
        case JM_TRUST: n = t->journal.trust; break;
        case JM_GROWN: n = journal_bits(t->journal.grown); break;
        case JM_SEEN: n = journal_bits(t->journal.seen); break;
        case JM_DECOR:
            for (int i = 0; i < SD_ITEM_COUNT; i++)
                n += (tank_decor_placeable(i) || SD_IS_FIXED_SCENERY(i)) && progression_item_unlocked(t, i);
            break;
        case JM_POP: n = t->journal.population; break;
    }
    return n > JOURNAL_GOALS[goal].target ? JOURNAL_GOALS[goal].target : n;
}
static void journal_add_xp(tank_t *t, uint32_t xp) {
    if (progression_creative(t)) return;
    t->journal.xp += xp > JOURNAL_XP_MAX - t->journal.xp ? JOURNAL_XP_MAX - t->journal.xp : xp;
}
bool journal_theme_unlocked(const tank_t *t, int theme) {
    return theme >= 0 && theme < JOURNAL_THEME_COUNT &&
        (progression_creative(t) || journal_level(t) >= JOURNAL_THEME_LEVELS[theme]);
}
bool journal_set_theme(tank_t *t, int theme) {
    if (!journal_theme_unlocked(t, theme)) return false;
    uint8_t old = t->journal.theme; t->journal.theme = (uint8_t)theme;
    if (progression_save(t)) return true;
    t->journal.theme = old; return false;
}
bool journal_claim(tank_t *t, int goal) {
    if (progression_creative(t)) return false;
    bool active = false;
    for (int slot = 0; slot < 3; slot++) active |= journal_active(t, slot) == goal;
    if (!active || goal < 0 || goal >= JOURNAL_GOAL_N || journal_progress(t, goal) < JOURNAL_GOALS[goal].target) return false;
    int reward = JOURNAL_GOALS[goal].coins;
    if (t->sd_balance > INT_MAX - reward || t->sd_earned > INT_MAX - reward) return false;
    journal_state_t old = t->journal;
    t->journal.claimed |= 1u << goal; journal_add_xp(t, JOURNAL_GOALS[goal].xp);
    t->sd_balance += reward; t->sd_earned += reward;
    if (progression_save(t)) return true;
    t->journal = old; t->sd_balance -= reward; t->sd_earned -= reward; return false;
}
const char *const CARE_TASK_NAMES[CARE_TASK_COUNT] = {"A GOOD MEAL", "A LITTLE CONNECTION", "YOUR DAILY REEF"};
const char *const CARE_TASK_DETAILS[CARE_TASK_COUNT] = {
    "ONE PLAYER FEEDING EATEN",
    "SAY HELLO OR CALM HOLD 60S",
    "TEND PLANTS/GLASS OR WATCH 5M"
};
static const char *s_shop_notice;
const uint8_t CARE_COINS[CARE_TASK_COUNT] = {30, 30, 65};
const uint8_t CARE_XP[CARE_TASK_COUNT] = {12, 13, 25};
static bool care_valid(const care_state_t *s) {
    if (!s->version) {
        care_state_t empty = {0};
        return !memcmp(s, &empty, sizeof empty);
    }
    return s->version == 1 && s->day >= 0 && s->day <= 47482 &&
        s->elapsed_s < 86400 && s->viewed_s <= CARE_VIEW_SECONDS &&
        s->calm_s <= CARE_CALM_SECONDS && !(s->ready & ~7u) &&
        !(s->claimed & ~s->ready) && s->game_paid <= GAME_DAILY_TOTAL && s->fallback <= 1;
}
static bool game_income_valid(game_income_state_t *s, unsigned total) {
    if (!s->version) {
        game_income_state_t empty = {0};
        if (memcmp(s, &empty, sizeof empty) || total > CARE_GAME_LIMIT) return false;
        s->version = 1; s->legacy = (uint8_t)total;
    }
    unsigned sum = s->legacy;
    if (s->version != 1 || s->legacy > CARE_GAME_LIMIT) return false;
    for (int i = 0; i < 4; i++) if (s->reserved[i]) return false;
    for (int i = 0; i < GAME_INCOME_COUNT; i++) {
        if (s->paid[i] + s->legacy > CARE_GAME_LIMIT) return false;
        sum += s->paid[i];
    }
    return sum == total && sum <= GAME_DAILY_TOTAL;
}
static bool mussel_valid(const mussel_state_t *s) {
    if (s->version > 2 || (s->version < 2 && s->owned > 1) || s->seconds >= MUSSEL_SECONDS ||
        s->bank > MUSSEL_DAILY_LIMIT || s->generated > MUSSEL_DAILY_LIMIT ||
        s->reserved[0] || s->reserved[1]) return false;
    return s->owned ? s->version >= 1 : !s->seconds && !s->bank && !s->generated;
}
void progression_care_refresh(tank_t *t) {
    int64_t now = clock_port_now_unix();
    int64_t day = now >= INT64_C(1609459200) && now <= INT64_C(4102444800) ? now / 86400 : 0;
    care_state_t *s = &t->care;
    if (!s->version) {
        memset(s, 0, sizeof *s);
        s->version = 1; s->day = day;
        mark_dirty();
    } else if (day && (!s->day || s->fallback)) {
        /* Clock synchronization adopts an existing fallback allowance, never a second payout. */
        if (day > s->day) s->day = day;
        s->fallback = 0; mark_dirty();
    } else if ((day && s->day && day > s->day) || (!day && s->elapsed_s >= 86400)) {
        int64_t high = day > s->day ? day : s->day;
        memset(s, 0, sizeof *s);
        s->version = 1; s->day = high; s->fallback = !day;
        t->mussel.generated = 0;
        memset(&t->game_income, 0, sizeof t->game_income);
        t->game_income.version = 1;
        mark_dirty();
    }
}
void progression_care_greet(tank_t *t) {
    progression_care_refresh(t);
    if (!progression_creative(t)) { t->care.ready |= 2; mark_dirty(); }
}
bool progression_care_claim(tank_t *t, int task) {
    progression_care_refresh(t);
    if (progression_creative(t) || task < 0 || task >= CARE_TASK_COUNT ||
        !(t->care.ready & (1u << task)) || (t->care.claimed & (1u << task))) return false;
    int coins = CARE_COINS[task];
    if (t->sd_balance > INT_MAX - coins || t->sd_earned > INT_MAX - coins) {
        fprintf(stderr, "Daily care reward would overflow wallet\n"); return false;
    }
    uint32_t xp = t->journal.xp;
    t->care.claimed |= (uint8_t)(1u << task);
    t->sd_balance += coins; t->sd_earned += coins;
    journal_add_xp(t, CARE_XP[task]);
    if (progression_save(t)) return true;
    t->care.claimed &= (uint8_t)~(1u << task);
    t->sd_balance -= coins; t->sd_earned -= coins; t->journal.xp = xp;
    fprintf(stderr, "Daily care save failed; reward rolled back\n");
    return false;
}
bool progression_mussel_collect(tank_t *t) {
    s_shop_notice = NULL;
    if (!t || progression_creative(t) || !t->mussel.owned || !t->mussel.bank) {
        s_shop_notice = "NO PEARLS READY"; return false;
    }
    unsigned coins = t->mussel.bank;
    if (t->sd_balance > INT_MAX - (int)coins || t->sd_earned > INT_MAX - (int)coins) {
        fprintf(stderr, "Mussel income would overflow wallet; pearls retained for retry\n");
        s_shop_notice = "WALLET FULL - NOT COLLECTED"; return false;
    }
    t->mussel.bank = 0; t->sd_balance += coins; t->sd_earned += coins;
    if (progression_save(t)) { t->mussel_glint = 1.6f; return true; }
    t->mussel.bank = (uint8_t)coins; t->sd_balance -= coins; t->sd_earned -= coins;
    s_shop_notice = "SAVE FAILED - PEARLS KEPT";
    fprintf(stderr, "Mussel income save failed; pearls retained for retry\n");
    return false;
}
bool progression_mussel_healthy(const tank_t *t) {
    if (!t || !t->n_fish) return false;
    for (int i = 0; i < t->n_fish; i++)
        if (!(t->fish[i].hunger < 8.5f) || !(t->fish[i].stress < 7)) return false;
    return true;
}
static void mussel_tick(tank_t *t, float dt) {
    if (!isfinite(dt) || dt <= 0 || progression_creative(t) || !t->mussel.owned) return;
    double total = t->mussel_fraction + fminf(dt, 1);
    unsigned seconds = (unsigned)total;
    t->mussel_fraction = total - seconds;
    if (!seconds) return;
    /* Older saves may have uncollected pearls; settle them without a collection menu. */
    if (t->mussel.bank && !progression_mussel_collect(t)) return;
    if (!progression_item_owned(t, SD_SHOP_MUSSEL) ||
        t->mussel.generated >= MUSSEL_DAILY_LIMIT || !progression_mussel_healthy(t)) return;
    t->mussel.seconds += (uint16_t)seconds;
    if (t->mussel.seconds >= MUSSEL_SECONDS) {
        t->mussel.seconds -= MUSSEL_SECONDS;
        t->mussel.bank++; t->mussel.generated++;
        progression_mussel_collect(t);
    }
    mark_dirty();
}
static void care_tick(tank_t *t, float dt) {
    if (!isfinite(dt) || dt <= 0 || progression_creative(t)) return;
    progression_care_refresh(t);
    float total = t->care_fraction + fminf(dt, 86400);
    unsigned seconds = (unsigned)total;
    t->care_fraction = total - seconds;
    t->care.elapsed_s = (uint32_t)fminf(86400, t->care.elapsed_s + seconds);
    progression_care_refresh(t);
    /* A synchronized day uses the wall clock; elapsed time is only the offline-clock fallback. */
    if (t->care.elapsed_s >= 86400) t->care.elapsed_s = 86399;
    if (t->ui_cover || !t->n_fish || t->night) return;
    bool settled = false, nearby = false;
    for (int i = 0; i < t->n_fish; i++) {
        const fish_t *f = &t->fish[i];
        if (f->hunger < 8.5f && f->stress < 7) settled = true;
        if (t->hold_active && t->hold_time >= 2.7f &&
            tank_dist(f->x, f->y, t->hold_x, t->hold_y) < 80 && f->stress < 7) nearby = true;
    }
    if (settled) t->care.viewed_s = (uint16_t)fminf(CARE_VIEW_SECONDS, t->care.viewed_s + seconds);
    if (nearby) t->care.calm_s = (uint16_t)fminf(CARE_CALM_SECONDS, t->care.calm_s + seconds);
    uint8_t ready = t->care.ready;
    if (t->care.calm_s >= CARE_CALM_SECONDS) t->care.ready |= 2;
    if (t->care.viewed_s >= CARE_VIEW_SECONDS) t->care.ready |= 4;
    if (ready != t->care.ready) mark_dirty();
}
void journal_tick(tank_t *t) {
    if (progression_creative(t)) return;
    progression_care_refresh(t);
    journal_state_t before = t->journal;
    uint32_t values[4] = {
        (uint32_t)(t->player_feedings > 0 ? t->player_feedings : 0),
        (uint32_t)(t->algae_colonies > 0 ? t->algae_colonies : 0),
        (uint32_t)fminf(fmaxf(t->trim_px / (PX_PER_CM * 10), 0), 1000000),
        (uint32_t)(t->hold_approaches > 0 ? t->hold_approaches : 0)
    };
    for (int i = 0; i < 4; i++) {
        if (t->journal.initialized && values[i] > t->journal.baseline[i]) {
            if (i == 0) t->care.ready |= 1;
            if (i == 1 || i == 2) t->care.ready |= 4;
            mark_dirty();
        }
        t->journal.baseline[i] = values[i];
    }
    t->journal.initialized = 1;
    if (t->n_fish > t->journal.population) t->journal.population = (uint8_t)t->n_fish;
    for (int i = 0; i < t->n_fish; i++) {
        const fish_t *f = &t->fish[i];
        if (f->trust > t->journal.trust) t->journal.trust = (uint8_t)f->trust;
        if (f->variety < FISH_VARIETY_COUNT) {
            t->journal.seen |= 1u << f->variety;
            if (f->stage >= STAGE_ADULT) t->journal.grown |= 1u << f->variety;
        }
    }
    for (int i = 0; i < t->bank_count; i++) {
        int variety = bank_variety(&t->bank[i]);
        if (variety < FISH_VARIETY_COUNT) t->journal.seen |= 1u << variety;
    }
    if (memcmp(&before, &t->journal, sizeof before)) mark_dirty();
}

float progression_age_s(const tank_t *t, int idx) { (void)t; return idx >= 0 && idx < N_FISH_MAX ? (float)s_age[idx] : 0; }
bool  progression_arrival_pending(void) { return s_arrival_pending; }
bool  progression_setup_pending(void)   { return s_setup_pending; }
bool progression_setup_done(tank_t *t) {
    bool pending = s_setup_pending;
    s_setup_pending = false;
    if (progression_save(t)) return true;
    s_setup_pending = pending;
    fprintf(stderr, "Welcome completion save failed; retry required\n");
    return false;
}
int   progression_newborn(void)         { return s_newborn; }
uint32_t progression_loaded_release(void) { return s_loaded_release; }
int64_t  progression_loaded_unix(void) { return s_loaded_unix; }
bool progression_newborn_done(tank_t *t) {
    int pending = s_newborn;
    s_newborn = -1;
    if (progression_save(t)) return true;
    s_newborn = pending;
    fprintf(stderr, "Fry welcome completion save failed; retry required\n");
    return false;
}

static void set_ms(fish_t *f, uint32_t bit) { if (!(f->ms_bits & bit)) { f->ms_bits |= bit; mark_dirty(); } }
/* ---- sand dollars ---- */
const sd_item_t SD_ITEMS[SD_ITEM_COUNT] = {
    { SD_ITEM_PLANT, "SWORD PLANT", "BROAD, VERTICAL LEAVES", "MORE COVER FOR YOUR CRITTERS", SD_PRICE_PLANT },   /* Strato's words (2026-09-16); the second line is 28 chars, the shop modal is 352 wide for it */
    { SD_ITEM_SNAIL, "SNAIL",       "GRAZES THE GLASS CLEAN,",   "EVEN WHILE THE TANK SLEEPS",  SD_PRICE_SNAIL },
    { SD_ITEM_CASTLE, "CASTLE",     "STONE TOWERS AND AN ARCH",  "THE FISH SWIM THROUGH IT",    SD_PRICE_CASTLE },   /* 2026-09-16 */
    { SD_ITEM_CORAL,  "CORAL",      "A BRANCHING REEF CORAL,",   "GROWS FOR WEEKS, YOUR COLOR", SD_PRICE_CORAL },    /* 2026-09-23 */
    { SD_ITEM_CLUSTER, "REEF CLUSTER", "A MATURE REEF ON A ROCK,", "FILLS OUT, THEN IT BLOOMS",  SD_PRICE_CLUSTER },  /* 2026-09-24: the dearest; three looks on its page */
    { SD_ITEM_SHRIMP,  "SHRIMP",    "A SCHOOL OF CHERRY SHRIMP", "THEY EAT SCRAPS AND MULTIPLY", SD_PRICE_SHRIMP },
    { SD_ITEM_URCHIN,  "SEA URCHIN", "NIBBLES THE TALL GRASS,",  "EVEN WHILE THE TANK SLEEPS",  SD_PRICE_URCHIN },  /* 2026-10-02: the episode 5 promise, a resident like the snail */  /* 2026-09-29: a resident, like the snail; Strato: "should mention that they multiply" (28 chars, as the plant's) */
    { SD_ITEM_SEAHORSE, "SEAHORSE",  "HOLDS ON TO YOUR FINGER,",  "FISH TRUST YOU TWICE AS FAST", SD_PRICE_SEAHORSE },  /* 0.3.6: the tame one (28 chars, as the plant's) */
    { SD_ITEM_PUMPKIN, "PUMPKIN",    "GLOWS WHEN THE LIGHTS GO OUT", "FOR OCTOBER, YOURS TO KEEP", SD_PRICE_PUMPKIN },  /* 0.3.6: the seasonal lantern (Strato 10-17) */
    { SD_ITEM_CHEST, "TREASURE CHEST", "A SUNKEN CHEST OF GOLD", "DRAG IT TO YOUR FAVORITE SPOT", SD_PRICE_CHEST },
    { SD_ITEM_SHELL, "SCALLOP SHELL", "A PEARLY SEASIDE SOUVENIR", "A LITTLE COLOR ON THE SAND", SD_PRICE_SHELL },
    { SD_ITEM_STARFISH, "STARFISH", "A CORAL-COLORED ORNAMENT", "FIVE ARMS, NO EXTRA FEEDING", SD_PRICE_STARFISH },
    { SD_ITEM_ANCHOR, "SUNKEN ANCHOR", "A WEATHERED NAUTICAL RELIC", "MAKE A LITTLE SHIPWRECK", SD_PRICE_ANCHOR },
    { SD_ITEM_BUOY, "TETHERED BUOY", "BOBS ABOVE A WEIGHTED ROPE", "FILL THE OPEN WATER", SD_PRICE_BUOY },
    { SD_ITEM_SAND, "SAND PALETTE", "NATURAL, NEON AND RAINBOW", "CHOOSE IN SETTINGS ANY TIME", SD_PRICE_SAND },
    { SD_ITEM_LIGHTS, "MOOD LIGHTS", "CLASSIC, NEON AND RAINBOW", "CHOOSE IN SETTINGS ANY TIME", SD_PRICE_LIGHTS },
    { SD_ITEM_WRECK, "SHIPWRECK", "A BROKEN HULL AND OLD SAIL", "BUILD YOUR OWN LOST HARBOR", 200 },
    { SD_ITEM_URN, "TREASURE URN", "AN ANCIENT POT OF GOLD", "A RELIC FROM THE DEEP", 60 },
    { SD_ITEM_CRYSTAL, "CRYSTALS", "VIVID AMETHYST CRYSTALS", "A LITTLE UNDERWATER MAGIC", 100 },
    { SD_ITEM_FAN, "SEA FAN", "A TALL DECORATIVE SEA FAN", "FILLS THE OPEN WATER", 80 },
    { SD_ITEM_RUINS, "SUNKEN RUINS", "AN ARCH FROM A LOST CITY", "FISH SWIM PAST THE STONES", 160 },
    { SD_ITEM_JELLY, "JELLY LANTERN", "A FLOATING JELLY ORNAMENT", "TETHERED AND GENTLY BOBBING", 120 },
    { SD_ITEM_PEBBLES, "ZEN PEBBLES", "A STACK OF SMOOTH STONES", "FIXED SCENERY ON THE SAND", 40 },
    { SD_ITEM_PEARL, "PEARL CLAM", "AN OPEN SHELL WITH A PEARL", "FIXED SCENERY ON THE SAND", 70 },
    { SD_ITEM_FLOAT_LOG, "FLOATING LOG", "BOBS ALONG THE SURFACE", "FIXED SPOT, NO EXTRA CARE", 90 },
    { SD_ITEM_SPONGES, "TUBE SPONGES", "A BRIGHT GOLD SPONGE PATCH", "FIXED SCENERY ON THE SAND", 100 },
    { SD_ITEM_BARREL, "LOST BARREL", "AN OLD OAK HARBOR BARREL", "FIXED SCENERY ON THE SAND", 110 },
    { SD_ITEM_HELMET, "DIVE HELMET", "A BRASS DEEP-SEA SOUVENIR", "FIXED SCENERY ON THE SAND", 140 },
    { SD_ITEM_LILIES, "LILY PADS", "PADS AND A PINK WATER LILY", "FIXED SPOT AT THE SURFACE", 80 },
    { SD_ITEM_DUCK, "DUCK FLOAT", "A LITTLE YELLOW BOBBING DUCK", "FIXED SPOT AT THE SURFACE", 60 },
    { SD_ITEM_FILTER, "BIO FILTER", "HALVES NEW ALGAE GROWTH", "WORKS AWAKE AND ASLEEP", 180 },
    { SD_ITEM_GARDEN, "SLOW GARDEN", "HALVES GRASS GROWTH ONLY", "WORKS AWAKE AND ASLEEP", 160 },
    { 0, "CALM CURRENT", "25% FASTER REST RECOVERY", "ENERGY ONLY, ALSO IN SLEEP", 160 },
    { 0, "COZY SHELTER", "25% FASTER STRESS DECAY", "BASE CALMING, ALSO IN SLEEP", 180 },
    { 0, "PLAY KIT", "25% MORE EXPLORE CURIOSITY", "NO CHANGE TO CURIOSITY USE", 200 },
    { 0, "TRAINING RING", "25% MORE TRUST FROM HOLDS", "ADDS TO SEAHORSE BONUS", 240 },
    /* fixed sand scenery (1.2): owned in polish.scenery_unlocks, baked into the backdrop */
    { 0, "SEA GLASS", "TUMBLED GLASS IN 3 COLORS", "FIXED SPOT ON THE SAND", 50 },
    { 0, "BRAIN CORAL", "A ROUND, WINDING REEF DOME", "FIXED SPOT ON THE SAND", 110 },
    { 0, "TIKI STATUE", "A CARVED ISLAND GUARDIAN", "FIXED SPOT ON THE SAND", 130 },
    { 0, "BOTTLE NOTE", "A CORKED NOTE FROM AFAR", "FIXED SPOT ON THE SAND", 70 },
    { 0, "ANEMONE", "SOFT PINK TENTACLE BLOOM", "FIXED SPOT ON THE SAND", 90 },
    { 0, "OLD TEAPOT", "A LOST PORCELAIN TEAPOT", "FIXED SPOT ON THE SAND", 80 },
    { 0, "PEARL MUSSEL", "PASSIVE INCOME: 1 COIN/10M", "HEALTHY LIVE TANK. 15/DAY.", SD_PRICE_MUSSEL },
};
_Static_assert(SD_SCENERY_END == SD_SHOP_MUSSEL, "mussel follows the stable scenery IDs");
_Static_assert(SD_SCENERY_N <= 8, "fixed scenery fits polish.scenery_unlocks");
const char *progression_shop_notice(void) { return s_shop_notice; }
void progression_shop_notice_clear(void) { s_shop_notice = NULL; }
static void shop_save_failed(const char *what, const char *name) {
    s_shop_notice = what;
    fprintf(stderr, "Shop save failed for %s; %s\n", name, what);
}
bool progression_item_owned(const tank_t *t, int item) {
    if (!t || item < 0 || item >= SD_ITEM_COUNT) return false;
    if (item == SD_SHOP_MUSSEL)
        return tank_mussel_owned(t);
    if (SD_IS_SCENERY(item)) return (t->polish.scenery_unlocks >> (item - SD_SCENERY_FIRST)) & 1u;
    return item < 32 ? (t->sd_unlocks & SD_ITEMS[item].bit) != 0 :
        (t->aquariums.stat_upgrades & (1u << (item - 32))) != 0;
}
bool progression_item_unlocked(const tank_t *t, int item) {
    return item >= 0 && item < SD_ITEM_COUNT &&
        (progression_creative(t) || (t->profile.owned_items & (UINT64_C(1) << item)) ||
         progression_item_owned(t, item));
}
int progression_shop_count(const tank_t *t) {
    return SD_ITEM_COUNT - !progression_pumpkin_offered(t);
}
int progression_shop_item(const tank_t *t, int row) {
    if (row < 0 || row >= progression_shop_count(t)) return -1;
    int items[SD_ITEM_COUNT], count = 0;
    for (int i = 0; i < SD_ITEM_COUNT; i++) {
        if (i == 8 && !progression_pumpkin_offered(t)) continue;
        int j = count++;
        while (j > 0 && SD_ITEMS[items[j - 1]].price > SD_ITEMS[i].price) {
            items[j] = items[j - 1]; j--;
        }
        items[j] = i;
    }
    return items[row];
}
/* the pumpkin's season (0.3.6; Strato 2026-10-17: "it can remain on their
 * board until they upgrade ... whatever release comes after oct 31 will
 * remove the pumpkin"): no date check - SD_PUMPKIN_IN_SHOP says whether
 * this build sells it; a keeper who has one keeps it (and its shop row, to
 * MOVE or SELL it) whatever the build */
bool progression_pumpkin_offered(const tank_t *t) {
    return progression_item_unlocked(t, 8) || SD_PUMPKIN_IN_SHOP;
}
static void sd_award(tank_t *t, int n) {
    if (n <= 0 || progression_creative(t)) return;
    t->sd_balance += n; t->sd_earned += n; s_sd_pending += n;
    mark_dirty();
}
int  progression_sd_take_award(void) { int n = s_sd_pending; s_sd_pending = 0; return n; }
void progression_sd_grant(tank_t *t, int n) {
    if (progression_creative(t)) return;
    if (n >= 0) sd_award(t, n);
    else { t->sd_balance += n; if (t->sd_balance < 0) t->sd_balance = 0; mark_dirty(); }
}
bool progression_game_reward(tank_t *t, int game, int dollars) {
    if (progression_creative(t)) return false;
    if (game < 0 || game >= GAME_INCOME_COUNT || dollars < 1 || dollars > 10 ||
        t->sd_balance > INT_MAX - dollars || t->sd_earned > INT_MAX - dollars) {
        fprintf(stderr, "Invalid or overflowing mini-game reward: %d\n", dollars); return false;
    }
    progression_care_refresh(t);
    game_income_state_t income_before = t->game_income;
    int paid = progression_game_payout(t, game, dollars);
    t->game_income.version = 1;
    t->game_income.paid[game] += (uint8_t)paid;
    t->care.game_paid += (uint8_t)paid;
    t->sd_balance += paid; t->sd_earned += paid;
    if (progression_save(t)) return true;
    t->sd_balance -= paid; t->sd_earned -= paid; t->care.game_paid -= (uint8_t)paid;
    t->game_income = income_before;
    fprintf(stderr, "Mini-game reward save failed; wallet rolled back\n"); return false;
}
int progression_game_payout(const tank_t *t, int game, int dollars) {
    return game >= 0 && game < GAME_INCOME_COUNT && !progression_creative(t) && dollars > 0 &&
        t->game_income.paid[game] + t->game_income.legacy < CARE_GAME_LIMIT &&
        t->care.game_paid < GAME_DAILY_TOTAL ? 1 : 0;
}
/* Fish firsts retain one-time rewards. Historical chore/snack ledgers keep
 * their frozen fields, but repeating care now pays through the daily checklist. */
static void sd_tick(tank_t *t) {
    if (progression_creative(t)) return;
    for (int i = 0; i < t->n_fish; i++) {
        fish_t *f = &t->fish[i]; uint32_t *paid = &t->sd_paid_fish[i];
        if ((f->ms_bits & MS_REACHED_JUV)   && !(*paid & SD_PAID_JUV))   { *paid |= SD_PAID_JUV;   sd_award(t, SD_STAGE_JUV); }
        if ((f->ms_bits & MS_REACHED_ADULT) && !(*paid & SD_PAID_ADULT)) { *paid |= SD_PAID_ADULT; sd_award(t, SD_STAGE_ADULT); }
        if ((f->ms_bits & MS_REACHED_ELDER) && !(*paid & SD_PAID_ELDER)) { *paid |= SD_PAID_ELDER; sd_award(t, SD_STAGE_ELDER); }
        if (f->trust >= 10.0f && !(*paid & SD_PAID_TRUST))               { *paid |= SD_PAID_TRUST; sd_award(t, SD_TRUST); }
    }
    int32_t hund = t->algae_colonies / SD_CHORE_EVERY;
    if (hund > t->sd_colonies_paid) t->sd_colonies_paid = hund;
    hund = (int32_t)(t->trim_px / (PX_PER_CM * SD_TRIM_CM));
    if (hund > t->sd_inches_paid) t->sd_inches_paid = hund;
    if (t->sh_snack_pay > t->sd_snacks_paid) t->sd_snacks_paid = t->sh_snack_pay;
}
int progression_sell_value(int item) { return item < 0 || item >= SD_ITEM_COUNT ? 0 : SD_ITEMS[item].price * SD_SELL_PCT / 100; }
int progression_sell_value_for(const tank_t *t, int item) {
    if (!t || item < 0 || item >= SD_ITEM_COUNT || progression_creative(t) ||
        (!tank_decor_placeable(item) && !SD_IS_FIXED_SCENERY(item)) ||
        !progression_item_owned(t, item) || !(t->profile.paid_items & (UINT64_C(1) << item))) return 0;
    return progression_sell_value(item);
}
bool progression_sell(tank_t *t, int item) {
    if (item < 0 || item >= SD_ITEM_COUNT || (!tank_decor_placeable(item) && !SD_IS_FIXED_SCENERY(item))) return false;   /* residents and functional upgrades stay */
    const sd_item_t *it = &SD_ITEMS[item];
    if (!progression_item_owned(t, item)) return false;
    int back = progression_sell_value_for(t, item);
    s_shop_notice = NULL;
    if (t->sd_balance > INT_MAX - back) { s_shop_notice = "WALLET FULL - NOT SOLD"; return false; }
    if (SD_IS_SCENERY(item)) {
        uint8_t flag = (uint8_t)(1u << (item - SD_SCENERY_FIRST)), old = t->polish.scenery_unlocks;
        uint64_t paid = t->profile.paid_items, item_bit = UINT64_C(1) << item;
        if (t->sd_balance > INT_MAX - back) return false;
        t->profile.paid_items &= ~item_bit;
        t->polish.scenery_unlocks = (uint8_t)(old & ~flag); t->sd_balance += back; s_sd_pending += back;
        if (progression_save(t)) return true;
        t->polish.scenery_unlocks = old; t->sd_balance -= back; s_sd_pending -= back;
        t->profile.paid_items = paid;
        shop_save_failed("SAVE FAILED - NOT SOLD", it->name);
        return false;
    }
    tank_t *before = NULL;
    if (tank_decor_placeable(item)) {
        before = malloc(sizeof *before);
        if (!before) { fprintf(stderr, "Shop sale needs more memory; no change\n"); return false; }
        *before = *t;
    }
    t->sd_unlocks &= ~it->bit;
    uint64_t paid_before = t->profile.paid_items;
    t->profile.paid_items &= ~(uint64_t)it->bit;
    t->sd_balance += back; s_sd_pending += back;
    if (item >= 22) {
        if (progression_save(t)) { free(before); return true; }
        t->sd_unlocks |= it->bit; t->sd_balance -= back; s_sd_pending -= back;
        t->profile.paid_items = paid_before;
        free(before);
        shop_save_failed("SAVE FAILED - NOT SOLD", it->name);
        return false;
    }
    tank_decor_reset(t, item);
    if (progression_save(t)) { free(before); return true; }
    *t = *before; free(before); s_sd_pending -= back;
    shop_save_failed("SAVE FAILED - NOT SOLD", it->name);
    return false;
}
/* ---- a fish sold (2026-10-01) ---- */
int progression_fish_value(const tank_t *t, int fish) {
    static const int worth[4] = { SD_FISH_FRY, SD_FISH_JUV, SD_FISH_ADULT, SD_FISH_ELDER };
    return fish < 0 || fish >= t->n_fish ? 0 : worth[t->fish[fish].stage & 3];
}
bool progression_fish_sellable(const tank_t *t, int fish) {
    if (progression_creative(t)) return false;
    return fish >= 0 && fish < t->n_fish && t->n_fish > FISH_KEEP_MIN && s_newborn < 0;
}
static fish_save_t capture_fish(const fish_t *f, double age) {
    fish_save_t s = {0};
    s.preset = (uint8_t)f->preset; s.stage = (uint8_t)f->stage;
    s.size = f->size; s.trust = f->trust; s.bold = f->bold; s.sociable = f->sociable;
    s.bold0 = f->bold0; s.sociable0 = f->sociable0;
    s.hunger = f->hunger; s.energy = f->energy; s.stress = f->stress; s.curiosity = f->curiosity;
    s.age_s = (float)age; s.rest_dx = f->rest_dx; s.rest_dy = f->rest_dy;
    s.eaten = f->eaten; s.eaten_player = f->eaten_player; s.ms_bits = f->ms_bits;
    return s;
}
static void restore_fish_state(fish_t *f, const fish_save_t *s) {
    f->size = s->size; f->trust = s->trust; f->bold0 = s->bold0; f->sociable0 = s->sociable0;
    f->hunger = s->hunger; f->energy = s->energy; f->stress = s->stress; f->curiosity = s->curiosity;
    f->eaten = s->eaten; f->eaten_player = s->eaten_player;
    f->ms_bits = s->ms_bits & ~MS_RETIRED_MASK;
    f->rest_dx = s->rest_dx; f->rest_dy = s->rest_dy;
}
static bank_fish_t bank_capture(const tank_t *t, int i) {
    const fish_t *f = &t->fish[i];
    bank_fish_t b = {0}; b.fish = capture_fish(f, s_age[i]);
    snprintf(b.name, sizeof b.name, "%s", f->name);
    b.body = f->color; b.accent = f->accent; b.fish.pad = (uint16_t)(f->variety | (f->pattern << 8));
    b.paid = t->sd_paid_fish[i]; b.seen = f->ms_seen; b.drift = f->drift_acc;
    return b;
}
static void bank_restore(tank_t *t, int i, const bank_fish_t *b) {
    const fish_save_t *s = &b->fish;
    tank_make_fish(t, i, s->preset, s->sociable, s->bold, (stage_t)s->stage);
    fish_t *f = &t->fish[i]; restore_fish_state(f, s);
    tank_set_name(t, i, b->name); tank_set_look(t, i, b->body, b->accent);
    f->variety = bank_variety(b); f->pattern = bank_pattern(b);
    f->ms_seen = b->seen & f->ms_bits; f->drift_acc = b->drift;
    f->parent_a = f->parent_b = -1; /* Slot-based family links cannot follow a fish outside this tank. */
    t->sd_paid_fish[i] = b->paid; s_age[i] = s->age_s; s_starve_s[i] = 0;
}
static int occupied_slots(const tank_t *t) { return t->n_fish + t->companion.away; }
bool progression_adventure_depart(tank_t *t, int adventure, int fish) {
    if (adventure < 0 || adventure >= COMPANION_ADVENTURE_COUNT || fish < 0 || fish >= t->n_fish ||
        t->n_fish < 2 || t->companion.adventure || s_setup_pending || s_newborn >= 0 ||
        (t->companion.adventure_claimed & (1u << adventure))) return false;
    tank_t *next = malloc(sizeof *next);
    if (!next) { fprintf(stderr, "Not enough memory for adventure departure\n"); return false; }
    *next = *t;
    companion_polish_observe(next);
    companion_state_t *s = &next->companion;
    s->version = COMPANION_VERSION; s->adventure = (uint8_t)(adventure + 1); s->adventure_s = 0;
    s->traveler = bank_capture(t, fish); s->traveler_bond = s->active[fish]; s->away = 1;
    next->polish.traveler = next->polish.active[fish];
    next->story.traveler = next->story.active[fish];
    s->adventure_clock = clock_port_now_unix();
    if (s->adventure_clock < 0 || s->adventure_clock > 4102444800LL) s->adventure_clock = 0;
    memcpy(s->portrait_name, t->fish[fish].name, sizeof s->portrait_name);
    s->portrait_body = t->fish[fish].color; s->portrait_accent = t->fish[fish].accent;
    s->portrait_variety = t->fish[fish].variety; s->portrait_pattern = t->fish[fish].pattern;
    s->portrait_wearable = s->active[fish].wearable;
    s->portrait_colors = s->active[fish].wear_colors;
    double old_age[N_FISH_MAX]; float old_starve[N_FISH_MAX];
    memcpy(old_age, s_age, sizeof old_age); memcpy(old_starve, s_starve_s, sizeof old_starve);
    tank_remove_fish(next, fish);
    for (int i = fish; i < next->n_fish; i++) { s_age[i] = s_age[i + 1]; s_starve_s[i] = s_starve_s[i + 1]; }
    s_age[next->n_fish] = 0; s_starve_s[next->n_fish] = 0;
    bool arrival = s_arrival_pending; float spawn = s_spawn_in;
    s_arrival_pending = false; s_spawn_in = -1;
    bool saved = progression_save(next);
    if (saved) *t = *next;
    else {
        memcpy(s_age, old_age, sizeof s_age); memcpy(s_starve_s, old_starve, sizeof s_starve_s);
        s_arrival_pending = arrival; s_spawn_in = spawn;
        fprintf(stderr, "Adventure departure save failed; fish remains in aquarium\n");
    }
    free(next); return saved;
}
bool progression_adventure_return(tank_t *t) {
    companion_state_t *s = &t->companion;
    if (!s->away || !s->adventure || s->adventure_s < companion_adventure_total(s, s->adventure - 1) ||
        t->n_fish >= POP_CAP || t->n_fish >= N_FISH_MAX) return false;
    tank_t *next = malloc(sizeof *next);
    if (!next) { fprintf(stderr, "Not enough memory for adventure return\n"); return false; }
    *next = *t;
    int slot = next->n_fish++, a = s->adventure - 1;
    double age = s_age[slot]; float starve = s_starve_s[slot];
    bank_restore(next, slot, &s->traveler);
    next->companion.active[slot] = s->traveler_bond;
    next->polish.active[slot] = next->polish.traveler;
    next->polish.active[slot].adventures |= (uint8_t)(1u << a);
    memset(&next->polish.traveler, 0, sizeof next->polish.traveler);
    next->story.active[slot] = next->story.traveler;
    memset(&next->story.traveler, 0, sizeof next->story.traveler);
    companion_memory(next, MEMORY_ADVENTURE, slot, a);
    next->fish[slot].wearable = s->traveler_bond.wearable;
    next->fish[slot].wear_colors = s->traveler_bond.wear_colors;
    next->companion.away = 0; next->companion.adventure_clock = 0;
    memset(&next->companion.traveler, 0, sizeof next->companion.traveler);
    memset(&next->companion.traveler_bond, 0, sizeof next->companion.traveler_bond);
    next->companion.adventure_done |= (uint8_t)(1u << a);
    if (s->adventure_choice) next->companion.adventure_routes |= (uint8_t)(1u << a);
    next->companion.discoveries |= 1u << (8 + a); next->companion.last_discovery = (uint8_t)(9 + a);
    if (next->companion.adventure_claimed & (1u << a)) {
        /* A different aquarium may already have claimed this shared story.
         * Return this real traveler, but never offer a second reward. */
        next->companion.adventure = 0; next->companion.adventure_s = 0;
    }
    next->roster_gen++;
    companion_polish_init(next, false);
    bool saved = progression_save(next);
    if (saved) *t = *next;
    else { s_age[slot] = age; s_starve_s[slot] = starve; }
    free(next); return saved;
}
const char *progression_bank_move(tank_t *t, int bank, int active) {
    if (bank < -1 || bank >= t->bank_count || active < -1 || active >= t->n_fish || (bank < 0 && active < 0))
        return "INVALID FISH SELECTION";
    if (s_setup_pending || s_newborn >= 0) return "FINISH NAMING YOUR FRY FIRST";
    if (bank < 0 && t->bank_count >= FISH_BANK_CAP) return "BANK FULL - SWAP A FISH INSTEAD";
    if (bank < 0 && t->n_fish <= FISH_KEEP_MIN) return "KEEP TWO IN TANK - USE SWAP";
    if (active < 0 && (occupied_slots(t) >= POP_CAP ||
        (progression_creative(t) && occupied_slots(t) >= CREATIVE_FISH_CAP)))
        return "TANK FULL / ADVENTURE SLOT RESERVED";
    /* A heap draft avoids a large stack snapshot and keeps failed writes entirely transactional. */
    tank_t *next = malloc(sizeof *next);
    if (!next) return "NOT ENOUGH MEMORY - TRY AGAIN";
    *next = *t;
    companion_polish_observe(next);
    double old_age[N_FISH_MAX]; float old_starve[N_FISH_MAX];
    memcpy(old_age, s_age, sizeof old_age); memcpy(old_starve, s_starve_s, sizeof old_starve);
    if (bank < 0) {
        next->companion.bank[next->bank_count] = next->companion.active[active];
        next->polish.bank[next->bank_count] = next->polish.active[active];
        next->story.bank[next->bank_count] = next->story.active[active];
        next->bank[next->bank_count++] = bank_capture(t, active);
        tank_remove_fish(next, active);
        for (int i = active; i < next->n_fish; i++) { s_age[i] = s_age[i + 1]; s_starve_s[i] = s_starve_s[i + 1]; }
        s_age[next->n_fish] = 0; s_starve_s[next->n_fish] = 0;
    } else {
        bank_fish_t incoming = next->bank[bank];
        companion_bond_t incoming_bond = next->companion.bank[bank];
        fish_life_t incoming_life = next->polish.bank[bank];
        fish_lineage_t incoming_line = next->story.bank[bank];
        if (active < 0) {
            active = next->n_fish++;
            memmove(&next->companion.bank[bank], &next->companion.bank[bank + 1],
                    (next->bank_count - bank - 1) * sizeof next->companion.bank[0]);
            memset(&next->companion.bank[next->bank_count - 1], 0, sizeof next->companion.bank[0]);
            memmove(&next->polish.bank[bank], &next->polish.bank[bank + 1],
                    (next->bank_count - bank - 1) * sizeof next->polish.bank[0]);
            memset(&next->polish.bank[next->bank_count - 1], 0, sizeof next->polish.bank[0]);
            memmove(&next->story.bank[bank], &next->story.bank[bank + 1],
                    (next->bank_count - bank - 1) * sizeof next->story.bank[0]);
            memset(&next->story.bank[next->bank_count - 1], 0, sizeof next->story.bank[0]);
            memmove(&next->bank[bank], &next->bank[bank + 1], (next->bank_count - bank - 1) * sizeof next->bank[0]);
            memset(&next->bank[--next->bank_count], 0, sizeof next->bank[0]);
        } else {
            next->companion.bank[bank] = next->companion.active[active];
            next->polish.bank[bank] = next->polish.active[active];
            next->story.bank[bank] = next->story.active[active];
            next->bank[bank] = bank_capture(t, active);
            for (int i = 0; i < next->n_fish; i++) {
                if (next->fish[i].parent_a == active) next->fish[i].parent_a = -1;
                if (next->fish[i].parent_b == active) next->fish[i].parent_b = -1;
            }
        }
        bank_restore(next, active, &incoming);
        next->companion.active[active] = incoming_bond;
        next->polish.active[active] = incoming_life;
        next->story.active[active] = incoming_line;
        next->fish[active].wearable = incoming_bond.wearable;
        next->fish[active].wear_colors = incoming_bond.wear_colors;
        next->roster_gen++;
    }
    next->courting = false; next->court_a = next->court_b = -1; next->court_active = 0;
    companion_polish_init(next, false);
    next->spawning = false; next->spawn_danced = 0; next->stage_fish = -1;
    bool arrival = s_arrival_pending; float spawn = s_spawn_in;
    s_arrival_pending = false; s_spawn_in = -1;
    bool saved = progression_save(next);
    if (saved) *t = *next;
    else {
        memcpy(s_age, old_age, sizeof s_age); memcpy(s_starve_s, old_starve, sizeof s_starve_s);
        s_arrival_pending = arrival; s_spawn_in = spawn;
    }
    free(next);
    return saved ? NULL : "SAVE FAILED - NO FISH MOVED";
}
static void set_tms(tank_t *t, uint32_t bit);   /* (below) */
static void fry_age(tank_t *t, float seconds);
bool progression_sell_fish(tank_t *t, int fish) {
    if (progression_creative(t)) return false;
    if (!progression_fish_sellable(t, fish)) return false;
    int worth = progression_fish_value(t, fish);
    if (t->sd_balance > INT_MAX - worth || t->sd_earned > INT_MAX - worth ||
        t->player_feedings > INT_MAX - SELL_FRY_MEALS) return false;
    tank_t *old = malloc(sizeof *old);
    if (!old) return false;
    *old = *t;
    double old_age[N_FISH_MAX]; float old_starve[N_FISH_MAX];
    memcpy(old_age, s_age, sizeof old_age); memcpy(old_starve, s_starve_s, sizeof old_starve);
    bool arrival = s_arrival_pending; float spawn = s_spawn_in;
    int32_t sale = s_sale_need; int pending = s_sd_pending;
    uint16_t seed = s_fry_seed; double fry_seconds = s_fry_age; uint8_t sold = s_fry_sold;
    if (!tank_remove_fish(t, fish)) { free(old); return false; }
    for (int i = fish; i < t->n_fish; i++) { s_age[i] = s_age[i + 1]; s_starve_s[i] = s_starve_s[i + 1]; }   /* the clocks move down with their fish */
    s_age[t->n_fish] = 0; s_starve_s[t->n_fish] = 0;
    /* the slot it leaves is earned again by care, not handed back: a fry
       already on its way is called off, and the next one waits for
       SELL_FRY_MEALS more meals (care_gates) - every other gate was met long
       ago in a grown tank, so without this the fry would be in the grass
       within the minute */
    s_arrival_pending = false; s_spawn_in = -1;
    s_sale_need = t->player_feedings + SELL_FRY_MEALS;
    set_tms(t, TMS_FIRST_SALE);                            /* the first sale opens the shop's fry (0.3.5) */
    fry_age(t, 0);                                         /* ... and draws the first three */
    sd_award(t, worth);
    bool ok = progression_save(t);
    if (!ok) {
        *t = *old;
        memcpy(s_age, old_age, sizeof s_age); memcpy(s_starve_s, old_starve, sizeof s_starve_s);
        s_arrival_pending = arrival; s_spawn_in = spawn; s_sale_need = sale; s_sd_pending = pending;
        s_fry_seed = seed; s_fry_age = fry_seconds; s_fry_sold = sold;
    } else tank_emit(TEV_CONFIRM, -1);
    free(old); return ok;
}
int progression_item_price_for(const tank_t *t, int item) {
    if (!t || item < 0 || item >= SD_ITEM_COUNT) return 0;
    if (item == SD_SHOP_MUSSEL) return progression_creative(t) ? 0 : SD_PRICE_MUSSEL;
    if (progression_creative(t) || (tank_decor_placeable(item) &&
        progression_inventory_stored(t, item))) return 0;
    if (tank_decor_placeable(item) && (t->profile.owned_items & (UINT64_C(1) << item)))
        return SD_ITEMS[item].price;
    return progression_item_unlocked(t, item) ? 0 : SD_ITEMS[item].price;
}
static bool buy_item(tank_t *t, int item) {
    if (item < 0 || item >= SD_ITEM_COUNT) return false;
    const sd_item_t *it = &SD_ITEMS[item];
    uint64_t item_bit = UINT64_C(1) << item;
    int price = progression_item_price_for(t, item);
    if (progression_item_owned(t, item) || t->sd_balance < price) return false;
    t->profile.owned_items |= item_bit;
    t->profile.paid_items = (t->profile.paid_items & ~item_bit) | (price ? item_bit : 0);
    s_shop_notice = NULL;
    if (item == SD_SHOP_MUSSEL) {
        t->sd_balance -= price;
        if (t->mussel.version < 2 && t->mussel.owned)
            t->mussel.owned = (uint8_t)(1u << t->aquariums.active);
        t->mussel.version = 2;
        t->mussel.owned |= (uint8_t)(1u << t->aquariums.active);
        return progression_save(t);
    }
    if (SD_IS_SCENERY(item)) {
        /* Observing may seed polish/RNG/familiarity; stage on a copy so a failed save changes nothing. */
        tank_t *next = malloc(sizeof *next);
        if (!next) { shop_save_failed("NOT ENOUGH MEMORY - NOT BOUGHT", it->name); return false; }
        *next = *t;
        companion_polish_observe(next);     /* first: migrating an uninitialised polish clears it */
        next->sd_balance -= price;
        next->polish.scenery_unlocks = (uint8_t)(next->polish.scenery_unlocks | (1u << (item - SD_SCENERY_FIRST)));
        size_t since = 32 + (size_t)(item - SD_SCENERY_FIRST);
        if (since < sizeof next->polish.decor_since / sizeof next->polish.decor_since[0]) {
            uint32_t powered = next->companion.powered_s;
            next->polish.decor_since[since] = powered == UINT32_MAX ? UINT32_MAX : powered + 1;
        }
        bool saved = progression_save(next);
        if (saved) *t = *next;
        free(next);
        if (!saved) shop_save_failed("SAVE FAILED - NOT BOUGHT", it->name);
        return saved;
    }
    if (item >= 32) {
        uint8_t flag = (uint8_t)(1u << (item - 32));
        t->sd_balance -= price; t->aquariums.stat_upgrades |= flag;
        if (progression_save(t)) return true;
        t->sd_balance += price; t->aquariums.stat_upgrades &= (uint8_t)~flag;
        shop_save_failed("SAVE FAILED - NOT BOUGHT", it->name);
        return false;
    }
    if (it->bit == SD_ITEM_PUMPKIN && !progression_pumpkin_offered(t)) return false;   /* out of season */
    t->sd_balance -= price; t->sd_unlocks |= it->bit;
    if (item >= 22) {
        if (progression_save(t)) return true;
        t->sd_balance += price; t->sd_unlocks &= ~it->bit;
        shop_save_failed("SAVE FAILED - NOT BOUGHT", it->name);
        return false;
    }
    if (it->bit == SD_ITEM_PLANT) tank_plant_place(t);
    if (it->bit == SD_ITEM_SNAIL) tank_snail_place(t);
    if (it->bit == SD_ITEM_CASTLE) tank_castle_place(t);
    if (it->bit == SD_ITEM_CORAL) tank_coral_place(t);
    if (it->bit == SD_ITEM_CLUSTER) tank_cluster_place(t);
    if (it->bit == SD_ITEM_SHRIMP) { tank_shrimp_place(t, SHRIMP_START); t->shrimp_food = 0; t->shrimp_cool = 0; }
    if (it->bit == SD_ITEM_URCHIN) tank_urchin_place(t);
    if (it->bit == SD_ITEM_SEAHORSE) { tank_seahorse_place(t); t->sh_s = 0; t->sh_hitches = 0; t->sh_trust_passed = 0; }
    if (it->bit == SD_ITEM_PUMPKIN) tank_pumpkin_place(t);
    if (item >= 9 && tank_decor_placeable(item)) tank_decor_reset(t, item);
    if (it->bit == SD_ITEM_SAND) t->sand_style = 1;
    if (it->bit == SD_ITEM_LIGHTS) t->water_style = 1;
    if (tank_decor_placeable(item)) tank_decor_raise(t, item);   /* the piece placed last goes on top (2026-10-17) */
    return progression_save(t);                            /* a purchase sticks at once */
}
bool progression_buy(tank_t *t, int item) {
    s_shop_notice = NULL;
    if (!t || item < 0 || item >= SD_ITEM_COUNT) {
        s_shop_notice = "INVALID ITEM"; return false;
    }
    if (t && tank_decor_placeable(item) && !progression_item_owned(t, item) &&
        progression_inventory_stored(t, item)) return progression_inventory_place(t, item);
    if (progression_item_owned(t, item)) { s_shop_notice = "ITEM ALREADY PLACED"; return false; }
    if (t->sd_balance < progression_item_price_for(t, item)) {
        s_shop_notice = "NOT ENOUGH SAND DOLLARS"; return false;
    }
    tank_t *next = malloc(sizeof *next);
    if (!next) { shop_save_failed("NOT ENOUGH MEMORY - NOT BOUGHT", SD_ITEMS[item].name); return false; }
    *next = *t;
    bool ok = buy_item(next, item);
    if (ok) *t = *next;
    else if (!s_shop_notice) shop_save_failed("SAVE FAILED - NOT BOUGHT", SD_ITEMS[item].name);
    free(next);
    return ok;
}
bool progression_inventory_duplicate(int item) {
    return item == 9 || tank_ornament_index(item) >= 0;
}
static int inventory_slot(int item) {
    return tank_decor_placeable(item) ? tank_decor_slot(item) : -1;
}
int progression_inventory_stored(const tank_t *t, int item) {
    int slot = inventory_slot(item);
    return t && slot >= 0 ? t->inventory.stored[slot] : 0;
}
int progression_inventory_placed(const tank_t *t, int item) {
    if (!t || inventory_slot(item) < 0) return 0;
    int n = progression_item_owned(t, item) ? 1 : 0;
    for (int i = 0; i < DECOR_EXTRA_CAP; i++) n += t->inventory.extra[i].item == item + 1;
    return n;
}
static bool inventory_valid(const inventory_state_t *v) {
    if (v->version > 1 || v->reserved[0] || v->reserved[1] || v->reserved[2]) return false;
    unsigned total = 0;
    for (int s = 0; s < DECOR_SLOTS; s++) {
        if (v->paid[s] > v->stored[s]) return false;
        total += v->stored[s];
    }
    if (total > DECOR_INVENTORY_CAP) return false;
    for (int i = 0; i < DECOR_EXTRA_CAP; i++) {
        const inventory_extra_t *e = &v->extra[i];
        if (!e->item) {
            if (e->paid || e->rank || e->z || e->x != 0) return false;
        } else if (!progression_inventory_duplicate(e->item - 1) ||
                   (e->z != DECOR_Z_BACK && e->z != DECOR_Z_FRONT) ||
                   e->paid > 1 || !isfinite(e->x) || e->x <= 0 || e->x > TANK_MAX_W) return false;
        if (!v->version && e->item) return false;
    }
    return v->version || !total;
}
static bool inventory_restore_safe(const save_t *to, const save_t *current) {
    if (to->creative.enabled && current->creative.enabled &&
        to->profile.mode == GAME_MODE_CREATIVE && current->profile.mode == GAME_MODE_CREATIVE) return true;
    if (current->aquariums.count < 2) return true;
    /* Portable snapshots have no per-copy identity. An old snapshot must not
     * reclaim an ornament that has since been transferred to another tank. */
    if (memcmp(to->inventory.stored, current->inventory.stored, DECOR_SLOTS) ||
        memcmp(to->inventory.paid, current->inventory.paid, DECOR_SLOTS)) return false;
    for (int s = 0; s < DECOR_SLOTS; s++) {
        int item = tank_decor_slot_item(s), count[2] = {0}, paid[2] = {0};
        const save_t *v[2] = {to, current};
        for (int j = 0; j < 2; j++) {
            uint64_t bit = UINT64_C(1) << item;
            count[j] = !!(v[j]->sd_unlocks & SD_ITEMS[item].bit);
            paid[j] = count[j] && (v[j]->profile.paid_items & bit);
            for (int i = 0; i < DECOR_EXTRA_CAP; i++) {
                const inventory_extra_t *e = &v[j]->inventory.extra[i];
                if (e->item == item + 1) { count[j]++; paid[j] += e->paid; }
            }
        }
        if (count[0] != count[1] || paid[0] != paid[1]) return false;
    }
    return true;
}
static bool inventory_fail(const char *notice) { s_shop_notice = notice; return false; }
static tank_t *inventory_begin(tank_t *t, int item) {
    s_shop_notice = NULL;
    if (!t || inventory_slot(item) < 0) {
        inventory_fail("THIS ITEM CANNOT BE STORED"); return NULL;
    }
    if (!inventory_valid(&t->inventory)) {
        inventory_fail("INVALID DECORATION INVENTORY"); return NULL;
    }
    tank_t *next = malloc(sizeof *next);
    if (!next) { inventory_fail("NOT ENOUGH MEMORY - NO CHANGE"); return NULL; }
    *next = *t; next->inventory.version = 1;
    return next;
}
static bool inventory_commit(tank_t *t, tank_t *next) {
    bool ok = progression_save(next);
    if (ok) *t = *next;
    else inventory_fail("SAVE FAILED - NO CHANGE");
    free(next); return ok;
}
static bool inventory_room(const tank_t *t) {
    unsigned n = 0;
    for (int s = 0; s < DECOR_SLOTS; s++) n += t->inventory.stored[s];
    return n < DECOR_INVENTORY_CAP;
}
bool progression_inventory_buy(tank_t *t, int item) {
    tank_t *next = inventory_begin(t, item);
    if (!next) return false;
    if (!(t->profile.owned_items & (UINT64_C(1) << item)) &&
        !progression_inventory_placed(t, item) && !progression_inventory_stored(t, item)) {
        free(next); return inventory_fail("BUY YOUR FIRST ONE IN THE SHOP");
    }
    int price = progression_creative(t) ? 0 : SD_ITEMS[item].price;
    if (!inventory_room(t) || t->sd_balance < price || (item == 8 && !progression_pumpkin_offered(t))) {
        free(next);
        return inventory_fail(!inventory_room(t) ? "DECORATION STORAGE FULL" :
                              t->sd_balance < price ? "NOT ENOUGH SAND DOLLARS" : "ITEM NOT AVAILABLE");
    }
    int slot = inventory_slot(item);
    next->inventory.stored[slot]++;
    next->inventory.paid[slot] += price != 0;
    next->sd_balance -= price;
    next->profile.owned_items |= UINT64_C(1) << item;
    return inventory_commit(t, next);
}
bool progression_inventory_store(tank_t *t, int item, int copy) {
    tank_t *next = inventory_begin(t, item);
    if (!next) return false;
    if (!inventory_room(t)) { free(next); return inventory_fail("DECORATION STORAGE FULL"); }
    bool paid;
    uint64_t bit = UINT64_C(1) << item;
    if (copy == -1 && progression_item_owned(t, item)) {
        paid = (t->profile.paid_items & bit) != 0;
        next->sd_unlocks &= ~SD_ITEMS[item].bit;
        next->profile.paid_items &= ~bit;
        /* Keep the primary geometry, colours and growth for a later placement. */
    } else if (copy >= 0 && copy < DECOR_EXTRA_CAP && next->inventory.extra[copy].item == item + 1) {
        paid = next->inventory.extra[copy].paid != 0;
        memset(&next->inventory.extra[copy], 0, sizeof next->inventory.extra[copy]);
    } else { free(next); return inventory_fail("PLACED COPY NOT FOUND"); }
    int slot = inventory_slot(item);
    next->inventory.stored[slot]++;
    next->inventory.paid[slot] += paid;
    return inventory_commit(t, next);
}
bool progression_inventory_place(tank_t *t, int item) {
    tank_t *next = inventory_begin(t, item);
    if (!next) return false;
    int slot = inventory_slot(item);
    if (!next->inventory.stored[slot]) { free(next); return inventory_fail("NO STORED COPY"); }
    bool paid = next->inventory.paid[slot] == next->inventory.stored[slot];
    if (!progression_item_owned(t, item)) {
        next->sd_unlocks |= SD_ITEMS[item].bit;
        uint64_t bit = UINT64_C(1) << item;
        next->profile.owned_items |= bit;
        next->profile.paid_items = (next->profile.paid_items & ~bit) | (paid ? bit : 0);
        if (item == 0 && next->veg_growth[3] <= 0) tank_plant_place(next);
        tank_decor_raise(next, item);
    } else {
        if (!progression_inventory_duplicate(item)) {
            free(next); return inventory_fail("ONE LIVING/LARGE PIECE PER TYPE");
        }
        int i = 0;
        while (i < DECOR_EXTRA_CAP && next->inventory.extra[i].item) i++;
        if (i == DECOR_EXTRA_CAP) { free(next); return inventory_fail("EXTRA DECORATION SLOTS FULL"); }
        next->inventory.extra[i] = (inventory_extra_t){
            .item = (uint8_t)(item + 1), .z = DECOR_Z_BACK, .paid = paid,
            .rank = (uint8_t)(DECOR_SLOTS + i), .x = TANK_W * 0.5f
        };
    }
    next->inventory.stored[slot]--;
    next->inventory.paid[slot] -= paid;
    return inventory_commit(t, next);
}
bool progression_inventory_sell(tank_t *t, int item) {
    tank_t *next = inventory_begin(t, item);
    if (!next) return false;
    int slot = inventory_slot(item);
    if (!next->inventory.stored[slot]) { free(next); return inventory_fail("NO STORED COPY"); }
    bool paid = next->inventory.paid[slot] == next->inventory.stored[slot];
    int value = paid && !progression_creative(t) ? progression_sell_value(item) : 0;
    if (t->sd_balance > INT_MAX - value) { free(next); return inventory_fail("WALLET FULL - NOT SOLD"); }
    next->inventory.stored[slot]--; next->inventory.paid[slot] -= paid;
    next->sd_balance += value;
    /* Resale is not an earned reward and cannot create milestone dollars. */
    return inventory_commit(t, next);
}
const char *const *progression_sd_earn_lines(void) {
    static char lines[SD_EARN_LINES][30]; static const char *ptr[SD_EARN_LINES + 1]; static bool made;
    if (!made) {
        /* <= 25 chars each: the modal is 336 px wide at scale 2 */
        snprintf(lines[0], 30, "GOALS > TODAY: +125/DAY");
        snprintf(lines[1], 30, "+%d/%d/%d  A FISH GROWS UP", SD_STAGE_JUV, SD_STAGE_ADULT, SD_STAGE_ELDER);
        snprintf(lines[2], 30, "+%d  A NEW FRY IS BORN", SD_BIRTH);
        snprintf(lines[3], 30, "+%d  A FISH FULLY TRUSTS", SD_TRUST);
        snprintf(lines[4], 30, "MILESTONE GOALS PAY ONCE");
        snprintf(lines[5], 30, "GAMES: +1, MAX 5 EACH/DAY");
        snprintf(lines[6], 30, "NO STREAKS OR DAILY DEBT");
        for (int i = 0; i < SD_EARN_LINES; i++) ptr[i] = lines[i];
        ptr[SD_EARN_LINES] = NULL; made = true;
    }
    return ptr;
}
static void set_tms(tank_t *t, uint32_t bit) { if (!(t->tank_ms_bits & bit)) { t->tank_ms_bits |= bit; mark_dirty(); } }

/* the stage the age has earned. Only the director's set_age (`down`) takes
 * one away: a fish grown up under shorter stage times (0.3.4 and before)
 * keeps its stage on the longer ones and grows on from there. */
static void apply_stage(fish_t *f, double age, bool down) {
    stage_t st = age >= STAGE_ELDER_AGE ? STAGE_ELDER : age >= STAGE_ADULT_AGE ? STAGE_ADULT
               : age >= STAGE_JUV_AGE ? STAGE_JUV : STAGE_FRY;
    if (st < f->stage && !down) st = f->stage;
    if (st != f->stage) { f->stage = st; mark_dirty(); }         /* silent surprise */
    if (st >= STAGE_JUV)   set_ms(f, MS_REACHED_JUV);
    if (st >= STAGE_ADULT) set_ms(f, MS_REACHED_ADULT);
    if (st >= STAGE_ELDER) set_ms(f, MS_REACHED_ELDER);
}

/* size: base by stage, plus a meal-fed bonus; never shrinks below stage base */
static void apply_growth(fish_t *f) {
    static const float stage_scale[4] = { 0.55f, 0.78f, 1.04f, 1.23f };  /* elder +14% (2026-08-30) */
    float fed = 1.0f + clampf(f->eaten / 60.0f, 0, 1) * 0.18f;
    f->size = f->base_size * stage_scale[f->stage] * fed;
}

static const uint32_t POP_TMS[N_FISH_MAX + 1] = { 0, 0, TMS_PAIR, TMS_TRIO, TMS_QUARTET, TMS_QUINTET, TMS_SEXTET };

/* the parents: the two most trusting grown fish - or, with fewer than two
 * grown (at two fish no stage is gated), the most trusting of the rest fill
 * in, so there is always a pair to court (2026-09-24: the spawning needs
 * two fish in the grass; tank_add_fish already fell back to fish 0 and 1) */
static float parent_rank(const fish_t *f) { return (f->stage >= STAGE_ADULT ? 100.0f : 0.0f) + f->trust; }
static void pick_parents(const tank_t *t, int *pa, int *pb) {
    int a = -1, b = -1;
    for (int i = 0; i < t->n_fish; i++) {
        float r = parent_rank(&t->fish[i]);
        if (a < 0 || r > parent_rank(&t->fish[a])) { b = a; a = i; }
        else if (b < 0 || r > parent_rank(&t->fish[b])) b = i;
    }
    *pa = a; *pb = b;
}

/* the arrival itself: a fry in the nursery grass, traits inherited from the
 * parents; the stage clock starts from zero */
static void do_arrival(tank_t *t) {
    if (progression_creative(t)) return;
    int a, b;
    pick_parents(t, &a, &b);
    int slot = tank_add_fish(t, a, b);
    s_arrival_pending = false;
    s_spawn_in = -1; t->spawning = false; t->spawn_danced = 0;
    if (slot < 0) return;
    int nb = tank_nursery_bed(t);            /* born in the grass it was courted in */
    if (nb >= 0) {
        float x0, x1; tank_veg_bed(t, nb, &x0, &x1, NULL, NULL);
        t->fish[slot].x = (x0 + x1) * 0.5f; t->fish[slot].y = TANK_BOT - 16 - 18;
    }
    s_age[slot] = 0;
    t->fish[slot].ms_bits = MS_ARRIVED;
    companion_polish_observe(t);
    t->sd_paid_fish[slot] = 0;               /* a new ledger for the new fish */
    if (t->n_fish <= N_FISH_MAX) set_tms(t, POP_TMS[t->n_fish]);
    s_newborn = slot;                        /* owed its welcome: the birth flow (setup.c) */
    sd_award(t, SD_BIRTH);
    mark_dirty();
}

/* ---- fry for sale (0.3.5) ---- */
/* 0.4's nine colors (main: LOOK_COLORS, in its order), so a bought fry's
 * look snaps to a genome there exactly. The body comes from the first
 * eight - charcoal is a marking only on 0.3.x, whose fin rule would draw a
 * charcoal fish's fins black. */
static const uint32_t FRY_COLORS[9] = { 0x38dcc7, 0xff725c, 0x78d67d, 0xa799ff, 0xffd166, 0xf48fb1, 0x4da3ff, 0xe8f1f2, 0x23262c };
#define FRY_BODY_N 8
static uint32_t fry_hash(uint32_t x) { x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15; x *= 0x846ca68bu; x ^= x >> 16; return x; }
bool progression_fry_for_sale(const tank_t *t) {
    return !progression_creative(t) &&
        ((t->tank_ms_bits & TMS_FIRST_SALE) != 0 || (progression_hard(t) && t->n_fish < 2));
}
bool progression_fry_room(const tank_t *t) { return occupied_slots(t) < POP_CAP && occupied_slots(t) < N_FISH_MAX && s_newborn < 0; }
int  progression_fry_left(void) { int n = 0; for (int k = 0; k < FRY_OFFER_N; k++) n += !(s_fry_sold >> k & 1); return n; }
float progression_fry_new_in_s(void) { double s = FRY_OFFER_S - s_fry_age; return s > 0 ? (float)s : 0; }
void progression_fry_offer(const tank_t *t, int k, fry_offer_t *o) {
    /* three different bodies: the seed shuffles the eight and the first three are the offer */
    uint32_t h = fry_hash(s_fry_seed * 2654435761u + 0x5eed);
    int order[FRY_BODY_N];
    for (int i = 0; i < FRY_BODY_N; i++) order[i] = i;
    for (int i = FRY_BODY_N - 1; i > 0; i--) { h = fry_hash(h + (uint32_t)i); int j = (int)(h % (uint32_t)(i + 1)), x = order[i]; order[i] = order[j]; order[j] = x; }
    if (k < 0 || k >= FRY_OFFER_N) k = 0;
    uint32_t body = FRY_COLORS[order[k]];
    int a = (int)(fry_hash(h ^ (0xacce5u * (uint32_t)(k + 1))) % 9u);
    if (FRY_COLORS[a] == body) a = (a + 1) % 9;              /* markings the body's own colour would vanish */
    o->body = body; o->accent = FRY_COLORS[a]; o->sold = (s_fry_sold >> k & 1) != 0;
    uint8_t varieties[FISH_VARIETY_COUNT]; int count = 0;
    for (int i = 0; i < FISH_VARIETY_COUNT; i++)
        if (journal_variety_unlocked(t, i)) varieties[count++] = (uint8_t)i;
    o->variety = varieties[(s_fry_seed + k) % count];
    o->pattern = (uint8_t)((s_fry_seed / FISH_VARIETY_COUNT + k) % FISH_PATTERN_COUNT);
}
void progression_fry_roll(tank_t *t) {
    s_fry_seed = (uint16_t)(1 + (int)tank_randf(t, 0, 65534.99f));   /* never 0: 0 = none drawn */
    s_fry_age = 0; s_fry_sold = 0;
    mark_dirty();
}
/* the offer's clock: tank time, awake or asleep, while the shop sells fry */
static void fry_age(tank_t *t, float seconds) {
    if (!progression_fry_for_sale(t)) return;
    if (!s_fry_seed) { progression_fry_roll(t); return; }    /* the first sale: the first three */
    s_fry_age += seconds;
    if (s_fry_age >= FRY_OFFER_S) progression_fry_roll(t);    /* a week away is one fresh three */
}
void progression_fry_open(tank_t *t) { set_tms(t, TMS_FIRST_SALE); fry_age(t, 0); }
bool progression_buy_fry(tank_t *t, int k) {
    if (progression_creative(t)) return false;
    if (!progression_fry_for_sale(t) || !s_fry_seed || k < 0 || k >= FRY_OFFER_N || (s_fry_sold >> k & 1) ||
        !progression_fry_room(t) || t->sd_balance < SD_PRICE_FRY) return false;
    fry_offer_t o; progression_fry_offer(t, k, &o);
    int slot = tank_add_bought_fry(t, o.body, o.accent);
    if (slot < 0) return false;
    t->fish[slot].variety = o.variety; t->fish[slot].pattern = o.pattern;
    t->sd_balance -= SD_PRICE_FRY;
    s_fry_sold |= (uint8_t)(1u << k);
    s_age[slot] = 0; s_starve_s[slot] = 0;
    t->fish[slot].ms_bits = MS_ARRIVED;
    companion_polish_observe(t);
    t->sd_paid_fish[slot] = 0;                     /* its stages pay; no birth dollars - it was bought */
    apply_growth(&t->fish[slot]);
    if (t->n_fish <= N_FISH_MAX) set_tms(t, POP_TMS[t->n_fish]);
    if (occupied_slots(t) >= POP_CAP) { s_arrival_pending = false; s_spawn_in = -1; t->spawning = false; t->spawn_danced = 0; }   /* the last slot: a staged fry is called off */
    s_newborn = slot;                              /* owed its welcome: the birth flow (setup.c) */
    progression_save(t);
    return true;
}

/* Care gates require earned attention, never time alone.
 * Counted, not just checked, so the tank can TELL when it's close: one
 * condition shy of an arrival, the parents-to-be start courting - and the
 * milestones page can LIST them (progression_next_fry, 2026-09-14). One
 * table serves both: `have` / `need` are the numbers behind the words. */
typedef struct { int kind; float have, need, frac; bool met; } gate_t;
#define CARE_GATES_MAX 4                 /* the population's three + the glass */
static int care_gates(const tank_t *t, gate_t g[CARE_GATES_MAX]) {
    float min_trust = 10;
    for (int i = 0; i < t->n_fish; i++)
        if (t->fish[i].trust < min_trust) min_trust = t->fish[i].trust;
    int last = t->n_fish - 1;
    float age = last >= 0 ? (float)s_age[last] : 0;
    int n = 0;
    /* the MEALS gate after a sale: the table's count, or the meals owed since
       the fish was sold, whichever is further off */
#define MEALS(tbl) ((float)((tbl) > s_sale_need ? (tbl) : s_sale_need))
#define GATE(k, h, nd, m) do { g[n].kind = (k); g[n].have = (h); g[n].need = (nd); g[n].met = (m); \
        g[n].frac = g[n].met ? 1.0f : (nd) > 0 ? (h) / (nd) : 0; if (g[n].frac > 1) g[n].frac = 1; n++; } while (0)
    switch (t->n_fish) {
    case 2:
        GATE(FRY_REQ_TRUST, min_trust, 6.0f, min_trust >= 6.0f);
        GATE(FRY_REQ_FEED, (float)t->player_feedings, MEALS(12), t->player_feedings >= MEALS(12));
        GATE(FRY_REQ_HOLD, (float)t->hold_approaches, 1, t->hold_approaches >= 1);
        break;
    case 3:
        /* 2026-09-15 (Strato): MEALS and CHANGE were one "40 meals OR someone
         * drifted 0.11" row - and the parents had always drifted long before,
         * so it was dead weight. Now three rows, and CHANGE is the YOUNGEST
         * fish's accumulated drift pressure (tank.h drift_acc): the same
         * ~26 lit minutes fed and calm, and impossible to soft-lock for a fry
         * born on a trait clamp (inheritance puts them there). */
        GATE(FRY_REQ_GROW, age, (float)STAGE_JUV_AGE, t->fish[last].stage >= STAGE_JUV);
        GATE(FRY_REQ_FEED, (float)t->player_feedings, MEALS(40), t->player_feedings >= MEALS(40));
        GATE(FRY_REQ_CHANGE, t->fish[last].drift_acc, DRIFT_CHANGE, t->fish[last].drift_acc >= DRIFT_CHANGE);
        break;
    case 4:
        GATE(FRY_REQ_GROW, age, (float)STAGE_ADULT_AGE, t->fish[last].stage >= STAGE_ADULT);
        GATE(FRY_REQ_FEED, (float)t->player_feedings, MEALS(80), t->player_feedings >= MEALS(80));
        GATE(FRY_REQ_TRUST, min_trust, 7.0f, min_trust >= 7.0f);
        break;
    default:
        GATE(FRY_REQ_GROW, age, (float)STAGE_ADULT_AGE, t->fish[last].stage >= STAGE_ADULT);
        GATE(FRY_REQ_FEED, (float)t->player_feedings, MEALS(140), t->player_feedings >= MEALS(140));
        GATE(FRY_REQ_TRUST, min_trust, 8.0f, min_trust >= 8.0f);
        break;
    }
    /* and a clean tank (Strato, 2026-09-16: "fish should not be able to
     * breed in a dirty tank. Some algae is OK but if a certain percentage of
     * glass crosses a threshold it will prevent new fries from spawning
     * unless cleaned"): film on no more than ALGAE_DIRTY of the glass. A
     * care gate like the others, so the parents court while only the glass
     * holds them back, and the checklist lists it. The bar reads the other
     * way from the rest - empty at the growth cap, full at the threshold -
     * so it fills as the keeper wipes. Note the staged fry is NOT held by
     * the glass at the light-on that brings it: a night's sleep films ~25%
     * of the glass, so a fry conceived clean would otherwise never land on
     * a morning (do_arrival waits for the nursery only). */
    {
        float cover = tank_algae_cover(t);
        GATE(FRY_REQ_GLASS, cover, ALGAE_DIRTY, cover <= ALGAE_DIRTY);
        if (!g[n - 1].met) {
            float frac = 1.0f - (cover - ALGAE_DIRTY) / ALGAE_DIRTY;
            g[n - 1].frac = frac < 0 ? 0 : frac > 1 ? 1 : frac;
        }
    }
#undef GATE
#undef MEALS
    return n;
}
static void arrival_conditions(const tank_t *t, int *met, int *total) {
    gate_t g[CARE_GATES_MAX];
    *total = care_gates(t, g); *met = 0;
    for (int i = 0; i < *total; i++) *met += g[i].met;
}

/* how each gate is moved - the words behind the HOW? button */
static const char *const TIP_TRUST[]  = { "REST A FINGER ON THE GLASS", "AND KEEP IT STILL. EVERY", "FISH EARNS TRUST WHILE IT", "RESTS THERE. 3 QUICK TAPS", "SCARE THEM AND COST TRUST.", NULL };
static const char *const TIP_FEED[]   = { "TAP THE WATER AT THE TOP", "OF THE TANK TO DROP FOOD.", "A FEEDING COUNTS AS A MEAL", "ONCE A FISH EATS FROM IT.", NULL };
static const char *const TIP_HOLD[]   = { "REST A FINGER ON THE GLASS", "FOR A FEW SECONDS. A FISH", "THAT TRUSTS YOU SWIMS OVER", "AND STAYS. FEED FIRST: A", "HUNGRY FISH WON'T COME.", NULL };
static const char *const TIP_GROW[]   = { "FISH GROW WITH TIME,", "SLOWER WHEN THE TANK IS", "IN SLEEP MODE.", NULL };
static const char *const TIP_CHANGE[] = { "FISH PERSONALITIES WILL", "NATURALLY DRIFT AS THEY", "INTERACT WITH THE WORLD.", NULL };   /* Strato: intentionally vague */
static const char *const TIP_GRASS[]  = { "GRASS REGROWS ON ITS OWN,", "FASTEST WHILE THE TANK", "SLEEPS.", NULL };
static const char *const TIP_GLASS[]  = { "DRAG A FINGER ACROSS THE", "GLASS TO WIPE IT CLEAN.", "UNLOCKABLE CRITTERS CAN", "HELP KEEP IT CLEAN.", NULL };   /* Strato's words */
const char *const *progression_fry_tip(int kind) {
    switch (kind) {
    case FRY_REQ_TRUST:  return TIP_TRUST;
    case FRY_REQ_FEED:   return TIP_FEED;
    case FRY_REQ_HOLD:   return TIP_HOLD;
    case FRY_REQ_GROW:   return TIP_GROW;
    case FRY_REQ_CHANGE: return TIP_CHANGE;
    case FRY_REQ_GLASS:  return TIP_GLASS;
    default:             return TIP_GRASS;
    }
}

/* the checklist, in words. No %f: the device's printf may be the nano one. */
static const char *const STAGE_WORDS[4] = { "A FRY", "A JUVENILE", "AN ADULT", "AN ELDER" };
int progression_next_fry(const tank_t *t, fry_req_t out[FRY_REQ_MAX], bool *staged) {
    if (progression_creative(t)) { if (staged) *staged = false; return 0; }
    if (staged) *staged = s_arrival_pending;
    if (occupied_slots(t) >= POP_CAP || occupied_slots(t) >= N_FISH_MAX || t->n_fish < 2) return 0;
    gate_t g[CARE_GATES_MAX];
    int n = care_gates(t, g);
    for (int i = 0; i < n; i++) {
        fry_req_t *r = &out[i];
        memset(r, 0, sizeof *r);
        r->kind = g[i].kind; r->frac = g[i].frac; r->met = g[i].met;
        int have = (int)g[i].have, need = (int)g[i].need;
        switch (g[i].kind) {
        case FRY_REQ_TRUST:
            snprintf(r->title, sizeof r->title, "TRUST");
            snprintf(r->words, sizeof r->words, "ALL FISH MUST HAVE TRUST");
            snprintf(r->words2, sizeof r->words2, "OF AT LEAST %d OUT OF 10", need);
            if (r->met) snprintf(r->progress, sizeof r->progress, "EVERY FISH DOES");
            else { int tenths = (int)(g[i].have * 10 + 0.5f);
                   snprintf(r->progress, sizeof r->progress, "LOWEST NOW %d.%d", tenths / 10, tenths % 10); }
            break;
        case FRY_REQ_FEED:
            snprintf(r->title, sizeof r->title, "MEALS");
            snprintf(r->words, sizeof r->words, "FEED THE FISH AT LEAST");
            snprintf(r->words2, sizeof r->words2, "%d TIMES IN ALL", need);
            if (r->met) snprintf(r->progress, sizeof r->progress, "DONE");
            else snprintf(r->progress, sizeof r->progress, "%d OF %d SO FAR", have, need);
            break;
        case FRY_REQ_HOLD:
            snprintf(r->title, sizeof r->title, "HOLD");
            snprintf(r->words, sizeof r->words, "REST A FINGER ON THE GLASS");
            snprintf(r->words2, sizeof r->words2, "UNTIL A FISH SWIMS TO IT");
            snprintf(r->progress, sizeof r->progress, r->met ? "DONE" : "NOT YET");
            break;
        case FRY_REQ_GROW: {
            const fish_t *f = &t->fish[t->n_fish - 1];
            snprintf(r->title, sizeof r->title, "GROW");
            snprintf(r->words, sizeof r->words, "THE YOUNGEST FISH MUST");
            snprintf(r->words2, sizeof r->words2, need >= STAGE_ADULT_AGE ? "GROW INTO AN ADULT" : "GROW INTO A JUVENILE");
            snprintf(r->progress, sizeof r->progress, "%s IS %s", f->name, STAGE_WORDS[f->stage & 3]);
            break; }
        case FRY_REQ_CHANGE: {
            const fish_t *f = &t->fish[t->n_fish - 1];
            snprintf(r->title, sizeof r->title, "CHANGE");
            snprintf(r->words, sizeof r->words, "%s'S PERSONALITY", f->name);
            snprintf(r->words2, sizeof r->words2, "MUST START TO SHIFT");
            if (r->met) snprintf(r->progress, sizeof r->progress, "DONE");
            else snprintf(r->progress, sizeof r->progress, "%d%% THERE", (int)(r->frac * 100 + 0.5f));
            break; }
        case FRY_REQ_GLASS: {
            int pct = (int)(g[i].have * 100 + 0.5f), limit = (int)(ALGAE_DIRTY * 100 + 0.5f);
            snprintf(r->title, sizeof r->title, "GLASS");
            snprintf(r->words, sizeof r->words, "NO MORE THAN %d%%", limit);          /* Strato's words, 2026-09-16 */
            snprintf(r->words2, sizeof r->words2, "ALGAE COVERAGE");
            if (r->met) snprintf(r->progress, sizeof r->progress, "CURRENTLY CLEAN ENOUGH");
            else snprintf(r->progress, sizeof r->progress, "%d%% COVERED NOW", pct);
            break; }
        }
    }
    /* and the nursery: every arrival needs grass to be born in */
    {
        fry_req_t *r = &out[n++];
        memset(r, 0, sizeof *r);
        float best = 0;
        for (int b = 0; b < VEG_BEDS; b++) if (t->veg_growth[b] > best) best = t->veg_growth[b];
        r->kind = FRY_REQ_GRASS; r->met = tank_nursery_bed(t) >= 0;
        r->frac = r->met ? 1.0f : best / VEG_NURSERY;
        snprintf(r->title, sizeof r->title, "GRASS");
        snprintf(r->words, sizeof r->words, "ONE GRASS BED MUST GROW");
        snprintf(r->words2, sizeof r->words2, "TALL ENOUGH TO HIDE IN");
        if (r->met) snprintf(r->progress, sizeof r->progress, "A NURSERY BED IS READY");
        else snprintf(r->progress, sizeof r->progress, "TALLEST BED %d%% THERE", (int)(r->frac * 100 + 0.5f));
    }
    return n;
}

static bool arrival_earned(const tank_t *t) {
    if (progression_creative(t)) return false;
    if (occupied_slots(t) >= POP_CAP || occupied_slots(t) >= N_FISH_MAX) return false;
    if (tank_nursery_bed(t) < 0) return false;   /* no grass to be born in */
    int met, total;
    arrival_conditions(t, &met, &total);
    return met == total;
}

void progression_force_arrival(tank_t *t) { if (!progression_creative(t)) { s_arrival_pending = true; do_arrival(t); } }
void progression_woke(tank_t *t) { if (!progression_creative(t) && s_arrival_pending) do_arrival(t); }
void progression_stage_arrival(tank_t *t) { (void)t; if (!s_arrival_pending) { s_arrival_pending = true; mark_dirty(); } }

void progression_fresh(tank_t *t) {
    memset(&t->game_income, 0, sizeof t->game_income); t->game_income.version = 1;
    memset(&t->mussel, 0, sizeof t->mussel); t->mussel_fraction = 0; t->mussel_glint = 0;
    s_hard_save_failed = s_hard_departed = false;
    if (!t->profile.marker) {
        t->profile.marker = PROFILE_MARKER;
        t->profile.version = 1;
    }
    if (!t->aquariums.name[0])
        snprintf(t->aquariums.name, sizeof t->aquariums.name, "TANK %u", t->aquariums.active + 1);
    tank_reframe(t, true);
    t->companion.version = COMPANION_VERSION;
    t->companion.display_compact = 1;
    tank_new_population(t);          /* a new tank: two FRY, contrasting -
                                      * the keeper watches them grow up */
    for (int i = 0; i < N_FISH_MAX; i++) s_age[i] = 0;
    t->tank_ms_bits = TMS_PAIR;
    for (int i = 0; i < t->n_fish; i++) { t->fish[i].ms_bits = MS_ARRIVED; apply_growth(&t->fish[i]); }
    memset(&t->polish, 0, sizeof t->polish);
    companion_polish_init(t, true);
    s_arrival_pending = false; s_spawn_in = -1; s_prev_night = t->night;
    s_ravenous = false; s_ravenous_t = 0; s_gave_up = false;
    for (int i = 0; i < N_FISH_MAX; i++) s_starve_s[i] = 0;
    s_setup_pending = true;          /* a new tank: welcome, names, colours */
    s_newborn = -1;
    s_sd_pending = 0;
    s_sale_need = 0;
    s_fry_seed = 0; s_fry_age = 0; s_fry_sold = 0;
    s_booted = true;
    mark_dirty();
}

bool progression_reset(tank_t *t, uint32_t seed) {
    if (!persist_port_erase()) return false;
    /* Erase commits a durable empty-profile tombstone. From this point the
     * wipe succeeded, even if writing the new unchosen population needs a
     * retry. Reporting failure would falsely promise the old profile remains. */
    tank_init(t, seed);
    aquarium_port_id(t->aquariums.id);
    progression_fresh(t);
    s_wipe_save_pending = !progression_save(t);
    return true;
}

void progression_set_age(tank_t *t, int idx, float seconds) {
    if (idx < 0 || idx >= t->n_fish) return;
    s_age[idx] = seconds < 0 ? 0 : seconds;
    apply_stage(&t->fish[idx], s_age[idx], true);
    apply_growth(&t->fish[idx]);
    mark_dirty();
}

/* restore the saved tank into t; false = no usable save (t untouched).
 * *saved_unix gets the save's wall-clock stamp (0 if unknown). */
static bool bank_valid(const bank_fish_t *b);
static bool migrate_profile(save_t *sv) {
    if (!sv->profile.marker) {
        /* The unshipped schema-15 Creative slot is not a Normal save. Never
         * leak its free fish/items into Normal on an upgrade. */
        if (sv->creative.enabled) return false;
        memset(&sv->profile, 0, sizeof sv->profile);
        sv->profile.marker = PROFILE_MARKER;
        sv->profile.version = 1;
        sv->profile.chosen = 1;
        sv->profile.reserved = 1;
        sv->profile.owned_items = sv->sd_unlocks;
        sv->profile.paid_items = sv->sd_unlocks;
        sv->profile.owned_items |= (uint64_t)sv->aquariums.stat_upgrades << 32;
        sv->profile.owned_items |= (uint64_t)sv->polish.scenery_unlocks << SD_SCENERY_FIRST;
        sv->profile.paid_items = sv->profile.owned_items;
    }
    return profile_valid(&sv->profile) &&
        !!sv->creative.enabled == (sv->profile.mode == GAME_MODE_CREATIVE);
}
static bool extra_games_valid(const game_records_t *s) {
    return companion_games_valid(s);
}
static bool load_save(tank_t *t, int64_t *saved_unix, const save_t *record) {
    save_t sv; memset(&sv, 0, sizeof sv);
    *saved_unix = 0; s_loaded_release = 0; s_loaded_unix = 0;
    /* an older build's shorter save fills a prefix; the zeroed rest reads as
     * every later tail's defaults (see the tail comments in save_t) */
    size_t got = 0;
    bool loaded;
    if (record) { sv = *record; got = sizeof sv; loaded = true; }
    else loaded = persist_port_load(&sv, sizeof sv, &got) && got >= SAVE_CORE_SIZE && got <= sizeof sv;
    if (!loaded || (got > offsetof(save_t, care) && got < offsetof(save_t, inventory)) ||
        (got > offsetof(save_t, inventory) && got != sizeof sv) ||
        sv.magic != SAVE_MAGIC || !migrate_profile(&sv) || !care_valid(&sv.care) ||
        !inventory_valid(&sv.inventory) || sv.n_fish > N_FISH_MAX ||
        !mussel_valid(&sv.mussel) ||
        !game_income_valid(&sv.game_income, sv.care.game_paid) ||
        (sv.profile.mode != GAME_MODE_HARD && sv.n_fish < 2 && !sv.companion.away) ||
        sv.n_fish + sv.companion.away > N_FISH_MAX) return false;
    if (!sv.aquariums.count) sv.aquariums.count = 1;
    if (sv.aquariums.count > AQUARIUM_CAP || sv.aquariums.active >= sv.aquariums.count ||
        (sv.aquariums.stat_upgrades & ~STAT_MASK) ||
        (sv.aquariums.count > 1 && !(sv.aquariums.id[0] | sv.aquariums.id[1]))) return false;
    if (sv.bank_count > FISH_BANK_CAP) return false;
    if (got > offsetof(save_t, companion) && got != offsetof(save_t, companion) + 392 &&
        got != offsetof(save_t, companion) + 512 &&
        got < offsetof(save_t, companion) + sizeof sv.companion) return false;
    if (!companion_valid_mode(&sv.companion, sv.creative.enabled != 0)) return false;
    if ((got > offsetof(save_t, polish) && got < offsetof(save_t, story)) ||
        (got > offsetof(save_t, story) && got < offsetof(save_t, games)) ||
        (got > offsetof(save_t, games) && got < offsetof(save_t, rpg)) ||
        (got > offsetof(save_t, rpg) && got < offsetof(save_t, creative)) ||
        (got > offsetof(save_t, creative) && got < offsetof(save_t, games_extra)) ||
        (got > offsetof(save_t, games_extra) && got < offsetof(save_t, care)) ||
        !creative_valid(&sv.creative) ||
        (sv.creative.enabled && sv.n_fish + sv.companion.away > CREATIVE_FISH_CAP) ||
        !companion_rpg_valid(&sv.rpg, sv.polish.next_id) ||
        !companion_games_valid(&sv.games) || !extra_games_valid(&sv.games_extra) ||
        !companion_polish_valid(&sv.polish, sv.n_fish, sv.bank_count, sv.companion.away) ||
        !companion_story_valid(&sv.story, sv.n_fish, sv.bank_count, sv.companion.away, sv.polish.next_id)) {
        fprintf(stderr, "Saved aquarium memories invalid or incomplete (%u bytes)\n", (unsigned)got);
        return false;
    }
    if (sv.companion.away && !bank_valid(&sv.companion.traveler)) return false;
    for (int i = 0; i < sv.bank_count; i++) if (!bank_valid(&sv.bank[i])) return false;
    tank_reframe(t, sv.companion.display_compact != 0);
    if (got == SAVE_PRE_BUBBLE_SIZE) {         /* the first public installer's layout: see SAVE_PRE_BUBBLE_SIZE */
        memmove(&sv.ms_seen, &sv.bubble_x, sizeof sv.ms_seen + sizeof sv.tank_ms_seen);
        sv.bubble_x = 0;                       /* = the default column */
    }
    *saved_unix = sv.saved_unix; s_loaded_unix = sv.saved_unix;
    s_loaded_release = sv.saved_release;
    t->n_fish = 0;
    for (int i = 0; i < sv.n_fish; i++) {
        const fish_save_t *s = &sv.fish[i];
        tank_make_fish(t, i, s->preset % tank_roster_count(), s->sociable, s->bold, (stage_t)(s->stage & 3));
        fish_t *f = &t->fish[i];
        restore_fish_state(f, s);
        f->ms_seen = sv.ms_seen[i] & f->ms_bits;
        f->rest_dx = s->rest_dx; f->rest_dy = s->rest_dy;
        f->drift_acc = sv.drift_acc[i];
        s_age[i] = s->age_s;
        t->n_fish = i + 1;
        if (sv.names[i][0]) { sv.names[i][FISH_NAME_MAX] = 0; tank_set_name(t, i, sv.names[i]); }
        tank_set_look(t, i, sv.body[i], sv.accent[i]);        /* zeros keep the preset's */
        f->parent_a = (int8_t)(sv.parent_p1[i][0] - 1); f->parent_b = (int8_t)(sv.parent_p1[i][1] - 1);   /* 0 = none = -1 */
        if (f->parent_a >= sv.n_fish) f->parent_a = -1;
        if (f->parent_b >= sv.n_fish) f->parent_b = -1;
    }
    s_setup_pending = sv.setup_pending != 0;
    s_newborn = sv.newborn_p1 && sv.newborn_p1 <= sv.n_fish ? sv.newborn_p1 - 1 : -1;
    if (s_newborn >= 0 && t->fish[s_newborn].stage != STAGE_FRY) {
        fprintf(stderr, "Clearing obsolete fry welcome for an established fish\n");
        s_newborn = -1;
        mark_dirty();
    }
    if (sv.bubble_x > 0) tank_set_bubble_x(t, sv.bubble_x);
    t->light_idle_s = sv.light_idle_s ? sv.light_idle_s : LIGHT_IDLE_S;
    t->light_auto = sv.light_auto != 0;
    t->light_manual_off = !t->light_auto && sv.light_manual_off != 0;
    t->light_tip_seen = sv.light_tip_seen != 0;
    t->tools_locked = sv.tools_locked != 0;
    t->screen_turned = false;
    t->orient_lock = (sv.orient & 1) != 0; if (t->orient_lock) t->orient_inv = (sv.orient & 2) != 0;
    t->autofeed_off = sv.autofeed_off != 0;
    t->light_override = false; t->light_on = true;   /* never restored (2026-09-15): a saved
                                                      * override once froze a tank in permanent day */
    t->feed_spot_x = sv.feed_spot_x; t->player_feedings = sv.player_feedings;
    t->hold_approaches = sv.hold_approaches; t->tank_ms_bits = sv.tank_ms_bits;
    t->tank_ms_seen = sv.tank_ms_seen & t->tank_ms_bits;
    for (int b = 0; b < VEG_BEDS; b++) {
        if (sv.veg_h[b][0] > 0)
            for (int i = 0; i < VEG_FRONDS_MAX; i++) t->veg_h[b][i] = sv.veg_h[b][i];
        else if (sv.veg_growth[b] > 0) tank_veg_set(t, b, sv.veg_growth[b]);
    }
    tank_veg_sync(t);
    memcpy(t->algae, sv.algae, ALGAE_CELLS);
    t->trims = sv.trims; t->cells_cleaned = sv.cells_cleaned;
    /* the sand dollar tail (zeros for an older save: the ledger back-pays) */
    t->sd_balance = sv.sd_balance; t->sd_earned = sv.sd_earned; t->sd_unlocks = sv.sd_unlocks & SD_ITEM_MASK;
    for (int i = 0; i < N_FISH_MAX; i++) t->sd_paid_fish[i] = i < t->n_fish ? sv.sd_paid_fish[i] : 0;
    t->sd_colonies_paid = sv.sd_colonies_paid; t->sd_inches_paid = sv.sd_inches_paid;
    t->algae_colonies = sv.algae_colonies; t->trim_px = sv.trim_px;
    if (sv.snail_x > 0) { t->snail_x = sv.snail_x; t->snail_y = sv.snail_y; }
    t->snail_grazed = sv.snail_grazed;
    if ((t->sd_unlocks & SD_ITEM_PLANT) || sv.inventory.stored[tank_decor_slot(0)]) {
        if (sv.veg_h3[0] > 0) for (int i = 0; i < VEG_FRONDS_MAX; i++) t->veg_h[3][i] = sv.veg_h3[i];
        else tank_plant_place(t);
        tank_veg_sync(t);
    }
    if (sv.plant_x > 0) tank_decor_set(t, 0, sv.plant_x, sv.plant_z1 ? sv.plant_z1 - 1 : DECOR_Z_MIDDLE);
    if (sv.castle_x > 0) tank_decor_set(t, 2, sv.castle_x, sv.castle_z1 ? sv.castle_z1 - 1 : DECOR_Z_FRONT);
    if (sv.coral_x > 0) tank_decor_set(t, 3, sv.coral_x, sv.coral_z1 ? sv.coral_z1 - 1 : DECOR_Z_FRONT);   /* (an AMONG save reads as FRONT) */
    if (sv.coral_rgb) tank_coral_set_rgb(t, sv.coral_rgb);
    t->coral_growth = sv.coral_growth > 0 ? sv.coral_growth : 0;   /* 0 = full (tank_coral_growth) */
    if (sv.cluster_x > 0) tank_decor_set(t, 4, sv.cluster_x, sv.cluster_z1 ? sv.cluster_z1 - 1 : DECOR_Z_FRONT);
    tank_cluster_set_scheme(t, sv.cluster_scheme);
    t->cluster_growth = sv.cluster_growth > 0 ? sv.cluster_growth : 0;
    tank_pumpkin_place(t);                            /* the lantern's spot and depth (0.3.6) */
    if (sv.pumpkin_z1 == DECOR_Z_FRONT + 1) t->pumpkin_z = DECOR_Z_FRONT;
    if (sv.pumpkin_x > 0) tank_decor_set(t, 8, sv.pumpkin_x, t->pumpkin_z);
    memcpy(t->decor_rank, sv.decor_rank, sizeof sv.decor_rank);
    t->decor_rank[5] = sv.chest_rank;
    t->chest_x = 0; t->chest_z = DECOR_Z_BACK;
    if (sv.chest_x > 0) tank_decor_set(t, 9, sv.chest_x, sv.chest_z1 ? sv.chest_z1 - 1 : DECOR_Z_BACK);
    for (int i = 0; i < 4; i++) {
        t->decor_rank[6 + i] = sv.ornament_rank[i];
        t->ornament_x[i] = 0; t->ornament_z[i] = DECOR_Z_BACK;
        if (sv.ornament_x[i] > 0) tank_decor_set(t, 10 + i, sv.ornament_x[i], sv.ornament_z1[i] ? sv.ornament_z1[i] - 1 : DECOR_Z_BACK);
    }
    t->sand_style = (t->sd_unlocks & SD_ITEM_SAND) && sv.sand_style < SAND_STYLE_COUNT ? sv.sand_style : 0;
    t->water_style = (t->sd_unlocks & SD_ITEM_LIGHTS) && sv.water_style < WATER_STYLE_COUNT ? sv.water_style : 0;
    for (int i = 0; i < 6; i++) {
        t->decor_rank[10 + i] = sv.extra_rank[i];
        t->ornament_x[4 + i] = 0; t->ornament_z[4 + i] = DECOR_Z_BACK;
        if (sv.extra_x[i] > 0) tank_decor_set(t, 16 + i, sv.extra_x[i], sv.extra_z1[i] ? sv.extra_z1[i] - 1 : DECOR_Z_BACK);
    }
    for (int i = 0; i < N_FISH_MAX; i++) {
        t->fish[i].variety = sv.fish_variety[i] < FISH_VARIETY_COUNT ? sv.fish_variety[i] : 0;
        t->fish[i].pattern = sv.fish_pattern[i] < FISH_PATTERN_COUNT ? sv.fish_pattern[i] : 0;
    }
    t->aquariums = sv.aquariums;
    t->companion = sv.companion;
    t->polish = sv.polish;
    t->story = sv.story;
    t->games = sv.games;
    t->rpg = sv.rpg;
    t->creative = sv.creative;
    t->games_extra = sv.games_extra;
    t->profile = sv.profile;
    t->inventory = sv.inventory;
    t->mussel = sv.mussel; t->mussel_fraction = 0; t->mussel_glint = 0;
    if (t->mussel.version == 1 && t->mussel.owned) {
        t->mussel.version = 2;
        t->mussel.owned = (uint8_t)(1u << t->aquariums.active);
    }
    t->game_income = sv.game_income;
    t->care = sv.care; t->care_fraction = 0;
    s_hard_save_failed = s_hard_departed = false;
    t->visitor_left = 0;
    for (int i = 0; i < t->n_fish; i++) {
        t->fish[i].wearable = t->companion.active[i].wearable;
        t->fish[i].wear_colors = t->companion.active[i].wear_colors;
    }
    if (!t->companion.version) t->companion.version = COMPANION_VERSION;
    t->companion_fraction = 0;
    if (t->aquariums.clock_theme >= CLOCK_THEME_COUNT ||
        t->aquariums.clock_utc_quarters < -48 || t->aquariums.clock_utc_quarters > 56 ||
        !clock_layout_valid(t->aquariums.clock_layout) || !clock_delay_valid(t->aquariums.clock_delay)) {
        fprintf(stderr, "Invalid saved clock options; clock disabled\n");
        t->aquariums.clock_theme = CLOCK_OFF; t->aquariums.clock_flags = 0; t->aquariums.clock_utc_quarters = 0; t->aquariums.clock_layout = 0;
        t->aquariums.clock_delay = 0;
    }
    if (!memchr(t->aquariums.name, 0, sizeof t->aquariums.name) || !t->aquariums.name[0])
        snprintf(t->aquariums.name, sizeof t->aquariums.name, "TANK %u", t->aquariums.active + 1);
    t->journal = sv.journal;
    t->bank_count = sv.bank_count;
    memcpy(t->bank, sv.bank, sizeof t->bank);
    if (t->journal.xp > JOURNAL_XP_MAX) t->journal.xp = JOURNAL_XP_MAX;
    t->journal.claimed &= (1u << JOURNAL_GOAL_N) - 1;
    t->journal.seen &= (1u << FISH_VARIETY_COUNT) - 1;
    t->journal.grown &= t->journal.seen;
    t->journal.initialized = !!t->journal.initialized;
    if (t->journal.trust > 10) t->journal.trust = 10;
    if (t->journal.population > N_FISH_MAX) t->journal.population = N_FISH_MAX;
    if (!journal_theme_unlocked(t, t->journal.theme)) t->journal.theme = 0;
    if (t->sd_unlocks & SD_ITEM_SHRIMP) {             /* the school back in the grass, its count and its progress */
        tank_shrimp_place(t, sv.shrimp_n >= SHRIMP_START ? sv.shrimp_n : SHRIMP_START);
        t->shrimp_food = sv.shrimp_food <= SHRIMP_PER_JOIN ? sv.shrimp_food : SHRIMP_PER_JOIN;
        t->shrimp_cool = sv.shrimp_cool > 0 && sv.shrimp_cool <= SHRIMP_COOLDOWN_S ? sv.shrimp_cool : 0;
        t->shrimp_eaten = sv.shrimp_eaten > 0 ? sv.shrimp_eaten : 0;
    } else { t->shrimp_n = 0; t->shrimp_eaten = 0; }
    if (t->sd_unlocks & SD_ITEM_URCHIN) {             /* back where it was on the floor, its tally */
        tank_urchin_place(t);
        if (sv.urchin_x > 0) t->urchin_x = sv.urchin_x;
        t->urchin_grazed_px = sv.urchin_grazed_px > 0 ? sv.urchin_grazed_px : 0;
    } else { t->urchin_x = -1; t->urchin_grazed_px = 0; }
    if (t->sd_unlocks & SD_ITEM_SEAHORSE) {           /* back on a blade, its tally */
        tank_seahorse_place(t);
        t->sh_s = sv.sh_s > 0 ? sv.sh_s : 0;
        t->sh_hitches = sv.sh_hitches > 0 ? sv.sh_hitches : 0;
        t->sh_trust_passed = sv.sh_trust_passed > 0 ? sv.sh_trust_passed : 0;
        t->sh_eaten = sv.sh_eaten > 0 ? sv.sh_eaten : 0;
        t->sh_snack_pay = sv.sh_snack_pay > 0 ? sv.sh_snack_pay : 0;
        t->sd_snacks_paid = sv.sd_snacks_paid > 0 ? sv.sd_snacks_paid : 0;
        t->sh_snack_cool = sv.sh_snack_cool > 0 && sv.sh_snack_cool <= SH_SNACK_COOL_S ? sv.sh_snack_cool : 0;
    } else { t->sh_x = -1; t->sh_s = 0; t->sh_hitches = 0; t->sh_trust_passed = 0; t->sh_eaten = 0; t->sh_snack_pay = 0; t->sd_snacks_paid = 0; t->sh_snack_cool = 0; }
    s_sd_pending = 0;
    s_arrival_pending = sv.arrival_pending; s_spawn_in = -1;
    s_sale_need = sv.sale_meals_need > 0 ? sv.sale_meals_need : 0;
    s_fry_seed = sv.fry_seed; s_fry_age = sv.fry_age_min * 60.0f; s_fry_sold = sv.fry_sold & ((1u << FRY_OFFER_N) - 1);
    s_prev_night = t->night;
    companion_polish_init(t, false);
    return true;
}

/* A cold boot lives the absence exactly as a deep-sleep wake does
   (2026-09-16). It used to restore the save and pin everyone at hunger 9.6
   after an hour away - "the one offline rule" - while only the deep-sleep
   wake simulated the span; so the morning after a cell died in the night
   (a PMIC power-off, hence a cold boot) came up with no algae, no growth
   and no night credited. Now every boot with a clock and a save is a wake;
   a long night ends in ravenous begging by itself (hunger +0.8/h crosses
   the begging line in progression_tick). Without a clock nothing can be
   simulated and the tank simply resumes. Returns the hours lived through,
   -1 for none (no save, no clock). */
float progression_boot(tank_t *t) {
    return progression_wake(t, clock_port_now_unix());
}

void progression_settings_changed(void) { mark_dirty(); }

void progression_slept(tank_t *t, float seconds) {
    if (seconds <= 0) return;
    tank_tick_sleep(t, seconds);
    fry_age(t, seconds);                      /* the fry offer ages asleep too, in full */
    for (int i = 0; i < t->n_fish; i++) {     /* and they grew, slowly, in the dark */
        s_age[i] += seconds * SLEEP_GROWTH_FRAC;
        apply_stage(&t->fish[i], s_age[i], false); apply_growth(&t->fish[i]);
    }
    /* the tank milestone (2026-09-15, was "first quiet night" - every fish
       resting under the old day/night cycle): one unbroken stretch of device
       sleep as long as a night. The cap keeps a week's absence a single span. */
    if (seconds >= FULL_NIGHT_S) set_tms(t, TMS_FIRST_FULL_NIGHT);
    mark_dirty();
}

float progression_wake(tank_t *t, int64_t now_unix) {
    int64_t saved_unix;
    s_booted = true;
    if (!load_save(t, &saved_unix, NULL)) {
        /* A corrupt/incompatible existing save must never be replaced by a
         * fresh tank, even when its bytes had a valid storage CRC. */
        save_t *probe = calloc(1, sizeof *probe);
        if (!probe) { s_booted = false; return -1; }
        size_t got = 0;
        bool exists = persist_port_load(probe, sizeof *probe, &got);
        free(probe);
        if (exists || persist_port_blocked()) {
            s_booted = false;
            return -1;
        }
        progression_fresh(t); progression_care_refresh(t); return -1;
    }
    float slept = -1;
    if (now_unix > 0 && saved_unix > 0 && now_unix > saved_unix) {
        int64_t span = now_unix - saved_unix;
        if (span > PROGRESSION_SLEEP_CAP_S) span = PROGRESSION_SLEEP_CAP_S;
        progression_slept(t, (float)span);
        slept = span / 3600.0f;
    }
    progression_woke(t);                     /* a fry staged before the sleep: born at the wake */
    progression_care_refresh(t);
    return slept;
}

const char *progression_hard_status(const tank_t *t) {
    if (persist_port_blocked() || !s_booted) return "SAVE UNAVAILABLE - RESTART / RETRY";
    if (s_wipe_save_pending) return "PROFILE WIPED - NEW SAVE PENDING RETRY";
    if (!progression_hard(t)) return NULL;
    if (s_hard_save_failed) return "DEATH SAVE FAILED - FISH KEPT; RETRY";
    if (!t->n_fish) return "AQUARIUM EMPTY - RESCUE PAIR AVAILABLE";
    for (int i = 0; i < t->n_fish; i++)
        if (t->profile.starvation_s[i] >= HARD_STARVATION_S - 3600)
            return "STARVATION RISK - FEED YOUR FISH NOW";
    if (s_hard_departed) return "A FISH DIED FROM PROLONGED STARVATION";
    return NULL;
}
bool progression_storage_ready(void) {
    return s_booted && !persist_port_blocked();
}
static void hard_tick(tank_t *t, float dt) {
    if (!progression_hard(t) || !isfinite(dt) || dt <= 0) return;
    for (int i = 0; i < t->n_fish; i++) {
        float *elapsed = &t->profile.starvation_s[i];
        if (t->fish[i].hunger < 8.5f) *elapsed = 0;
        else if (t->fish[i].hunger >= 9.5f) *elapsed = fminf(HARD_STARVATION_S, *elapsed + dt);
        if (*elapsed < HARD_STARVATION_S) continue;
        tank_t *next = malloc(sizeof *next);
        if (!next) { s_hard_save_failed = true; return; }
        *next = *t;
        companion_memory(next, MEMORY_DEATH, i, 0);
        tank_remove_fish(next, i);
        /* capture_save uses precise ages kept outside tank_t. */
        double ages[N_FISH_MAX]; memcpy(ages, s_age, sizeof ages);
        for (int k = i; k < next->n_fish; k++) s_age[k] = s_age[k + 1];
        s_age[next->n_fish] = 0;
        if (progression_save(next)) {
            *t = *next; s_hard_save_failed = false; s_hard_departed = true;
            for (int k = i; k < t->n_fish; k++) s_starve_s[k] = s_starve_s[k + 1];
            s_starve_s[t->n_fish] = 0;
            s_newborn = -1; s_arrival_pending = false; s_spawn_in = -1;
            i--;
        } else {
            memcpy(s_age, ages, sizeof ages); s_hard_save_failed = true;
        }
        free(next);
        if (s_hard_save_failed) return;
    }
}
void progression_tick(tank_t *t, float dt) {
    if (!s_booted || !isfinite(dt) || dt <= 0) return;
    t->mussel_glint = fmaxf(0, t->mussel_glint - fminf(dt, 1));
    care_tick(t, dt);
    mussel_tick(t, dt);
    hard_tick(t, dt);
    float aged = dt * progression_time_scale;                    /* growth: every awake second, lit or not */
    float tended = t->night ? 0 : aged;                          /* drift: pressure only while lit and lived-in */
    int n_dart = 0; bool changed_someone = false, reef_visit = false;
    for (int i = 0; i < t->n_fish; i++) {
        fish_t *f = &t->fish[i];
        s_age[i] += aged;
        apply_stage(f, s_age[i], false);
        apply_growth(f);
        /* trait drift, slow: calm + fed -> bolder/more social; startled -> shyer */
        float k = tended / (DRIFT_HOURS * 3600.0f);          /* full unit per DRIFT_HOURS of pressure */
        float pressure = 0;                                   /* what the traits WANTED to move, before the clamps */
        if (f->stress > 7) { f->bold = clampf(f->bold - k, 0.05f, 0.95f); pressure += k; }
        else if (f->hunger < 4 && f->stress < 2) { f->bold = clampf(f->bold + k * 0.5f, 0.05f, 0.95f); pressure += k * 0.5f; }
        if (f->goal.id == GOAL_FOLLOW_FRIEND) { f->sociable = clampf(f->sociable + k * 0.5f, 0.05f, 0.95f); pressure += k * 0.5f; }
        else if (f->goal.id == GOAL_EXPLORE) { f->sociable = clampf(f->sociable - k * 0.2f, 0.05f, 0.95f); pressure += k * 0.2f; }
        f->drift_acc += pressure;
        if (fabsf(f->bold - f->bold0) >= 0.11f || fabsf(f->sociable - f->sociable0) >= 0.11f) changed_someone = true;

        /* milestones: firsts the fish chose to do */
        if (f->goal.id == GOAL_DART_PLAY && f->goal_age > 1.0f) set_ms(f, MS_FIRST_DART);
        if (f->goal.id == GOAL_VISIT_BUBBLES && tank_dist(f->x, f->y, t->bubble_x, t->bubble_y) < 90) set_ms(f, MS_FIRST_BUBBLES);
        if (f->goal.id == GOAL_INSPECT_REEF) {               /* its landmark: the reef cluster once there is one */
            float rx, ry; tank_reef_spot(t, &rx, &ry);
            if (tank_dist(f->x, f->y, rx, ry) < 90) {
                set_ms(f, MS_INSPECTED);                      /* no badge (0.3.0): it reveals the curiosity slider */
                if (t->sd_unlocks & SD_ITEM_CLUSTER) reef_visit = true;
            }
        }
        /* the seagrass: resting inside a canopy, or tucked into one while
           rattled - the grass as the place to be safe (2026-10-02, in the
           badge slot "first reef" had) */
        if (((f->goal.id == GOAL_REST && f->goal_age > 2.0f) || f->stress >= 5.0f || f->breather > 2.0f) && tank_in_grass(t, f->x, f->y))
            set_ms(f, MS_FIRST_GRASS);
        if (f->goal.id == GOAL_FOLLOW_FRIEND && f->goal_age > 2.0f) set_ms(f, MS_FIRST_FOLLOW);
        if (f->goal.id == GOAL_DART_PLAY) n_dart++;
    }
    if (n_dart >= 2) set_tms(t, TMS_FIRST_PLAY_SESSION);
    if (changed_someone) set_tms(t, TMS_CHANGED_SOMEONE);
    if (t->player_feedings > 0) set_tms(t, TMS_FIRST_FEEDING);
    /* upkeep milestones + event saves (a chore done deserves to stick) */
    static int32_t s_prev_trims, s_prev_cleaned;
    if (t->trims > 0) set_tms(t, TMS_FIRST_TRIM);
    if (t->algae_colonies > 0) set_tms(t, TMS_FIRST_CLEANING);   /* the first colony wiped away, as the first cut is the first trimming
                                                                    (0.3.2; it was 30 cells, hours of film on a new tank) */
    if ((t->sd_unlocks & SD_ITEM_SHRIMP) && t->shrimp_n >= SHRIMP_MAX) set_tms(t, TMS_FULL_SCHOOL);
    if ((t->sd_unlocks & SD_ITEM_SEAHORSE) && t->sh_eaten > 0) set_tms(t, TMS_SEAHORSE_FED);   /* its first snack (0.3.6) */
    if (reef_visit) set_tms(t, TMS_FIRST_REEF);  /* a fish chose to look at the reef cluster */
    if (s_sale_need > 0) set_tms(t, TMS_FIRST_SALE);   /* a tank that sold a fish before 0.3.5: its badge, and the fry */
    fry_age(t, aged);
    if (t->trims != s_prev_trims || t->cells_cleaned != s_prev_cleaned) {
        s_prev_trims = t->trims; s_prev_cleaned = t->cells_cleaned;
        mark_dirty();
    }
    sd_tick(t);                                  /* the sand dollars owed for all of the above */
    journal_tick(t);

    /* ravenous: a starving tank with empty water begs at the surface (tank.c
     * renders the wait; the trickle holds off so the keeper's pellets are the
     * event). Entered at boot after a long absence, or live whenever everyone
     * is starving - waking from device sleep lands here naturally. If nobody
     * comes, after a while the fish give up and the tank feeds itself. */
    int any_food = 0; for (int i = 0; i < MAX_FOOD; i++) any_food |= t->food[i].alive;
    if (!s_ravenous && !any_food && t->n_fish > 0) {
        float mn = 10;
        for (int i = 0; i < t->n_fish; i++) if (t->fish[i].hunger < mn) mn = t->fish[i].hunger;
        if (s_gave_up && (!t->autofeed_off || mn < 8.5f || t->player_feedings != s_gave_feedings0)) s_gave_up = false;
        if (mn >= 8.5f && !s_gave_up) { s_ravenous = true; s_ravenous_t = 0; s_rav_feedings0 = t->player_feedings; }
    }
    if (s_ravenous) {
        if (!any_food) s_ravenous_t += dt;    /* the wait; a dash for live pellets isn't giving up */
        float mx = 0;
        for (int i = 0; i < t->n_fish; i++) if (t->fish[i].hunger > mx) mx = t->fish[i].hunger;
        if (mx < 7.0f) s_ravenous = false;                    /* everyone got a bite */
        else if (s_ravenous_t > RAVENOUS_GIVE_UP_S) {         /* nobody came: back to life */
            s_ravenous = false;
            if (!t->autofeed_off) tank_scatter_food(t, 2);    /* so it doesn't re-trigger at once */
            else { s_gave_up = true; s_gave_feedings0 = t->player_feedings; }   /* AUTO FEED off: nothing falls; they
                                                                 stop begging and go hungry until the keeper feeds */
        }
    }
    /* AUTO FEED off: going hungry costs trust (the constants' note above) */
    for (int i = 0; i < t->n_fish; i++) {
        fish_t *f = &t->fish[i];
        if (t->companion.cozy || !t->autofeed_off || f->hunger < STARVE_HUNGER) { s_starve_s[i] = 0; continue; }
        if (t->night) continue;                               /* dark: they rest; the count holds */
        s_starve_s[i] += dt;
        if (s_starve_s[i] > STARVE_GRACE_S && f->trust > STARVE_TRUST_FLOOR)
            f->trust = fmaxf(STARVE_TRUST_FLOOR, f->trust - STARVE_TRUST_PER_S * dt);
    }
    /* tank.c picks the presentation: empty water = beg at the surface; live
     * pellets = feeding-frenzy dash (real starving fish DART at fresh food) */
    t->ravenous = s_ravenous;
    t->ravenous_fed = s_ravenous && t->player_feedings != s_rav_feedings0;

    /* the courtship tell: one condition shy of an arrival (or one staged),
     * the parents-to-be pair up - tank.c stages the episodes. The pair is
     * the one do_arrival will name (pick_parents). */
    t->courting = false; t->court_a = t->court_b = -1;
    if (!progression_creative(t) && occupied_slots(t) < POP_CAP && occupied_slots(t) < N_FISH_MAX && tank_nursery_bed(t) >= 0) {
        /* ... and only with a nursery: a bed tall enough to hide in. Shave
         * every bed and the courting stops until one regrows. */
        int met, total;
        arrival_conditions(t, &met, &total);
        if (s_arrival_pending || met >= total - 1) {
            int a, b;
            pick_parents(t, &a, &b);
            if (a >= 0 && b >= 0) { t->courting = true; t->court_a = (int8_t)a; t->court_b = (int8_t)b; }
        }
    }

    /* light-on: greet (the light no longer brings the fry) */
    bool light_on_edge = s_prev_night && !t->night;
    if (s_prev_night != t->night) mark_dirty();
    s_prev_night = t->night;
    if (light_on_edge) t->greet_timer = 6.0f;
    if (!s_arrival_pending && arrival_earned(t)) { s_arrival_pending = true; mark_dirty(); }

    /* the spawning (progression.h): a staged fry is born on its own, a
     * little after the last gate closed, while the keeper can see it. The
     * wait counts real awake seconds (not progression_time_scale) and pauses
     * under a page; a dance cut short (a page, the grass shaved) starts
     * over with a fresh wait. */
    if (!s_arrival_pending || !t->courting || t->ui_cover) {
        if (t->spawning) s_spawn_in = -1;
        t->spawning = false; t->spawn_danced = 0;
    } else if (!t->spawning) {
        if (s_spawn_in < 0) s_spawn_in = tank_randf(t, SPAWN_WAIT_MIN_S, SPAWN_WAIT_MAX_S);
        if ((s_spawn_in -= dt) <= 0) { s_spawn_in = -1; t->spawning = true; t->spawn_danced = 0; }
    } else if (t->spawn_danced >= SPAWN_DANCE_S) {
        do_arrival(t);                       /* between them, in the fronds */
        tank_court_puff(t, 3);
    }

    /* saves: coalesced event saves + heartbeat */
    s_since_save += dt; if (s_dirty) s_dirty_since += dt;
    if ((s_dirty && s_since_save >= SAVE_MIN_GAP_S) || s_since_save >= SAVE_HEARTBEAT_S)
        progression_save(t);
}

/* Fill caller storage: returning a whole save adds a second large CYD stack frame. */
static void capture_save(const tank_t *t, save_t *sv) {
    memset(sv, 0, sizeof *sv);
    sv->magic = SAVE_MAGIC; sv->saved_unix = clock_port_now_unix(); sv->clock = t->clock;
    sv->aquariums = t->aquariums;
    sv->companion = t->companion;
    sv->polish = t->polish;
    sv->story = t->story;
    sv->games = t->games;
    sv->rpg = t->rpg;
    sv->creative = t->creative;
    sv->games_extra = t->games_extra;
    sv->profile = t->profile;
    sv->inventory = t->inventory;
    sv->mussel = t->mussel;
    sv->game_income = t->game_income;
    sv->care = t->care;
    if (sv->polish.version)
        snprintf(sv->polish.tank_name, sizeof sv->polish.tank_name, "%s", t->aquariums.name);
    sv->saved_release = PT_RELEASE_NUM;
    sv->sale_meals_need = s_sale_need;
    sv->fry_seed = s_fry_seed; sv->fry_sold = s_fry_sold;
    sv->fry_age_min = (uint16_t)(s_fry_age / 60 < 65535 ? s_fry_age / 60 : 65535);
    /* light_override / light_on stay zero in the save (2026-09-15) */
    sv->light_idle_s = (uint16_t)t->light_idle_s; sv->light_auto = t->light_auto; sv->light_manual_off = t->light_manual_off;
    sv->light_tip_seen = t->light_tip_seen;
    sv->tools_locked = t->tools_locked;
    sv->screen_turned = t->screen_turned;
    sv->orient = (uint8_t)(t->orient_lock ? 1 | (t->orient_inv ? 2 : 0) : 0);
    sv->autofeed_off = t->autofeed_off;
    sv->arrival_pending = s_arrival_pending; sv->n_fish = (uint8_t)t->n_fish;
    if (progression_hard(t) && t->n_fish < 2) sv->arrival_pending = false;
    sv->feed_spot_x = t->feed_spot_x; sv->player_feedings = t->player_feedings;
    sv->hold_approaches = t->hold_approaches; sv->tank_ms_bits = t->tank_ms_bits;
    sv->tank_ms_seen = t->tank_ms_seen;
    for (int b = 0; b < VEG_BEDS; b++) {
        sv->veg_growth[b] = t->veg_growth[b];
        for (int i = 0; i < VEG_FRONDS_MAX; i++) sv->veg_h[b][i] = t->veg_h[b][i];
    }
    memcpy(sv->algae, t->algae, ALGAE_CELLS);
    sv->trims = t->trims; sv->cells_cleaned = t->cells_cleaned;
    sv->sd_balance = t->sd_balance; sv->sd_earned = t->sd_earned; sv->sd_unlocks = t->sd_unlocks;
    for (int i = 0; i < N_FISH_MAX; i++) sv->sd_paid_fish[i] = t->sd_paid_fish[i];
    sv->sd_colonies_paid = t->sd_colonies_paid; sv->sd_inches_paid = t->sd_inches_paid;
    sv->algae_colonies = t->algae_colonies; sv->trim_px = t->trim_px;
    sv->snail_x = t->snail_x > 0 ? t->snail_x : 0; sv->snail_y = t->snail_y > 0 ? t->snail_y : 0;
    sv->snail_grazed = t->snail_grazed;
    for (int i = 0; i < VEG_FRONDS_MAX; i++)
        sv->veg_h3[i] = ((t->sd_unlocks & SD_ITEM_PLANT) || progression_inventory_stored(t, 0)) ? t->veg_h[3][i] : 0;
    sv->plant_x = t->plant_x > 0 ? t->plant_x : 0; sv->plant_z1 = (uint8_t)(t->plant_z + 1);
    sv->castle_x = t->castle_x > 0 ? t->castle_x : 0; sv->castle_z1 = (uint8_t)(t->castle_z + 1);
    sv->coral_x = t->coral_x > 0 ? t->coral_x : 0; sv->coral_z1 = (uint8_t)(t->coral_z + 1); sv->coral_rgb = t->coral_rgb;
    sv->coral_growth = t->coral_growth;
    sv->cluster_x = t->cluster_x > 0 ? t->cluster_x : 0; sv->cluster_z1 = (uint8_t)(t->cluster_z + 1); sv->cluster_scheme = t->cluster_scheme;
    sv->cluster_growth = t->cluster_growth;
    sv->pumpkin_x = t->pumpkin_x > 0 ? t->pumpkin_x : 0; sv->pumpkin_z1 = (uint8_t)(t->pumpkin_z + 1);
    memcpy(sv->decor_rank, t->decor_rank, sizeof sv->decor_rank);
    sv->chest_x = t->chest_x; sv->chest_z1 = (uint8_t)(t->chest_z + 1); sv->chest_rank = t->decor_rank[5];
    for (int i = 0; i < 4; i++) {
        sv->ornament_x[i] = t->ornament_x[i]; sv->ornament_z1[i] = (uint8_t)(t->ornament_z[i] + 1);
        sv->ornament_rank[i] = t->decor_rank[6 + i];
    }
    sv->sand_style = t->sand_style; sv->water_style = t->water_style;
    sv->journal = t->journal;
    const setup_place_snapshot_t *layout = setup_place_original();
    if (layout) {
        /* Autosaves and JSON backups see the committed layout, never the live preview. */
        memcpy(sv->decor_rank, layout->rank, sizeof sv->decor_rank);
        sv->chest_rank = layout->rank[5];
        for (int i = 0; i < 4; i++) sv->ornament_rank[i] = layout->rank[6 + i];
        switch (layout->item) {
        case 0: sv->plant_x = layout->x; sv->plant_z1 = layout->z + 1; break;
        case 2: sv->castle_x = layout->x; sv->castle_z1 = layout->z + 1; break;
        case 3: sv->coral_x = layout->x; sv->coral_z1 = layout->z + 1; sv->coral_rgb = layout->rgb; break;
        case 4: sv->cluster_x = layout->x; sv->cluster_z1 = layout->z + 1; sv->cluster_scheme = layout->scheme; break;
        case 8: sv->pumpkin_x = layout->x; sv->pumpkin_z1 = layout->z + 1; break;
        case 9: sv->chest_x = layout->x; sv->chest_z1 = layout->z + 1; break;
        default: {
            int i = tank_ornament_index(layout->item);
            if (i >= 0 && i < 4) { sv->ornament_x[i] = layout->x; sv->ornament_z1[i] = layout->z + 1; }
            break;
        }
        }
    }
    sv->bank_count = t->bank_count;
    memcpy(sv->bank, t->bank, sizeof sv->bank);
    for (int i = 0; i < 6; i++) {
        sv->extra_x[i] = t->ornament_x[4 + i]; sv->extra_z1[i] = (uint8_t)(t->ornament_z[4 + i] + 1);
        sv->extra_rank[i] = layout ? layout->rank[10 + i] : t->decor_rank[10 + i];
        if (layout && tank_ornament_index(layout->item) == i + 4) {
            sv->extra_x[i] = layout->x; sv->extra_z1[i] = layout->z + 1;
        }
    }
    for (int i = 0; i < N_FISH_MAX; i++) {
        if (i < t->n_fish) { sv->fish_variety[i] = t->fish[i].variety; sv->fish_pattern[i] = t->fish[i].pattern; }
    }
    sv->shrimp_n = (t->sd_unlocks & SD_ITEM_SHRIMP) ? t->shrimp_n : 0; sv->shrimp_food = t->shrimp_food;
    sv->shrimp_cool = t->shrimp_cool > 0 ? t->shrimp_cool : 0; sv->shrimp_eaten = t->shrimp_eaten;
    sv->urchin_x = (t->sd_unlocks & SD_ITEM_URCHIN) && t->urchin_x > 0 ? t->urchin_x : 0;
    sv->urchin_grazed_px = t->urchin_grazed_px > 0 ? t->urchin_grazed_px : 0;
    if (t->sd_unlocks & SD_ITEM_SEAHORSE) {
        sv->sh_s = t->sh_s > 0 ? t->sh_s : 0; sv->sh_hitches = t->sh_hitches; sv->sh_trust_passed = t->sh_trust_passed;
        sv->sh_eaten = t->sh_eaten; sv->sh_snack_pay = t->sh_snack_pay; sv->sd_snacks_paid = t->sd_snacks_paid; sv->sh_snack_cool = t->sh_snack_cool > 0 ? t->sh_snack_cool : 0;
    }
    sv->setup_pending = s_setup_pending;
    sv->newborn_p1 = (uint8_t)(s_newborn >= 0 && s_newborn < t->n_fish ? s_newborn + 1 : 0);
    sv->bubble_x = t->bubble_x;
    for (int i = 0; i < t->n_fish; i++) {
        const fish_t *f = &t->fish[i]; fish_save_t *s = &sv->fish[i];
        if (strcmp(f->name, tank_roster_name(f->preset))) memcpy(sv->names[i], f->name, FISH_NAME_MAX + 1);
        sv->body[i] = f->color; sv->accent[i] = f->accent;      /* the preset's too: harmless, exact */
        sv->parent_p1[i][0] = (uint8_t)(f->parent_a + 1); sv->parent_p1[i][1] = (uint8_t)(f->parent_b + 1);
        *s = capture_fish(f, s_age[i]);
        sv->ms_seen[i] = f->ms_seen;
        sv->drift_acc[i] = f->drift_acc;
    }
}
bool progression_save(tank_t *t) {
    if (!s_booted || persist_port_blocked()) return false;
    save_t sv; capture_save(t, &sv);
    if (!persist_port_save(&sv, sizeof sv)) {
        if (!s_dirty) { s_dirty = true; s_dirty_since = 0; }
        s_since_save = 0;                         /* retry after SAVE_MIN_GAP_S */
        return false;
    }
    s_since_save = 0; s_dirty = false; s_dirty_since = 0;
    s_wipe_save_pending = false;
    return true;
}

void progression_ack_milestones(tank_t *t) {
    bool changed = false;
    for (int i = 0; i < t->n_fish; i++)
        if (t->fish[i].ms_seen != t->fish[i].ms_bits) { t->fish[i].ms_seen = t->fish[i].ms_bits; changed = true; }
    if (t->tank_ms_seen != t->tank_ms_bits) { t->tank_ms_seen = t->tank_ms_bits; changed = true; }
    if (changed) mark_dirty();
}

#include "cJSON.h"
#include <stdlib.h>
#include <limits.h>

typedef enum { J_U8, J_U16, J_U32, J_I32, J_I64, J_FLOAT, J_BOOL, J_HEX, J_NAMES } json_kind_t;
typedef struct {
    const char *name;
    size_t offset, count;
    json_kind_t kind;
    double min, max;
} json_field_t;
#define J(f, k, n, lo, hi) { #f, offsetof(save_t, f), n, k, lo, hi }
static const json_field_t SAVE_FIELDS[] = {
    J(saved_unix, J_I64, 1, 0, 9007199254740991.0), J(clock, J_FLOAT, 1, 0, 1e12),
    J(light_override, J_BOOL, 1, 0, 1), J(light_on, J_BOOL, 1, 0, 1),
    J(arrival_pending, J_BOOL, 1, 0, 1), J(n_fish, J_U8, 1, 0, N_FISH_MAX),
    J(feed_spot_x, J_FLOAT, 1, -1, TANK_MAX_W), J(player_feedings, J_I32, 1, 0, INT_MAX),
    J(hold_approaches, J_I32, 1, 0, INT_MAX), J(tank_ms_bits, J_U32, 1, 0, UINT32_MAX),
    J(veg_growth, J_FLOAT, VEG_BEDS, 0, 1), J(algae, J_HEX, ALGAE_SAVE, 0, 255),
    J(trims, J_I32, 1, 0, INT_MAX), J(cells_cleaned, J_I32, 1, 0, INT_MAX),
    J(veg_h, J_FLOAT, VEG_BEDS * VEG_FRONDS_MAX, 0, 1),
    J(setup_pending, J_U8, 1, 0, 1), J(names, J_NAMES, N_FISH_MAX, 0, FISH_NAME_MAX),
    J(body, J_U32, N_FISH_MAX, 0, 0xffffff), J(accent, J_U32, N_FISH_MAX, 0, 0xffffff),
    J(bubble_x, J_FLOAT, 1, 0, TANK_MAX_W), J(ms_seen, J_U32, N_FISH_MAX, 0, UINT32_MAX),
    J(tank_ms_seen, J_U32, 1, 0, UINT32_MAX), J(newborn_p1, J_U8, 1, 0, N_FISH_MAX),
    J(parent_p1, J_U8, N_FISH_MAX * 2, 0, N_FISH_MAX),
    J(drift_acc, J_FLOAT, N_FISH_MAX, 0, 1e12),
    J(light_idle_s, J_U16, 1, 0, 1800), J(light_auto, J_U8, 1, 0, 1),
    J(light_manual_off, J_U8, 1, 0, 1), J(sd_balance, J_I32, 1, 0, INT_MAX),
    J(sd_earned, J_I32, 1, 0, INT_MAX), J(sd_unlocks, J_U32, 1, 0, SD_ITEM_MASK),
    J(sd_paid_fish, J_U32, N_FISH_MAX, 0, UINT32_MAX),
    J(sd_colonies_paid, J_I32, 1, 0, INT_MAX), J(sd_inches_paid, J_I32, 1, 0, INT_MAX),
    J(algae_colonies, J_I32, 1, 0, INT_MAX), J(trim_px, J_FLOAT, 1, 0, 1e12),
    J(snail_x, J_FLOAT, 1, 0, TANK_MAX_W), J(snail_y, J_FLOAT, 1, 0, TANK_MAX_H),
    J(veg_h3, J_FLOAT, VEG_FRONDS_MAX, 0, 1), J(plant_x, J_FLOAT, 1, 0, TANK_MAX_W),
    J(plant_z1, J_U8, 1, 0, 3), J(snail_grazed, J_I32, 1, 0, INT_MAX),
    J(castle_x, J_FLOAT, 1, 0, TANK_MAX_W), J(castle_z1, J_U8, 1, 0, 3),
    J(coral_x, J_FLOAT, 1, 0, TANK_MAX_W), J(coral_z1, J_U8, 1, 0, 3),
    J(coral_rgb, J_U32, 1, 0, 0xffffff), J(coral_growth, J_FLOAT, 1, 0, CORAL_FULL),
    J(cluster_x, J_FLOAT, 1, 0, TANK_MAX_W), J(cluster_z1, J_U8, 1, 0, 3),
    J(cluster_scheme, J_U8, 1, 0, CLUSTER_SCHEME_N - 1), J(fry_seed, J_U16, 1, 0, UINT16_MAX),
    J(cluster_growth, J_FLOAT, 1, 0, CLUSTER_FULL), J(shrimp_n, J_U8, 1, 0, SHRIMP_MAX),
    J(shrimp_food, J_U8, 1, 0, SHRIMP_PER_JOIN), J(fry_age_min, J_U16, 1, 0, UINT16_MAX),
    J(shrimp_cool, J_FLOAT, 1, 0, SHRIMP_COOLDOWN_S), J(shrimp_eaten, J_I32, 1, 0, INT_MAX),
    J(saved_release, J_U32, 1, 0, UINT32_MAX), J(sale_meals_need, J_I32, 1, 0, INT_MAX),
    J(screen_turned, J_U8, 1, 0, 1), J(orient, J_U8, 1, 0, 3), J(autofeed_off, J_U8, 1, 0, 1),
    J(urchin_x, J_FLOAT, 1, 0, TANK_MAX_W), J(urchin_grazed_px, J_FLOAT, 1, 0, 1e12),
    J(light_tip_seen, J_U8, 1, 0, 1), J(tools_locked, J_U8, 1, 0, 1),
    J(fry_sold, J_U8, 1, 0, (1u << FRY_OFFER_N) - 1),
    J(sh_s, J_FLOAT, 1, 0, 1e12), J(sh_hitches, J_I32, 1, 0, INT_MAX),
    J(sh_trust_passed, J_FLOAT, 1, 0, 1e12), J(sh_eaten, J_I32, 1, 0, INT_MAX),
    J(sh_snack_pay, J_I32, 1, 0, INT_MAX), J(sd_snacks_paid, J_I32, 1, 0, INT_MAX),
    J(sh_snack_cool, J_FLOAT, 1, 0, SH_SNACK_COOL_S),
    J(pumpkin_x, J_FLOAT, 1, 0, TANK_MAX_W), J(pumpkin_z1, J_U8, 1, 0, 3),
    J(decor_rank, J_U8, 5, 0, UINT8_MAX),
    J(chest_x, J_FLOAT, 1, 0, TANK_MAX_W), J(chest_z1, J_U8, 1, 0, 3), J(chest_rank, J_U8, 1, 0, UINT8_MAX),
    J(ornament_x, J_FLOAT, 4, 0, TANK_MAX_W), J(ornament_z1, J_U8, 4, 0, 3), J(ornament_rank, J_U8, 4, 0, UINT8_MAX),
    J(sand_style, J_U8, 1, 0, SAND_STYLE_COUNT - 1), J(water_style, J_U8, 1, 0, WATER_STYLE_COUNT - 1),
    J(extra_x, J_FLOAT, 6, 0, TANK_MAX_W), J(extra_z1, J_U8, 6, 0, 3), J(extra_rank, J_U8, 6, 0, UINT8_MAX),
    J(fish_variety, J_U8, N_FISH_MAX, 0, FISH_VARIETY_COUNT - 1), J(fish_pattern, J_U8, N_FISH_MAX, 0, FISH_PATTERN_COUNT - 1),
    {"journal_xp", offsetof(save_t, journal.xp), 1, J_U32, 0, JOURNAL_XP_MAX},
    {"journal_claimed", offsetof(save_t, journal.claimed), 1, J_U32, 0, (1u << JOURNAL_GOAL_N) - 1},
    {"journal_seen", offsetof(save_t, journal.seen), 1, J_U32, 0, (1u << FISH_VARIETY_COUNT) - 1},
    {"journal_grown", offsetof(save_t, journal.grown), 1, J_U32, 0, (1u << FISH_VARIETY_COUNT) - 1},
    {"journal_baseline", offsetof(save_t, journal.baseline), 4, J_U32, 0, INT_MAX},
    {"journal_initialized", offsetof(save_t, journal.initialized), 1, J_U8, 0, 1},
    {"journal_trust", offsetof(save_t, journal.trust), 1, J_U8, 0, 10},
    {"journal_population", offsetof(save_t, journal.population), 1, J_U8, 0, N_FISH_MAX},
    {"journal_theme", offsetof(save_t, journal.theme), 1, J_U8, 0, JOURNAL_THEME_COUNT - 1},
};
#undef J
#define F(f, k, lo, hi) { #f, offsetof(fish_save_t, f), 1, k, lo, hi }
static const json_field_t FISH_FIELDS[] = {
    F(preset, J_U8, 0, 255), F(stage, J_U8, 0, 3), F(size, J_FLOAT, 0, 100),
    F(trust, J_FLOAT, 0, 10), F(bold, J_FLOAT, 0, 1), F(sociable, J_FLOAT, 0, 1),
    F(bold0, J_FLOAT, 0, 1), F(sociable0, J_FLOAT, 0, 1),
    F(hunger, J_FLOAT, 0, 10), F(energy, J_FLOAT, 0, 10), F(stress, J_FLOAT, 0, 10), F(curiosity, J_FLOAT, 0, 10),
    F(age_s, J_FLOAT, 0, 1e12), F(rest_dx, J_FLOAT, -1000, 1000), F(rest_dy, J_FLOAT, -1000, 1000),
    F(eaten, J_I32, 0, INT_MAX), F(eaten_player, J_I32, 0, INT_MAX), F(ms_bits, J_U32, 0, UINT32_MAX),
};
#undef F
#define C(f, k, n, lo, hi) { #f, offsetof(companion_state_t, f), n, k, lo, hi }
static const json_field_t COMPANION_FIELDS[] = {
    C(powered_s, J_U32, 1, 0, UINT32_MAX), C(discoveries, J_U32, 1, 0, 0xfff),
    C(collections, J_U32, 1, 0, 7), C(adventure_s, J_U32, 1, 0, 43200),
    C(portrait_body, J_U32, 1, 0, 0xffffff), C(portrait_accent, J_U32, 1, 0, 0xffffff),
    C(active, J_HEX, sizeof(((companion_state_t *)0)->active), 0, 255),
    C(bank, J_HEX, sizeof(((companion_state_t *)0)->bank), 0, 255),
    C(game_best, J_U16, 6, 0, UINT16_MAX), C(game_rounds, J_U16, 6, 0, UINT16_MAX),
    C(portrait_name, J_NAMES, 1, 0, FISH_NAME_MAX), C(game_medals, J_U8, 6, 0, 3),
    C(game_challenges, J_U8, 1, 0, 63), C(featured, J_U8, 1, 0, 5),
    C(version, J_U8, 1, 1, COMPANION_VERSION), C(cozy, J_U8, 1, 0, 1),
    C(captions, J_U8, 1, 0, 1), C(lively, J_U8, 1, 0, 1),
    C(adventure, J_U8, 1, 0, 3), C(adventure_done, J_U8, 1, 0, 7),
    C(adventure_claimed, J_U8, 1, 0, 7), C(cosmetic, J_U8, 1, 0, 3),
    C(last_discovery, J_U8, 1, 0, COMPANION_DISCOVERY_COUNT),
    C(adventure_choice, J_U8, 1, 0, 1), C(adventure_routes, J_U8, 1, 0, 7),
    C(decor, J_U8, 1, 0, 1),
    C(portrait_variety, J_U8, 1, 0, FISH_VARIETY_COUNT - 1), C(portrait_pattern, J_U8, 1, 0, FISH_PATTERN_COUNT - 1),
    C(traveler, J_HEX, sizeof(bank_fish_t), 0, 255),
    C(traveler_bond, J_HEX, sizeof(companion_bond_t), 0, 255),
    C(away, J_U8, 1, 0, 1), C(adventure_clock, J_I64, 1, 0, 4102444800LL),
    C(portrait_wearable, J_U8, 1, 0, WEAR_COUNT - 1),
    C(portrait_colors, J_U8, 1, 0, 255), C(creature_colors, J_U8, CREATURE_COUNT, 0, 255),
    C(display_compact, J_U8, 1, 0, 1),
};
#undef C
#define P(f, k, n, lo, hi) { #f, offsetof(tank_polish_t, f), n, k, lo, hi }
static const json_field_t POLISH_FIELDS[] = {
    P(next_id, J_U32, 1, 0, UINT32_MAX), P(viewed_s, J_U32, 1, 0, UINT32_MAX),
    P(next_visit_s, J_U32, 1, 0, UINT32_MAX), P(visits, J_U32, 1, 0, UINT32_MAX),
    P(backups, J_U32, 1, 0, UINT32_MAX),
    P(founded_unix, J_I64, 1, 0, 4102444800LL), P(backup_unix, J_I64, 1, 0, 4102444800LL),
    P(active, J_HEX, sizeof(((tank_polish_t *)0)->active), 0, 255),
    P(bank, J_HEX, sizeof(((tank_polish_t *)0)->bank), 0, 255),
    P(traveler, J_HEX, sizeof(fish_life_t), 0, 255),
    P(recap, J_HEX, sizeof(((tank_polish_t *)0)->recap), 0, 255),
    P(decor_owned, J_U32, 1, 0, UINT32_MAX), P(decor_since, J_U32, 38, 0, UINT32_MAX),
    P(helper_meals, J_U32, 1, 0, UINT32_MAX), P(helper_trim, J_U32, 1, 0, UINT32_MAX),
    P(version, J_U8, 1, 0, 1), P(recap_count, J_U8, 1, 0, TANK_RECAP_CAP),
    P(subtitle, J_U8, 1, 0, 4), P(emblem, J_U8, 1, 0, 3), P(scenery_unlocks, J_U8, 1, 0, 63),
    P(tank_name, J_HEX, 16, 0, 255),
};
#undef P
#define S(f, k, n, lo, hi) { #f, offsetof(tank_story_t, f), n, k, lo, hi }
static const json_field_t STORY_FIELDS[] = {
    S(version, J_U8, 1, 0, 1), S(flags, J_U8, 1, 0, 3), S(last_species, J_U8, 1, 0, VISITOR_SPECIES - 1),
    S(first_seen_s, J_U32, VISITOR_SPECIES, 0, UINT32_MAX), S(seen, J_U16, VISITOR_SPECIES, 0, UINT16_MAX),
    S(births, J_U16, 1, 0, UINT16_MAX),
    S(active, J_HEX, sizeof(((tank_story_t *)0)->active), 0, 255),
    S(bank, J_HEX, sizeof(((tank_story_t *)0)->bank), 0, 255),
    S(traveler, J_HEX, sizeof(fish_lineage_t), 0, 255),
    S(names, J_HEX, sizeof(((tank_story_t *)0)->names), 0, 255),
};
#undef S
#define G(f, k, n, lo, hi) { #f, offsetof(game_records_t, f), n, k, lo, hi }
static const json_field_t GAME_FIELDS[] = {
    G(best, J_U16, 6, 0, UINT16_MAX), G(rounds, J_U16, 6, 0, UINT16_MAX),
    G(medals, J_U8, 6, 0, 3), G(version, J_U8, 1, 0, 1)
};
#undef G
#define R(f, k, n, lo, hi) { #f, offsetof(tank_rpg_t, f), n, k, lo, hi }
static const json_field_t RPG_FIELDS[] = {
    R(participant, J_U32, 1, 0, UINT32_MAX), R(contributors, J_U16, 6, 0, UINT16_MAX),
    R(version, J_U8, 1, 0, 1), R(chapter, J_U8, 1, 0, 6), R(step, J_U8, 1, 0, 2),
    R(skills, J_U8, 3, 0, 18), R(routes, J_U8, 1, 0, 63), R(outcome, J_U8, 1, 0, 6)
};
#undef R
#define CREAT(f, k, hi) { #f, offsetof(creative_state_t, f), 1, k, 0, hi }
static const json_field_t CREATIVE_FIELDS[] = {
    CREAT(marker, J_U32, UINT32_MAX), CREAT(version, J_U8, 1),
    CREAT(enabled, J_U8, 1), CREAT(reserved, J_U16, 0)
};
#undef CREAT
#define PROF(f, k, n, hi) { #f, offsetof(profile_state_t, f), n, k, 0, hi }
static const json_field_t PROFILE_FIELDS[] = {
    PROF(marker, J_U32, 1, UINT32_MAX), PROF(version, J_U8, 1, 1),
    PROF(mode, J_U8, 1, GAME_MODE_HARD), PROF(chosen, J_U8, 1, 1),
    PROF(reserved, J_U8, 1, 15), PROF(starvation_s, J_FLOAT, N_FISH_MAX, HARD_STARVATION_S),
    PROF(owned_items, J_HEX, 8, 255), PROF(paid_items, J_HEX, 8, 255)
};
#undef PROF
#define CARE(f, k, hi) { #f, offsetof(care_state_t, f), 1, k, 0, hi }
static const json_field_t CARE_FIELDS[] = {
    CARE(day, J_I64, 47482), CARE(elapsed_s, J_U32, 86399),
    CARE(viewed_s, J_U16, CARE_VIEW_SECONDS), CARE(calm_s, J_U16, CARE_CALM_SECONDS),
    CARE(ready, J_U8, 7), CARE(claimed, J_U8, 7),
    CARE(game_paid, J_U8, GAME_DAILY_TOTAL), CARE(version, J_U8, 1), CARE(fallback, J_U8, 1)
};
#undef CARE
#define INV(f, k, n, hi) { #f, offsetof(inventory_state_t, f), n, k, 0, hi }
static const json_field_t INVENTORY_FIELDS[] = {
    INV(version, J_U8, 1, 1),
    INV(stored, J_HEX, DECOR_SLOTS, 255),
    INV(paid, J_HEX, DECOR_SLOTS, 255),
    INV(extra, J_HEX, sizeof(((inventory_state_t *)0)->extra), 255)
};
#undef INV
#define MUS(f, k, hi) { #f, offsetof(mussel_state_t, f), 1, k, 0, hi }
static const json_field_t MUSSEL_FIELDS[] = {
    MUS(seconds, J_U16, MUSSEL_SECONDS - 1), MUS(owned, J_U8, 255),
    MUS(bank, J_U8, MUSSEL_DAILY_LIMIT), MUS(generated, J_U8, MUSSEL_DAILY_LIMIT),
    MUS(version, J_U8, 2)
};
#undef MUS
#define GI(f, n, hi) { #f, offsetof(game_income_state_t, f), n, J_U8, 0, hi }
static const json_field_t GAME_INCOME_FIELDS[] = {
    GI(paid, GAME_INCOME_COUNT, CARE_GAME_LIMIT), GI(version, 1, 1), GI(legacy, 1, CARE_GAME_LIMIT)
};
#undef GI
#define B(f, k, n, lo, hi) { #f, offsetof(bank_fish_t, f), n, k, lo, hi }
static const json_field_t BANK_FIELDS[] = {
    B(name, J_NAMES, 1, 0, 0), B(body, J_U32, 1, 1, 0xffffff), B(accent, J_U32, 1, 1, 0xffffff),
    B(paid, J_U32, 1, 0, UINT32_MAX), B(seen, J_U32, 1, 0, UINT32_MAX),
    B(drift, J_FLOAT, 1, 0, 1e12),
    {"appearance", offsetof(bank_fish_t, fish.pad), 1, J_U16, 0, 65535}
};
#undef B
#define FIELD_COUNT(a) (sizeof(a) / sizeof((a)[0]))
static size_t json_stride(json_kind_t kind) {
    return kind == J_U16 ? 2 : kind == J_U32 || kind == J_I32 || kind == J_FLOAT ? 4 : kind == J_I64 ? 8 : 1;
}
static double json_number(const void *data, json_kind_t kind) {
    switch (kind) {
        case J_U8: return *(const uint8_t *)data;
        case J_U16: return *(const uint16_t *)data;
        case J_U32: return *(const uint32_t *)data;
        case J_I32: return *(const int32_t *)data;
        case J_I64: return (double)*(const int64_t *)data;
        case J_FLOAT: return *(const float *)data;
        case J_BOOL: return *(const bool *)data;
        default: return 0;
    }
}
static bool bank_valid(const bank_fish_t *b) {
    if (!b->name[0] || !memchr(b->name, 0, sizeof b->name) || b->fish.preset >= tank_roster_count() ||
        bank_variety(b) >= FISH_VARIETY_COUNT || bank_pattern(b) >= FISH_PATTERN_COUNT ||
        (b->seen & ~b->fish.ms_bits)) return false;
    for (const char *s = b->name; *s; s++) if ((unsigned char)*s < 32 || (unsigned char)*s > 126) return false;
    for (size_t i = 0; i < FIELD_COUNT(FISH_FIELDS); i++) {
        const json_field_t *f = &FISH_FIELDS[i];
        double n = json_number((const uint8_t *)&b->fish + f->offset, f->kind);
        if (!isfinite(n) || n < f->min || n > f->max) return false;
    }
    return b->body > 0 && b->body <= 0xffffff && b->accent > 0 && b->accent <= 0xffffff &&
           isfinite(b->drift) && b->drift >= 0 && b->drift <= 1e12;
}
static void json_store(void *data, json_kind_t kind, double n) {
    switch (kind) {
        case J_U8: *(uint8_t *)data = (uint8_t)n; break;
        case J_U16: *(uint16_t *)data = (uint16_t)n; break;
        case J_U32: *(uint32_t *)data = (uint32_t)n; break;
        case J_I32: *(int32_t *)data = (int32_t)n; break;
        case J_I64: *(int64_t *)data = (int64_t)n; break;
        case J_FLOAT: *(float *)data = (float)n; break;
        case J_BOOL: *(bool *)data = n != 0; break;
        default: break;
    }
}
static cJSON *json_fields(const void *data, const json_field_t *fields, size_t count) {
    cJSON *obj = cJSON_CreateObject();
    if (!obj) return NULL;
    for (size_t k = 0; k < count; k++) {
        const json_field_t *f = &fields[k];
        const uint8_t *p = (const uint8_t *)data + f->offset;
        cJSON *value = NULL;
        if (f->kind == J_HEX) {
            char *hex = malloc(f->count * 2 + 1);
            if (!hex) goto fail;
            for (size_t i = 0; i < f->count; i++) snprintf(hex + i * 2, 3, "%02x", p[i]);
            value = cJSON_CreateString(hex); free(hex);
        } else if (f->kind == J_NAMES || f->count > 1) {
            value = cJSON_CreateArray();
            if (!value) goto fail;
            for (size_t i = 0; i < f->count; i++) {
                cJSON *item = f->kind == J_NAMES ? cJSON_CreateString((const char *)p + i * (FISH_NAME_MAX + 1))
                    : cJSON_CreateNumber(json_number(p + i * json_stride(f->kind), f->kind));
                if (!item || !cJSON_AddItemToArray(value, item)) {
                    cJSON_Delete(item); cJSON_Delete(value); goto fail;
                }
            }
        } else if (f->kind == J_BOOL) value = cJSON_CreateBool(json_number(p, f->kind) != 0);
        else value = cJSON_CreateNumber(json_number(p, f->kind));
        if (!value || !cJSON_AddItemToObject(obj, f->name, value)) { cJSON_Delete(value); goto fail; }
    }
    return obj;
fail:
    cJSON_Delete(obj); return NULL;
}
static bool json_append(char **text, size_t *used, const char *part) {
    size_t n = strlen(part);
    if (n > 32768 - *used) return false;
    char *next = realloc(*text, *used + n + 1);
    if (!next) return false;
    memcpy(next + *used, part, n + 1);
    *text = next; *used += n;
    return true;
}
static bool json_append_value(char **text, size_t *used, cJSON *value) {
    if (!value) return false;
    char *part = cJSON_PrintUnformatted(value);
    cJSON_Delete(value);
    if (!part) return false;
    bool ok = json_append(text, used, part);
    free(part);
    return ok;
}
char *progression_backup_json(const tank_t *t) {
    save_t sv; capture_save(t, &sv);
    char header[128], *text = NULL;
    size_t used = 0;
    snprintf(header, sizeof header, "{\"format\":\"pocket-tank\",\"schema\":18,\"board\":\"cyd2432s028\",\"width\":%d,\"height\":%d,\"tank\":", TANK_W, TANK_H);
    if (!json_append(&text, &used, header)) goto fail;
    cJSON *tank = json_fields(&sv, SAVE_FIELDS, FIELD_COUNT(SAVE_FIELDS));
    if (!tank) goto fail;
    if (!cJSON_AddNumberToObject(tank, "stat_upgrades", sv.aquariums.stat_upgrades) ||
        !cJSON_AddNumberToObject(tank, "clock_theme", sv.aquariums.clock_theme) ||
        !cJSON_AddNumberToObject(tank, "clock_flags", sv.aquariums.clock_flags) ||
        !cJSON_AddNumberToObject(tank, "clock_utc_quarters", sv.aquariums.clock_utc_quarters) ||
        !cJSON_AddNumberToObject(tank, "clock_layout", sv.aquariums.clock_layout) ||
        !cJSON_AddNumberToObject(tank, "clock_delay", sv.aquariums.clock_delay & ~CLOCK_SECONDARY_MASK) ||
        !cJSON_AddNumberToObject(tank, "clock_secondary", sv.aquariums.clock_delay >> CLOCK_SECONDARY_SHIFT)) {
        cJSON_Delete(tank); goto fail;
    }
    /* Serialize one section at a time so SD backup fits beside weather's Wi-Fi buffers. */
    if (!json_append_value(&text, &used, tank) || !json_append(&text, &used, ",\"companion\":") ||
        !json_append_value(&text, &used, json_fields(&sv.companion, COMPANION_FIELDS, FIELD_COUNT(COMPANION_FIELDS))) ||
        !json_append(&text, &used, ",\"polish\":") ||
        !json_append_value(&text, &used, json_fields(&sv.polish, POLISH_FIELDS, FIELD_COUNT(POLISH_FIELDS))) ||
        !json_append(&text, &used, ",\"story\":") ||
        !json_append_value(&text, &used, json_fields(&sv.story, STORY_FIELDS, FIELD_COUNT(STORY_FIELDS))) ||
        !json_append(&text, &used, ",\"games\":") ||
        !json_append_value(&text, &used, json_fields(&sv.games, GAME_FIELDS, FIELD_COUNT(GAME_FIELDS))) ||
        !json_append(&text, &used, ",\"rpg\":") ||
        !json_append_value(&text, &used, json_fields(&sv.rpg, RPG_FIELDS, FIELD_COUNT(RPG_FIELDS))) ||
        !json_append(&text, &used, ",\"creative\":") ||
        !json_append_value(&text, &used, json_fields(&sv.creative, CREATIVE_FIELDS, FIELD_COUNT(CREATIVE_FIELDS))) ||
        !json_append(&text, &used, ",\"games_extra\":") ||
        !json_append_value(&text, &used, json_fields(&sv.games_extra, GAME_FIELDS, FIELD_COUNT(GAME_FIELDS))) ||
        !json_append(&text, &used, ",\"profile\":") ||
        !json_append_value(&text, &used, json_fields(&sv.profile, PROFILE_FIELDS, FIELD_COUNT(PROFILE_FIELDS))) ||
        !json_append(&text, &used, ",\"care\":") ||
        !json_append_value(&text, &used, json_fields(&sv.care, CARE_FIELDS, FIELD_COUNT(CARE_FIELDS))) ||
        !json_append(&text, &used, ",\"inventory\":") ||
        !json_append_value(&text, &used, json_fields(&sv.inventory, INVENTORY_FIELDS, FIELD_COUNT(INVENTORY_FIELDS))) ||
        !json_append(&text, &used, ",\"mussel\":") ||
        !json_append_value(&text, &used, json_fields(&sv.mussel, MUSSEL_FIELDS, FIELD_COUNT(MUSSEL_FIELDS))) ||
        !json_append(&text, &used, ",\"game_income\":") ||
        !json_append_value(&text, &used, json_fields(&sv.game_income, GAME_INCOME_FIELDS, FIELD_COUNT(GAME_INCOME_FIELDS))) ||
        !json_append(&text, &used, ",\"fish\":[")) goto fail;
    for (int i = 0; i < N_FISH_MAX; i++) {
        if ((i && !json_append(&text, &used, ",")) ||
            !json_append_value(&text, &used, json_fields(&sv.fish[i], FISH_FIELDS, FIELD_COUNT(FISH_FIELDS)))) goto fail;
    }
    if (!json_append(&text, &used, "],\"bank\":[")) goto fail;
    for (int i = 0; i < sv.bank_count; i++) {
        if (i && !json_append(&text, &used, ",")) goto fail;
        cJSON *b = json_fields(&sv.bank[i], BANK_FIELDS, FIELD_COUNT(BANK_FIELDS));
        if (!b) goto fail;
        cJSON *f = json_fields(&sv.bank[i].fish, FISH_FIELDS, FIELD_COUNT(FISH_FIELDS));
        if (!f || !cJSON_AddItemToObject(b, "fish", f)) { cJSON_Delete(f); cJSON_Delete(b); goto fail; }
        if (!json_append_value(&text, &used, b)) goto fail;
    }
    if (!json_append(&text, &used, "]}")) goto fail;
    return text;
fail:
    free(text); return NULL;
}
static int json_hex_digit(char c) {
    return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
}
static bool json_read_fields(void *data, const cJSON *obj, const json_field_t *fields, size_t count, char *error, size_t size) {
    if (!cJSON_IsObject(obj) || cJSON_GetArraySize(obj) != (int)count + (fields == BANK_FIELDS ? 1 : 0)) {
        snprintf(error, size, "Missing, extra or duplicate fields"); return false;
    }
    for (size_t k = 0; k < count; k++) {
        const json_field_t *f = &fields[k];
        uint8_t *p = (uint8_t *)data + f->offset;
        const cJSON *value = NULL, *entry;
        int found = 0;
        cJSON_ArrayForEach(entry, obj) if (entry->string && !strcmp(entry->string, f->name)) { value = entry; found++; }
        if (found != 1) goto invalid;
        if (f->kind == J_HEX) {
            if (!cJSON_IsString(value) || strlen(value->valuestring) != f->count * 2) goto invalid;
            for (size_t i = 0; i < f->count; i++) {
                int a = json_hex_digit(value->valuestring[i * 2]), b = json_hex_digit(value->valuestring[i * 2 + 1]);
                if (a < 0 || b < 0) goto invalid;
                p[i] = (uint8_t)(a * 16 + b);
            }
            continue;
        }
        if ((f->count > 1 || f->kind == J_NAMES) && (!cJSON_IsArray(value) || cJSON_GetArraySize(value) != (int)f->count)) goto invalid;
        for (size_t i = 0; i < f->count; i++) {
            const cJSON *v = f->count > 1 || f->kind == J_NAMES ? cJSON_GetArrayItem(value, (int)i) : value;
            if (f->kind == J_NAMES) {
                if (!cJSON_IsString(v) || strlen(v->valuestring) > FISH_NAME_MAX) goto invalid;
                for (const char *s = v->valuestring; *s; s++) if ((unsigned char)*s < 32 || (unsigned char)*s > 126) goto invalid;
                strcpy((char *)p + i * (FISH_NAME_MAX + 1), v->valuestring);
                continue;
            }
            double n;
            if (f->kind == J_BOOL) { if (!cJSON_IsBool(v)) goto invalid; n = cJSON_IsTrue(v); }
            else { if (!cJSON_IsNumber(v)) goto invalid; n = v->valuedouble; }
            if (!isfinite(n) || n < f->min || n > f->max || (f->kind != J_FLOAT && n != floor(n))) goto invalid;
            json_store(p + i * json_stride(f->kind), f->kind, n);
        }
        continue;
invalid:
        snprintf(error, size, "Invalid field: %s", f->name); return false;
    }
    return true;
}
static const char *json_space(const char *p, const char *end) {
    while (p < end && (*p == ' ' || *p == '\r' || *p == '\n' || *p == '\t')) p++;
    return p;
}
static bool json_bank_record(bank_fish_t *b, const cJSON *entry, char *error, size_t size) {
    if (!json_read_fields(b, entry, BANK_FIELDS, FIELD_COUNT(BANK_FIELDS), error, size) ||
        !json_read_fields(&b->fish, cJSON_GetObjectItemCaseSensitive(entry, "fish"),
                          FISH_FIELDS, FIELD_COUNT(FISH_FIELDS), error, size)) return false;
    if (!bank_valid(b)) { snprintf(error, size, "Invalid stored fish"); return false; }
    return true;
}
static cJSON *json_snapshot(const char *json, size_t len, save_t *sv, char *error, size_t size) {
    const char *limit = json + len, *p = json_space(json, limit);
    cJSON *root = NULL, *key = NULL, *value = NULL;
    if (p == limit || *p++ != '{' || !(root = cJSON_CreateObject())) goto fail;
    for (;;) {
        p = json_space(p, limit);
        const char *end = NULL;
        key = cJSON_ParseWithLengthOpts(p, (size_t)(limit - p), &end, false);
        if (!cJSON_IsString(key) || !end) goto fail;
        p = json_space(end, limit);
        if (p == limit || *p++ != ':') goto fail;
        p = json_space(p, limit);
        if (!strcmp(key->valuestring, "bank")) {
            /* Release each fish's tree before parsing the next on the no-PSRAM CYD. */
            if (p == limit || *p++ != '[' || !(value = cJSON_CreateArray())) goto fail;
            sv->bank_count = 0;
            p = json_space(p, limit);
            if (p < limit && *p == ']') p++;
            else for (;;) {
                if (sv->bank_count >= FISH_BANK_CAP) { snprintf(error, size, "Fish bank exceeds capacity"); goto fail; }
                end = NULL;
                cJSON *entry = cJSON_ParseWithLengthOpts(p, (size_t)(limit - p), &end, false);
                bool valid = entry && end && json_bank_record(&sv->bank[sv->bank_count], entry, error, size);
                cJSON_Delete(entry);
                if (!valid) goto fail;
                sv->bank_count++;
                p = json_space(end, limit);
                if (p < limit && *p == ']') { p++; break; }
                if (p == limit || *p++ != ',') goto fail;
                p = json_space(p, limit);
            }
        } else {
            end = NULL;
            value = cJSON_ParseWithLengthOpts(p, (size_t)(limit - p), &end, false);
            if (!value || !end) goto fail;
            p = end;
        }
        if (!cJSON_AddItemToObject(root, key->valuestring, value)) goto fail;
        value = NULL; cJSON_Delete(key); key = NULL;
        p = json_space(p, limit);
        if (p < limit && *p == '}') {
            if (json_space(p + 1, limit) != limit) goto fail;
            return root;
        }
        if (p == limit || *p++ != ',') goto fail;
    }
fail:
    cJSON_Delete(value); cJSON_Delete(key); cJSON_Delete(root); return NULL;
}
bool progression_backup_preview(const char *json, size_t len, char name[16], uint32_t *release) {
    char error[96];
    if (!progression_restore_json(json, len, false, error, sizeof error)) {
        fprintf(stderr, "Backup preview rejected: %s\n", error); return false;
    }
    save_t *sv = calloc(1, sizeof *sv);
    if (!sv) { fprintf(stderr, "Not enough memory for backup preview\n"); return false; }
    cJSON *root = json_snapshot(json, len, sv, error, sizeof error);
    if (!root) { free(sv); fprintf(stderr, "Backup preview parse failed: %s\n", error); return false; }
    const cJSON *tank = cJSON_GetObjectItemCaseSensitive(root, "tank");
    const cJSON *version = cJSON_GetObjectItemCaseSensitive(tank, "saved_release");
    const cJSON *polish = cJSON_GetObjectItemCaseSensitive(root, "polish");
    bool ok = !polish || json_read_fields(&sv->polish, polish, POLISH_FIELDS, FIELD_COUNT(POLISH_FIELDS), error, sizeof error);
    if (ok) {
        snprintf(name, 16, "%s", sv->polish.tank_name[0] ? sv->polish.tank_name : "OLDER BACKUP");
        *release = version ? (uint32_t)version->valuedouble : 0;
    } else fprintf(stderr, "Backup preview metadata rejected: %s\n", error);
    cJSON_Delete(root); free(sv); return ok;
}
static const char *restore_fish_identities(save_t *to, save_t *current);
bool progression_restore_json(const char *json, size_t len, bool commit, char *error, size_t error_size) {
    /* Bound parser stack and heap before allowing cJSON to allocate anything. */
    int depth = 0; bool quoted = false, escape = false;
    if (!json || !len || len > 32768 || memchr(json, 0, len)) { snprintf(error, error_size, "Invalid JSON size"); return false; }
    for (size_t i = 0; i < len; i++) {
        char c = json[i];
        if (quoted) { if (escape) escape = false; else if (c == '\\') escape = true; else if (c == '"') quoted = false; }
        else if (c == '"') quoted = true;
        else if (c == '{' || c == '[') { if (++depth > 8) { snprintf(error, error_size, "JSON too deeply nested"); return false; } }
        else if (c == '}' || c == ']') depth--;
    }
    bool ok = false;
    save_t sv; memset(&sv, 0, sizeof sv); sv.magic = SAVE_MAGIC;
    snprintf(error, error_size, "Invalid JSON or incompatible schema/board");
    cJSON *root = json_snapshot(json, len, &sv, error, error_size);
    if (!root) goto done;
    const cJSON *format = cJSON_GetObjectItemCaseSensitive(root, "format");
    const cJSON *schema = cJSON_GetObjectItemCaseSensitive(root, "schema");
    const cJSON *board = cJSON_GetObjectItemCaseSensitive(root, "board");
    const cJSON *width = cJSON_GetObjectItemCaseSensitive(root, "width");
    const cJSON *height = cJSON_GetObjectItemCaseSensitive(root, "height");
    const cJSON *fish = cJSON_GetObjectItemCaseSensitive(root, "fish");
    if (!cJSON_IsString(format) || strcmp(format->valuestring, "pocket-tank")
        || !cJSON_IsNumber(schema) || schema->valuedouble < 1 || schema->valuedouble > 18 ||
           schema->valuedouble != floor(schema->valuedouble)
        || cJSON_GetArraySize(root) != (schema->valuedouble >= 18 ? 20 : schema->valuedouble >= 17 ? 17 : schema->valuedouble >= 16 ? 16 : schema->valuedouble >= 15 ? 14 : schema->valuedouble >= 14 ? 13 : schema->valuedouble >= 13 ? 12 : schema->valuedouble >= 12 ? 11 : schema->valuedouble >= 11 ? 10 : schema->valuedouble >= 6 ? 9 : schema->valuedouble == 5 ? 8 : 7)
        || !cJSON_IsString(board) || strcmp(board->valuestring, "cyd2432s028")
        || !cJSON_IsNumber(width) || (width->valuedouble != 240 && width->valuedouble != 320)
        || !cJSON_IsNumber(height) || height->valuedouble * 4 != width->valuedouble * 3
        || !cJSON_IsArray(fish) || cJSON_GetArraySize(fish) != N_FISH_MAX) goto done;
    size_t fields = FIELD_COUNT(SAVE_FIELDS) - (schema->valuedouble < 4 ? 9 : 0) - (schema->valuedouble < 3 ? 5 : 0) - (schema->valuedouble == 1 ? 5 : 0);
    cJSON *tank_fields = cJSON_GetObjectItemCaseSensitive(root, "tank");
    cJSON *stat = cJSON_GetObjectItemCaseSensitive(tank_fields, "stat_upgrades");
    if (stat && (!cJSON_IsNumber(stat) || !isfinite(stat->valuedouble) ||
                 stat->valuedouble < 0 || stat->valuedouble > STAT_MASK || floor(stat->valuedouble) != stat->valuedouble)) {
        snprintf(error, error_size, "Invalid stat upgrades"); goto done;
    }
    uint8_t restored_stats = stat ? (uint8_t)stat->valuedouble : 0;
    if (stat) cJSON_Delete(cJSON_DetachItemViaPointer(tank_fields, stat));
    int clock_values[6] = {0};
    static const char *const clock_keys[] = {"clock_theme", "clock_flags", "clock_utc_quarters", "clock_layout", "clock_delay", "clock_secondary"};
    static const int clock_min[] = {0, 0, -48, 0, 0, 0}, clock_max[] = {CLOCK_THEME_COUNT - 1, CLOCK_FLAGS_MASK, 56, CLOCK_LAYOUT_MAX, 31, 7};
    for (int i = 0; i < 6; i++) {
        cJSON *option = cJSON_GetObjectItemCaseSensitive(tank_fields, clock_keys[i]);
        if (!option) continue;
        if (!cJSON_IsNumber(option) || !isfinite(option->valuedouble) || floor(option->valuedouble) != option->valuedouble ||
            option->valuedouble < clock_min[i] || option->valuedouble > clock_max[i]) {
            snprintf(error, error_size, "Invalid clock option: %s", clock_keys[i]); goto done;
        }
        clock_values[i] = (int)option->valuedouble;
        cJSON_Delete(cJSON_DetachItemViaPointer(tank_fields, option));
    }
    if (!clock_layout_valid((uint8_t)clock_values[3]) || !clock_delay_valid((uint8_t)clock_values[4])) {
        snprintf(error, error_size, "Invalid clock size or text layout"); goto done;
    }
    if (schema->valuedouble >= 15 &&
        (!json_read_fields(&sv.creative, cJSON_GetObjectItemCaseSensitive(root, "creative"),
                           CREATIVE_FIELDS, FIELD_COUNT(CREATIVE_FIELDS), error, error_size) ||
         !creative_valid(&sv.creative))) {
        snprintf(error, error_size, "Invalid Creative mode state"); goto done;
    }
    if (!json_read_fields(&sv, tank_fields, SAVE_FIELDS, fields, error, error_size)) goto done;
    if (schema->valuedouble >= 16 &&
        (!json_read_fields(&sv.profile, cJSON_GetObjectItemCaseSensitive(root, "profile"),
                          PROFILE_FIELDS, FIELD_COUNT(PROFILE_FIELDS), error, error_size) ||
         !json_read_fields(&sv.games_extra, cJSON_GetObjectItemCaseSensitive(root, "games_extra"),
                          GAME_FIELDS, FIELD_COUNT(GAME_FIELDS), error, error_size) ||
         !extra_games_valid(&sv.games_extra))) {
        snprintf(error, error_size, "Invalid game mode or extra game records"); goto done;
    }
    if (!migrate_profile(&sv)) {
        snprintf(error, error_size, "Invalid profile; legacy Creative backups cannot migrate"); goto done;
    }
    if (schema->valuedouble >= 18 &&
        (!json_read_fields(&sv.inventory, cJSON_GetObjectItemCaseSensitive(root, "inventory"),
                           INVENTORY_FIELDS, FIELD_COUNT(INVENTORY_FIELDS), error, error_size) ||
         !inventory_valid(&sv.inventory))) {
        snprintf(error, error_size, "Invalid decoration inventory"); goto done;
    }
    if (schema->valuedouble >= 18 &&
        (!json_read_fields(&sv.mussel, cJSON_GetObjectItemCaseSensitive(root, "mussel"),
                           MUSSEL_FIELDS, FIELD_COUNT(MUSSEL_FIELDS), error, error_size) ||
         !mussel_valid(&sv.mussel))) {
        snprintf(error, error_size, "Invalid pearl mussel"); goto done;
    }
    if (schema->valuedouble >= 17 &&
        (!json_read_fields(&sv.care, cJSON_GetObjectItemCaseSensitive(root, "care"),
                           CARE_FIELDS, FIELD_COUNT(CARE_FIELDS), error, error_size) ||
         !care_valid(&sv.care))) {
        snprintf(error, error_size, "Invalid daily care state"); goto done;
    }
    if ((schema->valuedouble >= 18 &&
         !json_read_fields(&sv.game_income, cJSON_GetObjectItemCaseSensitive(root, "game_income"),
                           GAME_INCOME_FIELDS, FIELD_COUNT(GAME_INCOME_FIELDS), error, error_size)) ||
        !game_income_valid(&sv.game_income, sv.care.game_paid)) {
        snprintf(error, error_size, "Invalid game income"); goto done;
    }
    if (schema->valuedouble == 1 && sv.sd_unlocks >= (1u << 10)) {
        snprintf(error, error_size, "Unknown items in schema 1 backup"); goto done;
    }
    if (schema->valuedouble == 2 && sv.sd_unlocks >= (1u << 16)) {
        snprintf(error, error_size, "Unknown items in schema 2 backup"); goto done;
    }
    if ((sv.sand_style && !(sv.sd_unlocks & SD_ITEM_SAND)) || (sv.water_style && !(sv.sd_unlocks & SD_ITEM_LIGHTS))) {
        snprintf(error, error_size, "Palette selected without its upgrade"); goto done;
    }
    if ((sv.journal.grown & ~sv.journal.seen) ||
        (!sv.creative.enabled && sv.journal.xp < journal_level_xp(JOURNAL_THEME_LEVELS[sv.journal.theme]))) {
        snprintf(error, error_size, "Invalid journal album/theme"); goto done;
    }
    for (int i = 0; i < N_FISH_MAX; i++) {
        if (!json_read_fields(&sv.fish[i], cJSON_GetArrayItem(fish, i), FISH_FIELDS, FIELD_COUNT(FISH_FIELDS), error, error_size)) goto done;
        if (i < sv.n_fish && sv.fish[i].preset >= tank_roster_count()) { snprintf(error, error_size, "Invalid fish preset"); goto done; }
    }
    if (schema->valuedouble >= 6) {
        memset(&sv.companion, 0, sizeof sv.companion);
        if (!json_read_fields(&sv.companion, cJSON_GetObjectItemCaseSensitive(root, "companion"),
                              COMPANION_FIELDS, FIELD_COUNT(COMPANION_FIELDS) - (schema->valuedouble == 6 ? 8 : schema->valuedouble == 7 ? 4 : schema->valuedouble == 8 ? 3 : schema->valuedouble == 9 ? 1 : 0), error, error_size) ||
            (schema->valuedouble == 6 && sv.companion.version > 1) ||
            (schema->valuedouble == 7 && sv.companion.version > 2) || !companion_valid_mode(&sv.companion, sv.creative.enabled != 0) ||
            (schema->valuedouble == 8 && sv.companion.version > 3) ||
            (schema->valuedouble == 9 && sv.companion.version > 4) ||
            (sv.companion.away && !bank_valid(&sv.companion.traveler))) {
            snprintf(error, error_size, "Invalid companion state"); goto done;
        }
    }
    if ((sv.profile.mode != GAME_MODE_HARD && sv.n_fish < 2 && !sv.companion.away) ||
        sv.n_fish + sv.companion.away > N_FISH_MAX) {
        snprintf(error, error_size, "Invalid aquarium population / adventure reservation"); goto done;
    }
    if (sv.creative.enabled && sv.n_fish + sv.companion.away > CREATIVE_FISH_CAP) {
        snprintf(error, error_size, "Creative aquariums allow at most 5 fish including travelers"); goto done;
    }
    if (schema->valuedouble >= 11 &&
        (!json_read_fields(&sv.polish, cJSON_GetObjectItemCaseSensitive(root, "polish"),
                           POLISH_FIELDS, FIELD_COUNT(POLISH_FIELDS), error, error_size) ||
         !companion_polish_valid(&sv.polish, sv.n_fish, sv.bank_count, sv.companion.away))) {
        snprintf(error, error_size, "Invalid aquarium memories"); goto done;
    }
    if (schema->valuedouble >= 12 &&
        (!json_read_fields(&sv.story, cJSON_GetObjectItemCaseSensitive(root, "story"),
                           STORY_FIELDS, FIELD_COUNT(STORY_FIELDS), error, error_size) ||
         !companion_story_valid(&sv.story, sv.n_fish, sv.bank_count, sv.companion.away, sv.polish.next_id))) {
        snprintf(error, error_size, "Invalid aquarium story"); goto done;
    }
    if (schema->valuedouble >= 13 &&
        (!json_read_fields(&sv.games, cJSON_GetObjectItemCaseSensitive(root, "games"),
                           GAME_FIELDS, FIELD_COUNT(GAME_FIELDS), error, error_size) ||
         !companion_games_valid(&sv.games))) {
        snprintf(error, error_size, "Invalid game records"); goto done;
    }
    if (schema->valuedouble >= 14 &&
        (!json_read_fields(&sv.rpg, cJSON_GetObjectItemCaseSensitive(root, "rpg"),
                          RPG_FIELDS, FIELD_COUNT(RPG_FIELDS), error, error_size) ||
         !companion_rpg_valid(&sv.rpg, sv.polish.next_id))) {
        snprintf(error, error_size, "Invalid Lost Current story"); goto done;
    }
    if (width->valuedouble != (sv.companion.display_compact ? 320 : 240)) {
        snprintf(error, error_size, "Display size does not match saved scale"); goto done;
    }
    for (size_t i = 0; i < FIELD_COUNT(SAVE_FIELDS); i++) {
        const json_field_t *field = &SAVE_FIELDS[i];
        if (field->kind != J_FLOAT) continue;
        double bound = field->max == TANK_MAX_W ? width->valuedouble :
                       field->max == TANK_MAX_H ? height->valuedouble : -1;
        if (bound < 0) continue;
        const float *values = (const float *)((const char *)&sv + field->offset);
        for (size_t j = 0; j < field->count; j++) if (values[j] > bound) {
            snprintf(error, error_size, "Position outside saved display"); goto done;
        }
    }
    if (schema->valuedouble >= 5) {
        const cJSON *bank = cJSON_GetObjectItemCaseSensitive(root, "bank");
        if (!cJSON_IsArray(bank)) {
            snprintf(error, error_size, "Invalid fish bank"); goto done;
        }
    }
    /* A restore resumes the snapshot, not years of offline simulation. */
    sv.saved_unix = clock_port_now_unix();
    if (!commit) {
        save_t *current = calloc(1, sizeof *current);
        if (!current) { snprintf(error, error_size, "Not enough memory to validate restore"); goto done; }
        size_t got = 0;
        bool loaded = persist_port_load(current, sizeof *current, &got);
        bool mismatch = loaded && got >= offsetof(save_t, companion) && current->magic == SAVE_MAGIC &&
            (!migrate_profile(current) ||
             !game_income_valid(&current->game_income, current->care.game_paid) ||
             (current->profile.chosen && current->profile.mode != sv.profile.mode));
        bool inventory_mismatch = loaded && current->magic == SAVE_MAGIC && !inventory_restore_safe(&sv, current);
        const char *identity_error = !mismatch && loaded && current->magic == SAVE_MAGIC ?
            restore_fish_identities(&sv, current) : NULL;
        free(current);
        if (mismatch || persist_port_blocked()) {
            snprintf(error, error_size, "Backup mode differs or current save is unavailable"); goto done;
        }
        if (inventory_mismatch) {
            snprintf(error, error_size, "Decoration inventory changed; cannot reclaim transferred copies"); goto done;
        }
        if (identity_error) { snprintf(error, error_size, "%s", identity_error); goto done; }
    }
    if (commit) {
        cJSON_Delete(root); root = NULL;
        /* Portable backups replace only the active aquarium, never its purchased slot identity. */
        save_t *current = calloc(1, sizeof *current);
        if (!current) { snprintf(error, error_size, "Not enough memory for restore identity"); goto done; }
        size_t got = 0;
        if (persist_port_load(current, sizeof *current, &got) && got >= offsetof(save_t, companion) && current->magic == SAVE_MAGIC) {
            if (!migrate_profile(current) || !current->profile.chosen || !sv.profile.chosen ||
                current->profile.mode != sv.profile.mode) {
                free(current);
                snprintf(error, error_size, "Backup mode differs; wipe and choose that mode first");
                goto done;
            }
            if (!inventory_restore_safe(&sv, current)) {
                free(current);
                snprintf(error, error_size, "Decoration inventory changed; cannot reclaim transferred copies"); goto done;
            }
            const char *identity_error = restore_fish_identities(&sv, current);
            if (identity_error) {
                free(current); snprintf(error, error_size, "%s", identity_error); goto done;
            }
            sv.aquariums = current->aquariums;
            if (!game_income_valid(&current->game_income, current->care.game_paid)) {
                free(current); snprintf(error, error_size, "Invalid current game income"); goto done;
            }
            sv.game_income = current->game_income;
            sv.care = current->care;
            /* A portable snapshot cannot rewind the shared creature or its
             * care-day allowance, including a snapshot from before unlock. */
            if (current->mussel.owned) {
                sv.mussel = current->mussel;
                sv.care = current->care;
            }
            if (current->creative.enabled) {
                sv.sd_balance = current->sd_balance;
                sv.sd_earned = current->sd_earned;
            }
        } else if (sv.creative.enabled) {
            free(current);
            snprintf(error, error_size, "Enter Creative mode before restoring this backup");
            goto done;
        }
        free(current);
        sv.aquariums.stat_upgrades = restored_stats;
        sv.aquariums.clock_theme = (uint8_t)clock_values[0];
        sv.aquariums.clock_flags = (uint8_t)clock_values[1];
        sv.aquariums.clock_utc_quarters = (int8_t)clock_values[2];
        sv.aquariums.clock_layout = (uint8_t)clock_values[3];
        sv.aquariums.clock_delay = (uint8_t)(clock_values[4] | (clock_values[5] << CLOCK_SECONDARY_SHIFT));
        if (!sv.aquariums.count) sv.aquariums.count = 1;
    }
    ok = !commit || persist_port_save(&sv, sizeof sv);
    if (!ok) snprintf(error, error_size, "Internal save write failed; restore not completed");
    else error[0] = 0;
done:
    cJSON_Delete(root); return ok;
}

/* Resident state and layout stay with each SD aquarium; Bank fish travel.
 * Inactive snapshots never override the active profile's shared state. */
static bool save_name_referenced(const save_t *s, uint16_t id) {
    for (int i = 0; i < N_FISH_MAX + FISH_BANK_CAP + 1; i++) {
        const fish_lineage_t *line = i < N_FISH_MAX ? &s->story.active[i] :
            i < N_FISH_MAX + FISH_BANK_CAP ? &s->story.bank[i - N_FISH_MAX] : &s->story.traveler;
        if (line->parent[0] == id || line->parent[1] == id) return true;
    }
    for (int i = 0; i < s->rpg.chapter && i < 6; i++)
        if (s->rpg.contributors[i] == id) return true;
    return false;
}
static void save_remember_name(save_t *s, uint32_t id, const char *name) {
    if (!id || id > UINT16_MAX || !save_name_referenced(s, (uint16_t)id)) return;
    int slot = -1;
    for (int i = 0; i < STORY_NAME_BOOK; i++)
        if (s->story.names[i].id == id) { slot = i; break; }
    for (int i = 0; slot < 0 && i < STORY_NAME_BOOK; i++)
        if (!s->story.names[i].id || !save_name_referenced(s, s->story.names[i].id)) slot = i;
    if (slot < 0) slot = 0; /* Same bounded ancestor-name book as ordinary departures. */
    s->story.names[slot].id = (uint16_t)id;
    if (name != s->story.names[slot].name)
        snprintf(s->story.names[slot].name, sizeof s->story.names[slot].name, "%.*s", FISH_NAME_MAX, name);
}
static void share_fish_names(save_t *to, const save_t *from) {
    for (int i = 0; i < STORY_NAME_BOOK; i++)
        save_remember_name(to, from->story.names[i].id, from->story.names[i].name);
    for (int i = 0; i < from->n_fish; i++)
        save_remember_name(to, from->polish.active[i].id, from->names[i]);
    for (int i = 0; i < from->bank_count; i++)
        save_remember_name(to, from->polish.bank[i].id, from->bank[i].name);
    if (from->companion.away)
        save_remember_name(to, from->polish.traveler.id, from->companion.traveler.name);
}
static bool save_init_memories(save_t *s) {
    if (s->polish.version && s->story.version) return true;
    tank_t *legacy = calloc(1, sizeof *legacy);
    if (!legacy) return false;
    legacy->n_fish = s->n_fish; legacy->bank_count = s->bank_count;
    legacy->companion = s->companion; legacy->polish = s->polish; legacy->story = s->story;
    memcpy(legacy->bank, s->bank, sizeof legacy->bank);
    for (int i = 0; i < s->n_fish; i++) {
        legacy->fish[i].stage = (stage_t)s->fish[i].stage;
        legacy->fish[i].ms_bits = s->fish[i].ms_bits;
        legacy->fish[i].color = s->body[i];
        legacy->fish[i].parent_a = (int8_t)(s->parent_p1[i][0] - 1);
        legacy->fish[i].parent_b = (int8_t)(s->parent_p1[i][1] - 1);
    }
    companion_polish_init(legacy, false);
    s->polish = legacy->polish; s->story = legacy->story;
    free(legacy); return true;
}
enum { SAVE_ID_REFS = (N_FISH_MAX + FISH_BANK_CAP + 1) * 4 + STORY_NAME_BOOK + 7 };
typedef struct {
    uint32_t ids[SAVE_ID_REFS], offset;
    unsigned count;
    bool full;
} save_identity_map_t;
static uint32_t save_map_identity(save_identity_map_t *map, uint32_t id) {
    if (!id) return 0;
    for (unsigned i = 0; i < map->count; i++)
        if (map->ids[i] == id) return map->offset + i + 1;
    if (map->count == SAVE_ID_REFS || map->offset >= UINT16_MAX - map->count) {
        map->full = true; return 0;
    }
    map->ids[map->count++] = id;
    return map->offset + map->count;
}
static bool save_rebase_identities(save_t *to, uint32_t offset) {
    /* Map referenced IDs only: repeatedly restoring a snapshot must not
     * reserve its entire historical counter range again. */
    save_identity_map_t map = {.offset = offset};
    for (int i = 0; i < N_FISH_MAX + FISH_BANK_CAP + 1; i++) {
        fish_life_t *life = i < N_FISH_MAX ? &to->polish.active[i] :
            i < N_FISH_MAX + FISH_BANK_CAP ? &to->polish.bank[i - N_FISH_MAX] : &to->polish.traveler;
        fish_lineage_t *line = i < N_FISH_MAX ? &to->story.active[i] :
            i < N_FISH_MAX + FISH_BANK_CAP ? &to->story.bank[i - N_FISH_MAX] : &to->story.traveler;
        life->id = save_map_identity(&map, life->id);
        life->buddy = save_map_identity(&map, life->buddy);
        for (int p = 0; p < 2; p++)
            line->parent[p] = (uint16_t)save_map_identity(&map, line->parent[p]);
    }
    for (int i = 0; i < STORY_NAME_BOOK; i++)
        to->story.names[i].id = (uint16_t)save_map_identity(&map, to->story.names[i].id);
    to->rpg.participant = save_map_identity(&map, to->rpg.participant);
    for (int i = 0; i < 6; i++)
        to->rpg.contributors[i] = (uint16_t)save_map_identity(&map, to->rpg.contributors[i]);
    to->polish.next_id = offset + map.count + 1;
    return !map.full;
}
static const char *restore_fish_identities(save_t *to, save_t *current) {
    if (current->aquariums.count < 2) return NULL;
    if (!save_init_memories(to) || !save_init_memories(current))
        return "Not enough memory to restore fish identities";
    /* A portable/older snapshot must not reuse identities already allocated
     * to residents of other SD tanks. Its internal family links move together. */
    if (!save_rebase_identities(to, current->polish.next_id - 1))
        return "Fish identity limit; restore not completed";
    to->profile.reserved |= PROFILE_SHARED_BANK;
    return NULL;
}
static const char *share_bank(save_t *to, save_t *from) {
    if (!(to->profile.reserved & PROFILE_SHARED_BANK)) {
        if (to->bank_count + from->bank_count > FISH_BANK_CAP)
            return "MAKE ROOM IN GLOBAL BANK FIRST";
        /* Older SD tanks allocated overlapping local IDs. Rebase the entire
         * identity domain once, including departed parents and guide records. */
        if (!save_init_memories(to)) return "NOT ENOUGH MEMORY - NOT SWITCHED";
        if (!save_rebase_identities(to, from->polish.next_id - 1))
            return "FISH IDENTITY LIMIT - NOT SWITCHED";
        from->polish.next_id = to->polish.next_id;
        int first = from->bank_count;
        for (int i = 0; i < to->bank_count; i++) {
            from->bank[first + i] = to->bank[i];
            from->companion.bank[first + i] = to->companion.bank[i];
            from->polish.bank[first + i] = to->polish.bank[i];
            from->story.bank[first + i] = to->story.bank[i];
        }
        from->bank_count += to->bank_count;
        share_fish_names(from, to);
    }
    /* Names from a stale Bank can still be parents of this tank's residents. */
    share_fish_names(to, to);
    to->bank_count = from->bank_count;
    memcpy(to->bank, from->bank, sizeof to->bank);
    memcpy(to->companion.bank, from->companion.bank, sizeof to->companion.bank);
    memcpy(to->polish.bank, from->polish.bank, sizeof to->polish.bank);
    memcpy(to->story.bank, from->story.bank, sizeof to->story.bank);
    to->polish.next_id = from->polish.next_id;
    to->profile.reserved |= PROFILE_SHARED_BANK;
    from->profile.reserved |= PROFILE_SHARED_BANK;
    return NULL;
}
static void share_progress(save_t *to, const save_t *from) {
    /* The active profile is authoritative for wallet, records, mode and
     * progress. Unlock/first-event masks merge; residents, layout, theme,
     * local refund eligibility and starvation timers stay with this tank.
     * share_bank handles movable fish/IDs before this metadata copy.
     * Keep this field policy aligned with capture/load and profile tests. */
    uint8_t theme = to->journal.theme;
    to->sd_balance = from->sd_balance; to->sd_earned = from->sd_earned;
    to->journal = from->journal; to->journal.theme = theme;
    to->player_feedings = from->player_feedings;
    to->hold_approaches = from->hold_approaches;
    to->algae_colonies = from->algae_colonies; to->trim_px = from->trim_px;
    to->trims = from->trims; to->cells_cleaned = from->cells_cleaned;
    to->sd_colonies_paid = from->sd_colonies_paid; to->sd_inches_paid = from->sd_inches_paid;
    to->tank_ms_bits |= from->tank_ms_bits; to->tank_ms_seen |= from->tank_ms_seen;
    to->games = from->games; to->games_extra = from->games_extra;
    to->care = from->care;
    to->mussel = from->mussel;
    to->game_income = from->game_income;
    to->rpg = from->rpg;
    /* Guides retain global identities; choose a resident for the next action. */
    to->rpg.participant = 0;
    share_fish_names(to, from);
    to->companion.powered_s = from->companion.powered_s;
    to->companion.discoveries |= from->companion.discoveries;
    to->companion.collections |= from->companion.collections;
    to->companion.adventure_done |= from->companion.adventure_done;
    to->companion.adventure_claimed |= from->companion.adventure_claimed;
    to->companion.adventure_routes |= from->companion.adventure_routes;
    if (!to->companion.away) {
        if (!from->companion.away && from->companion.adventure) {
            to->companion.adventure = from->companion.adventure;
            to->companion.adventure_s = from->companion.adventure_s;
            to->companion.adventure_choice = from->companion.adventure_choice;
            memcpy(to->companion.portrait_name, from->companion.portrait_name, sizeof to->companion.portrait_name);
            to->companion.portrait_body = from->companion.portrait_body;
            to->companion.portrait_accent = from->companion.portrait_accent;
            to->companion.portrait_variety = from->companion.portrait_variety;
            to->companion.portrait_pattern = from->companion.portrait_pattern;
            to->companion.portrait_wearable = from->companion.portrait_wearable;
            to->companion.portrait_colors = from->companion.portrait_colors;
        }
        if (to->companion.adventure &&
            (to->companion.adventure_claimed & (1u << (to->companion.adventure - 1)))) {
            to->companion.adventure = 0; to->companion.adventure_s = 0;
        }
    }
    memcpy(to->companion.game_best, from->companion.game_best, sizeof to->companion.game_best);
    memcpy(to->companion.game_rounds, from->companion.game_rounds, sizeof to->companion.game_rounds);
    memcpy(to->companion.game_medals, from->companion.game_medals, sizeof to->companion.game_medals);
    to->companion.game_challenges = from->companion.game_challenges;
    memcpy(to->story.first_seen_s, from->story.first_seen_s, sizeof to->story.first_seen_s);
    memcpy(to->story.seen, from->story.seen, sizeof to->story.seen);
    to->profile.owned_items |= from->profile.owned_items;
    to->profile.marker = from->profile.marker; to->profile.version = from->profile.version;
    to->profile.mode = from->profile.mode; to->profile.chosen = from->profile.chosen;
    to->profile.reserved = from->profile.reserved;
    to->creative = from->creative;
    /* Storage belongs to the active profile, never to an inactive snapshot.
     * Placements and per-copy provenance stay with their aquarium. */
    memcpy(to->inventory.stored, from->inventory.stored, sizeof to->inventory.stored);
    memcpy(to->inventory.paid, from->inventory.paid, sizeof to->inventory.paid);
    to->inventory.version = 1;
}

static const char *aquarium_move(tank_t *t, int target) {
    if (target < 0 || target > t->aquariums.count || target >= AQUARIUM_CAP)
        return "INVALID TANK";
    if (target == t->aquariums.active) return "THIS TANK IS ALREADY ACTIVE";
    if (s_setup_pending || s_newborn >= 0) return "FINISH NAMING YOUR FRY FIRST";
    if (!aquarium_port_ready()) return "TANK STORAGE UNAVAILABLE";
    bool buy = target == t->aquariums.count;
    bool creative_new = buy && progression_creative(t);
    if (!progression_mode_chosen(t)) return "CHOOSE YOUR GAME MODE FIRST";
    if (buy && !creative_new && t->sd_balance < AQUARIUM_PRICE) return "NEED 500 SAND DOLLARS";
    struct { save_t outgoing, incoming; } *s = malloc(sizeof *s);
    if (!s) return "NOT ENOUGH MEMORY";
    const char *error = NULL;
    capture_save(t, &s->outgoing);
    aquarium_state_t owned = t->aquariums;
    if (!(owned.id[0] | owned.id[1])) aquarium_port_id(owned.id);
    if (!(owned.id[0] | owned.id[1])) { error = "COULD NOT CREATE TANK ID"; goto done; }
    if (buy) {
        tank_t *fresh = malloc(sizeof *fresh);
        if (!fresh) { error = "NOT ENOUGH MEMORY"; goto done; }
        bool compact = TANK_W == TANK_MAX_W;
        tank_resolution(true);
        tank_init(fresh, owned.id[0] ^ owned.id[1] ^ (uint32_t)target);
        fresh->companion.version = COMPANION_VERSION;
        fresh->companion.display_compact = 1;
        tank_new_population(fresh);
        fresh->tank_ms_bits = TMS_PAIR;
        for (int i = 0; i < fresh->n_fish; i++) {
            fresh->fish[i].ms_bits = MS_ARRIVED;
            apply_growth(&fresh->fish[i]);
        }
        companion_polish_init(fresh, true);
        if (creative_new) fresh->creative = (creative_state_t){CREATIVE_MARKER, 1, 1, 0};
        capture_save(fresh, &s->incoming);
        tank_resolution(compact);
        free(fresh);
        for (int i = 0; i < N_FISH_MAX; i++) s->incoming.fish[i].age_s = 0;
        s->incoming.setup_pending = creative_new ? 0 : 1; s->incoming.newborn_p1 = 0;
        s->incoming.arrival_pending = 0; s->incoming.sale_meals_need = 0;
        s->incoming.fry_seed = 0; s->incoming.fry_sold = 0; s->incoming.fry_age_min = 0;
        owned.count++;
        if (!creative_new) s->outgoing.sd_balance -= AQUARIUM_PRICE;
    } else {
        if (!aquarium_port_read(owned.id, target, &s->incoming, sizeof s->incoming)) {
            error = "TANK READ FAILED - NOT SWITCHED"; goto done;
        }
        save_t *v = &s->incoming;
        if (v->magic != SAVE_MAGIC || !migrate_profile(v) || v->profile.mode != t->profile.mode ||
            (v->profile.mode != GAME_MODE_HARD && v->n_fish < 2 && !v->companion.away) ||
            v->n_fish + v->companion.away > POP_CAP || v->bank_count > FISH_BANK_CAP ||
            !companion_valid_mode(&v->companion, v->creative.enabled != 0) ||
            !companion_polish_valid(&v->polish, v->n_fish, v->bank_count, v->companion.away) ||
            !companion_story_valid(&v->story, v->n_fish, v->bank_count, v->companion.away, v->polish.next_id) ||
            !companion_rpg_valid(&v->rpg, v->polish.next_id) ||
            !companion_games_valid(&v->games) || !extra_games_valid(&v->games_extra) || !creative_valid(&v->creative) ||
            !inventory_valid(&v->inventory) ||
            !mussel_valid(&v->mussel) ||
            !game_income_valid(&v->game_income, v->care.game_paid) ||
            (v->creative.enabled && v->n_fish + v->companion.away > CREATIVE_FISH_CAP) ||
            (v->companion.away && !bank_valid(&v->companion.traveler)) ||
            (v->aquariums.stat_upgrades & ~STAT_MASK) ||
            v->aquariums.clock_theme >= CLOCK_THEME_COUNT ||
            v->aquariums.clock_utc_quarters < -48 || v->aquariums.clock_utc_quarters > 56 ||
            !clock_layout_valid(v->aquariums.clock_layout) || !clock_delay_valid(v->aquariums.clock_delay) ||
            memcmp(v->aquariums.id, owned.id, sizeof owned.id) || v->aquariums.active != target) {
            error = "INVALID TANK SAVE"; goto done;
        }
        for (int i = 0; i < v->n_fish; i++) {
            if (v->fish[i].preset >= tank_roster_count()) { error = "INVALID TANK FISH"; goto done; }
            for (size_t k = 0; k < FIELD_COUNT(FISH_FIELDS); k++) {
                const json_field_t *f = &FISH_FIELDS[k];
                double n = json_number((const uint8_t *)&v->fish[i] + f->offset, f->kind);
                if (!isfinite(n) || n < f->min || n > f->max) { error = "INVALID TANK FISH"; goto done; }
            }
        }
        for (int i = 0; i < v->bank_count; i++)
            if (!bank_valid(&v->bank[i])) { error = "INVALID TANK BANK"; goto done; }
    }
    char incoming_name[16];
    uint8_t incoming_stats = s->incoming.aquariums.stat_upgrades | s->outgoing.aquariums.stat_upgrades;
    uint8_t incoming_clock = s->incoming.aquariums.clock_theme, incoming_flags = s->incoming.aquariums.clock_flags;
    int8_t incoming_offset = s->incoming.aquariums.clock_utc_quarters;
    uint8_t incoming_layout = s->incoming.aquariums.clock_layout;
    uint8_t incoming_delay = s->incoming.aquariums.clock_delay;
    memcpy(incoming_name, s->incoming.aquariums.name, sizeof incoming_name);
    s->outgoing.aquariums = owned;
    error = share_bank(&s->incoming, &s->outgoing);
    if (error) goto done;
    share_progress(&s->incoming, &s->outgoing);
    s->incoming.aquariums = owned; s->incoming.aquariums.active = (uint8_t)target;
    s->incoming.aquariums.stat_upgrades = incoming_stats;
    s->incoming.aquariums.clock_theme = incoming_clock;
    s->incoming.aquariums.clock_flags = incoming_flags;
    s->incoming.aquariums.clock_utc_quarters = incoming_offset;
    s->incoming.aquariums.clock_layout = incoming_layout;
    s->incoming.aquariums.clock_delay = incoming_delay;
    if (buy || !incoming_name[0] || !memchr(incoming_name, 0, sizeof incoming_name))
        snprintf(incoming_name, sizeof incoming_name, "TANK %d", target + 1);
    memcpy(s->incoming.aquariums.name, incoming_name, sizeof incoming_name);
    if (!companion_polish_valid(&s->incoming.polish, s->incoming.n_fish, s->incoming.bank_count, s->incoming.companion.away) ||
        !companion_story_valid(&s->incoming.story, s->incoming.n_fish, s->incoming.bank_count,
                               s->incoming.companion.away, s->incoming.polish.next_id) ||
        !companion_rpg_valid(&s->incoming.rpg, s->incoming.polish.next_id)) {
        error = "INVALID SHARED BANK - NOT SWITCHED"; goto done;
    }
    /* No time away is simulated for inactive tanks. Active storage is the final commit point. */
    s->incoming.saved_unix = clock_port_now_unix();
    if (!aquarium_port_write(owned.id, owned.active, &s->outgoing, sizeof s->outgoing) ||
        (buy && !aquarium_port_write(owned.id, target, &s->incoming, sizeof s->incoming))) {
        error = "TANK WRITE FAILED - NO CHANGE"; goto done;
    }
    if (!persist_port_save(&s->incoming, sizeof s->incoming)) { error = "SAVE FAILED - NO CHANGE"; goto done; }
    double mussel_fraction = t->mussel_fraction;
    tank_init(t, owned.id[0] ^ (uint32_t)target);
    int64_t stamp;
    s_ravenous = s_gave_up = false; s_ravenous_t = 0;
    memset(s_starve_s, 0, sizeof s_starve_s);
    s_since_save = s_dirty_since = 0; s_dirty = false; s_booted = true;
    /* The record was validated before committing, so this cannot require more I/O. */
    if (!load_save(t, &stamp, &s->incoming)) abort();
    t->mussel_fraction = mussel_fraction;
done:
    free(s); return error;
}

const char *progression_aquarium_move(tank_t *t, int target) {
    return aquarium_move(t, target);
}

const char *progression_creative_enter(tank_t *t) {
    return progression_mode_select(t, GAME_MODE_CREATIVE);
}

const char *progression_mode_select(tank_t *t, int mode) {
    if (mode < GAME_MODE_NORMAL || mode > GAME_MODE_HARD) return "INVALID GAME MODE";
    if (progression_mode_chosen(t)) return "WIPE THE PROFILE TO CHANGE GAME MODE";
    profile_state_t old = t->profile;
    creative_state_t creative = t->creative;
    uint8_t cozy = t->companion.cozy;
    bool autofeed = t->autofeed_off;
    t->profile.marker = PROFILE_MARKER; t->profile.version = 1;
    t->profile.mode = (uint8_t)mode; t->profile.chosen = 1;
    t->profile.reserved = (uint8_t)(1u << mode);
    memset(t->profile.starvation_s, 0, sizeof t->profile.starvation_s);
    t->creative = mode == GAME_MODE_CREATIVE ? (creative_state_t){CREATIVE_MARKER, 1, 1, 0} : (creative_state_t){0};
    if (mode == GAME_MODE_HARD) { t->companion.cozy = 0; t->autofeed_off = true; }
    if (progression_save(t)) return NULL;
    t->profile = old; t->creative = creative;
    t->companion.cozy = cozy; t->autofeed_off = autofeed;
    return "SAVE FAILED - MODE NOT SELECTED";
}

const char *progression_hard_rescue(tank_t *t) {
    if (!progression_hard(t) || t->n_fish) return "RESCUE REQUIRES AN EMPTY HARD AQUARIUM";
    if (2 + t->companion.away > POP_CAP || 2 + t->companion.away > N_FISH_MAX)
        return "NO ROOM FOR A RESCUE PAIR AND TRAVELER";
    tank_t *next = malloc(sizeof *next);
    if (!next) return "NOT ENOUGH MEMORY";
    *next = *t;
    /* An empty Hard aquarium always gets a free pair, keeping recovery
     * available without selling Bank residents or resetting progression. */
    for (int i = 0; i < 2; i++) {
        int slot = tank_add_bought_fry(next, i, 0);
        if (slot < 0) { free(next); return "RESCUE FAILED - NO CHANGE"; }
        next->fish[slot].ms_bits = MS_ARRIVED;
        next->profile.starvation_s[slot] = 0;
    }
    companion_polish_observe(next);
    double ages[N_FISH_MAX]; memcpy(ages, s_age, sizeof ages);
    s_age[0] = s_age[1] = 0;
    bool ok = progression_save(next);
    if (ok) { *t = *next; s_hard_departed = s_hard_save_failed = false; }
    else memcpy(s_age, ages, sizeof ages);
    free(next);
    return ok ? NULL : "SAVE FAILED - NO RESCUE ADDED";
}

const char *progression_creative_add_fish(tank_t *t, int variety) {
    if (!progression_creative(t)) return "ENTER CREATIVE MODE FIRST";
    if (variety < 0 || variety >= FISH_VARIETY_COUNT) return "INVALID FISH LOOK";
    if (occupied_slots(t) >= CREATIVE_FISH_CAP || occupied_slots(t) >= POP_CAP)
        return "LIMIT 5 FISH INCLUDING TRAVELERS";
    tank_t *next = malloc(sizeof *next);
    if (!next) return "NOT ENOUGH MEMORY";
    *next = *t;
    int slot = tank_add_bought_fry(next, 0, 0);
    if (slot < 0) { free(next); return "NO ROOM FOR ANOTHER FISH"; }
    next->fish[slot].variety = (uint8_t)variety;
    next->fish[slot].ms_bits = MS_ARRIVED;
    next->sd_paid_fish[slot] = 0;
    double age = s_age[slot]; float starve = s_starve_s[slot];
    s_age[slot] = 0; s_starve_s[slot] = 0;
    apply_growth(&next->fish[slot]);
    companion_polish_observe(next);
    bool ok = progression_save(next);
    if (ok) *t = *next;
    else { s_age[slot] = age; s_starve_s[slot] = starve; }
    free(next);
    return ok ? NULL : "SAVE FAILED - NO FISH ADDED";
}

const char *progression_creative_remove_fish(tank_t *t, int fish) {
    if (!progression_creative(t)) return "ENTER CREATIVE MODE FIRST";
    if (fish < 0 || fish >= t->n_fish) return "INVALID FISH";
    if (t->n_fish <= 2) return "KEEP AT LEAST 2 RESIDENT FISH";
    tank_t *next = malloc(sizeof *next);
    if (!next) return "NOT ENOUGH MEMORY";
    *next = *t;
    double age[N_FISH_MAX]; float starve[N_FISH_MAX];
    memcpy(age, s_age, sizeof age); memcpy(starve, s_starve_s, sizeof starve);
    tank_remove_fish(next, fish);
    for (int i = fish; i < next->n_fish; i++) {
        s_age[i] = s_age[i + 1]; s_starve_s[i] = s_starve_s[i + 1];
    }
    s_age[next->n_fish] = 0; s_starve_s[next->n_fish] = 0;
    bool ok = progression_save(next);
    if (ok) *t = *next;
    else { memcpy(s_age, age, sizeof age); memcpy(s_starve_s, starve, sizeof starve); }
    free(next);
    return ok ? NULL : "SAVE FAILED - NO FISH REMOVED";
}

const char *progression_aquarium_rename(tank_t *t, const char *name) {
    if (!name) return "ENTER A TANK NAME";
    size_t len = strlen(name);
    if (!len || len > 15) return "USE 1 TO 15 CHARACTERS";
    bool nonblank = false;
    for (size_t i = 0; i < len; i++) {
        if ((unsigned char)name[i] < 32 || (unsigned char)name[i] > 126) return "USE PRINTABLE CHARACTERS";
        nonblank |= name[i] != ' ';
    }
    if (!nonblank) return "ENTER A TANK NAME";
    char old[16]; memcpy(old, t->aquariums.name, sizeof old);
    memset(t->aquariums.name, 0, sizeof t->aquariums.name);
    memcpy(t->aquariums.name, name, len);
    if (progression_save(t)) return NULL;
    memcpy(t->aquariums.name, old, sizeof old); return "SAVE FAILED - NAME UNCHANGED";
}
bool progression_aquarium_label(const tank_t *t, int slot, char name[16]) {
    if (slot < 0 || slot >= t->aquariums.count) return false;
    if (slot == t->aquariums.active) {
        memcpy(name, t->aquariums.name, 16);
        if (!name[0]) snprintf(name, 16, "TANK %d", slot + 1);
        return true;
    }
    save_t *sv = malloc(sizeof *sv);
    if (!sv) return false;
    bool ok = aquarium_port_read(t->aquariums.id, slot, sv, sizeof *sv) &&
              sv->magic == SAVE_MAGIC && sv->aquariums.active == slot &&
              !memcmp(sv->aquariums.id, t->aquariums.id, sizeof sv->aquariums.id) &&
              memchr(sv->aquariums.name, 0, sizeof sv->aquariums.name);
    if (ok) {
        memcpy(name, sv->aquariums.name, 16);
        if (!name[0]) snprintf(name, 16, "TANK %d", slot + 1);
    }
    free(sv); return ok;
}

bool progression_aquarium_summary(const tank_t *t, int slot, aquarium_summary_t *out) {
    if (!out || slot < 0 || slot >= t->aquariums.count) return false;
    save_t *sv = NULL;
    uint32_t unlocks;
    uint8_t upgrades;
    if (slot == t->aquariums.active) {
        if (t->journal.theme >= JOURNAL_THEME_COUNT) return false;
        *out = (aquarium_summary_t){.fish = (uint8_t)t->n_fish, .away = t->companion.away,
            .bank = t->bank_count, .shrimp = (uint8_t)t->shrimp_n, .theme = t->journal.theme,
            .subtitle = t->polish.subtitle, .emblem = t->polish.emblem,
            .creative = progression_creative(t),
            .last_visit = clock_port_now_unix()};
        unlocks = t->sd_unlocks; upgrades = t->aquariums.stat_upgrades;
    } else {
        sv = malloc(sizeof *sv);
        if (!sv) return false;
        bool ok = aquarium_port_read(t->aquariums.id, slot, sv, sizeof *sv) &&
            sv->magic == SAVE_MAGIC && sv->aquariums.active == slot &&
            !memcmp(sv->aquariums.id, t->aquariums.id, sizeof sv->aquariums.id) &&
            sv->n_fish <= N_FISH_MAX && sv->bank_count <= FISH_BANK_CAP &&
            sv->journal.theme < JOURNAL_THEME_COUNT && !(sv->aquariums.stat_upgrades & ~STAT_MASK) &&
            creative_valid(&sv->creative);
        if (!ok) { free(sv); return false; }
        *out = (aquarium_summary_t){.fish = sv->n_fish, .away = sv->companion.away,
            .bank = t->bank_count, .shrimp = sv->shrimp_n, .theme = sv->journal.theme,
            .subtitle = sv->polish.subtitle, .emblem = sv->polish.emblem,
            .creative = sv->creative.enabled != 0,
            .last_visit = sv->saved_unix};
        unlocks = sv->sd_unlocks; upgrades = sv->aquariums.stat_upgrades;
        free(sv);
    }
    out->helpers = !!(unlocks & SD_ITEM_SNAIL) + !!(unlocks & SD_ITEM_URCHIN) + !!(unlocks & SD_ITEM_SEAHORSE);
    if (!(unlocks & SD_ITEM_SHRIMP)) out->shrimp = 0;
    out->upgrades = (uint8_t)__builtin_popcount(upgrades);
    return true;
}
