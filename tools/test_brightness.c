#include "brightness.h"
#include "ambient.h"
#include "nvs.h"
#undef NDEBUG
#include <assert.h>
#include <stdio.h>
#include <string.h>

static int stored = 100, staged, fail, writes, commits, applied, applications;
const char *esp_err_to_name(esp_err_t error) { (void)error; return "test failure"; }
esp_err_t nvs_open(const char *name, int mode, nvs_handle_t *handle) {
    assert(!strcmp(name, "tank")); (void)mode; *handle = 1;
    return fail == 1 ? -1 : ESP_OK;
}
esp_err_t nvs_get_u8(nvs_handle_t handle, const char *key, uint8_t *value) {
    assert(handle == 1 && !strcmp(key, "bright")); *value = (uint8_t)stored; return ESP_OK;
}
esp_err_t nvs_set_u8(nvs_handle_t handle, const char *key, uint8_t value) {
    assert(handle == 1 && !strcmp(key, "bright")); ++writes; staged = value;
    return fail == 2 ? -1 : ESP_OK;
}
esp_err_t nvs_commit(nvs_handle_t handle) {
    assert(handle == 1); ++commits;
    if (fail == 3) return -1;
    stored = staged; return ESP_OK;
}
void nvs_close(nvs_handle_t handle) { assert(handle == 1); }
void display_port_set_brightness(uint8_t level) { applied = level; applications++; }
/* Isolate brightness policy; ambient filtering has its own real-code harness. */
int ambient_brightness_pct(int requested) { return requested; }

int main(void) {
    brightness_init(); assert(brightness_level() == 100);
    for (int pct = 10; pct <= 100; pct++) {
        int before = writes;
        assert(brightness_preview_level(pct));
        assert(writes == before && brightness_level() == pct);
        brightness_apply(false); assert(applied == pct * 255 / 100);
        int old_applications = applications;
        brightness_apply(false); assert(applications == old_applications);
        brightness_apply(true); assert(applied == (pct * 255 / 100) * 6 / 10);
        assert(brightness_set_level(pct) && stored == pct && brightness_level() == pct);
        before = writes;
        assert(brightness_set_level(pct) && writes == before);
    }
    assert(!brightness_set_level(9) && !brightness_set_level(101));
    assert(!brightness_preview_level(-1) && !brightness_preview_level(255));
    assert(brightness_level() == 100);
    for (fail = 1; fail <= 3; fail++) {
        assert(brightness_preview_level(47) && brightness_level() == 47);
        assert(!brightness_set_level(47) && brightness_level() == 100 && stored == 100);
        brightness_apply(true); assert(applied == 153);
    }
    fail = 0;
    assert(brightness_set_level(47) && stored == 47);
    brightness_init(); assert(brightness_level() == 47);
    assert(brightness_save() && stored == 47);
    stored = 9; brightness_init(); assert(brightness_level() == 100);
    stored = 101; brightness_init(); assert(brightness_level() == 100);
    assert(commits > 90);
    puts("PASS: all 91 brightness percentages, live/no-write previews, persistence, invalid values, NVS open/set/commit rollback, night dim and unchanged target suppression");
    return 0;
}
