"""Exercise the actual daily SD mailbox/storage code with a deferred native worker.

No ESP-IDF build or SD card is needed. Scratch files stay in the working directory.
"""
import argparse
from pathlib import Path
import shutil
import subprocess
import uuid


HARNESS = r"""
#undef NDEBUG
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#include <io.h>
#define mkdir(p, mode) _mkdir(p)
#define fsync _commit
#else
#include <unistd.h>
#endif
#define SAVE_DIR "saves"
#define ESP_OK 0
#define ESP_ERR_NVS_NOT_FOUND 2
#define NVS_READONLY 0
#define NVS_READWRITE 1
#define pdPASS 1
#define NET_BUSY 1
#define tskIDLE_PRIORITY 0
#define pdMS_TO_TICKS(n) (n)
typedef int esp_err_t;
typedef int nvs_handle_t;
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(p) ((void)(p))
#define portEXIT_CRITICAL(p) ((void)(p))
static void log_message(const char *tag, const char *format, ...) {
    (void)tag; (void)format;
}
#define ESP_LOGE log_message
#define ESP_LOGW log_message
#define ESP_LOGI log_message
static const char *TAG = "test";
typedef struct {
    struct { uint32_t id[2]; uint8_t active; } aquariums;
    struct { uint8_t mode, chosen; } profile;
} tank_t;
static bool s_mounted = true, network, setup, updating, enabled = true, worker_context;
static FILE *s_upload, *s_photo;
static int fail_io, allocation_failure, validation_failure, task_failure;
static int status_count, prune_count, validation_count, marker_writes, marker_error;
static unsigned newest;
static int foreground_state;
static bool connected;
typedef int update_manifest_t;
static int64_t esp_timer_get_time(void) { return 100; }
static bool net_port_connected(void) { return connected; }
static int net_port_scan_state(void) { return foreground_state; }
static int net_port_connect_state(void) { return foreground_state; }
static int net_port_check_state(const update_manifest_t **m) { (void)m; return foreground_state; }
static int net_port_install_state(int *percent) { (void)percent; return foreground_state; }
static uint32_t marker;
static void (*pending)(void *);
static void *pending_arg;
static char snapshot[1200];
static int64_t clock_port_now_unix(void) { assert(!worker_context); return 1700000000LL; }
static bool weather_network_needed(void) { return network; }
static bool weather_network_needed_after(float dt) { (void)dt; return network; }
static bool net_port_weather_active(void) { return network; }
static bool setup_active(void) { return setup; }
static bool update_active(void) { return updating; }
static bool ambient_auto_backup(void) { return enabled; }
static void render_work_caches(bool allow) { assert(!worker_context && !allow); }
static char *progression_backup_json(const tank_t *tank) {
    assert(!worker_context && tank);
    if (allocation_failure) return NULL;
    char *text = malloc(sizeof snapshot);
    assert(text); memcpy(text, snapshot, sizeof snapshot); return text;
}
static bool progression_restore_json(const char *text, size_t length, bool commit,
                                     char *error, size_t cap) {
    assert(!worker_context && !commit && length == strlen(snapshot));
    assert(!strcmp(text, snapshot)); validation_count++;
    snprintf(error, cap, "injected semantic failure"); return !validation_failure;
}
static bool companion_backup_record(tank_t *tank) {
    assert(!worker_context && tank); status_count++; return true;
}
static const char *esp_err_to_name(int err) { (void)err; return "injected"; }
static int nvs_open(const char *name, int mode, nvs_handle_t *h) {
    assert(!worker_context && !strcmp(name, "tank")); (void)mode; *h = 1; return 0;
}
static int nvs_get_u32(nvs_handle_t h, const char *key, uint32_t *value) {
    (void)h; assert(!strcmp(key, "autobk_day")); *value = marker; return 0;
}
static int nvs_set_u32(nvs_handle_t h, const char *key, uint32_t value) {
    (void)h; assert(!worker_context && !strcmp(key, "autobk_day"));
    marker_writes++; if (!marker_error) marker = value; return marker_error;
}
static int nvs_commit(nvs_handle_t h) { (void)h; return marker_error; }
static void nvs_close(nvs_handle_t h) { (void)h; }
static void vTaskDelay(int ticks) { assert(ticks > 0); }
static void vTaskDelete(void *task) { assert(worker_context && !task); }
static int xTaskCreatePinnedToCore(void (*fn)(void *), const char *name, int stack,
    void *arg, int priority, void *handle, int core) {
    assert(!worker_context && !pending && !strcmp(name, "sd_backup"));
    assert(stack == 4096 && priority == 1 && !handle && core == 1);
    if (task_failure) return 0;
    pending = fn; pending_arg = arg; return pdPASS;
}
static uint32_t esp_crc32_le(uint32_t crc, const uint8_t *p, size_t n) {
    crc = ~crc;
    while (n--) { crc ^= *p++; for (int i = 0; i < 8; i++) crc = (crc >> 1) ^ (0xedb88320u & -(crc & 1)); }
    return ~crc;
}
static bool backup_latest(const char *prefix, char path[96], unsigned *number) {
    assert(worker_context && (!strcmp(prefix, "auto") || !strcmp(prefix, "auto-test")));
    snprintf(path, 96, SAVE_DIR "/auto-%04u.json", newest); *number = newest; return true;
}
static void auto_prune(unsigned number) { assert(worker_context && number == newest); prune_count++; }
static size_t test_write(const void *p, size_t size, size_t n, FILE *file) {
    return fail_io == 1 ? 0 : fwrite(p, size, n, file);
}
static int test_flush(FILE *file) { return fail_io == 2 ? EOF : fflush(file); }
static int test_sync(int fd) { return fail_io == 3 ? -1 : fsync(fd); }
static int test_close(FILE *file) { int err = fclose(file); return fail_io == 4 ? EOF : err; }
static size_t test_read(void *p, size_t size, size_t n, FILE *file) {
    size_t got = fread(p, size, n, file);
    if (fail_io == 5 && got) ((char *)p)[0] ^= 1;
    return got;
}
static int test_rename(const char *from, const char *to) {
    if (fail_io == 6) return -1;
    int err = rename(from, to); if (!err) newest++; return err;
}
#define fwrite test_write
#define fflush test_flush
#undef fsync
#define fsync test_sync
#define fclose test_close
#define fread test_read
#define rename test_rename
"""

