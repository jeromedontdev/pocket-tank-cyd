#include "ambient.h"
#include "board_pins.h"
#include "render.h"
#include "driver/ledc.h"
#include "driver/gpio.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "nvs.h"
#include <math.h>

static const char *TAG = "ambient";
static adc_oneshot_unit_handle_t s_adc;
static bool s_led_ready, s_adc_ready;
static uint8_t s_room = ROOM_LIGHT_OFF, s_led = BACK_LED_GLOW, s_auto_backup = 1;
static int s_dark_raw, s_lit_raw;                 /* manual calibration; 0 = learn the room */
static float s_base = -1, s_base_saved = -1, s_base_save_t;  /* the learned lit-room reading */
static float s_raw_avg = -1, s_sample_t, s_dark_for, s_lit_for, s_phase;
static bool s_dark;
static int64_t s_override_until;
static uint8_t s_led_rgb[3] = {1, 1, 1};

static uint8_t nvs_get(const char *key, uint8_t fallback, uint8_t max) {
    nvs_handle_t h; uint8_t v;
    if (nvs_open("tank", NVS_READONLY, &h) != ESP_OK) return fallback;
    esp_err_t err = nvs_get_u8(h, key, &v);
    nvs_close(h);
    return err == ESP_OK && v <= max ? v : fallback;
}
static bool nvs_put(const char *key, uint8_t v) {
    nvs_handle_t h;
    esp_err_t err = nvs_open("tank", NVS_READWRITE, &h);
    if (err == ESP_OK) {
        err = nvs_set_u8(h, key, v);
        if (err == ESP_OK) err = nvs_commit(h);
        nvs_close(h);
    }
    if (err != ESP_OK) ESP_LOGE(TAG, "save %s failed: %s", key, esp_err_to_name(err));
    return err == ESP_OK;
}

void ambient_init(void) {
    s_room = nvs_get("room", ROOM_LIGHT_OFF, ROOM_LIGHT_MODES - 1);
    s_led = nvs_get("led", BACK_LED_GLOW, BACK_LED_MODES - 1);
    s_auto_backup = nvs_get("autobk", 1, 1);
    nvs_handle_t h; uint16_t base;
    if (nvs_open("tank", NVS_READONLY, &h) == ESP_OK) {
        if (nvs_get_u16(h, "ldrbase", &base) == ESP_OK && base > 0 && base < 4096) s_base = s_base_saved = base;
        nvs_close(h);
    }
    static const int pins[3] = {CYD_PIN_LED_R, CYD_PIN_LED_G, CYD_PIN_LED_B};
    s_led_ready = true;
    for (int i = 0; i < 3; i++) {
        gpio_hold_dis(pins[i]);
        ledc_channel_config_t ch = {
            .gpio_num = pins[i], .speed_mode = LEDC_LOW_SPEED_MODE,
            .channel = (ledc_channel_t)(LEDC_CHANNEL_1 + i), .timer_sel = LEDC_TIMER_0,
            .duty = 0, .flags.output_invert = 1,
        };
        if (ledc_channel_config(&ch) != ESP_OK) s_led_ready = false;
    }
    adc_oneshot_unit_init_cfg_t unit = { .unit_id = ADC_UNIT_1 };
    adc_oneshot_chan_cfg_t chan = { .atten = ADC_ATTEN_DB_0, .bitwidth = ADC_BITWIDTH_12 };
    s_adc_ready = adc_oneshot_new_unit(&unit, &s_adc) == ESP_OK &&
                  adc_oneshot_config_channel(s_adc, CYD_LDR_ADC_CHANNEL, &chan) == ESP_OK;
    ambient_led(0, 0, 0);
    ESP_LOGI(TAG, "back LED %s, room sensor %s (raw %d) | room %d led %d auto backup %d",
             s_led_ready ? "ready" : "FAILED", s_adc_ready ? "ready" : "FAILED", ambient_ldr_raw(),
             s_room, s_led, s_auto_backup);
}

void ambient_led(uint8_t r, uint8_t g, uint8_t b) {
    if (!s_led_ready || (s_led_rgb[0] == r && s_led_rgb[1] == g && s_led_rgb[2] == b)) return;
    uint8_t v[3] = {r, g, b};
    for (int i = 0; i < 3; i++) {
        ledc_set_duty(LEDC_LOW_SPEED_MODE, (ledc_channel_t)(LEDC_CHANNEL_1 + i), v[i]);
        ledc_update_duty(LEDC_LOW_SPEED_MODE, (ledc_channel_t)(LEDC_CHANNEL_1 + i));
        s_led_rgb[i] = v[i];
    }
}
void ambient_led_override(int ms) { s_override_until = esp_timer_get_time() + (int64_t)ms * 1000; }

