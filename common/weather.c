#include "weather.h"
#include "render.h"
#include "update.h"
#include "cJSON.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static weather_config_t cfg;
static weather_reading_t reading;
static bool busy, dirty, have, failed, searching;
static float due, age, search_wait;
static unsigned failures;
static uint32_t generation;
static char query[40], message[64];
static weather_city_t cities[WEATHER_CITY_MAX];
static int city_count, page, last_error, busy_operation;

static bool coord(double lat, double lon) {
    return isfinite(lat) && isfinite(lon) && lat >= -90 && lat <= 90 && lon >= -180 && lon <= 180;
}
static bool safe_string(const char *s, size_t n) {
    const char *end = memchr(s, 0, n);
    if (!end) return false;
    for (; s < end; ++s) if ((unsigned char)*s < 32 || (unsigned char)*s > 126) return false;
    return true;
}
bool weather_config_valid(const weather_config_t *c) {
    return c && c->version == 1 && c->enabled <= 1 && c->fahrenheit <= 1 &&
        c->located <= 1 && c->reserved == 0 && coord(c->city.latitude, c->city.longitude) &&
        safe_string(c->city.name, sizeof c->city.name) &&
        safe_string(c->city.region, sizeof c->city.region) &&
        safe_string(c->city.country, sizeof c->city.country) && (!c->located || c->city.name[0]);
}
static cJSON *parse(const char *s, size_t n) {
    if (!s || !n || n > WEATHER_BODY_MAX || memchr(s, 0, n)) return NULL;
    int depth = 0;
    bool quoted = false, escape = false;
    for (size_t i = 0; i < n; ++i) {
        char c = s[i];
        if (quoted) {
            if (escape) escape = false;
            else if (c == '\\') escape = true;
            else if (c == '"') quoted = false;
        } else if (c == '"') quoted = true;
        else if (c == '{' || c == '[') { if (++depth > 16) return NULL; }
        else if (c == '}' || c == ']') { if (--depth < 0) return NULL; }
    }
    if (quoted || depth) return NULL;
    const char *end = NULL;
    cJSON *j = cJSON_ParseWithLengthOpts(s, n, &end, false);
    if (!j) return NULL;
    while (end < s + n && (*end == ' ' || *end == '\r' || *end == '\n' || *end == '\t')) ++end;
    if (end != s + n || !cJSON_IsObject(j)) { cJSON_Delete(j); return NULL; }
    return j;
}
static bool number(cJSON *j, const char *key, double *out) {
    cJSON *v = cJSON_GetObjectItemCaseSensitive(j, key);
    if (!cJSON_IsNumber(v) || !isfinite(v->valuedouble)) return false;
    *out = v->valuedouble;
    return true;
}
/* The pixel font is ASCII. Collapse each non-ASCII codepoint to one '?'.
 * Never allow API strings to inject controls or terminal escapes. */
