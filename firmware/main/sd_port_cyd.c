#include "sd_port_cyd.h"
#include "icons.h"
#include "icons_sd_bank.h"
#include "driver/sdspi_host.h"
#include "esp_vfs_fat.h"
#include "esp_crc.h"
#include "esp_log.h"
#include "sdmmc_cmd.h"
#include "progression.h"
#include "companion.h"
#include "aquarium_store.h"
#include "esp_random.h"
#include "render.h"
#include "esp_system.h"
#include "esp_ota_ops.h"
#include "esp_timer.h"
#include "psa/crypto.h"
#include "driver/uart.h"
#include "update.h"
#include "version.h"
#include "touch_port.h"
#include "ambient.h"
#include "weather.h"
#include "setup.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>

static const char *TAG = "sd";
#define BANK_PATH "/sdcard/" CYD_ICON_FILE
#define TEMP_PATH BANK_PATH ".upload"
static bool s_mounted;
static FILE *s_upload;
static bool s_bank;
static size_t s_uploaded;
static uint32_t s_upload_crc;
static FILE *s_photo;
typedef enum { AUTO_IDLE, AUTO_RUNNING, AUTO_DONE } auto_state_t;
static portMUX_TYPE s_auto_mux = portMUX_INITIALIZER_UNLOCKED;
static auto_state_t s_auto_state;

bool sd_port_auto_backup_busy(void) {
    portENTER_CRITICAL(&s_auto_mux);
    bool busy = s_auto_state != AUTO_IDLE;
    portEXIT_CRITICAL(&s_auto_mux);
    return busy;
}

