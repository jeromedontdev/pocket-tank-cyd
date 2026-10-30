/* Expanded saves live in the existing internal SPIFFS partition. NVS is
 * read-only migration input; no expanded blob is ever written to NVS. */
#include "progression.h"
#include "aquarium_store.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_spiffs.h"
#include "esp_partition.h"
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>
#include <dirent.h>

static const char *TAG = "persist";
static const char *directory = "/profiles";
static const uint32_t store_id[2] = {0x50524631u, 0x41435456u};
static bool s_save_blocked, s_mounted;
/* The marker as last read or written (-1 unknown): saving then skips its stat + read (~12 ms). */
static int s_marker = -1;
static const char *tank_directory = "/profiles/tanks";
bool sd_port_aquarium_read(const uint32_t id[2], int slot, void *data, size_t size);
bool sd_port_aquarium_read_sized(const uint32_t id[2], int slot, void *data, size_t size, size_t *got);

static bool blank_partition(const esp_partition_t *p) {
    unsigned char bytes[256];
    bool erased = true, legacy_empty = p->size == 0x20000;
    for (size_t offset = 0; offset < p->size; offset += sizeof bytes) {
        if (esp_partition_read(p, offset, bytes, sizeof bytes) != ESP_OK) return false;
        for (size_t i = 0; i < sizeof bytes; i++) {
            size_t address = offset + i;
            unsigned char expected = 0xff;
            /* Some older CYD installs formatted only the first 64 KiB of
             * this 128 KiB partition. Accept that EXACT empty SPIFFS image:
             * 16 lookup pages with length-dependent magic and erase count 0,
             * every object lookup/data byte and remaining 64 KiB erased.
             * Any file, tombstone, damage or other metadata blocks formatting. */
            if (address < 0x10000 && (address & 0xfff) >= 252 && (address & 0xfff) < 256) {
                unsigned magic = (0x20140529u ^ 256u ^ (16u - (unsigned)(address / 4096))) & 0xffffu;
                unsigned index = (unsigned)(address & 3u);
                expected = index < 2 ? (unsigned char)(magic >> (8 * index)) : 0;
            }
            if (bytes[i] != 0xff) erased = false;
            if (bytes[i] != expected) legacy_empty = false;
        }
    }
    if (legacy_empty) ESP_LOGW(TAG, "Verified empty legacy 64 KiB filesystem; initializing full save partition");
    return erased || legacy_empty;
}
static bool mount_store(void) {
    if (s_save_blocked) return false;
    if (s_mounted) return true;
    const esp_partition_t *partition = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, "storage");
    if (!partition) { s_save_blocked = true; return false; }
    /* Formatting requires a fully erased partition or the exact empty legacy
     * image above. A failed mount of any existing data is never a reset. */
    esp_vfs_spiffs_conf_t config = {
        .base_path = "/profiles", .partition_label = "storage", .max_files = 4,
        .format_if_mount_failed = blank_partition(partition)
    };
    esp_err_t error = esp_vfs_spiffs_register(&config);
    if (error != ESP_OK) {
        ESP_LOGE(TAG, "Internal save filesystem unavailable: %s; saving blocked", esp_err_to_name(error));
        s_save_blocked = true; return false;
    }
    s_mounted = true; return true;
}
/* Missing and unreadable are deliberately different: neither corrupt copies
 * nor missing copies after an established save may revive a stale NVS blob. */
