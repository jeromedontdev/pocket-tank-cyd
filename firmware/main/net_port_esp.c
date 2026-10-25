/* net_port_esp.c - the update flow's net port on the tank (common/update.h,
 * docs/OTA.md): the credential store, the Wi-Fi station, the manifest fetch
 * and the verified download into the other app slot.
 *
 * Runs only in update mode, at boot, before the tank exists (update_mode.c),
 * so the driver has the whole internal heap. Every call from the flow
 * returns at once: a worker task does the blocking work and the flow polls
 * the state each frame. net_port_off tears the whole stack down on the way
 * out - the radio is off whenever the tank is running.
 *
 *   the store     NVS namespace "wifi": ssid, pass, url (the director's
 *                 manifest override - the test channel). Its own namespace:
 *                 the tank's reset wipes "tank", never this.
 *   the manifest  latest stable GitHub release, with a tag-pinned app URL;
 *                 requires fork, board, release and app{url,size,sha256}.
 *                 See common/update.c for the shared parser and board gate.
 *   the board     every image carries a board marker right after its app
 *                 descriptor (pt_board_marker below, PT_BOARD_MARKER_OFFSET):
 *                 the download reads it back from the slot at the first
 *                 sector and stops if it is not the CYD board marker.
 *   the download  esp_https_ota into the other slot; exact size, board,
 *                 embedded release and full-file SHA-256 before activation.
 *                 Standard releases trust HTTPS, not publisher signatures.
 *   transport     HTTPS with the IDF certificate bundle; redirects followed
 *                 (GitHub's release assets redirect to a second host) */
#include "update.h"
#include "version.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_crt_bundle.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "psa/crypto.h"
#include "nvs.h"
#include "net_time.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include <string.h>
#include <stdlib.h>
#include <stdatomic.h>

static const char *TAG = "net";
#define WIFI_NVS_NS   "wifi"
static void forget_radio(void);
#include "sdkconfig.h"
/* the board marker: in .rodata_custom_desc, which the linker places right
 * after esp_app_desc_t - a fixed offset in the image (PT_BOARD_MARKER_OFFSET;
 * tools/make_ota_manifest.py and make_installer.py read it back from the .bin) */
typedef struct { char magic[8]; char board[24]; } pt_board_marker_t;
const __attribute__((section(".rodata_custom_desc"), used)) pt_board_marker_t pt_board_marker = { PT_BOARD_MAGIC, PT_BOARD };

/* ---- the store ---- */
static bool nvs_get_str_ns(const char *key, char *out, size_t cap) {
    nvs_handle_t h; if (nvs_open(WIFI_NVS_NS, NVS_READONLY, &h) != ESP_OK) return false;
    size_t n = cap; esp_err_t e = nvs_get_str(h, key, out, &n); nvs_close(h);
    if (e != ESP_OK) { out[0] = 0; return false; }
    return true;
}
bool net_port_creds_get(char *ssid, char *pass) {
    if (!nvs_get_str_ns("ssid", ssid, NET_SSID_MAX + 1) || !ssid[0]) return false;
    if (!nvs_get_str_ns("pass", pass, NET_PASS_MAX + 1)) pass[0] = 0;
    return true;
}
bool net_port_creds_set(const char *ssid, const char *pass) {
    nvs_handle_t h;
    esp_err_t err = nvs_open(WIFI_NVS_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) { ESP_LOGE(TAG, "network save: NVS open failed: %s", esp_err_to_name(err)); return false; }
    bool ok = nvs_set_str(h, "ssid", ssid) == ESP_OK && nvs_set_str(h, "pass", pass ? pass : "") == ESP_OK && nvs_commit(h) == ESP_OK;
    nvs_close(h);
    if (ok) ESP_LOGI(TAG, "network saved: %s (%s)", ssid, pass && pass[0] ? "with a password" : "open");
    else ESP_LOGE(TAG, "network save failed");
    return ok;
}
bool net_port_creds_forget(void) {
    nvs_handle_t h;
    esp_err_t err = nvs_open(WIFI_NVS_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) { ESP_LOGE(TAG, "network forget: NVS open failed: %s", esp_err_to_name(err)); return false; }
    esp_err_t ssid = nvs_erase_key(h, "ssid"), pass = nvs_erase_key(h, "pass");
    bool ok = (ssid == ESP_OK || ssid == ESP_ERR_NVS_NOT_FOUND) && (pass == ESP_OK || pass == ESP_ERR_NVS_NOT_FOUND) && nvs_commit(h) == ESP_OK;
    nvs_close(h);
    if (ok) { forget_radio(); ESP_LOGI(TAG, "network forgotten"); }
    else ESP_LOGE(TAG, "network forget failed");
    return ok;
}
const char *net_port_manifest_url(void) {                   /* the director's override, else the release channel */
    static char url[NET_URL_MAX + 1];
    if (nvs_get_str_ns("url", url, sizeof url) && url[0]) return url;
    return CONFIG_POCKET_TANK_OTA_URL;
}
bool net_port_set_manifest_url(const char *url) {           /* NULL / "" = back to the default */
    nvs_handle_t h; if (nvs_open(WIFI_NVS_NS, NVS_READWRITE, &h) != ESP_OK) return false;
    bool ok = url && url[0] ? nvs_set_str(h, "url", url) == ESP_OK : (nvs_erase_key(h, "url"), true);
    ok = nvs_commit(h) == ESP_OK && ok; nvs_close(h);
    return ok;
}