static bool verify_bank(FILE *file) {
    uint8_t buffer[512];
    uint32_t crc = 0; size_t total = 0, n;
    rewind(file);
    while ((n = fread(buffer, 1, sizeof buffer, file)) != 0) {
        crc = esp_crc32_le(crc, buffer, n); total += n;
    }
    bool ok = !ferror(file) && total == CYD_ICON_BYTES && crc == CYD_ICON_CRC;
    rewind(file);
    return ok;
}
static bool open_bank(void) {
    if (s_bank) return true;
    FILE *file = fopen(BANK_PATH, "rb");
    if (!file) {
        ESP_LOGW(TAG, "icon bank unavailable (%s): %s", BANK_PATH, strerror(errno)); return false;
    }
    if (!verify_bank(file)) {
        ESP_LOGE(TAG, "icon bank size/CRC mismatch; file left unchanged");
        fclose(file); return false;
    }
    fclose(file); s_bank = true;
    ESP_LOGI(TAG, "icon bank verified: %u bytes CRC32 %08lx", CYD_ICON_BYTES, (unsigned long)CYD_ICON_CRC);
    return true;
}
bool sd_port_init(void) {
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = SPI3_HOST; host.max_freq_khz = 10000;
    spi_bus_config_t bus = {
        .mosi_io_num = 23, .miso_io_num = 19, .sclk_io_num = 18,
        .quadwp_io_num = -1, .quadhd_io_num = -1, .max_transfer_sz = 4096,
    };
    esp_err_t err = spi_bus_initialize(SPI3_HOST, &bus, SPI_DMA_CH_AUTO);
    if (err != ESP_OK) { ESP_LOGE(TAG, "SD bus: %s", esp_err_to_name(err)); return false; }
    sdspi_device_config_t device = SDSPI_DEVICE_CONFIG_DEFAULT();
    device.host_id = SPI3_HOST; device.gpio_cs = 5;
    esp_vfs_fat_sdmmc_mount_config_t mount = {
        .format_if_mount_failed = false, .max_files = 3, .allocation_unit_size = 16 * 1024,
    };
    sdmmc_card_t *card;
    err = esp_vfs_fat_sdspi_mount("/sdcard", &host, &device, &mount, &card);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SD mount failed: %s; card NOT formatted", esp_err_to_name(err));
        /* Keep SPI3 allocated: ESP32 DMA teardown also stops the live SPI2 LCD. */
        return false;
    }
    s_mounted = true;
    ESP_LOGI(TAG, "SD mounted: %llu MiB", (unsigned long long)card->csd.capacity * card->csd.sector_size / (1024 * 1024));
    ESP_LOGI(TAG, "menu artwork embedded in flash; no SD icon cache allocated");
    return true;
}
const icon_t *icons_resolve(const icon_t *icon) {
    if (!icon || !icon->rgb || !icon->a) { ESP_LOGE(TAG, "invalid embedded icon"); abort(); }
    return icon;
}
#define SAVE_DIR "/sdcard/pocket-tank"
static bool legacy_aquarium_ready(void) {
    if (!s_mounted || s_upload || s_photo || sd_port_auto_backup_busy()) {
        ESP_LOGE(TAG, "legacy import unavailable: SD unavailable/busy"); return false;
    }
    if (mkdir(SAVE_DIR, 0755) != 0 && errno != EEXIST) {
        ESP_LOGE(TAG, "aquarium directory: %s", strerror(errno)); return false;
    }
    if (mkdir(SAVE_DIR "/tanks", 0755) != 0 && errno != EEXIST) {
        ESP_LOGE(TAG, "tank directory: %s", strerror(errno)); return false;
    }
    return true;
}
void aquarium_port_id(uint32_t id[2]) { esp_fill_random(id, 2 * sizeof *id); }
bool sd_port_aquarium_read(const uint32_t id[2], int slot, void *data, size_t size) {
    if (!legacy_aquarium_ready()) {
        ESP_LOGE(TAG, "Legacy tank import needs the original SD card");
        return false;
    }
    return aquarium_store_read(SAVE_DIR "/tanks", id, slot, data, size);
}
bool sd_port_aquarium_read_sized(const uint32_t id[2], int slot, void *data, size_t size, size_t *got) {
    if (got) *got = 0;
    if (!legacy_aquarium_ready()) {
        ESP_LOGE(TAG, "Legacy tank import needs the original SD card");
        return false;
    }
    return aquarium_store_read_sized(SAVE_DIR "/tanks", id, slot, data, size, got);
}
static bool backup_latest(const char *prefix, char path[96], unsigned *number) {
    DIR *dir = opendir(SAVE_DIR);
    if (!dir) {
        if (errno != ENOENT) { ESP_LOGE(TAG, "backup directory: %s", strerror(errno)); return false; }
        if (mkdir(SAVE_DIR, 0755) != 0) { ESP_LOGE(TAG, "create backup directory: %s", strerror(errno)); return false; }
        dir = opendir(SAVE_DIR);
        if (!dir) { ESP_LOGE(TAG, "open backup directory: %s", strerror(errno)); return false; }
    }
    unsigned latest = 0;
    struct dirent *entry;
    errno = 0;
    while ((entry = readdir(dir)) != NULL) {
        unsigned n; char tail;
        char pattern[48]; snprintf(pattern, sizeof pattern, "%s-%%4u.json%%c", prefix);
        if (sscanf(entry->d_name, pattern, &n, &tail) != 1 || !n || n > 9999) continue;
        char exact[48]; snprintf(exact, sizeof exact, "%s-%04u.json", prefix, n);
        if (!strcmp(exact, entry->d_name) && n > latest) latest = n;
    }
    int err = errno;
    closedir(dir);
    if (err) { ESP_LOGE(TAG, "read backup directory: %s", strerror(err)); return false; }
    *number = latest;
    snprintf(path, 96, SAVE_DIR "/%s-%04u.json", prefix, latest);
    return true;
}
static char *backup_read(const char *path, size_t *length) {
    FILE *file = fopen(path, "rb");
    if (!file) { ESP_LOGE(TAG, "read %s: %s", path, strerror(errno)); return NULL; }
    char *text = NULL;
    if (fseek(file, 0, SEEK_END) != 0) goto done;
    long size = ftell(file);
    if (size <= 0 || size > 32768 || fseek(file, 0, SEEK_SET) != 0) goto done;
    text = malloc((size_t)size + 1);
    if (!text) goto done;
    if (fread(text, 1, (size_t)size, file) != (size_t)size || ferror(file)) { free(text); text = NULL; goto done; }
    text[size] = 0; *length = (size_t)size;
done:
    if (fclose(file) != 0) { free(text); text = NULL; }
    if (!text) ESP_LOGE(TAG, "backup read failed (size, memory, or SD I/O): %s", path);
    return text;
}
static bool backup_write(const tank_t *tank, const char *prefix, char path[96]) {
    render_work_caches(false);
    unsigned n;
    if (!backup_latest(prefix, path, &n) || n >= 9999) { ESP_LOGE(TAG, "backup slots unavailable"); return false; }
    snprintf(path, 96, SAVE_DIR "/%s-%04u.json", prefix, n + 1);
    char temp[104]; snprintf(temp, sizeof temp, "%s.tmp", path);
    struct stat st;
    if (stat(temp, &st) == 0 || stat(path, &st) == 0) { ESP_LOGE(TAG, "backup destination exists; refusing overwrite"); return false; }
    char *text = progression_backup_json(tank);
    if (!text) { ESP_LOGE(TAG, "not enough memory for JSON backup"); return false; }
    size_t len = strlen(text);
    uint32_t crc = esp_crc32_le(0, (const uint8_t *)text, len);
    FILE *file = fopen(temp, "wb");
    if (!file) { ESP_LOGE(TAG, "create backup: %s", strerror(errno)); free(text); return false; }
    bool ok = fwrite(text, 1, len, file) == len;
    if (fflush(file) != 0) ok = false;
    if (fsync(fileno(file)) != 0) ok = false;
    if (fclose(file) != 0) ok = false;
    free(text);
    char error[96] = "write, flush or SD readback failed"; size_t read_len = 0;
    char *readback = ok ? backup_read(temp, &read_len) : NULL;
    if (readback && (read_len != len || esp_crc32_le(0, (const uint8_t *)readback, read_len) != crc))
        snprintf(error, sizeof error, "readback length/CRC mismatch");
    ok = readback && read_len == len && esp_crc32_le(0, (const uint8_t *)readback, read_len) == crc
        && progression_restore_json(readback, read_len, false, error, sizeof error);
    free(readback);
    if (ok) ok = rename(temp, path) == 0;
    if (!ok) {
        ESP_LOGE(TAG, "backup write/readback validation failed: %s; previous backups untouched", error);
        if (remove(temp) != 0) ESP_LOGW(TAG, "could not remove incomplete backup: %s", strerror(errno));
    } else ESP_LOGI(TAG, "JSON BACKUP verified: %s (%u bytes)", path, (unsigned)len);
    return ok;
}
static void backup_action(tank_t *tank, bool restore, bool confirmed, const char *prefix, unsigned requested) {
    render_work_caches(false);
    if (!tank || !s_mounted || s_upload || s_photo || sd_port_auto_backup_busy()) {
        ESP_LOGE(TAG, "backup unavailable: no tank/card or SD operation active");
        render_backup_status("SD UNAVAILABLE / BUSY", false); return;
    }
    if (!restore) {
        char path[96];
        bool ok = backup_write(tank, "backup", path);
        if (ok && !companion_backup_record(tank)) {
            ESP_LOGE(TAG, "Backup verified but last-backup status could not be saved");
            render_backup_status("SD BACKUP OK; STATUS SAVE FAILED", false);
        } else render_backup_status(ok ? "BACKUP SAVED ON SD" : "BACKUP FAILED - SEE SERIAL", false);
        return;
    }
    char path[96]; unsigned n;
    if (!backup_latest(prefix, path, &n) || (!n && !requested)) {
        ESP_LOGE(TAG, "no JSON backup available"); render_backup_status("NO BACKUP FOUND ON SD", false); return;
    }
    if (requested) snprintf(path, sizeof path, SAVE_DIR "/%s-%04u.json", prefix, requested);
    size_t len = 0; char error[96];
    char *text = backup_read(path, &len);
    if (!text || !progression_restore_json(text, len, false, error, sizeof error)) {
        ESP_LOGE(TAG, "restore rejected: %s", text ? error : "SD read failed");
        free(text); render_backup_status("INVALID BACKUP - NOT RESTORED", false); return;
    }
    if (!confirmed) {
        char name[16], preview[64]; uint32_t release;
        if (!progression_backup_preview(text, len, name, &release)) {
            free(text); render_backup_status("PREVIEW FAILED - NOT RESTORED", false); return;
        }
        snprintf(preview, sizeof preview, "RESTORE %.15s v%u.%u.%u? TAP AGAIN",
                 name, (unsigned)(release >> 16), (unsigned)((release >> 8) & 255), (unsigned)(release & 255));
        free(text); render_backup_status(preview, true); return;
    }
    uint32_t crc = esp_crc32_le(0, (const uint8_t *)text, len);
    size_t original_len = len;
    free(text);
    char safety[96];
    if (!backup_write(tank, "before-restore", safety)) {
        render_backup_status("SAFETY BACKUP FAILED", false); return;
    }
    /* Do not retain a full bank JSON buffer while exporting its safety snapshot. */
    text = backup_read(path, &len);
    if (!text || len != original_len || esp_crc32_le(0, (const uint8_t *)text, len) != crc) {
        free(text); ESP_LOGE(TAG, "restore snapshot changed or could not be reread");
        render_backup_status("BACKUP READ FAILED - NOT RESTORED", false); return;
    }
    bool ok = progression_restore_json(text, len, true, error, sizeof error);
    free(text);
    if (!ok) {
        ESP_LOGE(TAG, "restore failed: %s", error); render_backup_status("RESTORE FAILED - SEE SERIAL", false); return;
    }
    ESP_LOGI(TAG, "JSON RESTORE committed: %s; pre-restore snapshot: %s; restarting", path, safety);
    vTaskDelay(pdMS_TO_TICKS(100));
    esp_restart();
}
void sd_port_backup_action(tank_t *tank, bool restore, bool confirmed) {
    backup_action(tank, restore, confirmed, "backup", 0);
}

