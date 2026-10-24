#include "progression.h"
#include "companion.h"
#include "advisor.h"
#include "aquarium_store.h"
#include "render.h"
#include "home_navigation.h"
#include "setup.h"
#include "notice.h"
#include "icons.h"
#include "cJSON.h"
#include "update.h"
#include "version.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

static unsigned char saved[8192];
static size_t saved_size;
static int writes;
static bool reject_write;
static bool reject_erase;
static unsigned char slots[AQUARIUM_CAP][AQUARIUM_SAVE_BYTES];
static bool slot_exists[AQUARIUM_CAP], card_ready = true, reject_slot_read, reject_slot_write;
static int slot_write_count, reject_slot_number;
bool aquarium_port_ready(void) { return card_ready; }
void aquarium_port_id(uint32_t id[2]) { id[0] = 0x12345678; id[1] = 0x87654321; }
bool aquarium_port_read(const uint32_t id[2], int slot, void *data, size_t size) {
    (void)id;
    if (reject_slot_read || !slot_exists[slot]) return false;
    assert(size == AQUARIUM_SAVE_BYTES); memcpy(data, slots[slot], size); return true;
}
bool aquarium_port_write(const uint32_t id[2], int slot, const void *data, size_t size) {
    (void)id; slot_write_count++;
    if (reject_slot_write || slot_write_count == reject_slot_number) return false;
    assert(size == AQUARIUM_SAVE_BYTES); memcpy(slots[slot], data, size); slot_exists[slot] = true; return true;
}
bool persist_port_load(void *buf, size_t max, size_t *got) {
    if (!saved_size) return false;
    *got = saved_size < max ? saved_size : max;
    memcpy(buf, saved, *got); return true;
}
bool persist_port_save(const void *buf, size_t len) {
    if (reject_write) return false;
    assert(len <= sizeof saved);
    memcpy(saved, buf, len); saved_size = len; writes++; return true;
}
bool persist_port_erase(void) { if (reject_erase) return false; saved_size = 0; return true; }
void test_decor_scenery_reject_write(bool reject) { reject_write = reject; }
static int64_t test_unix = 2000000000;
int64_t clock_port_now_unix(void) { return test_unix; }
const char *version_port_string(void) { return "test"; }
const icon_t *icons_resolve(const icon_t *icon) {
    static uint16_t rgb[4096]; static uint8_t alpha[4096];
    static icon_t view;
    for (int i = 0; i < icon->w * icon->h; i++) {
        rgb[i] = i % 2 ? 0xf800 : 0x07e0;
        alpha[i] = 255;
    }
    view = *icon; view.rgb = rgb; view.a = alpha; return &view;
}
static char error[128];
static float boot_enlarged(tank_t *t) {
    bool fresh = saved_size == 0;
    float hours = progression_boot(t);
    if (fresh && !progression_mode_chosen(t)) assert(!progression_mode_select(t, GAME_MODE_NORMAL));
    if (fresh && t->companion.display_compact) {
        tank_reframe(t, false);
        t->companion.display_compact = 0;
    }
    return hours;
}
/* Narrow hook for test_decor_scenery.c: boot a fresh tank from the harness's persisted bytes. */
size_t test_decor_scenery_reload(tank_t *t, uint32_t seed) { tank_init(t, seed); boot_enlarged(t); return saved_size; }

static void test_normal_default(void) {
    tank_t tank;
    saved_size = 0;
    tank_resolution(false);
    tank_init(&tank, 987);
    assert(progression_boot(&tank) < 0);
    assert(TANK_W == 320 && TANK_H == 240 && tank.companion.display_compact);
    assert(progression_save(&tank));
    tank_resolution(false);
    tank_init(&tank, 123);
    progression_boot(&tank);
    assert(TANK_W == 320 && tank.companion.display_compact);
    assert(companion_compact(&tank, false));
    tank_init(&tank, 456);
    progression_boot(&tank);
    assert(TANK_W == 240 && !tank.companion.display_compact);
    assert(progression_reset(&tank, 789));
    assert(TANK_W == 320 && tank.companion.display_compact);
    saved_size = 0;
    tank_resolution(false);
}

static void test_mode_care_notices(void) {
    tank_t t;
    saved_size = 0; tank_init(&t, 781); progression_boot(&t);
    assert(!progression_mode_select(&t, GAME_MODE_HARD));
    progression_setup_done(&t); notice_sync(&t);
    t.fish[0].hunger = 10; t.profile.starvation_s[0] = 5 * 3600;
    notice_tick(&t, 0.01f, false);
    assert(notice_current() && notice_current()->kind == NOTICE_CARE_STATUS);
    assert(strstr(progression_hard_status(&t), "STARVATION RISK"));
    notice_dismiss(); notice_tick(&t, 2, false); notice_tick(&t, 0, false);
    assert(!notice_current() && !notice_pending());
    t.fish[0].hunger = 0; progression_tick(&t, 0.01f); notice_tick(&t, 0, false);
    assert(!progression_hard_status(&t));
    t.fish[0].hunger = 10; t.profile.starvation_s[0] = 6 * 3600;
    progression_tick(&t, 0.01f); notice_tick(&t, 0, false);
    assert(t.n_fish == 1 && notice_current() && notice_current()->kind == NOTICE_CARE_STATUS);
    assert(strstr(progression_hard_status(&t), "DIED"));
    t.fish[0].hunger = 10; t.profile.starvation_s[0] = 5 * 3600;
    assert(strstr(progression_hard_status(&t), "STARVATION RISK"));
    t.profile.starvation_s[0] = 6 * 3600;
    progression_tick(&t, 0.01f); notice_sync(&t); notice_tick(&t, 0, true);
    assert(!t.n_fish && !notice_current() && notice_pending() == 1);
    notice_tick(&t, 0, false);
    assert(notice_current() && notice_current()->kind == NOTICE_CARE_STATUS);
    for (int compact = 0; compact < 2; compact++) {
        tank_reframe(&t, compact != 0);
        uint16_t *fb = calloc((size_t)TANK_W * TANK_H, sizeof *fb); assert(fb);
        render_notice(&t, fb, TANK_W, NOTICE_CARE_STATUS, -1, 0, 1);
        render_life_open(); render_life(&t, fb, TANK_W);
        assert(render_life_tap(&t, render_frame_x(100), render_frame_y(100)) == HOME_RECOVERY);
        render_settings_open_care(); render_settings(&t, fb, TANK_W, 100);
        free(fb);
    }
    int value;
    render_settings_touch(&t, render_frame_x(200), render_frame_y(308), true, &value);
    assert(render_settings_touch(&t, render_frame_x(200), render_frame_y(308), false, &value) == SET_TAP_RECOVERED);
    assert(t.n_fish == 2 && !progression_hard_status(&t));
    notice_sync(&t); saved_size = 0; tank_resolution(false);
    puts("PASS: Hard starvation/death notices, deduplication, blocked queue, warning priority and free Home recovery at both scales");
}

