#include "sd_port_cyd.h"
#include "icons.h"
#include "icons_sd_bank.h"
#include "driver/sdspi_host.h"
#include "esp_vfs_fat.h"
#include "esp_crc.h"
#include "esp_log.h"
#include "sdmmc_cmd.h"
#include "progression.h"
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
        spi_bus_free(SPI3_HOST); return false;
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
bool aquarium_port_ready(void) {
    if (!s_mounted || s_upload) return false;
    if (mkdir(SAVE_DIR, 0755) != 0 && errno != EEXIST) {
        ESP_LOGE(TAG, "aquarium directory: %s", strerror(errno)); return false;
    }
    if (mkdir(SAVE_DIR "/tanks", 0755) != 0 && errno != EEXIST) {
        ESP_LOGE(TAG, "tank directory: %s", strerror(errno)); return false;
    }
    return true;
}
void aquarium_port_id(uint32_t id[2]) { esp_fill_random(id, 2 * sizeof *id); }
bool aquarium_port_read(const uint32_t id[2], int slot, void *data, size_t size) {
    return aquarium_port_ready() && aquarium_store_read(SAVE_DIR "/tanks", id, slot, data, size);
}
bool aquarium_port_write(const uint32_t id[2], int slot, const void *data, size_t size) {
    return aquarium_port_ready() && aquarium_store_write(SAVE_DIR "/tanks", id, slot, data, size);
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
    char error[96]; size_t read_len = 0;
    char *readback = ok ? backup_read(temp, &read_len) : NULL;
    ok = readback && read_len == len && esp_crc32_le(0, (const uint8_t *)readback, read_len) == crc
        && progression_restore_json(readback, read_len, false, error, sizeof error);
    free(readback);
    if (ok) ok = rename(temp, path) == 0;
    if (!ok) {
        ESP_LOGE(TAG, "backup write/readback validation failed; previous backups untouched");
        if (remove(temp) != 0) ESP_LOGW(TAG, "could not remove incomplete backup: %s", strerror(errno));
    } else ESP_LOGI(TAG, "JSON BACKUP verified: %s (%u bytes)", path, (unsigned)len);
    return ok;
}
static void backup_action(tank_t *tank, bool restore, bool confirmed, const char *prefix, unsigned requested) {
    render_work_caches(false);
    if (!tank || !s_mounted || s_upload) {
        ESP_LOGE(TAG, "backup unavailable: no tank/card or upload active");
        render_backup_status("SD UNAVAILABLE / BUSY", false); return;
    }
    if (!restore) {
        char path[96];
        bool ok = backup_write(tank, "backup", path);
        render_backup_status(ok ? "BACKUP SAVED ON SD" : "BACKUP FAILED - SEE SERIAL", false);
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
        free(text); render_backup_status("TAP RESTORE AGAIN TO CONFIRM", true); return;
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
    if (!tank || !s_mounted || s_upload) { update_error("SD UNAVAILABLE / BUSY"); return; }
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
    if (argc == 2 && !strcmp(argv[1], "update")) sd_port_update_action(tank);
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
