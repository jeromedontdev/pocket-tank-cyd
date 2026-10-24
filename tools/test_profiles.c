#include "progression.h"
#include "companion.h"
#include "setup.h"
#include "cJSON.h"
#include "aquarium_store.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef TEST_PROFILE_STORE
static void damage(const char *save, int slot, char copy) {
    char path[1200];
    snprintf(path, sizeof path, "%s.profiles/5052463141435456-%d.%c", save, slot, copy);
    FILE *f = fopen(path, "wb"); assert(f);
    assert(fwrite("interrupted", 1, 11, f) == 11); assert(fclose(f) == 0);
}
int main(void) {
    const char *save = getenv("POCKET_TANK_SAVE"); assert(save);
    unsigned char data[AQUARIUM_SAVE_BYTES], out[AQUARIUM_SAVE_BYTES]; size_t got;
    memset(data, 17, sizeof data);
    assert(!persist_port_load(out, sizeof out, &got) && !persist_port_blocked());
    assert(persist_port_save(data, sizeof data));
    memset(data, 29, sizeof data); assert(persist_port_save(data, sizeof data));
    assert(persist_port_load(out, sizeof out, &got) && got == sizeof out && out[0] == 29);
    damage(save, 0, 'b');
    assert(persist_port_load(out, sizeof out, &got) && out[0] == 17);
    assert(persist_port_save(data, sizeof data));
    assert(persist_port_load(out, sizeof out, &got) && out[0] == 29);
    for (int copy = 0; copy < 2; copy++) {
        char name[1200];
        snprintf(name, sizeof name, "%s.profiles/5052463141435456-0.%c", save, copy ? 'b' : 'a');
        if (copy == 0) assert(remove(name) == 0);
        else (void)remove(name);
    }
    assert(!persist_port_load(out, sizeof out, &got) && persist_port_blocked());
    assert(persist_port_erase());
    assert(!persist_port_load(out, sizeof out, &got) && !persist_port_blocked());
    assert(persist_port_save(data, sizeof data));
    damage(save, 0, 'a'); damage(save, 0, 'b');
    FILE *legacy = fopen(save, "wb"); assert(legacy);
    assert(fwrite(data, 1, sizeof data, legacy) == sizeof data); fclose(legacy);
    assert(!persist_port_load(out, sizeof out, &got) && persist_port_blocked());
    assert(!persist_port_save(data, sizeof data)); /* no stale legacy resurrection */
    assert(persist_port_erase());
    assert(!persist_port_load(out, sizeof out, &got) && !persist_port_blocked());
    /* Simulate power loss after tombstone commit, before old-copy cleanup.
     * Neither remaining active bytes nor a stale NVS/raw file may resurrect. */
    char directory[1200];
    snprintf(directory, sizeof directory, "%s.profiles", save);
    const uint32_t id[2] = {0x50524631u, 0x41435456u};
    assert(aquarium_store_write(directory, id, 0, data, sizeof data));
    legacy = fopen(save, "wb"); assert(legacy);
    assert(fwrite(data, 1, sizeof data, legacy) == sizeof data); fclose(legacy);
    assert(!persist_port_load(out, sizeof out, &got) && !persist_port_blocked());
    assert(persist_port_save(data, sizeof data));
    assert(persist_port_load(out, sizeof out, &got) && out[0] == 29);
    puts("Profile dual-copy corruption, interruption, legacy blocking and wipe passed.");
    return 0;
}
#else
static unsigned char saved[8192], slots[AQUARIUM_CAP][AQUARIUM_SAVE_BYTES];
static size_t saved_size, slot_sizes[AQUARIUM_CAP];
static bool card_ready = true, reject_write, reject_slot_write, reject_slot_read;
static bool reject_erase;
static unsigned id_serial = 100;
static int64_t now = 2000000000;
bool aquarium_port_ready(void) { return card_ready; }
void aquarium_port_id(uint32_t id[2]) { id[0] = ++id_serial; id[1] = 0x87654321; }
bool aquarium_port_read(const uint32_t id[2], int slot, void *data, size_t size) {
    (void)id;
    if (reject_slot_read || !slot_sizes[slot]) return false;
    memset(data, 0, size); memcpy(data, slots[slot], slot_sizes[slot]); return true;
}
bool aquarium_port_write(const uint32_t id[2], int slot, const void *data, size_t size) {
    (void)id;
    if (reject_slot_write) return false;
    memcpy(slots[slot], data, size); slot_sizes[slot] = size; return true;
}
bool persist_port_load(void *buf, size_t max, size_t *got) {
    if (!saved_size) return false;
    *got = saved_size < max ? saved_size : max; memcpy(buf, saved, *got); return true;
}
bool persist_port_save(const void *buf, size_t size) {
    if (reject_write) return false;
    memcpy(saved, buf, size); saved_size = size; return true;
}
bool persist_port_erase(void) { if (reject_erase) return false; saved_size = 0; return true; }
int64_t clock_port_now_unix(void) { return now; }
const char *version_port_string(void) { return "test"; }
const setup_place_snapshot_t *setup_place_original(void) { return NULL; }
static void reload(tank_t *t) { tank_init(t, 456); progression_boot(t); }
static void start(tank_t *t, int mode) {
    saved_size = 0; memset(slot_sizes, 0, sizeof slot_sizes);
    tank_init(t, 12); progression_boot(t);
    assert(!progression_mode_chosen(t));
    assert(!progression_mode_select(t, mode));
    progression_setup_done(t);
}
static void test_choice(tank_t *t) {
    tank_init(t, 12); progression_boot(t);
    assert(!progression_mode_chosen(t)); assert(progression_save(t));
    reload(t); assert(!progression_mode_chosen(t));
    tank_t before = *t;
    reject_write = true;
    assert(progression_mode_select(t, GAME_MODE_HARD)); assert(!memcmp(t, &before, sizeof *t));
    reject_write = false;
    assert(!progression_mode_select(t, GAME_MODE_NORMAL));
    assert(progression_mode_select(t, GAME_MODE_CREATIVE));
    reload(t); assert(progression_mode_chosen(t) && progression_mode(t) == GAME_MODE_NORMAL);
    /* Existing schemas migrate Normal, chosen, with names and money retained. */
    tank_set_name(t, 0, "LEGACY"); t->sd_balance = 41; assert(progression_save(t));
    saved_size = 5912; reload(t);
    assert(progression_mode_chosen(t) && !progression_creative(t));
    assert(t->sd_balance == 41 && !strcmp(t->fish[0].name, "legacy"));
    /* The old unshipped free Creative slot is incompatible, never Normal. */
    assert(progression_save(t));
    creative_state_t creative = {0x43525631u, 1, 1, 0};
    memcpy(saved + 5912, &creative, sizeof creative); saved_size = 5920;
    unsigned char preserved[5920]; memcpy(preserved, saved, sizeof preserved);
    reload(t); assert(!progression_save(t));
    assert(saved_size == 5920 && !memcmp(preserved, saved, sizeof preserved));
}
static void test_aquariums(tank_t *t) {
    start(t, GAME_MODE_NORMAL);
    t->sd_balance = 1200; t->sd_earned = 1300; t->journal.xp = 500;
    t->journal.claimed = 1; t->journal.seen = 3;
    t->player_feedings = 5; t->algae_colonies = 8;
    t->trim_px = 50 * PX_PER_CM; t->hold_approaches = 2;
    journal_tick(t);
    t->care.ready = t->care.claimed = 7; t->care.game_paid = CARE_GAME_LIMIT;
    t->companion.discoveries = 63; t->companion.collections = 1;
    t->companion.active[0].bond = 80; tank_set_name(t, 0, "LOCAL");
    assert(progression_buy(t, 0)); assert(t->sd_balance == 1160);
    t->sd_balance += SD_ITEMS[SD_SCENERY_FIRST].price;
    assert(progression_buy(t, SD_SCENERY_FIRST));
    tank_t before = *t;
    reject_write = true; assert(progression_aquarium_move(t, 1));
    assert(!memcmp(t, &before, sizeof *t)); reject_write = false;
    reject_slot_write = true; assert(progression_aquarium_move(t, 1));
    assert(!memcmp(t, &before, sizeof *t)); reject_slot_write = false;
    assert(!progression_aquarium_move(t, 1)); progression_setup_done(t);
    assert(t->sd_balance == 660 && t->journal.xp == 500 && t->journal.claimed == 1);
    assert(t->player_feedings == 5 && t->algae_colonies == 8 &&
           t->trim_px == 50 * PX_PER_CM && t->hold_approaches == 2);
    journal_tick(t); assert(t->journal.xp == 500);
    assert(t->care.claimed == 7 && t->care.game_paid == CARE_GAME_LIMIT);
    assert(t->companion.discoveries == 63 && t->companion.collections == 1);
    assert(t->companion.active[0].bond == 0 && strcmp(t->fish[0].name, "local"));
    assert(!progression_item_owned(t, 0)); /* globally unlocked, not cloned */
    assert(!progression_item_owned(t, SD_SCENERY_FIRST) && progression_item_unlocked(t, SD_SCENERY_FIRST));
    assert(progression_buy(t, 0) && t->sd_balance == 660); /* free local placement */
    assert(progression_sell_value_for(t, 0) == 0);
    assert(progression_sell(t, 0) && t->sd_balance == 660); /* cannot farm a free copy */
    t->journal.xp = 700; t->companion.discoveries = 127; t->sd_balance = 777;
    t->games_extra.version = 1; t->games_extra.rounds[0] = 2; t->games_extra.best[0] = 2;
    assert(!progression_aquarium_move(t, 0));
    assert(t->sd_balance == 777 && t->journal.xp == 700 && t->companion.discoveries == 127);
    assert(t->games_extra.rounds[0] == 2 && t->games_extra.best[0] == 2);
    assert(t->care.claimed == 7 && t->care.game_paid == CARE_GAME_LIMIT);
    assert(t->companion.active[0].bond == 80 && !strcmp(t->fish[0].name, "local"));
    assert(progression_item_owned(t, 0));
    assert(progression_item_owned(t, SD_SCENERY_FIRST));
    before = *t; reject_slot_read = true;
    assert(progression_aquarium_move(t, 1)); assert(!memcmp(t, &before, sizeof *t));
    reject_slot_read = false;
    char *json = progression_backup_json(t); assert(json);
    char error[160];
    assert(strstr(json, "\"schema\":17") && strstr(json, "\"games_extra\":") && strstr(json, "\"profile\":"));
    assert(progression_restore_json(json, strlen(json), false, error, sizeof error));
    assert(progression_restore_json(json, strlen(json), true, error, sizeof error));
    free(json); reload(t);
    assert(t->sd_balance == 777 && t->games_extra.rounds[0] == 2);
    uint32_t old_id = t->aquariums.id[0];
    assert(progression_reset(t, 123));
    assert(!progression_mode_chosen(t) && t->aquariums.count == 1 && !t->sd_balance && !t->journal.xp);
    assert(t->aquariums.id[0] != old_id);
    start(t, GAME_MODE_CREATIVE);
    assert(!progression_aquarium_move(t, 1));
    assert(progression_creative(t) && t->aquariums.count == 2 && !t->sd_balance);
}
static void test_refunds(tank_t *t) {
    const int items[] = {0, 9, SD_SCENERY_FIRST};
    for (size_t k = 0; k < sizeof items / sizeof items[0]; k++) {
        int item = items[k];
        start(t, GAME_MODE_NORMAL); t->sd_balance = 10000;
        assert(progression_buy(t, item));
        int refund = progression_sell_value(item), paid_balance = t->sd_balance;
        assert(progression_sell_value_for(t, item) == refund && refund > 0);
        tank_t before = *t; reject_write = true;
        assert(!progression_sell(t, item) && !memcmp(t, &before, sizeof *t));
        assert(progression_sell_value_for(t, item) == refund);
        reject_write = false;
        assert(progression_sell(t, item) && t->sd_balance == paid_balance + refund);
        reload(t);
        int balance = t->sd_balance;
        for (int repeat = 0; repeat < 3; repeat++) {
            assert(progression_buy(t, item) && t->sd_balance == balance);
            assert(progression_sell_value_for(t, item) == 0);
            assert(progression_sell(t, item) && t->sd_balance == balance);
            reload(t);
        }
        assert(!progression_aquarium_move(t, 1)); progression_setup_done(t);
        balance = t->sd_balance;
        for (int repeat = 0; repeat < 3; repeat++) {
            assert(progression_buy(t, item) && t->sd_balance == balance);
            assert(progression_sell_value_for(t, item) == 0);
            assert(progression_sell(t, item) && t->sd_balance == balance);
            assert(!progression_aquarium_move(t, 0));
            assert(!progression_aquarium_move(t, 1));
            assert(t->sd_balance == balance);
        }
    }
}
static void test_wipe_commit(tank_t *t) {
    start(t, GAME_MODE_HARD); t->sd_balance = 321; t->journal.xp = 456;
    tank_set_name(t, 0, "OLD"); assert(progression_save(t));
    tank_t before = *t;
    reject_erase = true;
    assert(!progression_reset(t, 789) && !memcmp(t, &before, sizeof *t));
    reject_erase = false; reject_write = true;
    assert(progression_reset(t, 789)); /* durable erase committed: not a failure */
    assert(!progression_mode_chosen(t) && !t->sd_balance && !t->journal.xp);
    assert(strstr(progression_hard_status(t), "WIPED"));
    assert(!saved_size);
    reload(t); /* No old mode/wallet/fish returns after failed fresh write. */
    assert(!progression_mode_chosen(t) && !t->sd_balance && !t->journal.xp);
    assert(strcmp(t->fish[0].name, "old"));
    reject_write = false; assert(progression_save(t)); reload(t);
    assert(!progression_mode_chosen(t) && !t->sd_balance && !t->journal.xp);
}
static void test_traveler_rescue(tank_t *t) {
    start(t, GAME_MODE_HARD);
    assert(companion_adventure_start(t, 0, 0));
    assert(t->companion.away && t->n_fish == 1);
    uint32_t traveler = t->polish.traveler.id;
    t->fish[0].hunger = 10;
    t->profile.starvation_s[0] = HARD_STARVATION_S - 1;
    progression_tick(t, 1);
    assert(t->n_fish == 0 && t->companion.away);
    reload(t);
    assert(!progression_hard_rescue(t));
    assert(t->n_fish == 2 && t->companion.away && t->polish.traveler.id == traveler);
    reload(t);
    t->companion.adventure_s = companion_adventure_total(&t->companion, 0);
    assert(progression_adventure_return(t));
    assert(t->n_fish == 3 && !t->companion.away);
    assert(t->polish.active[2].id == traveler);
}
static void test_hard(tank_t *t) {
    start(t, GAME_MODE_HARD);
    assert(!t->companion.cozy && t->autofeed_off);
    t->sd_balance = 17; t->journal.xp = 900;
    t->fish[0].hunger = t->fish[1].hunger = 10;
    progression_time_scale = 100;
    progression_tick(t, 10);
    assert(t->profile.starvation_s[0] == 10 && t->n_fish == 2);
    progression_slept(t, 86400);
    assert(t->profile.starvation_s[0] == 10 && t->n_fish == 2);
    t->fish[0].hunger = 8; progression_tick(t, 1);
    assert(t->profile.starvation_s[0] == 0);
    t->fish[0].hunger = t->fish[1].hunger = 10;
    t->profile.starvation_s[0] = t->profile.starvation_s[1] = HARD_STARVATION_S - 1;
    assert(progression_save(t)); reload(t);
    assert(t->profile.starvation_s[0] == HARD_STARVATION_S - 1);
    assert(progression_hard_status(t));
    reject_write = true; progression_tick(t, 1);
    assert(t->n_fish == 2 && strstr(progression_hard_status(t), "FAILED"));
    reject_write = false; progression_tick(t, 1);
    assert(t->n_fish == 0 && t->journal.xp >= 900);
    int balance = t->sd_balance;
    assert(t->polish.recap[0].kind == MEMORY_DEATH);
    reload(t); assert(t->n_fish == 0 && t->sd_balance == balance);
    char *json = progression_backup_json(t); char error[160]; assert(json);
    assert(progression_restore_json(json, strlen(json), true, error, sizeof error)); free(json);
    reject_write = true; assert(progression_hard_rescue(t)); assert(t->n_fish == 0);
    reject_write = false; assert(!progression_hard_rescue(t));
    assert(t->n_fish == 2 && t->sd_balance == balance && t->journal.xp >= 900);
    for (int i = 0; i < 2; i++) assert(t->profile.starvation_s[i] == 0);
    assert(progression_save(t));
    json = progression_backup_json(t); assert(json);
    start(t, GAME_MODE_NORMAL);
    assert(!progression_restore_json(json, strlen(json), true, error, sizeof error)); free(json);
    progression_time_scale = 1;
    for (int mode = GAME_MODE_NORMAL; mode <= GAME_MODE_CREATIVE; mode++) {
        start(t, mode); t->fish[0].hunger = t->fish[1].hunger = 10;
        progression_tick(t, HARD_STARVATION_S * 2);
        assert(t->n_fish >= 2 && t->profile.starvation_s[0] == 0);
    }
}
static void test_stories(tank_t *t) {
    start(t, GAME_MODE_NORMAL); t->sd_balance = 1000;
    for (int i = 0; i < 3; i++) assert(companion_rpg_choose(t, 0, i));
    assert(t->rpg.chapter == 1 && t->rpg.participant);
    uint16_t contributor = t->rpg.contributors[0];
    assert(!progression_aquarium_move(t, 1)); progression_setup_done(t);
    assert(t->rpg.chapter == 1 && t->rpg.participant == 0 && t->rpg.contributors[0] == contributor);
    assert(t->polish.active[0].id != contributor);
    assert(companion_rpg_contributor(t, contributor));
    assert(companion_rpg_valid(&t->rpg, t->polish.next_id));
    assert(companion_rpg_choose(t, 0, 0));
    assert(companion_adventure_start(t, 0, 0));
    assert(!progression_aquarium_move(t, 0));
    assert(t->rpg.contributors[0] == contributor && companion_rpg_contributor(t, contributor));
    assert(companion_adventure_start(t, 0, 0));
    t->companion.adventure_s = companion_adventure_total(&t->companion, 0);
    assert(progression_adventure_return(t));
    assert(companion_adventure_claim(t));
    int balance = t->sd_balance;
    assert(!progression_aquarium_move(t, 1));
    assert(t->companion.away && (t->companion.adventure_claimed & 1));
    t->companion.adventure_s = companion_adventure_total(&t->companion, 0);
    assert(progression_adventure_return(t));
    assert(!t->companion.away && !t->companion.adventure);
    assert(!companion_adventure_claim(t) && t->sd_balance == balance);
    reload(t); assert(t->n_fish == 2 && t->sd_balance == balance);
}
static void bank_child(tank_t *t, const char *name) {
    int fish = tank_add_fish(t, 0, 1); assert(fish == 2);
    tank_set_name(t, fish, name);
    progression_set_age(t, fish, 12345);
    t->companion.active[fish].bond = 73;
    t->companion.active[fish].wearable = WEAR_BOWTIE;
    t->companion.active[fish].wearables = 1u << (WEAR_BOWTIE - 1);
    t->companion.active[fish].wear_colors = 3;
    t->polish.active[fish].buddy = t->polish.active[0].id;
    t->polish.active[fish].friend_s = 120;
    assert(!progression_bank_move(t, -1, fish));
}
static void test_global_bank(tank_t *t) {
    for (int mode = GAME_MODE_NORMAL; mode <= GAME_MODE_HARD; mode++) {
        start(t, mode); t->sd_balance = 2000;
        tank_set_name(t, 0, "MOTHER"); tank_set_name(t, 1, "FATHER");
        bank_child(t, "ROVER");
        bank_fish_t fish = t->bank[0];
        companion_bond_t bond = t->companion.bank[0];
        fish_life_t life = t->polish.bank[0];
        fish_lineage_t line = t->story.bank[0];
        uint32_t mother = t->polish.active[0].id;
        assert(line.parent[0] == mother);
        tank_t before = *t;
        reject_write = true;
        assert(progression_aquarium_move(t, 1) && !memcmp(t, &before, sizeof *t));
        reload(t);
        if (t->bank_count != 1 || t->aquariums.count != 1)
            fprintf(stderr, "Bank rollback reload: mode %d, bank %u, tanks %u\n", mode, t->bank_count, t->aquariums.count);
        assert(t->bank_count == 1 && t->aquariums.count == 1);
        reject_write = false;
        assert(!progression_aquarium_move(t, 1)); progression_setup_done(t);
        assert(t->bank_count == 1 && t->n_fish == 2);
        assert(!memcmp(t->bank, &fish, sizeof fish));
        assert(!memcmp(t->companion.bank, &bond, sizeof bond));
        assert(!memcmp(t->polish.bank, &life, sizeof life));
        assert(!memcmp(t->story.bank, &line, sizeof line));
        assert(t->polish.active[0].id != mother && t->polish.active[0].id != life.id);
        assert(!strcmp(companion_story_name(t, (uint16_t)mother), "mother"));
        reload(t);
        before = *t; reject_write = true;
        assert(progression_bank_move(t, 0, -1) && !memcmp(t, &before, sizeof *t));
        reject_write = false;
        assert(!progression_bank_move(t, 0, -1));
        assert(t->bank_count == 0 && t->n_fish == 3 && t->polish.active[2].id == life.id);
        assert(!strcmp(t->fish[2].name, "rover") && progression_age_s(t, 2) == 12345);
        assert(!memcmp(&t->story.active[2], &line, sizeof line));
        assert(t->companion.active[2].bond == bond.bond && t->fish[2].wearable == bond.wearable);
        uint32_t resident = t->polish.active[0].id;
        assert(!progression_aquarium_move(t, 0));
        assert(t->bank_count == 0 && t->n_fish == 2 && t->polish.active[0].id == mother);
        assert(!progression_aquarium_move(t, 1));
        assert(t->n_fish == 3 && t->bank_count == 0 && t->polish.active[2].id == life.id);
        assert(!progression_bank_move(t, -1, 2));
        assert(!progression_bank_move(t, 0, 0)); /* Swap a local resident into the shared Bank. */
        assert(t->polish.active[0].id == life.id && t->polish.bank[0].id == resident);
        assert(!progression_aquarium_move(t, 0));
        assert(t->bank_count == 1 && t->polish.bank[0].id == resident);
        assert(!progression_bank_move(t, 0, -1));
        assert(t->polish.active[2].id == resident && t->bank_count == 0);
        reload(t);
        char *json = progression_backup_json(t); assert(json);
        char error[160];
        assert(progression_restore_json(json, strlen(json), true, error, sizeof error));
        free(json); reload(t);
        assert(!progression_aquarium_move(t, 1));
        assert(t->bank_count == 0 && t->polish.active[0].id == life.id);
        assert(companion_polish_valid(&t->polish, t->n_fish, t->bank_count, t->companion.away));
    }
}
static void test_legacy_banks(tank_t *t) {
    const uint32_t collection[2] = {1234, 5678};
    unsigned char old_bank[AQUARIUM_SAVE_BYTES];
    start(t, GAME_MODE_NORMAL);
    memcpy(t->aquariums.id, collection, sizeof collection);
    t->aquariums.count = 2; t->aquariums.active = 1;
    tank_set_name(t, 0, "PARENTB");
    bank_child(t, "OLDB");
    assert(progression_save(t) && saved_size == sizeof old_bank);
    memcpy(old_bank, saved, sizeof old_bank);
    start(t, GAME_MODE_NORMAL);
    memcpy(t->aquariums.id, collection, sizeof collection);
    t->aquariums.count = 2;
    tank_set_name(t, 0, "PARENTA");
    bank_child(t, "OLDA");
    assert(progression_save(t));
    memcpy(slots[1], old_bank, sizeof old_bank); slot_sizes[1] = sizeof old_bank;
    uint32_t original = t->polish.bank[0].id;
    tank_t before = *t;
    reject_slot_write = true;
    assert(progression_aquarium_move(t, 1) && !memcmp(t, &before, sizeof *t));
    reject_slot_write = false;
    reject_write = true;
    assert(progression_aquarium_move(t, 1) && !memcmp(t, &before, sizeof *t));
    reject_write = false;
    assert(!progression_aquarium_move(t, 1));
    assert(t->bank_count == 2 && t->polish.bank[0].id == original);
    assert(!strcmp(t->bank[0].name, "olda") && !strcmp(t->bank[1].name, "oldb"));
    assert(t->polish.bank[1].id != original && t->story.bank[1].parent[0] == t->polish.active[0].id);
    assert(!strcmp(companion_story_name(t, t->story.bank[0].parent[0]), "parenta"));
    uint32_t migrated = t->polish.bank[1].id;
    assert(!progression_bank_move(t, 1, -1));
    for (int i = 0; i < 3; i++) {
        assert(!progression_aquarium_move(t, 0));
        assert(t->bank_count == 1 && t->polish.bank[0].id == original);
        assert(!progression_aquarium_move(t, 1));
        assert(t->bank_count == 1 && t->n_fish == 3 && t->polish.active[2].id == migrated);
    }
    assert(!progression_bank_move(t, -1, 2));
    assert(!progression_aquarium_move(t, 0));
    assert(t->bank_count == 2);
    assert(!strcmp(companion_story_name(t, t->story.bank[1].parent[0]), "parentb"));
    /* A full shared Bank must not discard the legacy tank's stored fish. */
    start(t, GAME_MODE_NORMAL);
    memcpy(t->aquariums.id, collection, sizeof collection);
    t->aquariums.count = 2;
    memcpy(slots[1], old_bank, sizeof old_bank); slot_sizes[1] = sizeof old_bank;
    for (int i = 0; i < FISH_BANK_CAP; i++) bank_child(t, "FULL");
    before = *t;
    assert(strstr(progression_aquarium_move(t, 1), "MAKE ROOM"));
    assert(!memcmp(t, &before, sizeof *t));
    assert(!progression_bank_move(t, 0, -1));
    assert(!progression_aquarium_move(t, 1) && t->bank_count == FISH_BANK_CAP);
    /* Pre-memory snapshots still acquire stable life/family records. */
    start(t, GAME_MODE_NORMAL);
    memcpy(t->aquariums.id, collection, sizeof collection);
    t->aquariums.count = 2;
    memcpy(slots[1], old_bank, 4520); slot_sizes[1] = 4520;
    assert(!progression_aquarium_move(t, 1));
    assert(t->bank_count == 1 && !strcmp(t->bank[0].name, "oldb"));
    assert(companion_polish_valid(&t->polish, t->n_fish, t->bank_count, false));
    assert(companion_story_valid(&t->story, t->n_fish, t->bank_count, false, t->polish.next_id));
}
static void test_bank_restore_identities(tank_t *t) {
    start(t, GAME_MODE_NORMAL); t->sd_balance = 2000;
    bank_child(t, "BACKUP");
    uint32_t original = t->polish.bank[0].id;
    char *json = progression_backup_json(t); assert(json);
    assert(!progression_aquarium_move(t, 1)); progression_setup_done(t);
    char error[160];
    tank_t before = *t;
    assert(progression_restore_json(json, strlen(json), false, error, sizeof error));
    assert(!memcmp(t, &before, sizeof *t));
    reject_write = true;
    assert(!progression_restore_json(json, strlen(json), true, error, sizeof error));
    reject_write = false;
    reload(t); assert(t->polish.bank[0].id == original);
    uint32_t next = t->polish.next_id;
    for (int i = 0; i < 20; i++) {
        assert(progression_restore_json(json, strlen(json), true, error, sizeof error));
        reload(t);
        assert(t->polish.bank[0].id >= next);
        assert(t->story.bank[0].parent[0] == t->polish.active[0].id);
        assert(t->polish.next_id <= next + 4); /* Referenced IDs, not historical counter ranges. */
        next = t->polish.next_id;
    }
    assert(!progression_aquarium_move(t, 0));
    assert(t->bank_count == 1 && t->polish.bank[0].id != original);
    t->polish.next_id = UINT16_MAX + 1u; assert(progression_save(t));
    before = *t;
    assert(!progression_restore_json(json, strlen(json), false, error, sizeof error));
    assert(strstr(error, "identity limit") && !memcmp(t, &before, sizeof *t));
    assert(!progression_restore_json(json, strlen(json), true, error, sizeof error));
    reload(t); assert(t->polish.next_id == UINT16_MAX + 1u);
    free(json);
}
int main(void) {
    tank_t t;
    test_choice(&t); test_aquariums(&t); test_hard(&t); test_stories(&t); test_refunds(&t);
    test_wipe_commit(&t); test_traveler_rescue(&t);
    test_global_bank(&t); test_legacy_banks(&t); test_bank_restore_identities(&t);
    puts("Permanent modes, global Bank transfers, legacy Bank merges, stable families, shared progress, transactional restores and online-only Hard starvation passed.");
    return 0;
}
#endif