void net_sim_advance(float dt);
static void invalid(const char *text, size_t len) {
    int before = writes;
    assert(!progression_restore_json(text, len, true, error, sizeof error));
    assert(error[0] && writes == before);
}
static void settings_section(tank_t *tank, int index) {
    int value;
    render_settings_open();
    float x = render_frame_x(100 + (index % 2) * 212), y = render_frame_y(100 + (index / 2) * 80);
    render_settings_touch(tank, x, y, true, &value);
    render_settings_touch(tank, x, y, false, &value);
}
static void invalid_object(cJSON *root) {
    char *text = cJSON_PrintUnformatted(root); assert(text);
    invalid(text, strlen(text)); free(text); cJSON_Delete(root);
}
static void replace_number(cJSON *object, const char *key, double value) {
    assert(cJSON_ReplaceItemInObjectCaseSensitive(object, key, cJSON_CreateNumber(value)));
    if (!strcmp(key, "schema") && value < 17) cJSON_DeleteItemFromObjectCaseSensitive(object, "care");
    if (!strcmp(key, "schema") && value < 5) cJSON_DeleteItemFromObjectCaseSensitive(object, "bank");
    if (!strcmp(key, "schema") && value < 6) cJSON_DeleteItemFromObjectCaseSensitive(object, "companion");
    if (!strcmp(key, "schema") && value < 11) cJSON_DeleteItemFromObjectCaseSensitive(object, "polish");
    if (!strcmp(key, "schema") && value < 12) cJSON_DeleteItemFromObjectCaseSensitive(object, "story");
    if (!strcmp(key, "schema") && value < 13) cJSON_DeleteItemFromObjectCaseSensitive(object, "games");
    if (!strcmp(key, "schema") && value < 14) cJSON_DeleteItemFromObjectCaseSensitive(object, "rpg");
    if (!strcmp(key, "schema") && value < 15) cJSON_DeleteItemFromObjectCaseSensitive(object, "creative");
    if (!strcmp(key, "schema") && value < 16) {
        cJSON_DeleteItemFromObjectCaseSensitive(object, "profile");
        cJSON_DeleteItemFromObjectCaseSensitive(object, "games_extra");
    }
    if (!strcmp(key, "schema") && value < 10)
        cJSON_DeleteItemFromObjectCaseSensitive(cJSON_GetObjectItemCaseSensitive(object, "companion"), "display_compact");
}
static void remove_journal_fields(cJSON *obj) {
    const char *fields[] = {"journal_xp", "journal_claimed", "journal_seen", "journal_grown", "journal_baseline",
                            "journal_initialized", "journal_trust", "journal_population", "journal_theme"};
    for (size_t i = 0; i < sizeof fields / sizeof fields[0]; i++) cJSON_DeleteItemFromObjectCaseSensitive(obj, fields[i]);
}
static int add_bank_candidate(tank_t *t, int number) {
    int slot = tank_add_fish(t, 0, 1); assert(slot >= 0);
    char name[8]; snprintf(name, sizeof name, "fish%02d", number);
    memset(t->fish[slot].name, 0, sizeof t->fish[slot].name);
    tank_set_name(t, slot, name); progression_set_age(t, slot, (24.0f + number) * 3600);
    fish_t *f = &t->fish[slot];
    f->variety = (uint8_t)(number % FISH_VARIETY_COUNT); f->pattern = (uint8_t)(number % FISH_PATTERN_COUNT);
    tank_set_look(t, slot, FISH_BODY_COLORS[number % FISH_COLOR_N], FISH_MARKING_COLORS[number % FISH_COLOR_N]);
    f->trust = 7.25f; f->hunger = 3.25f; f->bold = 0.65f; f->sociable = 0.35f;
    f->drift_acc = 0.123f; f->eaten = 50; f->eaten_player = 40;
    f->ms_bits |= MS_ARRIVED; f->ms_seen = f->ms_bits; t->sd_paid_fish[slot] = f->ms_bits;
    return slot;
}
static void test_bank(void) {
    tank_t t, loaded; saved_size = 0; tank_init(&t, 17); boot_enlarged(&t);
    assert(progression_bank_move(&t, -1, 0)); /* Setup/new fry must be finished. */
    progression_setup_done(&t);
    assert(progression_bank_move(&t, -1, 0) && !t.bank_count); /* Two must stay. */
    assert(progression_bank_move(&t, -1, -1) && progression_bank_move(&t, 0, -1));
    int slot = add_bank_candidate(&t, 0);
    fish_t original = t.fish[slot]; float age = progression_age_s(&t, slot);
    tank_t before = t; int old_writes = writes;
    reject_write = true;
    assert(progression_bank_move(&t, -1, slot));
    assert(!memcmp(&t, &before, sizeof t) && progression_age_s(&t, slot) == age && writes == old_writes);
    reject_write = false;
    assert(!progression_bank_move(&t, -1, slot) && t.n_fish == 2 && t.bank_count == 1);
    assert(!strcmp(t.bank[0].name, original.name) && t.bank[0].fish.age_s == age && bank_variety(&t.bank[0]) == original.variety);
    bank_fish_t frozen = t.bank[0];
    progression_tick(&t, 3600);
    assert(!memcmp(&t.bank[0], &frozen, sizeof frozen)); /* No aging/care or reward changes in the bank. */
    before = t; reject_write = true;
    assert(progression_bank_move(&t, 0, -1) && !memcmp(&before, &t, sizeof t)); reject_write = false;
    assert(!progression_bank_move(&t, 0, -1) && t.n_fish == 3 && !t.bank_count);
    fish_t *f = &t.fish[2];
    assert(!strcmp(f->name, original.name) && f->trust == original.trust && f->size == original.size &&
           f->bold == original.bold && f->sociable == original.sociable && f->bold0 == original.bold0 &&
           f->hunger == original.hunger && f->energy == original.energy && f->stress == original.stress &&
           f->color == original.color && f->accent == original.accent && f->fin == original.fin &&
           f->variety == original.variety && f->pattern == original.pattern && f->eaten == original.eaten &&
           f->ms_bits == original.ms_bits && f->ms_seen == original.ms_seen && f->drift_acc == original.drift_acc);
    assert(progression_age_s(&t, 2) == age && t.sd_paid_fish[2] == frozen.paid);
    assert(!progression_bank_move(&t, -1, 2));
    for (int i = 1; i < FISH_BANK_CAP; i++) {
        slot = add_bank_candidate(&t, i);
        assert(!progression_bank_move(&t, -1, slot));
    }
    assert(t.bank_count == FISH_BANK_CAP && t.n_fish == 2);
    add_bank_candidate(&t, 21); before = t;
    assert(progression_bank_move(&t, -1, 2) && !memcmp(&before, &t, sizeof t)); /* Bank full. */
    while (t.n_fish < POP_CAP) add_bank_candidate(&t, 21 + t.n_fish);
    before = t;
    assert(progression_bank_move(&t, 0, -1) && !memcmp(&before, &t, sizeof t)); /* Tank full. */
    reject_write = true; assert(progression_bank_move(&t, 0, 1) && !memcmp(&before, &t, sizeof t)); reject_write = false;
    uint32_t paid = t.sd_paid_fish[1]; fish_t outgoing = t.fish[1];
    assert(!progression_bank_move(&t, 0, 1) && t.n_fish == POP_CAP && t.bank_count == FISH_BANK_CAP);
    assert(!strcmp(t.fish[1].name, frozen.name) && !strcmp(t.bank[0].name, outgoing.name) && t.bank[0].paid == paid);
    assert(t.sd_balance == before.sd_balance && t.sd_earned == before.sd_earned);
    assert(progression_save(&t) && saved_size == AQUARIUM_SAVE_BYTES);
    tank_init(&loaded, 5); boot_enlarged(&loaded);
    assert(loaded.bank_count == FISH_BANK_CAP && !memcmp(loaded.bank, t.bank, sizeof t.bank));
    char *json = progression_backup_json(&t); assert(json && strlen(json) < 32768);
    assert(progression_restore_json(json, strlen(json), false, error, sizeof error));
    assert(progression_restore_json(json, strlen(json), true, error, sizeof error));
    tank_init(&loaded, 3); boot_enlarged(&loaded);
    assert(loaded.bank_count == t.bank_count && !memcmp(loaded.bank, t.bank, sizeof t.bank));
    /* Root order and whitespace must not affect incremental bank parsing. */
    cJSON *reordered = cJSON_Parse(json), *bank_first = cJSON_CreateObject();
    cJSON_AddItemToObject(bank_first, "bank", cJSON_DetachItemFromObjectCaseSensitive(reordered, "bank"));
    while (reordered->child) {
        cJSON *entry = cJSON_DetachItemViaPointer(reordered, reordered->child);
        assert(cJSON_AddItemToObject(bank_first, entry->string, entry));
    }
    cJSON_Delete(reordered);
    char *pretty = cJSON_Print(bank_first); cJSON_Delete(bank_first); assert(pretty);
    assert(progression_restore_json(pretty, strlen(pretty), false, error, sizeof error)); free(pretty);
    size_t json_len = strlen(json);
    char *bad = malloc(json_len + 2); assert(bad); memcpy(bad, json, json_len);
    bad[json_len - 1] = ','; bad[json_len] = '}'; bad[json_len + 1] = 0;
    invalid(bad, json_len + 1);
    strcpy(bad, json); char *comma = strstr(bad, ",\"bank\":["); assert(comma);
    memmove(comma, comma + 1, strlen(comma)); invalid(bad, strlen(bad)); free(bad);
    cJSON *root = cJSON_Parse(json), *bank = cJSON_GetObjectItemCaseSensitive(root, "bank");
    cJSON_AddItemToArray(bank, cJSON_Duplicate(cJSON_GetArrayItem(bank, 0), true)); invalid_object(root);
    root = cJSON_Parse(json); bank = cJSON_GetObjectItemCaseSensitive(root, "bank");
    replace_number(cJSON_GetArrayItem(bank, 0), "appearance", 65535); invalid_object(root);
    root = cJSON_Parse(json); bank = cJSON_GetObjectItemCaseSensitive(root, "bank");
    replace_number(cJSON_GetObjectItemCaseSensitive(cJSON_GetArrayItem(bank, 0), "fish"), "age_s", -1); invalid_object(root);
    root = cJSON_Parse(json); bank = cJSON_GetObjectItemCaseSensitive(root, "bank");
    cJSON_AddNumberToObject(cJSON_GetArrayItem(bank, 0), "unknown", 1); invalid_object(root);
    root = cJSON_Parse(json); replace_number(root, "schema", 4);
    char *old = cJSON_PrintUnformatted(root); cJSON_Delete(root); assert(old);
    assert(progression_restore_json(old, strlen(old), true, error, sizeof error));
    saved_size = 2040; tank_init(&loaded, 3); boot_enlarged(&loaded);
    assert(!loaded.bank_count && loaded.journal.xp == t.journal.xp && loaded.fish[1].variety == t.fish[1].variety);
    free(old); free(json);
    /* Actual UI paths: store/cancel/confirm, choose/swap at the two-fish minimum, then return. */
    tank_init(&t, 3); saved_size = 0; boot_enlarged(&t); progression_setup_done(&t); add_bank_candidate(&t, 9);
    render_bank_open(); uint16_t frame[TANK_W * TANK_H];
    render_bank(&t, frame, TANK_W);
    render_bank_tap(&t, render_frame_x(300), render_frame_y(330));
    render_bank_tap(&t, render_frame_x(350), render_frame_y(BANK_ROW_Y + 2 * BANK_ROW_DY + 10));
    render_bank(&t, frame, TANK_W);
    render_bank_tap(&t, render_frame_x(120), render_frame_y(240)); assert(!t.bank_count);
    render_bank_tap(&t, render_frame_x(350), render_frame_y(BANK_ROW_Y + 2 * BANK_ROW_DY + 10));
    render_bank_tap(&t, render_frame_x(300), render_frame_y(240)); assert(t.bank_count == 1 && t.n_fish == 2);
    render_bank_tap(&t, render_frame_x(350), render_frame_y(BANK_ROW_Y + 10)); render_bank(&t, frame, TANK_W);
    render_bank_tap(&t, render_frame_x(300), render_frame_y(240)); /* Choose swap. */
    render_bank_tap(&t, render_frame_x(350), render_frame_y(BANK_ROW_Y + 10));
    render_bank_tap(&t, render_frame_x(300), render_frame_y(240)); assert(t.n_fish == 2 && t.bank_count == 1);
    render_bank_tap(&t, render_frame_x(350), render_frame_y(BANK_ROW_Y + 10));
    render_bank_tap(&t, render_frame_x(100), render_frame_y(240)); /* Return. */
    render_bank_tap(&t, render_frame_x(300), render_frame_y(240)); assert(t.n_fish == 3 && !t.bank_count);
    assert(!progression_bank_move(&t, -1, 2));
    assert(progression_reset(&t, 5) && !t.bank_count);
    for (int i = 0; i < FISH_BANK_CAP; i++) assert(!t.bank[i].name[0]);
    progression_setup_done(&t);
    add_bank_candidate(&t, 1); add_bank_candidate(&t, 2);
    t.fish[3].parent_a = 0; t.fish[3].parent_b = 2;
    before = t; double ages[N_FISH_MAX];
    for (int i = 0; i < t.n_fish; i++) ages[i] = progression_age_s(&t, i);
    assert(!progression_bank_move(&t, -1, 0) && t.n_fish == 3);
    for (int i = 0; i < t.n_fish; i++) {
        assert(!strcmp(t.fish[i].name, before.fish[i + 1].name));
        assert(t.sd_paid_fish[i] == before.sd_paid_fish[i + 1] && progression_age_s(&t, i) == ages[i + 1]);
    }
    assert(t.fish[2].parent_a == -1 && t.fish[2].parent_b == 1);
    assert(!progression_bank_move(&t, 0, 1) && t.fish[2].parent_b == -1);
    printf("PASS: 20-slot bank paused care, exact fish identity, limits, swap at full tank, rollback, schema 4 migration, JSON and UI (tank RAM %zu bytes)\n", sizeof t);
}
static void test_aquariums(void) {
    tank_t t, before, original;
    saved_size = 0; memset(slot_exists, 0, sizeof slot_exists);
    tank_init(&t, 42); boot_enlarged(&t);
    assert(t.aquariums.count == 1 && t.aquariums.active == 0);
    assert(progression_aquarium_move(&t, 1)); /* Name the first fry before leaving. */
    progression_setup_done(&t); t.sd_balance = 499;
    assert(progression_aquarium_rename(&t, "") && progression_aquarium_rename(&t, "   "));
    assert(progression_aquarium_rename(&t, "NAME IS TOO LONG!") && progression_aquarium_rename(&t, "BAD\nNAME"));
    assert(!progression_aquarium_rename(&t, "CORAL GARDEN"));
    before = t; reject_write = true;
    assert(progression_aquarium_rename(&t, "LOST") && !memcmp(&t, &before, sizeof t)); reject_write = false;
    assert(progression_aquarium_move(&t, 1) && t.sd_balance == 499);
    t.sd_balance = 750; t.sd_earned = 1000; t.journal.xp = 580;
    t.aquariums.clock_theme = CLOCK_ANALOG; t.aquariums.clock_flags = CLOCK_FLAGS_MASK;
    t.aquariums.clock_utc_quarters = -14;
    t.aquariums.clock_layout = (CLOCK_BOTTOM_RIGHT << 2) | CLOCK_MEDIUM;
    t.aquariums.clock_delay = CLOCK_DELAY_MANUAL | (6 << CLOCK_SECONDARY_SHIFT);
    tank_set_name(&t, 0, "keeper"); progression_set_age(&t, 0, 25 * 3600);
    int fish = add_bank_candidate(&t, 9); assert(!progression_bank_move(&t, -1, fish));
    original = t; before = t;
    card_ready = false; assert(progression_aquarium_move(&t, 1) && !memcmp(&before, &t, sizeof t)); card_ready = true;
    reject_slot_write = true;
    assert(progression_aquarium_move(&t, 1) && !memcmp(&before, &t, sizeof t)); reject_slot_write = false;
    reject_slot_number = slot_write_count + 2;
    assert(progression_aquarium_move(&t, 1) && !memcmp(&before, &t, sizeof t)); reject_slot_number = 0;
    reject_write = true; assert(progression_aquarium_move(&t, 1) && !memcmp(&before, &t, sizeof t)); reject_write = false;
    double age = progression_age_s(&t, 0);
    assert(!progression_aquarium_move(&t, 1));
    assert(t.aquariums.count == 2 && t.aquariums.active == 1 && t.n_fish == 2);
    aquarium_summary_t summary;
    assert(progression_aquarium_summary(&t, 0, &summary));
    assert(summary.fish == original.n_fish && summary.bank == original.bank_count);
    assert(summary.theme == original.journal.theme && summary.last_visit == test_unix);
    reject_slot_read = true;
    assert(!progression_aquarium_summary(&t, 0, &summary));
    reject_slot_read = false;
    assert(progression_setup_pending() && t.sd_balance == 250 && t.sd_earned == 1000 && !t.sd_unlocks && t.bank_count == original.bank_count && t.journal.xp == original.journal.xp);
    assert(!strcmp(t.aquariums.name, "TANK 2"));
    assert(t.aquariums.clock_theme == CLOCK_OFF && !t.aquariums.clock_flags && !t.aquariums.clock_utc_quarters);
    assert(!t.aquariums.clock_layout && !t.aquariums.clock_delay);
    assert(progression_age_s(&t, 0) == 0);
    assert(progression_aquarium_move(&t, 0));
    progression_setup_done(&t); tank_set_name(&t, 0, "second");
    t.aquariums.clock_theme = CLOCK_NEON; t.aquariums.clock_utc_quarters = 23;
    assert(!progression_aquarium_rename(&t, "SHARK BAY"));
    char tank_label[16];
    assert(progression_aquarium_label(&t, 0, tank_label) && !strcmp(tank_label, "CORAL GARDEN"));
    progression_sd_grant(&t, 37);
    progression_tick(&t, 3600); /* Original tank remains paused even while this one ages. */
    before = t; reject_slot_read = true;
    assert(progression_aquarium_move(&t, 0) && !memcmp(&before, &t, sizeof t)); reject_slot_read = false;
    slots[0][0] ^= 1;
    assert(progression_aquarium_move(&t, 0) && !memcmp(&before, &t, sizeof t));
    slots[0][0] ^= 1;
    reject_write = true; assert(progression_aquarium_move(&t, 0) && !memcmp(&before, &t, sizeof t)); reject_write = false;
    assert(!progression_aquarium_move(&t, 0));
    assert(t.aquariums.count == 2 && t.aquariums.active == 0 && t.sd_balance == before.sd_balance && t.sd_earned == before.sd_earned);
    assert(!strcmp(t.aquariums.name, "CORAL GARDEN"));
    assert(t.aquariums.clock_theme == CLOCK_ANALOG && t.aquariums.clock_flags == CLOCK_FLAGS_MASK && t.aquariums.clock_utc_quarters == -14);
    assert(t.aquariums.clock_layout == ((CLOCK_BOTTOM_RIGHT << 2) | CLOCK_MEDIUM) && t.aquariums.clock_delay == (CLOCK_DELAY_MANUAL | (6 << CLOCK_SECONDARY_SHIFT)));
    assert(!strcmp(t.fish[0].name, "keeper") && progression_age_s(&t, 0) == age);
    assert(t.journal.xp == before.journal.xp && t.bank_count == 1 && !memcmp(t.bank, original.bank, sizeof t.bank));
    assert(!progression_aquarium_move(&t, 1) && !strcmp(t.fish[0].name, "second"));
    assert(t.aquariums.clock_theme == CLOCK_NEON && t.aquariums.clock_utc_quarters == 23);
    assert(!strcmp(t.aquariums.name, "SHARK BAY") && t.sd_balance == before.sd_balance);
    tank_init(&t, 41); boot_enlarged(&t);
    assert(!strcmp(t.aquariums.name, "SHARK BAY") && t.sd_balance == before.sd_balance);
    for (int target = 2; target < AQUARIUM_CAP; target++) {
        progression_setup_done(&t); progression_newborn_done(&t); t.sd_balance = AQUARIUM_PRICE;
        assert(!progression_aquarium_move(&t, target));
        assert(t.aquariums.count == target + 1 && t.aquariums.active == target);
    }
    progression_setup_done(&t);
    before = t; assert(progression_aquarium_move(&t, AQUARIUM_CAP) && !memcmp(&t, &before, sizeof t));
    assert(!progression_aquarium_move(&t, 0) && t.aquariums.count == AQUARIUM_CAP);
    aquarium_state_t owned = t.aquariums;
    assert(progression_save(&t));
    char *json = progression_backup_json(&t); assert(json);
    assert(progression_restore_json(json, strlen(json), true, error, sizeof error)); free(json);
    tank_init(&t, 41); boot_enlarged(&t);
    assert(!memcmp(&t.aquariums, &owned, sizeof owned)); /* JSON restores do not clone collection identity. */
    t.sd_balance = 123; t.sd_earned = 456;
    assert(progression_reset(&t, 3) && !t.bank_count);
    assert(t.aquariums.count == 1 && !t.aquariums.active && memcmp(&t.aquariums, &owned, sizeof owned));
    assert(!t.sd_balance && !t.sd_earned && !t.journal.xp && !progression_mode_chosen(&t));
    assert(!progression_mode_select(&t, GAME_MODE_NORMAL));
    progression_setup_done(&t);
    before = t;
    assert(progression_aquarium_move(&t, 1) && !memcmp(&before, &t, sizeof t));
    t.sd_balance = AQUARIUM_PRICE;
    assert(!progression_aquarium_move(&t, 1) && t.aquariums.count == 2);
    assert(strcmp(t.fish[0].name, "second")); /* Old collection snapshots cannot reappear after wiping. */
    /* A pre-aquarium binary save migrates to one original tank. */
    assert(progression_save(&t)); saved_size = 3968;
    tank_init(&t, 1); boot_enlarged(&t);
    assert(t.aquariums.count == 1 && t.aquariums.active == 0);
    /* The page confirms a purchase and cancels without writes. */
    saved_size = 0; tank_init(&t, 7); boot_enlarged(&t); progression_setup_done(&t); t.sd_balance = 500;
    before = t; int old_writes = writes;
    uint16_t pixels[TANK_W * TANK_H];
    render_aquariums_open(&t); render_aquariums(&t, pixels, TANK_W);
    render_aquariums_tap(&t, render_frame_x(80), render_frame_y(338));
    render_aquariums(&t, pixels, TANK_W);
    render_aquariums_tap(&t, render_frame_x(140), render_frame_y(250));
    render_aquariums_tap(&t, render_frame_x(350), render_frame_y(250));
    assert(!strcmp(t.aquariums.name, "UANK 1")); /* T -> U through the actual editor. */
    before = t; old_writes = writes;
    render_shop_leave();
    assert(render_shop_tap(&t, render_frame_x(250), render_frame_y(326)) == SHOP_TAP_TANKS);
    render_aquariums_tap(&t, render_frame_x(350), render_frame_y(160));
    render_aquariums(&t, pixels, TANK_W);
    render_aquariums_tap(&t, render_frame_x(120), render_frame_y(250));
    assert(!memcmp(&before, &t, sizeof t) && old_writes == writes);
    render_aquariums_tap(&t, render_frame_x(350), render_frame_y(160));
    assert(render_aquariums_tap(&t, render_frame_x(300), render_frame_y(250)) == 2);
    assert(t.aquariums.count == 2 && t.aquariums.active == 1);
    assert(t.sd_balance == 0 && progression_setup_pending());
    assert(render_text_w("WIPE THIS TANK AND ITS BANK", 2) <= RENDER_CONFIRM_W);
    assert(render_text_w("OTHER TANKS / SD FILES KEPT", 2) <= RENDER_CONFIRM_W);
    /* Real on-disk storage keeps the previous copy when an alternate is truncated. */
    const char *directory = getenv("POCKET_TANK_TEST_SLOTS"); assert(directory);
    uint32_t id[2] = {1, 2};
    unsigned char first[64], second[64], readback[64];
    memset(first, 0x12, sizeof first); memset(second, 0xa5, sizeof second);
    assert(aquarium_store_write(directory, id, 0, first, sizeof first));
    assert(aquarium_store_write(directory, id, 0, second, sizeof second));
    assert(aquarium_store_read(directory, id, 0, readback, sizeof readback) && !memcmp(readback, second, sizeof second));
    char path[1200]; snprintf(path, sizeof path, "%s/0000000100000002-0.b", directory);
    FILE *file = fopen(path, "wb"); assert(file); assert(fputs("interrupted", file) >= 0); assert(!fclose(file));
    assert(aquarium_store_read(directory, id, 0, readback, sizeof readback) && !memcmp(readback, first, sizeof first));
    assert(aquarium_store_write(directory, id, 0, second, sizeof second));
    assert(aquarium_store_read(directory, id, 0, readback, sizeof readback) && !memcmp(readback, second, sizeof second));
    assert(!aquarium_store_read(directory, id, 1, readback, sizeof readback));
    assert(!aquarium_store_read(directory, id, 0, readback, sizeof readback - 1));
    puts("PASS: 500-sand-dollar aquarium purchases, global wallet, persistent names and editor, eight-slot cap, paused saves, SD/NVS rollback and interrupted SD recovery");
}
static void assert_shop_icon_colors(const uint16_t *locked, const uint16_t *owned,
                                    int item, int x0, int y0, int x1, int y1) {
    int changed = 0;
    for (int y = (int)render_frame_y(y0); y < (int)render_frame_y(y1); y++)
        for (int x = (int)render_frame_x(x0); x < (int)render_frame_x(x1); x++) {
            uint16_t v = locked[y * TANK_W + x];
            int r = (v >> 11) * 8, g = ((v >> 5) & 63) * 4, b = (v & 31) * 8;
            assert(abs(r - g) <= 32 && abs(r - b) <= 32 && abs(g - b) <= 32);
            changed += v != owned[y * TANK_W + x];
        }
    if (!changed) fprintf(stderr, "Shop icon %d unchanged in region %d,%d..%d,%d\n", item, x0, y0, x1, y1);
    assert(changed > 0);
}
static void test_shop_expansion(void) {
    tank_t t, loaded;
    saved_size = 0; tank_init(&t, 17); boot_enlarged(&t); progression_setup_done(&t);
    bool seen[SD_ITEM_COUNT] = {false}; int previous = -1;
    for (int row = 0; row < progression_shop_count(&t); row++) {
        int item = progression_shop_item(&t, row);
        assert(item >= 0 && item < SD_ITEM_COUNT && !seen[item]);
        assert(SD_ITEMS[item].price >= previous);
        seen[item] = true; previous = SD_ITEMS[item].price;
    }
    assert(progression_shop_item(&t, -1) == -1 && progression_shop_item(&t, SD_ITEM_COUNT) == -1);
    assert(SHOP_TAP_BUY + SD_ITEM_COUNT <= SHOP_TAP_MOVE);
    assert(SHOP_TAP_MOVE + SD_ITEM_COUNT <= SHOP_TAP_SELL);
    assert(SHOP_TAP_SELL + SD_ITEM_COUNT <= SHOP_TAP_FRY);
    int total = 0;
    for (int item = 22; item < SD_ITEM_COUNT; item++) {
        assert(!tank_decor_placeable(item) && tank_ornament_index(item) == -1);
        assert(SD_ITEMS[item].bit == (item < 32 ? (UINT32_C(1) << item) : 0));
        assert(render_text_w(SD_ITEMS[item].name, 3) <= SHP_MODAL_W - 16);
        assert(render_text_w(SD_ITEMS[item].words, 2) <= SHP_MODAL_W - 16);
        assert(render_text_w(SD_ITEMS[item].words2, 2) <= SHP_MODAL_W - 16);
        t.sd_balance = SD_ITEMS[item].price - 1;
        assert(!progression_buy(&t, item));
        t.sd_balance++;
        tank_t before = t; reject_write = true;
        assert(!progression_buy(&t, item) && !memcmp(&before, &t, sizeof t)); reject_write = false;
        assert(progression_buy(&t, item) && t.sd_balance == 0);
        assert(!progression_buy(&t, item));
        if (SD_IS_FIXED_SCENERY(item)) {
            before = t; reject_write = true;
            assert(!progression_sell(&t, item) && !memcmp(&before, &t, sizeof t)); reject_write = false;
            assert(progression_sell(&t, item) && !progression_item_owned(&t, item));
            assert(t.sd_balance == SD_ITEMS[item].price / 5);
            t.sd_balance = SD_ITEMS[item].price;
            assert(progression_buy(&t, item) && t.sd_balance == SD_ITEMS[item].price);
            assert(progression_sell_value_for(&t, item) == 0);
        } else assert(!progression_sell(&t, item));
        total += SD_ITEMS[item].price;
    }
    assert(total == 2340);
    tank_init(&loaded, 1); boot_enlarged(&loaded);
    assert(loaded.sd_unlocks == t.sd_unlocks && (loaded.sd_unlocks & SD_ITEM_GARDEN));
    assert(loaded.aquariums.stat_upgrades == STAT_MASK);
    assert(loaded.polish.scenery_unlocks == SD_SCENERY_MASK);
    char *json = progression_backup_json(&loaded); assert(json);
    assert(progression_restore_json(json, strlen(json), true, error, sizeof error)); free(json);
    tank_init(&loaded, 1); boot_enlarged(&loaded);
    assert(loaded.sd_unlocks == t.sd_unlocks && saved_size == AQUARIUM_SAVE_BYTES);
    assert(loaded.aquariums.stat_upgrades == STAT_MASK);
    assert(loaded.polish.scenery_unlocks == SD_SCENERY_MASK);
    loaded.sd_balance = AQUARIUM_PRICE;
    assert(!progression_aquarium_move(&loaded, 1) && !loaded.sd_unlocks);
    assert(loaded.aquariums.stat_upgrades == STAT_MASK);
    progression_setup_done(&loaded);
    assert(!progression_aquarium_move(&loaded, 0) && loaded.sd_unlocks == t.sd_unlocks);
    assert(loaded.aquariums.stat_upgrades == STAT_MASK);
    for (int sleep = 0; sleep < 2; sleep++) {
        tank_t normal, upgraded;
        tank_init(&normal, 32); tank_new_population(&normal);
        normal.sd_unlocks = SD_ITEM_PLANT;
        for (int b = 0; b < VEG_BEDS_MAX; b++) tank_veg_set(&normal, b, 0.2f);
        upgraded = normal; upgraded.sd_unlocks |= SD_ITEM_FILTER | SD_ITEM_GARDEN;
        float start = normal.veg_h[0][0];
        if (sleep) { tank_tick_sleep(&normal, 60); tank_tick_sleep(&upgraded, 60); }
        else { tank_tick(&normal, 1, advisor_rules); tank_tick(&upgraded, 1, advisor_rules); }
        assert(fabsf(normal.algae_acc - 2 * upgraded.algae_acc) < 0.0001f);
        assert(fabsf((normal.veg_h[0][0] - start) - 2 * (upgraded.veg_h[0][0] - start)) < 0.000001f);
        assert(normal.veg_h[3][0] == upgraded.veg_h[3][0]); /* Sword plant is not grass. */
        assert(normal.algae_colonies == 0 && upgraded.algae_colonies == 0 && upgraded.trim_px == 0);
        tank_init(&normal, 32); tank_new_population(&normal);
        upgraded = normal; upgraded.sd_unlocks = SD_ITEM_FILTER;
        normal.algae_acc = upgraded.algae_acc = sleep ? 179.25f : 239.25f;
        if (sleep) { tank_tick_sleep(&normal, 1); tank_tick_sleep(&upgraded, 1); }
        else { tank_tick(&normal, 1, advisor_rules); tank_tick(&upgraded, 1, advisor_rules); }
        assert(normal.algae_acc < 1 && upgraded.algae_acc == (sleep ? 179.75f : 239.75f));
    }
    uint16_t *base = calloc(TANK_W * TANK_H, sizeof *base);
    uint16_t *art = calloc(TANK_W * TANK_H, sizeof *art);
    uint16_t *scene = calloc(TANK_W * TANK_H, sizeof *scene);
    uint32_t *dirty = calloc(RENDER_DIRTY_WORDS, sizeof *dirty);
    void *scratch = calloc(1, render_decor_scratch_size());
    assert(base && art && scene && dirty && scratch);
    render_set_decor_scratch(scratch); render_set_scene_cache(scene); render_set_dirty_mask(dirty);
    tank_init(&t, 17);
    render_tank(&t, base, TANK_W);
    for (int item = 22; item <= 29; item++) {
        t.sd_unlocks = SD_ITEMS[item].bit;
        render_tank(&t, art, TANK_W);
        int changed = 0;
        for (int px = 0; px < TANK_W * TANK_H; px++) changed += art[px] != base[px];
        assert(changed > 40);
        t.sd_unlocks = 0;
        render_tank(&t, art, TANK_W);
        assert(!memcmp(base, art, TANK_W * TANK_H * sizeof *base)); /* No cached scenery remains after selling. */
    }
    t.sd_unlocks = SD_ITEM_LILIES | SD_ITEM_FLOAT_LOG | SD_ITEM_DUCK;
    render_tank(&t, base, TANK_W); t.clock = 2; render_tank(&t, art, TANK_W);
    for (int i = 0; i < 3; i++) {
        int changed = 0, left = (i + 1) * TANK_W / 4 - 20;
        for (int y = 0; y < 42; y++) for (int x = left; x < left + 40; x++)
            changed += base[y * TANK_W + x] != art[y * TANK_W + x];
        assert(changed > 10);
    }
    render_set_decor_scratch(NULL); render_set_scene_cache(NULL); render_set_dirty_mask(NULL);
    tank_t shop = t;
    shop.sd_unlocks = 0; shop.aquariums.stat_upgrades = 0; shop.sd_balance = 1000;
    for (int row = 0; row < progression_shop_count(&shop); row++) {
        int item = progression_shop_item(&shop, row);
        tank_t owned = shop;
        if (item < 32) owned.sd_unlocks |= SD_ITEMS[item].bit;
        else if (SD_IS_SCENERY(item)) owned.polish.scenery_unlocks |= (uint8_t)(1u << (item - SD_SCENERY_FIRST));
        else owned.aquariums.stat_upgrades |= (uint8_t)(1u << (item - 32));
        int display_row = row + progression_fry_for_sale(&shop);
        render_shop_leave();
        for (int page = 0; page < display_row / SHP_PER_PAGE; page++)
            render_shop_tap(&shop, render_frame_x(SHP_ARROW_X1 + 12), render_frame_y(SHP_ARROW_Y + 8));
        int top = SHP_ROW_Y0 + display_row % SHP_PER_PAGE * SHP_ROW_DY;
        render_shop(&shop, base, TANK_W); render_shop(&owned, art, TANK_W);
        assert_shop_icon_colors(base, art, item, 24, top, 66, top + 40);
        render_shop_tap(&shop, render_frame_x(150), render_frame_y(top + 16));
        render_shop(&shop, base, TANK_W); render_shop(&owned, art, TANK_W);
        assert_shop_icon_colors(base, art, item, PAGE_W / 2 - 40, SHP_MODAL_Y + 8,
                                PAGE_W / 2 + 40, SHP_MODAL_Y + 80);
    }
    render_shop_leave();
    free(base); free(art); free(scene); free(dirty); free(scratch);
    for (int sleep = 0; sleep < 2; sleep++) for (int goal = 0; goal < 2; goal++) {
        tank_t base, boosted;
        tank_init(&base, 721); tank_new_population(&base);
        base.companion.cozy = 0; /* Pin original physiology independently of new-tank Cozy defaults. */
        base.n_fish = 1;
        base.sd_unlocks = SD_ITEM_PLANT;
        for (int bed = 0; bed < VEG_BEDS_MAX; bed++) tank_veg_set(&base, bed, 0.2f);
        fish_t *f = &base.fish[0];
        f->energy = f->stress = f->curiosity = 5;
        f->goal.id = goal ? GOAL_EXPLORE : GOAL_REST;
        boosted = base; boosted.aquariums.stat_upgrades = STAT_MASK;
        float dt = sleep ? 60 : 0.1f;
        if (sleep) { tank_tick_sleep(&base, dt); tank_tick_sleep(&boosted, dt); }
        else { tank_tick(&base, dt, NULL); tank_tick(&boosted, dt, NULL); }
        const fish_t *a = &base.fish[0], *b = &boosted.fish[0];
        assert(fabsf(b->energy - (sleep || !goal ? 5 + 1.25f * (a->energy - 5) : a->energy)) < 0.00001f);
        float stress_bonus = 0.25f * (sleep ? 2.0f / 3600 : goal ? 0.18f : 0.48f) * dt;
        assert(fabsf(b->stress - (a->stress - stress_bonus)) < 0.00001f);
        assert(fabsf(b->curiosity - (!sleep && goal ? 5 + 1.25f * (a->curiosity - 5) : a->curiosity)) < 0.00001f);
        assert(a->hunger == b->hunger && a->trust == b->trust && a->size == b->size);
    }
    for (int hitch = 0; hitch < 2; hitch++) for (int cap = 0; cap < 2; cap++) {
        tank_t base, boosted;
        tank_init(&base, 321); tank_new_population(&base); base.n_fish = 1;
        base.fish[0].trust = cap ? 9.9999f : 3;
        base.hold_active = true; base.hold_x = base.fish[0].x; base.hold_y = base.fish[0].y;
        base.sh_hitched = hitch != 0;
        boosted = base; boosted.aquariums.stat_upgrades = STAT_MASK;
        tank_tick(&base, 0.1f, NULL); tank_tick(&boosted, 0.1f, NULL);
        assert(fabsf(boosted.fish[0].trust - (cap ? 10 : 3 + 0.0012f * (hitch ? 2.25f : 1.25f))) < 0.000001f);
        if (!cap) assert(fabsf(base.sh_trust_passed - boosted.sh_trust_passed) < 0.000001f);
        assert(boosted.fish[0].energy <= 10 && boosted.fish[0].stress >= 0 && boosted.fish[0].curiosity <= 10);
    }
    for (int goal = 0; goal < 2; goal++) {
        tank_t capped;
        tank_init(&capped, 372); tank_new_population(&capped); capped.n_fish = 1;
        capped.sd_unlocks = SD_ITEM_PLANT; capped.aquariums.stat_upgrades = STAT_MASK;
        for (int bed = 0; bed < VEG_BEDS_MAX; bed++) tank_veg_set(&capped, bed, 0.2f);
        fish_t *f = &capped.fish[0];
        f->energy = f->curiosity = 9.9999f; f->stress = 0.0001f;
        f->goal.id = goal ? GOAL_EXPLORE : GOAL_REST;
        tank_tick(&capped, 0.1f, NULL);
        assert(f->stress == 0 && (goal ? f->curiosity == 10 : f->energy == 10));
        f->energy = 9.9999f; f->stress = 0.0001f;
        tank_tick_sleep(&capped, 60);
        assert(f->energy == 10 && f->stress == 0);
    }
    {
        tank_t resting, boosted;
        tank_init(&resting, 108); tank_new_population(&resting); resting.n_fish = 1;
        for (int bed = 0; bed < VEG_BEDS_MAX; bed++) tank_veg_set(&resting, bed, 0);
        fish_t *f = &resting.fish[0];
        f->energy = 0.5f; f->hunger = 2; f->goal.id = GOAL_EXPLORE;
        f->x = resting.reef_x + 13 + f->rest_dx; f->y = TANK_BOT - 50 + f->rest_dy;
        tank_tick(&resting, 0.1f, NULL);
        assert(f->breather > 0 && f->goal.id == GOAL_EXPLORE);
        boosted = resting; boosted.aquariums.stat_upgrades = STAT_CURRENT;
        float before = f->energy;
        tank_tick(&resting, 0.1f, NULL); tank_tick(&boosted, 0.1f, NULL);
        assert(fabsf((boosted.fish[0].energy - before) - 1.25f * (f->energy - before)) < 0.000001f);
        f->energy = 4; tank_tick(&resting, 0.1f, NULL);
        assert(!f->breather && f->goal.id == GOAL_EXPLORE);
        boosted.fish[0].breather = 2; tank_tick_sleep(&boosted, 60);
        assert(!boosted.fish[0].breather);
    }
    json = progression_backup_json(&loaded); assert(json);
    cJSON *root = cJSON_Parse(json); free(json); assert(root);
    cJSON *tank = cJSON_GetObjectItemCaseSensitive(root, "tank");
    cJSON_SetNumberValue(cJSON_GetObjectItemCaseSensitive(tank, "stat_upgrades"), 16);
    json = cJSON_PrintUnformatted(root); assert(json);
    assert(!progression_restore_json(json, strlen(json), true, error, sizeof error)); free(json);
    cJSON_DeleteItemFromObjectCaseSensitive(tank, "stat_upgrades");
    json = cJSON_PrintUnformatted(root); cJSON_Delete(root); assert(json);
    assert(progression_restore_json(json, strlen(json), true, error, sizeof error)); free(json);
    tank_init(&loaded, 1); boot_enlarged(&loaded);
    assert(!loaded.aquariums.stat_upgrades);
    loaded.aquariums.stat_upgrades = STAT_MASK; assert(progression_reset(&loaded, 123));
    assert(!loaded.aquariums.stat_upgrades);
    puts("PASS: 36 sorted shop IDs, atomic purchases, per-tank stat upgrades, binary/JSON/legacy persistence, exact 25% recovery/trust, seahorse attribution, scenery and upkeep");
}
static int clock_tap(tank_t *t, int x, int y) {
    int value;
    render_settings_touch(t, render_frame_x(x), render_frame_y(y), true, &value);
    return render_settings_touch(t, render_frame_x(x), render_frame_y(y), false, &value);
}
static void test_clock(void) {
    tank_t t, loaded; saved_size = 0;
    tank_init(&t, 42); boot_enlarged(&t); progression_setup_done(&t);
    t.companion.captions = 0; /* Face-only invariants; integrated captions have their own bounds matrix. */
    assert(t.aquariums.clock_theme == CLOCK_OFF && sizeof(t.aquariums) == 32);
    char text[9]; int hour, minute;
    const int64_t midnight = INT64_C(1735689600); /* 2025-01-01 00:00 UTC */
    assert(render_clock_format(&t, midnight, text, &hour, &minute) && !strcmp(text, "00:00") && hour == 0 && minute == 0);
    t.aquariums.clock_flags = CLOCK_12H;
    assert(render_clock_format(&t, midnight, text, &hour, &minute) && !strcmp(text, "12:00") && hour == 0);
    assert(render_clock_format(&t, midnight + 43200, text, &hour, &minute) && !strcmp(text, "12:00") && hour == 12);
    t.aquariums.clock_utc_quarters = -1;
    assert(render_clock_format(&t, midnight, text, &hour, &minute) && !strcmp(text, "11:45") && hour == 23 && minute == 45);
    t.aquariums.clock_flags = 0; t.aquariums.clock_utc_quarters = 23;
    assert(render_clock_format(&t, midnight, text, &hour, &minute) && !strcmp(text, "05:45"));
    t.aquariums.clock_flags = CLOCK_DST;
    assert(render_clock_format(&t, midnight, text, &hour, &minute) && !strcmp(text, "06:45"));
    t.aquariums.clock_flags |= CLOCK_12H; t.aquariums.clock_utc_quarters = -1;
    assert(render_clock_format(&t, midnight, text, &hour, &minute) && !strcmp(text, "12:45") && hour == 0);
    t.aquariums.clock_flags = 0;
    t.aquariums.clock_utc_quarters = 56;
    assert(render_clock_format(&t, midnight + 23 * 3600, text, &hour, &minute) && !strcmp(text, "13:00"));
    assert(!render_clock_format(&t, 0, text, &hour, &minute) && !strcmp(text, "--:--"));
    const size_t count = (TANK_W + 3) * TANK_H;
    uint16_t *fb = malloc((count + 2) * sizeof *fb), *old = calloc(count, sizeof *old);
    assert(fb && old); fb[0] = fb[count + 1] = 0xbeef;
    for (int theme = CLOCK_OFF; theme < CLOCK_THEME_COUNT; theme++)
        for (int size = 0; size < CLOCK_SIZE_COUNT; size++) for (int pos = 0; pos < CLOCK_POSITION_COUNT; pos++)
            for (int synced = 0; synced < 2; synced++) {
                memset(fb + 1, 0, count * sizeof *fb);
                t.aquariums.clock_theme = (uint8_t)theme; t.aquariums.clock_flags = CLOCK_12H;
                t.aquariums.clock_layout = clock_layout_set_size((uint8_t)(pos << 2), size);
                int bx, by, w, h; render_clock_bounds(&t, &bx, &by, &w, &h);
                assert(bx >= 0 && by >= 0 && bx + w <= TANK_W && by + h <= TANK_H);
                if (pos == CLOCK_CENTER) assert(abs(2 * bx + w - TANK_W) <= 1 && abs(2 * by + h - TANK_H) <= 1);
                if (size == CLOCK_LARGE && theme != CLOCK_ANALOG && theme != CLOCK_ORBIT) assert(w >= 220 && h >= 100);
                render_clock_overlay(&t, fb + 1, TANK_W + 3, synced ? midnight : 0);
                for (int y = 0; y < TANK_H; y++) for (int x = 0; x < TANK_W; x++)
                    if (theme == CLOCK_OFF || x < bx || x >= bx + w || y < by || y >= by + h)
                        assert(fb[1 + y * (TANK_W + 3) + x] == 0);
                for (int y = 0; y < TANK_H; y++) for (int x = TANK_W; x < TANK_W + 3; x++)
                    assert(fb[1 + y * (TANK_W + 3) + x] == 0);
                assert(fb[0] == 0xbeef && fb[count + 1] == 0xbeef);
            }
    t.aquariums.clock_utc_quarters = 0;
    for (int theme = CLOCK_COMPACT; theme < CLOCK_THEME_COUNT; theme++) {
        t.aquariums.clock_theme = (uint8_t)theme;
        t.aquariums.clock_flags = CLOCK_DST;
        memset(old, 0, count * sizeof *old);
        render_clock_overlay(&t, old, TANK_W + 3, midnight - 1800);
        t.aquariums.clock_flags = 0;
        memset(fb + 1, 0, count * sizeof *fb);
        render_clock_overlay(&t, fb + 1, TANK_W + 3, midnight + 1800);
        assert(!memcmp(old, fb + 1, count * sizeof *old)); /* DST must advance hands, AM/PM and date together. */
    }
    for (int theme = CLOCK_COMPACT; theme < CLOCK_THEME_COUNT; theme++)
        for (int size = 0; size < CLOCK_SIZE_COUNT; size++)
            for (int color = 0; color < 8; color++) for (int secondary = 0; secondary < 8; secondary++) for (int hidden = 0; hidden < 8; hidden++) {
                t.aquariums.clock_theme = (uint8_t)theme; t.aquariums.clock_layout = clock_layout_set_size(0, size);
                t.aquariums.clock_flags = (uint8_t)((color << CLOCK_COLOR_SHIFT) | (hidden << 2));
                t.aquariums.clock_delay = (uint8_t)(secondary << CLOCK_SECONDARY_SHIFT);
                int bx, by, w, h; render_clock_bounds(&t, &bx, &by, &w, &h);
                int64_t now = midnight + 15 * 3600 + 34 * 60 + 22;
                memset(old, 0, count * sizeof *old);
                render_clock_overlay(&t, old, TANK_W + 3, now);
                if (color) {
                    static const uint32_t inks[] = {0, 0xffffff, 0xffdc73, 0x89ffb1, 0x73e5ff, 0xffa3cf, 0xc5adff, 0xff8173};
                    uint32_t rgb = inks[color];
                    uint16_t expected = (uint16_t)(((rgb >> 8) & 0xf800) | ((rgb >> 5) & 0x07e0) | ((rgb >> 3) & 0x001f));
                    size_t i = 0; while (i < count && old[i] != expected) i++;
                    assert(i < count);
                }
                assert((old[by * (TANK_W + 3) + bx] == 0) == !!(t.aquariums.clock_flags & CLOCK_HIDE_PANEL));
                if (secondary && !(t.aquariums.clock_flags & CLOCK_HIDE_PANEL)) {
                    static const uint32_t inks[] = {0, 0xffffff, 0xffdc73, 0x89ffb1, 0x73e5ff, 0xffa3cf, 0xc5adff, 0xff8173};
                    uint32_t rgb = inks[secondary];
                    uint16_t expected = (uint16_t)(((rgb >> 8) & 0xf800) | ((rgb >> 5) & 0x07e0) | ((rgb >> 3) & 0x001f));
                    assert(old[by * (TANK_W + 3) + bx] == expected);
                }
                memset(fb + 1, 0, count * sizeof *fb);
                render_clock_overlay(&t, fb + 1, TANK_W + 3, now + 1);
                assert((!memcmp(old, fb + 1, count * sizeof *old)) == !!(t.aquariums.clock_flags & CLOCK_HIDE_SECONDS));
                memset(fb + 1, 0, count * sizeof *fb);
                render_clock_overlay(&t, fb + 1, TANK_W + 3, now + 86400);
                assert((!memcmp(old, fb + 1, count * sizeof *old)) == !!(t.aquariums.clock_flags & CLOCK_HIDE_DATE));
                for (int yy = 0; yy < TANK_H; yy++) for (int xx = 0; xx < TANK_W; xx++)
                    if (xx < bx || xx >= bx + w || yy < by || yy >= by + h)
                        assert(old[yy * (TANK_W + 3) + xx] == 0);
                assert(fb[0] == 0xbeef && fb[count + 1] == 0xbeef);
            }
    t.aquariums.clock_delay = 0;
    for (int size = CLOCK_LARGE; size <= CLOCK_SMALL; size++) {
        t.aquariums.clock_theme = CLOCK_ORBIT; t.aquariums.clock_layout = (uint8_t)size;
        t.aquariums.clock_flags = CLOCK_HIDE_PANEL | CLOCK_HIDE_DATE | CLOCK_12H;
        int bx, by, w, h; render_clock_bounds(&t, &bx, &by, &w, &h);
        memset(fb + 1, 0, count * sizeof *fb);
        render_clock_overlay(&t, fb + 1, TANK_W + 3, midnight + 11 * 3600 + 59 * 60 + 59);
        int radius = w / 2 - 14, bottom = by + 2 * radius + 4, caption = by + h - 24;
        assert(caption - bottom >= 10);
        for (int yy = bottom + 1; yy < caption; yy++) for (int xx = bx; xx < bx + w; xx++)
            assert(fb[1 + yy * (TANK_W + 3) + xx] == 0);
    }
    t.aquariums.clock_flags = CLOCK_12H;
    {
        t.aquariums.clock_theme = CLOCK_BINARY; t.aquariums.clock_layout = 0;
        static const int digits[] = {0, 3, 3, 4, 2, 2};
        static const uint32_t colors[] = {0xffdc73, 0x73f5e5, 0xff7289};
        int bx, by, w, h; render_clock_bounds(&t, &bx, &by, &w, &h);
        memset(fb + 1, 0, count * sizeof *fb);
        render_clock_overlay(&t, fb + 1, TANK_W + 3, midnight + 15 * 3600 + 34 * 60 + 22);
        for (int col = 0; col < 6; col++) for (int row = 0; row < 4; row++) {
            uint32_t rgb = colors[col / 2];
            uint16_t lit = (uint16_t)(((rgb >> 8) & 0xf800) | ((rgb >> 5) & 0x07e0) | ((rgb >> 3) & 0x001f));
            int px = bx + 24 + col * ((w - 32) / 6), py = by + 24 + row * 12 + 3;
            assert((fb[1 + py * (TANK_W + 3) + px] == lit) == !!(digits[col] & (1 << (3 - row))));
        }
    }
    t.aquariums.clock_theme = CLOCK_DIGITAL; t.aquariums.clock_layout = 0;
    {
        static const uint8_t expected[] = {0x3f, 0x06, 0x5b, 0x4f, 0x66, 0x6d, 0x7d, 0x07, 0x7f, 0x6f};
        static const int sample_x[] = {14, 26, 26, 14, 2, 2, 14}, sample_y[] = {2, 14, 38, 50, 38, 14, 26};
        for (int digit = 0; digit < 10; digit++) {
            memset(fb + 1, 0, count * sizeof *fb);
            render_clock_overlay(&t, fb + 1, TANK_W + 3, midnight + digit * 60);
            int bx, by, w, h; render_clock_bounds(&t, &bx, &by, &w, &h);
            int dx = bx + (w - 22 * 8) / 2 + 18 * 8, dy = by + 10;
            for (int segment = 0; segment < 7; segment++) {
                uint16_t pixel = fb[1 + (dy + sample_y[segment]) * (TANK_W + 3) + dx + sample_x[segment]];
                assert((pixel == 0xffff) == ((expected[digit] & (1 << segment)) != 0));
            }
            assert(fb[1 + (dy + 3) * (TANK_W + 3) + dx + 25] != 0xffff); /* isolated beveled corners */
            assert(fb[1 + dy * (TANK_W + 3) + dx + 4] != 0xffff);
        }
    }
    static const int delays[] = {60, 15, 30, 120, 300, 600};
    for (int i = 0; i < 6; i++) {
        t.aquariums.clock_delay = (uint8_t)i; t.clock_idle_s = delays[i] - 0.01f;
        assert(!render_clock_active(&t)); t.clock_idle_s = (float)delays[i]; assert(render_clock_active(&t));
    }
    t.aquariums.clock_delay = CLOCK_DELAY_MANUAL; t.clock_idle_s = 1e6f;
    assert(!render_clock_active(&t)); t.clock_requested = true; assert(render_clock_active(&t));
    t.ui_cover = true; assert(!render_clock_active(&t)); t.ui_cover = false;
    t.aquariums.clock_theme = CLOCK_OFF; assert(!render_clock_active(&t));
    t.aquariums.clock_theme = CLOCK_DIGITAL; tank_handled(&t);
    assert(!render_clock_active(&t) && !t.clock_requested && !t.clock_idle_s);
    t.clock_requested = true;
    {
        size_t bytes = TANK_W * TANK_H * sizeof(uint16_t);
        uint16_t *cache = malloc(bytes); uint32_t *dirty = calloc(RENDER_DIRTY_WORDS, sizeof *dirty);
        assert(cache && dirty); render_set_scene_cache(cache); render_set_dirty_mask(dirty);
        t.aquariums.clock_theme = CLOCK_OFF; t.ui_cover = false;
        render_tank(&t, fb + 1, TANK_W); memcpy(old, fb + 1, bytes);
        render_clock_synchronized(true);
        t.aquariums.clock_theme = CLOCK_NEON; render_tank(&t, fb + 1, TANK_W);
        assert(memcmp(old, fb + 1, bytes));
        t.aquariums.clock_theme = CLOCK_OFF;
        unsigned epoch; const uint16_t *scene = render_scene_buf(&epoch); assert(scene);
        memcpy(fb + 1, scene, bytes); render_fb_primed(fb + 1, epoch);
        render_tank(&t, fb + 1, TANK_W); assert(!memcmp(old, fb + 1, bytes));
        t.aquariums.clock_theme = CLOCK_DIGITAL; render_clock_synchronized(false);
        render_tank(&t, fb + 1, TANK_W); memcpy(old, fb + 1, bytes);
        render_clock_synchronized(true); render_tank(&t, fb + 1, TANK_W);
        assert(memcmp(old, fb + 1, bytes));
        render_clock_synchronized(false); render_set_scene_cache(NULL); render_set_dirty_mask(NULL);
        free(cache); free(dirty);
    }
    t.aquariums.clock_theme = CLOCK_OFF; t.aquariums.clock_flags = 0; t.aquariums.clock_utc_quarters = 0;
    t.aquariums.clock_layout = t.aquariums.clock_delay = 0;
    render_settings_open(); clock_tap(&t, 330, 100); clock_tap(&t, 100, 100); clock_tap(&t, 220, 84);
    assert(t.aquariums.clock_theme == CLOCK_COMPACT);
    reject_write = true; clock_tap(&t, 220, 84); reject_write = false;
    assert(t.aquariums.clock_theme == CLOCK_COMPACT);
    clock_tap(&t, 220, 132); clock_tap(&t, 220, 180);
    clock_tap(&t, 380, 28); clock_tap(&t, 330, 180); clock_tap(&t, 220, 84);
    reject_write = true; clock_tap(&t, 220, 132); clock_tap(&t, 380, 270); reject_write = false;
    assert(t.aquariums.clock_flags == CLOCK_12H && !t.aquariums.clock_utc_quarters);
    clock_tap(&t, 220, 132); clock_tap(&t, 220, 180);
    t.aquariums.clock_utc_quarters = 55;
    clock_tap(&t, 380, 270); assert(t.aquariums.clock_utc_quarters == 56);
    clock_tap(&t, 380, 270); assert(t.aquariums.clock_utc_quarters == -48);
    clock_tap(&t, 70, 270); assert(t.aquariums.clock_utc_quarters == 56);
    t.aquariums.clock_utc_quarters = 0;
    clock_tap(&t, 380, 270); assert(t.aquariums.clock_utc_quarters == 4);
    clock_tap(&t, 220, 272); clock_tap(&t, 70, 270); assert(t.aquariums.clock_utc_quarters == 2);
    clock_tap(&t, 220, 272); clock_tap(&t, 70, 270); assert(t.aquariums.clock_utc_quarters == 1);
    clock_tap(&t, 220, 272);
    assert(t.aquariums.clock_flags == (CLOCK_12H | CLOCK_DST) && t.aquariums.clock_utc_quarters == 1);
    assert(t.aquariums.clock_layout == 5 && t.aquariums.clock_delay == CLOCK_DELAY_15_SEC);
    clock_tap(&t, 380, 28); clock_tap(&t, 100, 100);
    for (int i = 0; i < 6; i++) clock_tap(&t, 220, 84);
    assert(t.aquariums.clock_theme == CLOCK_ORBIT);
    clock_tap(&t, 380, 28); clock_tap(&t, 330, 100);
    reject_write = true; clock_tap(&t, 220, 84); reject_write = false;
    assert(t.aquariums.clock_flags == (CLOCK_12H | CLOCK_DST));
    for (int i = 0; i < 7; i++) clock_tap(&t, 220, 84);
    reject_write = true; clock_tap(&t, 220, 132); reject_write = false;
    assert(t.aquariums.clock_delay == CLOCK_DELAY_15_SEC);
    for (int i = 0; i < 8; i++) clock_tap(&t, 220, 132);
    assert(t.aquariums.clock_delay == CLOCK_DELAY_15_SEC);
    for (int i = 0; i < 4; i++) clock_tap(&t, 220, 132);
    clock_tap(&t, 220, 180);
    clock_tap(&t, 380, 28); clock_tap(&t, 100, 180);
    clock_tap(&t, 220, 180); clock_tap(&t, 220, 228);
    assert(t.aquariums.clock_flags == CLOCK_FLAGS_MASK);
    clock_tap(&t, 380, 20);
    clock_tap(&t, 330, 180); clock_tap(&t, 220, 180);
    assert(t.aquariums.clock_delay == ((4 << CLOCK_SECONDARY_SHIFT) | CLOCK_DELAY_30_SEC));
    assert(clock_tap(&t, 330, 318) != SET_TAP_WIFI);
    clock_tap(&t, 380, 20);
    clock_tap(&t, 100, 260);
    uint8_t weather_size_before = t.aquariums.clock_delay;
    reject_write = true; clock_tap(&t, 320, 288); reject_write = false;
    assert(t.aquariums.clock_delay == weather_size_before);
    for (int i = 0; i < CLOCK_TEXT_SIZE_COUNT; i++) {
        clock_tap(&t, 320, 288);
        assert((t.aquariums.clock_delay & ~CLOCK_CAPTION_MASK) == (weather_size_before & ~CLOCK_CAPTION_MASK));
        assert(((t.aquariums.clock_delay & CLOCK_CAPTION_MASK) >> CLOCK_CAPTION_SHIFT) == (i + 1) % CLOCK_TEXT_SIZE_COUNT);
    }
    assert(t.aquariums.clock_delay == weather_size_before);
    clock_tap(&t, 380, 20);
    tank_init(&loaded, 3); boot_enlarged(&loaded);
    assert(loaded.aquariums.clock_theme == CLOCK_ORBIT && loaded.aquariums.clock_flags == CLOCK_FLAGS_MASK && loaded.aquariums.clock_utc_quarters == 1);
    assert(loaded.aquariums.clock_layout == 5 && loaded.aquariums.clock_delay == ((4 << CLOCK_SECONDARY_SHIFT) | CLOCK_DELAY_30_SEC));
    loaded.clock_idle_s = 29; assert(!render_clock_active(&loaded));
    loaded.clock_idle_s = 30; assert(render_clock_active(&loaded));
    loaded.aquariums.clock_layout = CLOCK_LAYOUT_MAX;
    loaded.aquariums.clock_delay |= 2 << CLOCK_CAPTION_SHIFT;
    assert(progression_save(&loaded));
    tank_init(&loaded, 3); boot_enlarged(&loaded);
    assert(loaded.aquariums.clock_layout == CLOCK_LAYOUT_MAX);
    char error[120], *json = progression_backup_json(&loaded); assert(json);
    assert(progression_restore_json(json, strlen(json), true, error, sizeof error));
    tank_init(&loaded, 3); boot_enlarged(&loaded);
    assert(loaded.aquariums.clock_delay == ((4 << CLOCK_SECONDARY_SHIFT) | (2 << CLOCK_CAPTION_SHIFT) | CLOCK_DELAY_30_SEC));
    assert(loaded.aquariums.clock_layout == CLOCK_LAYOUT_MAX);
    cJSON *root = cJSON_Parse(json); free(json); assert(root);
    cJSON *fields = cJSON_GetObjectItemCaseSensitive(root, "tank");
    static const char *const keys[] = {"clock_theme", "clock_flags", "clock_utc_quarters", "clock_layout", "clock_delay", "clock_secondary"};
    static const int invalid_values[] = {CLOCK_THEME_COUNT, CLOCK_FLAGS_MASK + 1, 57, CLOCK_LAYOUT_MAX + 1, CLOCK_DELAY_COUNT, 8};
    assert(cJSON_GetObjectItemCaseSensitive(fields, "clock_secondary")->valueint == 4);
    assert(cJSON_GetObjectItemCaseSensitive(fields, "clock_delay")->valueint == ((2 << CLOCK_CAPTION_SHIFT) | CLOCK_DELAY_30_SEC));
    for (int i = 0; i < 6; i++) {
        cJSON *option = cJSON_GetObjectItemCaseSensitive(fields, keys[i]); double before = option->valuedouble;
        cJSON_SetNumberValue(option, invalid_values[i]);
        json = cJSON_PrintUnformatted(root); assert(json);
        assert(!progression_restore_json(json, strlen(json), true, error, sizeof error)); free(json);
        cJSON_SetNumberValue(option, before);
    }
    cJSON *layout = cJSON_GetObjectItemCaseSensitive(fields, "clock_layout");
    double valid_layout = layout->valuedouble;
    cJSON_SetNumberValue(layout, CLOCK_LAYOUT_MAX + 1); cJSON_DeleteItemFromObjectCaseSensitive(fields, "clock_delay");
    json = cJSON_PrintUnformatted(root); assert(json);
    assert(!progression_restore_json(json, strlen(json), true, error, sizeof error)); free(json);
    cJSON_SetNumberValue(layout, valid_layout);
    for (int i = 0; i < 6; i++) cJSON_DeleteItemFromObjectCaseSensitive(fields, keys[i]);
    json = cJSON_PrintUnformatted(root); cJSON_Delete(root); assert(json);
    assert(progression_restore_json(json, strlen(json), true, error, sizeof error)); free(json);
    tank_init(&loaded, 3); boot_enlarged(&loaded); assert(!loaded.aquariums.clock_theme);
    loaded.aquariums.clock_theme = CLOCK_NEON; assert(progression_reset(&loaded, 4));
    assert(!loaded.aquariums.clock_theme && !loaded.aquariums.clock_flags && !loaded.aquariums.clock_utc_quarters);
    assert(!loaded.aquariums.clock_layout && !loaded.aquariums.clock_delay);
    free(fb); free(old);
    puts("PASS: seven clock themes, 64 primary/secondary combinations, all details/sizes/bounds, 10-pixel Orbit caption clearance, idle delays/OFF/manual, segments, UTC/DST, rollback and binary/JSON/legacy saves");
}
static void test_fish_card_next(void) {
    tank_t t; saved_size = 0; tank_init(&t, 42); boot_enlarged(&t); progression_setup_done(&t);
    assert(t.n_fish > 1);
    int x = RENDER_CARD_NEXT_X + RENDER_CARD_NEXT_W / 2, y = RENDER_CARD_NEXT_Y + RENDER_CARD_NEXT_H / 2;
    assert(render_stats_next_hit(&t, 0, render_frame_x(x), render_frame_y(y)));
    assert(!RENDER_CARD_HIT(render_frame_x(x), render_frame_y(y)));
    assert(!render_stats_next_hit(&t, -1, render_frame_x(x), render_frame_y(y)));
    assert(!render_stats_next_hit(&t, RENDER_CARD_SNAIL, render_frame_x(x), render_frame_y(y)));
    assert(!render_stats_next_hit(&t, t.n_fish, render_frame_x(x), render_frame_y(y)));
    const size_t pixels = TANK_W * TANK_H;
    uint16_t *fb = calloc(pixels, sizeof *fb); assert(fb);
    render_stats_card(&t, 0, fb, TANK_W);
    int px = RENDER_CARD_NEXT_X * TANK_W / PAGE_W, py = RENDER_CARD_NEXT_Y * TANK_H / PAGE_H;
    assert(fb[py * TANK_W + px]);
    t.n_fish = 1;
    assert(!render_stats_next_hit(&t, 0, render_frame_x(x), render_frame_y(y)));
    memset(fb, 0, pixels * sizeof *fb); render_stats_card(&t, 0, fb, TANK_W);
    assert(!fb[py * TANK_W + px]);
    free(fb);
    puts("PASS: fish-card next arrow rendered outside card, shared hit target, no arrow for one fish or special-creature cards");
}
static void test_readable_text(void) {
    const size_t pixels = (TANK_W + 3) * TANK_H;
    uint16_t *guarded = calloc(pixels + 2, sizeof *guarded); assert(guarded);
    uint16_t *fb = guarded + 1; const int stride = TANK_W + 3;
    guarded[0] = guarded[pixels + 1] = 0xbeef;
    static const uint8_t letter_e[7] = {0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x1f};
    for (int scale = 1; scale <= 2; scale++) {
        for (int x = 0; x < 16; x++) for (int y = 0; y < 16; y++) {
            memset(fb, 0, pixels * sizeof *fb);
            render_text(fb, stride, x, y, scale, 0xffffff, "E");
            int left = x * TANK_W / PAGE_W, top = y * TANK_H / PAGE_H, lit = 0;
            for (int row = 0; row < 7; row++) for (int col = 0; col < 5; col++) {
                bool expected = (letter_e[row] & (0x10 >> col)) != 0;
                assert(fb[(top + row) * stride + left + col] == (expected ? 0xffff : 0));
                lit += expected;
            }
            int actual = 0;
            for (size_t i = 0; i < pixels; i++) actual += fb[i] != 0;
            assert(actual == lit); /* No merged strokes, extra pixels, or missing rows at any alignment. */
        }
    }
    assert(render_text_w("JELLYFISH", 1) == render_text_w("JELLYFISH", 2));
    static const uint8_t arrows[2][7] = {
        {0x02, 0x04, 0x08, 0x10, 0x08, 0x04, 0x02},
        {0x08, 0x04, 0x02, 0x01, 0x02, 0x04, 0x08}
    };
    for (int arrow = 0; arrow < 2; arrow++) {
        memset(fb, 0, pixels * sizeof *fb);
        char label[2] = {arrow ? '>' : '<', 0};
        render_text(fb, stride, 0, 0, 2, 0xffffff, label);
        for (int row = 0; row < 7; row++) for (int col = 0; col < 5; col++)
            assert(fb[row * stride + col] == ((arrows[arrow][row] & (0x10 >> col)) ? 0xffff : 0));
    }
    assert(render_text_w("JELLYFISH", 2) <= SHP_FRY_BOX_W - 8);
    assert(3 * SHP_FRY_BOX_W + 2 * SHP_FRY_BOX_GAP <= SHP_MODAL_W);
    assert(render_text_w("TAP TO CYCLE - ADULT PREVIEW", 2) <= PAGE_W - 32);
    assert(render_text_w("LOOKS - CUSTOMIZATION COUNTS", 2) <= 400);
    assert(render_text_w("TRUSTED FILES ONLY - NOT SIGNED", 2) <= PAGE_W - 64);
    for (int goal = 0; goal < JOURNAL_GOAL_N; goal++)
        assert(render_text_w(JOURNAL_GOALS[goal].detail, 2) <= 384);
    assert(render_text_wrap(fb, stride, 32, 40, 396, 0xffffff,
                           "RESTORE FROM SD FAILED - THE ORIGINAL TANK WAS NOT CHANGED") == 2);
    assert(render_text_wrap(fb, stride, 32, 40, 120, 0xffffff, "ONE\nTWO") == 2);
    assert(render_text_wrap(fb, stride, 32, 40, 120, 0xffffff, "") == 0);
    render_text(fb, stride, -7, -9, 1, 0xffffff, "CLIP LEFT");
    render_text(fb, stride, PAGE_W - 2, PAGE_H - 2, 2, 0xffffff, "CLIP RIGHT");
    render_glyph(fb, stride, -8, PAGE_H - 2, 1, 0xffffff, letter_e);
    for (int y = 0; y < TANK_H; y++) for (int x = TANK_W; x < stride; x++) assert(!fb[y * stride + x]);
    assert(guarded[0] == 0xbeef && guarded[pixels + 1] == 0xbeef);
    free(guarded);
    puts("PASS: exact 5x7 text at all alignments, minimum font, word wrapping, caption fit, clipping and stride guards");
}
static void test_journal(void) {
    tank_t t, reloaded; saved_size = 0; tank_init(&t, 7); boot_enlarged(&t);
    assert(!t.journal.xp && !t.journal.initialized);
    t.player_feedings = 20; t.algae_colonies = 30; t.trim_px = 200 * PX_PER_CM; t.hold_approaches = 5;
    t.fish[0].variety = FISH_SHARK; t.fish[0].stage = STAGE_ADULT; t.fish[0].trust = 6.5f;
    journal_tick(&t);
    assert(t.journal.xp == 0 && t.journal.initialized);
    assert((t.journal.seen & (1u << FISH_SHARK)) && (t.journal.grown & (1u << FISH_SHARK)));
    t.player_feedings++; t.algae_colonies++; t.trim_px += 10 * PX_PER_CM; t.hold_approaches++;
    journal_tick(&t); assert(t.journal.xp == 0 && (t.care.ready & 5) == 5);
    journal_tick(&t); assert(t.journal.xp == 0);
    tank_feed(&t, TANK_W / 2, 1); journal_tick(&t); assert(t.journal.xp == 0); /* Not eaten yet. */
    for (int level = 1; level <= 15; level++) {
        t.journal.xp = journal_level_xp(level); assert(journal_level(&t) == level);
        if (level > 1) { t.journal.xp--; assert(journal_level(&t) == level - 1); }
    }
    t.journal.xp = 0; assert(!journal_set_theme(&t, 1) && !journal_set_theme(&t, -1));
    t.sd_unlocks = SD_ITEM_MASK;
    t.journal.seen = t.journal.grown = (1u << FISH_VARIETY_COUNT) - 1; t.journal.population = 3;
    assert(journal_active(&t, 0) == 0 && journal_active(&t, 2) == 2);
    assert(journal_active(&t, 3) == -1 && !journal_claim(&t, 3) && !journal_claim(&t, -1));
    reject_write = true;
    assert(!journal_claim(&t, 0) && !t.journal.claimed && !t.journal.xp && !t.sd_balance);
    reject_write = false;
    unsigned xp = 0, coins = 0;
    for (int goal = 0; goal < JOURNAL_GOAL_N; goal++) {
        assert(journal_progress(&t, goal) == JOURNAL_GOALS[goal].target);
        assert(journal_claim(&t, goal)); xp += JOURNAL_GOALS[goal].xp; coins += JOURNAL_GOALS[goal].coins;
        assert(!journal_claim(&t, goal) && t.journal.xp == xp && t.sd_balance == (int)coins);
    }
    assert(journal_active(&t, 0) == -1);
    t.journal.xp = journal_level_xp(10);
    reject_write = true; assert(!journal_set_theme(&t, 3) && t.journal.theme == 0); reject_write = false;
    assert(journal_set_theme(&t, 3));
    tank_init(&reloaded, 8); boot_enlarged(&reloaded);
    assert(reloaded.journal.claimed == t.journal.claimed && reloaded.journal.theme == 3 &&
           reloaded.journal.seen == t.journal.seen && reloaded.sd_balance == t.sd_balance);
    unsigned old_xp = reloaded.journal.xp; reloaded.sd_balance = 0;
    assert(journal_level(&reloaded) == 10); journal_tick(&reloaded); assert(reloaded.journal.xp == old_xp);
    reloaded.n_fish = 1; journal_tick(&reloaded);
    assert(reloaded.journal.seen == t.journal.seen && reloaded.journal.grown == t.journal.grown);
    char *json = progression_backup_json(&t); assert(json);
    assert(progression_restore_json(json, strlen(json), true, error, sizeof error));
    cJSON *root = cJSON_Parse(json), *obj = cJSON_GetObjectItemCaseSensitive(root, "tank");
    replace_number(obj, "journal_seen", 0); invalid_object(root);
    root = cJSON_Parse(json); obj = cJSON_GetObjectItemCaseSensitive(root, "tank");
    replace_number(obj, "journal_xp", 0); invalid_object(root);
    root = cJSON_Parse(json); obj = cJSON_GetObjectItemCaseSensitive(root, "tank");
    remove_journal_fields(obj); replace_number(root, "schema", 3);
    char *old_json = cJSON_PrintUnformatted(root); cJSON_Delete(root); assert(old_json);
    assert(progression_restore_json(old_json, strlen(old_json), true, error, sizeof error));
    saved_size = 2000; tank_init(&reloaded, 8); boot_enlarged(&reloaded);
    assert(!reloaded.journal.xp && !reloaded.journal.seen && !reloaded.journal.initialized);
    assert(reloaded.fish[0].variety == FISH_SHARK && reloaded.sd_unlocks == t.sd_unlocks);
    journal_tick(&reloaded); assert(!reloaded.journal.xp && (reloaded.journal.seen & (1u << FISH_SHARK)));
    free(json); free(old_json);
    size_t pixels = TANK_W * TANK_H;
    uint16_t *frames = calloc(JOURNAL_THEME_COUNT * pixels + 2, sizeof *frames), *cache = calloc(pixels, sizeof *cache);
    uint32_t *dirty = calloc(RENDER_DIRTY_WORDS, sizeof *dirty); assert(frames && cache && dirty);
    frames[0] = frames[JOURNAL_THEME_COUNT * pixels + 1] = 0xbeef;
    t.n_fish = 0; t.journal.xp = journal_level_xp(15); t.sd_unlocks = 0;
    for (int cached = 0; cached < 2; cached++) {
        render_set_scene_cache(cached ? cache : NULL); render_set_dirty_mask(cached ? dirty : NULL);
        for (int theme = 0; theme < JOURNAL_THEME_COUNT; theme++) {
            t.journal.theme = (uint8_t)theme;
            render_tank(&t, frames + 1 + theme * pixels, TANK_W);
            for (int previous = 0; previous < theme; previous++)
                assert(memcmp(frames + 1 + theme * pixels, frames + 1 + previous * pixels, pixels * 2));
            if (theme) {
                size_t different = 0;
                for (size_t p = 0; p < pixels; p++) different += frames[1 + theme * pixels + p] != frames[1 + p];
                assert(different > pixels / 6);
            }
            if (theme == 6) {
                const uint16_t *ice = frames + 1 + theme * pixels;
                for (int x = 58; x <= 175; x += 39) {
                    for (int y = 0; y < 6; y++) {
                        int ice_green = (ice[y * TANK_W + x] >> 5) & 63;
                        int water_green = (ice[y * TANK_W + x + 18] >> 5) & 63;
                        assert(ice_green > water_green + 4);
                    }
                }
            }
        }
    }
    render_journal_open(); render_journal(&t, frames + 1, TANK_W);
    for (int tab = 0; tab < 3; tab++) {
        render_journal_open_section(tab);
        int inert_y = tab == 0 ? JOURNAL_GOAL_Y + 60 : JOURNAL_TAB_Y + 10;
        assert(!render_journal_tap(&t, render_frame_x(60 + tab * 136), render_frame_y(inert_y)));
        render_journal(&t, frames + 1 + tab * pixels, TANK_W);
        uint16_t *again = calloc(pixels, sizeof *again); assert(again);
        assert(!render_journal_tap(&t, render_frame_x(60 + ((tab + 1) % 3) * 136), render_frame_y(inert_y)));
        render_journal(&t, again, TANK_W);
        assert(!memcmp(frames + 1 + tab * pixels, again, pixels * 2));
        free(again);
    }
    assert(memcmp(frames + 1, frames + 1 + pixels, pixels * 2));
    assert(memcmp(frames + 1 + pixels, frames + 1 + 2 * pixels, pixels * 2));
    assert(frames[0] == 0xbeef && frames[JOURNAL_THEME_COUNT * pixels + 1] == 0xbeef);
    assert(render_journal_tap(&t, render_frame_x(SET_CLOSE_X + 4), render_frame_y(SET_CLOSE_Y + 4)) == 1);
    render_set_scene_cache(NULL); render_set_dirty_mask(NULL);
    free(frames); free(cache); free(dirty);
    t.n_fish = N_FISH_START;
    assert(!journal_theme_unlocked(&t, -1) && !journal_theme_unlocked(&t, JOURNAL_THEME_COUNT));
    for (int theme = 0; theme < JOURNAL_THEME_COUNT; theme++) {
        t.journal.xp = journal_level_xp(JOURNAL_THEME_LEVELS[theme]);
        assert(journal_theme_unlocked(&t, theme));
        if (theme) {
            t.journal.xp--;
            assert(!journal_set_theme(&t, theme));
            t.journal.xp++;
        }
        reject_write = true;
        uint8_t old_theme = t.journal.theme;
        assert(!journal_set_theme(&t, theme) && t.journal.theme == old_theme);
        reject_write = false;
        assert(journal_set_theme(&t, theme));
        tank_init(&reloaded, 8); boot_enlarged(&reloaded);
        assert(reloaded.journal.theme == theme && reloaded.journal.xp == t.journal.xp);
        char *theme_json = progression_backup_json(&t); assert(theme_json);
        assert(progression_restore_json(theme_json, strlen(theme_json), true, error, sizeof error));
        cJSON *theme_root = cJSON_Parse(theme_json); assert(theme_root);
        cJSON *theme_tank = cJSON_GetObjectItemCaseSensitive(theme_root, "tank");
        replace_number(theme_tank, "journal_theme", JOURNAL_THEME_COUNT); invalid_object(theme_root);
        free(theme_json);
    }
    t.journal.xp = JOURNAL_XP_MAX - 1; t.player_feedings++;
    journal_tick(&t); assert(t.journal.xp == JOURNAL_XP_MAX - 1);
    t.journal.claimed = 0; t.sd_balance = INT_MAX;
    int previous_writes = writes;
    assert(!journal_claim(&t, 0) && !t.journal.claimed && writes == previous_writes);
    assert(progression_reset(&t, 9) && !t.journal.claimed && !t.journal.xp && !t.journal.theme && !t.journal.seen);
    puts("PASS: Journal XP thresholds/deltas, 15 rewards, failure rollback, album, schema 3/2000 migration, themes and UI");
}
static void test_variety_unlocks(void) {
    tank_t t, loaded; saved_size = 0; tank_init(&t, 42); boot_enlarged(&t);
    assert(!journal_variety_unlocked(&t, -1) && !journal_variety_unlocked(&t, FISH_VARIETY_COUNT));
    for (int level = 1; level <= JOURNAL_LEVEL_MAX; level++) {
        t.journal.xp = journal_level_xp(level);
        for (int v = 0; v < FISH_VARIETY_COUNT; v++)
            assert(journal_variety_unlocked(&t, v) == (level >= JOURNAL_VARIETY_LEVELS[v]));
        for (int roll = 0; roll < 30; roll++) {
            progression_fry_roll(&t);
            for (int k = 0; k < FRY_OFFER_N; k++) {
                fry_offer_t offer; progression_fry_offer(&t, k, &offer);
                assert(journal_variety_unlocked(&t, offer.variety));
            }
        }
        setup_begin_customize(&t, 0);
        for (int step = 0; step < FISH_VARIETY_COUNT; step++) setup_activate(&t, SETUP_HIT_CUSTOM0);
        setup_activate(&t, SETUP_HIT_NEXT); setup_take_renamed();
        assert(JOURNAL_VARIETY_LEVELS[t.fish[0].variety] <= level);
        /* Test level gating independently of discoveries from customization. */
        t.fish[0].variety = FISH_CLASSIC; t.journal.seen = 0;
    }
    t.journal.xp = 0;
    t.fish[0].variety = FISH_SHARK;
    t.bank_count = 1; t.bank[0].fish.pad = FISH_JELLYFISH;
    assert(journal_variety_unlocked(&t, FISH_SHARK) && journal_variety_unlocked(&t, FISH_JELLYFISH));
    journal_tick(&t);
    t.fish[0].variety = FISH_CLASSIC; t.bank_count = 0;
    assert(journal_variety_unlocked(&t, FISH_SHARK) && journal_variety_unlocked(&t, FISH_JELLYFISH));
    assert(!journal_variety_unlocked(&t, FISH_STINGRAY));
    assert(progression_save(&t));
    tank_init(&loaded, 43); boot_enlarged(&loaded);
    assert(journal_variety_unlocked(&loaded, FISH_JELLYFISH) && !journal_variety_unlocked(&loaded, FISH_STINGRAY));
    t.journal.seen = 0; t.fish[0].variety = FISH_SHARK;
    setup_begin_customize(&t, 0); setup_activate(&t, SETUP_HIT_CUSTOM0);
    reject_write = true; setup_activate(&t, SETUP_HIT_NEXT);
    assert(setup_active() && !t.journal.seen && t.fish[0].variety == FISH_SHARK);
    reject_write = false; setup_activate(&t, SETUP_HIT_NEXT); setup_take_renamed();
    assert(t.fish[0].variety == FISH_CLASSIC && journal_variety_unlocked(&t, FISH_SHARK));
    assert(progression_save(&t));
    tank_init(&loaded, 43); boot_enlarged(&loaded);
    assert(journal_variety_unlocked(&loaded, FISH_SHARK));
    puts("PASS: ten level-gated varieties, fresh Classic-only tanks, unlocked shop offers, customization, owned/seen/Bank grandfathering and save persistence");
}
static void test_daily_care(void) {
    tank_t t, loaded;
    int64_t original_time = test_unix;
    saved_size = 0; tank_init(&t, 42); boot_enlarged(&t);
    t.ui_cover = false; t.night = false;
    journal_tick(&t);
    assert(!t.care.ready && !progression_care_claim(&t, 0));
    t.player_feedings++; journal_tick(&t);
    assert(t.care.ready == 1 && !t.journal.xp);
    reject_write = true;
    assert(!progression_care_claim(&t, 0) && !t.care.claimed && !t.sd_balance && !t.journal.xp);
    reject_write = false;
    assert(progression_care_claim(&t, 0) && t.sd_balance == 25 && t.journal.xp == 12);
    assert(!progression_care_claim(&t, 0));
    care_state_t before = t.care;
    reject_write = true;
    assert(!companion_greet(&t, 0) && !memcmp(&before, &t.care, sizeof before));
    reject_write = false;
    assert(companion_greet(&t, 0) && (t.care.ready & 2));
    t.fish[0].hunger = 0; t.fish[0].stress = 0;
    t.ui_cover = true;
    progression_tick(&t, 300); assert(!t.care.viewed_s && !(t.care.ready & 4));
    t.ui_cover = false;
    progression_tick(&t, 299); assert(t.care.viewed_s == 299 && !(t.care.ready & 4));
    progression_tick(&t, 1); assert(t.care.ready == 7);
    int coins = t.sd_balance; uint32_t xp = t.journal.xp;
    assert(progression_care_claim(&t, 1) && progression_care_claim(&t, 2));
    assert(t.sd_balance == coins + 75 && t.journal.xp == xp + 38 && t.care.claimed == 7);
    for (int i = 0; i < 100; i++) {
        t.player_feedings++; t.algae_colonies++; t.trim_px += 10 * PX_PER_CM;
        journal_tick(&t);
        assert(!progression_care_claim(&t, 0));
    }
    assert(t.journal.xp == xp + 38);
    for (int game = 0; game < 18; game++) {
        int reward = game < 12 ? 10 : companion_game_reward(game, game == 12 ? 16 : game == 13 ? 8 : 2048, 8);
        assert(companion_game_claim(&t, game, game == 12 ? 16 : game == 13 ? 8 : 2048, 8, reward, false));
    }
    assert(t.care.game_paid == 5 && t.journal.xp == xp + 38);
    assert(progression_save(&t));
    tank_init(&loaded, 43); boot_enlarged(&loaded);
    assert(loaded.care.claimed == 7 && loaded.care.game_paid == 5 && loaded.journal.xp == t.journal.xp);
    saved_size = 6008;
    tank_init(&loaded, 43); boot_enlarged(&loaded);
    assert(progression_storage_ready() && loaded.sd_balance == t.sd_balance && loaded.journal.xp == t.journal.xp);
    assert(loaded.care.version == 1 && !loaded.care.claimed && !loaded.care.game_paid);
    for (size_t partial = 6009; partial < AQUARIUM_SAVE_BYTES; partial++) {
        saved_size = partial;
        tank_init(&loaded, 43); boot_enlarged(&loaded);
        assert(!progression_storage_ready() && saved_size == partial);
    }
    saved_size = AQUARIUM_SAVE_BYTES;
    tank_init(&loaded, 43); boot_enlarged(&loaded);
    assert(progression_storage_ready() && loaded.care.claimed == 7 && loaded.care.game_paid == 5);
    char *json = progression_backup_json(&loaded); assert(json);
    assert(progression_restore_json(json, strlen(json), true, error, sizeof error));
    cJSON *root = cJSON_Parse(json); assert(root);
    cJSON *care = cJSON_GetObjectItemCaseSensitive(root, "care");
    replace_number(care, "game_paid", 6); invalid_object(root);
    root = cJSON_Parse(json); care = cJSON_GetObjectItemCaseSensitive(root, "care");
    replace_number(care, "ready", 0); invalid_object(root);
    root = cJSON_Parse(json);
    replace_number(root, "schema", 16);
    char *old = cJSON_PrintUnformatted(root); assert(old);
    assert(progression_restore_json(old, strlen(old), true, error, sizeof error));
    tank_init(&loaded, 43); boot_enlarged(&loaded);
    assert(loaded.journal.xp == t.journal.xp && loaded.sd_balance == t.sd_balance);
    free(old); cJSON_Delete(root); free(json);
    /* Current-save reload, not the explicit old-backup restore, retains today's quota. */
    assert(progression_save(&t));
    test_unix -= 86400;
    progression_care_refresh(&t); assert(t.care.claimed == 7 && t.care.game_paid == 5);
    test_unix += 86400 * 8;
    progression_care_refresh(&t);
    assert(!t.care.claimed && !t.care.ready && !t.care.game_paid);
    assert(!progression_care_claim(&t, 0)); /* No back-pay for missed days. */
    test_unix = 0;
    t.care.claimed = t.care.ready = 7; t.care.game_paid = 5;
    t.care.elapsed_s = 86399;
    progression_tick(&t, 1);
    assert(!t.care.claimed && !t.care.game_paid);
    t.care.claimed = t.care.ready = 7;
    test_unix = original_time + 86400 * 7;
    progression_care_refresh(&t); assert(t.care.claimed == 7);
    test_unix = 0; t.care.elapsed_s = 86399;
    progression_tick(&t, 1);
    assert(t.care.fallback && !t.care.claimed);
    t.care.claimed = t.care.ready = 7; t.care.game_paid = 5;
    assert(progression_save(&t));
    tank_init(&loaded, 43); boot_enlarged(&loaded);
    assert(loaded.care.fallback && loaded.care.claimed == 7 && loaded.care.game_paid == 5);
    test_unix = original_time + 86400 * 8;
    progression_care_refresh(&loaded);
    assert(!loaded.care.fallback && loaded.care.claimed == 7 && loaded.care.game_paid == 5);
    t = loaded;
    /* Alternative connection and maintenance routes need no bought helpers or games. */
    test_unix += 86400; progression_care_refresh(&t);
    t.hold_active = true; t.hold_time = 3;
    t.hold_x = t.fish[0].x; t.hold_y = t.fish[0].y;
    t.night = false; t.ui_cover = false; t.fish[0].stress = 0;
    progression_tick(&t, 59); assert(!(t.care.ready & 2));
    progression_tick(&t, 1); assert(t.care.ready & 2);
    t.algae_colonies++; journal_tick(&t); assert(t.care.ready & 4);
    unsigned goal_xp = 0, goal_coins = 0, shop = 0;
    for (int g = 0; g < JOURNAL_GOAL_N; g++) { goal_xp += JOURNAL_GOALS[g].xp; goal_coins += JOURNAL_GOALS[g].coins; }
    for (int i = 0; i < SD_ITEM_COUNT; i++) shop += SD_ITEMS[i].price;
    unsigned earned = 0, earned_xp = 0;
    for (int day = 1; day <= 84; day++) {
        test_unix += 86400; progression_care_refresh(&t);
        t.player_feedings++; journal_tick(&t);
        progression_care_greet(&t);
        t.hold_active = false; t.fish[0].hunger = 0; t.fish[0].stress = 0;
        progression_tick(&t, 300);
        int start_coins = t.sd_balance; uint32_t start_xp = t.journal.xp;
        for (int task = 0; task < CARE_TASK_COUNT; task++) assert(progression_care_claim(&t, task));
        earned += t.sd_balance - start_coins; earned_xp += t.journal.xp - start_xp;
        assert(earned == (unsigned)day * 100 && earned_xp == (unsigned)day * 50);
        if (day == 28) assert(earned_xp + goal_xp < journal_level_xp(15));
        if (day == 56) assert(earned_xp + goal_xp >= journal_level_xp(15) && earned + goal_coins >= shop);
        if (day == 84) assert(earned + goal_coins >= shop + 7 * AQUARIUM_PRICE);
    }
    test_unix = original_time;
    puts("PASS: daily care claims/rollback, real greetings, timed calm/view routes, no spam XP, all-game shared cap, reload/JSON/schema16 migration, backward/no clock, missed days, and 12-week economy budget");
}
static void test_game_rewards(void) {
    tank_t t, loaded; saved_size = 0; tank_init(&t, 42); boot_enlarged(&t);
    int previous_writes = writes;
    assert(!progression_game_reward(&t, 0) && !progression_game_reward(&t, 11) && writes == previous_writes);
    reject_write = true;
    assert(!progression_game_reward(&t, 8) && !t.sd_balance && !t.sd_earned && !t.journal.xp);
    reject_write = false;
    for (int reward = 1; reward <= 10; reward++) {
        int balance = t.sd_balance; uint32_t xp = t.journal.xp;
        uint32_t goals = t.journal.claimed;
        assert(progression_game_reward(&t, reward));
        assert(t.sd_balance == balance + (reward <= CARE_GAME_LIMIT) && t.sd_earned == t.sd_balance);
        assert(t.journal.xp == xp && t.journal.claimed == goals);
        tank_init(&loaded, 43); boot_enlarged(&loaded);
        assert(loaded.sd_balance == t.sd_balance && loaded.journal.xp == t.journal.xp);
    }
    t.journal.xp = JOURNAL_XP_MAX - 1;
    assert(progression_game_reward(&t, 10) && t.journal.xp == JOURNAL_XP_MAX - 1);
    previous_writes = writes; t.sd_balance = INT_MAX;
    assert(!progression_game_reward(&t, 1) && writes == previous_writes);
    t.sd_balance = 0; t.sd_earned = INT_MAX;
    assert(!progression_game_reward(&t, 1) && writes == previous_writes);
    uint16_t *frame = calloc(TANK_W * TANK_H + 2, sizeof *frame); assert(frame);
    frame[0] = frame[TANK_W * TANK_H + 1] = 0xbeef;
    for (int empty = 0; empty < 2; empty++) {
        if (empty) t.n_fish = 0;
        for (int game = 0; game < 6; game++) {
            render_games_open();
            assert(!render_games_tap(&t, render_frame_x(110 + game % 2 * 204), render_frame_y(140 + game / 2 * 64)));
            render_games_tick(NAN); render_games_tick(-1); render_games_tick(0); render_games_tick(1000);
            render_games(&t, frame + 1, TANK_W);
            assert(frame[0] == 0xbeef && frame[TANK_W * TANK_H + 1] == 0xbeef);
        }
    }
    free(frame);
    puts("PASS: bounded game rewards, atomic coin/Journal XP persistence, save rollback, overflow and XP cap");
}
static void test_companion(void) {
    tank_t t, loaded; saved_size = 0;
    tank_init(&t, 75); boot_enlarged(&t); progression_setup_done(&t);
    assert(t.companion.cozy && companion_captions(&t) && !companion_lively(&t));
    {
        tank_t fresh;
        game_records_t empty = {0};
        memset(&fresh, 0xa5, sizeof fresh);
        tank_init(&fresh, 74);
        assert(!companion_lively(&fresh) && !memcmp(&fresh.games, &empty, sizeof empty));
        memset(&fresh.games, 0xa5, sizeof fresh.games);
        companion_init(&fresh);
        assert(!companion_lively(&fresh) && !memcmp(&fresh.games, &empty, sizeof empty));
    }
    int meals = t.player_feedings, eaten = t.fish[0].eaten_player;
    float trust = t.fish[0].trust;
    t.autofeed_off = true; t.fish[0].hunger = 9; t.fish[0].energy = 0;
    tank_tick(&t, 0.1f, NULL); progression_tick(&t, 0.1f);
    assert(t.fish[0].hunger <= 6 && t.fish[0].energy >= 4);
    assert(t.player_feedings == meals && t.fish[0].eaten_player == eaten && t.fish[0].trust == trust);
    uint32_t powered = t.companion.powered_s;
    progression_slept(&t, 3600);
    assert(t.fish[0].hunger <= 6 && t.companion.powered_s == powered);
    assert(companion_greet(&t, 0)); assert(!companion_greet(&t, 0));
    assert(t.companion.active[0].bond == 5 && t.companion.active[0].greetings == 1);
    companion_tick(&t, 30); assert(companion_greet(&t, 0));
    t.fish[0].dart_x = -999; t.fish[0].dart_y = -999; t.fish[0].dart_timer = 2;
    assert(companion_trick(&t, 0, 0) && !companion_trick(&t, 0, 1));
    assert(t.fish[0].dart_timer == 0);
    tank_tick(&t, .1f, advisor_rules);
    assert(t.fish[0].dart_x >= 0 && t.fish[0].dart_y >= 0);
    assert(t.companion.active[0].tricks == 1 && t.companion.active[0].bond == 10);
    assert(companion_collection_ready(&t, 1) && companion_collection_claim(&t, 1));
    assert(!companion_collection_claim(&t, 1));
    assert(t.companion.cosmetic == 2);
    companion_state_t state = t.companion;
    reject_write = true;
    assert(!companion_set_options(&t, false, false, false));
    assert(!memcmp(&state, &t.companion, sizeof state));
    reject_write = false;
    assert(companion_set_options(&t, false, false, false));
    assert(!t.companion.cozy && !companion_captions(&t) && !companion_lively(&t));
    assert(companion_set_options(&t, true, true, true));
    {
        /* Favorites/captions: derived, deterministic, and read-only. */
        tank_t before = t; char a[96], b[96], fav[48];
        int g = companion_favorite(&t, 0); assert(g >= 0 && g < GOAL_COUNT && g == companion_favorite(&t, 0));
        companion_favorite_text(&t, 0, fav, sizeof fav); assert(strstr(fav, "(by nature)") || strstr(fav, "(seen!)"));
        t.fish[0].sociable0 = 0.9f; t.fish[0].bold0 = 0.1f; t.fish[0].ms_bits &= ~MS_FIRST_FOLLOW;
        assert(companion_favorite(&t, 0) == GOAL_FOLLOW_FRIEND);
        companion_favorite_text(&t, 0, fav, sizeof fav); assert(!strcmp(fav, "swimming with tankmates (by nature)"));
        t.fish[0].ms_bits |= MS_FIRST_FOLLOW;
        companion_favorite_text(&t, 0, fav, sizeof fav); assert(!strcmp(fav, "swimming with tankmates (seen!)"));
        /* Compact CARE labels: every favorite, NATURE and SEEN, fits one 7px native line. */
        assert(250 * TANK_H / PAGE_H + 7 <= 268 * TANK_H / PAGE_H && 268 * TANK_H / PAGE_H + 7 <= 284 * TANK_H / PAGE_H);
        assert((220 + 30) * TANK_H / PAGE_H <= 250 * TANK_H / PAGE_H); /* below HELLO buttons, above tricks */
        static const float favorite_bold[] = {0.5f, 0.9f, 0.9f, 0.1f, 0.1f, 0.5f, 0.5f, 0.5f};
        static const float favorite_social[] = {0.9f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f};
        static const int favorite_preset[] = {0, 1, 0, 1, 0, 0, 1, 2};
        uint32_t found = 0;
        for (int seen = 0; seen < 2; seen++) for (int k = 0; k < 8; k++) {
            t.fish[0].bold0 = favorite_bold[k]; t.fish[0].sociable0 = favorite_social[k];
            t.fish[0].preset = favorite_preset[k]; t.fish[0].ms_bits = seen ? 0xffffffffu : 0;
            found |= 1u << companion_favorite(&t, 0);
            char label[64]; companion_favorite_label(&t, 0, label, sizeof label);
            assert(strlen(label) <= 34 && !strncmp(label, "LIKES: ", 7));
            assert(strstr(label, seen && companion_favorite(&t, 0) != GOAL_EXPLORE ? "(SEEN)" : "(NATURE)"));
            assert((14 + render_text_w(label, 1)) * TANK_W / PAGE_W <= TANK_W - 12);
        }
        assert(found == ((1u << GOAL_VISIT_BUBBLES) | (1u << GOAL_FOLLOW_FRIEND) | (1u << GOAL_EXPLORE) |
                         (1u << GOAL_REST) | (1u << GOAL_DART_PLAY) | (1u << GOAL_INSPECT_REEF)));
        assert((14 + render_text_w("Bonds never fade. Hello: every 30s.", 1)) * TANK_W / PAGE_W <= TANK_W - 4);        t = before; t.greet_timer = 0; t.companion.last_discovery = 0; assert(t.n_fish >= 2);
        t.fish[0].goal.id = GOAL_FOLLOW_FRIEND;
        t.visitor_left = 0;
        t.companion.away = t.companion.adventure_done = 0;
        for (int j = 0; j < t.n_fish; j++) { t.fish[j].hunger = 0; t.fish[j].stress = 0; }
        t.companion.powered_s = 0; before = t;
        companion_status(&t, a, sizeof a); companion_status(&t, b, sizeof b);
        assert(!strcmp(a, b) && !a[0]);
        t.companion.powered_s = 6; companion_status(&t, a, sizeof a);
        assert(!a[0]);
        t.fish[0].hunger = 8; companion_status(&t, a, sizeof a);
        assert(strstr(a, "HUNGRY") && strstr(a, t.fish[0].name));
        t.fish[0].hunger = 0; t.fish[0].stress = 7; companion_status(&t, a, sizeof a);
        assert(strstr(a, "NEEDS CALM"));
        t.fish[0].stress = 0;
        t.companion.powered_s = 0; assert(!memcmp(&before, &t, sizeof t));
        strcpy(t.companion.portrait_name, "Nori");
        t.companion.away = t.companion.adventure = 1;
        companion_status(&t, a, sizeof a); assert(!strcmp(a, "Nori: ADVENTURING"));
        t.companion.away = 0; t.companion.adventure_done = 1;
        companion_status(&t, a, sizeof a); assert(!strcmp(a, "Nori: POSTCARD READY"));
        t.companion.adventure_claimed = 1;
        companion_status(&t, a, sizeof a); assert(!a[0]);
        t = before;
        /* The companion clock runs under a cover; nothing else changes. */
        t.ui_cover = true; fish_t fishes[N_FISH_MAX]; memcpy(fishes, t.fish, sizeof fishes);
        uint32_t disc = t.companion.discoveries; t.fish[0].goal_age = 5; memcpy(fishes, t.fish, sizeof fishes);
        int wallet = t.sd_balance;
        companion_tick(&t, 31); assert(t.companion.powered_s == 31 && t.companion.discoveries == disc);
        assert(!memcmp(fishes, t.fish, sizeof fishes) && t.sd_balance == wallet);
        companion_tick(&t, 0); assert(t.companion.powered_s == 31);
        t = before;
    }
    int fish = add_bank_candidate(&t, 5);
    t.companion.active[fish].bond = 60; t.companion.active[fish].tricks = 7;
    assert(!progression_bank_move(&t, -1, fish));
    assert(t.companion.bank[0].bond == 60 && !t.companion.active[t.n_fish].bond);
    assert(!progression_bank_move(&t, 0, 0));
    assert(t.companion.active[0].bond == 60 && t.companion.bank[0].bond == 10);
    fish = 0;
    assert(companion_adventure_choice(&t, 1));
    t.fish[fish].variety = FISH_BETTA; t.fish[fish].pattern = PATTERN_SPOTS;
    test_unix = 0;
    tank_t before_departure = t;
    reject_write = true; assert(!companion_adventure_start(&t, 0, fish)); reject_write = false;
    assert(!memcmp(&t, &before_departure, sizeof t));
    assert(companion_adventure_start(&t, 0, fish));
    assert(t.n_fish == before_departure.n_fish - 1 && t.companion.away);
    assert(!strcmp(t.companion.traveler.name, before_departure.fish[fish].name));
    assert(t.companion.traveler_bond.bond == 60);
    assert(t.companion.portrait_variety == FISH_BETTA && t.companion.portrait_pattern == PATTERN_SPOTS);
    {
        /* The STORY portrait is a real fish drawing inside its frame; text stays left of it. */
        assert(render_text_w("WWWWWWW 12H 00M LEFT", 2) + 14 < 330);
        assert(render_text_w("SHELF OFF", 2) <= 116);
        uint16_t *pfb = calloc((size_t)TANK_W * TANK_H, sizeof *pfb); assert(pfb);
        companion_open_section(COMPANION_SECTION_ADVENTURES);
        companion_render(&t, pfb, TANK_W);
        uint16_t colors[16]; int distinct = 0;
        for (int y = 140 * TANK_H / PAGE_H; y < 196 * TANK_H / PAGE_H; y++)
            for (int x = 336 * TANK_W / PAGE_W; x < 428 * TANK_W / PAGE_W; x++) {
                uint16_t px = pfb[y * TANK_W + x]; int k = 0;
                while (k < distinct && colors[k] != px) k++;
                if (k == distinct && distinct < 16) colors[distinct++] = px;
            }
        assert(distinct >= 4);
        free(pfb);
    }
    char portrait[8]; memcpy(portrait, t.companion.portrait_name, sizeof portrait);
    assert(!companion_adventure_choice(&t, 0));
    assert(!strcmp(t.companion.portrait_name, portrait));
    assert(!companion_adventure_start(&t, 1, 0));
    /* A games/modal frame may tick timers, never fish care or goals. */
    t.ui_cover = true; fish_t old_fish[N_FISH_MAX]; memcpy(old_fish, t.fish, sizeof old_fish);
    companion_tick(&t, 37.75f); companion_tick(&t, 0.25f);
    assert(t.companion.adventure_s == 38 && !memcmp(t.fish, old_fish, sizeof old_fish));
    assert(progression_save(&t)); tank_init(&loaded, 76); boot_enlarged(&loaded);
    assert(loaded.companion.adventure_s == 38 && loaded.companion.adventure_choice == 1);
    assert(loaded.companion.traveler_bond.bond == 60 && loaded.companion.bank[0].bond == 10);
    assert(!strcmp(loaded.companion.portrait_name, portrait));
    assert(loaded.companion.portrait_variety == FISH_BETTA && loaded.companion.portrait_pattern == PATTERN_SPOTS);
    reject_write = true; companion_tick(&loaded, COMPANION_ADVENTURE_SECONDS[0] - 38);
    assert(loaded.companion.away && !loaded.companion.adventure_done && loaded.n_fish == 1);
    reject_write = false; companion_tick(&loaded, 1);
    assert(!loaded.companion.away && loaded.n_fish == 2 && loaded.companion.active[1].bond == 60);
    assert(!strcmp(loaded.fish[1].name, portrait) && loaded.fish[1].variety == FISH_BETTA &&
           loaded.fish[1].pattern == PATTERN_SPOTS);
    assert(loaded.companion.adventure_done == 1 && loaded.companion.adventure_routes == 1);
    state = loaded.companion; int coins = loaded.sd_balance; uint32_t xp = loaded.journal.xp;
    reject_write = true; assert(!companion_adventure_claim(&loaded));
    assert(!memcmp(&state, &loaded.companion, sizeof state) && coins == loaded.sd_balance && xp == loaded.journal.xp);
    reject_write = false; assert(companion_adventure_claim(&loaded)); assert(!companion_adventure_claim(&loaded));
    assert(loaded.sd_balance == coins + 10 && !loaded.companion.adventure);
    for (int a = 1; a < 3; a++) {
        assert(companion_adventure_choice(&loaded, 0)); assert(companion_adventure_start(&loaded, a, 0));
        companion_tick(&loaded, COMPANION_ADVENTURE_SECONDS[a] + 10);
        assert(companion_adventure_claim(&loaded)); assert(!companion_adventure_start(&loaded, a, 0));
    }
    test_unix = 2000000000;
    assert(companion_collection_ready(&loaded, 2) && companion_collection_claim(&loaded, 2));
    assert(!companion_collection_claim(&loaded, 2) && loaded.companion.adventure_done == 7);
    loaded.fish[0].goal_age = 3; loaded.fish[0].goal.id = GOAL_EXPLORE;
    companion_tick(&loaded, 0.1f); assert(loaded.companion.discoveries & 2u);
    loaded.fish[0].goal.id = GOAL_VISIT_BUBBLES; loaded.fish[0].at_bubbles = true;
    companion_tick(&loaded, 0.1f); assert(loaded.companion.discoveries & 1u);
    loaded.fish[0].goal.id = GOAL_REST;
    float x0, x1, top; int fronds; tank_veg_bed(&loaded, 0, &x0, &x1, &top, &fronds);
    loaded.fish[0].x = (x0 + x1) / 2; loaded.fish[0].y = TANK_BOT - 10;
    assert(tank_in_grass(&loaded, loaded.fish[0].x, loaded.fish[0].y));
    companion_tick(&loaded, 0.1f);
    loaded.fish[0].goal.id = GOAL_FOLLOW_FRIEND; companion_tick(&loaded, 0.1f);
    loaded.fish[0].goal.id = GOAL_DART_PLAY; companion_tick(&loaded, 0.1f);
    loaded.fish[0].goal.id = GOAL_INSPECT_REEF; loaded.fish[0].ms_bits |= MS_INSPECTED;
    companion_tick(&loaded, 0.1f); assert(companion_collection_ready(&loaded, 0));
    state = loaded.companion; reject_write = true;
    assert(!companion_collection_claim(&loaded, 0) && !memcmp(&state, &loaded.companion, sizeof state));
    reject_write = false; assert(companion_collection_claim(&loaded, 0)); assert(!companion_collection_claim(&loaded, 0));
    /* All six record metrics, gold thresholds, relaxed reward-only, and atomic rollback. */
    static const int scores[] = {24, 0, 24, 10, 40, 0}, moves[] = {0, 16, 0, 0, 0, 18};
    for (int g = 0; g < 6; g++) assert(companion_game_claim(&loaded, g, scores[g], moves[g], 2, false));
    assert(loaded.companion.game_challenges == 63 && loaded.companion.featured == 0);
    for (int g = 0; g < 6; g++) assert(loaded.companion.game_rounds[g] == 1 && loaded.companion.game_medals[g] == 3);
    assert(loaded.companion.game_best[1] == 16 && loaded.companion.game_best[5] == 18);
    state = loaded.companion; coins = loaded.sd_balance;
    assert(companion_game_claim(&loaded, 0, 100, 0, 1, true));
    assert(!memcmp(&state, &loaded.companion, sizeof state) && loaded.sd_balance == coins);
    reject_write = true; coins = loaded.sd_balance; xp = loaded.journal.xp;
    assert(!companion_game_claim(&loaded, 0, 200, 0, 2, false));
    assert(!memcmp(&state, &loaded.companion, sizeof state) && coins == loaded.sd_balance && xp == loaded.journal.xp);
    reject_write = false;
    loaded.sd_balance = INT_MAX; assert(!companion_game_claim(&loaded, 0, 200, 0, 2, false));
    assert(!memcmp(&state, &loaded.companion, sizeof state)); loaded.sd_balance = coins;
    loaded.companion.game_rounds[0] = UINT16_MAX;
    assert(companion_game_claim(&loaded, 0, 70000, 0, 1, false));
    assert(loaded.companion.game_rounds[0] == UINT16_MAX && loaded.companion.game_best[0] == UINT16_MAX);
    assert(companion_valid(&loaded.companion));
    char *json = progression_backup_json(&loaded); assert(json);
    assert(progression_restore_json(json, strlen(json), true, error, sizeof error));
    tank_init(&t, 77); boot_enlarged(&t);
    assert(!memcmp(&loaded.companion, &t.companion, sizeof t.companion));
    cJSON *root = cJSON_Parse(json), *obj = cJSON_GetObjectItemCaseSensitive(root, "companion");
    replace_number(obj, "cosmetic", 4); invalid_object(root);
    root = cJSON_Parse(json); obj = cJSON_GetObjectItemCaseSensitive(root, "companion");
    replace_number(obj, "portrait_variety", FISH_VARIETY_COUNT); invalid_object(root);
    root = cJSON_Parse(json); obj = cJSON_GetObjectItemCaseSensitive(root, "companion");
    cJSON_DeleteItemFromObjectCaseSensitive(obj, "portrait_pattern"); invalid_object(root);
    root = cJSON_Parse(json); obj = cJSON_GetObjectItemCaseSensitive(root, "companion");
    cJSON_DeleteItemFromObjectCaseSensitive(obj, "game_rounds"); invalid_object(root);
    root = cJSON_Parse(json); obj = cJSON_GetObjectItemCaseSensitive(root, "companion");
    replace_number(obj, "collections", 8); invalid_object(root);
    root = cJSON_Parse(json); obj = cJSON_GetObjectItemCaseSensitive(root, "companion");
    replace_number(obj, "adventure", 1); invalid_object(root); /* Claimed adventures cannot run again. */
    root = cJSON_Parse(json); obj = cJSON_GetObjectItemCaseSensitive(root, "companion");
    const char *packed = cJSON_GetObjectItemCaseSensitive(obj, "active")->valuestring;
    char invalid_bonds[N_FISH_MAX * sizeof(companion_bond_t) * 2 + 1];
    strcpy(invalid_bonds, packed);
    memcpy(invalid_bonds + 8, "ffff", 4); /* Frozen offset 4: bond cannot exceed 100. */
    assert(cJSON_ReplaceItemInObjectCaseSensitive(obj, "active", cJSON_CreateString(invalid_bonds)));
    invalid_object(root);
    root = cJSON_Parse(json); replace_number(root, "schema", 5);
    char *legacy = cJSON_PrintUnformatted(root); assert(legacy); cJSON_Delete(root);
    assert(progression_restore_json(legacy, strlen(legacy), true, error, sizeof error));
    tank_init(&t, 78); boot_enlarged(&t);
    assert(!t.companion.cozy && !companion_captions(&t) && !t.companion.discoveries);
    free(legacy);
    assert(progression_restore_json(json, strlen(json), true, error, sizeof error));
    saved_size = 4000; tank_init(&t, 79); boot_enlarged(&t);
    assert(!t.companion.cozy && !companion_captions(&t));
    assert(t.n_fish == loaded.n_fish && !strcmp(t.fish[0].name, loaded.fish[0].name));
    free(json);
    int sold = add_bank_candidate(&t, 6);
    t.companion.active[sold].bond = 40; state = t.companion;
    int population = t.n_fish; reject_write = true;
    assert(!progression_sell_fish(&t, sold) && population == t.n_fish && !memcmp(&state, &t.companion, sizeof state));
    reject_write = false; assert(progression_sell_fish(&t, sold));
    assert(t.n_fish == population - 1 && !t.companion.active[t.n_fish].bond);
    /* Shorter checksummed SD snapshots survive appended tails, then migrate. */
    const char *directory = getenv("POCKET_TANK_TEST_SLOTS"); assert(directory);
    uint32_t id[2] = {0xfeed1234, 0xdead5678};
    unsigned char before[4000], after[AQUARIUM_SAVE_BYTES];
    memset(before, 0x5a, sizeof before);
    assert(aquarium_store_write(directory, id, 0, before, sizeof before));
    memset(after, 0xff, sizeof after);
    assert(aquarium_store_read(directory, id, 0, after, sizeof after));
    assert(!memcmp(before, after, sizeof before));
    for (size_t i = sizeof before; i < sizeof after; i++) assert(after[i] == 0);
    after[4000] = 1;
    assert(aquarium_store_write(directory, id, 0, after, sizeof after));
    memset(after, 0, sizeof after); assert(aquarium_store_read(directory, id, 0, after, sizeof after));
    assert(after[4000] == 1);
    uint16_t *fb = calloc((size_t)TANK_W * TANK_H + 2, sizeof *fb); assert(fb);
    fb[0] = fb[TANK_W * TANK_H + 1] = 0xbeef;
    loaded.companion.decor = 0;
    companion_render_decor(&loaded, fb + 1, TANK_W);
    for (int i = 1; i <= TANK_W * TANK_H; i++) assert(fb[i] == 0);
    assert(companion_set_decor(&loaded, true));
    companion_render_decor(&loaded, fb + 1, TANK_W);
    int changed = 0; for (int i = 1; i <= TANK_W * TANK_H; i++) changed += fb[i] != 0;
    assert(changed > 20);
    companion_open();
    for (int tab = 0; tab < 3; tab++) {
        companion_tap(&loaded, render_frame_x(30 + tab * 146), render_frame_y(60));
        companion_render(&loaded, fb + 1, TANK_W);
        assert(fb[0] == 0xbeef && fb[TANK_W * TANK_H + 1] == 0xbeef);
    }
    assert(companion_tap(&loaded, render_frame_x(392), render_frame_y(20)));
    free(fb);
    puts("PASS: cozy migration, real departures/automatic return, permanent bonds/identity, adventure save rollback, collections, six game records, schema 7 and legacy SD migration");
}
static void test_adventure_traveler(void) {
    tank_t t, loaded; saved_size = 0; test_unix = 2000000000;
    tank_init(&t, 91); boot_enlarged(&t); progression_setup_done(&t);
    fish_t original = t.fish[0]; float age = progression_age_s(&t, 0);
    t.companion.active[0].bond = 25;
    assert(companion_adventure_start(&t, 0, 0) && t.n_fish == 1);
    bank_fish_t held = t.companion.traveler;
    assert(!strcmp(held.name, original.name) && held.fish.trust == original.trust);
    assert(!companion_adventure_start(&t, 1, 0));
    char *json = progression_backup_json(&t); assert(json);
    assert(progression_restore_json(json, strlen(json), true, error, sizeof error)); free(json);
    tank_init(&loaded, 92); boot_enlarged(&loaded);
    assert(loaded.n_fish == 1 && loaded.companion.away && !memcmp(&held, &loaded.companion.traveler, sizeof held));
    loaded.ui_cover = false; tank_tick(&loaded, 1, NULL); progression_tick(&loaded, 1);
    assert(!memcmp(&held, &loaded.companion.traveler, sizeof held)); /* No care or aging while absent. */
    test_unix += 7199; companion_tick(&loaded, .25f);
    assert(loaded.companion.away && loaded.companion.adventure_s == 7199);
    test_unix += 1; companion_tick(&loaded, .25f);
    assert(!loaded.companion.away && loaded.n_fish == 2 && loaded.companion.active[1].bond == 25);
    assert(!strcmp(loaded.fish[1].name, original.name) && progression_age_s(&loaded, 1) == age);
    assert(loaded.fish[1].trust == original.trust && loaded.fish[1].hunger == original.hunger);
    assert(progression_save(&loaded)); tank_init(&t, 93); boot_enlarged(&t);
    assert(t.n_fish == 2 && !t.companion.away && t.companion.adventure_done == 1);
    int population = t.n_fish; companion_tick(&t, 1); assert(t.n_fish == population);
    /* A legacy schema-6 companion remains importable and keeps its old story timer. */
    json = progression_backup_json(&t); cJSON *root = cJSON_Parse(json); free(json); assert(root);
    replace_number(root, "schema", 6);
    cJSON *s = cJSON_GetObjectItemCaseSensitive(root, "companion");
    cJSON_DeleteItemFromObjectCaseSensitive(s, "traveler"); cJSON_DeleteItemFromObjectCaseSensitive(s, "traveler_bond");
    cJSON_DeleteItemFromObjectCaseSensitive(s, "away"); cJSON_DeleteItemFromObjectCaseSensitive(s, "adventure_clock");
    cJSON_DeleteItemFromObjectCaseSensitive(s, "portrait_wearable");
    cJSON_DeleteItemFromObjectCaseSensitive(s, "portrait_colors"); cJSON_DeleteItemFromObjectCaseSensitive(s, "creature_colors");
    replace_number(s, "version", 1); replace_number(s, "adventure_s", 120);
    json = cJSON_PrintUnformatted(root); cJSON_Delete(root); assert(json);
    assert(progression_restore_json(json, strlen(json), true, error, sizeof error)); free(json);
    tank_init(&t, 94); boot_enlarged(&t); assert(t.companion.adventure_s == 120 && !t.companion.away);
    saved_size = 4392; tank_init(&loaded, 95); boot_enlarged(&loaded);
    assert(loaded.n_fish == t.n_fish && loaded.companion.version == 1 && !loaded.companion.away);
    /* Reserving the return slot also blocks bank withdrawals and new fry. */
    saved_size = 0; tank_init(&t, 96); boot_enlarged(&t); progression_setup_done(&t);
    int fish = add_bank_candidate(&t, 3); assert(!progression_bank_move(&t, -1, fish));
    while (t.n_fish < POP_CAP) add_bank_candidate(&t, t.n_fish);
    assert(companion_adventure_start(&t, 0, 0) && t.n_fish == POP_CAP - 1);
    assert(progression_bank_move(&t, 0, -1) && !progression_fry_room(&t));
    assert(tank_add_fish(&t, 0, 1) < 0);
    test_unix += 7200; companion_tick(&t, .25f);
    assert(t.n_fish == POP_CAP && !t.companion.away);
    puts("PASS: real absent fish has frozen identity/care/age, 2-hour wall-clock return once, schema-6 migration and reserved return capacity");
}
static void test_fish_sand_clearance(void) {
    uint16_t with_fish[TANK_W * TANK_H], without_fish[TANK_W * TANK_H];
    for (int variety = 0; variety < FISH_VARIETY_COUNT; variety++)
        for (int stage = STAGE_FRY; stage <= STAGE_ELDER; stage++)
            for (int pose = 0; pose < 5; pose++)
              for (int wear = WEAR_NONE; wear < WEAR_COUNT; wear++) {
                tank_t t; tank_init(&t, 42);
                t.n_fish = 1; t.autofeed_off = true;
                fish_t *f = &t.fish[0];
                f->variety = (uint8_t)variety;
                f->stage = (stage_t)stage;
                f->size = stage == STAGE_FRY ? 0.35f : stage == STAGE_JUV ? 0.7f : 1.3f;
                f->x = TANK_W / 2; f->y = TANK_H - 1;
                f->heading = (pose - 2) * 0.7f;
                f->speed = pose % 2 ? 95 : 5;
                f->yaw = pose == 2 ? 0 : 1; f->yaw_tail = -f->yaw;
                f->goal.id = GOAL_REST;
                t.companion.active[0].wearable = (uint8_t)wear;
                t.companion.active[0].wearables = 63;
                tank_tick(&t, 0.01f, NULL);
                assert(f->y + tank_fish_clearance(f) <= TANK_SAND_TOP + 0.001f);
                for (int phase = 0; phase < 3; phase++) {
                    t.clock = phase * 0.4f;
                    render_tank(&t, with_fish, TANK_W);
                    t.n_fish = 0;
                    render_tank(&t, without_fish, TANK_W);
                    t.n_fish = 1;
                    for (int y = TANK_SAND_TOP; y < TANK_H; y++)
                        for (int x = 0; x < TANK_W; x++)
                            assert(with_fish[y * TANK_W + x] == without_fish[y * TANK_W + x]);
                }
                f->hunger = 8; f->goal.id = GOAL_SEEK_FOOD;
                f->heading = 0; f->speed = 0;
                f->y = TANK_SAND_TOP - tank_fish_clearance(f);
                t.food[0].alive = true; t.food[0].x = f->x;
                t.food[0].y = FOOD_FLOOR_Y; t.food[0].floor_s = 1;
                int eaten = f->eaten;
                tank_tick(&t, 0.01f, NULL);
                assert(!t.food[0].alive && f->eaten == eaten + 1);
            }
    for (int variety = FISH_SHARK; variety <= FISH_JELLYFISH; variety += FISH_JELLYFISH - FISH_SHARK) {
        tank_t t; tank_init(&t, 42); t.n_fish = 1;
        fish_t *f = &t.fish[0];
        f->variety = (uint8_t)variety; f->stage = STAGE_ADULT;
        f->size = 1; f->heading = 0; f->speed = 0; tank_fish_face(f);
        float clearance = tank_fish_clearance(f);
        assert(clearance <= (variety == FISH_SHARK ? 19 : 24));
        f->x = TANK_W / 2; f->y = TANK_SAND_TOP - clearance;
        render_tank(&t, with_fish, TANK_W); t.n_fish = 0;
        render_tank(&t, without_fish, TANK_W);
        int last = -1;
        for (int y = 0; y < TANK_SAND_TOP; y++) for (int x = 0; x < TANK_W; x++)
            if (with_fish[y * TANK_W + x] != without_fish[y * TANK_W + x]) last = y;
        assert(last >= TANK_SAND_TOP - 4);
    }
    puts("PASS: ten varieties x four stages x seven wearables x dive/turn/speed/animation poses stay above sand and eat floor pellets; shark/jellyfish rendered tips approach within four pixels");
}
static void test_wearables(void) {
    tank_t t, loaded; saved_size = 0; test_unix = 2000000000;
    tank_init(&t, 93); boot_enlarged(&t); progression_setup_done(&t);
    assert(!t.fish[0].wearable && !t.companion.active[0].wearables);
    t.sd_balance = 1000; t.journal.xp = 0;
    assert(!companion_wearable(&t, 0, WEAR_CROWN));
    t.journal.xp = 10000;
    t.sd_balance = 0; assert(!companion_wearable(&t, 0, WEAR_TOP_HAT)); t.sd_balance = 1000;
    companion_state_t before = t.companion;
    reject_write = true; assert(!companion_wearable(&t, 0, WEAR_TOP_HAT)); reject_write = false;
    assert(t.sd_balance == 1000 && !t.fish[0].wearable && !memcmp(&before, &t.companion, sizeof before));
    for (int i = 1; i < WEAR_COUNT; i++) {
        int32_t wallet = t.sd_balance;
        assert(companion_wearable(&t, 0, i) && t.sd_balance == wallet - WEAR_PRICES[i]);
        assert(t.fish[0].wearable == i && (t.companion.active[0].wearables & (1u << (i - 1))));
        wallet = t.sd_balance;
        assert(companion_wearable(&t, 0, WEAR_NONE) && !t.fish[0].wearable);
        assert(companion_wearable(&t, 0, i) && t.sd_balance == wallet);
    }
    assert(t.companion.active[0].wearables == 63 && companion_valid(&t.companion));
    reject_write = true; assert(!companion_wearable(&t, 0, WEAR_NONE)); reject_write = false;
    assert(t.fish[0].wearable == WEAR_BANDANA && t.companion.active[0].wearable == WEAR_BANDANA);
    tank_init(&loaded, 94); boot_enlarged(&loaded);
    assert(loaded.fish[0].wearable == WEAR_BANDANA && loaded.companion.active[0].wearables == 63);
    char *json = progression_backup_json(&loaded); assert(json);
    assert(progression_restore_json(json, strlen(json), true, error, sizeof error));
    tank_init(&t, 95); boot_enlarged(&t); free(json);
    assert(t.fish[0].wearable == WEAR_BANDANA && t.companion.active[0].wearables == 63);
    companion_state_t invalid = t.companion;
    invalid.active[0].wearable = WEAR_COUNT; assert(!companion_valid(&invalid));
    invalid = t.companion; invalid.active[0].wearables = 0; assert(!companion_valid(&invalid));
    invalid = t.companion; invalid.bank[19].wearables = 128; assert(!companion_valid(&invalid));
    invalid = t.companion; invalid.color_pad[0] = 1; assert(!companion_valid(&invalid));
    invalid = t.companion; invalid.version = 2; assert(!companion_valid(&invalid));
    invalid = t.companion; invalid.portrait_wearable = WEAR_COUNT; assert(!companion_valid(&invalid));
    int candidate = add_bank_candidate(&t, 4);
    assert(companion_wearable(&t, candidate, WEAR_CROWN));
    assert(!progression_bank_move(&t, -1, candidate));
    assert(t.companion.bank[0].wearable == WEAR_CROWN);
    assert(!progression_bank_move(&t, 0, 0));
    assert(t.fish[0].wearable == WEAR_CROWN && t.companion.bank[0].wearables == 63);
    assert(!progression_bank_move(&t, 0, -1));
    assert(t.fish[t.n_fish - 1].wearable == WEAR_BANDANA);
    assert(companion_adventure_start(&t, 0, 0));
    assert(t.companion.traveler_bond.wearable == WEAR_CROWN && t.companion.portrait_wearable == WEAR_CROWN);
    json = progression_backup_json(&t); assert(json);
    assert(progression_restore_json(json, strlen(json), true, error, sizeof error)); free(json);
    tank_init(&loaded, 96); boot_enlarged(&loaded);
    assert(loaded.companion.traveler_bond.wearable == WEAR_CROWN && loaded.companion.portrait_wearable == WEAR_CROWN);
    test_unix += 7200; companion_tick(&loaded, .25f);
    assert(!loaded.companion.away && loaded.fish[loaded.n_fish - 1].wearable == WEAR_CROWN);
    assert(loaded.companion.portrait_wearable == WEAR_CROWN);
    int fry = tank_add_fish(&loaded, 0, 1); assert(fry >= 0);
    assert(!loaded.fish[fry].wearable && !loaded.companion.active[fry].wearables);
    /* Explicit schema-7 fixture still loads without new equipment fields. */
    saved_size = 0; tank_init(&t, 97); boot_enlarged(&t);
    json = progression_backup_json(&t); cJSON *root = cJSON_Parse(json); free(json); assert(root);
    replace_number(root, "schema", 7);
    cJSON *state = cJSON_GetObjectItemCaseSensitive(root, "companion");
    replace_number(state, "version", 2); cJSON_DeleteItemFromObjectCaseSensitive(state, "portrait_wearable");
    cJSON_DeleteItemFromObjectCaseSensitive(state, "portrait_colors"); cJSON_DeleteItemFromObjectCaseSensitive(state, "creature_colors");
    json = cJSON_PrintUnformatted(root); cJSON_Delete(root);
    assert(progression_restore_json(json, strlen(json), true, error, sizeof error)); free(json);
    tank_init(&loaded, 98); boot_enlarged(&loaded);
    assert(!loaded.fish[0].wearable && !loaded.companion.active[0].wearables);
    uint16_t *portraits = calloc(WEAR_COUNT * TANK_W * TANK_H, sizeof *portraits); assert(portraits);
    size_t pixels = TANK_W * TANK_H;
    for (int variety = 0; variety < FISH_VARIETY_COUNT; variety++)
        for (int size = 0; size < 2; size++) {
            memset(portraits, 0, WEAR_COUNT * pixels * sizeof *portraits);
            fish_t f = loaded.fish[0]; f.variety = (uint8_t)variety; f.stage = STAGE_ADULT;
            for (int wear = 0; wear < WEAR_COUNT; wear++) {
                f.wearable = (uint8_t)wear;
                render_fish_portrait(portraits + wear * pixels, TANK_W, 224, 170, size ? 1.8f : .55f, &f, 0);
                for (int previous = 0; previous < wear; previous++)
                    assert(memcmp(portraits + previous * pixels, portraits + wear * pixels, pixels * sizeof *portraits));
            }
        }
    free(portraits);
    puts("PASS: six per-fish wearables, level/funds gates, single-charge purchases, free equip/remove, failure rollback, binary/schema-8 JSON/schema-7 migration, Bank/swap/adventure/portraits and unequipped newborns");
}
static void test_cosmetic_colors(void) {
    tank_t t, loaded; saved_size = 0; test_unix = 2000000000;
    tank_init(&t, 95); boot_enlarged(&t); progression_setup_done(&t);
    t.sd_balance = 1000; t.journal.xp = 10000;
    companion_state_t before = t.companion;
    reject_write = true; assert(!companion_colors(&t, false, CREATURE_SHRIMP, 0x3b)); reject_write = false;
    assert(!memcmp(&before, &t.companion, sizeof before));
    assert(companion_colors(&t, true, 0, 0x93));
    assert(!t.companion.active[0].wearables && !t.fish[0].wearable);
    assert(t.fish[0].wear_colors == 0x93 && t.sd_balance == 1000 && t.journal.xp == 10000);
    before = t.companion; reject_write = true;
    assert(!companion_colors(&t, true, 0, 0xff)); reject_write = false;
    assert(t.fish[0].wear_colors == 0x93 && !memcmp(&before, &t.companion, sizeof before));
    for (int i = 0; i < CREATURE_COUNT; i++) assert(companion_colors(&t, false, i, (uint8_t)(0x31 + i)));
    assert(companion_wearable(&t, 0, WEAR_CROWN));
    assert(progression_save(&t)); tank_init(&loaded, 96); boot_enlarged(&loaded);
    assert(loaded.fish[0].wear_colors == 0x93 && loaded.companion.active[0].wear_colors == 0x93);
    assert(!memcmp(t.companion.creature_colors, loaded.companion.creature_colors, CREATURE_COUNT));
    char *json = progression_backup_json(&loaded); assert(json);
    assert(progression_restore_json(json, strlen(json), true, error, sizeof error)); free(json);
    tank_init(&t, 97); boot_enlarged(&t);
    json = progression_backup_json(&t); cJSON *bad = cJSON_Parse(json); free(json); assert(bad);
    cJSON *bad_colors = cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(bad, "companion"), "creature_colors");
    assert(cJSON_ReplaceItemInArray(bad_colors, 0, cJSON_CreateNumber(256))); invalid_object(bad);
    int candidate = add_bank_candidate(&t, 4);
    assert(companion_colors(&t, true, candidate, 0xd2));
    assert(!progression_bank_move(&t, -1, candidate) && t.companion.bank[0].wear_colors == 0xd2);
    assert(!progression_bank_move(&t, 0, 0));
    assert(t.fish[0].wear_colors == 0xd2 && t.companion.bank[0].wear_colors == 0x93);
    assert(!progression_bank_move(&t, 0, -1));
    int traveler = t.n_fish - 1;
    assert(companion_adventure_start(&t, 0, traveler));
    assert(t.companion.portrait_colors == 0x93 && t.companion.traveler_bond.wear_colors == 0x93);
    assert(progression_save(&t)); tank_init(&loaded, 98); boot_enlarged(&loaded);
    test_unix += 7200; companion_tick(&loaded, .25f);
    assert(loaded.fish[loaded.n_fish - 1].wear_colors == 0x93 && loaded.companion.portrait_colors == 0x93);
    int fry = tank_add_fish(&loaded, 0, 1); assert(fry >= 0 && !loaded.fish[fry].wear_colors);
    saved_size = 0; tank_init(&t, 99); boot_enlarged(&t); t.companion.version = 3;
    assert(progression_save(&t)); assert(saved_size == AQUARIUM_SAVE_BYTES);
    saved_size = 4512; tank_init(&loaded, 100); boot_enlarged(&loaded);
    assert(loaded.companion.version == 3 && !loaded.companion.creature_colors[0]);
    json = progression_backup_json(&loaded); cJSON *root = cJSON_Parse(json); free(json); assert(root);
    replace_number(root, "schema", 8); cJSON *state = cJSON_GetObjectItemCaseSensitive(root, "companion");
    cJSON_DeleteItemFromObjectCaseSensitive(state, "portrait_colors"); cJSON_DeleteItemFromObjectCaseSensitive(state, "creature_colors");
    json = cJSON_PrintUnformatted(root); cJSON_Delete(root);
    assert(progression_restore_json(json, strlen(json), true, error, sizeof error)); free(json);
    tank_init(&loaded, 101); boot_enlarged(&loaded);
    assert(!loaded.fish[0].wear_colors && !loaded.companion.creature_colors[0]);
    companion_state_t invalid = loaded.companion;
    invalid.active[0].wear_colors = 1; assert(!companion_valid(&invalid));
    uint16_t *frames = calloc(3 * TANK_W * TANK_H, sizeof *frames); assert(frames);
    size_t bytes = TANK_W * TANK_H * sizeof *frames;
    for (int type = 0; type < CREATURE_COUNT; type++) {
        memset(frames, 0, bytes);
        render_creature_portrait(frames, TANK_W, 224, 165, type, 0, 1);
        for (int c = 1; c < COSMETIC_COLOR_COUNT; c++) {
            memset(frames + TANK_W * TANK_H, 0, bytes * 2);
            render_creature_portrait(frames + TANK_W * TANK_H, TANK_W, 224, 165, type, (uint8_t)c, 1);
            render_creature_portrait(frames + 2 * TANK_W * TANK_H, TANK_W, 224, 165, type, (uint8_t)(c << 4), 1);
            assert(memcmp(frames, frames + TANK_W * TANK_H, bytes));
            assert(memcmp(frames, frames + 2 * TANK_W * TANK_H, bytes));
        }
    }
    fish_t f = loaded.fish[0]; f.stage = STAGE_ADULT;
    for (int wear = 1; wear < WEAR_COUNT; wear++) {
        f.wearable = (uint8_t)wear; f.wear_colors = 0; memset(frames, 0, bytes);
        render_fish_portrait(frames, TANK_W, 224, 165, 1.8f, &f, 1);
        for (int c = 1; c < COSMETIC_COLOR_COUNT; c++) {
            memset(frames + TANK_W * TANK_H, 0, bytes * 2);
            f.wear_colors = (uint8_t)c;
            render_fish_portrait(frames + TANK_W * TANK_H, TANK_W, 224, 165, 1.8f, &f, 1);
            f.wear_colors = (uint8_t)(c << 4);
            render_fish_portrait(frames + 2 * TANK_W * TANK_H, TANK_W, 224, 165, 1.8f, &f, 1);
            assert(memcmp(frames, frames + TANK_W * TANK_H, bytes));
            assert(memcmp(frames, frames + 2 * TANK_W * TANK_H, bytes));
        }
    }
    for (int type = 0; type < CREATURE_COUNT; type++) {
        tank_init(&t, 103); t.n_fish = 0; t.companion.decor = t.companion.captions = 0;
        t.sd_unlocks = type == CREATURE_SHRIMP ? SD_ITEM_SHRIMP : type == CREATURE_SNAIL ? SD_ITEM_SNAIL :
                       type == CREATURE_URCHIN ? SD_ITEM_URCHIN : SD_ITEM_SEAHORSE;
        t.shrimp_n = type == CREATURE_SHRIMP ? 1 : 0;
        t.shrimp[0].x = 110; t.shrimp[0].y = 100; t.shrimp[0].yaw = 1;
        t.snail_x = 110; t.snail_y = 80; t.snail_heading = 0;
        t.urchin_x = 110; t.sh_x = 110; t.sh_y = 100; t.sh_mode = SH_DRIFT; t.sh_hidden = false;
        render_tank(&t, frames, TANK_W);
        t.companion.creature_colors[type] = 0x3b;
        render_tank(&t, frames + TANK_W * TANK_H, TANK_W);
        assert(memcmp(frames, frames + TANK_W * TANK_H, bytes));
        t.companion.creature_colors[type] = 0;
        render_tank(&t, frames + 2 * TANK_W * TANK_H, TANK_W);
        assert(!memcmp(frames, frames + 2 * TANK_W * TANK_H, bytes));
    }
    free(frames);
    puts("PASS: free primary/detail colors on four actual creatures and six accessories; all 15 tints render distinctly, Natural restores exactly, rollback/Bank/adventure/portraits/newborns, 4520-byte/schema-9 and 4512-byte/schema-8 migration");
}
static void compact_draw(tank_t *t, uint16_t *fb, int stride, int kind) {
    if (kind == 0) render_tank(t, fb, stride);
    else if (kind == 1) render_life(t, fb, stride);
    else if (kind == 2) render_milestones(t, fb, stride);
    else if (kind == 3) render_settings(t, fb, stride, 75);
    else if (kind >= 4 && kind <= 11) companion_render(t, fb, stride);
    else if (kind == 12) { render_tank(t, fb, stride); render_stats_card(t, 0, fb, stride); }
    else if (kind == 13) render_journal(t, fb, stride);
    else if (kind == 14) render_bank(t, fb, stride);
    else if (kind == 15) render_shop(t, fb, stride);
    else if (kind == 16) render_games(t, fb, stride);
    else if (kind == 17) { render_tank(t, fb, stride); render_confirm_reset(fb, stride, 1); }
    else render_tank(t, fb, stride);
}
static void test_theme_edges(void) {
#if CYD_FLAT_EDGES
    tank_t t;
    for (int mode = 0; mode < 2; mode++) {
        tank_resolution(mode != 0);
        tank_init(&t, 123);
        t.night = false;
        t.journal.xp = journal_level_xp(15);
        t.n_fish = 0;
        for (int i = 0; i < MAX_BUBBLE; i++) t.bubble[i].y = TANK_H / 2;
        memset(t.algae, 0, sizeof t.algae);
        uint16_t *fb = calloc((size_t)TANK_W * TANK_H, sizeof *fb);
        assert(fb);
        for (int theme = 3; theme <= 6; theme += 3) {
            t.journal.theme = theme;
            render_tank(&t, fb, TANK_W);
            unsigned star = (fb[0] >> 11) + ((fb[0] >> 5) & 63) + (fb[0] & 31);
            unsigned water = (fb[1] >> 11) + ((fb[1] >> 5) & 63) + (fb[1] & 31);
            if (star <= water + 15)
                fprintf(stderr, "Theme edge: mode %d theme %d star %04x water %04x\n", mode, theme, fb[0], fb[1]);
            assert(star > water + 15);
        }
        free(fb);
    }
    tank_resolution(false);
#endif
}

