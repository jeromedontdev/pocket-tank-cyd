/* CYD firmware: rules-based tank, SPI display, resistive touch and SD. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "esp_partition.h"
#include "esp_system.h"
#include "esp_memory_utils.h"
#include "tank.h"
#include "advisor.h"
#include "render.h"
#include "memory_plan.h"
#include "display_port.h"
#include "touch_port.h"
#include "director.h"
#include "update.h"
#include "update_mode.h"
#include "net_time.h"
#include <sys/time.h>
#include "esp_ota_ops.h"
#include "brightness.h"
#include "ambient.h"
#include "progression.h"
#include "companion.h"
#include "weather.h"
#include "sd_port_cyd.h"
#include "notice.h"
#include "tank_events.h"
#include "setup.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_app_desc.h"
#include "version.h"
#include "clock_port_esp.h"
#include "driver/gpio.h"
#include "esp_sleep.h"
#include "driver/rtc_io.h"

#define BTN_SLEEP GPIO_NUM_0

static const char *TAG = "pocket-tank";
static tank_t tank;
static uint16_t *fb[PLAN_FB_COUNT];
static void deep_sleep_now(int wake_after_s);
static bool s_btn_armed, s_btn_used;
static int64_t s_btn_low_since;
#define SLEEP_GRACE_CLOCKLESS_US (60LL * 60 * 1000000)
#define DIRECTOR_GRACE_US (5LL * 1000000)

typedef struct { float x, y, heading; uint8_t goal, valid; } fish_snap_t;
RTC_DATA_ATTR static fish_snap_t s_snap[N_FISH_MAX]; RTC_DATA_ATTR static int s_snap_n;
static void snap_log(const char *what) {          /* "FeZ 156,238/explore mira ..." */
    char line[N_FISH_MAX * 40] = ""; size_t l = 0;
    for (int i = 0; i < tank.n_fish && l + 40 < sizeof line; i++)
        l += snprintf(line + l, sizeof line - l, "%s%s %.0f,%.0f/%s", i ? " " : "", tank.fish[i].name,
                      tank.fish[i].x, tank.fish[i].y, GOAL_NAMES[tank.fish[i].goal.id]);
    ESP_LOGI(TAG, "%s: %s", what, line);
}
static void snapshot_fish(void) {
    s_snap_n = tank.n_fish;
    for (int i = 0; i < tank.n_fish; i++) {
        const fish_t *f = &tank.fish[i];
        s_snap[i] = (fish_snap_t){ f->x, f->y, f->heading, (uint8_t)f->goal.id, 1 };
    }
    snap_log("sleep snapshot");
}
static int restore_fish(void) {
    int n = 0;
    for (int i = 0; i < tank.n_fish && i < s_snap_n; i++) {
        const fish_snap_t *s = &s_snap[i];
        if (!s->valid || s->x < 0 || s->x > TANK_W || s->y < 0 || s->y > TANK_H) continue;
        fish_t *f = &tank.fish[i];
        f->x = s->x; f->y = s->y; f->heading = s->heading; tank_fish_face(f);
        if (s->goal < GOAL_COUNT) f->goal.id = (goal_id_t)s->goal;
        n++;
    }
    s_snap_n = 0;
    snap_log("wake restored");
    return n;
}
/* NVS netclk tracks whether timestamps use real time; netok records the last
 * sync result. The first successful sync rebases the build/save clock without
 * aging the tank through that artificial jump. Later gaps are real time away.
 * Deep sleep retains the ESP32 clock; a cold boot seeds it from build/save
 * time and can resync using saved Wi-Fi credentials before the SD cache starts. */
static int nvs_u8(const char *key, int dflt) {
    nvs_handle_t h; uint8_t v = (uint8_t)dflt;
    if (nvs_open("tank", NVS_READONLY, &h) == ESP_OK) { nvs_get_u8(h, key, &v); nvs_close(h); }
    return v;
}
static void nvs_u8_put(const char *key, int v) {
    nvs_handle_t h;
    if (nvs_open("tank", NVS_READWRITE, &h) == ESP_OK) { nvs_set_u8(h, key, (uint8_t)v); nvs_commit(h); nvs_close(h); }
}
static bool net_saved(void) { char s[NET_SSID_MAX + 1], p[NET_PASS_MAX + 1]; return net_port_creds_get(s, p); }
static int64_t s_rebase_unix, s_rebase_us;  /* a first sync, applied once the save has loaded on the old clock */
static float s_boot_lived_h;
static char s_boot_sync[96] = "no sync at this boot";   /* what this boot's sync did, for `clock` (the USB log of a boot off the cable is gone) */
/* The sync runs in a worker; the glass is drawn by the boot task alone. The
 * panel driver is one task's at a time (esp_lcd's SPI io keeps a plain
 * in-flight count and takes the bus per call): 0.3.2 drew this page from its
 * own task and let the boot go on after 0.5 s whether the page had stopped
 * or not - a frame still on the wire met the tank's first frame, both waited
 * on the bus for good, and the pendant froze on CHECKING THE TIME (three
 * times, found halted 2026-10-06: syncscr and tank both in spi_device_acquire_bus). */
