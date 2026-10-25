#include "progression.h"
#include "companion.h"
#include "setup.h"
#include "cJSON.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned char saved[8192], slots[AQUARIUM_CAP][AQUARIUM_SAVE_BYTES];
static size_t saved_size, slot_sizes[AQUARIUM_CAP];
static bool card_ready = true, reject_write, reject_slot_write, reject_slot_read;
static int slot_writes, reject_slot_number;
static char error[128];
bool aquarium_port_ready(void) { return card_ready; }
void aquarium_port_id(uint32_t id[2]) { id[0] = 0x12345678; id[1] = 0x87654321; }
bool aquarium_port_read(const uint32_t id[2], int slot, void *data, size_t size) {
    (void)id;
    assert(size == AQUARIUM_SAVE_BYTES);
    if (reject_slot_read || !slot_sizes[slot]) return false;
    memset(data, 0, size);
    memcpy(data, slots[slot], slot_sizes[slot]);
    return true;
}
bool aquarium_port_write(const uint32_t id[2], int slot, const void *data, size_t size) {
    (void)id;
    slot_writes++;
    if (reject_slot_write || slot_writes == reject_slot_number) return false;
    assert(size == AQUARIUM_SAVE_BYTES);
    memcpy(slots[slot], data, size); slot_sizes[slot] = size;
    return true;
}
bool persist_port_load(void *buf, size_t max, size_t *got) {
    if (!saved_size) return false;
    *got = saved_size < max ? saved_size : max;
    memcpy(buf, saved, *got); return true;
}
bool persist_port_save(const void *buf, size_t size) {
    if (reject_write) return false;
    assert(size <= sizeof saved); memcpy(saved, buf, size); saved_size = size; return true;
}
bool persist_port_erase(void) { saved_size = 0; return true; }
int64_t clock_port_now_unix(void) { return 2000000000; }
const char *version_port_string(void) { return "test"; }
const setup_place_snapshot_t *setup_place_original(void) { return NULL; }

static void reload(tank_t *t) { tank_init(t, 456); progression_boot(t); }
static void wallet(const tank_t *t, int balance, int earned, uint32_t xp) {
    assert(t->sd_balance == balance && t->sd_earned == earned && t->journal.xp == xp);
}
static void unchanged(const tank_t *a, const tank_t *b) { assert(!memcmp(a, b, sizeof *a)); }