void ambient_off_for_sleep(void) {
    static const int pins[3] = {CYD_PIN_LED_R, CYD_PIN_LED_G, CYD_PIN_LED_B};
    for (int i = 0; i < 3; i++) {
        if (s_led_ready) ledc_stop(LEDC_LOW_SPEED_MODE, (ledc_channel_t)(LEDC_CHANNEL_1 + i), 1);
        gpio_reset_pin(pins[i]);
        gpio_set_direction(pins[i], GPIO_MODE_OUTPUT);
        gpio_set_level(pins[i], 1);
        gpio_hold_en(pins[i]);
    }
}

int ambient_ldr_raw(void) {
    int raw = -1;
    if (!s_adc_ready || adc_oneshot_read(s_adc, CYD_LDR_ADC_CHANNEL, &raw) != ESP_OK) return -1;
    return raw;
}
/* The CYD's sensor reads higher as the room darkens and its range differs from board to board, so
 * by default the tank learns the lit room's reading and judges darkness on a log scale from it. */
#define LDR_DARK_RATIO 5.0f
int ambient_room_pct(void) {
    if (s_raw_avg < 0) return 100;
    if (s_dark_raw <= 0) {
        if (s_base <= 0) return 100;
        float base = s_base < CYD_LDR_MIN_BASE ? CYD_LDR_MIN_BASE : s_base;
        float ratio = s_raw_avg / base;
        if (ratio <= 1) return 100;
        float pct = 100.0f * (1.0f - logf(ratio) / logf(LDR_DARK_RATIO));
        return pct < 0 ? 0 : (int)lroundf(pct);
    }
    float span = (float)(s_dark_raw - s_lit_raw);
    if (fabsf(span) < 1) return 100;
    float pct = 100.0f * (s_dark_raw - s_raw_avg) / span;
    return pct < 0 ? 0 : pct > 100 ? 100 : (int)lroundf(pct);
}
bool ambient_room_dark(void) { return s_dark; }
int ambient_room_mode(void) { return s_room; }
int ambient_led_mode(void) { return s_led; }
bool ambient_auto_backup(void) { return s_auto_backup != 0; }
bool ambient_set_room_mode(int mode) {
    if (mode < 0 || mode >= ROOM_LIGHT_MODES || !nvs_put("room", (uint8_t)mode)) return false;
    s_room = (uint8_t)mode; return true;
}
bool ambient_set_led_mode(int mode) {
    if (mode < 0 || mode >= BACK_LED_MODES || !nvs_put("led", (uint8_t)mode)) return false;
    s_led = (uint8_t)mode; return true;
}
bool ambient_set_auto_backup(bool on) {
    if (!nvs_put("autobk", on)) return false;
    s_auto_backup = on; return true;
}
bool ambient_save_all(void) {
    return nvs_put("room", s_room) && nvs_put("led", s_led) && nvs_put("autobk", s_auto_backup);
}
void ambient_calibrate(int dark_raw, int bright_raw) {
    if (dark_raw <= bright_raw) dark_raw = bright_raw = 0;
    s_dark_raw = dark_raw; s_lit_raw = bright_raw;
}

static void learn_base(float dt) {
    if (s_raw_avg < 0) return;
    if (s_base <= 0 || s_raw_avg < s_base) s_base = s_raw_avg;
    else {
        float ratio = s_raw_avg / (s_base < CYD_LDR_MIN_BASE ? CYD_LDR_MIN_BASE : s_base);
        float tau = ratio < 1.5f ? 20 * 60.0f : ratio < 3.0f ? 6 * 3600.0f : 0;  /* drift, never the night */
        if (tau > 0) s_base += (s_raw_avg - s_base) * (dt / tau);
    }
    s_base_save_t += dt;
    if (s_base_save_t >= 30 * 60 && fabsf(s_base - s_base_saved) > s_base_saved * 0.1f + 2) {
        nvs_handle_t h;
        if (nvs_open("tank", NVS_READWRITE, &h) == ESP_OK) {
            if (nvs_set_u16(h, "ldrbase", (uint16_t)lroundf(s_base)) == ESP_OK && nvs_commit(h) == ESP_OK)
                s_base_saved = s_base;
            nvs_close(h);
        }
        s_base_save_t = 0;
    }
}

