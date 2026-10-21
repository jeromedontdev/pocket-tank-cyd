#include "progression.h"
#include "advisor.h"
#include "aquarium_store.h"
#include "render.h"
#include "setup.h"
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
int64_t clock_port_now_unix(void) { return 2000000000; }
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
void net_sim_advance(float dt);
static void invalid(const char *text, size_t len) {
    int before = writes;
    assert(!progression_restore_json(text, len, true, error, sizeof error));
    assert(error[0] && writes == before);
}
static void invalid_object(cJSON *root) {
    char *text = cJSON_PrintUnformatted(root); assert(text);
    invalid(text, strlen(text)); free(text); cJSON_Delete(root);
}
static void replace_number(cJSON *object, const char *key, double value) {
    assert(cJSON_ReplaceItemInObjectCaseSensitive(object, key, cJSON_CreateNumber(value)));
    if (!strcmp(key, "schema") && value < 5) cJSON_DeleteItemFromObjectCaseSensitive(object, "bank");
}
static void remove_journal_fields(cJSON *obj) {
    const char *fields[] = {"journal_xp", "journal_claimed", "journal_seen", "journal_grown", "journal_baseline",
                            "journal_initialized", "journal_trust", "journal_population", "journal_theme"};
    for (size_t i = 0; i < sizeof fields / sizeof fields[0]; i++) cJSON_DeleteItemFromObjectCaseSensitive(obj, fields[i]);
}
static int add_bank_candidate(tank_t *t, int number) {
    int slot = tank_add_fish(t, 0, 1); assert(slot >= 0);
    char name[8]; snprintf(name, sizeof name, "fish%02d", number);
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
    tank_t t, loaded; saved_size = 0; tank_init(&t, 17); progression_boot(&t);
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
    tank_init(&loaded, 5); progression_boot(&loaded);
    assert(loaded.bank_count == FISH_BANK_CAP && !memcmp(loaded.bank, t.bank, sizeof t.bank));
    char *json = progression_backup_json(&t); assert(json && strlen(json) < 32768);
    assert(progression_restore_json(json, strlen(json), false, error, sizeof error));
    assert(progression_restore_json(json, strlen(json), true, error, sizeof error));
    tank_init(&loaded, 3); progression_boot(&loaded);
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
    saved_size = 2040; tank_init(&loaded, 3); progression_boot(&loaded);
    assert(!loaded.bank_count && loaded.journal.xp == t.journal.xp && loaded.fish[1].variety == t.fish[1].variety);
    free(old); free(json);
    /* Actual UI paths: store/cancel/confirm, choose/swap at the two-fish minimum, then return. */
    tank_init(&t, 3); saved_size = 0; progression_boot(&t); progression_setup_done(&t); add_bank_candidate(&t, 9);
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
    tank_init(&t, 42); progression_boot(&t);
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
    assert(progression_setup_pending() && t.sd_balance == 250 && t.sd_earned == 1000 && !t.sd_unlocks && !t.bank_count && !t.journal.xp);
    assert(!strcmp(t.aquariums.name, "TANK 2"));
    assert(progression_age_s(&t, 0) == 0);
    assert(progression_aquarium_move(&t, 0));
    progression_setup_done(&t); tank_set_name(&t, 0, "second");
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
    assert(!strcmp(t.fish[0].name, "keeper") && progression_age_s(&t, 0) == age);
    assert(t.journal.xp == original.journal.xp && t.bank_count == 1 && !memcmp(t.bank, original.bank, sizeof t.bank));
    assert(!progression_aquarium_move(&t, 1) && !strcmp(t.fish[0].name, "second"));
    assert(!strcmp(t.aquariums.name, "SHARK BAY") && t.sd_balance == before.sd_balance);
    tank_init(&t, 41); progression_boot(&t);
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
    tank_init(&t, 41); progression_boot(&t);
    assert(!memcmp(&t.aquariums, &owned, sizeof owned)); /* JSON restores do not clone collection identity. */
    t.sd_balance = 123; t.sd_earned = 456;
    assert(progression_reset(&t, 3) && !memcmp(&t.aquariums, &owned, sizeof owned) && !t.bank_count);
    assert(t.sd_balance == 123 && t.sd_earned == 456);
    progression_setup_done(&t);
    assert(!progression_aquarium_move(&t, 1) && t.aquariums.count == AQUARIUM_CAP);
    /* A pre-aquarium binary save migrates to one original tank. */
    assert(progression_save(&t)); saved_size = 3968;
    tank_init(&t, 1); progression_boot(&t);
    assert(t.aquariums.count == 1 && t.aquariums.active == 0);
    /* The page confirms a purchase and cancels without writes. */
    saved_size = 0; tank_init(&t, 7); progression_boot(&t); progression_setup_done(&t); t.sd_balance = 500;
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
    saved_size = 0; tank_init(&t, 17); progression_boot(&t); progression_setup_done(&t);
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
        if (item <= 29) {
            before = t; reject_write = true;
            assert(!progression_sell(&t, item) && !memcmp(&before, &t, sizeof t)); reject_write = false;
            assert(progression_sell(&t, item) && !(t.sd_unlocks & SD_ITEMS[item].bit));
            assert(t.sd_balance == SD_ITEMS[item].price / 5);
            t.sd_balance = SD_ITEMS[item].price;
            assert(progression_buy(&t, item) && t.sd_balance == 0);
        } else assert(!progression_sell(&t, item));
        total += SD_ITEMS[item].price;
    }
    assert(total == 1810);
    tank_init(&loaded, 1); progression_boot(&loaded);
    assert(loaded.sd_unlocks == t.sd_unlocks && (loaded.sd_unlocks & SD_ITEM_GARDEN));
    assert(loaded.aquariums.stat_upgrades == STAT_MASK);
    char *json = progression_backup_json(&loaded); assert(json);
    assert(progression_restore_json(json, strlen(json), true, error, sizeof error)); free(json);
    tank_init(&loaded, 1); progression_boot(&loaded);
    assert(loaded.sd_unlocks == t.sd_unlocks && saved_size == AQUARIUM_SAVE_BYTES);
    assert(loaded.aquariums.stat_upgrades == STAT_MASK);
    loaded.sd_balance = AQUARIUM_PRICE;
    assert(!progression_aquarium_move(&loaded, 1) && !loaded.sd_unlocks);
    assert(!loaded.aquariums.stat_upgrades);
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
    json = progression_backup_json(&loaded); assert(json);
    cJSON *root = cJSON_Parse(json); free(json); assert(root);
    cJSON *tank = cJSON_GetObjectItemCaseSensitive(root, "tank");
    cJSON_SetNumberValue(cJSON_GetObjectItemCaseSensitive(tank, "stat_upgrades"), 16);
    json = cJSON_PrintUnformatted(root); assert(json);
    assert(!progression_restore_json(json, strlen(json), true, error, sizeof error)); free(json);
    cJSON_DeleteItemFromObjectCaseSensitive(tank, "stat_upgrades");
    json = cJSON_PrintUnformatted(root); cJSON_Delete(root); assert(json);
    assert(progression_restore_json(json, strlen(json), true, error, sizeof error)); free(json);
    tank_init(&loaded, 1); progression_boot(&loaded);
    assert(!loaded.aquariums.stat_upgrades);
    loaded.aquariums.stat_upgrades = STAT_MASK; assert(progression_reset(&loaded, 123));
    assert(!loaded.aquariums.stat_upgrades);
    puts("PASS: 36 sorted shop IDs, atomic purchases, per-tank stat upgrades, binary/JSON/legacy persistence, exact 25% recovery/trust, seahorse attribution, scenery and upkeep");
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
    tank_t t, reloaded; saved_size = 0; tank_init(&t, 7); progression_boot(&t);
    assert(!t.journal.xp && !t.journal.initialized);
    t.player_feedings = 20; t.algae_colonies = 30; t.trim_px = 200 * PX_PER_CM; t.hold_approaches = 5;
    t.fish[0].variety = FISH_SHARK; t.fish[0].stage = STAGE_ADULT; t.fish[0].trust = 6.5f;
    journal_tick(&t);
    assert(t.journal.xp == 0 && t.journal.initialized);
    assert((t.journal.seen & (1u << FISH_SHARK)) && (t.journal.grown & (1u << FISH_SHARK)));
    t.player_feedings++; t.algae_colonies++; t.trim_px += 10 * PX_PER_CM; t.hold_approaches++;
    journal_tick(&t); assert(t.journal.xp == 22);
    journal_tick(&t); assert(t.journal.xp == 22);
    tank_feed(&t, TANK_W / 2, 1); journal_tick(&t); assert(t.journal.xp == 22); /* Not eaten yet. */
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
    tank_init(&reloaded, 8); progression_boot(&reloaded);
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
    saved_size = 2000; tank_init(&reloaded, 8); progression_boot(&reloaded);
    assert(!reloaded.journal.xp && !reloaded.journal.seen && !reloaded.journal.initialized);
    assert(reloaded.fish[0].variety == FISH_SHARK && reloaded.sd_unlocks == t.sd_unlocks);
    journal_tick(&reloaded); assert(!reloaded.journal.xp && (reloaded.journal.seen & (1u << FISH_SHARK)));
    free(json); free(old_json);
    size_t pixels = TANK_W * TANK_H;
    uint16_t *frames = calloc(4 * pixels + 2, sizeof *frames), *cache = calloc(pixels, sizeof *cache);
    uint32_t *dirty = calloc(RENDER_DIRTY_WORDS, sizeof *dirty); assert(frames && cache && dirty);
    frames[0] = frames[4 * pixels + 1] = 0xbeef;
    t.n_fish = 0; t.journal.xp = journal_level_xp(15); t.sd_unlocks = 0;
    for (int cached = 0; cached < 2; cached++) {
        render_set_scene_cache(cached ? cache : NULL); render_set_dirty_mask(cached ? dirty : NULL);
        for (int theme = 0; theme < 4; theme++) {
            t.journal.theme = (uint8_t)theme;
            render_tank(&t, frames + 1 + theme * pixels, TANK_W);
            for (int previous = 0; previous < theme; previous++)
                assert(memcmp(frames + 1 + theme * pixels, frames + 1 + previous * pixels, pixels * 2));
        }
    }
    render_journal_open(); render_journal(&t, frames + 1, TANK_W);
    for (int tab = 0; tab < 3; tab++) {
        assert(!render_journal_tap(&t, render_frame_x(60 + tab * 136), render_frame_y(JOURNAL_TAB_Y + 10)));
        render_journal(&t, frames + 1 + tab * pixels, TANK_W);
    }
    assert(memcmp(frames + 1, frames + 1 + pixels, pixels * 2));
    assert(memcmp(frames + 1 + pixels, frames + 1 + 2 * pixels, pixels * 2));
    assert(frames[0] == 0xbeef && frames[4 * pixels + 1] == 0xbeef);
    assert(render_journal_tap(&t, render_frame_x(SET_CLOSE_X + 4), render_frame_y(SET_CLOSE_Y + 4)) == 1);
    render_set_scene_cache(NULL); render_set_dirty_mask(NULL);
    free(frames); free(cache); free(dirty);
    t.journal.xp = JOURNAL_XP_MAX - 1; t.player_feedings++;
    journal_tick(&t); assert(t.journal.xp == JOURNAL_XP_MAX);
    t.journal.claimed = 0; t.sd_balance = INT_MAX;
    int previous_writes = writes;
    assert(!journal_claim(&t, 0) && !t.journal.claimed && writes == previous_writes);
    assert(progression_reset(&t, 9) && !t.journal.claimed && !t.journal.xp && !t.journal.theme && !t.journal.seen);
    puts("PASS: Journal XP thresholds/deltas, 15 rewards, failure rollback, album, schema 3/2000 migration, themes and UI");
}
int main(int argc, char **argv) {
    tank_t tank; memset(&tank, 0xa5, sizeof tank); tank_init(&tank, 1234); progression_boot(&tank);
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
    tank_t restored; tank_init(&restored, 4321); progression_boot(&restored);
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
    restored.sd_balance = 119; assert(!progression_buy(&restored, 9));
    restored.sd_balance = 120; assert(progression_buy(&restored, 9) && restored.sd_balance == 0);
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
    render_settings(&tank, fb, TANK_W, 100);
    int value = 0;
    float bx = render_frame_x(100), rx = render_frame_x(300), y = render_frame_y(115);
    assert(render_settings_tap(bx, y, &value) == SET_TAP_BACKUP);
    assert(render_settings_tap(rx, y, &value) == SET_TAP_RESTORE);
    assert(render_settings_tap(render_frame_x(300), render_frame_y(SET_ROW5_Y), &value) == SET_TAP_WATER);
    for (int px = SET_LABEL_X + 2; px < SET_LABEL_X + SET_COLOR_W; px += 20) {
        assert(render_settings_tap(render_frame_x(px), render_frame_y(SET_ROW4_Y), &value) == SET_TAP_SAND);
        assert(render_settings_tap(render_frame_x(px), render_frame_y(SET_ROW5_Y), &value) == SET_TAP_WATER);
    }
    assert(render_settings_tap(render_frame_x(392), render_frame_y(19), &value) == SET_TAP_CLOSE);
    assert(render_settings_tap(render_frame_x(88), render_frame_y(SET_FOOT_Y + 15), &value) == SET_TAP_UPDATES);
    assert(render_settings_tap(render_frame_x(360), render_frame_y(SET_FOOT_Y + 15), &value) == SET_TAP_TOUCH);
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
    render_settings_touch(&tank, cx, cy, true, &value);
    assert(render_settings_touch(&tank, cx, cy, false, &value) == SET_TAP_CLOSE);
    cx = render_frame_x(88); cy = render_frame_y(SET_FOOT_Y + 15);
    render_settings_touch(&tank, cx, cy, true, &value);
    assert(render_settings_touch(&tank, cx, cy, false, &value) == SET_TAP_UPDATES);
    cx = render_frame_x(360);
    render_settings_touch(&tank, cx, cy, true, &value);
    assert(render_settings_touch(&tank, cx, cy, false, &value) == SET_TAP_TOUCH);
    cx = render_frame_x(SET_WIPE_X + SET_WIPE_W / 2); cy = render_frame_y(SET_WIPE_Y + SET_WIPE_H / 2);
    int wipe_writes = writes;
    render_settings_touch(&tank, cx, cy, true, &value);
    assert(writes == wipe_writes);
    assert(render_settings_touch(&tank, cx, cy, false, &value) == SET_TAP_WIPE);
    assert(writes == wipe_writes);
    render_settings_touch(&tank, cx, cy, true, &value);
    assert(render_settings_touch(&tank, render_frame_x(224), cy, false, &value) == SET_TAP_NONE);
    render_confirm_reset(fb, TANK_W, 1);
    assert(render_confirm_hit(render_frame_x(RENDER_CONFIRM_NO_X + 30), render_frame_y(RENDER_CONFIRM_BTN_Y + 25)) == -1);
    assert(render_confirm_hit(render_frame_x(RENDER_CONFIRM_YES_X + 30), render_frame_y(RENDER_CONFIRM_BTN_Y + 25)) == 1);
    assert(render_confirm_hit(cx, cy) == 0);
    cx = render_frame_x(SET_SEG_X + SET_SEG_DX + 20); cy = render_frame_y(SET_ROW3_Y);
    render_settings_touch(&tank, cx, cy, true, &value);
    assert(render_settings_touch(&tank, cx, cy, false, &value) == SET_TAP_FEED && value == 0 && tank.autofeed_off);
    cx = render_frame_x(SET_SEG_X + 20);
    render_settings_touch(&tank, cx, cy, true, &value);
    assert(render_settings_touch(&tank, cx, cy, false, &value) == SET_TAP_FEED && value == 1 && !tank.autofeed_off);
    tank.orient_lock = true; tank.orient_inv = true;
    assert(!tank_orient(&tank, true));
    tank.orient_lock = false;
    assert(!tank_orient(&tank, true));
    render_settings_touch(&tank, rx, y, true, &value);
    assert(render_settings_touch(&tank, rx, y, false, &value) == SET_TAP_RESTORE && value == 0);
    render_backup_status("confirm", true);
    render_settings_touch(&tank, rx, y, true, &value);
    assert(render_settings_touch(&tank, rx, y, false, &value) == SET_TAP_RESTORE && value == 1);
    tank.clock += 11;
    render_settings_touch(&tank, rx, y, true, &value);
    assert(render_settings_touch(&tank, rx, y, false, &value) == SET_TAP_RESTORE && value == 0);
    render_set_scene_cache(NULL); render_set_dirty_mask(NULL); render_set_decor_scratch(NULL);
    free(scene); free(dirty); free(scratch); free(fb); free(text);
    tank_init(&tank, 999); saved_size = 0; progression_boot(&tank);
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
    tank_init(&restored, 234); progression_boot(&restored);
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
        tank_init(&restored, 222); progression_boot(&restored);
        assert(restored.water_style == style);
        free(color_json);
    }
    for (int style = SAND_STYLE_RAINBOW; style < SAND_STYLE_COUNT; style++) {
        tank.sand_style = (uint8_t)style;
        char *color_json = progression_backup_json(&tank); assert(color_json);
        assert(progression_restore_json(color_json, strlen(color_json), true, error, sizeof error));
        tank_init(&restored, 222); progression_boot(&restored);
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
    for (int i = 0; i < WATER_STYLE_COUNT; i++) {
        float sx = render_frame_x(300), sy = render_frame_y(SET_ROW5_Y);
        render_settings_touch(&tank, sx, sy, true, &value);
        assert(render_settings_touch(&tank, sx, sy, false, &value) == SET_TAP_WATER);
        assert(value == (i + 1) % WATER_STYLE_COUNT);
    }
    for (int i = 0; i < SAND_STYLE_COUNT; i++) {
        float sx = render_frame_x(120), sy = render_frame_y(SET_ROW4_Y);
        render_settings_touch(&tank, sx, sy, true, &value);
        assert(render_settings_touch(&tank, sx, sy, false, &value) == SET_TAP_SAND);
        assert(value == (i + 1) % SAND_STYLE_COUNT);
    }
    for (int item = 10; item < SD_ITEM_COUNT; item++) {
        tank_t shop = tank; shop.sd_unlocks = 0; shop.sd_balance = SD_ITEMS[item].price;
        int row_index = 0;
        while (progression_shop_item(&shop, row_index) != item) row_index++;
        row_index += progression_fry_for_sale(&shop);
        render_shop_leave();
        for (int page = 0; page < row_index / SHP_PER_PAGE; page++)
            render_shop_tap(&shop, render_frame_x(SHP_ARROW_X1 + 12), render_frame_y(SHP_ARROW_Y + 8));
        render_shop_tap(&shop, render_frame_x(150), render_frame_y(SHP_ROW_Y0 + row_index % SHP_PER_PAGE * SHP_ROW_DY + 16));
        render_shop(&shop, a, TANK_W);
        assert(render_shop_tap(&shop, render_frame_x(224), render_frame_y(260)) == SHOP_TAP_BUY + item);
        if (item >= 22 && item <= 29) {
            shop.sd_unlocks = SD_ITEMS[item].bit;
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
    tank_init(&restored, 222); progression_boot(&restored);
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
    tank_init(&restored, 222); progression_boot(&restored);
    assert(restored.fish[0].variety == FISH_PUFFER && restored.fish[0].pattern == PATTERN_BAND);
    char *custom_json = progression_backup_json(&edits); assert(custom_json);
    assert(progression_restore_json(custom_json, strlen(custom_json), true, error, sizeof error));
    tank_init(&restored, 222); progression_boot(&restored);
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
        tank_init(&restored, 222); progression_boot(&restored);
        assert(restored.fish[0].variety == style);
        char *species_json = progression_backup_json(&edits); assert(species_json);
        assert(progression_restore_json(species_json, strlen(species_json), true, error, sizeof error)); free(species_json);
        tank_init(&restored, 222); progression_boot(&restored);
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
    fry_offer_t offer; progression_fry_offer(0, &offer);
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
    saved_size = 1952; tank_init(&restored, 222); progression_boot(&restored);
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
    tank_init(&restored, 321); progression_boot(&restored);
    assert(restored.sd_unlocks == SD_ITEM_CHEST && restored.sand_style == 0 && restored.water_style == 0);
    for (int i = 0; i < 4; i++) assert(restored.ornament_x[i] == 0 && restored.decor_rank[i + 6] == 0);
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
    tank_init(&restored, 1); progression_boot(&restored);
    assert(progression_setup_pending() && restored.n_fish == 2 && restored.sd_unlocks == 0 && restored.sd_balance == 0);
    reject_write = true; assert(!progression_reset(&restored, 9876)); reject_write = false;
    assert(progression_save(&restored));
    test_journal();
    test_readable_text();
    test_bank();
    test_aquariums();
    test_shop_expansion();
    puts("PASS: shop, 10 creature looks/24 colors, pairwise silhouettes, animation, edge guards, wipe, breeding and JSON/legacy saves");
    return 0;
}