static void label(cJSON *j, const char *key, char *out, size_t cap) {
    cJSON *v = cJSON_GetObjectItemCaseSensitive(j, key);
    const unsigned char *s = (const unsigned char *)(cJSON_IsString(v) ? v->valuestring : "");
    size_t n = 0;
    while (*s && n + 1 < cap) {
        unsigned char c = *s++;
        if (c >= 128) {
            if ((c & 0xc0) == 0x80) continue;
            c = '?';
        }
        out[n++] = c >= 32 && c < 127 ? (char)c : ' ';
    }
    out[n] = 0;
}
bool weather_parse_cities(const char *s, size_t n, weather_city_t *out, int *count) {
    if (!out || !count) return false;
    *count = 0;
    cJSON *j = parse(s, n);
    if (!j) return false;
    cJSON *a = cJSON_GetObjectItemCaseSensitive(j, "results");
    bool ok = !a || cJSON_IsArray(a);
    if (cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(j, "error"))) ok = false;
    cJSON *v;
    if (ok) cJSON_ArrayForEach(v, a) {
        weather_city_t city = {0};
        if (!number(v, "latitude", &city.latitude) || !number(v, "longitude", &city.longitude) ||
            !coord(city.latitude, city.longitude)) continue;
        label(v, "name", city.name, sizeof city.name);
        label(v, "admin1", city.region, sizeof city.region);
        label(v, "country", city.country, sizeof city.country);
        if (!city.name[0]) continue;
        out[(*count)++] = city;
        if (*count == WEATHER_CITY_MAX) break;
    }
    cJSON_Delete(j);
    return ok;
}
const char *weather_condition(int c) {
    switch (c) {
    case 0: return "Clear";
    case 1: case 2: return "Partly cloudy";
    case 3: return "Overcast";
    case 45: case 48: return "Fog";
    case 51: case 53: case 55: return "Drizzle";
    case 56: case 57: case 66: case 67: return "Freezing rain";
    case 61: case 63: case 65: return "Rain";
    case 71: case 73: case 75: case 77: return "Snow";
    case 80: case 81: case 82: return "Showers";
    case 85: case 86: return "Snow showers";
    case 95: case 96: case 99: return "Thunderstorm";
    default: return NULL;
    }
}
bool weather_parse_current(const char *s, size_t n, weather_reading_t *out) {
    if (!out) return false;
    cJSON *j = parse(s, n);
    if (!j) return false;
    cJSON *v = cJSON_GetObjectItemCaseSensitive(j, "current");
    cJSON *units = cJSON_GetObjectItemCaseSensitive(j, "current_units");
    cJSON *unit = cJSON_GetObjectItemCaseSensitive(units, "temperature_2m");
    double temp = 0, code = 0, time = 0;
    bool ok = !cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(j, "error")) &&
        cJSON_IsString(unit) && !strcmp(unit->valuestring, "\xc2\xb0" "C") &&
        number(v, "temperature_2m", &temp) && temp >= -100 && temp <= 65 &&
        number(v, "weather_code", &code) && code >= 0 && code <= 99 &&
        floor(code) == code && weather_condition((int)code) &&
        number(v, "time", &time) && time >= 1577836800 && time <= 7258118400.0;
    if (ok) { out->celsius = temp; out->code = (int)code; }
    cJSON_Delete(j);
    return ok;
}
void weather_init(void) {
    memset(&cfg, 0, sizeof cfg);
    cfg.version = 1; cfg.fahrenheit = 1;
    weather_port_init();
    weather_config_t loaded;
    if (weather_port_load(&loaded) && weather_config_valid(&loaded)) cfg = loaded;
    busy = dirty = have = failed = searching = false;
    due = age = search_wait = 0; failures = 0; generation = 1;
    query[0] = message[0] = 0; city_count = page = 0;
    last_error = WEATHER_ERR_NONE;
}
static void changed(void) { dirty = true; ++generation; due = 0; failures = 0; }
static bool start(int op) {
    weather_request_t r = {0};
    r.operation = op; r.generation = generation; r.config = cfg;
    snprintf(r.query, sizeof r.query, "%s", query);
    if (!weather_port_start(&r)) {
        last_error = WEATHER_ERR_MEMORY;
        snprintf(message, sizeof message, "Low memory; retry later");
        due = 60;
        if (op == WEATHER_CURRENT) failed = true;
        if (op == WEATHER_SEARCH) searching = false;
        return false;
    }
    busy = true;
    busy_operation = op;
    return true;
}
static bool network_requested(void) {
    return searching || (busy && busy_operation != WEATHER_SAVE) ||
        (!busy && !dirty && cfg.enabled && cfg.located && due <= 0);
}
bool weather_network_needed_after(float dt) {
    float advance = isfinite(dt) && dt > 0 ? dt : 0;
    return (cfg.enabled && cfg.located && due <= advance) || searching ||
        (busy && busy_operation != WEATHER_SAVE) || net_port_weather_active();
}
bool weather_network_needed(void) { return weather_network_needed_after(0); }
void weather_tick(float dt) {
    if (isfinite(dt) && dt > 0) {
        if (have) age += dt;
        if (searching) search_wait += dt;
        if (due > 0) { due -= dt; if (due < 0) due = 0; }
    }
    weather_result_t r;
    if (busy && weather_port_poll(&r)) {
        busy = false;
        if (r.generation == generation) {
            if (r.operation == WEATHER_SAVE) {
                if (r.ok) { dirty = false; message[0] = 0; }
                else { snprintf(message, sizeof message, "Save failed; retrying"); due = 30; }
            } else if (r.operation == WEATHER_SEARCH) {
                searching = false;
                city_count = r.ok ? r.count : 0;
                if (city_count) { memcpy(cities, r.cities, sizeof cities); page = 2; }
                snprintf(message, sizeof message, "%s", !r.ok ?
                    r.error == WEATHER_ERR_MEMORY ? "Low memory; retry later" : "Search failed; try again" :
                    city_count ? "Select your city" : "No cities found");
            } else if (r.operation == WEATHER_CURRENT) {
                failed = !r.ok;
                last_error = r.error;
                snprintf(message, sizeof message, "%s", r.error == WEATHER_ERR_MEMORY ? "Low memory; retry later" : "");
                if (r.ok) { reading = r.reading; have = true; age = 0; failures = 0; due = 900; }
                else { if (failures < 5) ++failures; due = (float)(30u << failures); if (due > 900) due = 900; }
            }
        }
    }
    net_port_weather(network_requested());
    if (busy) return;
    if (dirty) { if (due <= 0) start(WEATHER_SAVE); return; }
    if (searching) {
        if (!net_port_connected()) {
            if (search_wait >= 30) {
                searching = false; page = 0;
                snprintf(message, sizeof message, "Offline: check saved Wi-Fi");
            }
        }
        else start(WEATHER_SEARCH);
    } else if (cfg.enabled && cfg.located && due <= 0 && net_port_connected()) {
        start(WEATHER_CURRENT);
    }
    net_port_weather(network_requested());
}
void weather_caption(char *out, size_t cap) {
    if (!out || !cap) return;
    if (!cfg.enabled) out[0] = 0;
    else if (!cfg.located) snprintf(out, cap, "Weather: choose city");
    else if (!have) snprintf(out, cap, "Weather: %s", !net_port_connected() ? "offline" :
        failed ? last_error == WEATHER_ERR_MEMORY ? "low memory" : "unavailable" : "updating");
    else {
        double t = cfg.fahrenheit ? reading.celsius * 1.8 + 32 : reading.celsius;
        const char *state = failed && last_error == WEATHER_ERR_OFFLINE ? " [offline]" :
            age >= 1800 || failed ? " [stale]" : "";
        snprintf(out, cap, "%s %.0f%c %s%s", cfg.city.name, t, cfg.fahrenheit ? 'F' : 'C', weather_condition(reading.code), state);
    }
}
void weather_clock_lines(char *city, size_t city_cap, char *detail, size_t detail_cap) {
    if (!city || !city_cap || !detail || !detail_cap) return;
    city[0] = detail[0] = 0;
    if (!cfg.enabled) return;
    snprintf(city, city_cap, "%s", cfg.located ? cfg.city.name : "Weather");
    if (!cfg.located) snprintf(detail, detail_cap, "Choose city");
    else if (!have) snprintf(detail, detail_cap, "%s", !net_port_connected() ? "Offline" :
        failed ? last_error == WEATHER_ERR_MEMORY ? "Low memory" : "Unavailable" : "Updating");
    else {
        double t = cfg.fahrenheit ? reading.celsius * 1.8 + 32 : reading.celsius;
        const char *state = failed && last_error == WEATHER_ERR_OFFLINE ? "OFFLINE " :
            age >= 1800 || failed ? "STALE " : "";
        snprintf(detail, detail_cap, "%s%.0f%c %s", state, t, cfg.fahrenheit ? 'F' : 'C', weather_condition(reading.code));
    }
}

