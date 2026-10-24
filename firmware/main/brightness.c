#include "brightness.h"
#include "ambient.h"
#include "display_port.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"

static const char *TAG = "brightness";
static int s_pct = 100, s_saved = 100, s_applied = -1;

void brightness_init(void) {
    s_pct = s_saved = 100; s_applied = -1;
    nvs_handle_t h; uint8_t v;
    if (nvs_open("tank", NVS_READONLY, &h) != ESP_OK) return;
    if (nvs_get_u8(h, "bright", &v) == ESP_OK && v >= 10 && v <= 100) s_pct = s_saved = v;
    nvs_close(h);
}
bool brightness_save(void) {
    nvs_handle_t h;
    esp_err_t err = nvs_open("tank", NVS_READWRITE, &h);
    if (err == ESP_OK) {
        err = nvs_set_u8(h, "bright", (uint8_t)s_pct);
        if (err == ESP_OK) err = nvs_commit(h);
        nvs_close(h);
    }
    if (err != ESP_OK) { ESP_LOGE(TAG, "save failed: %s", esp_err_to_name(err)); return false; }
    s_saved = s_pct;
    return true;
}
int brightness_level(void) { return s_pct; }
bool brightness_preview_level(int pct) {
    if (pct < 10 || pct > 100) return false;
    s_pct = pct; s_applied = -1;
    return true;
}
bool brightness_set_level(int pct) {
    if (!brightness_preview_level(pct)) return false;
    if (pct != s_saved && !brightness_save()) {
        s_pct = s_saved; s_applied = -1; return false;
    }
    ESP_LOGI(TAG, "level %d%%", s_pct);
    return true;
}
void brightness_cycle(void) { brightness_set_level(s_pct == 100 ? 60 : s_pct == 60 ? 30 : 100); }
void brightness_apply(bool night) {
    int target = ambient_brightness_pct(s_pct) * 255 / 100;
    if (night) target = target * 6 / 10;
    if (target != s_applied) { display_port_set_brightness((uint8_t)target); s_applied = target; }
}