int main(void) {
    tank_t t, before;
    memset(&t, 0xa5, sizeof t); tank_init(&t, 12);
    creative_state_t empty = {0};
    assert(!memcmp(&t.creative, &empty, sizeof empty));
    memset(&t.creative, 0xa5, sizeof t.creative); companion_init(&t);
    assert(!memcmp(&t.creative, &empty, sizeof empty));
    saved_size = 0; progression_boot(&t); progression_setup_done(&t);
    assert(!progression_mode_select(&t, GAME_MODE_NORMAL));
    t.sd_balance = 37; t.sd_earned = 59; t.journal.xp = 12;
    tank_set_name(&t, 0, "HOME");
    assert(progression_save(&t));
    char *normal_json = progression_backup_json(&t); assert(normal_json);
    assert(progression_creative_add_fish(&t, 0));
    assert(progression_creative_remove_fish(&t, 0));
    before = t;
    assert(progression_creative_enter(&t)); unchanged(&before, &t);
    assert(progression_reset(&t, 123));
    assert(!progression_mode_chosen(&t) && !t.sd_balance && !t.journal.xp);
    before = t;
    reject_write = true;
    assert(progression_creative_enter(&t)); unchanged(&before, &t); reject_write = false;
    card_ready = false; /* Mode selection needs internal storage, not an SD slot. */
    assert(!progression_creative_enter(&t));
    card_ready = true;
    assert(saved_size == AQUARIUM_SAVE_BYTES);
    assert(progression_creative(&t) && t.aquariums.count == 1 && t.aquariums.active == 0);
    assert(progression_setup_pending());
    progression_setup_done(&t);
    t.sd_balance = 37; t.sd_earned = 59;
    assert(progression_save(&t));
    aquarium_summary_t summary;
    assert(progression_aquarium_summary(&t, 0, &summary) && summary.creative);
    wallet(&t, 37, 59, 0);
    char *creative_json = progression_backup_json(&t); assert(creative_json);
    assert(strstr(creative_json, "\"schema\":18") && strstr(creative_json, "\"creative\":"));
    reload(&t); assert(progression_creative(&t)); wallet(&t, 37, 59, 0);
    assert(!t.companion.collections && !t.companion.active[0].bond);
    assert(companion_cosmetic(&t, 3));
    assert(!companion_valid(&t.companion) && companion_valid_mode(&t.companion, true));
    for (int i = 0; i < 3; i++) assert(companion_trick(&t, 0, i));
    assert(!t.companion.active[0].bond && t.companion.active[0].tricks == 7);
    reload(&t);
    assert(t.companion.cosmetic == 3 && t.companion.active[0].tricks == 7);
    for (int i = 0; i < 3; i++) {
        assert(companion_collection_ready(&t, i));
        assert(companion_collection_claim(&t, i));
    }
    for (int i = 0; i < 5; i++) assert(companion_identity(&t, false));
    for (int i = 0; i < 4; i++) assert(companion_identity(&t, true));
    wallet(&t, 37, 59, 0);

    for (int i = 0; i < FISH_VARIETY_COUNT; i++) assert(journal_variety_unlocked(&t, i));
    for (int i = 0; i < JOURNAL_THEME_COUNT; i++) assert(journal_theme_unlocked(&t, i));
    assert(journal_set_theme(&t, JOURNAL_THEME_COUNT - 1)); wallet(&t, 37, 59, 0);
    before = t; reject_write = true;
    assert(progression_creative_add_fish(&t, FISH_VARIETY_COUNT - 1)); unchanged(&before, &t);
    assert(!progression_buy(&t, 0)); unchanged(&before, &t);
    reject_write = false;
    assert(progression_creative_add_fish(&t, -1));
    assert(!progression_creative_add_fish(&t, FISH_VARIETY_COUNT - 1));
    assert(t.n_fish == 3 && t.fish[2].variety == FISH_VARIETY_COUNT - 1 && t.fish[2].name[0]);
    assert(progression_newborn() == -1);
    assert(t.polish.active[2].id && companion_polish_valid(&t.polish, t.n_fish, t.bank_count, false));
    for (int i = 0; i < 3; i++) progression_set_age(&t, i, (float)(i + 1) * 100);
    before = t; reject_write = true;
    assert(progression_creative_remove_fish(&t, 0)); unchanged(&before, &t); reject_write = false;
    assert(progression_age_s(&t, 0) == 100 && progression_age_s(&t, 2) == 300);
    assert(!progression_creative_remove_fish(&t, 0) && t.n_fish == 2);
    assert(progression_age_s(&t, 0) == 200 && progression_age_s(&t, 1) == 300);
    assert(progression_creative_remove_fish(&t, 0));
    assert(!progression_creative_add_fish(&t, 0));
    assert(!progression_creative_add_fish(&t, 1));
    assert(!progression_creative_add_fish(&t, 2));
    assert(t.n_fish == 5 && progression_creative_add_fish(&t, 3));
    assert(!progression_bank_move(&t, -1, 0) && t.bank_count == 1);
    assert(!progression_creative_add_fish(&t, 3));
    assert(progression_bank_move(&t, 0, -1) && t.n_fish == 5 && t.bank_count == 1);
    assert(!progression_creative_remove_fish(&t, 4));
    assert(!progression_bank_move(&t, 0, -1) && t.n_fish == 5 && !t.bank_count);
    assert(progression_adventure_depart(&t, 0, 0));
    assert(t.n_fish == 4 && t.companion.away);
    assert(progression_creative_add_fish(&t, 3));
    t.companion.adventure_s = companion_adventure_total(&t.companion, 0);
    assert(progression_adventure_return(&t) && t.n_fish == 5);
    assert(companion_adventure_claim(&t));
    wallet(&t, 37, 59, 0);

    for (int i = 0; i < SD_ITEM_COUNT; i++) {
        if (progression_item_owned(&t, i)) continue;
        assert(progression_buy(&t, i));
        assert(progression_item_owned(&t, i));
        wallet(&t, 37, 59, 0);
    }
    for (int i = 0; i < JOURNAL_THEME_COUNT; i++) assert(journal_set_theme(&t, i));
    for (int i = 0; i < WEAR_COUNT; i++) {
        assert(companion_wearable(&t, 0, i)); wallet(&t, 37, 59, 0);
    }
    assert(progression_sell(&t, 2)); wallet(&t, 37, 59, 0);
    assert(progression_buy(&t, 2)); wallet(&t, 37, 59, 0);
    assert(!progression_sell_fish(&t, 0));
    assert(!progression_buy_fry(&t, 0));
    assert(!progression_game_reward(&t, 0, 10));
    assert(!companion_game_claim(&t, 0, 99, 2, 10, false));
    assert(!journal_claim(&t, 0));
    assert(!companion_adventure_claim(&t));
    progression_sd_grant(&t, 500); progression_sd_grant(&t, -100);
    t.player_feedings += 100; t.algae_colonies += 100; t.trim_px += 1000;
    for (int i = 0; i < t.n_fish; i++) t.fish[i].ms_bits |= MS_REACHED_JUV | MS_REACHED_ADULT;
    progression_tick(&t, 1); wallet(&t, 37, 59, 0);
    assert(progression_save(&t));
    char *json = progression_backup_json(&t); assert(json);
    assert(progression_restore_json(json, strlen(json), true, error, sizeof error));
    reload(&t); wallet(&t, 37, 59, 0); assert(progression_creative(&t));
    free(json);

    before = t; reject_write = true;
    assert(progression_aquarium_move(&t, 1)); unchanged(&before, &t); reject_write = false;
    reject_slot_write = true;
    assert(progression_aquarium_move(&t, 1)); unchanged(&before, &t); reject_slot_write = false;
    assert(!progression_aquarium_move(&t, 1)); progression_setup_done(&t);
    assert(progression_creative(&t) && t.aquariums.count == 2 && t.aquariums.active == 1);
    wallet(&t, 37, 59, 0);
    assert(!t.sd_unlocks && !t.companion.active[0].wearables);
    assert(!progression_restore_json(normal_json, strlen(normal_json), true, error, sizeof error));
    before = t; reject_slot_read = true;
    assert(progression_aquarium_move(&t, 0)); unchanged(&before, &t); reject_slot_read = false;
    assert(!progression_aquarium_move(&t, 0));
    assert(progression_creative_enter(&t)); wallet(&t, 37, 59, 0);
    assert(progression_restore_json(creative_json, strlen(creative_json), true, error, sizeof error));
    reload(&t); wallet(&t, 37, 59, 0);
    assert(progression_reset(&t, 124));
    assert(!progression_mode_select(&t, GAME_MODE_NORMAL));
    assert(!progression_restore_json(creative_json, strlen(creative_json), true, error, sizeof error));
    free(creative_json);
    assert(progression_restore_json(normal_json, strlen(normal_json), true, error, sizeof error));
    free(normal_json); reload(&t); wallet(&t, 37, 59, 12);

    /* Previous exact binary tail and schema 14 remain ordinary aquariums. */
    assert(progression_save(&t)); saved_size = 5912; reload(&t);
    assert(!progression_creative(&t) && !strcmp(t.fish[0].name, "home"));
    json = progression_backup_json(&t); assert(json);
    cJSON *root = cJSON_Parse(json); free(json); assert(root);
    cJSON_DeleteItemFromObjectCaseSensitive(root, "creative");
    cJSON_DeleteItemFromObjectCaseSensitive(root, "profile");
    cJSON_DeleteItemFromObjectCaseSensitive(root, "care");
    cJSON_DeleteItemFromObjectCaseSensitive(root, "inventory");
    cJSON_DeleteItemFromObjectCaseSensitive(root, "mussel");
    cJSON_DeleteItemFromObjectCaseSensitive(root, "game_income");
    cJSON_DeleteItemFromObjectCaseSensitive(root, "games_extra");
    cJSON_SetNumberValue(cJSON_GetObjectItemCaseSensitive(root, "schema"), 14);
    json = cJSON_PrintUnformatted(root); cJSON_Delete(root);
    assert(progression_restore_json(json, strlen(json), true, error, sizeof error)); free(json);
    reload(&t); assert(!progression_creative(&t));
    /* Unknown/partial Creative tails must fail instead of converting a tank. */
    json = progression_backup_json(&t); assert(json);
    root = cJSON_Parse(json); free(json); assert(root);
    cJSON *mode = cJSON_GetObjectItemCaseSensitive(root, "creative");
    cJSON_SetNumberValue(cJSON_GetObjectItemCaseSensitive(mode, "enabled"), 1);
    json = cJSON_PrintUnformatted(root); cJSON_Delete(root); assert(json);
    assert(!progression_restore_json(json, strlen(json), true, error, sizeof error)); free(json);
    /* Old SD snapshots zero-fill their new tail before activation. */
    progression_setup_done(&t); t.sd_balance = AQUARIUM_PRICE;
    assert(!progression_aquarium_move(&t, 1)); progression_setup_done(&t);
    assert(!progression_aquarium_move(&t, 0));
    slot_sizes[1] = 5912;
    assert(!progression_aquarium_move(&t, 1) && !progression_creative(&t));
    before = t;
    t.aquariums.count = AQUARIUM_CAP;
    for (int i = 2; i < AQUARIUM_CAP; i++) {
        memcpy(slots[i], slots[0], sizeof slots[i]); slot_sizes[i] = 5912;
    }
    assert(progression_creative_enter(&t) && !progression_creative(&t));
    assert(t.aquariums.count == AQUARIUM_CAP);
    t = before;
    puts("Fixed first-run Creative: free editing, shared-mode aquariums, wipe boundaries, rollback and migrations passed");
    return 0;
}
