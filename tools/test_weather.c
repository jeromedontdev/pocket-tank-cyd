#include "weather.h"
#include "render.h"
#include "update.h"
#undef NDEBUG
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <time.h>
#endif

int tank_width = 320, tank_height = 240;
static int draw_calls;
void render_weather_icon(uint16_t *fb, int st, int x, int y, int size, int code) {
    (void)fb; (void)st;
    assert(x >= 0 && y >= 0 && x + size <= PAGE_W && y + size <= PAGE_H && weather_condition(code));
    ++draw_calls;
}
static bool saw_search_failed, saw_ellipsis;
void render_text(uint16_t *f, int s, int x, int y, int z, uint32_t c, const char *t) {
    (void)f; (void)s; (void)c; assert(t); ++draw_calls;
    assert(x >= 0 && y >= 0 && x + (int)strlen(t) * 6 * z <= PAGE_W && y + 7 * z <= PAGE_H);
    if (strstr(t, "Search failed")) saw_search_failed = true;
    if (strstr(t, "...")) saw_ellipsis = true;
}
int render_text_w(const char *s, int z) { return (int)strlen(s) * 6 * z; }
int render_text_wrap(uint16_t *f, int s, int x, int y, int w, uint32_t c, const char *t) {
    (void)f; (void)s; (void)c; assert(t);
    assert(x >= 0 && y >= 0 && w > 0 && x + w <= PAGE_W);
    ++draw_calls; return 20;
}
void render_rect(uint16_t *f, int s, int x, int y, int w, int h, uint32_t c) {
    (void)f; (void)s; (void)c;
    assert(x >= 0 && y >= 0 && w >= 0 && h >= 0 && x + w <= PAGE_W && y + h <= PAGE_H);
    ++draw_calls;
}
extern void net_sim_advance(float dt);
extern bool net_sim_radio_on(void);
extern void net_sim_disconnect(void);
static void pause_worker(void) {
#ifdef _WIN32
    Sleep(1);
#else
    struct timespec t = {0, 1000000};
    nanosleep(&t, NULL);
#endif
}
static void pump(void) {
    for (int i = 0; i < 120; ++i) { net_sim_advance(0.02f); weather_tick(0.001f); pause_worker(); }
}
static void leases(void) {
    net_port_weather(false); net_port_off();
    assert(!net_sim_radio_on());
    assert(net_port_creds_set("Fishbowl", "password"));
    net_port_weather(false); net_sim_advance(3);
    assert(!net_sim_radio_on());
    net_port_weather(true); net_sim_advance(2);
    assert(net_port_connected());
    net_port_off(); /* setup exit hands the association back to weather */
    assert(net_port_connected() && net_sim_radio_on());
    net_sim_disconnect(); net_sim_advance(2);
    assert(net_port_connected());
    net_port_scan_start();
    net_port_weather(false); /* disabled weather must not stop setup */
    assert(net_sim_radio_on() && net_port_scan_state() == NET_BUSY);
    net_sim_advance(2); assert(net_port_scan_state() == NET_DONE);
    net_port_weather(true); net_port_off(); net_sim_advance(2);
    assert(net_port_connected());
    net_port_weather(false); assert(!net_sim_radio_on());
    assert(net_port_creds_forget());
    net_port_weather(true); net_sim_advance(3);
    assert(!net_sim_radio_on()); /* no credentials: never guess a network */
    net_port_weather(false); net_port_off();
    assert(net_port_creds_set("Fishbowl", "password"));
}
static weather_result_t await(void) {
    weather_result_t r;
    for (int i = 0; i < 3000; ++i) {
        if (weather_port_poll(&r)) return r;
        pause_worker();
    }
    assert(!"worker timed out");
    memset(&r, 0, sizeof r);
    return r;
}
static void tap(int x, int y) {
    bool closed = weather_tap(render_frame_x((float)x), render_frame_y((float)y));
    if (closed) fprintf(stderr, "Unexpected weather exit at %d,%d\n", x, y);
    assert(!closed);
}
static const char *current =
    "{\"current\":{\"temperature_2m\":25,\"weather_code\":0,\"time\":1792760400},"
    "\"current_units\":{\"temperature_2m\":\"\\u00b0C\"}}";