static void test_compact(void) {
    tank_resolution(false);
    tank_t t; tank_init(&t, 43); saved_size = 0; boot_enlarged(&t); setup_cancel(&t);
    float x = t.fish[0].x, bubble = t.bubble_x;
    int balance = t.sd_balance; uint32_t xp = t.journal.xp;
    reject_write = true;
    assert(!companion_compact(&t, true) && TANK_W == 240 && t.fish[0].x == x);
    reject_write = false;
    assert(companion_compact(&t, true) && TANK_W == 320 && TANK_H == 240);
    assert(fabsf(t.fish[0].x - x * 4 / 3) < .001f && fabsf(t.bubble_x - bubble * 4 / 3) < .001f);
    tank_resolution(false);
    tank_t loaded; tank_init(&loaded, 7); boot_enlarged(&loaded);
    assert(TANK_W == 320 && loaded.companion.display_compact && loaded.sd_balance == balance && loaded.journal.xp == xp);
    char error[128], *json = progression_backup_json(&loaded); assert(json);
    assert(progression_restore_json(json, strlen(json), false, error, sizeof error)); free(json);
    assert(companion_compact(&t, false) && fabsf(t.fish[0].x - x) < .001f);
    for (int mode = 0; mode < 2; mode++) {
        assert(companion_compact(&t, mode != 0));
        int stride = TANK_W + 3, height = mode ? 80 : 60;
        size_t pixels = (size_t)stride * TANK_H, tile_pixels = (size_t)stride * height;
        uint16_t *full = malloc(pixels * sizeof *full), *tile = malloc((tile_pixels + 2) * sizeof *tile);
        size_t cache_words = (86400 - 320 * 80 * 2) / 2;
        uint16_t *backdrop = malloc((cache_words + 2) * sizeof *backdrop);
        assert(full && tile && backdrop);
        backdrop[0] = backdrop[cache_words + 1] = 0x5a5a;
        render_life_open(); render_journal_open(); render_bank_open(); render_games_open(); render_milestones_leave();
        for (int kind = 0; kind <= 18 + JOURNAL_THEME_COUNT * 2; kind++) {
            if (kind >= 18) {
                t.journal.xp = journal_level_xp(15);
                t.journal.theme = (kind - 18) % JOURNAL_THEME_COUNT;
                t.night = kind >= 18 + JOURNAL_THEME_COUNT;
                for (int item = 9; item < 30; item++) t.sd_unlocks |= UINT32_C(1) << item;
            }
            if (kind == 3) settings_section(&t, 0);
            if (kind >= 4 && kind <= 11) companion_open_section(kind - 4);
            for (size_t i = 0; i < pixels; i++) full[i] = 0xa5a5;
            render_tile(0, 0); compact_draw(&t, full, stride, kind);
            render_set_backdrop_cache(backdrop + 1,
                kind == 18 + JOURNAL_THEME_COUNT * 2 ? 8 : cache_words * sizeof *backdrop);
            for (int y = 0; y < TANK_H; y += height) {
                for (size_t i = 0; i < tile_pixels + 2; i++) tile[i] = 0xa5a5;
                render_tile(y, height); compact_draw(&t, tile + 1, stride, kind);
                assert(tile[0] == 0xa5a5 && tile[tile_pixels + 1] == 0xa5a5);
                for (int row = 0; row < height; row++) {
                    if (memcmp(tile + 1 + row * stride, full + (y + row) * stride, TANK_W * sizeof *tile)) {
                        fprintf(stderr, "Tile differs: mode %d page %d row %d\n", mode, kind, y + row); assert(false);
                    }
                    for (int col = TANK_W; col < stride; col++) assert(tile[1 + row * stride + col] == 0xa5a5);
                }
            }
            render_tile(0, 0);
            render_set_backdrop_cache(NULL, 0);
            assert(backdrop[0] == 0x5a5a && backdrop[cache_words + 1] == 0x5a5a);
        }
        free(full); free(tile); free(backdrop);
    }
    test_fish_sand_clearance();
    { extern void test_clock_weather(void); test_clock_weather(); }
    assert(companion_compact(&t, false));
    json = progression_backup_json(&t); assert(json);
    cJSON *legacy = cJSON_Parse(json); free(json); assert(legacy);
    replace_number(legacy, "schema", 9);
    replace_number(cJSON_GetObjectItemCaseSensitive(legacy, "companion"), "version", 4);
    json = cJSON_PrintUnformatted(legacy); cJSON_Delete(legacy); assert(json);
    assert(progression_restore_json(json, strlen(json), false, error, sizeof error)); free(json);
    puts("PASS: Enlarged/Normal cold resolution load, position round-trip, rollback, JSON, pixel-identical compressed/live backgrounds and tiled tank/menus/cards; all themes day/night, undersized-cache live rows and guarded strides");
}
static goal_t keep_swim_goal(const tank_t *t, int fish, bool request) {
    (void)request;
    return t->fish[fish].goal;
}
static void test_fish_tile_culling(void) {
    tank_resolution(true);
    tank_t t; tank_init(&t, 39); tank_new_population(&t); setup_cancel(&t);
    t.n_fish = 1; t.sd_unlocks = 0; t.aquariums.clock_theme = CLOCK_OFF;
    uint16_t *full = malloc((size_t)TANK_W * TANK_H * sizeof *full);
    uint16_t *tile = malloc((size_t)TANK_W * 80 * sizeof *tile);
    assert(full && tile);
    for (int variety = 0; variety < FISH_VARIETY_COUNT; variety++)
        for (int stage = 0; stage <= STAGE_ELDER; stage++)
            for (int wearable = 0; wearable < WEAR_COUNT; wearable++) {
                fish_t *f = &t.fish[0];
                f->variety = (uint8_t)variety; f->stage = (stage_t)stage; f->wearable = (uint8_t)wearable;
                f->size = stage == STAGE_ELDER ? 1.3f : 1.0f;
                f->heading = (wearable - 3) * .35f; f->speed = 120;
                f->yaw = .7f; f->yaw_tail = -.6f; f->x = TANK_W / 2;
                f->y = 80 + (stage - 1) * 14;
                render_tile(0, 0); render_tank(&t, full, TANK_W);
                for (int y = 0; y < TANK_H; y += 80) {
                    render_tile(y, 80); render_tank(&t, tile, TANK_W);
                    assert(!memcmp(full + y * TANK_W, tile, (size_t)TANK_W * 80 * sizeof *tile));
                }
                for (int x = 0; x < TANK_W; x += 32) {
                    render_tile(80, 40); render_region(x, 32); render_tank(&t, tile, TANK_W);
                    for (int y = 0; y < 40; y++)
                        assert(!memcmp(full + (80 + y) * TANK_W + x, tile + y * TANK_W + x, 32 * sizeof *tile));
                }
                render_region(0, 0);
            }
    free(full); free(tile); render_tile(0, 0); tank_resolution(false);
    puts("PASS: fish tile culling preserves all varieties, stages and wearables at tile boundaries");
}
static void test_swim_extent(void) {
    for (int compact = 0; compact < 2; compact++) {
        tank_resolution(compact != 0);
        tank_t t; tank_init(&t, 712); tank_new_population(&t); setup_cancel(&t);
        t.n_fish = 1; t.stage_fish = -1; t.autofeed_off = true;
        t.fish[0].size = 1.1f;
        t.fish[0].goal.id = GOAL_EXPLORE; t.fish[0].goal.urgency = 3;
        float left = TANK_W, right = 0;
        for (int i = 0; i < 9000; i++) {
            t.fish[0].hunger = 0; t.fish[0].energy = 8; t.fish[0].stress = 0;
            t.fish[0].breather = 0; t.night = false; t.spawning = false; t.court_active = 0;
            tank_tick(&t, .1f, keep_swim_goal);
            fish_t *f = &t.fish[0];
            assert(isfinite(f->x) && isfinite(f->y) && f->x >= 15 * f->size && f->x <= TANK_W - 15 * f->size);
            if (f->x < left) left = f->x;
            if (f->x > right) right = f->x;
        }
        if (left > 32 || right < TANK_W - 32) {
            fprintf(stderr, "Explore extent mode %d: %.1f..%.1f width %d\n", compact, left, right, TANK_W);
            assert(false);
        }
        int n; tank_veg_bed(&t, 1, NULL, NULL, NULL, &n);
        float last; tank_veg_frond(&t, 1, n - 1, &last);
        assert(last <= TANK_W - 8 && TANK_W - last < 20);
        assert(last == TANK_W - 12);
        for (int lush = 0; lush <= 4; lush++) {
            t.tank_ms_bits = (uint8_t)((1u << lush) - 1);
            t.reef_x = compact ? 48 : 36;
            float root; tank_veg_frond(&t, 0, 0, &root);
            assert(root >= 12);
            for (int bed = 0; bed < tank_veg_beds(&t); bed++) {
                int count; tank_veg_bed(&t, bed, NULL, NULL, NULL, &count);
                for (int frond = 0; frond < count; frond++) {
                    tank_veg_frond(&t, bed, frond, &root);
                    assert(root >= 12 && root <= TANK_W - 12);
                }
            }
        }
        t.fish[0].goal.id = GOAL_DART_PLAY; t.fish[0].goal.urgency = 9;
        t.fish[0].speed = 0; t.fish[0].bold = .95f; t.fish[0].dart_timer = 0;
        for (int i = 0; i < 100; i++) {
            t.fish[0].energy = 8;
            tank_tick(&t, .1f, keep_swim_goal);
            assert(t.fish[0].speed <= 73 && t.fish[0].target_speed <= 73);
        }
    }
    tank_resolution(false);
    puts("PASS: Enlarged/Normal explorers reach both side-glass bands without clipping, right seagrass fills its last safe slot, and playful dart speed is bounded");
}
static void test_collision_turns(void) {
    for (int compact = 0; compact < 2; compact++) {
        tank_resolution(compact != 0);
        tank_t t; tank_init(&t, 712); tank_new_population(&t); setup_cancel(&t);
        t.n_fish = 2; t.stage_fish = -1; t.autofeed_off = true;
        fish_t *f = &t.fish[0];
        f->goal.id = GOAL_DART_PLAY; f->goal.urgency = 9;
        f->size = 1; f->heading = 0; f->facing = 1; f->yaw = 1; f->behind = 0;
        int flips = 0, previous = f->facing, last_flip = -100;
        for (int i = 0; i < 240; i++) {
            f->x = TANK_W / 2; f->y = 70; f->hunger = 0; f->energy = 8; f->breather = 0;
            f->dart_x = f->x + f->facing * 40; f->dart_y = f->y; f->dart_timer = 2;
            t.fish[1].x = f->x + f->facing * 2; t.fish[1].y = f->y;
            t.night = false; t.spawning = false; t.court_active = 0;
            tank_tick(&t, .05f, keep_swim_goal);
            if (f->facing != previous) {
                assert(i - last_flip >= 13);
                last_flip = i; previous = f->facing; flips++;
            }
            assert(isfinite(f->x) && isfinite(f->yaw));
        }
        assert(flips >= 3 && flips <= 19);
        /* A side-glass impact must not bounce the facing back on the next tick. */
        f->x = 15 * f->size; f->heading = 3.14159f; f->facing = -1; f->yaw = -1;
        f->behind = 0; f->dart_x = -100; f->dart_timer = 10;
        for (int i = 0; i < 20; i++) {
            tank_tick(&t, .05f, keep_swim_goal);
            assert(f->facing == 1 && f->x >= 15 * f->size);
        }
    }
    tank_resolution(false);
    puts("PASS: crowded urgent turns retain a 650 ms settle period, and side-glass impacts do not chatter");
}
static void test_cpu_damage(void) {
    tank_resolution(true);
    tank_t t; tank_init(&t, 54); tank_new_population(&t); setup_cancel(&t);
    t.companion.display_compact = 1;
    t.companion.lively = 0;
    t.journal.xp = journal_level_xp(15);
    size_t pixels = (size_t)TANK_W * TANK_H;
    uint16_t *full = malloc(pixels * sizeof *full), *retained = calloc(pixels, sizeof *retained);
    uint16_t *tile = malloc(((size_t)TANK_W * 80 + 2) * sizeof *tile);
    assert(full && retained && tile);
    render_work_caches(true);
    int narrow_regions = 0;
    for (int i = 0; i < 240; i++) {
        tank_tick(&t, 0.2f, advisor_rules);
        if (i < 160 && i % 20 == 0) { t.journal.theme = (uint8_t)(i / 20); t.night = i % 40 != 0; }
        if (i == 5) for (int item = 9; item < 30; item++) t.sd_unlocks |= UINT32_C(1) << item;
        if (i == 10) { t.snail_x = 80; t.snail_y = SNAIL_FLOOR_Y; }
        if (i == 15) { t.urchin_x = 150; t.sh_x = 130; }
        if (i == 25) t.sd_unlocks = 0;
        if (i == 35 && t.n_fish) t.n_fish--;
        if (i == 45) { t.aquariums.clock_theme = CLOCK_ANALOG; t.clock_requested = true; }
        if (i == 50) { t.companion.cozy = 1; t.companion.lively = 1; t.companion.captions = 1; }
        if (i == 55) t.aquariums.clock_layout = CLOCK_BOTTOM_RIGHT << 2;
        if (i == 65) { t.aquariums.clock_theme = CLOCK_OFF; t.clock_requested = false; }
        if (i == 70) { t.companion.cozy = 0; t.companion.lively = 0; t.companion.captions = 0; }
        if (i == 75) t.algae[0] = 200;
        if (i == 85) { t.water_style = WATER_STYLE_RAINBOW; t.sand_style = SAND_STYLE_RAINBOW; }
        if (i == 95) { t.food[0].alive = true; t.food[0].x = 60; t.food[0].y = 40; }
        if (i == 105) t.food[0].alive = false;
        if (i == 160) {
            tank_init(&t, 54); tank_new_population(&t); setup_cancel(&t); t.n_fish = 1;
            t.companion.display_compact = 1;
            t.sd_unlocks = SD_ITEM_CASTLE | (UINT32_C(1) << 13) | (UINT32_C(1) << 21);
            t.aquariums.clock_theme = CLOCK_OFF; t.clock_requested = false;
        }
        if (i >= 160) {
            memset(t.algae, 0, sizeof t.algae);
            for (int b = 0; b < VEG_BEDS_MAX; b++) for (int f = 0; f < VEG_FRONDS_MAX; f++) t.veg_h[b][f] = 0;
            for (int bubble = 0; bubble < MAX_BUBBLE; bubble++) {
                t.bubble[bubble].x = 12; t.bubble[bubble].y = 50 + bubble * 3;
            }
        }
        if (i == 185) {
            t.aquariums.clock_theme = CLOCK_ANALOG; t.clock_requested = true;
            t.aquariums.clock_layout = CLOCK_TOP_LEFT << 2;
        }
        if (i == 210) t.companion.decor = 1;
        if (i == 215) t.rpg.chapter = 3;
        if (i == 220) t.rpg.chapter = 6;
        if (i == 225) t.rpg.chapter = 0;
        bool force = i == 115;
        if (force) memset(retained, 0x83, pixels * sizeof *retained); /* Returning from a covering page. */
        render_tile(0, 0); render_region(0, 0); render_tank(&t, full, TANK_W);
        render_damage_begin(&t, force);
        int cursor = 0, x, first, width, height;
        while (render_damage_next(&cursor, &x, &first, &width, &height)) {
            if (width < TANK_W) narrow_regions++;
            tile[0] = tile[(size_t)TANK_W * height + 1] = 0x5a5a;
            render_tile(first, height); render_region(x, width); render_tank(&t, tile + 1, TANK_W);
            assert(tile[0] == 0x5a5a && tile[(size_t)TANK_W * height + 1] == 0x5a5a);
            for (int row = 0; row < height; row++)
                memcpy(retained + (first + row) * TANK_W + x, tile + 1 + row * TANK_W + x, (size_t)width * sizeof *tile);
        }
        render_tile(0, 0); render_region(0, 0);
        for (size_t p = 0; p < pixels; p++) if (full[p] != retained[p]) {
            fprintf(stderr, "CPU damage mismatch frame %d x %d y %d\n", i, (int)(p % TANK_W), (int)(p / TANK_W)); assert(false);
        }
    }
    assert(narrow_regions > 10);
    tank_init(&t, 54); t.n_fish = 0; t.sd_unlocks = 0;
    for (int i = 0; i < MAX_BUBBLE; i++) t.bubble[i].y = TANK_BOT - 20;
    for (int b = 0; b < VEG_BEDS_MAX; b++) for (int f = 0; f < VEG_FRONDS_MAX; f++) t.veg_h[b][f] = 0;
    for (int frame = 0; frame < 3; frame++) render_damage_begin(&t, false);
    int rows = 0;
    for (int y = 0; y < TANK_H; y += 80) {
        int first, height; if (render_damage_range(y, 80, &first, &height)) rows += height;
    }
    assert(rows < TANK_H - 20);
    render_work_caches(false);
    free(full); free(retained); free(tile); tank_resolution(false);
    puts("PASS: retained CPU damage output equals full renders across movement, removals, theme/night/palette/clock changes and page returns; unchanged rows avoid CPU drawing");
}
static int damage_area(tank_t *t, bool full) {
    render_damage_begin(t, full);
    int cursor = 0, x, y, w, h, area = 0;
    while (render_damage_next(&cursor, &x, &y, &w, &h)) area += w * h;
    return area;
}
static void test_compact_activity(void) {
    tank_resolution(true);
    tank_t t; tank_init(&t, 91);
    t.companion.display_compact = 1; t.aquariums.clock_theme = CLOCK_OFF;
    for (int n = 0; n <= SHRIMP_MAX; n++) {
        t.shrimp_n = (uint8_t)n;
        assert(tank_shrimp_display_count(&t) == (n + 1) / 2);
        t.companion.display_compact = 0;
        assert(tank_shrimp_display_count(&t) == n);
        t.companion.display_compact = 1;
    }
    for (int b = 0; b < VEG_BEDS_MAX; b++) for (int i = 0; i < VEG_FRONDS_MAX; i++) t.veg_h[b][i] = 0.8f;
    for (int i = 0; i < MAX_BUBBLE; i++) t.bubble[i].x = t.bubble[i].y = -1000;
    t.clock = .05f;
    damage_area(&t, true); damage_area(&t, false); damage_area(&t, false);
    int still = damage_area(&t, false);
    t.clock = .6f;
    int sway = damage_area(&t, false);
    assert(still < TANK_W * TANK_H / 3 && sway > still * 2);
    int settled = damage_area(&t, false);
    assert(settled == still); /* No redundant second grass repaint. */
    size_t pixels = (size_t)TANK_W * TANK_H;
    uint16_t *a = malloc(pixels * sizeof *a), *b = malloc(pixels * sizeof *b);
    assert(a && b);
    t.bubble[1].x = 100; t.bubble[1].y = 80;
    render_tank(&t, a, TANK_W);
    t.bubble[1].x = 200; t.bubble[1].y = 30;
    render_tank(&t, b, TANK_W);
    assert(!memcmp(a, b, pixels * sizeof *a));
    t.bubble[0].x = 100; t.bubble[0].y = 80;
    render_tank(&t, b, TANK_W);
    assert(memcmp(a, b, pixels * sizeof *a));
    free(a); free(b);
    tank_t school[2];
    for (int compact = 0; compact < 2; compact++) {
        tank_init(&school[compact], 15);
        school[compact].autofeed_off = true;
        memset(school[compact].food, 0, sizeof school[compact].food);
        school[compact].sd_unlocks = SD_ITEM_SHRIMP;
        tank_shrimp_place(&school[compact], SHRIMP_MAX);
        school[compact].companion.display_compact = (uint8_t)compact;
        school[compact].shrimp_tx = TANK_W / 2; school[compact].shrimp_ty = 130; school[compact].shrimp_tt = 1000;
    }
    float width[2];
    for (int compact = 0; compact < 2; compact++) {
        for (int frame = 0; frame < 600; frame++) tank_tick(&school[compact], .1f, keep_swim_goal);
        float lo = TANK_W, hi = 0;
        for (int i = 0; i < SHRIMP_MAX; i++) {
            lo = fminf(lo, school[compact].shrimp[i].x); hi = fmaxf(hi, school[compact].shrimp[i].x);
        }
        width[compact] = hi - lo;
        assert(school[compact].shrimp_n == SHRIMP_MAX && !school[compact].shrimp_eaten);
    }
    assert(width[1] < width[0] * .8f);
    tank_resolution(false);
    puts("PASS: Compact shows ceil(population/2) shrimp with a tighter school and unchanged population, half the bubbles, and grass damage only on 2 Hz pose ticks without a redundant repaint");
}
static void test_static_backdrop(void) {
    tank_resolution(true);
    tank_t scenes[12];
    tank_init(&scenes[0], 31); tank_new_population(&scenes[0]); setup_cancel(&scenes[0]);
    scenes[0].sd_unlocks = SD_ITEM_CASTLE | SD_ITEM_CORAL | SD_ITEM_CLUSTER |
        SD_ITEM_PEBBLES | SD_ITEM_PEARL | SD_ITEM_SPONGES | SD_ITEM_BARREL | SD_ITEM_HELMET;
    scenes[0].aquariums.clock_theme = CLOCK_OFF;
    for (int i = 1; i < 12; i++) {
        scenes[i] = scenes[i - 1]; scenes[i].clock += .3f;
        if (i == 2) tank_decor_set(&scenes[i], 2, 65, DECOR_Z_FRONT);
        if (i == 3) scenes[i].coral_growth = .6f;
        if (i == 4) scenes[i].coral_rgb = 0xf09030;
        if (i == 5) scenes[i].cluster_growth = .8f;
        if (i == 6) scenes[i].sd_unlocks |= (UINT32_C(1) << 13) | (UINT32_C(1) << 21);
        if (i == 7) scenes[i].night = true;
        if (i == 8) scenes[i].sd_unlocks |= SD_ITEM_PUMPKIN;
        if (i == 9) scenes[i].night = false;
        if (i == 10) scenes[i].sd_unlocks &= ~SD_ITEM_CASTLE;
    }
    size_t pixels = (size_t)TANK_W * TANK_H, words = 35200 / 2;
    uint16_t *reference = malloc(12 * pixels * sizeof *reference);
    uint16_t *tile = malloc((size_t)TANK_W * 80 * sizeof *tile);
    uint16_t *cache = malloc((words + 2) * sizeof *cache);
    assert(reference && tile && cache);
    cache[0] = cache[words + 1] = 0x5a5a;
    render_set_backdrop_cache(NULL, 0); render_tile(0, 0); render_region(0, 0);
    for (int i = 0; i < 12; i++) render_tank(&scenes[i], reference + i * pixels, TANK_W);
    render_set_backdrop_cache(cache + 1, words * sizeof *cache);
    for (int i = 0; i < 12; i++) for (int y = 0; y < TANK_H; y += 80) for (int x = 0; x < TANK_W; x += 32) {
        render_tile(y, 80); render_region(x, 32); render_tank(&scenes[i], tile, TANK_W);
        for (int row = 0; row < 80; row++) for (int column = x; column < x + 32; column++) {
            if (tile[row * TANK_W + column] != reference[i * pixels + (y + row) * TANK_W + column]) {
                fprintf(stderr, "Static backdrop mismatch scene %d at %d,%d: %04x != %04x\n",
                    i, column, y + row, tile[row * TANK_W + column], reference[i * pixels + (y + row) * TANK_W + column]);
                assert(false);
            }
        }
    }
    assert(cache[0] == 0x5a5a && cache[words + 1] == 0x5a5a);
    render_set_backdrop_cache(NULL, 0); render_tile(0, 0); render_region(0, 0);
    free(reference); free(tile); free(cache); tank_resolution(false);
    puts("PASS: warm static backdrop matches live output through rectangular replay, growth/color/position/ownership changes, animated pieces and lantern day/night");
}
static void test_solid_clock(void) {
    tank_resolution(true);
    tank_t t; tank_init(&t, 43); tank_new_population(&t); setup_cancel(&t);
    t.companion.display_compact = 1; t.companion.lively = 0; t.companion.captions = 1;
    t.companion.version = COMPANION_VERSION;
    t.companion.collections = 7; t.companion.game_challenges = 63; t.companion.decor = 1;
    t.aquariums.clock_theme = CLOCK_FLIP; t.aquariums.clock_delay = CLOCK_DELAY_MANUAL;
    t.aquariums.clock_flags = CLOCK_HIDE_SECONDS; t.clock_requested = true;
    size_t pixels = (size_t)TANK_W * TANK_H;
    uint16_t *reference = malloc(pixels * sizeof *reference), *actual = malloc(pixels * sizeof *actual);
    uint16_t *retained = calloc(pixels, sizeof *retained), *tile = malloc(((size_t)TANK_W * 80 + 2) * sizeof *tile);
    assert(reference && actual && retained && tile);
    render_clock_synchronized(false);
    for (int cache = 0; cache < 2; cache++) {
        render_work_caches(cache != 0);
        for (int theme = CLOCK_COMPACT; theme < CLOCK_THEME_COUNT; theme++)
            for (int size = 0; size < CLOCK_SIZE_COUNT; size++) for (int position = 0; position < CLOCK_POSITION_COUNT; position++) {
                t.aquariums.clock_theme = (uint8_t)theme; t.aquariums.clock_layout = clock_layout_set_size((uint8_t)(position << 2), size);
                t.clock_requested = false;
                render_tank(&t, reference, TANK_W);
                t.clock_requested = true; render_clock_overlay(&t, reference, TANK_W, 0);
                memset(actual, 0x85, pixels * sizeof *actual);
                render_tank(&t, actual, TANK_W);
                if (memcmp(reference, actual, pixels * sizeof *actual)) {
                    fprintf(stderr, "Solid occlusion differs cache %d theme %d size %d position %d\n", cache, theme, size, position);
                    assert(false);
                }
                for (int y = 0; y < TANK_H; y += 80) {
                    render_tile(y, 80); render_tank(&t, tile + 1, TANK_W);
                    assert(!memcmp(actual + y * TANK_W, tile + 1, (size_t)TANK_W * 80 * sizeof *tile));
                }
                render_tile(0, 0);
            }
    }
    int skipped = 0;
    for (int b = 0; b < VEG_BEDS_MAX; b++) for (int f = 0; f < VEG_FRONDS_MAX; f++) t.veg_h[b][f] = 0;
    t.aquariums.clock_theme = CLOCK_FLIP; t.aquariums.clock_layout = CLOCK_CENTER << 2;
    for (int i = 0; i < 80; i++) {
        tank_tick(&t, .1f, keep_swim_goal);
        if (i == 10) t.aquariums.clock_flags |= CLOCK_HIDE_PANEL;
        if (i == 20) t.aquariums.clock_flags &= (uint8_t)~CLOCK_HIDE_PANEL;
        if (i == 30) t.aquariums.clock_layout = (CLOCK_BOTTOM_RIGHT << 2) | CLOCK_MEDIUM;
        if (i == 40) t.aquariums.clock_flags |= CLOCK_HIDE_PANEL;
        if (i == 50) t.aquariums.clock_flags &= (uint8_t)~CLOCK_HIDE_PANEL;
        if (i == 60) t.clock_requested = false;
        if (i == 70) t.clock_requested = true;
        render_tank(&t, reference, TANK_W);
        render_damage_begin(&t, i == 0);
        int cursor = 0, x, y, w, h, area = 0;
        while (render_damage_next(&cursor, &x, &y, &w, &h)) {
            tile[0] = tile[(size_t)TANK_W * h + 1] = 0x5a5a;
            render_tile(y, h); render_region(x, w); render_tank(&t, tile + 1, TANK_W);
            assert(tile[0] == 0x5a5a && tile[(size_t)TANK_W * h + 1] == 0x5a5a);
            for (int row = 0; row < h; row++)
                memcpy(retained + (y + row) * TANK_W + x, tile + 1 + row * TANK_W + x, (size_t)w * sizeof *tile);
            area += w * h;
        }
        render_tile(0, 0); render_region(0, 0);
        if (memcmp(reference, retained, pixels * sizeof *reference)) {
            fprintf(stderr, "Solid retained display differs frame %d\n", i); assert(false);
        }
        if (i > 2 && i < 10 && area < (int)pixels) skipped++;
    }
    assert(skipped >= 5);
    t.companion.collections = 0; t.companion.game_challenges = 0;
    t.aquariums.clock_flags &= (uint8_t)~CLOCK_HIDE_PANEL;
    t.aquariums.clock_delay = CLOCK_DELAY_30_SEC | (4 << CLOCK_SECONDARY_SHIFT);
    assert(progression_save(&t));
    tank_t loaded; tank_init(&loaded, 1); boot_enlarged(&loaded);
    assert(loaded.aquariums.clock_delay == t.aquariums.clock_delay);
    char *json = progression_backup_json(&t); assert(json);
    assert(progression_restore_json(json, strlen(json), true, error, sizeof error));
    tank_init(&loaded, 1); boot_enlarged(&loaded);
    assert(loaded.aquariums.clock_delay == t.aquariums.clock_delay);
    free(json);
    render_settings_open(); clock_tap(&t, 330, 100); clock_tap(&t, 330, 100);
    reject_write = true; clock_tap(&t, 220, 180); reject_write = false;
    assert(!(t.aquariums.clock_flags & CLOCK_HIDE_PANEL));
    clock_tap(&t, 220, 180); assert(t.aquariums.clock_flags & CLOCK_HIDE_PANEL);
    clock_tap(&t, 220, 180); assert(!(t.aquariums.clock_flags & CLOCK_HIDE_PANEL));
    clock_tap(&t, 220, 132);
    assert(t.aquariums.clock_delay >> CLOCK_SECONDARY_SHIFT == 5);
    clock_tap(&t, 380, 20); clock_tap(&t, 330, 180); clock_tap(&t, 220, 180);
    assert((t.aquariums.clock_delay & CLOCK_DELAY_MASK) == CLOCK_DELAY_2_MIN);
    render_settings_open(); render_work_caches(false);
    free(reference); free(actual); free(retained); free(tile); tank_resolution(false);
    puts("PASS: opaque clock occlusion is pixel-exact across themes/layouts, retained transitions skip hidden blocks, and panel visibility persists with rollback");
}
static void test_social_and_preview(void) {
    for (int compact = 0; compact < 2; compact++) {
        tank_resolution(compact != 0);
        tank_t t; tank_init(&t, 54); tank_new_population(&t);
        t.companion.display_compact = (uint8_t)compact;
        t.fish[0].stress = t.fish[1].stress = 0;
        t.fish[0].x = t.fish[1].x = TANK_W / 2;
        t.fish[0].y = t.fish[1].y = TANK_H / 2;
        for (int second = 0; second < 32; second++) {
            t.fish[0].x = t.fish[1].x = TANK_W / 2;
            t.fish[0].y = t.fish[1].y = TANK_H / 2;
            tank_tick(&t, 1, NULL);
        }
        assert(tank_familiar_friend(&t, 0) == 1 && tank_familiar_friend(&t, 1) == 0);
        t.fish[0].goal.id = GOAL_FOLLOW_FRIEND; t.fish[0].goal_age = 15;
        t.fish[0].hunger = 2; t.fish[0].energy = 8; t.fish[0].lazy = 0;
        assert(advisor_rules(&t, 0, true).id == GOAL_EXPLORE);
        t.fish[0].energy = 1;
        assert(advisor_rules(&t, 0, true).id == GOAL_REST);
        t.sd_unlocks = UINT32_MAX;
        int favorite = tank_favorite_decor(&t, 0);
        assert(favorite >= 0 && tank_decor_owned(&t, favorite));
        tank_set_name(&t, 0, "renamed"); t.fish[0].color ^= 0xffffff;
        assert(tank_favorite_decor(&t, 0) == favorite);
        for (int item = 0; item < SD_ITEM_COUNT; item++) {
            if (!tank_decor_placeable(item)) continue;
            char *original = progression_backup_json(&t); assert(original);
            setup_begin_place(&t, item); assert(setup_is_place());
            tank_decor_set(&t, item, TANK_W * .7f, DECOR_Z_BACK);
            tank_decor_shift(&t, item, 1);
            if (item == 3) tank_coral_set_rgb(&t, 0xff0000);
            if (item == 4) tank_cluster_set_scheme(&t, 2);
            char *preview = progression_backup_json(&t); assert(preview && !strcmp(preview, original)); free(preview);
            assert(progression_save(&t));
            setup_cancel(&t);
            char *cancelled = progression_backup_json(&t); assert(cancelled && !strcmp(cancelled, original)); free(cancelled);
            setup_begin_place(&t, item);
            tank_decor_set(&t, item, TANK_W * .65f, DECOR_Z_BACK);
            reject_write = true; setup_activate(&t, SETUP_HIT_NEXT);
            assert(setup_is_place());
            preview = progression_backup_json(&t); assert(preview && !strcmp(preview, original)); free(preview);
            reject_write = false; setup_activate(&t, SETUP_HIT_NEXT); assert(!setup_active());
            preview = progression_backup_json(&t); assert(preview && strcmp(preview, original)); free(preview);
            free(original);
        }
        int item = 9; t.sd_unlocks |= SD_ITEM_CHEST; t.sd_balance = 100;
        tank_t before = t;
        reject_write = true; assert(!progression_sell(&t, item)); reject_write = false;
        assert(!memcmp(&before, &t, sizeof t));
        aquarium_summary_t s;
        t.aquariums.count = 1; t.aquariums.active = 0;
        t.aquariums.stat_upgrades = STAT_CURRENT | STAT_TRAINING;
        t.journal.theme = 3;
        assert(progression_aquarium_summary(&t, 0, &s));
        assert(s.fish == t.n_fish && s.upgrades == 2 && s.theme == 3 && s.helpers == 3 && s.last_visit == test_unix);
        assert(!progression_aquarium_summary(&t, 1, &s));
        const int cards[] = {RENDER_CARD_SHRIMP, RENDER_CARD_SNAIL, RENDER_CARD_URCHIN, RENDER_CARD_SEAHORSE};
        for (int kind = 0; kind < 4; kind++) {
            float x = 224.0f * TANK_W / PAGE_W, y = 300.0f * TANK_H / PAGE_H;
            assert(render_creature_colors_hit(&t, cards[kind], x, y) == kind);
            assert(render_creature_colors_hit(&t, cards[kind], 0, 0) == -1);
        }
        assert(render_creature_colors_hit(&t, 0, 224, 300) == -1);
    }
    tank_resolution(false);
    puts("PASS: calm friendships, bounded pair leisure, stable favorite decor, transactional placement/autosaves/cancel/retry/sale and creature shortcuts at both scales");
}