static volatile bool s_sync_finished;
static bool s_sync_ok;
static bool s_wall_clock_synced;
#define CLOCK_TRUST_MAGIC 0x4e545043u
static RTC_NOINIT_ATTR uint32_t s_wall_clock_magic, s_wall_clock_check;
static void wall_clock_synced(void) {
    s_wall_clock_magic = CLOCK_TRUST_MAGIC; s_wall_clock_check = ~CLOCK_TRUST_MAGIC;
    s_wall_clock_synced = true; render_clock_synchronized(true);
}
static int64_t s_sync_u, s_sync_at;
static TaskHandle_t s_sync_waiter;
static void sync_worker(void *arg) {
    (void)arg;
    s_sync_ok = net_time_sync(&s_sync_u, &s_sync_at);
    s_sync_finished = true;
    xTaskNotifyGive(s_sync_waiter);
    vTaskDelete(NULL);
}
static void net_clock_boot(uint16_t *fb) {  /* before the tank exists: the radio has the internal heap to itself */
    esp_reset_reason_t reason = esp_reset_reason();
    s_wall_clock_synced = reason != ESP_RST_POWERON && reason != ESP_RST_BROWNOUT && reason != ESP_RST_UNKNOWN &&
        s_wall_clock_magic == CLOCK_TRUST_MAGIC && s_wall_clock_check == ~CLOCK_TRUST_MAGIC && clock_port_now_unix() != 0;
    if (!s_wall_clock_synced) s_wall_clock_magic = s_wall_clock_check = 0;
    render_clock_synchronized(s_wall_clock_synced);
    if (!net_saved()) return;
    bool real = nvs_u8("netclk", 0), set = clock_port_now_unix() != 0, ok_before = nvs_u8("netok", 0);
    if (real && set && ok_before && s_wall_clock_synced && !update_mode_wifi_only()) return;    /* retained NTP time; Wi-Fi setup explicitly resyncs */
    int64_t u = 0, at = 0;
    bool ok;
    s_sync_finished = false; s_sync_waiter = xTaskGetCurrentTaskHandle();
    if (fb && xTaskCreatePinnedToCore(sync_worker, "timesync", 8192, NULL, 5, NULL, 1) == pdPASS) {
        int64_t t0 = esp_timer_get_time();
        brightness_apply(false);
        do {                                 /* the page until the worker is done - no time limit: the tank never shares the panel */
            render_clock_sync(fb, TANK_W, (esp_timer_get_time() - t0) / 1e6f);
            display_port_flush(fb);
            ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(40));
        } while (!s_sync_finished);
        u = s_sync_u; at = s_sync_at; ok = s_sync_ok;
    } else ok = net_time_sync(&u, &at);      /* no glass (or no task): ask in place */
    nvs_u8_put("netok", ok);
    snprintf(s_boot_sync, sizeof s_boot_sync, "%s", ok ? "got the time" : "FAILED: no network or no answer");
    if (!ok) {
        ESP_LOGW(TAG, "clock: no time from the internet - %s; tonight is a deep sleep, the next wake asks again",
                 set ? "the clock runs on as it was" : "it resumes at the save, the stretch is owed");
        return;
    }
    int64_t now = u + (esp_timer_get_time() - at) / 1000000;
    if (real) {
        int64_t was = clock_port_now_unix();
        struct timeval tv = { .tv_sec = (time_t)now };
        if (settimeofday(&tv, NULL) != 0) {
            ESP_LOGE(TAG, "NTP received, but setting the system clock failed"); return;
        }
        wall_clock_synced();
        ESP_LOGI(TAG, "clock: set from the internet (%s) - the time away is lived through at this boot",
                 was ? "it had run on" : "it had stopped with the power");
        if (was) ESP_LOGI(TAG, "clock: it was %lld s %s", (long long)(now > was ? now - was : was - now), now >= was ? "behind (owed time, lived now)" : "ahead");
        snprintf(s_boot_sync, sizeof s_boot_sync, "got the time: %s", was ? "the clock had run on (corrected)" : "the clock had stopped (the time away lived from the save)");
    } else {
        s_rebase_unix = u; s_rebase_us = at;
        ESP_LOGI(TAG, "clock: first time from the internet - the clock moves to it once the tank is loaded, with nothing lived for the jump");
        snprintf(s_boot_sync, sizeof s_boot_sync, "got the time: the FIRST sync (clock moved, nothing lived)");
    }
}
static void net_clock_rebase(void) {        /* after progression_boot / _wake: a first sync takes over the clock */
    if (!s_rebase_us) return;
    struct timeval tv = { .tv_sec = (time_t)(s_rebase_unix + (esp_timer_get_time() - s_rebase_us) / 1000000) };
    if (settimeofday(&tv, NULL) != 0) {
        ESP_LOGE(TAG, "First NTP sync could not set the system clock"); return;
    }
    wall_clock_synced();
    nvs_u8_put("netclk", 1);
    s_rebase_us = 0;
    ESP_LOGI(TAG, "clock: on real time from now on - nights can be a power-off");
}

void device_clock_log(void) {
    ESP_LOGI(TAG, "clock: %s | lived %.2f h | now %lld | network %s | real time %s",
             s_boot_sync, s_boot_lived_h, (long long)clock_port_now_unix(),
             net_saved() ? "saved" : "none", s_wall_clock_synced ? "yes" : "not yet");
}
static void deep_sleep_now(int wake_after_s) {
    ambient_off_for_sleep();
    ESP_LOGI(TAG, "deep sleep: BOOT wakes%s", wake_after_s > 0 ? ", or the timer" : "");
    rtc_gpio_pullup_en(BTN_SLEEP);
    rtc_gpio_pulldown_dis(BTN_SLEEP);
    ESP_ERROR_CHECK(esp_sleep_enable_ext0_wakeup(BTN_SLEEP, 0));
    if (wake_after_s > 0)
        ESP_ERROR_CHECK(esp_sleep_enable_timer_wakeup((int64_t)wake_after_s * 1000000));
    esp_deep_sleep_start();
}
static void enter_sleep_for(int wake_after_s) {
    int64_t grace = wake_after_s > 0 ? DIRECTOR_GRACE_US : SLEEP_GRACE_CLOCKLESS_US;
    touch_port_confirm_answer(-1);
    if (!progression_save(&tank))
        ESP_LOGE(TAG, "sleep: save failed; the last good save stands");
    snapshot_fish();
    display_port_sleep();
    ambient_led(0, 0, 0);
    while (!gpio_get_level(BTN_SLEEP)) vTaskDelay(pdMS_TO_TICKS(10));
    int64_t t0 = esp_timer_get_time();
    ESP_ERROR_CHECK(gpio_wakeup_enable(BTN_SLEEP, GPIO_INTR_LOW_LEVEL));
    ESP_ERROR_CHECK(esp_sleep_enable_gpio_wakeup());
    ESP_ERROR_CHECK(esp_sleep_enable_timer_wakeup(grace));
    ESP_ERROR_CHECK(esp_light_sleep_start());
    esp_sleep_wakeup_cause_t why = esp_sleep_get_wakeup_cause();
    gpio_wakeup_disable(BTN_SLEEP);
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
    if (why == ESP_SLEEP_WAKEUP_GPIO) {
        while (!gpio_get_level(BTN_SLEEP)) vTaskDelay(pdMS_TO_TICKS(10));
        tank_tick_sleep(&tank, (esp_timer_get_time() - t0) / 1e6f);
        progression_woke(&tank);
        display_port_wake();
        s_snap_n = 0;
        s_btn_armed = false;
        s_btn_low_since = 0;
        ESP_LOGI(TAG, "resumed tank after light sleep");
        return;
    }
    deep_sleep_now(wake_after_s);
}
static void enter_sleep(void) { enter_sleep_for(0); }
void device_sleep(int wake_after_s) { enter_sleep_for(wake_after_s); }
void device_poweroff(void) {
    if (!progression_save(&tank))
        ESP_LOGE(TAG, "deep sleep: save failed; the last good save stands");
    snapshot_fish();
    display_port_sleep();
    while (!gpio_get_level(BTN_SLEEP)) vTaskDelay(pdMS_TO_TICKS(10));
    deep_sleep_now(0);
}

/* BOOT: the RESET chord (2026-09-11) - while it is held, a finger landing on
 * the glass opens the confirm prompt; a finger already resting there doesn't
 * count. With no PMIC (so no PWR key) a short press, at RELEASE, is the sleep
 * key as it was until 2026-09-16; a chord press never is. */