/* ---- the radio ---- */
enum { OP_NONE, OP_SCAN, OP_CONNECT, OP_CHECK, OP_INSTALL };
static volatile int  s_op, s_state, s_err, s_percent;
static volatile bool s_abort;
static TaskHandle_t  s_task;
static EventGroupHandle_t s_ev;
#define EV_GOT_IP       (1 << 0)
#define EV_DISCONNECTED (1 << 1)
static esp_netif_t  *s_netif;
static bool          s_up;
static atomic_bool   s_connected;
static atomic_bool   s_weather, s_foreground, s_background_busy, s_weather_reconnect;
static atomic_bool   s_weather_radio;
static int64_t       s_weather_retry;
static int64_t       s_weather_init_retry;
static unsigned      s_weather_failures;
bool net_port_connected(void) { return atomic_load(&s_connected); }
bool net_port_weather_active(void) {
    return atomic_load(&s_weather) || atomic_load(&s_weather_radio) || atomic_load(&s_background_busy);
}
static void forget_radio(void) {
    s_connected = false;
    if (s_up) esp_wifi_disconnect();
    atomic_store(&s_weather_reconnect, true);
}
static int           s_disc_reason;
static char          s_ssid[NET_SSID_MAX + 1], s_pass[NET_PASS_MAX + 1];
static net_ap_t      s_aps[NET_SCAN_MAX]; static int s_n_aps;
static update_manifest_t s_m; static char s_app_url[NET_URL_MAX + 1];
static esp_https_ota_handle_t s_ota;

static void on_wifi(void *arg, esp_event_base_t base, int32_t id, void *data) {
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t *d = data;
        s_disc_reason = d->reason; s_connected = false;
        xEventGroupSetBits(s_ev, EV_DISCONNECTED);
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        s_connected = true;
        xEventGroupSetBits(s_ev, EV_GOT_IP);
    }
}
static void log_dram(const char *what) {
    ESP_LOGI(TAG, "%s: 8-bit internal free %u, largest %u; all internal free %u",
             what, (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
}
static bool radio_up(void) {
    if (s_up) return true;
    log_dram("radio up (before)");
    ESP_LOGI(TAG, "radio up: internal heap %u KB, largest %u KB, psram %u KB free before",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024,
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) / 1024,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024);
    if (esp_netif_init() != ESP_OK) return false;
    esp_err_t err = esp_event_loop_create_default();
    if (err != ESP_OK) return false;
    s_netif = esp_netif_create_default_wifi_sta();
    if (!s_netif) { esp_event_loop_delete_default(); return false; }
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    bool background = atomic_load(&s_weather) && !atomic_load(&s_foreground);
    if (background) {
        /* One tiny HTTPS request at a time; aggregation throughput is wasted
         * on this no-PSRAM device and retains scarce packet/reorder buffers. */
        cfg.static_rx_buf_num = 3;
        cfg.dynamic_rx_buf_num = 4;
        cfg.dynamic_tx_buf_num = 4;
        cfg.cache_tx_buf_num = 0;
        cfg.ampdu_rx_enable = 0;
        cfg.ampdu_tx_enable = 0;
        cfg.rx_ba_win = 0;
    }
    err = esp_wifi_init(&cfg);
    if (err != ESP_OK) goto failed_netif;
    bool wifi_handler = false, ip_handler = false;
    if ((err = esp_wifi_set_storage(WIFI_STORAGE_RAM)) != ESP_OK) goto failed_wifi;
    if ((err = esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_wifi, NULL)) != ESP_OK) goto failed_wifi;
    wifi_handler = true;
    if ((err = esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_wifi, NULL)) != ESP_OK) goto failed_wifi;
    ip_handler = true;
    if ((err = esp_wifi_set_mode(WIFI_MODE_STA)) != ESP_OK) goto failed_wifi;
    if ((err = esp_wifi_set_ps(WIFI_PS_NONE)) != ESP_OK) goto failed_wifi;
    if ((err = esp_wifi_start()) != ESP_OK) goto failed_wifi;
    s_up = true;
    log_dram("radio up (after)");
    ESP_LOGI(TAG, "radio up: internal heap %u KB free after", (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024);
    return true;
failed_wifi:
    if (ip_handler) esp_event_handler_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, on_wifi);
    if (wifi_handler) esp_event_handler_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, on_wifi);
    esp_wifi_deinit();