static const char *search =
    "{\"results\":[{\"name\":\"Paris\",\"admin1\":\"Ile-de-France\",\"country\":\"France\","
    "\"latitude\":48.85,\"longitude\":2.35},"
    "{\"name\":\"Paris\",\"admin1\":\"Texas\",\"country\":\"United States\",\"latitude\":33.66,\"longitude\":-95.55}]}";
static void parsers(void) {
    weather_city_t cities[WEATHER_CITY_MAX];
    int count = 99;
    assert(weather_parse_cities(search, strlen(search), cities, &count) && count == 2);
    assert(!strcmp(cities[1].country, "United States"));
    assert(weather_parse_cities("{}", 2, cities, &count) && count == 0);
    const char *invalid = "{\"results\":[{\"name\":\"X\",\"latitude\":91,\"longitude\":0}]}";
    assert(weather_parse_cities(invalid, strlen(invalid), cities, &count) && count == 0);
    invalid = "{\"error\":true}";
    assert(!weather_parse_cities(invalid, strlen(invalid), cities, &count));
    invalid = "{\"results\":false}";
    assert(!weather_parse_cities(invalid, strlen(invalid), cities, &count));
    weather_reading_t reading;
    assert(weather_parse_current(current, strlen(current), &reading) && reading.celsius == 25 && reading.code == 0);
    assert(!weather_parse_current(current, strlen(current) - 1, &reading));
    assert(!weather_parse_current("{} garbage", 10, &reading));
    assert(!weather_parse_current("{}", WEATHER_BODY_MAX + 1, &reading));
    assert(!weather_parse_current(NULL, 1, &reading));
    char deep[80];
    memset(deep, '[', 20); memset(deep + 20, ']', 20);
    assert(!weather_parse_current(deep, 40, &reading));
    const char *bad[] = {
        "{\"current\":{\"temperature_2m\":999,\"weather_code\":0,\"time\":1792760400},\"current_units\":{\"temperature_2m\":\"\\u00b0C\"}}",
        "{\"current\":{\"temperature_2m\":25,\"weather_code\":4,\"time\":1792760400},\"current_units\":{\"temperature_2m\":\"\\u00b0C\"}}",
        "{\"current\":{\"temperature_2m\":25,\"weather_code\":2.5,\"time\":1792760400},\"current_units\":{\"temperature_2m\":\"\\u00b0C\"}}",
        "{\"current\":{\"temperature_2m\":25,\"weather_code\":0,\"time\":0},\"current_units\":{\"temperature_2m\":\"\\u00b0C\"}}",
        "{\"current\":{\"temperature_2m\":25,\"weather_code\":0,\"time\":1792760400},\"current_units\":{\"temperature_2m\":\"\\u00b0F\"}}",
        "{\"current\":{\"temperature_2m\":null,\"weather_code\":0,\"time\":1792760400}}"
    };
    for (size_t i = 0; i < sizeof bad / sizeof *bad; ++i) assert(!weather_parse_current(bad[i], strlen(bad[i]), &reading));
    assert(weather_condition(99) && !weather_condition(-1) && !weather_condition(100));
    weather_config_t c = {0}; c.version = 1; c.fahrenheit = 1;
    assert(weather_config_valid(&c));
    c.located = 1; assert(!weather_config_valid(&c));
    strcpy(c.city.name, "City"); assert(weather_config_valid(&c));
    c.city.latitude = NAN; assert(!weather_config_valid(&c));
}
static void write_fixture(const char *name, const char *body) {
    FILE *f = fopen(getenv(name), "wb"); assert(f);
    assert(fwrite(body, 1, strlen(body), f) == strlen(body)); assert(!fclose(f));
}
static void verify_file(const char *path, bool cities) {
    FILE *f = fopen(path, "rb"); assert(f);
    char body[WEATHER_BODY_MAX + 1];
    size_t n = fread(body, 1, sizeof body, f);
    assert(!ferror(f) && n <= WEATHER_BODY_MAX); fclose(f);
    weather_city_t city[WEATHER_CITY_MAX];
    weather_reading_t r;
    int count;
    if (cities) {
        assert(weather_parse_cities(body, n, city, &count) && count > 0);
        printf("Verified real geocoding fixture: %s / %s\n", city[0].name, city[0].country);
    } else {
        assert(weather_parse_current(body, n, &r));
        printf("Verified real current fixture: %.1fC / %s\n", r.celsius, weather_condition(r.code));
    }
}
int main(int argc, char **argv) {
    if (argc == 3) { verify_file(argv[1], true); verify_file(argv[2], false); }
    assert(getenv("POCKET_TANK_WEATHER"));
    parsers();
    leases();
    write_fixture("POCKET_TANK_WEATHER_SEARCH_FIXTURE", search);
    write_fixture("POCKET_TANK_WEATHER_CURRENT_FIXTURE", current);
    weather_init();
    char caption[128];
    weather_caption(caption, sizeof caption); assert(!caption[0]);
    weather_tick(NAN); weather_tick(-1); weather_caption(NULL, 0);
    assert(!weather_tap(NAN, 1) && !weather_tap(-1, 1));
    weather_open(); weather_render(NULL, TANK_W, 1); assert(draw_calls > 10);
    assert(weather_tap(render_frame_x(390), render_frame_y(20)));
    tap(100, 68); pump();
    assert(!weather_network_needed() && !net_sim_radio_on());
    weather_caption(caption, sizeof caption); assert(strstr(caption, "choose city"));
    weather_config_t saved;
    assert(weather_port_load(&saved) && saved.enabled && saved.fahrenheit && !saved.located);
    /* Explicit city search, with the complete QWERTY keyboard. */
    net_port_connect_start("Fishbowl", "password");
    net_sim_advance(2);
    assert(net_port_connected());
    net_port_off(); /* weather searches must acquire their own temporary lease */
    tap(140, 234); weather_render(NULL, TANK_W, 1);
    tap(320, 340); weather_render(NULL, TANK_W, 1); /* Short searches stay on the keyboard. */
    /* A failed search stays on the keyboard and says so (not the idle hint). */
    remove(getenv("POCKET_TANK_WEATHER_SEARCH_FIXTURE"));
    tap(418, 150); tap(52, 196); tap(320, 340); pump();
    saw_search_failed = false; weather_render(NULL, TANK_W, 1); assert(saw_search_failed);
    assert(!weather_network_needed() && !net_sim_radio_on());
    write_fixture("POCKET_TANK_WEATHER_SEARCH_FIXTURE", search);
    tap(360, 290); tap(360, 290); /* Delete the previous two letters. */
    tap(418, 150); /* P */
    tap(52, 196);  /* A */
    tap(160, 150); /* R */
    tap(332, 150); /* I */
    tap(95, 196);  /* S */
    tap(320, 340); pump(); weather_render(NULL, TANK_W, 1);
    tap(150, 65); pump();
    weather_caption(caption, sizeof caption);
    assert(strstr(caption, "Paris") && strstr(caption, "77F") && strlen(caption) <= 38);
    assert(!weather_network_needed() && !net_sim_radio_on() && !net_port_connected());
    assert(!strstr(caption, "[offline]") && !strstr(caption, "[stale]"));
    tap(330, 68); pump();
    weather_caption(caption, sizeof caption); assert(strstr(caption, "25C"));
    assert(weather_port_load(&saved) && saved.located && !saved.fahrenheit);
    assert(!strcmp(saved.city.country, "France"));
    /* Sleeping Wi-Fi retains fresh values and makes no request before one hour. */
    char city[64], detail[64];
    weather_clock_lines(city, sizeof city, detail, sizeof detail);
    assert(strstr(detail, "25C") && !strstr(detail, "OFFLINE") && !strstr(detail, "STALE"));
    remove(getenv("POCKET_TANK_WEATHER_CURRENT_FIXTURE"));
    weather_tick(3590); pump();
    assert(!weather_network_needed() && !net_sim_radio_on());
    weather_caption(caption, sizeof caption);
    assert(strstr(caption, "25C") && !strstr(caption, "[stale]"));
    /* Reaching the deadline reconnects without a foreground Wi-Fi session. */
    write_fixture("POCKET_TANK_WEATHER_CURRENT_FIXTURE",
        "{\"current\":{\"temperature_2m\":26,\"weather_code\":0,\"time\":1792760400},"
        "\"current_units\":{\"temperature_2m\":\"\\u00b0C\"}}");
    /* Predict a deadline crossed inside the next frame before starting Wi-Fi. */
    for (int i = 0; !weather_network_needed_after(0.2f); ++i) {
        assert(i < 100);
        weather_tick(0.125f);
    }
    assert(!weather_network_needed() && !net_sim_radio_on());
    assert(!weather_network_needed_after(0));
    assert(!weather_network_needed_after(-1));
    assert(!weather_network_needed_after(NAN));
    assert(!weather_network_needed_after(INFINITY));
    weather_tick(0.2f);
    assert(weather_network_needed());
    pump();
    weather_caption(caption, sizeof caption);
    assert(strstr(caption, "26C") && !strstr(caption, "[stale]"));
    assert(!weather_network_needed() && !net_sim_radio_on());
    /* Failed updates retain values, label stale, and sleep through backoff. */
    remove(getenv("POCKET_TANK_WEATHER_CURRENT_FIXTURE"));
    weather_tick(3601); pump();
    weather_caption(caption, sizeof caption); assert(strstr(caption, "[stale]") && strstr(caption, "26C"));
    weather_clock_lines(city, sizeof city, detail, sizeof detail);
    assert(strstr(detail, "STALE") && !strstr(detail, "OFFLINE"));
    assert(!weather_network_needed() && !net_sim_radio_on());
    write_fixture("POCKET_TANK_WEATHER_CURRENT_FIXTURE", current);
    pump(); weather_caption(caption, sizeof caption); assert(strstr(caption, "[stale]"));
    weather_tick(3590); pump();
    assert(!weather_network_needed() && !net_sim_radio_on());
    weather_caption(caption, sizeof caption); assert(strstr(caption, "[stale]"));
    weather_tick(11); pump(); weather_caption(caption, sizeof caption); assert(!strstr(caption, "[stale]"));
    assert(strstr(caption, "25C") && !weather_network_needed() && !net_sim_radio_on());
    /* Disabling invalidates in-flight results and persists independently. */
    weather_tick(3601); net_sim_advance(2); weather_tick(0);
    tap(100, 68);
    assert(weather_network_needed()); /* pending CURRENT must drain before release */
    pump();
    weather_caption(caption, sizeof caption); assert(!caption[0]);
    assert(!weather_network_needed() && !net_sim_radio_on());
    weather_init(); weather_caption(caption, sizeof caption); assert(!caption[0]);
    assert(weather_port_load(&saved) && !saved.enabled && saved.located);
    /* Re-enable schedules a refresh; unit saves still work with the radio off. */
    tap(100, 68); assert(weather_network_needed()); pump();
    weather_caption(caption, sizeof caption); assert(strstr(caption, "25C"));
    assert(!weather_network_needed() && !net_sim_radio_on());
    tap(100, 68); pump();
    tap(330, 68); assert(!weather_network_needed()); pump();
    assert(weather_port_load(&saved) && !saved.enabled && saved.fahrenheit);
    assert(!net_sim_radio_on());
    /* Search also works while disabled, then relinquishes its lease. */
    tap(140, 234); tap(418, 150); tap(52, 196); tap(320, 340);
    assert(weather_network_needed()); pump();
    assert(!weather_network_needed() && !net_sim_radio_on());
    tap(390, 20);
    tap(390, 20); /* Results -> query -> weather, one level at a time. */
    /* Cancelling a pending search must stop background reconnect too. */
    tap(140, 234); tap(418, 150); tap(52, 196); tap(320, 340);
    weather_tick(0); assert(weather_network_needed());
    tap(390, 20); weather_tick(0);
    assert(!weather_network_needed() && !net_sim_radio_on());
    /* A search that cannot connect times out and releases the radio lease. */
    assert(net_port_creds_forget());
    tap(140, 234); tap(418, 150); tap(52, 196); tap(320, 340);
    weather_tick(31);
    assert(!weather_network_needed() && !net_sim_radio_on());
    assert(net_port_creds_set("Fishbowl", "password"));
    /* A boot refresh with missing credentials stops after 30 seconds, then
     * stays asleep for an hour even if credentials become available. */
    tap(100, 68); pump();
    assert(weather_port_load(&saved) && saved.enabled);
    assert(net_port_creds_forget());
    weather_init();
    weather_tick(0);
    assert(weather_network_needed());
    weather_tick(31); pump();
    assert(!weather_network_needed() && !net_sim_radio_on());
    assert(net_port_creds_set("Fishbowl", "password"));
    weather_tick(3590); pump();
    assert(!weather_network_needed() && !net_sim_radio_on());
    weather_tick(11); pump();
    assert(!weather_network_needed() && !net_sim_radio_on());
    weather_caption(caption, sizeof caption); assert(strstr(caption, "77F"));
    /* Direct port mailbox carries generation and bounded parsed fixtures. */
    weather_request_t request = {0};
    request.operation = WEATHER_SEARCH; request.generation = 123; strcpy(request.query, "Paris");
    assert(weather_port_start(&request)); assert(!weather_port_start(&request));
    weather_result_t result = await();
    assert(result.ok && result.count == 2 && result.generation == 123);
    tap(100, 288); pump();
    assert(weather_port_load(&saved) && saved.located); /* Forget requires confirmation. */
    tap(100, 288); pump();
    assert(weather_port_load(&saved) && !saved.located && !saved.city.name[0]);
    FILE *f = fopen(getenv("POCKET_TANK_WEATHER"), "wb"); assert(f);
    fputs("corrupt", f); fclose(f);
    weather_init();
    assert(!weather_port_load(&saved));
    weather_caption(caption, sizeof caption); assert(!caption[0]);
    /* Long cities retain their identity; UI labels are bounded with ellipses,
     * and clock lines never spend the city budget on condition/status text. */
    memset(&request, 0, sizeof request);
    request.operation = WEATHER_SAVE; request.config.version = 1;
    request.config.enabled = request.config.located = 1;
    strcpy(request.config.city.name, "Saint-Remy-en-Bouzemont-Saint-Genest");
    strcpy(request.config.city.region, "A region with a very long display name");
    strcpy(request.config.city.country, "A country with a long name");
    assert(weather_port_start(&request)); result = await(); assert(result.ok);
    weather_init(); saw_ellipsis = false;
    weather_render(NULL, TANK_W, 3); assert(saw_ellipsis);
    weather_clock_lines(city, sizeof city, detail, sizeof detail);
    assert(!strcmp(city, request.config.city.name) && detail[0]);
    weather_clock_lines(NULL, 0, NULL, 0);
    weather_caption(caption, 1); assert(!caption[0]);
    const char *long_search = "{\"results\":[{\"name\":\"Saint-Remy-en-Bouzemont-Saint-Genest\","
        "\"admin1\":\"A region with a very long display name\",\"country\":\"A country with a long name\","
        "\"latitude\":48,\"longitude\":2}]}";
    write_fixture("POCKET_TANK_WEATHER_SEARCH_FIXTURE", long_search);
    tap(140, 234); tap(418, 150); tap(52, 196);
    tap(320, 340); pump(); saw_ellipsis = false;
    weather_render(NULL, TANK_W, 3); assert(saw_ellipsis);
    tap(390, 20); tap(390, 20); tap(100, 68); pump();
    puts("Weather: parser, config, mailbox, QWERTY, units, boot/hourly success/failure refresh, bounded offline attempts and Wi-Fi ownership tests passed");
    return 0;
}
