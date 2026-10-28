/* No resident weather task: HTTPS jobs run on the existing net worker (which
 * already owns the Wi-Fi lease and a TLS-sized stack), and a settings save
 * runs on a short-lived task. Requests/results are copied through a single
 * slot under a spinlock; no buffers or config pointers are shared. */
#include "weather.h"
#include "update.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { SLOT_IDLE, SLOT_PENDING, SLOT_RUNNING, SLOT_DONE };
static portMUX_TYPE slot_lock = portMUX_INITIALIZER_UNLOCKED;
static int slot_state;
static int64_t slot_since;
static weather_request_t slot_request;
static weather_result_t *slot_result;   /* heap, handed to weather_port_poll (internal RAM, 2026-10-28) */
static const char *TAG = "weather";
void weather_port_esp_service(void); /* run by net_port_esp.c's worker */
static weather_config_t saved;
static bool saved_valid;

bool weather_port_load(weather_config_t *out) {
    nvs_handle_t h;
    if (nvs_open("weather", NVS_READONLY, &h) != ESP_OK) return false;
    weather_config_t c = {0};
    size_t n = sizeof c;
    bool ok = nvs_get_blob(h, "config1", &c, &n) == ESP_OK && n == sizeof c && weather_config_valid(&c);
    nvs_close(h);
    if (ok) { *out = c; saved = c; saved_valid = true; }
    return ok;
}
static bool save(const weather_config_t *c) {
    if (!weather_config_valid(c)) return false;
    if (saved_valid && !memcmp(c, &saved, sizeof *c)) return true;
    nvs_handle_t h;
    if (nvs_open("weather", NVS_READWRITE, &h) != ESP_OK) return false;
    bool ok = nvs_set_blob(h, "config1", c, sizeof *c) == ESP_OK && nvs_commit(h) == ESP_OK;
    nvs_close(h);
    if (ok) { saved = *c; saved_valid = true; }
    return ok;
}
static bool fetch(const char *url, char **body, size_t *length, int *error) {
    esp_http_client_config_t config = {
        .url = url, .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 8000, .disable_auto_redirect = true,
        .buffer_size = 1024, .buffer_size_tx = 512,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) { *error = WEATHER_ERR_MEMORY; return false; }
    bool ok = false;
    ESP_LOGI(TAG, "https start: 8-bit internal free %u, largest %u",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL));
    esp_err_t opened = esp_http_client_open(client, 0);
    if (opened != ESP_OK)
        ESP_LOGW(TAG, "https open failed: %s (8-bit internal free %u, largest %u)", esp_err_to_name(opened),
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL),
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL));
    if (opened == ESP_ERR_NO_MEM) *error = WEATHER_ERR_MEMORY;
    if (opened == ESP_OK) {
        int64_t declared = esp_http_client_fetch_headers(client);
        int status = esp_http_client_get_status_code(client);
        if (status != 200 || declared > WEATHER_BODY_MAX)
            ESP_LOGW(TAG, "https status %d, declared length %lld", status, (long long)declared);
        if (status == 200 && declared <= WEATHER_BODY_MAX) {
            *body = heap_caps_malloc(WEATHER_BODY_MAX + 2, MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);
            if (!*body) {
                *error = WEATHER_ERR_MEMORY;
                ESP_LOGE(TAG, "no memory for %u-byte response buffer", (unsigned)WEATHER_BODY_MAX + 2);
                goto cleanup;
            }
            size_t n = 0;
            int64_t began = esp_timer_get_time();
            while (n <= WEATHER_BODY_MAX && esp_timer_get_time() - began < 20000000) {
                int got = esp_http_client_read(client, *body + n, (int)(WEATHER_BODY_MAX + 1 - n));
                if (got < 0) break;
                if (!got) {
                    ok = esp_http_client_is_complete_data_received(client) && n > 0 && n <= WEATHER_BODY_MAX;
                    break;
                }
                n += (size_t)got;
            }
            if (ok) { (*body)[n] = 0; *length = n; }
        }
    }