TEST = r"""
#undef fwrite
#undef fflush
#undef fsync
#undef fclose
#undef fread
#undef rename
static tank_t tank = {.aquariums = {.id = {123, 456}, .active = 2},
                      .profile = {.mode = 1, .chosen = 1}};
static void run_worker(void) {
    if (s_auto_request.pending) sd_port_auto_backup_service(&tank, 0, true);
    assert(pending && sd_port_auto_backup_busy());
    void (*fn)(void *) = pending; pending = NULL;
    worker_context = true; fn(pending_arg); worker_context = false;
    assert(sd_port_auto_backup_busy() && s_auto_job.text == NULL);
}
static void reset(void) {
    assert(!pending && !sd_port_auto_backup_busy() && !s_auto_request.pending);
    remove(SAVE_DIR "/auto-0001.json"); remove(SAVE_DIR "/auto-0001.json.tmp");
    remove(SAVE_DIR "/auto-test-0001.json"); remove(SAVE_DIR "/auto-test-0001.json.tmp");
    s_auto_wait = 0; s_auto_day = 0; s_auto_day_loaded = true;
    fail_io = allocation_failure = validation_failure = task_failure = marker_error = 0;
    status_count = prune_count = validation_count = marker_writes = 0;
    newest = marker = 0; network = setup = updating = false; enabled = s_mounted = true;
    foreground_state = 0; connected = false;
    s_upload = s_photo = NULL;
}
int main(void) {
    assert(mkdir(SAVE_DIR, 0755) == 0);
    memset(snapshot, 'x', sizeof snapshot - 1);
    for (int failure = 0; failure <= 6; failure++) {
        reset(); fail_io = failure;
        sd_port_auto_backup_tick(&tank, 1);
        assert(pending && status_count == 0 && marker == 0 && validation_count == 1);
        assert(!sd_port_auto_backup_now(&tank));
        run_worker();
        assert(status_count == 0 && marker == 0);
        enabled = false; /* Completion is collected even after disabling the setting. */
        sd_port_auto_backup_tick(&tank, 1);
        assert(!sd_port_auto_backup_busy());
        assert(status_count == (failure ? 0 : 1));
        assert(prune_count == (failure ? 0 : 1));
        assert(marker_writes == (failure ? 0 : 1));
        assert(s_auto_day == (failure ? 0 : 1700000000u / 86400u));
        assert(s_auto_wait == (failure ? 3600 : 60));
        FILE *file = fopen(SAVE_DIR "/auto-0001.json", "rb");
        assert((file != NULL) == !failure);
        if (file) {
            char bytes[sizeof snapshot];
            assert(fread(bytes, 1, sizeof snapshot, file) == sizeof snapshot - 1);
            assert(!memcmp(bytes, snapshot, sizeof snapshot - 1)); fclose(file);
        }
        assert(!fopen(SAVE_DIR "/auto-0001.json.tmp", "rb"));
    }
    for (int gate = 0; gate < 10; gate++) {
        reset();
        if (gate == 0) network = true;
        if (gate == 1) setup = true;
        if (gate == 2) s_upload = (FILE *)1;
        if (gate == 3) s_photo = (FILE *)1;
        if (gate == 4) allocation_failure = 1;
        if (gate == 5) validation_failure = 1;
        if (gate == 6) task_failure = 1;
        if (gate == 7) updating = true;
        if (gate == 8) connected = true;
        if (gate == 9) foreground_state = NET_BUSY;
        bool queued = sd_port_auto_backup_now(&tank);
        assert(queued == (gate >= 4 && gate <= 6));
        if (queued) {
            assert(!pending && !validation_count && !sd_port_auto_backup_busy());
            sd_port_auto_backup_service(&tank, 0, true);
        }
        assert(!pending && !sd_port_auto_backup_busy() && !marker_writes);
    }
    for (int identity = 0; identity < 4; identity++) {
        reset(); sd_port_auto_backup_tick(&tank, 1);
        tank_t original = tank;
        if (identity == 0) tank.aquariums.id[0]++;
        if (identity == 1) tank.aquariums.active++;
        if (identity == 2) tank.profile.mode++;
        if (identity == 3) tank.profile.chosen = 0;
        run_worker(); sd_port_auto_backup_tick(&tank, 0);
        assert(!status_count && !marker_writes && !sd_port_auto_backup_busy());
        tank = original;
    }
    reset(); marker_error = 7;
    sd_port_auto_backup_tick(&tank, 1); run_worker(); sd_port_auto_backup_tick(&tank, 0);
    assert(status_count == 1 && marker_writes == 1 && !s_auto_day && s_auto_wait == 3600);
    reset();
    sd_port_auto_backup_service(&tank, 1, false);
    assert(!pending);
    sd_port_auto_backup_service(&tank, 1, true); run_worker();
    sd_port_auto_backup_service(&tank, 0, false);
    assert(status_count == 1 && marker_writes == 1 && !sd_port_auto_backup_busy());
    reset();
    assert(auto_request(&tank, true));
    assert(!pending && !validation_count && !sd_port_auto_backup_busy());
    sd_port_auto_backup_service(&tank, 0, false);
    assert(s_auto_request.pending && !pending && !validation_count);
    run_worker();
    sd_port_auto_backup_service(&tank, 0, false);
    assert(!status_count && !marker_writes && !prune_count && !sd_port_auto_backup_busy());
    assert(!fopen(SAVE_DIR "/auto-test-0001.json", "rb"));
    reset();
    assert(sd_port_auto_backup_now(&tank));
    assert(!pending && !validation_count && s_auto_request.pending);
    tank.aquariums.active++;
    sd_port_auto_backup_service(&tank, 0, false);
    tank.aquariums.active--;
    assert(!s_auto_request.pending && !pending && !validation_count && !status_count);
    reset();
    assert(sd_port_auto_backup_now(&tank)); run_worker(); sd_port_auto_backup_tick(&tank, 0);
    assert(status_count == 1 && !marker_writes); /* Explicit command does not mark a UTC day. */
    reset(); newest = 9999;
    assert(sd_port_auto_backup_now(&tank)); run_worker(); sd_port_auto_backup_tick(&tank, 0);
    assert(!status_count && !marker_writes && !prune_count);
    reset();
    FILE *old = fopen(SAVE_DIR "/auto-0001.json.tmp", "wb");
    assert(old); assert(fputs("preserve", old) >= 0); fclose(old);
    assert(sd_port_auto_backup_now(&tank)); run_worker(); sd_port_auto_backup_tick(&tank, 0);
    assert(!status_count && !marker_writes);
    old = fopen(SAVE_DIR "/auto-0001.json.tmp", "rb");
    assert(old && fgetc(old) == 'p'); fclose(old);
    reset();
    puts("PASS: async snapshot ownership, exact readback, failure rollback, gates, identity and daily marker");
}
"""


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default="cc")
    args = parser.parse_args()
    root = Path(__file__).resolve().parent.parent
    source = (root / "firmware" / "main" / "sd_port_cyd.c").read_text()
    state = source[source.index("typedef enum { AUTO_IDLE"):source.index("static bool verify_bank")]
    job = source[source.index("typedef struct {\n    char *text;"):source.index("static void auto_prune")]
    worker = source[source.index("static bool auto_store"):source.index("/* 1.3 photo mode")]
    settings = source[source.index("#define AUTO_KEEP"):source.index("typedef struct {\n    char *text;")]
    for start, end in (
        ("static void backup_action", "void sd_port_backup_action"),
        ("bool sd_port_photo_begin", "bool sd_port_photo_rows"),
        ("void sd_port_update_action", "static void update_upload"),
        ("void sd_port_command", None),
        ("static bool legacy_aquarium_ready", "void aquarium_port_id"),
    ):
        section = source[source.index(start):source.index(end) if end else len(source)]
        assert "sd_port_auto_backup_busy()" in section, start
    storage = worker[:worker.index("static bool auto_network_busy")]
    assert storage.count("vTaskDelay(pdMS_TO_TICKS(1))") == 2
    assert "if (n > 512) n = 512;" in storage
    assert "if (n > sizeof buffer) n = sizeof buffer;" in storage
    for forbidden in ("progression_", "companion_", "render_", "nvs_", "tank->", "clock_port_"):
        assert forbidden not in storage, forbidden
    command = source[source.index("void sd_port_command"):]
    diagnostic = command[command.index('"auto-verify"'):command.index('"update"')]
    assert "auto_request(tank, true)" in diagnostic and "auto_start(" not in diagnostic
    request = worker[worker.index("static bool auto_request"):worker.index("static void auto_complete")]
    for forbidden in ("progression_", "auto_start(", "render_work_caches"):
        assert forbidden not in request, forbidden
    folder = Path.cwd() / (".sd-backup-test-" + uuid.uuid4().hex)
    folder.mkdir()
    try:
        test = folder / "test.c"
        test.write_text(HARNESS + state + settings + job + worker + TEST)
        executable = folder / "test.exe"
        command = [args.cc]
        if Path(args.cc).stem.lower() == "zig":
            command.append("cc")
        subprocess.run(command + ["-std=c11", "-O1", "-Wall", "-Wextra", "-Werror",
                                  str(test), "-o", str(executable)], check=True)
        subprocess.run([str(executable)], cwd=folder, check=True)
    finally:
        shutil.rmtree(folder)


if __name__ == "__main__":
    main()
