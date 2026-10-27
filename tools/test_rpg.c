/* Reuse the native persistence ports and complete legacy-backup harness. */
#define main backup_tests_main
#include "test_tank_backup.c"
#undef main

/* This core suite deliberately has no navigation/rendering dependency. The
 * separate simulator integration exercises the real companion pages. */
void companion_open(void) {}
void companion_open_section(int section) { (void)section; }
void companion_open_creature(int kind) { (void)kind; }
void companion_render(const tank_t *t, uint16_t *fb, int stride) {
    (void)t; (void)fb; (void)stride;
}
void companion_render_decor(const tank_t *t, uint16_t *fb, int stride) {
    (void)t; (void)fb; (void)stride;
}
bool companion_tap(tank_t *t, float x, float y) {
    (void)t; (void)x; (void)y; return false;
}

static void rpg_reload(tank_t *t) {
    tank_init(t, 456);
    progression_boot(t);
}

int main(void) {
    tank_t t;
    memset(&t, 0xa5, sizeof t);
    tank_init(&t, 123);
    tank_rpg_t empty = {0};
    assert(!memcmp(&t.rpg, &empty, sizeof empty));
    memset(&t.rpg, 0xa5, sizeof t.rpg);
    companion_init(&t);
    assert(!memcmp(&t.rpg, &empty, sizeof empty));
    saved_size = 0;
    boot_enlarged(&t);
    assert(companion_rpg_valid(&t.rpg, t.polish.next_id));
    assert(!companion_rpg_choose(&t, -1, 0));
    assert(!companion_rpg_choose(&t, 0, 3));
    fish_t fish_before[N_FISH_MAX];
    memcpy(fish_before, t.fish, sizeof fish_before);
    uint32_t id = t.polish.active[0].id;
    assert(companion_rpg_choose(&t, 0, 0));
    assert(!memcmp(fish_before, t.fish, sizeof fish_before));
    assert(t.rpg.step == 1 && t.rpg.skills[0] == 1 && t.rpg.participant == id);
    assert(saved_size == AQUARIUM_SAVE_BYTES);
    rpg_reload(&t);
    assert(t.rpg.step == 1 && t.rpg.participant == id);
    tank_rpg_t before = t.rpg;
    tank_polish_t polish_before = t.polish;
    reject_write = true;
    assert(!companion_rpg_choose(&t, 1, 1));
    assert(!memcmp(&before, &t.rpg, sizeof before));
    assert(!memcmp(&polish_before, &t.polish, sizeof polish_before));
    reject_write = false;
    assert(companion_rpg_choose(&t, 1, 1));
    assert(companion_rpg_choose(&t, 0, 2));
    assert(t.rpg.chapter == 1 && !t.rpg.step && t.rpg.routes == 1);
    assert(t.rpg.contributors[0] == id && companion_rpg_contributor(&t, id));
    assert(t.polish.recap[0].kind == MEMORY_RPG);
    t.story.active[1].parent[0] = (uint16_t)id;
    assert(companion_rpg_legacy(&t, 1));
    tank_story_remember(&t, id, t.fish[0].name);
    assert(progression_save(&t));
    rpg_reload(&t);
    assert(companion_rpg_legacy(&t, 1));
    assert(companion_rpg_contributor(&t, id));
    assert(strstr(companion_rpg_route(&t, 0), "PROMISE"));
    assert(tank_add_fish(&t, -1, -1) >= 0);
    companion_polish_observe(&t);
    assert(tank_remove_fish(&t, 0));
    assert(companion_rpg_contributor(&t, id));
    assert(companion_rpg_legacy(&t, 0));
    assert(companion_story_name(&t, (uint16_t)id));
    assert(progression_save(&t));
    rpg_reload(&t);
    assert(companion_rpg_contributor(&t, id) && companion_rpg_legacy(&t, 0));
    char *json = progression_backup_json(&t);
    assert(json && strstr(json, "\"schema\":19") && strstr(json, "\"rpg\":"));
    assert(progression_restore_json(json, strlen(json), true, error, sizeof error));
    free(json);
    rpg_reload(&t);
    before = t.rpg; before.version = 2;
    assert(!companion_rpg_valid(&before, t.polish.next_id));
    before = t.rpg; before.skills[0]++;
    assert(!companion_rpg_valid(&before, t.polish.next_id));
    before = t.rpg; before.routes |= 32;
    assert(!companion_rpg_valid(&before, t.polish.next_id));
    before = t.rpg; before.contributors[5] = (uint16_t)id;
    assert(!companion_rpg_valid(&before, t.polish.next_id));
    for (int i = 3; i < 18; i++) {
        int chapter = t.rpg.chapter;
        const char *prompt = companion_rpg_prompt(&t);
        assert(strlen(prompt) <= 100);
        char choice[80];
        companion_rpg_choice_label(&t, i % 3, choice, sizeof choice);
        assert(strlen(choice) <= 34);
        if (t.rpg.step == 2) {
            before = t.rpg; polish_before = t.polish; reject_write = true;
            assert(!companion_rpg_choose(&t, i % t.n_fish, i % 3));
            assert(!memcmp(&before, &t.rpg, sizeof before));
            assert(!memcmp(&polish_before, &t.polish, sizeof polish_before));
            reject_write = false;
        }
        assert(companion_rpg_choose(&t, i % t.n_fish, i % 3));
        if (t.rpg.chapter != chapter) {
            before = t.rpg; polish_before = t.polish; reject_write = true;
            assert(!companion_rpg_choose(&t, 0, 0));
            assert(!memcmp(&before, &t.rpg, sizeof before));
            assert(!memcmp(&polish_before, &t.polish, sizeof polish_before));
            reject_write = false;
        }
        rpg_reload(&t);
        assert(companion_rpg_valid(&t.rpg, t.polish.next_id));
    }
    before = t.rpg;
    assert(t.rpg.chapter == 6);
    int balance = t.sd_balance;
    assert(!companion_rpg_choose(&t, 0, 0));
    assert(t.sd_balance == balance && !memcmp(&before, &t.rpg, sizeof before));
    json = progression_backup_json(&t);
    cJSON *root = cJSON_Parse(json); free(json); assert(root);
    cJSON_DeleteItemFromObjectCaseSensitive(root, "rpg");
    cJSON_DeleteItemFromObjectCaseSensitive(root, "creative");
    cJSON_DeleteItemFromObjectCaseSensitive(root, "profile");
    cJSON_DeleteItemFromObjectCaseSensitive(root, "care");
    cJSON_DeleteItemFromObjectCaseSensitive(root, "inventory");
    cJSON_DeleteItemFromObjectCaseSensitive(root, "mussel");
    cJSON_DeleteItemFromObjectCaseSensitive(root, "game_income");
    cJSON_DeleteItemFromObjectCaseSensitive(root, "games_extra");
    assert(cJSON_ReplaceItemInObjectCaseSensitive(root, "schema", cJSON_CreateNumber(13)));
    json = cJSON_PrintUnformatted(root); cJSON_Delete(root);
    assert(progression_restore_json(json, strlen(json), true, error, sizeof error));
    free(json); rpg_reload(&t);
    assert(!memcmp(&t.rpg, &empty, sizeof empty));
    assert(companion_rpg_choose(&t, 0, 0));
    /* Exact previous binary size must not look like a partial new tail. */
    saved_size = 5888;
    rpg_reload(&t);
    assert(!memcmp(&t.rpg, &empty, sizeof empty));
    for (int approach = 0; approach < 3; approach++) {
        saved_size = 0; tank_init(&t, 789); boot_enlarged(&t);
        for (int action = 0; action < 18; action++) {
            assert(companion_rpg_choose(&t, 0, approach));
        }
        assert(t.rpg.skills[approach] == 18);
        const char *ending = companion_rpg_route(&t, 5);
        assert(strstr(ending, approach == 0 ? "JOURNEYS" : approach == 1 ? "WONDERS" : "WELCOME"));
        assert(strlen(companion_rpg_prompt(&t)) <= 100);
        rpg_reload(&t);
        assert(!strcmp(ending, companion_rpg_route(&t, 5)));
    }
    saved_size = 0; tank_init(&t, 789); boot_enlarged(&t);
    t.story.active[0].traits = t.story.active[0].mutation = 0;
    assert(companion_rpg_choose(&t, 0, 0));
    assert(t.rpg.outcome == 4); /* untrained attempt still advances */
    saved_size = 0; tank_init(&t, 789); boot_enlarged(&t);
    t.story.active[0].traits = t.story.active[0].mutation = 0;
    t.companion.active[0].bond = 25;
    assert(companion_rpg_choose(&t, 0, 0));
    assert(t.rpg.outcome == 1); /* an actual bond changes the outcome */
    char context[80];
    companion_rpg_context(&t, 0, context, sizeof context);
    assert(!strcmp(context, "Your bond helps your guide."));
    t.companion.active[0].bond = 0;
    companion_rpg_context(&t, 0, context, sizeof context);
    assert(!strcmp(context, "Every approach is open. No hurry."));
    t.story.active[0].mutation = GENE_MUTANT_PATTERN;
    companion_rpg_context(&t, 0, context, sizeof context);
    assert(!strcmp(context, "Your fish's genes offer courage."));
    t.rpg.contributors[0] = (uint16_t)t.polish.active[1].id;
    t.rpg.chapter = 1;
    t.story.active[0].parent[0] = t.rpg.contributors[0];
    companion_rpg_context(&t, 0, context, sizeof context);
    assert(!strcmp(context, "Family legacy helps your guide."));
    puts("Lost Current native persistence and campaign tests passed");
    return 0;
}