failed_netif:
    log_dram("radio init failed");
    ESP_LOGE(TAG, "radio init failed: %s (free %u KB, largest %u KB)", esp_err_to_name(err),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024,
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) / 1024);
    esp_netif_destroy_default_wifi(s_netif); s_netif = NULL;
    esp_event_loop_delete_default();
    return false;
}

static void do_scan(void) {
    wifi_scan_config_t sc = { .show_hidden = false, .scan_type = WIFI_SCAN_TYPE_ACTIVE };
    esp_err_t err = esp_wifi_scan_start(&sc, true);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "scan start: %s", esp_err_to_name(err));
        s_err = NET_ERR_RADIO; s_state = NET_FAILED; return;
    }
    uint16_t n = 0;
    err = esp_wifi_scan_get_ap_num(&n);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "scan count: %s", esp_err_to_name(err));
        esp_wifi_clear_ap_list(); s_err = NET_ERR_RADIO; s_state = NET_FAILED; return;
    }
    if (n > 40) n = 40;
    wifi_ap_record_t *recs = heap_caps_calloc(n ? n : 1, sizeof *recs,
                                            MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!recs) {
        ESP_LOGE(TAG, "no memory for %u scan records", (unsigned)n);
        esp_wifi_clear_ap_list(); s_err = NET_ERR_RADIO; s_state = NET_FAILED; return;
    }
    err = esp_wifi_scan_get_ap_records(&n, recs);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "scan records: %s", esp_err_to_name(err));
        free(recs); esp_wifi_clear_ap_list(); s_err = NET_ERR_RADIO; s_state = NET_FAILED; return;
    }
    s_n_aps = 0;
    for (int i = 0; i < n; i++) {                            /* strongest first (the driver sorts by rssi), names unique, no hidden */
        const char *name = (const char *)recs[i].ssid;
        if (!name[0]) continue;
        bool dup = false;
        for (int k = 0; k < s_n_aps; k++) if (!strcmp(s_aps[k].ssid, name)) { dup = true; break; }
        if (dup || s_n_aps >= NET_SCAN_MAX) continue;
        snprintf(s_aps[s_n_aps].ssid, sizeof s_aps[s_n_aps].ssid, "%s", name);
        s_aps[s_n_aps].rssi = recs[i].rssi;
        s_aps[s_n_aps].secured = recs[i].authmode != WIFI_AUTH_OPEN;
        s_n_aps++;
    }
    free(recs);
    ESP_LOGI(TAG, "scan: %d networks (%d shown)", n, s_n_aps);
    s_state = NET_DONE;
}
static void log_link(const char *ssid) {
    esp_netif_ip_info_t ip = {0};
    esp_netif_dns_info_t dns = {0};
    if (s_netif) { esp_netif_get_ip_info(s_netif, &ip); esp_netif_get_dns_info(s_netif, ESP_NETIF_DNS_MAIN, &dns); }
    ESP_LOGI(TAG, "connected to %s: ip " IPSTR ", gw " IPSTR ", dns " IPSTR, ssid,
             IP2STR(&ip.ip), IP2STR(&ip.gw), IP2STR(&dns.ip.u_addr.ip4));
}
static int connect_with(const char *ssid, const char *pass, int attempts, int wait_ms) {
    wifi_config_t wc = { 0 };
    memcpy(wc.sta.ssid, ssid, sizeof wc.sta.ssid);
    memcpy(wc.sta.password, pass, sizeof wc.sta.password);
    wc.sta.threshold.authmode = pass[0] ? WIFI_AUTH_WPA_PSK : WIFI_AUTH_OPEN;
    wc.sta.pmf_cfg.capable = true; wc.sta.pmf_cfg.required = false;
    wc.sta.scan_method = WIFI_ALL_CHANNEL_SCAN; wc.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
    esp_wifi_set_config(WIFI_IF_STA, &wc);
    int err = NET_ERR_NO_SIGNAL;
    for (int attempt = 0; attempt < attempts && !s_abort; attempt++) {    /* routers refuse first tries; the page's TRY AGAIN does five more */
        xEventGroupClearBits(s_ev, EV_GOT_IP | EV_DISCONNECTED);
        s_disc_reason = 0;
        if (esp_wifi_connect() != ESP_OK) { err = NET_ERR_RADIO; break; }
        EventBits_t b = xEventGroupWaitBits(s_ev, EV_GOT_IP | EV_DISCONNECTED, pdTRUE, pdFALSE, pdMS_TO_TICKS(wait_ms));
        if (b & EV_GOT_IP) { log_link(ssid); return NET_ERR_NONE; }
        int r = s_disc_reason;
        ESP_LOGW(TAG, "connect attempt %d: %s (reason %d)", attempt + 1, b & EV_DISCONNECTED ? "disconnected" : "timed out", r);
        if (r == WIFI_REASON_NO_AP_FOUND) { err = NET_ERR_NO_SIGNAL; break; }
        if (r == WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT || r == WIFI_REASON_AUTH_FAIL || r == WIFI_REASON_HANDSHAKE_TIMEOUT
            || r == WIFI_REASON_AUTH_EXPIRE || r == WIFI_REASON_NO_AP_FOUND_W_COMPATIBLE_SECURITY
            || r == WIFI_REASON_CONNECTION_FAIL) err = NET_ERR_PASSWORD;
        else err = NET_ERR_NO_SIGNAL;
        /* DHCP can finish just after the timeout: accept that link instead of
         * reconnecting over it (or dropping it below). */
        b = xEventGroupWaitBits(s_ev, EV_GOT_IP, pdTRUE, pdFALSE, pdMS_TO_TICKS(1000 + attempt * 500));
        if ((b & EV_GOT_IP) && s_connected && !s_abort) { log_link(ssid); return NET_ERR_NONE; }
    }
    /* Clear the flag before disconnecting: the DISCONNECTED event arrives
     * later, and nothing may treat this dropped link as usable meanwhile. */
    s_connected = false;
    esp_wifi_disconnect();
    return s_abort ? NET_ERR_ABORTED : err;
}
static int s_attempts = 5, s_wait_ms = 15000;
static void do_connect(void) {
    s_err = connect_with(s_ssid, s_pass, s_attempts, s_wait_ms);
    s_state = s_err == NET_ERR_NONE ? NET_DONE : NET_FAILED;
}
/* provisioning at install (update_mode.c): the page waits 45 s for an answer */
void net_port_esp_connect_budget(int attempts, int wait_ms) { s_attempts = attempts; s_wait_ms = wait_ms; }