static int exists(int slot) {
    int found = 0;
    for (int copy = 0; copy < 2; copy++) {
        char name[96]; struct stat info;
        snprintf(name, sizeof name, "%s/%08lx%08lx-%d.%c", directory,
            (unsigned long)store_id[0], (unsigned long)store_id[1], slot, copy ? 'b' : 'a');
        if (stat(name, &info) == 0) found = 1;
        else if (errno != ENOENT) return -1;
    }
    return found;
}
static bool remove_active(void) {
    bool ok = true;
    for (int copy = 0; copy < 2; copy++) {
        char name[96];
        snprintf(name, sizeof name, "%s/%08lx%08lx-0.%c", directory,
            (unsigned long)store_id[0], (unsigned long)store_id[1], copy ? 'b' : 'a');
        if (remove(name) != 0 && errno != ENOENT) ok = false;
    }
    return ok;
}
static bool remove_archives(void) {
    DIR *dir = opendir(tank_directory);
    if (!dir) {
        if (errno == ENOENT) return true;
        ESP_LOGE(TAG, "Cannot enumerate obsolete tanks: %s", strerror(errno)); return false;
    }
    bool ok = true;
    struct dirent *entry;
    for (;;) {
        errno = 0;
        entry = readdir(dir);
        if (!entry) {
            if (errno) {
                ESP_LOGE(TAG, "Cannot enumerate obsolete tanks: %s", strerror(errno));
                ok = false;
            }
            break;
        }
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;
        char path[128];
        int n = snprintf(path, sizeof path, "%s/%s", tank_directory, entry->d_name);
        if (n < 0 || (size_t)n >= sizeof path || remove(path) != 0) {
            ESP_LOGE(TAG, "Cannot remove obsolete tank archive: %s", entry->d_name);
            ok = false;
        }
    }
    if (closedir(dir) != 0) {
        ESP_LOGE(TAG, "Cannot close obsolete tank directory: %s", strerror(errno));
        ok = false;
    }
    return ok;
}
bool aquarium_port_ready(void) { return mount_store(); }
bool aquarium_port_read(const uint32_t id[2], int slot, void *data, size_t size) {
    return aquarium_port_read_sized(id, slot, data, size, NULL);
}
bool aquarium_port_read_sized(const uint32_t id[2], int slot, void *data, size_t size, size_t *got) {
    if (got) *got = 0;
    if (!mount_store() || slot < 0 || slot >= AQUARIUM_CAP) return false;
    int present = aquarium_store_present(tank_directory, id, slot);
    if (present != 0)
        return present > 0 && aquarium_store_read_sized(tank_directory, id, slot, data, size, got);
    size_t stored_size = 0;
    if (!sd_port_aquarium_read_sized(id, slot, data, size, &stored_size)) return false;
    if (!aquarium_store_archive(tank_directory, id, slot, data, stored_size)) {
        ESP_LOGE(TAG, "Legacy tank %d import failed; SD source unchanged", slot + 1);
        return false;
    }
    ESP_LOGI(TAG, "Tank %d imported into internal flash; SD source retained", slot + 1);
    if (got) *got = stored_size;
    return true;
}
bool aquarium_port_write(const uint32_t id[2], int slot, const void *data, size_t size) {
    if (!mount_store() || slot < 0 || slot >= AQUARIUM_CAP) return false;
    bool ok = aquarium_store_archive(tank_directory, id, slot, data, size);
    if (!ok) ESP_LOGE(TAG, "Internal tank %d write failed; active aquarium unchanged", slot + 1);
    return ok;
}
static bool legacy_load(void *buf, size_t max, size_t *got) {
    nvs_handle_t h;
    esp_err_t error = nvs_open(SAVE_NVS_NS, NVS_READONLY, &h);
    if (error == ESP_ERR_NVS_NOT_FOUND) return false;
    if (error != ESP_OK) { s_save_blocked = true; return false; }
    size_t size = 0;
    error = nvs_get_blob(h, SAVE_NVS_KEY, NULL, &size);
    if (error == ESP_OK && size && size <= max) error = nvs_get_blob(h, SAVE_NVS_KEY, buf, &size);
    else if (error == ESP_OK) error = ESP_FAIL;
    nvs_close(h);
    if (error == ESP_OK) { *got = size; return true; }
    if (error != ESP_ERR_NVS_NOT_FOUND) s_save_blocked = true;
    return false;
}
bool persist_port_blocked(void) { return s_save_blocked; }
bool persist_port_load(void *buf, size_t max, size_t *got) {
    *got = 0;
    if (!mount_store()) return false;
    int active = exists(0), marker_exists = exists(1);
    if (active < 0 || marker_exists < 0) { s_save_blocked = true; return false; }
    uint32_t state = 0;
    if (marker_exists) {
        if (!aquarium_store_read(directory, store_id, 1, &state, sizeof state) || (state != 1 && state != 2)) {
            s_save_blocked = true; return false;
        }
        if (state == 2) { s_marker = 2; return false; } /* Durable wipe overrides all old copies. */
        s_marker = 1;
    }
    if (active) {
        if (aquarium_store_read_sized(directory, store_id, 0, buf, max, got)) return true;
        s_save_blocked = true; return false;
    }
    if (marker_exists) {
        s_save_blocked = true;
        return false;
    }
    return legacy_load(buf, max, got);
}
bool persist_port_save(const void *buf, size_t len) {
    if (!mount_store()) return false;
    int64_t started = esp_timer_get_time();
    uint32_t initialized = 1, state = 0;
    if (s_marker >= 0) state = (uint32_t)s_marker;
    else {
        int marker = exists(1);
        if (marker < 0 || (marker && !aquarium_store_read(directory, store_id, 1, &state, sizeof state))) return false;
    }
    if (state == 2) {
        /* Keep the wipe authoritative until both old generations are gone
         * and a complete fresh record has passed CRC/readback. */
        if (!remove_archives() || !remove_active() || !aquarium_store_write(directory, store_id, 0, buf, len)) return false;
        s_marker = -1;
        if (!aquarium_store_write(directory, store_id, 1, &initialized, sizeof initialized)) return false;
        s_marker = 1; return true;
    }
    if (state != 1) {
        s_marker = -1;
        if (!aquarium_store_write(directory, store_id, 1, &initialized, sizeof initialized)) return false;
        s_marker = 1;
    }
    if (!aquarium_store_write(directory, store_id, 0, buf, len)) return false;
    /* Each save stalls the render loop; this line shows its cost and cadence. */
    size_t total = 0, used = 0;
    esp_spiffs_info("storage", &total, &used);
    ESP_LOGI(TAG, "saved in %lld ms (spiffs %u/%u KiB)", (long long)((esp_timer_get_time() - started) / 1000),
             (unsigned)(used / 1024), (unsigned)(total / 1024));
    /* Active is the sole commit point. Its absence from now on is an error. */
    return true;
}
bool persist_port_erase(void) {
    /* No filesystem format, repartition or SD deletion is part of a wipe. */
    if (!s_mounted && !mount_store()) return false;
    uint32_t wiped = 2;
    s_marker = -1;
    if (!aquarium_store_write(directory, store_id, 1, &wiped, sizeof wiped)) return false;
    s_marker = 2;
    /* The tombstone is the commit point, including for NVS-only legacy saves.
     * From here failures are cleanup warnings, never a false failed wipe. */
    nvs_handle_t h;
    esp_err_t error = nvs_open(SAVE_NVS_NS, NVS_READWRITE, &h);
    if (error == ESP_OK) {
        error = nvs_erase_all(h);
        if (error == ESP_OK) error = nvs_commit(h);
        nvs_close(h);
    }
    if (error != ESP_OK) ESP_LOGW(TAG, "Profile wiped; legacy NVS cleanup failed: %s", esp_err_to_name(error));
    /* Once the tombstone is durable, deletion is cleanup, not a rollbackable
     * operation. A retrying fresh save also removes any remaining old files. */
    if (!remove_active()) ESP_LOGW(TAG, "Profile wiped; obsolete copies await cleanup");
    if (!remove_archives()) ESP_LOGW(TAG, "Profile wiped; obsolete tank archives await cleanup");
    s_save_blocked = false;
    return true;
}
int64_t clock_port_now_unix(void) {
    time_t now = time(NULL); return now > 1700000000 ? (int64_t)now : 0;
}
