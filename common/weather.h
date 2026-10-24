/* Optional city weather. All public calls belong to the frame/UI thread.
 * Ports alone own blocking storage/network work; results cross a mailbox.
 * Link weather.c + cJSON + render helpers + existing net port, and exactly
 * one weather_port_esp.c / weather_port_sim.c. A backend in an earlier static
 * archive may require -u weather_port_init. POSIX simulator needs -pthread.
 * Call init once at startup, tick every frame, then caption/open/render/tap.
 * Captions are empty when disabled. Clock lines retain the city separately
 * from the reading; callers fit them to their own bounded layout.
 * Due refreshes and explicit city searches lease the saved Wi-Fi network;
 * successful refreshes sleep Wi-Fi for 15 minutes, retaining cached readings.
 * Failed updates retain explicit stale/error labels during retry backoff.
 * Setup/update takes priority. Async settings saves need no radio lease.
 * No radio lease while disabled/unconfigured. Never infers a location.
 * Search sends only explicitly entered text; forecasts
 * send only selected coordinates to fixed Open-Meteo HTTPS endpoints.
 * ESP configuration uses independent NVS "weather"/"config1". Simulator:
 * POCKET_TANK_WEATHER chooses its config file (default weather-config.bin);
 * POCKET_TANK_WEATHER_SEARCH_FIXTURE and ..._CURRENT_FIXTURE select local
 * Open-Meteo JSON fixtures. Simulator never makes live network requests. */
#ifndef WEATHER_H
#define WEATHER_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define WEATHER_CITY_MAX 5
#define WEATHER_BODY_MAX 4096
typedef struct {
    char name[40], region[40], country[32];
    double latitude, longitude;
} weather_city_t;
typedef struct {
    uint32_t version;
    uint8_t enabled, fahrenheit, located, reserved;
    weather_city_t city;
} weather_config_t;
typedef struct { double celsius; int code; } weather_reading_t;

void weather_init(void);
void weather_tick(float dt);
bool weather_network_needed(void); /* current/upcoming lease or backend still active */
/* Release optional caches BEFORE tick(dt), including a deadline crossed by dt.
 * Invalid/nonpositive dt does not advance the prediction, matching tick.
 * Retain cache release until weather_network_needed() becomes false. */
bool weather_network_needed_after(float dt);
void weather_caption(char *out, size_t cap);
void weather_clock_lines(char *city, size_t city_cap, char *detail, size_t detail_cap);
int weather_icon_code(void); /* -1 without an enabled, located cached reading */
void weather_open(void);
void weather_render(uint16_t *fb, int stride, int text_size);
bool weather_tap(float x, float y); /* frame coordinates; true = close */
bool weather_size_hit(float x, float y);
void weather_size_status(bool saved);

/* Shared, bounded validators, also used by native tests. */
bool weather_config_valid(const weather_config_t *config);
bool weather_parse_cities(const char *json, size_t length, weather_city_t *out, int *count);
bool weather_parse_current(const char *json, size_t length, weather_reading_t *out);
const char *weather_condition(int code);

enum { WEATHER_SAVE, WEATHER_SEARCH, WEATHER_CURRENT };
enum { WEATHER_ERR_NONE, WEATHER_ERR_OFFLINE, WEATHER_ERR_MEMORY,
       WEATHER_ERR_HTTP, WEATHER_ERR_INVALID, WEATHER_ERR_SAVE };
typedef struct {
    int operation;
    uint32_t generation;
    weather_config_t config;
    char query[40];
} weather_request_t;
typedef struct {
    int operation;
    uint32_t generation;
    bool ok;
    int error;
    int count;
    weather_city_t cities[WEATHER_CITY_MAX];
    weather_reading_t reading;
} weather_result_t;
/* Port contract: one request outstanding; start/poll never block on I/O.
 * load only at init; missing/corrupt config leaves defaults untouched.
 * save failures are surfaced; never silently enable or infer a location. */
void weather_port_init(void);
bool weather_port_load(weather_config_t *out);
bool weather_port_start(const weather_request_t *request);
bool weather_port_poll(weather_result_t *out);
#endif