/* ---- the time (net_time.h, 2026-10-03) ----
 * Plain SNTP by hand: ESP-IDF's client waits a random 0-5 s before its first
 * question (LWIP_SNTP_STARTUP_DELAY) and sets the system clock itself - the
 * boot wants neither (the keeper is looking at a dark glass, and the first
 * sync must not move the clock before the save is loaded). One 48-byte
 * question, the server's transmit time back; three servers, 2 s each. */
static bool ntp_ask(const char *host, int64_t *unix_out) {
    struct addrinfo hints = { .ai_family = AF_INET, .ai_socktype = SOCK_DGRAM }, *res = NULL;
    if (getaddrinfo(host, "123", &hints, &res) != 0 || !res) { ESP_LOGW(TAG, "time: %s did not resolve", host); return false; }
    int s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    bool ok = false;
    if (s >= 0) {
        struct timeval tv = { .tv_sec = 2 };
        setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
        uint8_t q[48] = { 0x23 }, r[48];                         /* LI 0, version 4, mode 3 = a client */
        if (sendto(s, q, sizeof q, 0, res->ai_addr, res->ai_addrlen) == (int)sizeof q
            && recv(s, r, sizeof r, 0) == (int)sizeof r && (r[0] & 7) == 4 && r[1] != 0) {   /* mode 4 = a server's answer, stratum 0 = a refusal */
            uint32_t secs = (uint32_t)r[40] << 24 | (uint32_t)r[41] << 16 | (uint32_t)r[42] << 8 | r[43];   /* the transmit stamp, seconds since 1900 */
            int64_t u = (int64_t)secs - 2208988800LL;
            if (u > 1700000000) { *unix_out = u; ok = true; }
        }
        close(s);
    }
    freeaddrinfo(res);
    if (!ok) ESP_LOGW(TAG, "time: no answer from %s", host);
    return ok;
}
bool net_time_sync(int64_t *unix_out, int64_t *at_us) {
    char ssid[NET_SSID_MAX + 1], pass[NET_PASS_MAX + 1];
    if (!net_port_creds_get(ssid, pass)) return false;
    int64_t t0 = esp_timer_get_time();
    if (!s_ev) s_ev = xEventGroupCreate();
    snprintf(s_ssid, sizeof s_ssid, "%s", ssid); snprintf(s_pass, sizeof s_pass, "%s", pass);
    s_abort = false; s_state = NET_BUSY;
    bool ok = false;
    if (radio_up()) {
        int err = connect_with(s_ssid, s_pass, 2, 8000);          /* a dark glass is waiting: two tries, not the page's five */
        s_state = err == NET_ERR_NONE ? NET_DONE : NET_FAILED;
        if (err == NET_ERR_NONE) {
            static const char *HOSTS[] = { "pool.ntp.org", "time.google.com", "time.cloudflare.com" };
            for (size_t i = 0; i < sizeof HOSTS / sizeof *HOSTS && !ok; i++)
                if (ntp_ask(HOSTS[i], unix_out)) { *at_us = esp_timer_get_time(); ok = true; }
        }
    }
    net_port_off();
    ESP_LOGI(TAG, "time from the internet: %s in %d ms", ok ? "got it" : "FAILED", (int)((esp_timer_get_time() - t0) / 1000));
    return ok;
}
static void do_check(void) {
    const char *url = net_port_manifest_url();
    if (!url[0]) {
        ESP_LOGW(TAG, "no CYD fork OTA channel configured; Wi-Fi remains available");
        s_err = NET_ERR_CHANNEL; s_state = NET_FAILED; return;
    }
    if (strncmp(url, "https://", 8)) {
        ESP_LOGE(TAG, "update manifest requires HTTPS");
        s_err = NET_ERR_BAD_MANIFEST; s_state = NET_FAILED; return;
    }
    ESP_LOGI(TAG, "manifest: %s", url);
    esp_http_client_config_t hc = { .url = url, .crt_bundle_attach = esp_crt_bundle_attach, .timeout_ms = 12000,
                                    .buffer_size = 2048, .buffer_size_tx = 2048, .max_redirection_count = 8 };
    esp_http_client_handle_t c = esp_http_client_init(&hc);
    if (!c) { s_err = NET_ERR_NO_NET; s_state = NET_FAILED; return; }
    char *buf = NULL;
    int err = NET_ERR_NO_NET, n = 0;
    /* GitHub answers a release asset's address with a redirect, twice (releases/latest/download ->
     * releases/download/<tag> -> the asset host, a 900-byte signed address). The open / fetch_headers
     * pair used here does NOT follow redirects by itself (only esp_http_client_perform does): 0.3.0's
     * first build read the 302 as the answer and said NO ANSWER to every check against the real
     * release - a tank that could never see an update (2026-10-04; the rehearsal's files sat on a
     * host that answers directly). Follow them by hand, a few hops at most. */
    int status = 0;
    for (int hop = 0; hop < 6; hop++) {
        if (esp_http_client_open(c, 0) != ESP_OK) { status = 0; break; }
        (void)esp_http_client_fetch_headers(c);
        status = esp_http_client_get_status_code(c);
        if (status != 301 && status != 302 && status != 303 && status != 307 && status != 308) break;
        ESP_LOGI(TAG, "manifest: HTTP %d, following the redirect (hop %d)", status, hop + 1);
        if (esp_http_client_set_redirection(c) != ESP_OK) break;
        char redirect[1024];
        if (esp_http_client_get_url(c, redirect, sizeof redirect) != ESP_OK ||
            strncmp(redirect, "https://", 8)) {
            ESP_LOGE(TAG, "refusing update manifest redirect outside HTTPS");
            status = 0; break;
        }
        int drained = 0; esp_http_client_flush_response(c, &drained);
        esp_http_client_close(c);
    }
    if (status == 200) {
        buf = heap_caps_malloc(4096, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        if (!buf) ESP_LOGE(TAG, "no memory for update manifest");
    }
    if (buf) {
        while (n < 4095) { int r = esp_http_client_read(c, buf + n, 4095 - n); if (r <= 0) break; n += r; }
        buf[n] = 0;
        if (status == 200 && n > 0 && n < 4095 && esp_http_client_is_complete_data_received(c))
            err = update_parse_manifest(buf, &s_m, s_app_url, sizeof s_app_url) ? NET_ERR_NONE : NET_ERR_BAD_MANIFEST;
        else { ESP_LOGW(TAG, "manifest: HTTP %d, %d bytes", status, n); err = status == 404 ? NET_ERR_BAD_MANIFEST : NET_ERR_NO_NET; }
    }
    esp_http_client_close(c); esp_http_client_cleanup(c); free(buf);
    if (err == NET_ERR_NONE) {
        ESP_LOGI(TAG, "manifest: release %s for %s (build %s), app %lu B at %s%s", s_m.release, s_m.board[0] ? s_m.board : "NO BOARD", s_m.build,
                 (unsigned long)s_m.app_size, s_app_url, s_m.needs_cable ? " - NEEDS THE CABLE" : "");
        s_state = NET_DONE;
    } else { s_err = err; s_state = NET_FAILED; }
}
static bool verify_download(const esp_partition_t *slot, int received) {
    if (received <= 0 || (uint32_t)received != s_m.app_size) {
        ESP_LOGE(TAG, "download size differs from release manifest");
        return false;
    }
    pt_board_marker_t marker = {0};
    esp_app_desc_t descriptor = {0};
    if (esp_partition_read(slot, PT_BOARD_MARKER_OFFSET, &marker, sizeof marker) != ESP_OK ||
        memcmp(marker.magic, PT_BOARD_MAGIC, sizeof marker.magic) ||
        memcmp(marker.board, PT_BOARD, sizeof PT_BOARD) ||
        esp_partition_read(slot, 32, &descriptor, sizeof descriptor) != ESP_OK ||
        strncmp(descriptor.version, s_m.release, sizeof descriptor.version)) {
        ESP_LOGE(TAG, "download board/version differs from release manifest");
        return false;
    }
    psa_hash_operation_t hash = PSA_HASH_OPERATION_INIT;
    bool ok = psa_crypto_init() == PSA_SUCCESS && psa_hash_setup(&hash, PSA_ALG_SHA_256) == PSA_SUCCESS;
    uint8_t buffer[1024], digest[32];
    for (uint32_t offset = 0; ok && offset < s_m.app_size;) {
        if (s_abort) { ok = false; break; }
        size_t count = s_m.app_size - offset;
        if (count > sizeof buffer) count = sizeof buffer;
        ok = esp_partition_read(slot, offset, buffer, count) == ESP_OK &&
             psa_hash_update(&hash, buffer, count) == PSA_SUCCESS;
        offset += count;
    }
    size_t length = 0;
    ok = ok && psa_hash_finish(&hash, digest, sizeof digest, &length) == PSA_SUCCESS &&
         length == sizeof digest && !memcmp(digest, s_m.sha256, sizeof digest);
    psa_hash_abort(&hash);
    if (!ok) ESP_LOGE(TAG, "download SHA-256 verification failed");
    else ESP_LOGI(TAG, "download board, release, exact size and SHA-256 verified");
    return ok;
}
static void do_install(void) {
    esp_http_client_config_t hc = { .url = s_app_url, .crt_bundle_attach = esp_crt_bundle_attach, .timeout_ms = 20000,
                                    .buffer_size = 4096, .buffer_size_tx = 2048, .max_redirection_count = 8, .keep_alive_enable = true };
    esp_https_ota_config_t oc = { .http_config = &hc, .bulk_flash_erase = false };
    const esp_partition_t *slot = esp_ota_get_next_update_partition(NULL);
    if (!slot) { s_err = NET_ERR_SPACE; s_state = NET_FAILED; return; }
    if (s_m.app_size && s_m.app_size > slot->size) { ESP_LOGE(TAG, "image %lu B > slot %lu B", (unsigned long)s_m.app_size, (unsigned long)slot->size); s_err = NET_ERR_SPACE; s_state = NET_FAILED; return; }
    ESP_LOGI(TAG, "download %s -> %s (0x%lx)", s_app_url, slot->label, (unsigned long)slot->address);
    esp_err_t e = esp_https_ota_begin(&oc, &s_ota);
    if (e != ESP_OK) { ESP_LOGE(TAG, "ota begin: %s", esp_err_to_name(e)); s_err = NET_ERR_DOWNLOAD; s_state = NET_FAILED; return; }
    int total = esp_https_ota_get_image_size(s_ota);
    if (total <= 0) total = (int)s_m.app_size;
    int64_t last_log = 0;
    bool board_checked = false;
    while (!s_abort) {
        e = esp_https_ota_perform(s_ota);
        if (e != ESP_ERR_HTTPS_OTA_IN_PROGRESS) break;
        int got = esp_https_ota_get_image_len_read(s_ota);
        if (!board_checked && got >= 4096) {                 /* the marker is in the first sector, written by now */
            pt_board_marker_t mk = {0}; board_checked = true;
            bool ok = esp_partition_read(slot, PT_BOARD_MARKER_OFFSET, &mk, sizeof mk) == ESP_OK
                   && !memcmp(mk.magic, PT_BOARD_MAGIC, sizeof mk.magic) && !strncmp(mk.board, PT_BOARD, sizeof mk.board);
            if (!ok) {
                mk.board[sizeof mk.board - 1] = 0;
                ESP_LOGE(TAG, "download: the image is for board \"%s\", this is %s - refused", memcmp(mk.magic, PT_BOARD_MAGIC, 8) ? "(no marker)" : mk.board, PT_BOARD);
                esp_https_ota_abort(s_ota); s_ota = NULL; s_err = NET_ERR_BOARD; s_state = NET_FAILED; return;
            }
            ESP_LOGI(TAG, "download: the image is for %s - this board", PT_BOARD);
        }
        s_percent = total > 0 ? (int)((int64_t)got * 99 / total) : 0;
        int64_t now = esp_timer_get_time();
        if (now - last_log > 5000000) { ESP_LOGI(TAG, "download: %d / %d bytes (%d%%)", got, total, s_percent); last_log = now; }
    }
    if (s_abort) { esp_https_ota_abort(s_ota); s_ota = NULL; s_err = NET_ERR_ABORTED; s_state = NET_FAILED; ESP_LOGW(TAG, "download aborted"); return; }
    if (e != ESP_OK || !esp_https_ota_is_complete_data_received(s_ota)) {
        ESP_LOGE(TAG, "download: %s%s", esp_err_to_name(e), e == ESP_OK ? " (incomplete)" : "");
        esp_https_ota_abort(s_ota); s_ota = NULL;
        s_err = e == ESP_ERR_OTA_VALIDATE_FAILED || e == ESP_ERR_IMAGE_INVALID ? NET_ERR_VERIFY : NET_ERR_DOWNLOAD; s_state = NET_FAILED; return;
    }
    if (!verify_download(slot, esp_https_ota_get_image_len_read(s_ota)) || s_abort) {
        esp_https_ota_abort(s_ota); s_ota = NULL;
        s_err = s_abort ? NET_ERR_ABORTED : NET_ERR_VERIFY; s_state = NET_FAILED; return;
    }
    e = esp_https_ota_finish(s_ota); s_ota = NULL;          /* validates the ESP image, then selects the boot slot */
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "ota finish: %s", esp_err_to_name(e));
        s_err = e == ESP_ERR_OTA_VALIDATE_FAILED || e == ESP_ERR_IMAGE_INVALID ? NET_ERR_VERIFY : NET_ERR_DOWNLOAD; s_state = NET_FAILED; return;
    }
    s_percent = 100;
    ESP_LOGI(TAG, "installed: %s boots next", slot->label);
    s_state = NET_DONE;
}
static void radio_down(void) {
    if (!s_up) return;
    s_connected = false;
    esp_wifi_disconnect(); esp_wifi_stop(); esp_wifi_deinit();
    esp_event_handler_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, on_wifi);
    esp_event_handler_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, on_wifi);
    if (s_netif) { esp_netif_destroy_default_wifi(s_netif); s_netif = NULL; }
    esp_event_loop_delete_default();
    s_up = false;
    ESP_LOGI(TAG, "radio off: internal heap %u KB free", (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024);
}
void weather_port_esp_service(void);
static void background_weather_work(void) {
    if (atomic_load(&s_foreground)) return;
    if (atomic_exchange(&s_weather_reconnect, false)) s_weather_retry = 0;
    if (!atomic_load(&s_weather)) {
        if (s_weather_radio) { radio_down(); s_weather_radio = false; }
        return;
    }
    if (net_port_connected()) { s_weather_failures = 0; return; }
    int64_t now = esp_timer_get_time();
    if (now < s_weather_retry) return;
    char ssid[NET_SSID_MAX + 1] = {0}, pass[NET_PASS_MAX + 1] = {0};
    s_weather_retry = now + 60000000;
    if (!net_port_creds_get(ssid, pass)) return;
    s_weather_radio = true;
    if (radio_up() && !atomic_load(&s_foreground) && atomic_load(&s_weather)) {
        s_abort = false;
        (void)connect_with(ssid, pass, 2, 8000);   /* all-channel scan + DHCP needs more than 3.5 s */
    }
    memset(pass, 0, sizeof pass);
    if (net_port_connected()) s_weather_failures = 0;
    else {
        if (s_weather_failures < 4) ++s_weather_failures;
        unsigned seconds = 30u << s_weather_failures;
        if (seconds > 900) seconds = 900;
        s_weather_retry = now + (int64_t)seconds * 1000000;
    }
}
static void background_weather(void) {
    atomic_store(&s_background_busy, true);
    background_weather_work();
    /* After the join attempt, so a queued request sees the fresh link. */
    if (atomic_load(&s_weather) && !atomic_load(&s_foreground)) weather_port_esp_service();
    atomic_store(&s_background_busy, false);
}
static void worker(void *arg) {
    (void)arg;
    for (;;) {
        uint32_t op = 0;
        xTaskNotifyWait(0, 0xffffffff, &op, pdMS_TO_TICKS(1000));
        if (op == OP_NONE) { background_weather(); continue; }
        if (!radio_up()) { s_err = NET_ERR_RADIO; s_state = NET_FAILED; continue; }
        switch (op) {
        case OP_SCAN: do_scan(); break;
        case OP_CONNECT: do_connect(); break;
        case OP_CHECK: do_check(); break;
        case OP_INSTALL: do_install(); break;
        default: break;
        }
    }
}
static void begin(int op) {
    atomic_store(&s_foreground, true);
    if (!s_ev) s_ev = xEventGroupCreate();
    if (!s_task) xTaskCreatePinnedToCore(worker, "net", 12288, NULL, 5, &s_task, 1);
    s_op = op; s_state = NET_BUSY; s_err = NET_ERR_NONE; s_abort = false;
    xTaskNotify(s_task, (uint32_t)op, eSetValueWithOverwrite);
}
void net_port_weather(bool wanted) {
    bool before = atomic_exchange(&s_weather, wanted);
    if (wanted && !before) atomic_store(&s_weather_reconnect, true);
    if (s_task) return;
    if (!wanted && !s_task) return;
    int64_t now = esp_timer_get_time();
    if (now < s_weather_init_retry) return;
    s_weather_init_retry = now + 60000000;
    if (!s_ev) s_ev = xEventGroupCreate();
    if (!s_ev) return;
    if (!s_task && xTaskCreatePinnedToCore(worker, "net", 12288, NULL, 5, &s_task, 1) != pdPASS) return;
    s_weather_init_retry = 0;
}
void net_port_scan_start(void) { begin(OP_SCAN); }
int  net_port_scan_state(void) { return s_op == OP_SCAN ? s_state : NET_IDLE; }
int  net_port_scan_results(net_ap_t *out, int max) { int n = s_n_aps < max ? s_n_aps : max; memcpy(out, s_aps, n * sizeof *out); return n; }
void net_port_connect_start(const char *ssid, const char *pass) {
    atomic_store(&s_foreground, true);
    snprintf(s_ssid, sizeof s_ssid, "%s", ssid); snprintf(s_pass, sizeof s_pass, "%s", pass ? pass : "");
    if (s_connected) { s_state = NET_DONE; s_op = OP_CONNECT; return; }   /* the retry after a failed check: still on */
    begin(OP_CONNECT);
}
int  net_port_connect_state(void) { return s_op == OP_CONNECT ? s_state : NET_IDLE; }
void net_port_check_start(void) { begin(OP_CHECK); }
int  net_port_check_state(const update_manifest_t **m) { if (s_op != OP_CHECK) return NET_IDLE; if (s_state == NET_DONE) *m = &s_m; return s_state; }
void net_port_install_start(void) { s_percent = 0; begin(OP_INSTALL); }
int  net_port_install_state(int *percent) { if (s_op != OP_INSTALL) return NET_IDLE; *percent = s_percent; return s_state; }
void net_port_abort(void) { s_abort = true; if (s_op == OP_CONNECT) esp_wifi_disconnect(); }
int  net_port_fail_reason(void) { return s_err; }
void net_port_off(void) {
    if (atomic_load(&s_weather)) {
        /* Foreground releases its lease; weather may reuse this association. */
        s_weather_radio = true;
        atomic_store(&s_foreground, false);
        return;
    }
    atomic_store(&s_foreground, true); /* no new background work during teardown */
    s_abort = true;
    if (s_task) {                                            /* let a download notice the abort before the driver goes */
        for (int i = 0; i < 100 && (s_state == NET_BUSY || atomic_load(&s_background_busy)); i++) vTaskDelay(pdMS_TO_TICKS(50));
        vTaskDelete(s_task); s_task = NULL;
    }
    radio_down(); s_weather_radio = false;
    s_op = OP_NONE; s_state = NET_IDLE;
    atomic_store(&s_foreground, false);
}