/* 1.3 daily automatic backup: once per UTC day (the wall clock, or the clock
 * resumed from the save), "auto-NNNN.json" verified like a manual backup;
 * only the newest AUTO_KEEP stay. The day is kept in NVS "tank"/"autobk_day". */
#define AUTO_KEEP 7
static float s_auto_wait = 90;          /* settle after boot before the first try */
static uint32_t s_auto_day;
static bool s_auto_day_loaded;
typedef struct {
    char *text;
    size_t length;
    uint32_t crc, day, id[2];
    uint8_t active, mode, chosen;
    bool ok, diagnostic;
} auto_job_t;
static auto_job_t s_auto_job;
static struct {
    uint32_t id[2];
    uint8_t active, mode, chosen;
    bool pending, diagnostic;
} s_auto_request;
static void auto_prune(unsigned newest) {
    DIR *dir = opendir(SAVE_DIR);
    if (!dir) return;
    struct dirent *entry; unsigned doomed[AUTO_KEEP * 2]; int n_doomed = 0;
    while ((entry = readdir(dir)) != NULL && n_doomed < AUTO_KEEP * 2) {
        unsigned n; char exact[24];
        if (sscanf(entry->d_name, "auto-%4u.json", &n) != 1 || n > 9999) continue;
        snprintf(exact, sizeof exact, "auto-%04u.json", n);
        if (!strcmp(exact, entry->d_name) && n + AUTO_KEEP <= newest) doomed[n_doomed++] = n;
    }
    closedir(dir);
    for (int i = 0; i < n_doomed; i++) {
        char path[64]; snprintf(path, sizeof path, SAVE_DIR "/auto-%04u.json", doomed[i] % 10000);
        if (remove(path) != 0) ESP_LOGW(TAG, "auto backup prune %s: %s", path, strerror(errno));
        else ESP_LOGI(TAG, "auto backup pruned: %s", path);
    }
}
/* The worker owns only immutable JSON, never a tank pointer or the restore
 * validator (which reads the current save and shared identity state). */