cleanup:
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return ok;
}
static bool query_url(const weather_request_t *r, char *url, size_t cap) {
    if (r->operation == WEATHER_CURRENT) {
        if (!r->config.enabled || !r->config.located || !weather_config_valid(&r->config)) return false;
        snprintf(url, cap, "https://api.open-meteo.com/v1/forecast?latitude=%.6f&longitude=%.6f"
            "&current=temperature_2m,weather_code&temperature_unit=celsius&timeformat=unixtime",
            r->config.city.latitude, r->config.city.longitude);
        return true;
    }
    size_t n = 0;
    char encoded[sizeof r->query * 3];
    for (size_t i = 0; i < sizeof r->query && r->query[i]; ++i) {
        unsigned char c = (unsigned char)r->query[i];
        if (n + 3 >= sizeof encoded) return false;
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '-') encoded[n++] = (char)c;
        else { snprintf(encoded + n, sizeof encoded - n, "%%%02X", c); n += 3; }
    }
    if (n < 2) return false;
    encoded[n] = 0;
    snprintf(url, cap, "https://geocoding-api.open-meteo.com/v1/search?name=%s&count=5&language=en&format=json", encoded);
    return true;
}
static void finish(weather_result_t *result) {      /* takes the heap result; NULL = no memory for one */
    portENTER_CRITICAL(&slot_lock);
    slot_result = result;
    slot_state = SLOT_DONE;
    portEXIT_CRITICAL(&slot_lock);
}
static bool claim(bool want_save, weather_request_t *request) {
    bool ok = false;
    portENTER_CRITICAL(&slot_lock);
    if (slot_state == SLOT_PENDING && (slot_request.operation == WEATHER_SAVE) == want_save) {
        *request = slot_request;
        slot_state = SLOT_RUNNING;
        ok = true;
    }
    portEXIT_CRITICAL(&slot_lock);
    return ok;
}
static void save_task(void *unused) {
    (void)unused;
    weather_request_t request;
    if (claim(true, &request)) {
        weather_result_t *result = calloc(1, sizeof *result);
        bool ok = save(&request.config);
        if (result) {
            result->operation = request.operation; result->generation = request.generation;
            result->ok = ok; result->error = ok ? WEATHER_ERR_NONE : WEATHER_ERR_SAVE;
        }
        finish(result);
    }
    vTaskDelete(NULL);
}
void weather_port_esp_service(void) {
    static weather_request_t request;   /* only the net worker touches it */
    if (!claim(false, &request)) return;
    weather_result_t *result = calloc(1, sizeof *result);
    if (!result) { finish(NULL); ESP_LOGW(TAG, "no memory for a result"); return; }
    int64_t started = esp_timer_get_time();
    result->operation = request.operation; result->generation = request.generation;
    if (net_port_connected()) {
        char url[256];
        char *body = NULL;
        size_t n = 0;
        result->error = WEATHER_ERR_HTTP;
        if (query_url(&request, url, sizeof url) && fetch(url, &body, &n, &result->error)) {
            result->ok = request.operation == WEATHER_SEARCH ?
                weather_parse_cities(body, n, result->cities, &result->count) :
                weather_parse_current(body, n, &result->reading);
            result->error = result->ok ? WEATHER_ERR_NONE : WEATHER_ERR_INVALID;
            if (!result->ok) ESP_LOGW(TAG, "response rejected (%u bytes)", (unsigned)n);
        }
        free(body);
    } else result->error = WEATHER_ERR_OFFLINE;
    bool ok = result->ok; int error = result->error;
    finish(result);
    ESP_LOGI(TAG, "request %d finished in %lld ms: %s (error %d)",
             request.operation, (long long)((esp_timer_get_time() - started) / 1000),
             ok ? "OK" : "FAILED", error);
}
void weather_port_init(void) { }
bool weather_port_start(const weather_request_t *r) {
    portENTER_CRITICAL(&slot_lock);
    bool ok = slot_state == SLOT_IDLE;
    if (ok) { slot_request = *r; slot_state = SLOT_PENDING; slot_since = esp_timer_get_time(); }
    portEXIT_CRITICAL(&slot_lock);
    if (ok && r->operation == WEATHER_SAVE &&
        xTaskCreate(save_task, "weather_save", 3072, NULL, 2, NULL) != pdPASS) {
        portENTER_CRITICAL(&slot_lock);
        slot_state = SLOT_IDLE;
        portEXIT_CRITICAL(&slot_lock);
        ok = false;
    }
    return ok;
}
bool weather_port_poll(weather_result_t *out) {
    bool ok = false;
    int64_t now = esp_timer_get_time();
    portENTER_CRITICAL(&slot_lock);
    weather_result_t *done = NULL;
    if (slot_state == SLOT_DONE) {
        done = slot_result; slot_result = NULL; slot_state = SLOT_IDLE; ok = true;
        if (!done) {                       /* finished with no memory for a result */
            memset(out, 0, sizeof *out);
            out->operation = slot_request.operation; out->generation = slot_request.generation;
            out->error = WEATHER_ERR_MEMORY;
        }
    }
    else if (slot_state == SLOT_PENDING && slot_request.operation != WEATHER_SAVE && now - slot_since > 45000000) {
        /* Never picked up: the net worker is busy in the foreground or absent. */
        memset(out, 0, sizeof *out);
        out->operation = slot_request.operation; out->generation = slot_request.generation;
        out->error = WEATHER_ERR_OFFLINE;
        slot_state = SLOT_IDLE;
        ok = true;
    }
    portEXIT_CRITICAL(&slot_lock);
    if (done) { *out = *done; free(done); }
    return ok;
}