static bool forget_confirm;
#define BG 0x0c1720
#define INK 0xe3edf2
#define MUTED 0xa8bcc9
#define BUTTON 0x1c2f36
static void text(uint16_t *fb, int st, int x, int y, const char *s) {
    render_text(fb, st, x, y, 2, INK, s);
}
static void bounded_text(uint16_t *fb, int st, int x, int y, int width, int scale, uint32_t color, const char *s) {
    char line[100];
    snprintf(line, sizeof line, "%s", s);
    size_t n = strlen(line);
    if (render_text_w(line, scale) > width) {
        while (n && render_text_w(line, scale) > width) line[--n] = 0;
        if (n >= 3) memcpy(line + n - 3, "...", 3);
    }
    render_text(fb, st, x, y, scale, color, line);
}
static void button(uint16_t *fb, int st, int x, int y, int w, int h, const char *s) {
    render_rect(fb, st, x, y, w, h, BUTTON);
    text(fb, st, x + (w - render_text_w(s, 2)) / 2, y + (h - 14) / 2, s);
}
void weather_open(void) { page = 0; forget_confirm = false; message[0] = 0; }
int weather_icon_code(void) { return cfg.enabled && cfg.located && have ? reading.code : -1; }
void weather_size_status(bool saved) {
    snprintf(message, sizeof message, "%s", saved ? "Weather/event text size saved" : "Text size save failed - retry");
}
bool weather_size_hit(float x, float y) {
    x = render_page_x(x); y = render_page_y(y);
    return page == 0 && x >= 232 && x < 416 && y >= 268 && y < 308;
}
void weather_render(uint16_t *fb, int st, int text_size) {
    render_rect(fb, st, 0, 0, PAGE_W, PAGE_H, BG);
    text(fb, st, 24, 18, page == 1 ? "CITY SEARCH" : page == 2 ? "CHOOSE CITY" : "WEATHER");
    button(fb, st, 326, 8, 112, 40, "BACK");
    if (page == 0) {
        char line[100];
        button(fb, st, 32, 64, 184, 40, cfg.enabled ? "WEATHER: ON" : "WEATHER: OFF");
        button(fb, st, 232, 64, 184, 40, cfg.fahrenheit ? "UNITS: F" : "UNITS: C");
        bounded_text(fb, st, 32, 124, 384, 2, INK, cfg.located ? cfg.city.name : "NO CITY SELECTED");
        if (cfg.located) {
            snprintf(line, sizeof line, "%.45s / %.45s", cfg.city.region, cfg.city.country);
            bounded_text(fb, st, 32, 148, 384, 1, MUTED, line);
        }
        button(fb, st, 32, 212, 384, 44, cfg.located ? "CHANGE CITY" : "CHOOSE CITY");
        if (cfg.located) button(fb, st, 32, 268, 184, 40, forget_confirm ? "CONFIRM" : "FORGET CITY");
        static const char *const sizes[] = {"TEXT: SMALL", "TEXT: MEDIUM", "TEXT: LARGE"};
        button(fb, st, 232, 268, 184, 40, sizes[text_size >= 1 && text_size <= 3 ? text_size - 1 : 0]);
        char city[40];
        weather_clock_lines(city, sizeof city, line, sizeof line);
        if (!cfg.enabled) snprintf(line, sizeof line, "Weather disabled (no requests)");
        if (weather_icon_code() >= 0) render_weather_icon(fb, st, 32, 172, 28, reading.code);
        bounded_text(fb, st, weather_icon_code() >= 0 ? 72 : 32, 180, weather_icon_code() >= 0 ? 344 : 384,
                     text_size >= 1 && text_size <= 3 ? text_size : 1, INK, line);
        render_text(fb, st, 32, 318, 1, MUTED, "CHOOSE CITY - NO AUTO LOCATION");
        render_text(fb, st, 32, 336, 1, MUTED, "OPEN-METEO / GEONAMES: 15M");
    } else if (page == 1) {
        render_rect(fb, st, 12, 56, 424, 32, BUTTON);
        bounded_text(fb, st, 20, 65, 408, 2, INK, query[0] ? query : "Type a city name");
        bounded_text(fb, st, 12, 100, 424, 2, INK, searching ? net_port_connected() ? "Searching..." : "Connecting to Wi-Fi..." :
            busy ? "Working..." : message[0] ? message : "City letters, space, - or '");
        static const char *rows[] = {"QWERTYUIOP", "ASDFGHJKL", "ZXCVBNM"};
        for (int row = 0; row < 3; ++row) {
            int count = (int)strlen(rows[row]), x = (PAGE_W - count * 43) / 2;
            for (int i = 0; i < count; ++i) {
                char key[2] = {rows[row][i], 0};
                button(fb, st, x + i * 43, 130 + row * 46, 40, 42, key);
            }
        }
        button(fb, st, 10, 272, 64, 40, "-");
        button(fb, st, 80, 272, 64, 40, "'");
        button(fb, st, 150, 272, 134, 40, "Space");
        button(fb, st, 290, 272, 148, 40, "Delete");
        button(fb, st, 12, 320, 424, 40, "SEARCH");
    } else {
        for (int i = 0; i < city_count; ++i) {
            int y = 56 + i * 56;
            char line[80];
            render_rect(fb, st, 10, y, 428, 52, BUTTON);
            bounded_text(fb, st, 18, y + 5, 412, 2, INK, cities[i].name);
            snprintf(line, sizeof line, "%.29s / %.28s", cities[i].region, cities[i].country);
            bounded_text(fb, st, 18, y + 29, 412, 1, MUTED, line);
        }
    }
    if (message[0] && page != 1) bounded_text(fb, st, 24, 354, 400, 1, INK, message);
}
bool weather_tap(float fx, float fy) {
    if (!isfinite(fx) || !isfinite(fy) || fx < 0 || fy < 0 || fx >= TANK_W || fy >= TANK_H) return false;
    int x = (int)render_page_x(fx), y = (int)render_page_y(fy);
    if (x >= 326 && x < 438 && y >= 8 && y < 48) {
        if (page) {
            page = page == 2 ? 1 : 0;
            message[0] = 0; forget_confirm = false;
            if (searching) { searching = false; ++generation; }
        } else return true;
        return false;
    }
    if (page == 0) {
        if (y >= 64 && y < 104) {
            if (x >= 32 && x < 216) cfg.enabled = !cfg.enabled;
            else if (x >= 232 && x < 416) cfg.fahrenheit = !cfg.fahrenheit;
            else return false;
            forget_confirm = false;
            changed();
        } else if (y >= 212 && y < 256 && x >= 32 && x < 416) {
            page = 1; forget_confirm = false; query[0] = message[0] = 0;
        } else if (y >= 268 && y < 308 && x >= 32 && x < 216 && cfg.located) {
            if (!forget_confirm) {
                forget_confirm = true; snprintf(message, sizeof message, "Tap CONFIRM to forget the saved city");
            } else {
                memset(&cfg.city, 0, sizeof cfg.city); cfg.located = 0;
                have = failed = forget_confirm = false; changed();
            }
        }
    } else if (page == 2) {
        int i = (y - 56) / 56;
        if (x >= 10 && x < 438 && y >= 56 && i < city_count && (y - 56) % 56 < 52) {
            cfg.city = cities[i]; cfg.located = 1; have = failed = false;
            changed(); page = 0;
        }
    } else if (!searching) {
        size_t n = strlen(query);
        char c = 0;
        if (y >= 130 && y < 268) {
            static const char *rows[] = {"QWERTYUIOP", "ASDFGHJKL", "ZXCVBNM"};
            int row = (y - 130) / 46, left = (PAGE_W - (int)strlen(rows[row]) * 43) / 2;
            int i = (x - left) / 43;
            if (x >= left && i < (int)strlen(rows[row]) && (x - left) % 43 < 40 && (y - 130) % 46 < 42) c = rows[row][i];
        } else if (y >= 272 && y < 312) {
            if (x >= 10 && x < 74) c = '-';
            else if (x >= 80 && x < 144) c = '\'';
            else if (x >= 150 && x < 284) c = ' ';
            else if (x >= 290 && x < 438 && n) query[n - 1] = 0;
        } else if (y >= 320 && y < 360) {
            if (x >= 12 && x < 436) {
                if (n >= 2) { searching = true; search_wait = 0; city_count = 0; message[0] = 0; }
                else snprintf(message, sizeof message, "Enter at least two letters");
            }
        }
        if (c && n < sizeof query - 1) { query[n] = c; query[n + 1] = 0; message[0] = 0; }
    }
    return false;
}