#define BTN_DEBOUNCE_US 50000
static void sleep_button_poll(int64_t now) {
    if (gpio_get_level(BTN_SLEEP)) {
        if (s_btn_armed && s_btn_low_since && !s_btn_used && now - s_btn_low_since >= BTN_DEBOUNCE_US)
            enter_sleep();
        s_btn_armed = true; s_btn_low_since = 0; s_btn_used = false;
    } else if (s_btn_armed) {
        if (!s_btn_low_since) s_btn_low_since = now;
        else if (!s_btn_used && touch_port_pressed_since(s_btn_low_since) && !touch_port_confirm_up()) {
            s_btn_used = true;
            ESP_LOGI(TAG, "BOOT + tap: reset prompt");
            touch_port_confirm_open();
        }
    }
}

/* A confirmed wipe invalidates the entire profile/SD collection and clears
 * its fixed mode. Explicit user backups remain. progression_reset commits
 * the durable wipe marker before staging a new first-run pair. */
static void reset_tank(void) {
    ESP_LOGW(TAG, "RESET: wiping the tank (%d fish) for a fresh one", tank.n_fish);
    if (!progression_reset(&tank, (uint32_t)esp_timer_get_time() ^ 0xC0FFEEu)) {
        ESP_LOGE(TAG, "WIPE FAILED: tank erase or fresh save could not be persisted");
        render_backup_status("WIPE FAILED - CHECK STORAGE", false);
        if (!progression_setup_pending()) { touch_port_show_settings(true); return; }
    }
    notice_sync(&tank);                         /* a fresh tank has nothing to announce */
    brightness_save();                          /* the erase took the setting with it */
    ambient_save_all();
    ESP_LOGI(TAG, "fresh tank: %s + %s, both fry", tank.fish[0].name, tank.fish[1].name);
    setup_begin(&tank);                         /* welcome, names, colours - as on a fresh install */
}

/* photo mode (1.3): the page closes, the tank settles for a moment, then one
 * full repaint is written tile by tile to the SD card */
static float s_photo_delay = -1;
static bool s_photo_reopen;
void device_photo(bool from_settings) {
    s_photo_reopen = from_settings;
    if (from_settings) touch_port_show_settings(false);
    s_photo_delay = 0.8f;
}
static void request_update(void);        /* below: CHECK FOR UPDATES */
static bool s_benchmark_requested;
void device_render_benchmark(void) { s_benchmark_requested = true; }

static void render_benchmark(uint16_t *buffer) {
    if (!buffer) {
        ESP_LOGE(TAG, "benchmark requires an allocated framebuffer");
        return;
    }
    if (!tank.companion.display_compact || !tank.n_fish || setup_active()) {
        ESP_LOGE(TAG, "benchmark requires Normal scale, residents and no setup panel");
        return;
    }
    tank_t *sample = malloc(sizeof *sample);
    if (!sample) {
        ESP_LOGE(TAG, "benchmark cannot allocate an isolated fixture");
        return;
    }
    ESP_LOGI("benchmark", "BEGIN RAM-only five-fish Normal Balanced; no progression or save writes");
    ESP_LOGI("benchmark", "Two fixed scenes, live-copy decor/clock, then maximum extra ornaments; includes 5 ms/frame service reserve");
    for (int fixture = 0; fixture < 4; fixture++) {
        if (fixture == 2) {
            *sample = tank;
            for (int i = sample->n_fish; i < 5; i++) {
                sample->fish[i] = sample->fish[0];
                sample->fish[i].x = TANK_W * .8f;
                sample->fish[i].y = TANK_BOT * .6f;
            }
            sample->n_fish = 5;
            sample->clock_requested = true;
        } else {
            memset(sample, 0, sizeof *sample);
            tank_init(sample, 0x5167);
            tank_new_population(sample);
            while (sample->n_fish < 5) if (tank_add_fish(sample, 0, 1) < 0) {
                ESP_LOGE(TAG, "benchmark could not populate its isolated fixture");
                free(sample);
                return;
            }
            sample->companion.display_compact = true;
            sample->sd_unlocks = SD_ITEM_PLANT | SD_ITEM_SHRIMP | SD_ITEM_SNAIL |
                SD_ITEM_URCHIN | SD_ITEM_SEAHORSE | SD_ITEM_LIGHTS | SD_ITEM_SAND |
                SD_ITEM_PEBBLES | SD_ITEM_PEARL | SD_ITEM_SPONGES | SD_ITEM_BARREL;
            sample->journal.xp = journal_level_xp(20);
            sample->journal.initialized = 1;
            sample->journal.theme = (uint8_t)(fixture % 2);
            sample->clock = 10;
            sample->clock_requested = false;
            sample->aquariums.clock_theme = CLOCK_OFF;
            sample->light_override = true; sample->light_on = true;
            sample->night = false;
            for (int i = 0; i < 5; i++) {
                sample->fish[i].stage = i == 4 ? STAGE_ELDER : STAGE_ADULT;
                sample->fish[i].size = i == 4 ? 1.25f : 1.05f;
                sample->fish[i].variety = (uint8_t)(fixture % 2 * 5 + i);
                sample->fish[i].pattern = (uint8_t)(i % 4);
                sample->fish[i].wearable = (uint8_t)(1 + i % (WEAR_COUNT - 1));
                sample->fish[i].hunger = 3; sample->fish[i].energy = 7;
                sample->fish[i].x = TANK_W * (i + 1) / 6.0f;
                sample->fish[i].y = TANK_BOT * (0.25f + i % 3 * .22f);
                sample->fish[i].heading = i * .7f;
                sample->fish[i].goal.id = GOAL_EXPLORE;
            }
            if (fixture == 3) {
                sample->mussel.owned = sample->mussel.bank = 1;
                for (int i = 0; i < DECOR_EXTRA_CAP; i++) {
                    sample->inventory.extra[i].item = 17;
                    sample->inventory.extra[i].x = TANK_W * (i + 1) / (float)(DECOR_EXTRA_CAP + 1);
                    sample->inventory.extra[i].z = i % 2 ? DECOR_Z_FRONT : DECOR_Z_BACK;
                }
            }
        }
        sample->ui_cover = sample->hold_active = sample->drag_active = false;
        render_device_t device = {0, 0, false, -1, SCENE_BALANCED};
        render_set_device(&device);
        render_work_caches(true);
        render_set_backdrop_cache((uint8_t *)buffer + TANK_W * 80 * 2,
                                 PLAN_FB_BYTES - TANK_W * 80 * 2);
        int timed_frames = fixture == 2 ? 1200 : 240;
        int64_t started = 0, window = 0, tick = 0, draw = 0, flush = 0;
        double lowest_fps = 1e9;
        for (int frame = 0; frame < timed_frames + 10; frame++) {
            if (frame == 10) {
                started = window = esp_timer_get_time();
                memset(render_prof_us, 0, sizeof render_prof_us);
                memset(render_helper_us, 0, sizeof render_helper_us);
            }
            int64_t a = esp_timer_get_time();
            tank_tick(sample, .1f, advisor_rules);
            int64_t b = esp_timer_get_time();
            render_damage_begin(sample, frame == 0);
            int cursor = 0, x, y, width, height;
            while (render_damage_next(&cursor, &x, &y, &width, &height)) {
                render_region(x, width); render_tile(y, height);
                int64_t before = esp_timer_get_time();
                render_tank(sample, buffer, TANK_W);
                int64_t after = esp_timer_get_time();
                display_port_flush(buffer);
                if (frame >= 10) {
                    draw += after - before;
                    flush += esp_timer_get_time() - after;
                }
            }
            render_tile(0, 0); render_region(0, 0);
            if (frame >= 10) tick += b - a;
            vTaskDelay(pdMS_TO_TICKS(5) + 1);
            if (frame >= 10 && (frame - 9) % 120 == 0) {
                int64_t now = esp_timer_get_time();
                double fps = 120000000.0 / (now - window);
                if (fps < lowest_fps) lowest_fps = fps;
                window = now;
            }
        }
        int64_t elapsed = esp_timer_get_time() - started;
        double divisor = timed_frames * 1000.0;
        ESP_LOGI("benchmark", "RESULT fixture %d %d frames %.2f fps (lowest 120-frame window %.2f) | tick %.2f render %.2f flush %.2f ms | scene %.2f veg %.2f fish/front %.2f fish-only %.2f ms",
                 fixture, timed_frames, timed_frames * 1000000.0 / elapsed, lowest_fps,
                 tick / divisor, draw / divisor, flush / divisor,
                 render_prof_us[0] / divisor, render_prof_us[2] / divisor, render_prof_us[4] / divisor,
                 render_prof_us[1] / divisor);
        ESP_LOGI("benchmark", "HELPERS snail %.2f urchin %.2f seahorse %.2f ms",
                 render_helper_us[0] / divisor, render_helper_us[1] / divisor, render_helper_us[2] / divisor);
    }
    free(sample);
    int64_t discarded_wait, discarded_send;
    display_port_flush_prof(&discarded_wait, &discarded_send);
    render_damage_begin(&tank, true);
    ESP_LOGI("benchmark", "END original aquarium retained; diagnostic excluded from normal FPS logs");
}