static bool auto_store(const auto_job_t *job) {
    int64_t started = esp_timer_get_time();
    char path[96], temp[104]; unsigned latest;
    const char *prefix = job->diagnostic ? "auto-test" : "auto";
    if (!backup_latest(prefix, path, &latest) || latest >= 9999) {
        ESP_LOGE(TAG, "auto backup slots unavailable"); return false;
    }
    snprintf(path, sizeof path, SAVE_DIR "/%s-%04u.json", prefix, latest + 1);
    snprintf(temp, sizeof temp, "%s.tmp", path);
    struct stat st;
    if (stat(temp, &st) == 0 || errno != ENOENT) {
        ESP_LOGE(TAG, "auto backup temporary destination exists or cannot be checked"); return false;
    }
    if (stat(path, &st) == 0 || errno != ENOENT) {
        ESP_LOGE(TAG, "auto backup destination exists or cannot be checked"); return false;
    }
    FILE *file = fopen(temp, "wb");
    if (!file) { ESP_LOGE(TAG, "create auto backup: %s", strerror(errno)); return false; }
    bool ok = true;
    for (size_t offset = 0; ok && offset < job->length;) {
        size_t n = job->length - offset;
        if (n > 512) n = 512;
        ok = fwrite(job->text + offset, 1, n, file) == n;
        offset += n;
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    if (fflush(file) != 0) ok = false;
    if (fsync(fileno(file)) != 0) ok = false;
    if (fclose(file) != 0) ok = false;
    file = ok ? fopen(temp, "rb") : NULL;
    ok = file != NULL;
    uint8_t buffer[512]; size_t offset = 0; uint32_t crc = 0;
    while (ok && offset < job->length) {
        size_t n = job->length - offset;
        if (n > sizeof buffer) n = sizeof buffer;
        ok = fread(buffer, 1, n, file) == n &&
             !memcmp(buffer, job->text + offset, n);
        if (ok) { crc = esp_crc32_le(crc, buffer, n); offset += n; }
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    if (file) {
        ok = ok && offset == job->length && crc == job->crc &&
             fgetc(file) == EOF && !ferror(file);
        if (fclose(file) != 0) ok = false;
    }
    if (ok && rename(temp, path) != 0) {
        ESP_LOGE(TAG, "auto backup rename: %s", strerror(errno)); ok = false;
    }
    if (!ok) {
        ESP_LOGE(TAG, "auto backup write/flush/readback failed; previous backups untouched");
        if (remove(temp) != 0) ESP_LOGW(TAG, "auto backup cleanup: %s", strerror(errno));
        return false;
    }
    ESP_LOGI(TAG, "JSON AUTO BACKUP verified: %s (%u bytes)", path, (unsigned)job->length);
    if (job->diagnostic) {
        if (remove(path) != 0) {
            ESP_LOGE(TAG, "auto backup diagnostic cleanup: %s", strerror(errno)); return false;
        }
    } else auto_prune(latest + 1);
    ESP_LOGI(TAG, "auto backup worker storage/readback%s: %lld us",
             job->diagnostic ? " diagnostic" : "", (long long)(esp_timer_get_time() - started));
    return true;
}
static void auto_worker(void *arg) {
    auto_job_t *job = arg;
    job->ok = auto_store(job);
    free(job->text); job->text = NULL;
    portENTER_CRITICAL(&s_auto_mux);
    s_auto_state = AUTO_DONE;
    portEXIT_CRITICAL(&s_auto_mux);
    vTaskDelete(NULL);
}
static bool auto_network_busy(void) {
    const update_manifest_t *manifest = NULL; int percent = 0;
    return weather_network_needed() || net_port_weather_active() || net_port_connected() ||
        setup_active() || update_active() ||
        net_port_scan_state() == NET_BUSY || net_port_connect_state() == NET_BUSY ||
        net_port_check_state(&manifest) == NET_BUSY || net_port_install_state(&percent) == NET_BUSY;
}
static bool auto_start(tank_t *tank, uint32_t day, bool diagnostic) {
    if (!tank || !s_mounted || s_upload || s_photo || sd_port_auto_backup_busy()) {
        ESP_LOGW(TAG, "auto backup skipped: SD unavailable/busy"); return false;
    }
    if (auto_network_busy()) {
        ESP_LOGW(TAG, "auto backup deferred: network/setup owns working memory"); return false;
    }
    render_work_caches(false);
    int64_t started = esp_timer_get_time();
    char *text = progression_backup_json(tank);
    int64_t captured = esp_timer_get_time();
    char error[96] = "not enough memory for JSON snapshot";
    size_t length = text ? strlen(text) : 0;
    /* Validate against the live identity domain here, before handing off. Exact
     * byte readback then proves the worker stored this validated snapshot. */
    if (!text || !progression_restore_json(text, length, false, error, sizeof error)) {
        free(text); ESP_LOGE(TAG, "auto backup snapshot rejected: %s", error); return false;
    }
    ESP_LOGI(TAG, "auto backup tank-task snapshot: %lld us; semantic/identity validation: %lld us",
             (long long)(captured - started), (long long)(esp_timer_get_time() - captured));
    s_auto_job = (auto_job_t){
        .text = text, .length = length,
        .crc = esp_crc32_le(0, (const uint8_t *)text, length), .day = day,
        .id = {tank->aquariums.id[0], tank->aquariums.id[1]},
        .active = tank->aquariums.active, .mode = tank->profile.mode, .chosen = tank->profile.chosen,
        .diagnostic = diagnostic,
    };
    portENTER_CRITICAL(&s_auto_mux);
    s_auto_state = AUTO_RUNNING;
    portEXIT_CRITICAL(&s_auto_mux);
    /* Share CPU1 below the LCD worker, rather than starving behind CPU0's
     * mostly CPU-bound tank task. SD uses SPI3; the LCD owns SPI2. */
    if (xTaskCreatePinnedToCore(auto_worker, "sd_backup", 4096, &s_auto_job,
                                tskIDLE_PRIORITY + 1, NULL, 1) != pdPASS) {
        free(text); s_auto_job.text = NULL;
        portENTER_CRITICAL(&s_auto_mux);
        s_auto_state = AUTO_IDLE;
        portEXIT_CRITICAL(&s_auto_mux);
        ESP_LOGE(TAG, "auto backup worker allocation failed; retrying later"); return false;
    }
    ESP_LOGI(TAG, "auto backup snapshot queued (%u bytes)", (unsigned)length);
    return true;
}
/* Commands run below the director's multi-kilobyte frame. Never capture or
 * validate there: even a successful return can have overwritten the stack guard. */
static bool auto_request(tank_t *tank, bool diagnostic) {
    if (!tank || !s_mounted || s_upload || s_photo || sd_port_auto_backup_busy() ||
        s_auto_request.pending || auto_network_busy()) {
        ESP_LOGE(TAG, "auto backup request rejected: SD/network busy or unavailable"); return false;
    }
    s_auto_request.id[0] = tank->aquariums.id[0];
    s_auto_request.id[1] = tank->aquariums.id[1];
    s_auto_request.active = tank->aquariums.active;
    s_auto_request.mode = tank->profile.mode;
    s_auto_request.chosen = tank->profile.chosen;
    s_auto_request.diagnostic = diagnostic;
    s_auto_request.pending = true;
    ESP_LOGI(TAG, "auto backup request queued for tank service");
    return true;
}
bool sd_port_auto_backup_now(tank_t *tank) {
    return auto_request(tank, false);
}
static void auto_complete(tank_t *tank) {
    portENTER_CRITICAL(&s_auto_mux);
    bool done = s_auto_state == AUTO_DONE;
    portEXIT_CRITICAL(&s_auto_mux);
    if (!done) return;
    bool same = tank && tank->aquariums.id[0] == s_auto_job.id[0] &&
        tank->aquariums.id[1] == s_auto_job.id[1] && tank->aquariums.active == s_auto_job.active &&
        tank->profile.mode == s_auto_job.mode && tank->profile.chosen == s_auto_job.chosen;
    s_auto_wait = s_auto_job.ok ? 60 : 3600;
    if (!s_auto_job.ok) ESP_LOGE(TAG, "auto backup FAILED; retrying in an hour");
    else if (!same) ESP_LOGW(TAG, "auto backup saved; tank changed, status/day not applied");
    else if (!s_auto_job.diagnostic) {
        if (!companion_backup_record(tank)) ESP_LOGE(TAG, "auto backup verified but its status could not be saved");
        if (s_auto_job.day) {
            nvs_handle_t h;
            esp_err_t err = nvs_open("tank", NVS_READWRITE, &h);
            if (err == ESP_OK) {
                err = nvs_set_u32(h, "autobk_day", s_auto_job.day);
                if (err == ESP_OK) err = nvs_commit(h);
                nvs_close(h);
            }
            if (err == ESP_OK) s_auto_day = s_auto_job.day;
            else {
                ESP_LOGE(TAG, "auto backup verified but day not saved: %s", esp_err_to_name(err));
                s_auto_wait = 3600;
            }
        }
    }
    portENTER_CRITICAL(&s_auto_mux);
    s_auto_state = AUTO_IDLE;
    portEXIT_CRITICAL(&s_auto_mux);
}
void sd_port_auto_backup_service(tank_t *tank, float dt, bool allow_start) {
    auto_complete(tank);
    if (s_auto_request.pending) {
        if (!tank || tank->aquariums.id[0] != s_auto_request.id[0] ||
            tank->aquariums.id[1] != s_auto_request.id[1] ||
            tank->aquariums.active != s_auto_request.active ||
            tank->profile.mode != s_auto_request.mode || tank->profile.chosen != s_auto_request.chosen) {
            s_auto_request.pending = false;
            ESP_LOGE(TAG, "auto backup request canceled: tank identity changed");
        } else {
            if (!allow_start || sd_port_auto_backup_busy() || s_upload || s_photo ||
                weather_network_needed_after(dt) || auto_network_busy()) return;
            bool diagnostic = s_auto_request.diagnostic;
            s_auto_request.pending = false;
            if (!auto_start(tank, 0, diagnostic)) s_auto_wait = 3600;
            return;
        }
    }
    if (!allow_start || !ambient_auto_backup() || !s_mounted || sd_port_auto_backup_busy()) return;
    s_auto_wait -= dt;
    if (s_auto_wait > 0) return;
    s_auto_wait = 60;
    int64_t now = clock_port_now_unix();
    if (now < 1600000000LL) return;
    uint32_t day = (uint32_t)(now / 86400);
    if (!s_auto_day_loaded) {
        nvs_handle_t h;
        esp_err_t err = nvs_open("tank", NVS_READONLY, &h);
        if (err == ESP_OK) { err = nvs_get_u32(h, "autobk_day", &s_auto_day); nvs_close(h); }
        if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
            ESP_LOGE(TAG, "auto backup day read: %s", esp_err_to_name(err)); s_auto_wait = 3600; return;
        }
        s_auto_day_loaded = true;
    }
    if (day == s_auto_day) return;
    if (s_upload || s_photo || weather_network_needed_after(dt) || auto_network_busy()) return;
    ESP_LOGI(TAG, "daily auto backup (day %lu)", (unsigned long)day);
    if (!auto_start(tank, day, false)) s_auto_wait = 3600;
}
void sd_port_auto_backup_tick(tank_t *tank, float dt) {
    sd_port_auto_backup_service(tank, dt, true);
}

/* 1.3 photo mode: the live tank, captured tile by tile during one full
 * repaint, as a top-down RGB565 BMP in pocket-tank/photos. */
#define PHOTO_DIR SAVE_DIR "/photos"
static int s_photo_rows;
static char s_photo_path[64];
static void put16(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void put32(uint8_t *p, uint32_t v) { put16(p, v); put16(p + 2, v >> 16); }
bool sd_port_photo_begin(int width, int height) {
    if (!s_mounted || s_upload || s_photo || sd_port_auto_backup_busy()) {
        ESP_LOGE(TAG, "photo unavailable: SD unavailable/busy"); return false;
    }
    if ((mkdir(SAVE_DIR, 0755) != 0 && errno != EEXIST) || (mkdir(PHOTO_DIR, 0755) != 0 && errno != EEXIST)) {
        ESP_LOGE(TAG, "photo directory: %s", strerror(errno)); return false;
    }
    unsigned latest = 0;
    DIR *dir = opendir(PHOTO_DIR);
    if (!dir) { ESP_LOGE(TAG, "photo directory: %s", strerror(errno)); return false; }
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        unsigned n;
        if (sscanf(entry->d_name, "photo-%4u.bmp", &n) == 1 && n > latest && n <= 9999) latest = n;
        if (sscanf(entry->d_name, "PHOTO-%4u.BMP", &n) == 1 && n > latest && n <= 9999) latest = n;
    }
    closedir(dir);
    if (latest >= 9999) { ESP_LOGE(TAG, "photo slots full"); return false; }
    snprintf(s_photo_path, sizeof s_photo_path, PHOTO_DIR "/photo-%04u.bmp", latest + 1);
    s_photo = fopen(s_photo_path, "wb");
    if (!s_photo) { ESP_LOGE(TAG, "create %s: %s", s_photo_path, strerror(errno)); return false; }
    uint32_t image = (uint32_t)width * height * 2;
    uint8_t header[66] = {'B', 'M'};
    put32(header + 2, sizeof header + image); put32(header + 10, sizeof header);
    put32(header + 14, 40); put32(header + 18, (uint32_t)width); put32(header + 22, (uint32_t)-height);
    put16(header + 26, 1); put16(header + 28, 16); put32(header + 30, 3); put32(header + 34, image);
    put32(header + 38, 2835); put32(header + 42, 2835);
    put32(header + 54, 0xf800); put32(header + 58, 0x07e0); put32(header + 62, 0x001f);
    s_photo_rows = 0;
    if (fwrite(header, 1, sizeof header, s_photo) != sizeof header) { sd_port_photo_end(false); return false; }
    return true;
}
bool sd_port_photo_rows(const uint16_t *rows, int first, int count, int width) {
    if (!s_photo || first != s_photo_rows) return false;
    if (fwrite(rows, sizeof *rows, (size_t)count * width, s_photo) != (size_t)count * width) return false;
    s_photo_rows += count;
    return true;
}
bool sd_port_photo_end(bool ok) {
    if (!s_photo) return false;
    if (fflush(s_photo) != 0 || fsync(fileno(s_photo)) != 0) ok = false;
    if (fclose(s_photo) != 0) ok = false;
    s_photo = NULL;
    if (!ok) {
        ESP_LOGE(TAG, "photo failed; removing %s", s_photo_path);
        remove(s_photo_path);
        render_backup_status("PHOTO FAILED - SEE SERIAL", false);
        return false;
    }
    ESP_LOGI(TAG, "PHOTO saved: %s (%d rows)", s_photo_path, s_photo_rows);
    char message[64]; snprintf(message, sizeof message, "PHOTO SAVED: %s", s_photo_path + strlen(SAVE_DIR) + 1);
    render_backup_status(message, false);
    return true;
}

#define UPDATE_PATH SAVE_DIR "/update.ptu"
#define UPDATE_TEMP UPDATE_PATH ".upload"
typedef struct {
    char magic[8], board[24];
    uint32_t size;
    uint8_t sha[32];
    char release[16];
} sd_update_header_t;
_Static_assert(sizeof(sd_update_header_t) == 84, "SD update package header");
static sd_update_header_t s_update_offer;
static int64_t s_update_deadline;

void sd_port_update_reset(void) {
    s_update_deadline = 0;
    updates_sd_status("COPY UPDATE.PTU TO POCKET-TANK", false);
}
static bool update_hash(FILE *file, uint32_t size, uint8_t digest[32], esp_ota_handle_t ota) {
    uint8_t buffer[1024];
    psa_hash_operation_t sha = PSA_HASH_OPERATION_INIT;
    bool ok = psa_crypto_init() == PSA_SUCCESS && psa_hash_setup(&sha, PSA_ALG_SHA_256) == PSA_SUCCESS;
    while (ok && size) {
        size_t n = size < sizeof buffer ? size : sizeof buffer;
        ok = fread(buffer, 1, n, file) == n && psa_hash_update(&sha, buffer, n) == PSA_SUCCESS;
        if (ok && ota) ok = esp_ota_write(ota, buffer, n) == ESP_OK;
        if (ok) size -= n;
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    size_t length = 0;
    if (ok) ok = !ferror(file) && fgetc(file) == EOF && !ferror(file) &&
        psa_hash_finish(&sha, digest, 32, &length) == PSA_SUCCESS && length == 32;
    psa_hash_abort(&sha);
    return ok;
}
static bool update_validate(FILE *file, sd_update_header_t *header, const esp_partition_t *slot) {
    uint8_t prefix[PT_BOARD_MARKER_OFFSET + 32], digest[32];
    struct stat st;
    if (!slot || fstat(fileno(file), &st) != 0 ||
        fread(header, 1, sizeof *header, file) != sizeof *header ||
        memcmp(header->magic, "PTCYDSD1", 8) ||
        memcmp(header->board, PT_BOARD, sizeof PT_BOARD) ||
        !header->release[0] || !memchr(header->release, 0, sizeof header->release) ||
        header->size < sizeof prefix || header->size > slot->size ||
        st.st_size != (off_t)(sizeof *header + header->size) ||
        fread(prefix, 1, sizeof prefix, file) != sizeof prefix ||
        prefix[0] != 0xe9 ||
        memcmp(prefix + PT_BOARD_MARKER_OFFSET, PT_BOARD_MAGIC, 8) ||
        memcmp(prefix + PT_BOARD_MARKER_OFFSET + 8, PT_BOARD, sizeof PT_BOARD) ||
        fseek(file, sizeof *header, SEEK_SET) != 0) return false;
    for (size_t i = 0; header->release[i]; i++)
        if (header->release[i] < 32 || header->release[i] > 126) return false;
    return update_hash(file, header->size, digest, 0) && !memcmp(digest, header->sha, sizeof digest);
}
static void update_error(const char *message) {
    s_update_deadline = 0;
    ESP_LOGE(TAG, "SD update: %s", message);
    updates_sd_status(message, false);
}
void sd_port_update_action(tank_t *tank) {
    touch_port_show_updates(true);
    if (!tank || !s_mounted || s_upload || s_photo || sd_port_auto_backup_busy()) { update_error("SD UNAVAILABLE / BUSY"); return; }
    FILE *file = fopen(UPDATE_PATH, "rb");
    if (!file) { update_error("NO POCKET-TANK/UPDATE.PTU"); return; }
    sd_update_header_t header;
    const esp_partition_t *slot = esp_ota_get_next_update_partition(NULL);
    if (!update_validate(file, &header, slot)) {
        fclose(file); update_error("INVALID PACKAGE / BOARD / HASH"); return;
    }
    if (esp_timer_get_time() >= s_update_deadline || memcmp(&header, &s_update_offer, sizeof header)) {
        fclose(file);
        s_update_offer = header; s_update_deadline = esp_timer_get_time() + 60000000;
        char message[48]; snprintf(message, sizeof message, "READY: %s - TAP INSTALL TO CONFIRM", header.release);
        updates_sd_status(message, true);
        ESP_LOGI(TAG, "SD update verified: %s, %lu bytes; tap again within 60 seconds", header.release, (unsigned long)header.size);
        return;
    }
    s_update_deadline = 0;
    char safety[96];
    if (!progression_save(tank) || !backup_write(tank, "before-update", safety)) {
        fclose(file); update_error("SAVE / SAFETY BACKUP FAILED"); return;
    }
    esp_ota_handle_t ota = 0;
    esp_err_t err = fseek(file, sizeof header, SEEK_SET) == 0 ? esp_ota_begin(slot, header.size, &ota) : ESP_FAIL;
    uint8_t digest[32];
    bool ok = err == ESP_OK && update_hash(file, header.size, digest, ota) && !memcmp(digest, header.sha, sizeof digest);
    if (fclose(file) != 0) ok = false;
    if (!ok) {
        if (ota) esp_ota_abort(ota);
        update_error("INSTALL FAILED - OLD SLOT KEPT"); return;
    }
    err = esp_ota_end(ota);
    if (err == ESP_OK) err = esp_ota_set_boot_partition(slot);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SD image validation/boot selection: %s", esp_err_to_name(err));
        update_error("IMAGE REJECTED - OLD SLOT KEPT"); return;
    }
    ESP_LOGI(TAG, "SD update installed in %s; safety backup %s; restarting", slot->label, safety);
    vTaskDelay(pdMS_TO_TICKS(100));
    esp_restart();
}

static void update_upload(unsigned long size) {
    const esp_partition_t *slot = esp_ota_get_next_update_partition(NULL);
    if (s_upload || !slot || size < sizeof(sd_update_header_t) || size > slot->size + sizeof(sd_update_header_t)) {
        update_error("INVALID UPLOAD SIZE / BUSY"); return;
    }
    struct stat st;
    if (stat(UPDATE_PATH, &st) == 0 || stat(UPDATE_TEMP, &st) == 0) {
        update_error("UPDATE FILE EXISTS - REMOVE IT FIRST"); return;
    }
    FILE *file = fopen(UPDATE_TEMP, "wb");
    if (!file) { update_error("CANNOT CREATE UPDATE FILE"); return; }
    ESP_LOGI(TAG, "SD UPDATE READY %lu", size);
    uint8_t buffer[256];
    unsigned long received = 0;
    bool ok = true;
    while (ok && received < size) {
        size_t want = size - received < sizeof buffer ? size - received : sizeof buffer;
        int n = uart_read_bytes(UART_NUM_0, buffer, want, pdMS_TO_TICKS(5000));
        ok = n > 0 && fwrite(buffer, 1, n, file) == (size_t)n;
        if (ok) { received += n; ESP_LOGI(TAG, "SD UPDATE ACK %lu", received); }
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    if (fflush(file) != 0 || fsync(fileno(file)) != 0) ok = false;
    if (fclose(file) != 0) ok = false;
    sd_update_header_t header;
    file = ok ? fopen(UPDATE_TEMP, "rb") : NULL;
    ok = file && update_validate(file, &header, slot);
    if (file && fclose(file) != 0) ok = false;
    if (ok) ok = rename(UPDATE_TEMP, UPDATE_PATH) == 0;
    if (!ok) {
        if (remove(UPDATE_TEMP) != 0) ESP_LOGW(TAG, "could not remove incomplete update: %s", strerror(errno));
        update_error("UPDATE UPLOAD FAILED"); return;
    }
    ESP_LOGI(TAG, "SD UPDATE UPLOADED verified");
}
void sd_port_command(tank_t *tank, int argc, char **argv) {
    if (!s_mounted) { ESP_LOGE(TAG, "SD FAIL not mounted"); return; }
    if (sd_port_auto_backup_busy() || s_photo) {
        ESP_LOGE(TAG, "SD FAIL automatic backup or photo active; retry later"); return;
    }
    if (s_upload && !(argc >= 2 && (!strcmp(argv[1], "cancel") ||
            !strcmp(argv[1], "chunk") || !strcmp(argv[1], "finish")))) {
        ESP_LOGE(TAG, "SD FAIL upload active; finish or cancel first"); return;
    }
    if (argc == 2 && !strcmp(argv[1], "auto-verify")) {
        if (!auto_request(tank, true)) ESP_LOGE(TAG, "SD automatic backup diagnostic not queued");
    }
    else if (argc == 2 && !strcmp(argv[1], "update")) sd_port_update_action(tank);
    else if (argc == 3 && !strcmp(argv[1], "update-upload")) {
        char *end; unsigned long size = strtoul(argv[2], &end, 10);
        if (!*argv[2] || *end) update_error("INVALID UPLOAD SIZE");
        else update_upload(size);
    }
    else if (argc >= 2 && !strcmp(argv[1], "backup")) sd_port_backup_action(tank, false, false);
    else if (argc == 2 && !strcmp(argv[1], "verify")) {
        if (!tank || !s_mounted || s_upload) { ESP_LOGE(TAG, "SD verification unavailable"); return; }
        char path[96] = "", safety[96] = "", error[96] = "";
        size_t len = 0;
        bool ok = backup_write(tank, "verify", path);
        ok = ok && backup_write(tank, "verify-safety", safety);
        char *text = ok ? backup_read(path, &len) : NULL;
        ok = text &&
             progression_restore_json(text, len, false, error, sizeof error);
        free(text);
        if (path[0] && remove(path) != 0 && errno != ENOENT) ESP_LOGE(TAG, "verify cleanup: %s", strerror(errno));
        if (safety[0] && remove(safety) != 0 && errno != ENOENT) ESP_LOGE(TAG, "verify safety cleanup: %s", strerror(errno));
        if (ok) ESP_LOGI(TAG, "SD backup and restore memory/I/O verified (%u bytes); tank unchanged", (unsigned)len);
        else ESP_LOGE(TAG, "SD verification failed: %s", error[0] ? error : "backup read/write/validation");
    }
    else if (argc >= 2 && (!strcmp(argv[1], "restore") || !strcmp(argv[1], "undo"))) {
        unsigned number = 0; bool confirmed = false, valid = true;
        for (int i = 2; i < argc; i++) {
            if (!strcmp(argv[i], "confirm") && !confirmed) confirmed = true;
            else {
                char *end; unsigned long parsed = strtoul(argv[i], &end, 10);
                if (!*argv[i] || *end || !parsed || parsed > 9999 || number) { valid = false; break; }
                number = (unsigned)parsed;
            }
        }
        if (!valid) { ESP_LOGE(TAG, "use sd restore|undo [1..9999] [confirm]"); return; }
        backup_action(tank, true, confirmed, !strcmp(argv[1], "undo") ? "before-restore" : "backup", number);
        if (!confirmed) ESP_LOGW(TAG, "add confirm to replace the tank; a pre-restore JSON snapshot is preserved");
    } else if (argc == 2 && !strcmp(argv[1], "export")) {
        char path[96]; unsigned n; size_t len = 0;
        if (!backup_latest("backup", path, &n) || !n) { ESP_LOGE(TAG, "no JSON backup available"); return; }
        char *text = backup_read(path, &len);
        if (!text) return;
        printf("JSON BEGIN %u\n%s\nJSON END\n", (unsigned)len, text);
        free(text);
    } else if (argc == 3 && !strcmp(argv[1], "photo-dump")) {
        char path[64]; snprintf(path, sizeof path, SAVE_DIR "/photos/photo-%04u.bmp", (unsigned)atoi(argv[2]) % 10000);
        FILE *f = fopen(path, "rb");
        if (!f) { ESP_LOGE(TAG, "%s: %s", path, strerror(errno)); return; }
        static const char b64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        uint8_t in[57]; char out[80]; size_t got;
        printf("PHOTO BEGIN %s\n", path);
        while ((got = fread(in, 1, sizeof in, f)) > 0) {
            size_t o = 0;
            for (size_t i = 0; i < got; i += 3) {
                uint32_t v = (uint32_t)in[i] << 16 | (i + 1 < got ? (uint32_t)in[i + 1] << 8 : 0) | (i + 2 < got ? in[i + 2] : 0);
                out[o++] = b64[v >> 18 & 63]; out[o++] = b64[v >> 12 & 63];
                out[o++] = i + 1 < got ? b64[v >> 6 & 63] : '='; out[o++] = i + 2 < got ? b64[v & 63] : '=';
            }
            out[o] = 0; printf("%s\n", out);
        }
        fclose(f);
        printf("PHOTO END\n");
    } else if (argc >= 2 && !strcmp(argv[1], "backups")) {
        char path[96]; unsigned n;
        if (backup_latest("backup", path, &n)) ESP_LOGI(TAG, "JSON latest: %s", n ? path : "none");
        if (backup_latest("before-restore", path, &n)) ESP_LOGI(TAG, "JSON safety: %s", n ? path : "none");
    } else if (argc < 2 || !strcmp(argv[1], "status")) {
        open_bank();
        ESP_LOGI(TAG, "SD READY %u %08lx %s", CYD_ICON_BYTES, (unsigned long)CYD_ICON_CRC, s_bank ? "verified" : "missing");
    } else if (!strcmp(argv[1], "cancel")) {
        if (s_upload) { fclose(s_upload); s_upload = NULL; }
        if (remove(TEMP_PATH) != 0 && errno != ENOENT) {
            ESP_LOGE(TAG, "SD FAIL cancel: %s", strerror(errno)); return;
        }
        ESP_LOGI(TAG, "SD CANCELED partial upload");
    } else if (!strcmp(argv[1], "begin")) {
        if (open_bank()) { ESP_LOGI(TAG, "SD EXISTS verified"); return; }
        struct stat st;
        if (stat(BANK_PATH, &st) == 0 || stat(TEMP_PATH, &st) == 0) {
            ESP_LOGE(TAG, "SD FAIL destination exists; refusing overwrite"); return;
        }
        if (s_upload) { ESP_LOGE(TAG, "SD FAIL upload already active"); return; }
        s_upload = fopen(TEMP_PATH, "wb");
        s_uploaded = 0; s_upload_crc = 0;
        if (!s_upload) { ESP_LOGE(TAG, "SD FAIL create: %s", strerror(errno)); return; }
        ESP_LOGI(TAG, "SD BEGIN");
    } else if (!strcmp(argv[1], "chunk") && argc == 4) {
        char *end;
        unsigned long offset = strtoul(argv[2], &end, 10);
        size_t len = strlen(argv[3]);
        uint8_t data[64];
        if (!s_upload || *end || offset != s_uploaded || !len || len > 128 || (len & 1) || s_uploaded + len / 2 > CYD_ICON_BYTES) {
            ESP_LOGE(TAG, "SD FAIL invalid chunk"); return;
        }
        for (size_t i = 0; i < len / 2; i++) {
            char pair[3] = {argv[3][2*i], argv[3][2*i+1], 0};
            unsigned long v = strtoul(pair, &end, 16);
            if (*end) { ESP_LOGE(TAG, "SD FAIL invalid hex"); return; }
            data[i] = (uint8_t)v;
        }
        if (fwrite(data, 1, len / 2, s_upload) != len / 2) {
            ESP_LOGE(TAG, "SD FAIL write: %s", strerror(errno)); return;
        }
        s_upload_crc = esp_crc32_le(s_upload_crc, data, len / 2); s_uploaded += len / 2;
        ESP_LOGI(TAG, "SD ACK %u", (unsigned)s_uploaded);
    } else if (!strcmp(argv[1], "finish")) {
        if (!s_upload) { ESP_LOGE(TAG, "SD FAIL no upload"); return; }
        int err = fclose(s_upload); s_upload = NULL;
        if (err || s_uploaded != CYD_ICON_BYTES || s_upload_crc != CYD_ICON_CRC) {
            ESP_LOGE(TAG, "SD FAIL upload size/CRC; incomplete file preserved"); return;
        }
        FILE *file = fopen(TEMP_PATH, "rb");
        bool ok = file && verify_bank(file);
        if (file) fclose(file);
        if (!ok || rename(TEMP_PATH, BANK_PATH) != 0 || !open_bank()) {
            ESP_LOGE(TAG, "SD FAIL readback/finalize"); return;
        }
        ESP_LOGI(TAG, "SD COMPLETE verified");
    } else {
        ESP_LOGE(TAG, "SD FAIL use status, backup, backups, export, restore|undo [number] [confirm], begin, chunk <offset> <hex>, finish");
    }
}