static void test_theme_detail_damage(void) {
    for (int compact = 0; compact < 2; compact++) {
        tank_resolution(compact != 0);
        tank_t t; tank_init(&t, 54); t.n_fish = 0; t.companion.lively = 0;
        t.companion.display_compact = (uint8_t)compact; t.journal.xp = journal_level_xp(15);
        t.sd_unlocks = SD_ITEM_CHEST | SD_ITEM_CRYSTAL;
        size_t pixels = (size_t)TANK_W * TANK_H;
        uint16_t *full = malloc(pixels * 2), *retained = calloc(pixels, 2), *tile = malloc((size_t)TANK_W * 80 * 2);
        assert(full && retained && tile);
        render_work_caches(true);
        const float times[] = {0, .1f, .25f, .75f, 1, 5.75f, 6, 19.75f, 20, 20.25f, 21.75f, 22, 47.75f, 48, 54, 80, 82};
        for (int clock = 0; clock < 2; clock++) for (int theme = 3; theme <= 7; theme += 4)
            for (int i = 0; i < (int)(sizeof times / sizeof times[0]); i++) {
                t.journal.theme = (uint8_t)theme; t.clock = times[i];
                t.visitor_left = i > 1 && i < 12 ? 24 - i : 0;
                t.clock_requested = clock != 0; t.aquariums.clock_theme = clock ? CLOCK_ANALOG : CLOCK_OFF;
                render_tile(0, 0); render_region(0, 0); render_tank(&t, full, TANK_W);
                render_damage_begin(&t, i == 0);
                int cursor = 0, x, y, w, h;
                while (render_damage_next(&cursor, &x, &y, &w, &h)) {
                    render_tile(y, h); render_region(x, w); render_tank(&t, tile, TANK_W);
                    for (int row = 0; row < h; row++)
                        memcpy(retained + (y + row) * TANK_W + x, tile + row * TANK_W + x, (size_t)w * 2);
                }
                render_tile(0, 0); render_region(0, 0);
                assert(!memcmp(full, retained, pixels * 2));
            }
        render_work_caches(false); free(full); free(retained); free(tile);
    }
    tank_resolution(false);
    puts("PASS: sparse silhouette/star/glint appearance, movement and removal replay pixel-exactly at both scales with and without opaque clock");
}