static void tank_task(void *arg);
static void start_tank_task(void *arg) {
    (void)arg;
    ESP_LOGI(TAG, "starting the tank task: internal heap %u KB free (largest block %u KB)",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024, (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) / 1024);
    if (xTaskCreatePinnedToCore(tank_task, "tank", 12288, NULL, 4, NULL, 0) != pdPASS)
        ESP_LOGE(TAG, "THE TANK TASK COULD NOT START: no internal RAM for its stack");
}
#define FRAME_MIN_MS 40                      /* the frame rate's ceiling: 25 fps (see the loop's foot) */
static void tank_task(void *arg) {
    (void)arg;
    int64_t last = esp_timer_get_time(); int cur = 0;
    int64_t last_log = last;
    int64_t render_us = 0, flush_us = 0, card_us = 0; uint32_t frames = 0, card_frames = 0;
    uint32_t drawn_rows = 0;
    /* Time the whole frame, not only rendering: keys/touch/console, simulation,
     * scene prefetch wait, post-flush work and the frame limiter. */
    int64_t polls_us = 0, tick_us = 0, wait_us = 0, tail_us = 0, sleep_us = 0, slept_from = 0;
    ESP_LOGI(TAG, "tank task up | heap int %u KB", (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024);
    for (;;) {
        if (s_benchmark_requested) {
            s_benchmark_requested = false;
            render_benchmark(fb[cur]);
            last = last_log = esp_timer_get_time();
            render_us = flush_us = card_us = polls_us = tick_us = wait_us = tail_us = sleep_us = slept_from = 0;
            frames = card_frames = drawn_rows = 0;
            memset(render_prof_us, 0, sizeof render_prof_us);
            memset(render_helper_us, 0, sizeof render_helper_us);
        }
        int64_t now = esp_timer_get_time();
        float dt = (now - last) / 1e6f; last = now;
        float max_dt = tank.companion.display_compact ? 1.0f : 0.25f;
        if (dt > max_dt) dt = max_dt;
        if (slept_from) sleep_us += now - slept_from;
        sleep_button_poll(now);
        tank_orient(&tank, false);
        touch_port_poll(&tank);
        director_poll(&tank);
        int ans = touch_port_confirm_take();
        if (ans > 0) reset_tank();
        else if (ans < 0) ESP_LOGI(TAG, "reset prompt: tank kept");
        { int v = 0, w = touch_port_take_setting(&v);                 /* the settings page */
          if (w == SET_TAP_BRIGHT_PREVIEW) brightness_preview_level(v);
          else if (w == SET_TAP_BRIGHT) {
              if (!brightness_set_level(v)) render_backup_status("BRIGHTNESS SAVE FAILED - RETRY", false);
          }
          else if (w == SET_TAP_WIFI) {
              render_work_caches(false);
              device_wifi_setup();
          }
          else if (w == SET_TAP_BACKUP || w == SET_TAP_RESTORE) sd_port_backup_action(&tank, w == SET_TAP_RESTORE, v != 0);
          else if (w == SET_TAP_DETAIL) {
              int detail = (ambient_scene_detail() + 1) % SCENE_DETAIL_COUNT;
              render_backup_status(ambient_set_scene_detail(detail) ?
                  (detail == SCENE_SIMPLE ? "SIMPLE SAVED - STATIC DECOR" :
                   detail == SCENE_ENHANCED ? "ENHANCED SAVED - SMOOTH EFFECTS" : "BALANCED SAVED - LIGHTER EFFECTS") :
                  "SCENE SAVE FAILED - UNCHANGED", false);
          }
          else if (w == SET_TAP_ROOM) {
              int m = (ambient_room_mode() + 1) % ROOM_LIGHT_MODES;
              static const char *const names[] = {"ROOM LIGHT OFF", "ROOM LIGHT DIMS THE SCREEN", "DARK ROOM: SCREEN DIMS, TANK SLEEPS"};
              render_backup_status(ambient_set_room_mode(m) ? names[m] : "ROOM LIGHT SAVE FAILED", false);
          }
          else if (w == SET_TAP_LED) {
              int m = (ambient_led_mode() + 1) % BACK_LED_MODES;
              static const char *const names[] = {"BACK LED OFF", "BACK LED GLOWS WITH THE WATER", "BACK LED: GLOW + HUNGER/VISITOR ALERTS"};
              render_backup_status(ambient_set_led_mode(m) ? names[m] : "BACK LED SAVE FAILED", false);
          }
          else if (w == SET_TAP_AUTO_BACKUP)
              render_backup_status(ambient_set_auto_backup(!ambient_auto_backup()) ?
                  (ambient_auto_backup() ? "DAILY SD BACKUP ON - KEEPS 7" : "DAILY SD BACKUP OFF") : "SETTING SAVE FAILED", false);
          else if (w == SET_TAP_PHOTO) device_photo(true);

          else if (w == SET_TAP_LIGHT) ESP_LOGI(TAG, "settings: lights out %s", v ? "AUTO (the idle rule)" : "MANUAL (double-tap the glass)");
          else if (w == SET_TAP_IDLE) ESP_LOGI(TAG, "settings: lights out after %d s still", v);
          else if (w == SET_TAP_FEED) ESP_LOGI(TAG, "settings: auto feed %s", v ? "ON" : "OFF (the keeper feeds; a starving fish loses trust)");
          else if (w == SET_TAP_SAND || w == SET_TAP_WATER) {
              ESP_LOGI(TAG, "settings: %s palette %d", w == SET_TAP_SAND ? "sand" : "water", v);
              if (!progression_save(&tank)) ESP_LOGE(TAG, "could not save palette selection");
          }
          }
        int update_action = touch_port_take_update();
        if (update_action == UPD_TAP_WIFI) request_update();
        if (update_action == UPD_TAP_CHECK) {
            updates_sd_status("CHECKING / INSTALLING - KEEP POWER ON", false);
            int height = tank.companion.display_compact ? 80 : TANK_H;
            for (int y = 0; y < TANK_H; y += height) {
                render_tile(y, tank.companion.display_compact ? height : 0);
                render_updates_page(fb[cur], TANK_W);
                display_port_flush(fb[cur]);
            }
            render_tile(0, 0);
            sd_port_update_action(&tank);
        }
        { int r = touch_port_take_shop();                               /* the shop's UNLOCK / MOVE / SELL */
          if (r >= SHOP_TAP_INVENTORY) {
              render_shop_inventory_action(&tank, r);
              if (setup_is_place()) touch_port_show_shop(false);
          } else if (r >= SHOP_TAP_FRY) {                                      /* a fry bought (0.3.5): the shop closes, the birth flow welcomes it */
              int k = r - SHOP_TAP_FRY;
              if (progression_buy_fry(&tank, k)) { touch_port_show_shop(false);
                  ESP_LOGI(TAG, "shop: fry %d bought, %d sand dollars left, %d of %d left today", k, (int)tank.sd_balance, progression_fry_left(), FRY_OFFER_N); }
              else ESP_LOGI(TAG, "shop: fry %d refused (balance %d, %d fish)", k, (int)tank.sd_balance, tank.n_fish);
          } else if (r >= SHOP_TAP_SELL) {                              /* sold back: the refund, the piece gone, the row for sale again */
              int item = r - SHOP_TAP_SELL;
              if (progression_sell(&tank, item)) { 
                  ESP_LOGI(TAG, "shop: %s sold back for %d, balance %d", SD_ITEMS[item].name, progression_sell_value(item), (int)tank.sd_balance); }
          } else if (r >= SHOP_TAP_MOVE) {                              /* a piece already in the tank: place it again */
              int item = r - SHOP_TAP_MOVE;
              touch_port_show_shop(false); setup_begin_place(&tank, item);
              ESP_LOGI(TAG, "shop: MOVE %s - placement page up (drag, DEPTH, DONE)", SD_ITEMS[item].name);
          } else if (r >= SHOP_TAP_BUY) {
              int item = r - SHOP_TAP_BUY;
              if (progression_buy(&tank, item)) { 
                  ESP_LOGI(TAG, "shop: %s unlocked, %d sand dollars left", SD_ITEMS[item].name, (int)tank.sd_balance);
                  if (tank_decor_placeable(item)) {                     /* a placeable piece: the page opens over the live tank */
                      touch_port_show_shop(false); setup_begin_place(&tank, item);
                      ESP_LOGI(TAG, "shop: placement page up for the %s", SD_ITEMS[item].name); } }
              else ESP_LOGI(TAG, "shop: %s refused (balance %d, price %d)", SD_ITEMS[item].name, (int)tank.sd_balance, SD_ITEMS[item].price); } }
        tank.room_dark = ambient_force_night();
        ambient_tick(&tank, dt);
        sd_port_auto_backup_tick(&tank, dt);
        { render_device_t d = { (uint8_t)ambient_room_mode(), (uint8_t)ambient_led_mode(), ambient_auto_backup(),
                                ambient_ldr_raw() < 0 ? -1 : ambient_room_pct(), (uint8_t)ambient_scene_detail() };
          render_set_device(&d); }
        brightness_apply(tank.night && !render_clock_active(&tank) && !touch_port_games());
        render_work_caches(!weather_network_needed_after(dt));
        weather_tick(dt);
        int64_t pf_tick = esp_timer_get_time(); polls_us += pf_tick - now;
        tank.hold_light = setup_active() || touch_port_confirm_up() || touch_port_calibrating() || touch_port_games();   /* no lights-out mid-name or game */
        tank.ui_cover = tank.hold_light || touch_port_milestones() || touch_port_settings() || touch_port_updates() || touch_port_shop();   /* a fry's spawning waits */
        /* Compact frames can exceed one physics step without slowing care and idle timers. */
        bool held = tank.hold_active, dragged = tank.drag_active;
        for (float left = dt; left > 0;) {
            float step = left < 0.25f ? left : 0.25f;
            tank.hold_active = held; tank.drag_active = dragged;
            if (touch_port_games()) render_games_tick(step);
            else {
                tank_tick(&tank, step, advisor_rules);
                progression_tick(&tank, step);
            }
            companion_tick(&tank, step);
            if (!touch_port_games()) notice_tick(&tank, step, tank.ui_cover || setup_birth_due());
            left -= step;
        }
        if (!touch_port_confirm_up() && !touch_port_games()) {           /* an arrival owed its welcome: the birth flow (setup.c) */
            int nb = setup_poll_birth(&tank);
            if (nb >= 0) { touch_port_dismiss(); 
                           ESP_LOGI(TAG, "a new fry, %s: birth flow up (announce, name, family; director `setup off` drops it)", tank.fish[nb].name); }
        }
        int64_t pf_wait = esp_timer_get_time(); tick_us += pf_wait - pf_tick;
        int64_t pf_tail = pf_wait;
        if (fb[cur]) {
            int64_t t0 = esp_timer_get_time();
            wait_us += t0 - pf_wait;
            touch_port_poll(&tank);
            int tile_height = 80;
            size_t tile_bytes = (size_t)TANK_W * tile_height * sizeof(uint16_t);
            render_set_backdrop_cache((uint8_t *)fb[cur] + tile_bytes, PLAN_FB_BYTES - tile_bytes);
            bool page = touch_port_milestones() || touch_port_settings() || touch_port_updates() || touch_port_shop();
            bool photo = false; int photo_rows = 0;
            if (s_photo_delay >= 0 && (s_photo_delay -= dt) < 0) {
                s_photo_delay = -1;
                photo = !page && sd_port_photo_begin(TANK_W, TANK_H);
                if (!photo) render_backup_status(page ? "PHOTO CANCELED - CLOSE THE PAGE" : "PHOTO FAILED - CHECK SD", false);
            }
            render_damage_begin(&tank, photo || page ||
                (touch_port_selected() >= 0 && !render_clock_active(&tank)) || setup_active() || touch_port_confirm_up() ||
                touch_port_calibrating() || (!render_clock_active(&tank) && notice_current() != NULL));
            int damage_cursor = 0, left = 0, first = 0, width = TANK_W, height = tile_height;
            while (render_damage_next(&damage_cursor, &left, &first, &width, &height)) {
            drawn_rows += height * width / TANK_W;
            render_region(left, width);
            render_tile(first, height);
            /* a page that covers the glass (KEEPER, SETTINGS, UPDATES, UPGRADES: each fills the whole frame before
               it draws): the tank under it is not rendered - it was every frame, hidden (2026-10-13, on a watch:
               19 fps with SETTINGS up, 25 without the hidden tank). The tank itself ticks on. No buffer holds
               the scene while a page is up, so none is "primed": the first tank frame after the page copies the
               scene itself. */
            if (page) render_fb_primed(NULL, 0);
            else render_tank(&tank, fb[cur], TANK_W);
            int sel = touch_port_selected();
            bool idle_clock = render_clock_active(&tank);
            if (idle_clock) sel = -1;
            int64_t tc = esp_timer_get_time();
            if (touch_port_milestones()) {       /* milestones page: covers the tank until a tap */
                if (touch_port_life()) render_life(&tank, fb[cur], TANK_W);
                else if (touch_port_games()) render_games(&tank, fb[cur], TANK_W);
                else if (touch_port_bank()) render_bank(&tank, fb[cur], TANK_W);
                else if (touch_port_journal()) render_journal(&tank, fb[cur], TANK_W);
                else render_milestones(&tank, fb[cur], TANK_W);
                sel = -1;
            } else if (touch_port_settings()) {  /* settings page: brightness + volume */
                render_settings(&tank, fb[cur], TANK_W, brightness_level());
                sel = -1;
            } else if (touch_port_updates()) {   /* the updates page (2026-09-30): version, network, CHECK FOR UPDATES */
                render_updates_page(fb[cur], TANK_W);
                sel = -1;
            } else if (touch_port_shop()) {      /* the shop: sand dollars and what they buy */
                if (touch_port_aquariums()) render_aquariums(&tank, fb[cur], TANK_W);
                else render_shop(&tank, fb[cur], TANK_W);
                sel = -1;
            } else if (!idle_clock) render_sd_toast(&tank, fb[cur], TANK_W);   /* the live tank: "+N" as dollars are earned */
            if (sel >= 0) render_stats_card(&tank, sel, fb[cur], TANK_W);   /* tapped fish: stats card (+ the pill) */
            if (!idle_clock) render_tool_chip(&tank, sel, fb[cur], TANK_W);
            if (!idle_clock && !touch_port_milestones() && !touch_port_settings() && !touch_port_updates() && !touch_port_shop()) {   /* an announcement over the live tank */
                const notice_t *nt = notice_current();
                if (nt) render_notice(&tank, fb[cur], TANK_W, nt->kind, nt->fish, nt->bit, 1.0f - nt->age / NOTICE_UP_S);
            }
            if (setup_active())                  /* first-run setup: over the tank, under the prompt */
                render_setup(&tank, fb[cur], TANK_W, tank.clock);
            if (touch_port_confirm_up())         /* reset prompt: over everything, fish still swim */
                render_confirm_reset(fb[cur], TANK_W, touch_port_confirm_frac());
            int64_t t1 = esp_timer_get_time();
            if (sel >= 0) { card_us += t1 - tc; card_frames++; }
            /* the next frame's scene prefetch starts BEFORE this frame's flush (2026-10-03): the copy
               into the idle framebuffer takes ~14 ms, and started after the flush it had only the
               tick and a 1 ms sleep to hide behind - the next frame waited ~9 ms for it. The flush
               only reads this frame's buffer; the copy rides alongside it. */
            touch_port_render_calibration(fb[cur], TANK_W);
            if (photo && width == TANK_W && sd_port_photo_rows(fb[cur], first, height, TANK_W)) photo_rows += height;
            display_port_flush(fb[cur]);
            pf_tail = esp_timer_get_time();
            render_us += t1 - t0; flush_us += pf_tail - t1;
            t0 = pf_tail;
            }
            render_tile(0, 0); render_region(0, 0);
            if (photo) {
                sd_port_photo_end(photo_rows == TANK_H);
                if (s_photo_reopen) touch_port_show_settings(true);
            }
            touch_port_poll(&tank);
            frames++;
        }
        if (now - last_log > 10 * 1000000) {
            if (frames) {
                unsigned ep; const uint16_t *sc = render_scene_buf(&ep);
                ESP_LOGI("display", "fb mid 0x%04x corner 0x%04x | scene mid 0x%04x ep %u | night %d prefetch %d",
                         fb[cur] ? fb[cur][5 * TANK_W + TANK_W / 2] : 0,
                         fb[cur] ? fb[cur][5 * TANK_W + 5] : 0,
                         sc ? sc[(TANK_H / 2) * TANK_W + TANK_W / 2] : 0, ep,
                         (int)tank.night, 0);
                ESP_LOGI("display", "%.1f fps | render %.1f ms flush %.1f ms | scene %.1f fish-only %.1f veg %.1f fd/bub %.1f fish/front %.1f vig %.1f algae %.1f clock %.1f | card %.1f ms x%lu | veg %.2f %.2f %.2f",
                         frames * 1e6f / (float)(now - last_log),
                         render_us / 1e3f / frames, flush_us / 1e3f / frames,
                         render_prof_us[0] / 1e3f / frames, render_prof_us[1] / 1e3f / frames,
                         render_prof_us[2] / 1e3f / frames, render_prof_us[3] / 1e3f / frames,
                         render_prof_us[4] / 1e3f / frames, render_prof_us[5] / 1e3f / frames,
                         render_prof_us[6] / 1e3f / frames,
                         render_prof_us[7] / 1e3f / frames,
                         card_frames ? card_us / 1e3f / card_frames : 0.0f, (unsigned long)card_frames,
                         tank.veg_growth[0], tank.veg_growth[1], tank.veg_growth[2]);
                int64_t fw, fs; display_port_flush_prof(&fw, &fs);
                ESP_LOGI("display", "frame %.1f ms = polls %.1f + tick %.1f + prefetch wait %.1f + render %.1f + flush %.1f + tail %.1f + sleep %.1f | flush: fill %.1f, wire wait %.1f, send calls %.1f",
                         (float)(now - last_log) / 1e3f / frames, polls_us / 1e3f / frames, tick_us / 1e3f / frames, wait_us / 1e3f / frames,
                         render_us / 1e3f / frames, flush_us / 1e3f / frames, tail_us / 1e3f / frames, sleep_us / 1e3f / frames,
                         (flush_us - fw - fs) / 1e3f / frames, fw / 1e3f / frames, fs / 1e3f / frames);
                ESP_LOGI("display", "CPU rows %.1f/%d per frame | artwork/clock cache %u bytes",
                         (float)drawn_rows / frames, TANK_H, (unsigned)render_work_cache_bytes());
                card_us = 0; card_frames = 0;
                memset(render_prof_us, 0, sizeof render_prof_us);
                memset(render_helper_us, 0, sizeof render_helper_us);
            }
            render_us = flush_us = 0; frames = 0; drawn_rows = 0;
            polls_us = tick_us = wait_us = tail_us = sleep_us = 0;
            char goals[N_FISH_MAX * 16] = ""; size_t gl = 0;
            for (int i = 0; i < tank.n_fish && gl + 16 < sizeof goals; i++)
                gl += snprintf(goals + gl, sizeof goals - gl, "%s%s", i ? " " : "", GOAL_NAMES[tank.fish[i].goal.id]);
            ESP_LOGI(TAG, "t=%.0fs %d fish goals: %s | rules advisor asks %lu | starve-ignored %d | heap int %u KB bright %d",
                     tank.clock, tank.n_fish, goals, (unsigned long)tank.advisor_asks,
                     tank_reflex_overrides,
                     (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024,
                     display_port_brightness());
            last_log = now;
        }
        /* the frame's ceiling (2026-10-04): FRAME_MIN_MS a frame, 25 fps. Frames and decisions share one
           budget - measured on the 1.8, the same code at 16.6 fps decides in 3.69 s and at 22.8 fps in
           3.93 s (~40 ms a decision per frame a second; the bigger glasses pay more). Strato: decisions
           past 4 s are too slow, so a light tank no longer spends the model's time on frames past 25.
           Always yield >= 1 tick. */
        int rest = (int)((FRAME_MIN_MS * 1000 - (esp_timer_get_time() - now)) / 1000);
        slept_from = esp_timer_get_time(); tail_us += slept_from - pf_tail;
        vTaskDelay(pdMS_TO_TICKS(rest < 1 ? 1 : rest));
    }
}

/* the settings page's dim version line: ESP-IDF stamps the app descriptor
   with `git describe --always --tags --dirty` of the checkout at build (the
   installer's Actions job checks out with the full history), the same words
   the installer page shows for what it would write */
const char *version_port_string(void) { return PT_BUILD_ID; }

/* NVS holds the keeper's tank, so it is never erased on a guess. Only "no
 * free pages" (a partition that can't mount at all) is answered with an
 * erase, and a raw copy of the partition goes to the unused front of
 * "storage" first, so the tank can still be pulled out by hand
 * (esptool read_flash 0xA90000 0x6000 rescue.bin). Any other error - a
 * newer IDF's format after a rollback, say - leaves the flash alone: the tank
 * runs without saving until a build that can read it is back. */
#define NVS_RESCUE_SIZE 0x6000
static void nvs_start(void) {
    esp_err_t e = nvs_flash_init();
    if (e == ESP_OK) return;
    if (e != ESP_ERR_NVS_NO_FREE_PAGES) {
        ESP_LOGE(TAG, "NVS init: %s - running WITHOUT saving; the saved tank is left as it is", esp_err_to_name(e));
        return;
    }
    const esp_partition_t *nvs = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_NVS, NULL);
    const esp_partition_t *st  = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "storage");
    uint8_t *raw = nvs && nvs->size <= NVS_RESCUE_SIZE ? heap_caps_malloc(nvs->size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) : NULL;
    bool kept = raw && st && st->size >= NVS_RESCUE_SIZE
             && esp_partition_read(nvs, 0, raw, nvs->size) == ESP_OK
             && esp_partition_erase_range(st, 0, NVS_RESCUE_SIZE) == ESP_OK
             && esp_partition_write(st, 0, raw, nvs->size) == ESP_OK;
    free(raw);
    if (!kept) {
        ESP_LOGE(TAG, "NVS init: no free pages and no rescue copy - running WITHOUT saving, nothing erased");
        return;
    }
    ESP_LOGE(TAG, "NVS init: no free pages - raw copy at storage+0 (0x%lx), erasing NVS", (unsigned long)st->address);
    nvs_flash_erase();
    nvs_flash_init();
}

