#include "board_hardware.h"
#include "esp_log.h"
#include "nvs.h"
#if PT_BOARD_UNIFIED
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#endif

static const char *TAG = "hardware";
static const board_hardware_t *s_hardware;

bool board_hardware_save(unsigned id) {
    const board_hardware_t *profile = board_hardware_lookup(id);
    if (!profile
#if !PT_BOARD_UNIFIED
        || profile->id != board_hardware_build_default()
#endif
        ) {
        ESP_LOGE(TAG, "hardware profile %u incompatible with this image", id);
        return false;
    }
    nvs_handle_t handle;
    esp_err_t error = nvs_open("pt_hardware", NVS_READWRITE, &handle);
    if (error == ESP_OK) {
        error = nvs_set_u8(handle, "profile", (uint8_t)profile->id);
        if (error == ESP_OK) error = nvs_commit(handle);
        nvs_close(handle);
    }
    if (error != ESP_OK) {
        ESP_LOGE(TAG, "hardware profile save failed: %s", esp_err_to_name(error));
        return false;
    }
    ESP_LOGI(TAG, "hardware profile saved: %s", profile->name);
    return true;
}

#if PT_BOARD_UNIFIED
static bool provision_profile(uint8_t *id) {
    char line[64];
    unsigned used = 0;
    bool overflow = false;
    ESP_LOGW(TAG, "profile required: send 'hardware cyd' or 'hardware elegoo' over USB serial");
    for (;;) {
        int c = getchar();
        if (c == EOF) {
            clearerr(stdin);
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }
        if (c == '\r') continue;
        if (c != '\n') {
            if (used + 1 < sizeof line) line[used++] = (char)c;
            else overflow = true;
            continue;
        }
        line[used] = '\0';
        const board_hardware_t *profile = NULL;
        if (!overflow && !strcmp(line, "hardware cyd"))
            profile = board_hardware_lookup(BOARD_HARDWARE_CYD);
        if (!overflow && !strcmp(line, "hardware elegoo"))
            profile = board_hardware_lookup(BOARD_HARDWARE_ELEGOO);
        used = 0;
        overflow = false;
        if (!profile) {
            ESP_LOGE(TAG, "invalid profile command; use 'hardware cyd' or 'hardware elegoo'");
            continue;
        }
        if (!board_hardware_save(profile->id)) continue;
        *id = (uint8_t)profile->id;
        return true;
    }
}
#endif

const board_hardware_t *board_hardware_current(void) {
    return s_hardware;
}

bool board_hardware_init(void) {
    s_hardware = NULL;
    nvs_handle_t handle;
    uint8_t id = (uint8_t)board_hardware_build_default();
    esp_err_t error = nvs_open("pt_hardware", NVS_READONLY, &handle);
    if (error == ESP_OK) {
        error = nvs_get_u8(handle, "profile", &id);
        nvs_close(handle);
    }
    if (error != ESP_OK && error != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGE(TAG, "cannot read hardware profile: %s", esp_err_to_name(error));
        return false;
    }
#if PT_BOARD_UNIFIED
    if (error == ESP_ERR_NVS_NOT_FOUND || !board_hardware_lookup(id)) {
        if (error == ESP_OK) ESP_LOGE(TAG, "unknown saved hardware profile %u; selection required", (unsigned)id);
        if (!provision_profile(&id)) return false;
        error = ESP_OK;
    }
#endif
    const board_hardware_t *profile = board_hardware_lookup(id);
    if (!profile
#if !PT_BOARD_UNIFIED
        || profile->id != board_hardware_build_default()
#endif
        ) {
        ESP_LOGE(TAG, "hardware profile %u incompatible with this board-specific image", (unsigned)id);
        return false;
    }
    s_hardware = profile;
    ESP_LOGI(TAG, "hardware profile %s%s", profile->name,
             error == ESP_ERR_NVS_NOT_FOUND ? " (image default)" : " (saved)");
    return true;
}