static void test_tank_polish(void) {
    tank_t t, loaded;
    saved_size = 0; tank_resolution(true); tank_init(&t, 123); progression_boot(&t);
    assert(!progression_mode_select(&t, GAME_MODE_NORMAL));
    progression_setup_done(&t);
    assert(t.polish.version == 1 && t.polish.founded_unix == test_unix);
    assert(t.polish.next_visit_s >= 21600 && t.polish.next_visit_s <= 43200);
    uint32_t a = t.polish.active[0].id, b = t.polish.active[1].id;
    assert(a && b && a != b);
    t.fish[0].familiar_id[1] = t.fish[1].social_id; t.fish[0].familiar[1] = 700;
    companion_polish_observe(&t);
    assert(t.polish.active[0].buddy == b && t.polish.active[0].friend_s == 700);
    assert(progression_save(&t));
    tank_init(&loaded, 3); progression_boot(&loaded);
    assert(loaded.polish.active[0].id == a && loaded.polish.active[1].id == b);
    assert(tank_familiar_friend(&loaded, 0) == 1 && loaded.fish[0].familiar[1] == 700);
    int slot = add_bank_candidate(&t, 3);
    companion_polish_observe(&t); uint32_t bank_id = t.polish.active[slot].id;
    assert(!progression_bank_move(&t, -1, slot) && t.polish.bank[0].id == bank_id);
    assert(!progression_bank_move(&t, 0, 1) && t.polish.active[1].id == bank_id && t.polish.bank[0].id == b);
    assert(!progression_bank_move(&t, 0, 1) && t.polish.active[1].id == b);
    assert(progression_adventure_depart(&t, 0, 0) && t.polish.traveler.id == a);
    t.companion.adventure_s = companion_adventure_total(&t.companion, 0);
    assert(progression_adventure_return(&t));
    assert(t.polish.active[1].id == a && (t.polish.active[1].adventures & 1));
    assert(!t.polish.traveler.id && companion_polish_valid(&t.polish, t.n_fish, t.bank_count, false));
    t.polish.next_visit_s = t.polish.viewed_s + 2;
    t.light_on = true; t.ui_cover = true;
    companion_tick(&t, 10); assert(!t.polish.visits);
    t.ui_cover = false; t.light_on = false;
    companion_tick(&t, 10); assert(!t.polish.visits);
    t.light_on = true;
    companion_tick(&t, 1); assert(!t.polish.visits);
    companion_tick(&t, 1); assert(t.polish.visits == 1 && t.visitor_left == 24);
    assert(t.polish.next_visit_s >= t.polish.viewed_s + 28800);
    companion_tick(&t, 24); assert(!t.visitor_left && t.polish.visits == 1);
    assert(t.polish.recap[0].kind == MEMORY_VISITOR);
    for (int i = 0; i < 20; i++) companion_memory(&t, MEMORY_HELPERS, -1, 0);
    assert(t.polish.recap_count == TANK_RECAP_CAP);
    char text[96];
    t.fish[0].hunger = 8; companion_care_text(&t, 0, text, sizeof text);
    assert(strstr(text, "Hungry"));
    uint8_t subtitle = t.polish.subtitle, emblem = t.polish.emblem;
    reject_write = true;
    assert(!companion_identity(&t, false) && t.polish.subtitle == subtitle);
    assert(!companion_identity(&t, true) && t.polish.emblem == emblem);
    assert(!companion_backup_record(&t) && !t.polish.backups);
    reject_write = false;
    assert(companion_identity(&t, false) && companion_identity(&t, true));
    assert(companion_backup_record(&t) && t.polish.backup_unix == test_unix && t.polish.backups == 1);
    char *json = progression_backup_json(&t); assert(json);
    char name[16]; uint32_t release;
    assert(progression_backup_preview(json, strlen(json), name, &release));
    assert(!strcmp(name, t.aquariums.name) && release == PT_RELEASE_NUM);
    assert(progression_restore_json(json, strlen(json), true, error, sizeof error));
    tank_init(&loaded, 9); progression_boot(&loaded);
    assert(loaded.polish.active[1].id == a && loaded.polish.active[0].id == b);
    assert(loaded.polish.visits == 1 && !loaded.visitor_left && loaded.polish.backups == 1);
    cJSON *root = cJSON_Parse(json);
    replace_number(cJSON_GetObjectItemCaseSensitive(root, "polish"), "version", 2);
    invalid_object(root);
    root = cJSON_Parse(json); replace_number(root, "schema", 10);
    char *legacy = cJSON_PrintUnformatted(root); cJSON_Delete(root); assert(legacy);
    assert(progression_restore_json(legacy, strlen(legacy), true, error, sizeof error));
    free(legacy); free(json);
    tank_init(&loaded, 9); progression_boot(&loaded);
    assert(loaded.polish.version == 1 && !loaded.polish.founded_unix && !loaded.polish.visits);
    assert(loaded.polish.active[0].arrived_s == UINT32_MAX);
    assert(progression_save(&loaded)); saved_size = 4520;
    tank_init(&loaded, 9); progression_boot(&loaded);
    assert(loaded.bank_count == t.bank_count && loaded.polish.active[0].id != loaded.polish.active[1].id);
    snprintf(loaded.aquariums.name, sizeof loaded.aquariums.name, "LONGEST TANK 12");
    loaded.polish.subtitle = 4; loaded.polish.emblem = 3;
    loaded.polish.founded_unix = loaded.polish.backup_unix = test_unix;
    loaded.polish.backups = loaded.polish.visits = UINT32_MAX;
    for (int k = 0; k < TANK_RECAP_CAP; k++)
        companion_memory(&loaded, k % MEMORY_VISITOR + 1, 0, COMPANION_DISCOVERY_COUNT - 1);
    loaded.snail_grazed = loaded.shrimp_eaten = INT32_MAX;
    loaded.urchin_grazed_px = loaded.sh_trust_passed = 1000000000.0f;
    for (int compact = 0; compact < 2; compact++) {
        tank_reframe(&loaded, compact != 0);
        size_t pixels = (size_t)TANK_W * TANK_H;
        uint16_t *fb = calloc(pixels + 2, sizeof *fb); assert(fb);
        uint16_t *edge = calloc(pixels, sizeof *edge); assert(edge);
        loaded.companion.cosmetic = 0;
        render_rect(edge, TANK_W, 0, 0, PAGE_W, PAGE_H, 0x102b36);
        render_rect_edge(edge, TANK_W, 3, 3, PAGE_W - 6, PAGE_H - 6, 0x83b8ac);
        fb[0] = fb[pixels + 1] = 0xa55a;
        for (int page = 8; page <= 23; page++) {
            if (page == 17) continue;
            companion_open_section(page);
            int cases = page == 11 ? SD_ITEM_COUNT : page == 13 ? COMPANION_DISCOVERY_COUNT : page == 9 ? 4 : page == 14 ? 2 : page == 20 ? 19 : 1;
            for (int k = 0; k < cases; k++) {
                if (page == 20) {
                    loaded.rpg.chapter = (uint8_t)(k / 3);
                    loaded.rpg.step = (uint8_t)(k % 3);
                    const char *prompt = companion_rpg_prompt(&loaded);
                    size_t capacity = (420 - render_text_w("W", 1)) /
                        (render_text_w("WW", 1) - render_text_w("W", 1)) + 1;
                    int lines = 0;
                    while (*prompt) {
                        size_t length = strlen(prompt), count = length < capacity ? length : capacity;
                        if (length > count) {
                            while (count && prompt[count] != ' ') count--;
                            if (!count) count = capacity;
                        }
                        char row[128]; assert(count < sizeof row);
                        memcpy(row, prompt, count); row[count] = 0;
                        assert(render_text_w(row, 1) <= 420);
                        prompt += count; while (*prompt == ' ') prompt++;
                        lines++;
                    }
                    int glyph_height = (7 * PAGE_H + TANK_H - 1) / TANK_H;
                    assert(lines <= 3 && 184 + (lines - 1) * 16 + glyph_height < 232);
                    for (int choice = 0; choice < 3; choice++) {
                        char label[128]; companion_rpg_choice_label(&loaded, choice, label, sizeof label);
                        assert(render_text_w(label, 1) <= 382);
                    }
                }
                if (page == 14) loaded.polish.active[0].milestones = k ? (1u << MS_FISH_COUNT) - 1u : 0;
                companion_render(&loaded, fb + 1, TANK_W);
                assert(fb[0] == 0xa55a && fb[pixels + 1] == 0xa55a);
                for (int y = 0; y < TANK_H; y++) for (int x = (int)ceilf(440.0f * TANK_W / PAGE_W); x < TANK_W; x++)
                    if (fb[1 + y * TANK_W + x] != edge[y * TANK_W + x]) {
                        fprintf(stderr, "Polish text crosses right margin: page %d case %d at %d,%d\n", page, k, x, y);
                        assert(false);
                    }
                if (cases > 1 && page != 20) companion_tap(&loaded, render_frame_x(300), render_frame_y(page == 13 ? 255 : 280));
            }
        }
        companion_open_section(0);
        companion_tap(&loaded, render_frame_x(350), render_frame_y(230));
        companion_tap(&loaded, render_frame_x(170), render_frame_y(320));
        companion_render(&loaded, fb + 1, TANK_W);
        assert(fb[0] == 0xa55a && fb[pixels + 1] == 0xa55a);
        free(edge); free(fb);
    }
    tank_resolution(false);
    puts("PASS: persistent unique fish stories/buddies through Bank/adventures/JSON, legacy migrations, bounded recap, view-only rare visitors, identity/backup rollback and polish menus at both scales");
}
static void test_story(void) {
    tank_t t, loaded; char error[96];
    saved_size = 0; tank_init(&t, 77); boot_enlarged(&t); progression_setup_done(&t);
    assert(t.story.version == 1 && !t.story.births);
    companion_polish_observe(&t);
    uint32_t a = t.polish.active[0].id, b = t.polish.active[1].id;
    char name_a[FISH_NAME_MAX + 1]; snprintf(name_a, sizeof name_a, "%s", t.fish[0].name);
    int kid = tank_add_fish(&t, 0, 1); assert(kid == 2);
    companion_polish_observe(&t);
    uint32_t c = t.polish.active[kid].id; assert(c && c != a && c != b);
    const uint16_t *pp = t.story.active[kid].parent;
    assert((pp[0] == a && pp[1] == b) || (pp[0] == b && pp[1] == a));
    assert(t.story.active[kid].generation == 1 && t.story.births == 1);
    int grand = tank_add_fish(&t, kid, 0); assert(grand == 3);
    companion_polish_observe(&t);
    pp = t.story.active[grand].parent;
    assert(t.story.active[grand].generation == 2 && (pp[0] == c || pp[1] == c) && (pp[0] == a || pp[1] == a));
    /* a parent leaving keeps its name in the book; the lines shift with the slots */
    assert(tank_remove_fish(&t, 0));
    assert(t.story.active[1].generation == 1 && t.story.active[2].generation == 2);
    assert(t.story.names[STORY_NAME_BOOK - 1].id == a && !strcmp(companion_story_name(&t, (uint16_t)a), name_a));
    assert(!strcmp(companion_story_name(&t, (uint16_t)c), t.fish[1].name));
    assert(companion_story_line(&t, (uint16_t)c) == &t.story.active[1]);
    assert(companion_story_valid(&t.story, t.n_fish, t.bank_count, false, t.polish.next_id));
    /* banked and travelling lines move with their fish; no name is booked while they live */
    fish_lineage_t line = t.story.active[2];
    assert(!progression_bank_move(&t, -1, 2) && !memcmp(&t.story.bank[0], &line, sizeof line));
    assert(!memcmp(&t.story.active[2], &(fish_lineage_t){0}, sizeof line));
    for (int i = 0; i < STORY_NAME_BOOK; i++) assert(t.story.names[i].id != t.polish.bank[0].id);
    assert(!progression_bank_move(&t, 0, -1) && !memcmp(&t.story.active[2], &line, sizeof line) && !t.bank_count);
    fish_lineage_t kid_line = t.story.active[1];
    assert(progression_adventure_depart(&t, 0, 1) && !memcmp(&t.story.traveler, &kid_line, sizeof kid_line));
    assert(companion_story_valid(&t.story, t.n_fish, t.bank_count, true, t.polish.next_id));
    t.companion.adventure_s = companion_adventure_total(&t.companion, 0);
    assert(progression_adventure_return(&t));
    int back = t.n_fish - 1;
    assert(!memcmp(&t.story.active[back], &kid_line, sizeof kid_line) && !t.story.traveler.generation);
    /* visitors: the first is the drifter, then every species turns up and is counted */
    t.polish.visits = 1; assert(companion_visitor_pick(&t) == VISITOR_DRIFTER);
    unsigned total = 0;
    for (int i = 0; i < 4000; i++) { t.polish.visits = 2 + i; t.rng = 0x1234567u * (i + 1); companion_visitor_pick(&t); }
    for (int i = 0; i < VISITOR_SPECIES; i++) { assert(t.story.seen[i] && t.story.first_seen_s[i]); total += t.story.seen[i]; }
    assert(total == 4001 && t.story.seen[VISITOR_DRIFTER] > t.story.seen[VISITOR_SQUID]);
    tank_memory_t m = {0}; char text[96]; m.kind = MEMORY_VISITOR; m.detail = VISITOR_MANTA;
    companion_memory_text(&m, text, sizeof text); assert(strstr(text, "manta"));
    /* seasons: north by month, SOUTH swapped, holiday lights Dec 15 - Jan 1, invalid clock none */
    assert(companion_season_for(1782907200LL, 0) == SEASON_SUMMER);
    assert(companion_season_for(1782907200LL, STORY_SEASONS_SOUTH) == SEASON_WINTER);
    assert(companion_season_for(1775779200LL, 0) == SEASON_SPRING && companion_season_for(1791590400LL, 0) == SEASON_AUTUMN);
    assert(companion_season_for(1797768000LL, 0) == (SEASON_WINTER | SEASON_LIGHTS));
    assert(companion_season_for(1797768000LL, STORY_SEASONS_SOUTH) == (SEASON_SUMMER | SEASON_LIGHTS));
    assert(companion_season_for(1798804800LL, 0) == (SEASON_WINTER | SEASON_LIGHTS) && companion_season_for(1798891200LL, 0) == SEASON_WINTER);
    assert(companion_season_for(1797768000LL, STORY_SEASONS_OFF) == SEASON_NONE && companion_season_for(0, 0) == SEASON_NONE);
    assert(companion_set_seasons(&t, STORY_SEASONS_SOUTH) && t.story.flags == STORY_SEASONS_SOUTH);
    assert(!companion_set_seasons(&t, 4) && t.story.flags == STORY_SEASONS_SOUTH);
    reject_write = true; assert(!companion_set_seasons(&t, 0) && t.story.flags == STORY_SEASONS_SOUTH); reject_write = false;
    /* save, JSON and the rejects */
    assert(progression_save(&t));
    tank_init(&loaded, 5); progression_boot(&loaded);
    assert(!memcmp(&loaded.story, &t.story, sizeof t.story));
    char *json = progression_backup_json(&t);     assert(json && strstr(json, "\"schema\":17") && strstr(json, "\"story\":{") && strstr(json, "\"games\":{") && strstr(json, "\"rpg\":{") && strstr(json, "\"creative\":{") && strstr(json, "\"profile\":{") && strstr(json, "\"games_extra\":{") && strstr(json, "\"care\":{"));
    assert(progression_restore_json(json, strlen(json), true, error, sizeof error));
    tank_init(&loaded, 6); progression_boot(&loaded);
    assert(!memcmp(&loaded.story, &t.story, sizeof t.story));
    cJSON *root = cJSON_Parse(json); replace_number(cJSON_GetObjectItemCaseSensitive(root, "story"), "flags", 4); invalid_object(root);
    root = cJSON_Parse(json); replace_number(cJSON_GetObjectItemCaseSensitive(root, "story"), "version", 2); invalid_object(root);
    root = cJSON_Parse(json); cJSON_DeleteItemFromObjectCaseSensitive(root, "story"); invalid_object(root);
    tank_story_t bad = t.story; bad.active[0].parent[0] = (uint16_t)t.polish.next_id;
    assert(!companion_story_valid(&bad, t.n_fish, t.bank_count, false, t.polish.next_id));
    bad = t.story; bad.bank[0].generation = 1;
    assert(!companion_story_valid(&bad, t.n_fish, 0, false, t.polish.next_id));
    bad = t.story; memset(bad.names[0].name, 'x', sizeof bad.names[0].name); bad.names[0].id = 1;
    assert(!companion_story_valid(&bad, t.n_fish, t.bank_count, false, t.polish.next_id));
    free(json);
    /* a 1.2 save (no story tail) migrates: the drifter count comes from the old visits */
    t.polish.visits = 9; assert(progression_save(&t)); saved_size = 5416;
    tank_init(&loaded, 8); progression_boot(&loaded);
    assert(loaded.story.version == 1 && loaded.story.seen[VISITOR_DRIFTER] == 9 && !loaded.story.births);
    assert(loaded.n_fish == t.n_fish && companion_story_valid(&loaded.story, loaded.n_fish, loaded.bank_count, false, loaded.polish.next_id));
    puts("PASS: 1.3 story - family lines through birth/sale/Bank/adventure, departed-name book, visitor bestiary, seasons, save/JSON/legacy");
}
static void test_genetics(void) {
    static tank_t t, u, loaded;
    saved_size = 0; tank_init(&t, 91); boot_enlarged(&t); progression_setup_done(&t);
    while (t.n_fish > 2) assert(tank_remove_fish(&t, t.n_fish - 1));
    /* founders carry seeded, valid genes */
    for (int i = 0; i < 64; i++) {
        fish_lineage_t l = {0}; tank_seed_line(&l, (uint32_t)i * 2654435761u);
        assert(l.pattern2 <= FISH_PATTERN_COUNT && l.color2 <= FISH_COLOR_N && !(l.traits & 15) && !l.mutation);
    }
    t.fish[0].pattern = t.fish[1].pattern = PATTERN_PLAIN;
    memset(&t.story.active[0].pattern2, 0, 4); memset(&t.story.active[1].pattern2, 0, 4);
    t.story.active[0].pattern2 = PATTERN_SPOTS + 1;           /* a hidden SPOTS allele */
    t.story.active[0].traits = t.story.active[1].traits = 16; /* both carry SHIMMER */
    tank_sync_traits(&t); assert(!t.fish[0].traits && !t.fish[1].traits);
    int spots = 0, shimmer = 0, carriers = 0, mutants = 0, plain_shimmer = 0;
    for (int i = 0; i < 600; i++) {
        u = t; u.rng = 0x9e3779b9u * (uint32_t)(i + 1); u.story.births = (uint16_t)i;
        int kid = tank_add_fish(&u, 0, 1); assert(kid == 2);
        const fish_lineage_t *l = &u.story.active[kid];
        assert(u.fish[kid].pattern < FISH_PATTERN_COUNT && l->pattern2 <= FISH_PATTERN_COUNT && l->color2 <= FISH_COLOR_N);
        assert(!(l->mutation & ~3u) && u.fish[kid].traits == (l->traits & 15));
        assert(companion_story_valid(&u.story, u.n_fish, u.bank_count, false, u.polish.next_id));
        spots += u.fish[kid].pattern == PATTERN_SPOTS; mutants += l->mutation != 0;
        if (l->mutation & GENE_MUTANT_PATTERN) assert(u.fish[kid].pattern >= PATTERN_SADDLE);
        if (!(l->mutation & GENE_MUTANT_TRAIT)) {
            shimmer += (l->traits & TRAIT_SHIMMER) != 0; carriers += (l->traits & (16 * TRAIT_SHIMMER)) != 0;
        }
        /* parents with no shimmer allele never have a shimmering fry without a mutation */
        u = t; u.rng = 0x85ebca6bu * (uint32_t)(i + 1); u.story.births = (uint16_t)i;
        u.story.active[0].traits = u.story.active[1].traits = 0;
        kid = tank_add_fish(&u, 0, 1);
        if (!(u.story.active[kid].mutation & GENE_MUTANT_TRAIT)) plain_shimmer += u.fish[kid].traits != 0;
    }
    assert(spots > 60 && spots < 480);                        /* the hidden marking resurfaces */
    assert(shimmer > 60 && shimmer < 300 && carriers > 150);  /* ~25% show, ~50% carry */
    assert(mutants > 10 && mutants < 200 && !plain_shimmer);
    /* save and reload keep the genes and the shown traits */
    int kid = tank_add_fish(&t, 0, 1); assert(kid == 2);
    t.story.active[kid].traits = TRAIT_GOLD_EYE | 16 * TRAIT_PALE_BELLY; tank_sync_traits(&t);
    companion_polish_observe(&t);
    assert(companion_story_valid(&t.story, t.n_fish, t.bank_count, false, t.polish.next_id));
    assert(progression_save(&t));
    tank_init(&loaded, 4); progression_boot(&loaded);
    assert(!memcmp(&loaded.story, &t.story, sizeof t.story) && loaded.fish[kid].traits == TRAIT_GOLD_EYE);
    tank_story_t bad = t.story; bad.active[0].pattern2 = FISH_PATTERN_COUNT + 1;
    assert(!companion_story_valid(&bad, t.n_fish, t.bank_count, false, t.polish.next_id));
    bad = t.story; bad.active[0].color2 = FISH_COLOR_N + 1;
    assert(!companion_story_valid(&bad, t.n_fish, t.bank_count, false, t.polish.next_id));
    bad = t.story; bad.active[0].mutation = 4;
    assert(!companion_story_valid(&bad, t.n_fish, t.bank_count, false, t.polish.next_id));
    /* every trait and every marking changes the portrait */
    size_t px = (size_t)TANK_W * TANK_H * sizeof(uint16_t);
    uint16_t *a = malloc(px), *c = malloc(px); assert(a && c);
    fish_t portrait = t.fish[0]; portrait.stage = STAGE_ADULT; portrait.variety = 0; portrait.pattern = PATTERN_PLAIN;
    portrait.traits = 0; portrait.accent = 0x203040; portrait.color = 0xe08040; portrait.fin = 0x80c0e0;
    memset(a, 0, px); render_fish_portrait(a, TANK_W, 224, 180, 2, &portrait, 0);
    for (int k = 0; k < TRAIT_COUNT; k++) {
        portrait.traits = (uint8_t)(1u << k); memset(c, 0, px);
        render_fish_portrait(c, TANK_W, 224, 180, 2, &portrait, 0); assert(memcmp(a, c, px));
    }
    portrait.variety = FISH_JELLYFISH; portrait.traits = 0;
    memset(a, 0, px); render_fish_portrait(a, TANK_W, 224, 180, 2, &portrait, 0);
    for (int k = 0; k < TRAIT_COUNT; k++) {
        portrait.traits = (uint8_t)(1u << k); memset(c, 0, px);
        render_fish_portrait(c, TANK_W, 224, 180, 2, &portrait, 0); assert(memcmp(a, c, px));
    }
    portrait.variety = 0; portrait.traits = 0;
    memset(a, 0, px); render_fish_portrait(a, TANK_W, 224, 180, 2, &portrait, 0);
    for (int p = PATTERN_SADDLE; p < FISH_PATTERN_COUNT; p++) {
        portrait.pattern = (uint8_t)p; memset(c, 0, px);
        render_fish_portrait(c, TANK_W, 224, 180, 2, &portrait, 0); assert(memcmp(a, c, px));
    }
    free(a); free(c);
    puts("PASS: 1.3 genetics - hidden markings resurface, recessive traits show ~1 in 4, bounded mutations, saved genes and rendered traits/markings");
}
static void test_home_navigation(void) {
    const struct { int action; home_destination_t destination; } routes[] = {
        {HOME_BANK, HOME_DEST_BANK}, {HOME_JOURNAL, HOME_DEST_JOURNAL},
        {HOME_ALBUM, HOME_DEST_JOURNAL}, {HOME_THEMES, HOME_DEST_JOURNAL},
        {HOME_GAMES, HOME_DEST_GAMES}, {HOME_SETTINGS, HOME_DEST_SETTINGS},
        {HOME_RECOVERY, HOME_DEST_SETTINGS}, {HOME_KEEPER, HOME_DEST_KEEPER},
        {HOME_SHOP, HOME_DEST_SHOP}, {HOME_TANKS, HOME_DEST_TANKS},
        {HOME_NONE, HOME_DEST_NONE}, {HOME_CLOSE, HOME_DEST_NONE}
    };
    for (int compact = 0; compact < 2; compact++) {
        tank_t t;
        saved_size = 0;
        tank_resolution(compact != 0);
        tank_init(&t, 19);
        boot_enlarged(&t);
        tank_resolution(compact != 0);
        progression_setup_done(&t);
        setup_cancel(&t);
        tank_t before = t;
        int writes_before = writes;
        for (unsigned i = 0; i < sizeof routes / sizeof *routes; i++)
            assert(home_open(&t, routes[i].action) == routes[i].destination);
        assert(home_open(&t, -1) == HOME_DEST_NONE);
        assert(home_open(&t, 9999) == HOME_DEST_NONE);
        assert(!memcmp(&t, &before, sizeof t) && writes == writes_before);
        float x = render_frame_x(390), y = render_frame_y(24);
        assert(home_open(&t, HOME_GAMES) == HOME_DEST_GAMES);
        assert(render_games_tap(&t, x, y));
        assert(home_open(&t, HOME_BANK) == HOME_DEST_BANK);
        assert(render_bank_tap(&t, x, y) == 1);
        assert(home_open(&t, HOME_JOURNAL) == HOME_DEST_JOURNAL);
        assert(render_journal_tap(&t, x, y) == 1);
        assert(progression_save(&t) && progression_storage_ready());
        saved[0] ^= 1;
        int before_failed_load = writes;
        progression_boot(&t);
        assert(!progression_storage_ready() && !progression_save(&t));
        assert(writes == before_failed_load);
        saved[0] ^= 1;
        progression_boot(&t);
        assert(progression_storage_ready());
    }
    puts("PASS: shared Home routes, BACK, both scales, no profile mutation or writes");
}
static void test_name_wheel(void) {
    tank_t t, loaded;
    for (int compact = 0; compact < 2; compact++) {
        saved_size = 0; tank_resolution(compact != 0); tank_init(&t, 19); boot_enlarged(&t);
        tank_resolution(compact != 0);
        progression_setup_done(&t); setup_cancel(&t);
        setup_begin(&t);
        while (setup_page() != SETUP_PG_NAME_A) {
            assert(setup_page() < SETUP_PG_NAME_A);
            setup_activate(&t, SETUP_HIT_NEXT);
        }
        assert(setup_hit(render_frame_x(SETUP_TOP_NEXT_X + SETUP_TOP_BTN_W / 2),
                         render_frame_y(SETUP_TOP_BTN_Y + SETUP_BTN_H / 2)) == SETUP_HIT_NEXT);
        setup_activate(&t, SETUP_HIT_NEXT);
        assert(setup_page() == SETUP_PG_LOOK_A);
        setup_cancel(&t);
        setup_begin_birth(&t, 1); setup_activate(&t, SETUP_HIT_NEXT);
        assert(setup_page() == SETUP_PG_NAME_NEW);
        setup_activate(&t, SETUP_HIT_NEXT);
        assert(setup_page() == SETUP_PG_FAMILY);
        setup_cancel(&t);
        tank_set_name(&t, 0, "abc"); assert(progression_save(&t));
        setup_begin_rename(&t, 0);
        assert(setup_page() == SETUP_PG_RENAME && setup_slot() == 0);
        /* Retired prototype key IDs must never edit the wheel's name. */
        for (int id = 100; id < 128; id++) setup_activate(&t, id);
        assert(!strcmp(t.fish[0].name, "abc"));
        float x = render_frame_x(SETUP_SLOT_X + SETUP_SLOT_W / 2);
        float y = render_frame_y(SETUP_SLOT_Y + SETUP_SLOT_H / 2);
        assert(setup_hit(x, y) == SETUP_HIT_SLOT0);
        setup_touch(&t, x, y, true);
        setup_touch(&t, x, y - SETUP_SPIN_PX - 1, true);
        setup_touch(&t, x, y - SETUP_SPIN_PX - 1, false);
        assert(!strcmp(t.fish[0].name, "bbc"));
        setup_activate(&t, SETUP_HIT_BACK);
        assert(!setup_active() && !strcmp(t.fish[0].name, "abc"));
        setup_take_renamed();
        setup_begin_rename(&t, 0);
        setup_activate(&t, SETUP_HIT_SLOT0 + 1);
        setup_activate(&t, SETUP_HIT_UP);
        assert(!strcmp(t.fish[0].name, "acc"));
        reject_write = true; setup_activate(&t, SETUP_HIT_NEXT);
        assert(setup_active());
        reject_write = false; setup_activate(&t, SETUP_HIT_NEXT);
        assert(!setup_active() && setup_take_renamed() == 0);
        tank_init(&loaded, 20); boot_enlarged(&loaded);
        assert(!strcmp(loaded.fish[0].name, "acc"));
    }
    tank_resolution(true);
    puts("PASS: first-run/birth letter-wheel navigation, touch, retired key isolation, rename cancel/save/retry and reload at both scales");
}