/* CHECK FOR UPDATES (the updates page, or the director's `ota check`): the
 * tank is saved as it is before sleep, then the board restarts into update
 * mode (update_mode.c) - the radio never runs beside the tank */
static void request_update(void) {
    touch_port_confirm_answer(-1);
    if (!progression_save(&tank)) {
        ESP_LOGE(TAG, "Wi-Fi update canceled: could not save aquarium");
        updates_sd_status("SAVE FAILED - WIFI NOT OPENED", false);
        return;
    }
    update_mode_request();                   /* restarts */
}
void device_update_check(void) { request_update(); }
void device_wifi_setup(void) {
    if (progression_save(&tank)) update_mode_wifi_request();
    else { ESP_LOGE(TAG, "Wi-Fi setup canceled: could not save aquarium"); render_backup_status("SAVE FAILED - WIFI NOT OPENED", false); }
}
/* the installer page asked a RUNNING tank for the networks (its Connect to /
 * Change Wi-Fi): saved the same way, then the restart into provisioning mode */
void device_provision_request(void) {
    touch_port_confirm_answer(-1);
    progression_save(&tank);

    provision_mode_request();                /* restarts */
}

static bool reserve_tank_scratch(void) {
    void *decor = heap_caps_calloc(1, render_decor_scratch_size(), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!decor) {
        ESP_LOGE(TAG, "cannot reserve CYD decoration scratch (%u B)", (unsigned)render_decor_scratch_size());
        return false;
    }
    uint32_t *masks = heap_caps_malloc(render_mask_scratch_size(), MALLOC_CAP_INTERNAL | MALLOC_CAP_32BIT);
    if (!masks) {
        heap_caps_free(decor);
        ESP_LOGE(TAG, "cannot reserve CYD word masks (%u B)", (unsigned)render_mask_scratch_size());
        return false;
    }
    render_set_decor_scratch(decor);
    render_set_mask_scratch(masks);
    return true;
}
void app_main(void) {
    ESP_LOGI(TAG, "pocket-tank v%s %s (build %s) for %s boot%s", PT_RELEASE, PT_RELEASE_STAGE, version_port_string(), PT_BOARD,
             esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT0 || esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT1 ? " (woken by button)" : "");
    gpio_config_t btn = { .pin_bit_mask = 1ULL << BTN_SLEEP, .mode = GPIO_MODE_INPUT,
                          .pull_up_en = GPIO_PULLUP_ENABLE };
    gpio_config(&btn);
    gpio_deep_sleep_hold_dis();              /* a deep-sleep wake is a boot: the night's pad holds end here */
    nvs_start();
    director_early();                        /* the installer page's handshake, answered from here on (director.c) */
    brightness_init();
    for (int i = 0; i < PLAN_FB_COUNT; i++) {
        fb[i] = heap_caps_aligned_alloc(64, PLAN_FB_ALLOC, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
        if (!fb[i]) fb[i] = i ? fb[0] : NULL;          /* no PSRAM: share or skip */
    }
    if (!fb[0]) ESP_LOGW(TAG, "no framebuffer RAM: rendering disabled (tank still runs)");
    bool network_mode = update_mode_pending();
    /* Network-only pages do not render fish/decor. Leave their scratch free for TLS. */
    if (!network_mode && !reserve_tank_scratch()) return;
    /* Network modes must run before SD/cache allocations consume Wi-Fi headroom. */
    render_clock_us = esp_timer_get_time;    /* per-stage frame profiling in the display log */
    if (network_mode) display_port_set_brightness(0);
    display_port_init();
    ambient_init();
    touch_port_init();
    weather_init();
    if (network_mode) update_mode_run(fb[0]);
    else if (provision_mode_wanted()) provision_mode_run(fb[0]);   /* just installed, no network yet: the page's Wi-Fi step, the glass dark */
    net_clock_boot(fb[0]);            /* no RTC chip and a saved network: the time from the internet, before the advisor takes the heap */
    if (network_mode && !reserve_tank_scratch()) return;
    sd_port_init();
    ESP_LOGI(TAG, "CYD build: rules advisor (no model partition or PSRAM)");
    ESP_LOGI(TAG, "after the advisor: internal heap %u KB free", (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024);
    /* CYD has no scene cache; its dirty/card caches are unused. Keep RAM for Wi-Fi/TLS. */
    render_set_dirty_mask(NULL);
    render_set_card_cache(NULL);
    director_init();                  /* serial scenario console (filming / bench) */
    tank_init(&tank, (uint32_t)esp_timer_get_time() ^ 0xC0FFEEu);
    esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
    bool from_sleep = cause == ESP_SLEEP_WAKEUP_EXT0 || cause == ESP_SLEEP_WAKEUP_EXT1 || cause == ESP_SLEEP_WAKEUP_TIMER;
    if (from_sleep) {                        /* the night, lived through in one step */
        float h = progression_wake(&tank, clock_port_now_unix());
        s_boot_lived_h = h;
        ESP_LOGI(TAG, "wake from deep sleep (%s): %s%.1f h simulated | hunger[0] %.1f",
                 cause == ESP_SLEEP_WAKEUP_TIMER ? "timer" : cause == ESP_SLEEP_WAKEUP_EXT1 ? "the PWR key" : "BOOT", h < 0 ? "no clock, " : "", h < 0 ? 0.0f : h,
                 tank.n_fish ? tank.fish[0].hunger : 0.0f);

        net_clock_rebase();
        int put_back = restore_fish();       /* where they fell asleep, on the goal they had */
        ESP_LOGI(TAG, "wake: %d of %d fish put back where they were", put_back, tank.n_fish);
    } else {                                 /* a cold boot - power-on, a flash, a PMIC power-off, a cell that died: the absence is lived through just the same (2026-09-16) */
        float h = progression_boot(&tank);
        s_boot_lived_h = h;
        ESP_LOGI(TAG, "cold boot: %s%.1f h lived through since the save | hunger[0] %.1f",
                 h < 0 ? "no save or no clock, " : "", h < 0 ? 0.0f : h,
                 tank.n_fish ? tank.fish[0].hunger : 0.0f);
        net_clock_rebase();
        if (clock_port_now_unix() == 0) clock_port_seed(progression_loaded_unix());   /* a clockless board after a power cut: time resumes at the save */
    }
    if (!progression_storage_ready()) {
        ESP_LOGE(TAG, "Profile unavailable; saves blocked. Existing data retained; check storage before retrying");
        render_backup_status("SAVE UNAVAILABLE - CHECK STORAGE", false);
    }
    notice_sync(&tank);                      /* what is already earned stays unannounced */
    { uint32_t r = progression_loaded_release();   /* which release wrote the tank we just loaded */
      if (r) ESP_LOGI(TAG, "the save was written by v%d.%d.%d", (int)(r >> 16), (int)(r >> 8 & 255), (int)(r & 255));
      else ESP_LOGI(TAG, "the save predates release numbers (or there was none)");
      if (r && r < (uint32_t)PT_RELEASE_NUM) notice_updated();   /* the first boot of a new release: its card, once (the next save stamps this release) */ }
    /* the save loaded under this image: it is good - the bootloader's
       rollback (a fresh image boots "pending" once) stands down. Before the
       first save write, so an older build never meets a newer save it
       cannot read (it reads its prefix anyway: persist_port_esp.c) */
    { esp_ota_img_states_t st; const esp_partition_t *run = esp_ota_get_running_partition();
      if (progression_storage_ready() && run && esp_ota_get_state_partition(run, &st) == ESP_OK && st == ESP_OTA_IMG_PENDING_VERIFY) {
          esp_ota_mark_app_valid_cancel_rollback();
          ESP_LOGI(TAG, "first boot of this image from %s: marked valid, rollback cancelled", run->label); } }
    ESP_LOGI(TAG, "population %d (cap %d): %s + %s ...", tank.n_fish, POP_CAP,
             tank.fish[0].name, tank.n_fish > 1 ? tank.fish[1].name : "-");
    if (progression_setup_pending()) {           /* a new tank (fresh install, or a reset mid-flow): the welcome */
        setup_begin(&tank);
        ESP_LOGI(TAG, "first-run setup: welcome, names, colours (director `setup off` drops it)");
    }
    if (network_mode && update_mode_wifi_only() && !progression_setup_pending()) {
        render_settings_open();
        touch_port_show_settings(true);
        ESP_LOGI(TAG, "Wi-Fi setup closed: returning to Settings");
    }
    /* the tank task starts from a one-shot timer, AFTER app_main has returned
       and its own 16 KB stack is back in the internal heap (2026-09-30: with
       the Wi-Fi stack linked, creating it from here found 11 KB free and the
       tank never started) */
    ESP_LOGI(TAG, "app_main done: internal heap %u KB free (largest block %u KB); the tank task starts once this stack is freed",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024, (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) / 1024);
    const esp_timer_create_args_t st = { .callback = start_tank_task, .name = "tank-start" };
    esp_timer_handle_t th; esp_timer_create(&st, &th); esp_timer_start_once(th, 200000);
}