int ambient_brightness_pct(int pct) {
    if (s_room == ROOM_LIGHT_OFF || s_raw_avg < 0) return pct;
    int room = ambient_room_pct();
    int scaled = pct * (35 + 65 * room / 100) / 100;   /* a dark room keeps 35% of the keeper's level */
    return scaled < 8 ? 8 : scaled;
}
bool ambient_force_night(void) { return s_room == ROOM_LIGHT_NIGHT && s_dark; }

static void led_from_rgb(uint32_t rgb, float level) {
    if (level < 0) level = 0;
    if (level > 1) level = 1;
    ambient_led((uint8_t)lroundf(((rgb >> 16) & 255) * level), (uint8_t)lroundf(((rgb >> 8) & 255) * level),
                (uint8_t)lroundf((rgb & 255) * level));
}
static uint32_t saturate(uint32_t rgb) {          /* the water's dark tones, lifted to a visible hue */
    int r = (rgb >> 16) & 255, g = (rgb >> 8) & 255, b = rgb & 255;
    int m = r > g ? (r > b ? r : b) : (g > b ? g : b);
    if (m < 1) return 0x00a0ff;
    return (uint32_t)(r * 255 / m) << 16 | (uint32_t)(g * 255 / m) << 8 | (uint32_t)(b * 255 / m);
}

void ambient_tick(const tank_t *t, float dt) {
    s_sample_t += dt;
    if (s_sample_t >= 0.25f) {
        float elapsed = s_sample_t;
        int raw = ambient_ldr_raw();
        if (raw >= 0) s_raw_avg = s_raw_avg < 0 ? raw : s_raw_avg + (raw - s_raw_avg) * (1.0f - expf(-elapsed / 8.0f));
        learn_base(s_sample_t);
        s_sample_t = 0;
        int pct = ambient_room_pct();
        if (pct <= 12) { s_dark_for += elapsed; s_lit_for = 0; }
        else if (pct >= 30) { s_lit_for += elapsed; s_dark_for = 0; }
        else s_dark_for = s_lit_for = 0;
        if (!s_dark && s_dark_for >= 30) { s_dark = true; ESP_LOGI(TAG, "the room went dark (raw %.0f)", s_raw_avg); }
        if (s_dark && s_lit_for >= 5) { s_dark = false; ESP_LOGI(TAG, "the room is lit again (raw %.0f)", s_raw_avg); }
    }
    if (esp_timer_get_time() < s_override_until) return;
    s_phase += dt;
    if (s_led == BACK_LED_OFF) { ambient_led(0, 0, 0); return; }
    float breathe = 0.8f + 0.2f * sinf(s_phase * 6.2831853f / 8.0f);
    float level = (t->night ? 0.03f : 0.16f) * breathe;
    uint32_t rgb = saturate(render_glow_rgb(t));
    if (s_led == BACK_LED_ALERTS) {
        bool hungry = t->ravenous;
        for (int i = 0; i < t->n_fish && !hungry; i++) hungry = t->fish[i].hunger >= 8.0f;
        if (t->visitor_left > 0) {
            float flick = 0.5f + 0.5f * sinf(s_phase * 6.2831853f / 1.5f);
            rgb = 0xc8f0ff; level = (t->night ? 0.08f : 0.25f) * (0.4f + 0.6f * flick);
        } else if (hungry) {
            float pulse = 0.5f + 0.5f * sinf(s_phase * 6.2831853f / 2.5f);
            rgb = 0xff7800; level = (t->night ? 0.06f : 0.22f) * (0.25f + 0.75f * pulse);
        }
    }
    led_from_rgb(rgb, level);
}

void ambient_log(void) {
    ESP_LOGI(TAG, "room sensor raw %d avg %.0f -> %d%% (%s) | learned lit %.0f, manual dark %d lit %d | room %d led %d auto backup %d | LED %u %u %u",
             ambient_ldr_raw(), s_raw_avg, ambient_room_pct(), s_dark ? "DARK" : "lit", s_base, s_dark_raw, s_lit_raw,
             s_room, s_led, s_auto_backup, s_led_rgb[0], s_led_rgb[1], s_led_rgb[2]);
}