static void test_simple_scene(void) {
    render_device_t device = {0, 1, 1, -1, true};
    for (int normal = 0; normal < 2; normal++) {
        tank_t t;
        tank_resolution(normal != 0);
        tank_init(&t, 42);
        t.companion.display_compact = normal != 0;
        t.n_fish = 0;
        t.sd_unlocks = SD_ITEM_SNAIL | SD_ITEM_SHRIMP | SD_ITEM_URCHIN | SD_ITEM_SEAHORSE;
        t.aquariums.clock_theme = CLOCK_OFF;
        setup_cancel(&t);
        tank_t before = t;
        size_t bytes = (size_t)TANK_W * TANK_H * sizeof(uint16_t);
        uint16_t *full = malloc(bytes), *other = malloc(bytes);
        uint16_t *tile = malloc(((size_t)TANK_W * 80 + 2) * sizeof(uint16_t));
        assert(full && other && tile);
        render_set_device(&device);
        render_tile(0, 0); render_region(0, 0);
        render_tank(&t, full, TANK_W);
        assert(!memcmp(&t, &before, sizeof t));
        assert(render_creature_card_hit(&t, t.snail_x, t.snail_y) == -1);
        for (int y = 0; y < TANK_H; y += 80) {
            int height = TANK_H - y < 80 ? TANK_H - y : 80;
            tile[0] = tile[1 + TANK_W * height] = 0xa55a;
            render_tile(y, height);
            render_tank(&t, tile + 1, TANK_W);
            assert(tile[0] == 0xa55a && tile[1 + TANK_W * height] == 0xa55a);
            assert(!memcmp(full + y * TANK_W, tile + 1, (size_t)TANK_W * height * 2));
        }
        render_tile(0, 0);
        t.clock += 10;
        t.snail_x += 30;
        t.bubble[0].x += 40;
        render_tank(&t, other, TANK_W);
        assert(!memcmp(full, other, bytes));
        t.food[0].alive = true; t.food[0].x = TANK_W / 2; t.food[0].y = 40;
        render_tank(&t, other, TANK_W);
        assert(memcmp(full, other, bytes));
        t.food[0].alive = false;
        t.sd_unlocks |= SD_ITEM_CHEST;
        render_tank(&t, other, TANK_W);
        assert(memcmp(full, other, bytes));
        memcpy(full, other, bytes);
        t.sd_unlocks |= SD_ITEM_SHELL | SD_ITEM_CORAL;
        render_tank(&t, other, TANK_W);
        /* All owned decorations remain visible in Simple. */
        assert(memcmp(full, other, bytes));
        memcpy(full, other, bytes);
        t.sd_unlocks &= ~SD_ITEM_SHELL;
        render_tank(&t, other, TANK_W);
        assert(memcmp(full, other, bytes));
        /* Same static scene, cached or live, including partial rows/regions. */
        render_set_backdrop_cache(NULL, 0);
        render_tank(&t, full, TANK_W);
        for (int capacity = 0; capacity < 2; capacity++) {
            size_t cache_bytes = capacity ? 48000 : 64;
            unsigned char *cache = malloc(cache_bytes + 4);
            assert(cache);
            cache[0] = cache[cache_bytes + 2] = 0xa5;
            render_set_backdrop_cache(cache + 2, cache_bytes);
            render_tank(&t, other, TANK_W);
            assert(!memcmp(full, other, bytes));
            assert(cache[0] == 0xa5 && cache[cache_bytes + 2] == 0xa5);
            for (int y = 0; y < TANK_H; y += 80) {
                int h = TANK_H - y < 80 ? TANK_H - y : 80;
                render_tile(y, h);
                render_tank(&t, tile + 1, TANK_W);
                assert(!memcmp(full + y * TANK_W, tile + 1, (size_t)TANK_W * h * 2));
            }
            render_tile(0, 0);
            render_region(17, 29);
            render_tank(&t, other, TANK_W);
            render_region(0, 0);
            assert(!memcmp(full, other, bytes));
            if (capacity) for (int change = 0; change < 6; change++) {
                if (change == 0) t.coral_rgb = 0xf09030;
                if (change == 1) tank_decor_set(&t, 3, TANK_W / 2, DECOR_Z_FRONT);
                if (change == 2) t.veg_h[0][0] = .1f;
                if (change == 3) t.season = 2;
                if (change == 4) t.sd_unlocks |= SD_ITEM_SHELL;
                if (change == 5) t.sd_unlocks &= ~SD_ITEM_CORAL;
                render_tank(&t, other, TANK_W);
                render_set_backdrop_cache(NULL, 0);
                render_tank(&t, full, TANK_W);
                assert(!memcmp(full, other, bytes));
                render_set_backdrop_cache(cache + 2, cache_bytes);
                render_tank(&t, other, TANK_W);
            }
            render_set_backdrop_cache(NULL, 0);
            free(cache);
        }
        t.clock += .75f;
        render_tank(&t, other, TANK_W);
        assert(!memcmp(full, other, bytes));
        t.n_fish = 1;
        t.fish[0] = before.fish[0];
        t.fish[0].x = TANK_W / 2; t.fish[0].y = TANK_H / 2;
        render_tank(&t, other, TANK_W);
        assert(memcmp(full, other, bytes));
        render_settings_open();
        clock_tap(&t, 100, 100);
        assert(clock_tap(&t, 200, 192) == SET_TAP_DETAIL);
        assert(clock_tap(&t, 200, 232) == SET_TAP_ROOM);
        assert(clock_tap(&t, 200, 272) == SET_TAP_LED);
        render_damage_begin(&t, false);
        int cursor = 0, x, y, width, height, area = 0;
        while (render_damage_next(&cursor, &x, &y, &width, &height)) area += width * height;
        assert(area == TANK_W * TANK_H);
        device.scene_detail = SCENE_BALANCED; render_set_device(&device);
        render_damage_begin(&t, false);
        cursor = area = 0;
        while (render_damage_next(&cursor, &x, &y, &width, &height)) area += width * height;
        assert(area == TANK_W * TANK_H);
        t.n_fish = 0;
        t.sd_unlocks = SD_ITEM_PLANT;
        for (int b = 0; b < VEG_BEDS_MAX; b++)
            for (int i = 0; i < VEG_FRONDS_MAX; i++) t.veg_h[b][i] = .8f;
        for (int detail = SCENE_BALANCED; detail <= SCENE_ENHANCED; detail += 2) {
            device.scene_detail = (uint8_t)detail; render_set_device(&device);
            t.clock = 2.01f; render_tank(&t, full, TANK_W);
            t.clock = 2.19f; render_tank(&t, other, TANK_W);
            assert((memcmp(full, other, bytes) != 0) == (detail == SCENE_ENHANCED));
        }
        device.scene_detail = SCENE_SIMPLE;
        free(full); free(other); free(tile);
    }
    device.scene_detail = SCENE_BALANCED; render_set_device(&device);
    tank_resolution(false);
    puts("PASS: Simple scene static grass/hidden helpers/bubbles, visible food, no tank mutation, guarded tiles, both scales and reversible settings/damage");
}
int main(int argc, char **argv) {
    setbuf(stdout, NULL);
    setbuf(stderr, NULL);
    test_tank_polish();
    test_story();
    test_genetics();
    test_social_and_preview();
    test_theme_detail_damage();
    if (argc == 2 && !strcmp(argv[1], "--clock-only")) {
        test_clock();
        { extern void test_clock_weather(void); test_clock_weather(); }
        return 0;
    }
    test_swim_extent();
    test_collision_turns();
    test_fish_tile_culling();
    test_cpu_damage();
    test_static_backdrop();
    test_solid_clock();
    test_compact_activity();
    size_t word_count = render_mask_scratch_size() / sizeof(uint32_t);
    assert(render_mask_scratch_size() % sizeof(uint32_t) == 0);
    uint32_t *word_guard = malloc((word_count + 2) * sizeof *word_guard); assert(word_guard);
    word_guard[0] = word_guard[word_count + 1] = 0xa5a5a5a5u;
    render_set_mask_scratch(word_guard + 1);
    tank_t tank; memset(&tank, 0xa5, sizeof tank); tank_init(&tank, 1234); boot_enlarged(&tank);
    assert(tank.pumpkin_x == 0 && tank.chest_x == 0 && tank.decor_rank[5] == 0);
    progression_sd_grant(&tank, SD_PRICE_CHEST + 20);
    assert(!progression_buy(&tank, -1) && !progression_buy(&tank, SD_ITEM_COUNT));
    assert(progression_buy(&tank, 9) && tank.sd_balance == 20);
    assert(!progression_buy(&tank, 9));
    assert(tank.sd_unlocks & SD_ITEM_CHEST);
    tank_decor_set(&tank, 9, 180, DECOR_Z_FRONT);
    assert(tank_decor_z(&tank, 9) == DECOR_Z_FRONT);
    assert(tank_decor_hit(&tank, tank_decor_x(&tank, 9), TANK_BOT - 22) == 9);
    tank_set_name(&tank, 0, "goldie");
    tank.tools_locked = true; tank.autofeed_off = true;
    tank.fish[0].trust = 7.25f; tank.fish[0].eaten = 11;
    tank.sd_paid_fish[0] = 3; tank.sd_colonies_paid = 2;
    tank.algae[0] = 123; tank.veg_h[0][0] = 0.45f;
    char *text = progression_backup_json(&tank); assert(text);
    int before = writes;
    bool valid = progression_restore_json(text, strlen(text), false, error, sizeof error);
    if (!valid) fprintf(stderr, "Round-trip validation failed: %s\n", error);
    assert(valid);
    assert(writes == before);
    assert(progression_restore_json(text, strlen(text), true, error, sizeof error));
    assert(writes == before + 1 && saved_size == AQUARIUM_SAVE_BYTES);
    tank_t restored; tank_init(&restored, 4321); boot_enlarged(&restored);
    assert(restored.sd_balance == 20 && restored.sd_unlocks & SD_ITEM_CHEST);
    assert(!strcmp(restored.fish[0].name, "goldie"));
    assert(restored.tools_locked && restored.autofeed_off);
    assert(restored.fish[0].trust == 7.25f && restored.fish[0].eaten == 11);
    assert(restored.sd_paid_fish[0] == 3 && restored.sd_colonies_paid == 2);
    assert(restored.algae[0] == 123 && restored.veg_h[0][0] == 0.45f);
    assert(restored.chest_x == tank.chest_x && restored.chest_z == DECOR_Z_FRONT);
    assert(restored.decor_rank[5] == tank.decor_rank[5]);
    reject_write = true;
    assert(!progression_restore_json(text, strlen(text), true, error, sizeof error));
    reject_write = false;
    assert(SD_ITEMS[9].price == 120 && progression_sell_value(9) == 24);
    assert(progression_sell(&restored, 9) && restored.sd_balance == 44);
    assert(!(restored.sd_unlocks & SD_ITEM_CHEST));
    restored.sd_balance = 119;
    assert(progression_item_unlocked(&restored, 9) && progression_buy(&restored, 9) && restored.sd_balance == 119);
    assert(!progression_sell_value_for(&restored, 9));
    assert(progression_sell(&restored, 9) && restored.sd_balance == 119);
    assert(progression_buy(&restored, 9) && restored.sd_balance == 119);
    invalid(text, strlen(text) - 1);
    invalid("{}", 2);
    invalid("null", 4);
    char *trailing = malloc(strlen(text) + 4); assert(trailing);
    sprintf(trailing, "%s []", text); invalid(trailing, strlen(trailing)); free(trailing);
    const char *keys[] = {"sd_balance", "n_fish", "chest_x", "sd_unlocks", "names", "veg_h", "algae", "chest_z1"};
    for (size_t i = 0; i < sizeof keys / sizeof keys[0]; i++) {
        cJSON *root = cJSON_Parse(text), *obj = cJSON_GetObjectItemCaseSensitive(root, "tank");
        cJSON_DeleteItemFromObjectCaseSensitive(obj, keys[i]); invalid_object(root);
    }
    const char *range_keys[] = {"sd_balance", "n_fish", "chest_x", "chest_z1", "sd_unlocks"};
    const double range_values[] = {-1, 7, TANK_W + 1, 4, (double)UINT32_MAX + 1};
    for (size_t i = 0; i < sizeof range_keys / sizeof range_keys[0]; i++) {
        cJSON *root = cJSON_Parse(text);
        replace_number(cJSON_GetObjectItemCaseSensitive(root, "tank"), range_keys[i], range_values[i]); invalid_object(root);
    }
    cJSON *root = cJSON_Parse(text); replace_number(root, "schema", 99); invalid_object(root);
    root = cJSON_Parse(text); replace_number(root, "width", 448); invalid_object(root);
    root = cJSON_Parse(text);
    cJSON_AddNumberToObject(cJSON_GetObjectItemCaseSensitive(root, "tank"), "sd_balance", 20); invalid_object(root);
    root = cJSON_Parse(text);
    cJSON_AddStringToObject(root, "unknown", "field"); invalid_object(root);
    root = cJSON_Parse(text);
    cJSON *f = cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(root, "fish"), 0);
    replace_number(f, "hunger", 99); invalid_object(root);
    root = cJSON_Parse(text);
    f = cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(root, "fish"), 0);
    replace_number(f, "preset", 255); invalid_object(root);
    root = cJSON_Parse(text);
    cJSON *obj = cJSON_GetObjectItemCaseSensitive(root, "tank");
    cJSON *names = cJSON_GetObjectItemCaseSensitive(obj, "names");
    cJSON_ReplaceItemInArray(names, 0, cJSON_CreateString("too-long-name")); invalid_object(root);
    root = cJSON_Parse(text);
    obj = cJSON_GetObjectItemCaseSensitive(root, "tank");
    cJSON_DeleteItemFromArray(cJSON_GetObjectItemCaseSensitive(obj, "veg_h"), 0); invalid_object(root);
    const char *needle = "\"sd_balance\":20";
    char *position = strstr(text, needle); assert(position);
    char *overflow = malloc(strlen(text) + 16); assert(overflow);
    size_t prefix = (size_t)(position - text);
    memcpy(overflow, text, prefix);
    sprintf(overflow + prefix, "\"sd_balance\":1e999%s", position + strlen(needle));
    invalid(overflow, strlen(overflow)); free(overflow);
    invalid("[[[[[[[[[[0]]]]]]]]]]", 21);
    char *huge = malloc(32769); assert(huge); memset(huge, ' ', 32769); invalid(huge, 32769); free(huge);
    for (int i = 1; i < argc; i++) {
        FILE *fixture = fopen(argv[i], "rb"); assert(fixture);
        saved_size = fread(saved, 1, sizeof saved, fixture); fclose(fixture);
        assert(saved_size >= 448 && saved_size < 1920);
        int expected = saved[23];
        tank_init(&restored, 2345); progression_wake(&restored, 0);
        assert(restored.n_fish >= expected);
        assert(!(restored.sd_unlocks & SD_ITEM_CHEST) && restored.chest_x == 0);
        assert(progression_save(&restored) && saved_size == AQUARIUM_SAVE_BYTES);
    }
    uint16_t *fb = calloc(TANK_W * TANK_H, sizeof *fb); assert(fb);
    tank_t shop_tank = tank;
    shop_tank.sd_unlocks &= ~SD_ITEM_CHEST; shop_tank.sd_balance = SD_PRICE_CHEST;
    render_shop_leave();
    int chest_row = 0;
    while (progression_shop_item(&shop_tank, chest_row) != 9) chest_row++;
    chest_row += progression_fry_for_sale(&shop_tank);
    for (int i = 0; i < chest_row / SHP_PER_PAGE; i++) assert(render_shop_tap(&shop_tank, render_frame_x(SHP_ARROW_X1 + 12), render_frame_y(40)) == SHOP_TAP_KEPT);
    int row = chest_row % SHP_PER_PAGE;
    assert(render_shop_tap(&shop_tank, render_frame_x(150), render_frame_y(SHP_ROW_Y0 + row * SHP_ROW_DY + 16)) == SHOP_TAP_KEPT);
    render_shop(&shop_tank, fb, TANK_W);
    assert(render_shop_tap(&shop_tank, render_frame_x(224), render_frame_y(260)) == SHOP_TAP_BUY + 9);
    render_shop_leave();
    size_t scratch_size = render_decor_scratch_size();
    unsigned char *scratch = calloc(1, scratch_size + 64); assert(scratch);
    memset(scratch + scratch_size, 0xa5, 64); render_set_decor_scratch(scratch);
    tank.sd_unlocks = SD_ITEM_MASK;
    for (int s = 0; s < DECOR_SLOTS; s++) tank_decor_set(&tank, tank_decor_slot_item(s), TANK_W / 2, DECOR_Z_FRONT);
    render_tank(&tank, fb, TANK_W);
    tank.chest_z = DECOR_Z_BACK; render_tank(&tank, fb, TANK_W);
    uint16_t *scene = calloc(TANK_W * TANK_H, sizeof *scene); assert(scene);
    uint32_t *dirty = calloc(RENDER_DIRTY_WORDS, sizeof *dirty); assert(dirty);
    render_set_scene_cache(scene); render_set_dirty_mask(dirty);
    for (int pass = 0; pass < 12; pass++) {
        tank.night = (pass & 1) != 0;
        tank.chest_z = pass % 3 ? DECOR_Z_FRONT : DECOR_Z_BACK;
        tank_decor_raise(&tank, tank_decor_slot_item(pass % DECOR_SLOTS));
        render_tank(&tank, fb, TANK_W);
        for (int i = 0; i < 64; i++) assert(scratch[scratch_size + i] == 0xa5);
    }
    settings_section(&tank, 3);
    render_settings(&tank, fb, TANK_W, 100);
    int value = 0;
    float bx = render_frame_x(224), rx = render_frame_x(224), y = render_frame_y(84);
    assert(render_settings_tap(bx, y, &value) == SET_TAP_BACKUP);
    assert(render_settings_tap(rx, render_frame_y(132), &value) == SET_TAP_RESTORE);
    render_settings_open();
    assert(render_settings_tap(render_frame_x(392), render_frame_y(19), &value) == SET_TAP_CLOSE);
    settings_section(&tank, 5);
    assert(render_settings_tap(render_frame_x(88), render_frame_y(120), &value) == SET_TAP_UPDATES);
    assert(render_settings_tap(render_frame_x(360), render_frame_y(190), &value) == SET_TAP_TOUCH);
    assert(render_settings_tap(render_frame_x(166), render_frame_y(SET_FOOT_Y + 15), &value) == SET_TAP_NONE);
    for (int py = 0; py < PAGE_H; py += 4) for (int px = 0; px < PAGE_W; px += 4) {
        int hit = render_settings_tap(render_frame_x(px), render_frame_y(py), &value);
        assert(hit != SET_TAP_LIGHT && hit != SET_TAP_IDLE);
    }
    assert(render_text_w("SAND: ELECTRIC CYAN", 2) < SET_COLOR_W - 16);
    assert(render_text_w("WATER: NEON LIME", 2) < SET_COLOR_W - 16);
    assert(render_text_w("TOUCH CALIBRATION", 2) < SET_TOUCH_W - 16);
    updates_sd_status("READY: 0.3.8 - TAP INSTALL TO CONFIRM", true);
    render_updates_page(fb, TANK_W);
    assert(updates_page_tap(render_frame_x(200), render_frame_y(UPD_CHECK_Y + 20)) == UPD_TAP_CHECK);
    assert(updates_page_tap(render_frame_x(100), render_frame_y(UPD_FORGET_Y + 10)) == UPD_TAP_NONE);
    updates_sd_status("NO POCKET-TANK/UPDATE.PTU", false);
    render_updates_page(fb, TANK_W);
    float cx = render_frame_x(392), cy = render_frame_y(19);
    render_settings_open();
    render_settings_touch(&tank, cx, cy, true, &value);
    assert(render_settings_touch(&tank, cx, cy, false, &value) == SET_TAP_CLOSE);
    settings_section(&tank, 5);
    cx = render_frame_x(88); cy = render_frame_y(120);
    render_settings_touch(&tank, cx, cy, true, &value);
    assert(render_settings_touch(&tank, cx, cy, false, &value) == SET_TAP_UPDATES);
    cx = render_frame_x(360); cy = render_frame_y(190);
    render_settings_touch(&tank, cx, cy, true, &value);
    assert(render_settings_touch(&tank, cx, cy, false, &value) == SET_TAP_TOUCH);
    cx = render_frame_x(100); cy = render_frame_y(260);
    int wipe_writes = writes;
    render_settings_touch(&tank, cx, cy, true, &value);
    assert(writes == wipe_writes);
    assert(render_settings_touch(&tank, cx, cy, false, &value) == SET_TAP_WIPE);
    assert(writes == wipe_writes);
    render_settings_touch(&tank, cx, cy, true, &value);
    assert(render_settings_touch(&tank, render_frame_x(440), cy, false, &value) == SET_TAP_NONE);
    render_confirm_reset(fb, TANK_W, 1);
    assert(render_confirm_hit(render_frame_x(RENDER_CONFIRM_NO_X + 30), render_frame_y(RENDER_CONFIRM_BTN_Y + 25)) == -1);
    assert(render_confirm_hit(render_frame_x(RENDER_CONFIRM_YES_X + 30), render_frame_y(RENDER_CONFIRM_BTN_Y + 25)) == 1);
    assert(render_confirm_hit(cx, cy) == 0);
    settings_section(&tank, 4);
    tank.autofeed_off = false;
    cx = render_frame_x(224); cy = render_frame_y(198);
    render_settings_touch(&tank, cx, cy, true, &value);
    assert(render_settings_touch(&tank, cx, cy, false, &value) == SET_TAP_FEED && value == 0 && tank.autofeed_off);
    render_settings_touch(&tank, cx, cy, true, &value);
    assert(render_settings_touch(&tank, cx, cy, false, &value) == SET_TAP_FEED && value == 1 && !tank.autofeed_off);
    tank.orient_lock = true; tank.orient_inv = true;
    assert(!tank_orient(&tank, true));
    tank.orient_lock = false;
    assert(!tank_orient(&tank, true));
    settings_section(&tank, 3);
    y = render_frame_y(132);
    render_settings_touch(&tank, rx, y, true, &value);
    assert(render_settings_touch(&tank, rx, y, false, &value) == SET_TAP_RESTORE && value == 0);
    render_backup_status("confirm", true);
    render_settings_touch(&tank, rx, y, true, &value);
    assert(render_settings_touch(&tank, rx, y, false, &value) == SET_TAP_RESTORE && value == 1);
    tank.clock += 11;
    render_settings_touch(&tank, rx, y, true, &value);
    assert(render_settings_touch(&tank, rx, y, false, &value) == SET_TAP_RESTORE && value == 0);
    const struct { int section, x, y, action; } device_taps[] = {
        {0, 200, 190, SET_TAP_DETAIL}, {0, 200, 232, SET_TAP_ROOM}, {0, 200, 272, SET_TAP_LED},
        {3, 224, 180, SET_TAP_AUTO_BACKUP}, {3, 224, 228, SET_TAP_PHOTO}
    };
    for (int scale = 0; scale < 2; scale++) {
        tank_resolution(scale != 0);
        for (unsigned i = 0; i < sizeof device_taps / sizeof *device_taps; i++) {
            render_backup_status("JSON BACKUP / RESTORE FROM SD", false);
            settings_section(&tank, device_taps[i].section);
            if (device_taps[i].section == 0) {
                size_t bytes = (size_t)TANK_W * TANK_H * sizeof(uint16_t);
                uint16_t *actual = malloc(bytes), *expected = malloc(bytes);
                assert(actual && expected);
                render_settings(&tank, actual, TANK_W, 100);
                int back = (int)render_frame_y(12) * TANK_W + (int)render_frame_x(350);
                int background = (int)render_frame_y(12) * TANK_W + (int)render_frame_x(330);
                assert(actual[back] != actual[background]);
                render_backup_status("ROOM LIGHT DIMMING / BACK LED GLOW", false);
                render_settings(&tank, expected, TANK_W, 100);
                assert(!memcmp(actual, expected, bytes));
                free(actual); free(expected);
            }
            float tx = render_frame_x(device_taps[i].x), ty = render_frame_y(device_taps[i].y);
            assert(render_settings_tap(tx, ty, &value) == device_taps[i].action);
            assert(render_settings_touch(&tank, tx, ty, true, &value) == SET_TAP_NONE);
            assert(render_settings_touch(&tank, tx, ty, false, &value) == device_taps[i].action);
            assert(render_settings_touch(&tank, tx, ty, false, &value) == SET_TAP_NONE);
            render_settings_touch(&tank, tx, ty, true, &value);
            assert(render_settings_touch(&tank, render_frame_x(440), ty, false, &value) == SET_TAP_NONE);
        }
    }
    tank_resolution(false);
    render_set_scene_cache(NULL); render_set_dirty_mask(NULL); render_set_decor_scratch(NULL);
    free(scene); free(dirty); free(scratch); free(fb); free(text);
    tank_init(&tank, 999); saved_size = 0; boot_enlarged(&tank);
    assert(tank.sand_style == 0 && tank.water_style == 0);
    progression_sd_grant(&tank, 1603);
    const int expected_prices[] = {60, 60, 80, 80, 300, 300, 200, 60, 100, 80, 160, 120};
    int cost = 0;
    for (int item = 10; item < 22; item++) {
        cost += SD_ITEMS[item].price;
        assert(SD_ITEMS[item].price == expected_prices[item - 10]);
        tank.sd_balance = SD_ITEMS[item].price - 1;
        assert(!progression_buy(&tank, item));
        tank.sd_balance = 1603 - (cost - SD_ITEMS[item].price);
        assert(progression_buy(&tank, item));
        assert(!progression_buy(&tank, item));
        if (tank_decor_placeable(item)) {
            tank_decor_set(&tank, item, 50 + (item - 10) * 45, DECOR_Z_FRONT);
            tank_decor_raise(&tank, item);
            assert(tank_decor_slot(item) == (item < 14 ? item - 4 : item - 6));
            tank_t only = tank; only.sd_unlocks = SD_ITEMS[item].bit;
            assert(tank_decor_hit(&only, tank_decor_x(&only, item),
                                 TANK_BOT - 14 + DECOR_SINK - tank_ornament_h(item) + 12) == item);
        }
    }
    assert(cost == 1600 && tank.sd_balance == 3);
    assert(tank.sand_style == 1 && tank.water_style == 1);
    assert(!tank_decor_placeable(14) && !tank_decor_placeable(15));
    assert(!progression_sell(&tank, 14) && !progression_sell(&tank, 15));
    text = progression_backup_json(&tank); assert(text);
    assert(progression_restore_json(text, strlen(text), true, error, sizeof error));
    tank_init(&restored, 234); boot_enlarged(&restored);
    assert(restored.sd_balance == 3 && restored.sand_style == 1 && restored.water_style == 1);
    for (int i = 0; i < 10; i++) {
        assert(restored.ornament_x[i] == tank.ornament_x[i]);
        assert(restored.ornament_z[i] == DECOR_Z_FRONT && restored.decor_rank[i + 6] == tank.decor_rank[i + 6]);
    }
    root = cJSON_Parse(text);
    obj = cJSON_GetObjectItemCaseSensitive(root, "tank");
    cJSON_DeleteItemFromArray(cJSON_GetObjectItemCaseSensitive(obj, "ornament_x"), 0); invalid_object(root);
    root = cJSON_Parse(text); replace_number(cJSON_GetObjectItemCaseSensitive(root, "tank"), "water_style", WATER_STYLE_COUNT); invalid_object(root);
    root = cJSON_Parse(text); replace_number(cJSON_GetObjectItemCaseSensitive(root, "tank"), "sand_style", SAND_STYLE_COUNT); invalid_object(root);
    root = cJSON_Parse(text); replace_number(cJSON_GetObjectItemCaseSensitive(root, "tank"), "sd_unlocks", SD_ITEM_BUOY); invalid_object(root);
    for (int item = 10; item < SD_ITEM_COUNT; item++) {
        if (!tank_decor_placeable(item)) continue;
        int balance = restored.sd_balance;
        assert(progression_sell(&restored, item));
        assert(restored.sd_balance == balance + progression_sell_value(item));
        assert(restored.ornament_x[tank_ornament_index(item)] == 0 && !tank_decor_owned(&restored, item));
    }
    uint16_t *a = calloc(TANK_W * TANK_H, sizeof *a), *b = calloc(TANK_W * TANK_H, sizeof *b); assert(a && b);
    scratch = calloc(1, render_decor_scratch_size()); assert(scratch); render_set_decor_scratch(scratch);
    render_tank(&tank, a, TANK_W);
    memset(a, 0, TANK_W * TANK_H * sizeof *a);
    render_ornament_preview(a, TANK_W, 13, 100, 100);
    tank.clock += 2; render_tank(&tank, b, TANK_W);
    memset(b, 0, TANK_W * TANK_H * sizeof *b);
    render_ornament_preview(b, TANK_W, 13, 100, 100);
    assert(tank_ornament_h(13) >= TANK_H / 2);
    assert(memcmp(a, b, TANK_W * TANK_H * sizeof *a));  /* isolated buoy art changes, not just fish/grass */
    scene = calloc(TANK_W * TANK_H, sizeof *scene); dirty = calloc(RENDER_DIRTY_WORDS, sizeof *dirty); assert(scene && dirty);
    render_set_scene_cache(scene); render_set_dirty_mask(dirty);
    for (int i = 0; i < SAND_STYLE_COUNT; i++) {
        tank.sand_style = (uint8_t)i; render_tank(&tank, a, TANK_W);
        tank.sand_style = (uint8_t)((i + 1) % SAND_STYLE_COUNT); render_tank(&tank, b, TANK_W);
        assert(memcmp(a, b, TANK_W * TANK_H * sizeof *a));
    }
    for (int i = 0; i < WATER_STYLE_COUNT; i++) {
        tank.water_style = (uint8_t)i; render_tank(&tank, a, TANK_W);
        tank.water_style = (uint8_t)((i + 1) % WATER_STYLE_COUNT); render_tank(&tank, b, TANK_W);
        assert(memcmp(a, b, TANK_W * TANK_H * sizeof *a));
    }
    for (int style = WATER_STYLE_RAINBOW; style < WATER_STYLE_COUNT; style++) {
        tank.water_style = (uint8_t)style;
        char *color_json = progression_backup_json(&tank); assert(color_json);
        assert(progression_restore_json(color_json, strlen(color_json), true, error, sizeof error));
        tank_init(&restored, 222); boot_enlarged(&restored);
        assert(restored.water_style == style);
        free(color_json);
    }
    for (int style = SAND_STYLE_RAINBOW; style < SAND_STYLE_COUNT; style++) {
        tank.sand_style = (uint8_t)style;
        char *color_json = progression_backup_json(&tank); assert(color_json);
        assert(progression_restore_json(color_json, strlen(color_json), true, error, sizeof error));
        tank_init(&restored, 222); boot_enlarged(&restored);
        assert(restored.sand_style == style);
        free(color_json);
    }
    tank_t empty; tank_init(&empty, 111);
    empty.n_fish = 0; empty.sd_unlocks = SD_ITEM_LIGHTS; empty.water_style = WATER_STYLE_RAINBOW;
    for (int i = 0; i < MAX_BUBBLE; i++) empty.bubble[i].x = -100;
    render_tank(&empty, a, TANK_W);
    int red_row = 8 * TANK_W + TANK_W / 2, green_row = 85 * TANK_W + TANK_W / 2;
    int blue_row = 140 * TANK_W + TANK_W / 2;
    assert(((a[red_row] >> 11) & 31) > (a[red_row] & 31) * 2);
    assert(((a[green_row] >> 5) & 63) > ((a[green_row] >> 11) & 31) * 2);
    assert((a[blue_row] & 31) > ((a[blue_row] >> 11) & 31) * 2);
    empty.sd_unlocks |= SD_ITEM_SAND; empty.sand_style = SAND_STYLE_RAINBOW;
    render_tank(&empty, a, TANK_W);
    uint16_t red_sand = scene[170 * TANK_W + 30], green_sand = scene[170 * TANK_W + 120];
    uint16_t blue_sand = scene[170 * TANK_W + 200];
    assert(((red_sand >> 11) & 31) > (red_sand & 31) * 2);
    assert(((green_sand >> 5) & 63) > ((green_sand >> 11) & 31) * 2);
    assert((blue_sand & 31) > ((blue_sand >> 11) & 31) * 2);
    render_set_scene_cache(NULL); render_set_dirty_mask(NULL);
    render_tank(&empty, a, TANK_W);
    empty.sand_style = 0; empty.water_style = 0; render_tank(&empty, b, TANK_W);
    assert(memcmp(a, b, TANK_W * TANK_H * sizeof *a));
    render_set_scene_cache(scene); render_set_dirty_mask(dirty);
    tank.sand_style = 0; tank.water_style = 0;
    companion_open_section(COMPANION_SECTION_APPEARANCE);
    for (int i = 0; i < WATER_STYLE_COUNT; i++) {
        assert(!companion_tap(&tank, render_frame_x(300), render_frame_y(175)));
        assert(tank.water_style == (i + 1) % WATER_STYLE_COUNT);
    }
    for (int i = 0; i < SAND_STYLE_COUNT; i++) {
        assert(!companion_tap(&tank, render_frame_x(120), render_frame_y(115)));
        assert(tank.sand_style == (i + 1) % SAND_STYLE_COUNT);
    }
    for (int item = 10; item < SD_ITEM_COUNT; item++) {
        tank_t shop = tank; shop.sd_unlocks = 0; shop.polish.scenery_unlocks = 0; shop.sd_balance = SD_ITEMS[item].price;
        int row_index = 0;
        while (progression_shop_item(&shop, row_index) != item) row_index++;
        row_index += progression_fry_for_sale(&shop);
        render_shop_leave();
        for (int page = 0; page < row_index / SHP_PER_PAGE; page++)
            render_shop_tap(&shop, render_frame_x(SHP_ARROW_X1 + 12), render_frame_y(SHP_ARROW_Y + 8));
        render_shop_tap(&shop, render_frame_x(150), render_frame_y(SHP_ROW_Y0 + row_index % SHP_PER_PAGE * SHP_ROW_DY + 16));
        render_shop(&shop, a, TANK_W);
        assert(render_shop_tap(&shop, render_frame_x(224), render_frame_y(260)) == SHOP_TAP_BUY + item);
        if (SD_IS_FIXED_SCENERY(item)) {
            if (SD_IS_SCENERY(item)) shop.polish.scenery_unlocks = (uint8_t)(1u << (item - SD_SCENERY_FIRST));
            else shop.sd_unlocks = SD_ITEMS[item].bit;
            render_shop_tap(&shop, render_frame_x(150), render_frame_y(SHP_ROW_Y0 + row_index % SHP_PER_PAGE * SHP_ROW_DY + 16));
            assert(render_shop_tap(&shop, render_frame_x(224), render_frame_y(260)) == SHOP_TAP_KEPT);
            render_shop(&shop, a, TANK_W);
            assert(render_shop_tap(&shop, render_frame_x(224), render_frame_y(260)) == SHOP_TAP_SELL + item);
        }
    }
    render_shop_leave();
    tank_t edits = tank;
    int card_fish, rename_x, sell_x, button_y;
    render_milestones_show_fish(&edits, 0); render_milestones(&edits, a, TANK_W);
    assert(render_milestones_card(&edits, &card_fish, &rename_x, &sell_x, &button_y) && card_fish == 0);
    assert(render_milestones_tap(&edits, render_frame_x(224), button_y - render_frame_y(44)) == MS_TAP_CUSTOM);
    render_milestones_show_fish(&edits, 0);
    assert(render_milestones_tap(&edits, rename_x, button_y) == MS_TAP_RENAME);
    render_milestones_leave();
    edits.journal.xp = journal_level_xp(JOURNAL_LEVEL_MAX);
    uint32_t old_body = edits.fish[0].color, old_accent = edits.fish[0].accent;
    setup_begin_customize(&edits, 0);
    assert(setup_active() && setup_fish() == 0 && setup_page() == SETUP_PG_CUSTOM);
    for (int row = 0; row < 4; row++)
        assert(setup_hit(render_frame_x(220), render_frame_y(SETUP_CUSTOM_Y + row * SETUP_CUSTOM_DY + 18)) == SETUP_HIT_CUSTOM0 + row);
    setup_activate(&edits, SETUP_HIT_CUSTOM0);
    setup_activate(&edits, SETUP_HIT_CUSTOM0 + 1);
    setup_activate(&edits, SETUP_HIT_CUSTOM0 + 2);
    setup_activate(&edits, SETUP_HIT_CUSTOM0 + 3);
    assert(edits.fish[0].variety == 0 && edits.fish[0].pattern == 0 && edits.fish[0].color == old_body);
    assert(progression_save(&edits));
    tank_init(&restored, 222); boot_enlarged(&restored);
    assert(restored.fish[0].variety == 0 && restored.fish[0].color == old_body);
    render_setup(&edits, a, TANK_W, 0);
    setup_activate(&edits, SETUP_HIT_BACK);
    assert(!setup_active() && edits.fish[0].variety == 0 && edits.fish[0].pattern == 0);
    assert(edits.fish[0].color == old_body && edits.fish[0].accent == old_accent);
    setup_take_renamed();
    setup_begin_customize(&edits, 0);
    for (int i = 0; i < 4; i++) setup_activate(&edits, SETUP_HIT_CUSTOM0);
    for (int i = 0; i < 2; i++) setup_activate(&edits, SETUP_HIT_CUSTOM0 + 1);
    reject_write = true; setup_activate(&edits, SETUP_HIT_NEXT);
    assert(setup_active());
    reject_write = false; setup_activate(&edits, SETUP_HIT_NEXT);
    assert(!setup_active() && setup_take_renamed() == 0);
    tank_init(&restored, 222); boot_enlarged(&restored);
    assert(restored.fish[0].variety == FISH_PUFFER && restored.fish[0].pattern == PATTERN_BAND);
    char *custom_json = progression_backup_json(&edits); assert(custom_json);
    assert(progression_restore_json(custom_json, strlen(custom_json), true, error, sizeof error));
    tank_init(&restored, 222); boot_enlarged(&restored);
    assert(restored.fish[0].variety == FISH_PUFFER && restored.fish[0].pattern == PATTERN_BAND);
    root = cJSON_Parse(custom_json);
    cJSON *values = cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(root, "tank"), "fish_variety");
    cJSON_ReplaceItemInArray(values, 0, cJSON_CreateNumber(FISH_VARIETY_COUNT)); invalid_object(root);
    free(custom_json);
    fish_t portrait = edits.fish[0]; portrait.stage = STAGE_ADULT; portrait.pattern = PATTERN_STRIPES;
    for (int style = 0; style < FISH_VARIETY_COUNT; style++) {
        memset(a, 0, TANK_W * TANK_H * sizeof *a); memset(b, 0, TANK_W * TANK_H * sizeof *b);
        portrait.variety = (uint8_t)style; render_fish_portrait(a, TANK_W, 224, 180, 2, &portrait, 0);
        portrait.variety = (uint8_t)((style + 1) % FISH_VARIETY_COUNT); render_fish_portrait(b, TANK_W, 224, 180, 2, &portrait, 0);
        assert(memcmp(a, b, TANK_W * TANK_H * sizeof *a));
    }
    for (int style = 0; style < FISH_PATTERN_COUNT; style++) {
        memset(a, 0, TANK_W * TANK_H * sizeof *a); memset(b, 0, TANK_W * TANK_H * sizeof *b);
        portrait.pattern = (uint8_t)style; render_fish_portrait(a, TANK_W, 224, 180, 2, &portrait, 0);
        portrait.pattern = (uint8_t)((style + 1) % FISH_PATTERN_COUNT); render_fish_portrait(b, TANK_W, 224, 180, 2, &portrait, 0);
        assert(memcmp(a, b, TANK_W * TANK_H * sizeof *a));
    }
    portrait.pattern = PATTERN_PLAIN;
    for (int style = 0; style < FISH_VARIETY_COUNT; style++) {
        assert(render_text_w(FISH_VARIETY_NAMES[style], 1) <= SHP_FRY_BOX_W - 8);
        assert(render_text_w(FISH_VARIETY_NAMES[style], 2) + render_text_w("VARIETY: ", 2) < SETUP_CUSTOM_W - 12);
        memset(a, 0, TANK_W * TANK_H * sizeof *a);
        portrait.variety = (uint8_t)style; render_fish_portrait(a, TANK_W, 224, 180, 2, &portrait, 0);
        for (int other = 0; other < style; other++) {
            memset(b, 0, TANK_W * TANK_H * sizeof *b);
            portrait.variety = (uint8_t)other; render_fish_portrait(b, TANK_W, 224, 180, 2, &portrait, 0);
            assert(memcmp(a, b, TANK_W * TANK_H * sizeof *a));
        }
        setup_begin_customize(&edits, 0);
        for (int step = 0; edits.fish[0].variety != style && step < FISH_VARIETY_COUNT; step++) {
            setup_activate(&edits, SETUP_HIT_CUSTOM0);
            setup_activate(&edits, SETUP_HIT_NEXT); setup_take_renamed();
            if (edits.fish[0].variety != style) setup_begin_customize(&edits, 0);
        }
        setup_cancel(&edits);
        edits.fish[0].variety = (uint8_t)style;
        assert(progression_save(&edits));
        tank_init(&restored, 222); boot_enlarged(&restored);
        assert(restored.fish[0].variety == style);
        char *species_json = progression_backup_json(&edits); assert(species_json);
        assert(progression_restore_json(species_json, strlen(species_json), true, error, sizeof error)); free(species_json);
        tank_init(&restored, 222); boot_enlarged(&restored);
        assert(restored.fish[0].variety == style && restored.fish[0].pattern == PATTERN_BAND);
    }
    portrait.variety = FISH_JELLYFISH;
    for (int variety = FISH_SHARK; variety < FISH_VARIETY_COUNT; variety++) {
        portrait.variety = (uint8_t)variety;
        for (int pattern = 0; pattern < FISH_PATTERN_COUNT; pattern++) {
            memset(a, 0, TANK_W * TANK_H * sizeof *a); memset(b, 0, TANK_W * TANK_H * sizeof *b);
            portrait.pattern = (uint8_t)pattern; render_fish_portrait(a, TANK_W, 224, 180, 2, &portrait, 0);
            portrait.pattern = (uint8_t)((pattern + 1) % FISH_PATTERN_COUNT); render_fish_portrait(b, TANK_W, 224, 180, 2, &portrait, 0);
            assert(memcmp(a, b, TANK_W * TANK_H * sizeof *a));
        }
        tank_t parents = edits; parents.n_fish = 2;
        parents.fish[0].variety = parents.fish[1].variety = (uint8_t)variety;
        int offspring = tank_add_fish(&parents, 0, 1);
        assert(offspring == 2 && parents.fish[offspring].variety == variety);
        tank_remove_fish(&parents, 0);
        assert(parents.fish[1].variety == variety);
    }
    portrait.variety = FISH_JELLYFISH; portrait.pattern = PATTERN_PLAIN;
    memset(a, 0, TANK_W * TANK_H * sizeof *a); memset(b, 0, TANK_W * TANK_H * sizeof *b);
    render_fish_portrait(a, TANK_W, 224, 180, 2, &portrait, 0);
    render_fish_portrait(b, TANK_W, 224, 180, 2, &portrait, 0.7f);
    assert(memcmp(a, b, TANK_W * TANK_H * sizeof *a));
    uint16_t *guarded = malloc((TANK_W * TANK_H + 2) * sizeof *guarded); assert(guarded);
    guarded[0] = 0xabcd; guarded[TANK_W * TANK_H + 1] = 0xdcba;
    tank_t creatures = edits; creatures.n_fish = 1;
    for (int style = 0; style < FISH_VARIETY_COUNT; style++)
        for (int stage = STAGE_FRY; stage <= STAGE_ELDER; stage++) for (int corner = 0; corner < 4; corner++) {
            fish_t *f = &creatures.fish[0]; f->variety = (uint8_t)style; f->stage = (uint8_t)stage;
            f->x = corner % 2 ? TANK_W - 2 : 2; f->y = corner / 2 ? TANK_H - 2 : 2;
            f->heading = corner * 1.57f; tank_fish_face(f);
            render_tank(&creatures, guarded + 1, TANK_W);
            assert(guarded[0] == 0xabcd && guarded[TANK_W * TANK_H + 1] == 0xdcba);
        }
    free(guarded);
    edits.fish[0].variety = edits.fish[1].variety = FISH_ANGEL;
    edits.fish[0].pattern = edits.fish[1].pattern = PATTERN_SPOTS;
    int baby = tank_add_fish(&edits, 0, 1); assert(baby >= 0);
    assert(edits.fish[baby].variety == FISH_ANGEL && edits.fish[baby].pattern == PATTERN_SPOTS);
    progression_newborn_done(&edits);
    progression_fry_open(&edits); edits.sd_balance = SD_PRICE_FRY;
    fry_offer_t offer; progression_fry_offer(&edits, 0, &offer);
    assert(offer.variety < FISH_VARIETY_COUNT && offer.pattern < FISH_PATTERN_COUNT);
    assert(progression_buy_fry(&edits, 0));
    assert(edits.fish[edits.n_fish - 1].variety == offer.variety && edits.fish[edits.n_fish - 1].pattern == offer.pattern);
    for (int item = 16; item < SD_ITEM_COUNT; item++) {
        memset(a, 0, TANK_W * TANK_H * sizeof *a);
        render_ornament_preview(a, TANK_W, item, 220, 180);
        int painted = 0;
        for (int px = 0; px < TANK_W * TANK_H; px++) painted += a[px] != 0;
        assert(painted > 40);
    }
    /* Schema 2 and its 1952-byte binary layout remain readable. */
    root = cJSON_Parse(text); replace_number(root, "schema", 2);
    obj = cJSON_GetObjectItemCaseSensitive(root, "tank");
    remove_journal_fields(obj);
    const char *expansion[] = {"extra_x", "extra_z1", "extra_rank", "fish_variety", "fish_pattern"};
    for (size_t i = 0; i < sizeof expansion / sizeof expansion[0]; i++) cJSON_DeleteItemFromObjectCaseSensitive(obj, expansion[i]);
    replace_number(obj, "sd_unlocks", tank.sd_unlocks & 65535u);
    char *schema2 = cJSON_PrintUnformatted(root); assert(schema2); cJSON_Delete(root);
    assert(progression_restore_json(schema2, strlen(schema2), true, error, sizeof error)); free(schema2);
    saved_size = 1952; tank_init(&restored, 222); boot_enlarged(&restored);
    assert(restored.sd_balance == 3 && restored.fish[0].variety == 0 && restored.fish[0].pattern == 0);
    for (int i = 4; i < 10; i++) assert(restored.ornament_x[i] == 0);
    /* Schema 1 backups and the original 1920-byte chest saves remain readable. */
    root = cJSON_Parse(text); replace_number(root, "schema", 1);
    obj = cJSON_GetObjectItemCaseSensitive(root, "tank");
    remove_journal_fields(obj);
    const char *added[] = {"ornament_x", "ornament_z1", "ornament_rank", "sand_style", "water_style", "extra_x", "extra_z1", "extra_rank", "fish_variety", "fish_pattern"};
    for (size_t i = 0; i < sizeof added / sizeof added[0]; i++) cJSON_DeleteItemFromObjectCaseSensitive(obj, added[i]);
    replace_number(obj, "sd_unlocks", SD_ITEM_CHEST);
    char *old_json = cJSON_PrintUnformatted(root); assert(old_json); cJSON_Delete(root);
    assert(progression_restore_json(old_json, strlen(old_json), true, error, sizeof error));
    saved_size = 1920;
    tank_init(&restored, 321); boot_enlarged(&restored);
    assert(restored.sd_unlocks == SD_ITEM_CHEST && restored.sand_style == 0 && restored.water_style == 0);
    for (int i = 0; i < 4; i++) assert(restored.ornament_x[i] == 0 && restored.decor_rank[i + 6] == 0);
    settings_section(&restored, 4);
    render_settings_touch(&restored, render_frame_x(120), render_frame_y(SET_ROW4_Y), true, &value);
    assert(render_settings_touch(&restored, render_frame_x(120), render_frame_y(SET_ROW4_Y), false, &value) == SET_TAP_NONE);
    assert(restored.sand_style == 0);
    render_set_scene_cache(NULL); render_set_dirty_mask(NULL); free(scene); free(dirty);
    render_set_decor_scratch(NULL); free(scratch); free(a); free(b); free(text); free(old_json);
    update_manifest_t manifest; char app_url[NET_URL_MAX + 1];
    assert(!update_parse_manifest("{\"release\":\"0.9.9\",\"app\":{\"url\":\"https://example.invalid/app.bin\"}}",
                                 &manifest, app_url, sizeof app_url));
    assert(!update_parse_manifest("{\"fork\":\"upstream\",\"release\":\"0.9.9\",\"app\":{\"url\":\"https://example.invalid/app.bin\"}}",
                                 &manifest, app_url, sizeof app_url));
    assert(update_parse_manifest("{\"fork\":\"pocket-tank-cyd\",\"board\":\"cyd2432s028\",\"release\":\"0.9.9\","
                                 "\"app\":{\"url\":\"https://example.invalid/app.bin\",\"size\":1234}}",
                                 &manifest, app_url, sizeof app_url));
    assert(!strcmp(manifest.board, PT_BOARD) && manifest.app_size == 1234);
    assert(manifest.release_num == ((9u << 8) | 9u));
    assert(!strcmp(app_url, "https://example.invalid/app.bin"));
    int update_writes = writes;
    net_port_creds_forget();
    update_begin();
    for (int i = 0; i < 10; i++) { net_sim_advance(0.2f); update_tick(0.2f); }
    assert(update_page() == UPD_PG_SCAN);
    net_ap_t networks[NET_SCAN_MAX];
    assert(net_port_scan_results(networks, NET_SCAN_MAX) > 0);
    assert(net_port_creds_set("Fishbowl", "test-only"));
    net_port_off();
    update_begin();
    for (int i = 0; i < 30; i++) { net_sim_advance(0.2f); update_tick(0.2f); }
    assert(update_page() == UPD_PG_MESSAGE);
    assert(!strcmp(update_message_title(), "NO UPDATE CHANNEL"));
    assert(net_port_fail_reason() == NET_ERR_CHANNEL);
    assert(writes == update_writes);
    net_port_creds_forget(); net_port_off();
    tank_t colors = restored;
    colors.fish[0].color = FISH_BODY_COLORS[0]; colors.fish[0].accent = 0x123456;
    for (int color = 0; color < FISH_COLOR_N; color++) {
        setup_begin_customize(&colors, 0);
        setup_activate(&colors, SETUP_HIT_CUSTOM0 + 2); setup_activate(&colors, SETUP_HIT_NEXT);
        assert(colors.fish[0].color == FISH_BODY_COLORS[(color + 1) % FISH_COLOR_N]);
        assert(colors.fish[0].fin == tank_fin_for(colors.fish[0].color));
        setup_take_renamed();
    }
    colors.fish[0].color = 0x123456; colors.fish[0].accent = FISH_MARKING_COLORS[0];
    for (int color = 0; color < FISH_COLOR_N; color++) {
        setup_begin_customize(&colors, 0);
        setup_activate(&colors, SETUP_HIT_CUSTOM0 + 3); setup_activate(&colors, SETUP_HIT_NEXT);
        assert(colors.fish[0].accent == FISH_MARKING_COLORS[(color + 1) % FISH_COLOR_N]);
        setup_take_renamed();
    }
    uint32_t before_color = colors.fish[0].color; int before_fish = colors.n_fish;
    reject_erase = true;
    assert(!progression_reset(&colors, 9876));
    assert(colors.n_fish == before_fish && colors.fish[0].color == before_color);
    reject_erase = false;
    assert(progression_reset(&colors, 9876));
    assert(progression_setup_pending() && colors.n_fish == 2 && colors.sd_balance == 0 && colors.sd_unlocks == 0);
    assert(colors.sand_style == 0 && colors.water_style == 0 && !colors.autofeed_off);
    for (int i = 0; i < colors.n_fish; i++)
        assert(colors.fish[i].stage == STAGE_FRY && colors.fish[i].variety == FISH_CLASSIC && colors.fish[i].pattern == PATTERN_STRIPES);
    tank_init(&restored, 1); boot_enlarged(&restored);
    assert(progression_setup_pending() && restored.n_fish == 2 && restored.sd_unlocks == 0 && restored.sd_balance == 0);
    reject_write = true;
    assert(progression_reset(&restored, 9876) && !progression_mode_chosen(&restored));
    assert(strstr(progression_hard_status(&restored), "PROFILE WIPED"));
    reject_write = false;
    assert(progression_save(&restored));
    test_journal();
    test_home_navigation();
    test_simple_scene();
    test_name_wheel();
    test_normal_default();
    test_mode_care_notices();
    test_companion();
    test_adventure_traveler();
    test_game_rewards();
    test_daily_care();
    test_variety_unlocks();
    test_readable_text();
    test_bank();
    test_aquariums();
    test_shop_expansion();
    { extern void test_decor_scenery(void); test_decor_scenery(); }
    test_clock();
    { extern void test_clock_weather(void); test_clock_weather(); }
    test_fish_card_next();
    test_fish_sand_clearance();
    test_wearables();
    test_cosmetic_colors();
    test_compact();
    test_theme_edges();
    assert(word_guard[0] == 0xa5a5a5a5u && word_guard[word_count + 1] == 0xa5a5a5a5u);
    render_set_mask_scratch(NULL); free(word_guard);
    puts("PASS: supplied word-only renderer masks/palette stay within guarded allocation in cached and live rendering");
    puts("PASS: shop, 10 creature looks/24 colors, pairwise silhouettes, animation, edge guards, wipe, breeding and JSON/legacy saves");
    return 0;
}
