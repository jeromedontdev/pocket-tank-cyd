/* Browser preview has no radio, credential store, firmware update or live weather. */
#include "update.h"
#include "weather.h"
#include <stdio.h>

void net_sim_advance(float dt) { (void)dt; }
bool net_port_creds_get(char *ssid, char *pass) { ssid[0] = pass[0] = 0; return false; }
bool net_port_creds_set(const char *ssid, const char *pass) { (void)ssid; (void)pass; return false; }
bool net_port_creds_forget(void) { return true; }
bool net_port_connected(void) { return false; }
void net_port_weather(bool wanted) { (void)wanted; }
bool net_port_weather_active(void) { return false; }
void net_port_scan_start(void) {}
int net_port_scan_state(void) { return NET_FAILED; }
int net_port_scan_results(net_ap_t *out, int max) { (void)out; (void)max; return 0; }
void net_port_connect_start(const char *ssid, const char *pass) { (void)ssid; (void)pass; }
int net_port_connect_state(void) { return NET_FAILED; }
void net_port_check_start(void) {}
int net_port_check_state(const update_manifest_t **m) { *m = NULL; return NET_FAILED; }
void net_port_install_start(void) {}
int net_port_install_state(int *percent) { *percent = 0; return NET_FAILED; }
void net_port_abort(void) {}
void net_port_off(void) {}
int net_port_fail_reason(void) { return NET_ERR_NO_NET; }

static weather_result_t result;
static bool ready;
void weather_port_init(void) {}
bool weather_port_load(weather_config_t *out) {
    FILE *file = fopen("weather.bin", "rb");
    if (!file) return false;
    bool ok = fread(out, 1, sizeof *out, file) == sizeof *out && weather_config_valid(out);
    fclose(file);
    return ok;
}
bool weather_port_start(const weather_request_t *request) {
    result = (weather_result_t){0};
    result.operation = request->operation;
    result.generation = request->generation;
    result.error = WEATHER_ERR_OFFLINE;
    if (request->operation == WEATHER_SAVE) {
        FILE *file = fopen("weather.bin.next", "wb");
        bool ok = file && weather_config_valid(&request->config);
        if (file) {
            if (ok) ok = fwrite(&request->config, 1, sizeof request->config, file) == sizeof request->config;
            if (fclose(file)) ok = false;
        }
        if (ok) ok = rename("weather.bin.next", "weather.bin") == 0;
        if (!ok) remove("weather.bin.next");
        result.ok = ok;
        result.error = ok ? WEATHER_ERR_NONE : WEATHER_ERR_SAVE;
    }
    ready = true;
    return true;
}
bool weather_port_poll(weather_result_t *out) {
    if (!ready) return false;
    *out = result; ready = false; return true;
}
