/* Deterministic offline port: never accesses the Internet. Optional JSON
 * fixtures exercise the same parser as hardware, never invented live weather.
 * Config is per simulator instance via POCKET_TANK_WEATHER (default cwd file). */
#include "weather.h"
#include "update.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
static CRITICAL_SECTION mutex;
#define LOCK() EnterCriticalSection(&mutex)
#define UNLOCK() LeaveCriticalSection(&mutex)
#else
#include <pthread.h>
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
#define LOCK() pthread_mutex_lock(&mutex)
#define UNLOCK() pthread_mutex_unlock(&mutex)
#endif

static weather_request_t pending;
static weather_result_t result;
static bool busy, ready, initialized;
static const char *path(void) {
    const char *p = getenv("POCKET_TANK_WEATHER");
    return p && p[0] ? p : "weather-config.bin";
}
void weather_port_init(void) {
    if (initialized) return;
    char ssid[NET_SSID_MAX + 1], pass[NET_PASS_MAX + 1];
    (void)net_port_creds_get(ssid, pass); /* startup-only credential cache */
#ifdef _WIN32
    InitializeCriticalSection(&mutex);
#endif
    initialized = true;
}
bool weather_port_load(weather_config_t *out) {
    FILE *f = fopen(path(), "rb");
    if (!f) return false;
    weather_config_t c = {0};
    bool ok = fread(&c, 1, sizeof c, f) == sizeof c && fgetc(f) == EOF && weather_config_valid(&c);
    fclose(f);
    if (ok) *out = c;
    return ok;
}
static bool save(const weather_config_t *c) {
    if (!weather_config_valid(c)) return false;
    char next[1024];
    int n = snprintf(next, sizeof next, "%s.new", path());
    if (n < 0 || (size_t)n >= sizeof next) return false;
    FILE *f = fopen(next, "wb");
    if (!f) return false;
    bool ok = fwrite(c, 1, sizeof *c, f) == sizeof *c;
    if (fflush(f)) ok = false;
    if (fclose(f)) ok = false;
#ifdef _WIN32
    if (ok) ok = MoveFileExA(next, path(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
    if (ok) ok = rename(next, path()) == 0;
#endif
    if (!ok) remove(next);
    return ok;
}
static void process(weather_result_t *out) {
    memset(out, 0, sizeof *out);
    out->operation = pending.operation; out->generation = pending.generation;
    if (pending.operation == WEATHER_SAVE) {
        out->ok = save(&pending.config); out->error = out->ok ? WEATHER_ERR_NONE : WEATHER_ERR_SAVE; return;
    }
    out->error = WEATHER_ERR_HTTP;
    if (pending.operation == WEATHER_CURRENT &&
        (!pending.config.enabled || !pending.config.located || !weather_config_valid(&pending.config))) return;
    const char *fixture = getenv(pending.operation == WEATHER_SEARCH ?
        "POCKET_TANK_WEATHER_SEARCH_FIXTURE" : "POCKET_TANK_WEATHER_CURRENT_FIXTURE");
    FILE *f = fixture && fixture[0] ? fopen(fixture, "rb") : NULL;
    if (!f) return;
    char body[WEATHER_BODY_MAX + 1];
    size_t n = fread(body, 1, sizeof body, f);
    bool ok = !ferror(f) && n <= WEATHER_BODY_MAX;
    fclose(f);
    if (ok) {
        out->ok = pending.operation == WEATHER_SEARCH ?
            weather_parse_cities(body, n, out->cities, &out->count) :
            weather_parse_current(body, n, &out->reading);
        out->error = out->ok ? WEATHER_ERR_NONE : WEATHER_ERR_INVALID;
    }
}
#ifdef _WIN32
static DWORD WINAPI worker(LPVOID ignored) {
#else
static void *worker(void *ignored) {
#endif
    (void)ignored;
    weather_result_t out;
    process(&out);
    LOCK(); result = out; ready = true; UNLOCK();
    return 0;
}
bool weather_port_start(const weather_request_t *r) {
    LOCK();
    if (busy) { UNLOCK(); return false; }
    pending = *r; busy = true; ready = false;
    UNLOCK();
#ifdef _WIN32
    HANDLE thread = CreateThread(NULL, 0, worker, NULL, 0, NULL);
    bool ok = thread != NULL;
    if (thread) CloseHandle(thread);
#else
    pthread_t thread;
    bool ok = pthread_create(&thread, NULL, worker, NULL) == 0;
    if (ok) pthread_detach(thread);
#endif
    if (!ok) { LOCK(); busy = false; UNLOCK(); }
    return ok;
}
bool weather_port_poll(weather_result_t *out) {
    LOCK();
    bool ok = ready;
    if (ok) { *out = result; busy = ready = false; }
    UNLOCK();
    return ok;
}
