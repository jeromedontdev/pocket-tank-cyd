/* director.c — serial console for staging scenarios (see director.h).
 * RX only through the USB-Serial-JTAG driver; the log keeps its polled
 * (no-driver) write path so an unattended tank never blocks on a host that
 * isn't reading. Called from the tank task, so no locking. */
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <ctype.h>
#include <math.h>
#include "sdkconfig.h"
#include "esp_log.h"
#include "progression.h"
#include "companion.h"
#include "setup.h"
#include "director.h"
#include "update.h"
#include "version.h"
#include "board_hardware.h"
#include "esp_ota_ops.h"
const char *net_port_manifest_url(void);
bool net_port_set_manifest_url(const char *url);
#include "touch_port.h"
#include "sd_port_cyd.h"
#include "display_port.h"
#include "driver/gpio.h"
#include "brightness.h"
#include "ambient.h"
#include "notice.h"
#include "esp_timer.h"
#include "esp_attr.h"
#include "esp_system.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#if CONFIG_SOC_USB_SERIAL_JTAG_SUPPORTED
#include "driver/usb_serial_jtag.h"
#else
#include "driver/uart.h"
#endif

static const char *TAG = "director";
static int shop_item(const char *name) {
    static const char *const keys[SD_ITEM_COUNT] = {
        "plant", "snail", "castle", "coral", "cluster", "shrimp", "urchin", "seahorse",
        "pumpkin", "chest", "shell", "starfish", "anchor", "buoy", "sand", "lights",
        "wreck", "urn", "crystal", "fan", "ruins", "jelly",
        "pebbles", "pearl", "log", "sponges", "barrel", "helmet", "lilies", "duck", "filter", "garden",
        "current", "shelter", "play", "training",
        "seaglass", "braincoral", "tiki", "bottle", "anemone", "teapot"
    };
    for (int i = 0; i < SD_ITEM_COUNT; i++) if (!strcmp(name, keys[i])) return i;
    return -1;
}

static bool nvs_has(const char *key) {
    nvs_handle_t h; if (nvs_open("tank", NVS_READONLY, &h) != ESP_OK) return false;
    size_t len = 0; bool ok = nvs_get_blob(h, key, NULL, &len) == ESP_OK && len > 0;
    nvs_close(h); return ok;
}
static void staging_disabled(void) {
    ESP_LOGE(TAG, "Hardware staging disabled: NVS stash cannot protect expanded saves. Use the simulator for demos and 'sd backup' for real backups.");
}
static bool s_ok;
static char s_line[192];
static int  s_len;

static int find_fish(const tank_t *t, const char *s) {
    if (!s) return -1;
    if (isdigit((unsigned char)s[0])) { int i = atoi(s); return i >= 0 && i < t->n_fish ? i : -1; }
    for (int i = 0; i < t->n_fish; i++)
        if (!strcasecmp(t->fish[i].name, s)) return i;
    return -1;
}

static float *drive_of(fish_t *f, const char *s) {
    if (!strcmp(s, "hunger"))    return &f->hunger;
    if (!strcmp(s, "energy"))    return &f->energy;
    if (!strcmp(s, "stress"))    return &f->stress;
    if (!strcmp(s, "curiosity")) return &f->curiosity;
    if (!strcmp(s, "trust"))     return &f->trust;
    return NULL;
}

static void clear_pellets(tank_t *t) {
    for (int i = 0; i < MAX_FOOD; i++) t->food[i].alive = false;
}

static void show_state(const tank_t *t) {
    for (int i = 0; i < t->n_fish; i++) {
        const fish_t *f = &t->fish[i];
        ESP_LOGI(TAG, "%d %-6s %-5s age %.1fh size %.2f hunger %.1f energy %.1f stress %.1f curiosity %.1f trust %.1f ms %03x  %s at %.0f,%.0f",
                 i, f->name, STAGE_NAMES[f->stage], progression_age_s(t, i) / 3600.0f, f->size,
                 f->hunger, f->energy, f->stress, f->curiosity, f->trust, (unsigned)f->ms_bits,
                 GOAL_NAMES[f->goal.id], f->x, f->y);
    }
    int pellets = 0, cells = 0;
    for (int i = 0; i < MAX_FOOD; i++) pellets += t->food[i].alive;
    for (int i = 0; i < ALGAE_CELLS; i++) cells += t->algae[i] > 0;
    ESP_LOGI(TAG, "pellets %d | trickle %s | ravenous %d | %s%s idle %.0fs | veg %.2f %.2f %.2f | algae cells %d/%d | courting %s%s%s%s | arrival %s",
             pellets, t->trickle_off ? "OFF" : t->autofeed_off ? "off (AUTO FEED off)" : "on", (int)t->ravenous,
             t->night ? "night" : "day", t->light_override ? " (manual)" : "",
             t->idle_s,
             t->veg_growth[0], t->veg_growth[1], t->veg_growth[2], cells, ALGAE_CELLS,
             t->courting ? t->fish[t->court_a].name : "no", t->courting ? "+" : "",
             t->courting ? t->fish[t->court_b].name : "", t->spawning ? " (SPAWNING)" : t->court_active > 0 ? " (circling)" : "",
             progression_arrival_pending() ? "staged" : "-");
    ESP_LOGI(TAG, "sand dollars %d (earned %d) | shop:%s%s%s%s%s%s%s%s%s | colonies %d | %.0f cm trimmed",
             (int)t->sd_balance, (int)t->sd_earned, t->sd_unlocks & SD_ITEM_PLANT ? " plant" : "", t->sd_unlocks & SD_ITEM_SNAIL ? " snail" : "",
             t->sd_unlocks & SD_ITEM_CASTLE ? " castle" : "", t->sd_unlocks & SD_ITEM_CORAL ? " coral" : "", t->sd_unlocks & SD_ITEM_CLUSTER ? " cluster" : "",
             t->sd_unlocks & SD_ITEM_SHRIMP ? " shrimp" : "", t->sd_unlocks & SD_ITEM_URCHIN ? " urchin" : "", t->sd_unlocks & SD_ITEM_SEAHORSE ? " seahorse" : "",
             t->sd_unlocks ? "" : " -", (int)t->algae_colonies, t->trim_px / PX_PER_CM);
    ESP_LOGI(TAG, "shop ownership %08lx | bio filter %s | slow garden %s",
             (unsigned long)t->sd_unlocks, t->sd_unlocks & SD_ITEM_FILTER ? "ON" : "off",
             t->sd_unlocks & SD_ITEM_GARDEN ? "ON" : "off");
    ESP_LOGI(TAG, "fish stat upgrades %02x | current %s | shelter %s | play %s | training %s",
             t->aquariums.stat_upgrades, progression_item_owned(t, 32) ? "ON" : "off",
             progression_item_owned(t, 33) ? "ON" : "off", progression_item_owned(t, 34) ? "ON" : "off",
             progression_item_owned(t, 35) ? "ON" : "off");
    ESP_LOGI(TAG, "clock theme %u | layout %u | delay %u | flags %u | UTC quarters %d | secondary %u | text %u captions %u",
             t->aquariums.clock_theme, t->aquariums.clock_layout, t->aquariums.clock_delay & CLOCK_DELAY_MASK,
             t->aquariums.clock_flags, t->aquariums.clock_utc_quarters,
             t->aquariums.clock_delay >> CLOCK_SECONDARY_SHIFT,
             t->aquariums.clock_layout >> CLOCK_DETAIL_SHIFT,
             (t->aquariums.clock_delay & CLOCK_CAPTION_MASK) >> CLOCK_CAPTION_SHIFT);
    if (t->sd_unlocks & SD_ITEM_URCHIN)                 /* where it is, what it is after */
        ESP_LOGI(TAG, "urchin at x %.0f | %s | appetite %.3f | %.0f cm grazed so far (keeps the grass over %.2f)", t->urchin_x,
                 tank_urchin_chewing(t) ? "chewing" : t->urchin_frond >= 0 ? "off to the tall grass" : t->urchin_rest > 0 ? "resting" : "ambling",
                 t->urchin_appetite, t->urchin_grazed_px / PX_PER_CM, (double)URCHIN_KEEP);
    if (t->sd_unlocks & SD_ITEM_SEAHORSE)               /* where it is, what it is doing, its tally */
        ESP_LOGI(TAG, "seahorse at %.0f,%.0f | %s | %.2f days with you, %d hitches, +%.2f trust passed on | %d pellets snapped (%d paid; next pays in %.0f s)", t->sh_x, t->sh_y,
                 t->sh_hitched ? "on the finger" : t->sh_mode == SH_HITCH ? "swimming to the finger" : t->sh_mode == SH_PERCH ? tank_seahorse_holding(t) : "adrift",
                 tank_seahorse_days(t), (int)t->sh_hitches, t->sh_trust_passed, (int)t->sh_eaten, (int)t->sd_snacks_paid, t->sh_snack_cool > 0 ? t->sh_snack_cool : 0);
    if (t->sd_unlocks & SD_ITEM_SNAIL) {                /* where it is, what it is after */
        int c = t->snail_cell, cells = 0; for (int i = 0; i < ALGAE_CELLS; i++) cells += t->algae[i] > 0;
        ESP_LOGI(TAG, "snail at %.0f,%.0f heading %.0f deg | %s cell %d at %d,%d (film %d) | %d cells on the glass | %d grazed so far", t->snail_x, t->snail_y,
                 t->snail_heading * 57.3f, c >= 0 ? "after" : "no target,", c, c >= 0 ? (c % ALGAE_COLS) * ALGAE_CELL + 8 : -1,
                 c >= 0 ? (c / ALGAE_COLS) * ALGAE_CELL + 8 : -1, c >= 0 ? t->algae[c] : 0, cells, (int)t->snail_grazed);
    }
    for (int b = 0; b < tank_veg_beds(t); b++) {   /* per-frond heights: which blades a sweep left standing */
        char row[VEG_FRONDS_MAX * 5 + 1]; int len = 0, n; float x0, f0, f1;
        tank_veg_bed(t, b, &x0, NULL, NULL, &n);
        tank_veg_frond(t, b, 0, &f0); tank_veg_frond(t, b, 1, &f1);     /* the pitch: 12, the sword plant's 14 */
        for (int i = 0; i < n && len < (int)sizeof row - 5; i++) len += snprintf(row + len, sizeof row - len, " %.2f", t->veg_h[b][i]);
        ESP_LOGI(TAG, "bed %d %s (x from %.0f, pitch %.0f):%s", b, b == 3 ? "leaves" : "fronds", x0 + 6, f1 - f0, row);
    }
    if (t->sd_unlocks & SD_ITEM_PLANT)
        ESP_LOGI(TAG, "plant placed: centre x %.0f (%s), depth %s", tank_decor_x(t, 0), t->plant_x > 0 ? "the keeper's" : "the default",
                 t->plant_z == DECOR_Z_BACK ? "BEHIND the fish" : t->plant_z == DECOR_Z_FRONT ? "IN FRONT of the fish" : "AMONG the fish");
    if (t->sd_unlocks & SD_ITEM_CASTLE)
        ESP_LOGI(TAG, "castle placed: centre x %.0f (%s), %s", tank_decor_x(t, 2), t->castle_x > 0 ? "the keeper's" : "the default",
                 t->castle_z == DECOR_Z_BACK ? "BEHIND the grass" : "IN FRONT of the grass (the fish swim through)");
    if (t->sd_unlocks & SD_ITEM_CORAL)
        ESP_LOGI(TAG, "coral placed: centre x %.0f (%s), %s, colour %06x, growth %.2f of %.2f (%s)", tank_decor_x(t, 3), t->coral_x > 0 ? "the keeper's" : "the default",
                 t->coral_z == DECOR_Z_BACK ? "BEHIND" : t->coral_z == DECOR_Z_FRONT ? "IN FRONT" : "AMONG the grass", (unsigned)tank_coral_rgb(t),
                 tank_coral_growth(t), CORAL_FULL, tank_coral_growth(t) >= CORAL_FULL ? "the crown is out" : tank_coral_growth(t) >= 1 ? "the fan is complete, the crown coming" : "growing");
    if (t->sd_unlocks & SD_ITEM_CLUSTER)
        ESP_LOGI(TAG, "reef cluster placed: centre x %.0f (%s), %s, look %s, growth %.2f of %.0f (%s)", tank_decor_x(t, 4), t->cluster_x > 0 ? "the keeper's" : "the default",
                 t->cluster_z == DECOR_Z_BACK ? "BEHIND" : t->cluster_z == DECOR_Z_FRONT ? "IN FRONT" : "AMONG the grass", CLUSTER_SCHEMES[tank_cluster_scheme(t)].name,
                 tank_cluster_growth(t), CLUSTER_FULL, tank_cluster_growth(t) >= CLUSTER_FULL ? "in full bloom" : tank_cluster_growth(t) >= 1 ? "full size, blooming" : "filling out");
    ESP_LOGI(TAG, "nursery bed %d (a bed >= %.2f) | legacy NVS stash: %s", tank_nursery_bed(t), (double)VEG_NURSERY,
             nvs_has("bk") ? "PRESENT (not the active save; do not erase)" : "none");

}

static void help(void) {
    ESP_LOGI(TAG, "hardware [cyd|elegoo] (query profile, or save aquarium/profile and restart; calibration preserved)");
    ESP_LOGI(TAG, "help | state | hungry [N] [level=9] (N fish starving, water cleared, trickle held)");
    ESP_LOGI(TAG, "fed [level=1] (everyone full, trickle back on) | set <hunger|energy|stress|curiosity|trust> <0-10> [fish name|idx]");
    ESP_LOGI(TAG, "feed [n=3] [x] (keeper drops pellets; trickle back on) | trickle on|off | pellets clear");
    ESP_LOGI(TAG, "algae <steps|clear> | veg <bed 0-2|all> <0.03-1> | light (toggle) | auto | sleep <hours> | save");
    ESP_LOGI(TAG, "STAGED TANKS: fresh / stages / stage are disabled on hardware; use simulator demos");
    ESP_LOGI(TAG, "benchmark (bounded RAM-only five-fish Normal/Balanced LCD test; pauses real care, never saves fixtures)");
    ESP_LOGI(TAG, "stash / restore / staging / age: disabled for expanded saves; use simulator demos");
    ESP_LOGI(TAG, "milestones [off] (the page, on cue; on the device: tap the open stats card)");
    ESP_LOGI(TAG, "journal | journal tap <x> <y> | journal demo (disabled on hardware; use simulator)");
    ESP_LOGI(TAG, "games | games tap <x> <y> (framebuffer coordinates; real aquarium care pauses)");
    ESP_LOGI(TAG, "home | home tap <x> <y> (main menu; life remains an alias; framebuffer coordinates)");
    ESP_LOGI(TAG, "rpg (read-only Lost Current progress and chapter contributors)");
    ESP_LOGI(TAG, "settings | settings tap <x> <y> | settings off (clock, weather and brightness; framebuffer coordinates)");
    ESP_LOGI(TAG, "bank | bank tap <x> <y> | bank verify (JSON validation) | bank demo (disabled on hardware; use simulator)");
    ESP_LOGI(TAG, "tanks | tanks tap <x> <y> | tanks demo (disabled on hardware; use simulator)");
    ESP_LOGI(TAG, "fry [open|roll|buy <0-2>] (the shop's fry: open = as if a fish was sold; roll = a fresh three now)");
    ESP_LOGI(TAG, "shop [off] (the sand dollar page) | dollars [n] (grant n; the balance and the chore counts) | buy plant|snail|castle|coral|cluster|shrimp|urchin|seahorse (at the price) | seahorse on|off (drawn or not: the fps A/B) | place [plant|castle|coral] [x [behind|among|front]] (the piece's spot; no x = the page; the castle has no among) | coral <0-7|rrggbb> (its colour) | coral grow <g> (its growth, 1 = the fan, 1.25 = the crown) | cluster look <0-2> | cluster grow <g> (1 = full size, 2 = every tentacle) | sell plant|castle|coral|cluster (20%% back)");
    ESP_LOGI(TAG, "reset (the keeper's confirm prompt, as BOOT + tap opens it) | reset yes|no - YES WIPES ALL PROGRESS AND AQUARIUMS; explicit SD backups are kept");
    ESP_LOGI(TAG, "setup [off] (first-run mode, welcome, names, colours; off drops the panel) | name <fish|idx> <newname> (up to %d letters, saved)", FISH_NAME_MAX);
    ESP_LOGI(TAG, "touch [bias <px>] [log on|off] [calibrate] (finger-landing correction: reported touches move up by px; not saved; also the raw span seen since boot)");
    ESP_LOGI(TAG, "bright <0-255> (panel now; not saved) | level <10-100> (the keeper's setting, saved)");
    ESP_LOGI(TAG, "shop: chest (%d), shell (%d), starfish (%d), anchor (%d), buoy (%d), sand (%d), lights (%d) | style sand <0..7> | style water <0..7>",
             SD_PRICE_CHEST, SD_PRICE_SHELL, SD_PRICE_STARFISH, SD_PRICE_ANCHOR, SD_PRICE_BUOY, SD_PRICE_SAND, SD_PRICE_LIGHTS);
    ESP_LOGI(TAG, "shop also: wreck, urn, crystal, fan, ruins, jelly | fish customize <0..%d> (free appearance editor)", N_FISH_MAX - 1);
    ESP_LOGI(TAG, "setup tap <x> <y> (activate the open setup page at framebuffer coordinates; same hit targets as touch)");
    ESP_LOGI(TAG, "clock | sd status | sd backup | sd backups | sd export | sd verify | sd restore|undo [number] [confirm] (JSON tank snapshots on SD)");
    ESP_LOGI(TAG, "overgrown (grass to the ceiling + fouled glass; fish stress climbs) | court (stage the fry: in ~10-20 s the pair courts in the grass, then it is born) | arrive (the fry, now)");
    ESP_LOGI(TAG, "sd update (check, then confirm within 60s) | sd update-upload <bytes> (USB package copy)");
    ESP_LOGI(TAG, "wifi | wifi setup (on-screen picker) | wifi set ssid=<name> pass=<secret> | wifi forget | ota | ota check (legacy Wi-Fi flow) | ota url <manifest url>|default");
}

/* `wifi set ssid=<name> pass=<secret>`: the raw line, since a name or a
 * passphrase may hold spaces (everything after "pass=" is the passphrase) */
static void wifi_set_raw(const char *args) {
    const char *sp = strstr(args, "ssid="), *pp = strstr(args, " pass=");
    if (!sp) { ESP_LOGW(TAG, "wifi set ssid=<name> pass=<secret> (pass= may be left out for an open network)"); return; }
    char ssid[NET_SSID_MAX + 1] = "", pass[NET_PASS_MAX + 1] = "";
    const char *s0 = sp + 5, *s1 = pp && pp > s0 ? pp : s0 + strlen(s0);
    snprintf(ssid, sizeof ssid, "%.*s", (int)(s1 - s0), s0);
    if (pp) snprintf(pass, sizeof pass, "%s", pp + 6);
    for (char *e = ssid + strlen(ssid); e > ssid && e[-1] == ' '; e--) e[-1] = 0;
    if (!ssid[0]) { ESP_LOGW(TAG, "wifi set: empty name"); return; }
    net_port_creds_set(ssid, pass);
}
static void imp_dump(void);
static void run(tank_t *t, char *line) {
    if (!strncmp(line, "wifi set ", 9)) { wifi_set_raw(line + 9); return; }
    if (!strcmp(line, "improv")) { imp_dump(); return; }     /* the handshake's flight recorder */
    if (!strncmp(line, "ota tap ", 8)) {                     /* update mode, hands-free: a tap at (x, y) in tank coordinates */
        float x = 0, y = 0;
        if (sscanf(line + 8, "%f %f", &x, &y) == 2 && update_active()) {
            int h = update_hit(x, y);
            update_touch(x, y, true); update_touch(x, y, false);
            ESP_LOGI(TAG, "ota tap %.0f,%.0f -> %s (page %d now)", x, y, update_hit_name(h), update_page());
        } else ESP_LOGW(TAG, "ota tap <x> <y> (update mode only)");
        return;
    }
    if (!strncmp(line, "ota url ", 8)) {                     /* the manifest to check (the test channel); "default" = the release channel */
        const char *u = line + 8; while (*u == ' ') u++;
        bool ok = net_port_set_manifest_url(strcmp(u, "default") ? u : NULL);
        ESP_LOGI(TAG, "ota url %s: %s", ok ? "set" : "NOT set", net_port_manifest_url());
        return;
    }
    char *argv[6]; int argc = 0;
    for (char *tok = strtok(line, " \t"); tok && argc < 6; tok = strtok(NULL, " \t")) argv[argc++] = tok;
    if (!argc) return;
    for (char *p = argv[0]; *p; p++) *p = (char)tolower((unsigned char)*p);
    const char *c = argv[0];
    if (!t && strcmp(c, "wifi") && strcmp(c, "ota") && strcmp(c, "help") &&
        strcmp(c, "sd") && strcmp(c, "hardware")) {   /* update mode: no tank to command */
        ESP_LOGW(TAG, "update mode: only wifi / ota / sd / hardware / help (hardware query only)"); return; }
    if (!strcmp(c, "hardware")) {
        if (argc == 1) {
            ESP_LOGI(TAG, "hardware profile %s | image %s", board_hardware_current()->name, PT_BOARD);
            return;
        }
        unsigned id = argc == 2 && !strcmp(argv[1], "cyd") ? BOARD_HARDWARE_CYD :
                      argc == 2 && !strcmp(argv[1], "elegoo") ? BOARD_HARDWARE_ELEGOO : 0;
        if (!id) { ESP_LOGE(TAG, "use hardware [cyd|elegoo]"); return; }
        if (!t) { ESP_LOGE(TAG, "profile changes unavailable during network update/setup"); return; }
        if (!progression_save(t)) { ESP_LOGE(TAG, "profile change canceled: aquarium save failed"); return; }
        if (!board_hardware_save(id)) return;
        ESP_LOGI(TAG, "hardware profile saved; restarting without changing calibration");
        esp_restart();
        return;
    }
    if (!strcmp(c, "sd")) { sd_port_command(t, argc, argv); return; }
    if (!strcmp(c, "help")) help();
    else if (!strcmp(c, "benchmark")) device_render_benchmark();
    else if (!strcmp(c, "state")) show_state(t);
    else if (!strcmp(c, "wifi")) {                            /* the saved network (docs/OTA.md) */
        char ssid[NET_SSID_MAX + 1], pass[NET_PASS_MAX + 1];
        if (argc > 1 && !strcmp(argv[1], "setup")) {
            if (t) device_wifi_setup(); else ESP_LOGW(TAG, "Wi-Fi setup is already open");
            return;
        }
        if (argc > 1 && !strcmp(argv[1], "forget")) { net_port_creds_forget(); return; }
        if (net_port_creds_get(ssid, pass)) ESP_LOGI(TAG, "network: %s (%s)", ssid, pass[0] ? "with a password" : "open");
        else ESP_LOGI(TAG, "no network saved (wifi set ssid=<name> pass=<secret>, or the wizard on the glass)");
    } else if (!strcmp(c, "ota")) {                           /* update mode and its channel */
        const esp_partition_t *run = esp_ota_get_running_partition(), *boot = esp_ota_get_boot_partition();
        if (argc > 1 && !strcmp(argv[1], "check")) { if (!t) { ESP_LOGW(TAG, "already in update mode"); return; }
                                                      ESP_LOGI(TAG, "ota check: saving, restarting into update mode"); device_update_check(); return; }
        if (argc > 1 && !strcmp(argv[1], "calib")) { update_debug_calib(); ESP_LOGI(TAG, "ota calib: nine crosshairs, a press each (the log fits the mapping)"); return; }
        if (argc > 2 && !strcmp(argv[1], "flip")) {            /* temporary diagnostic orientation */
            if (t) { ESP_LOGW(TAG, "ota flip: update-mode diagnostic only"); return; }
            bool inv = !strcmp(argv[2], "on"); display_port_set_inverted(inv); touch_port_set_inverted(inv);
            ESP_LOGI(TAG, "ota flip %s: the picture is %s", inv ? "on" : "off", inv ? "turned over" : "upright"); return; }
        if (argc > 1 && !strcmp(argv[1], "kbd")) { update_debug_keyboard(); ESP_LOGI(TAG, "ota kbd: the keyboard page (calibration; the saved network is untouched)"); return; }
        if (argc > 1 && !strcmp(argv[1], "page")) { ESP_LOGI(TAG, "update mode: %s, page %d, %d characters typed", update_active() ? "active" : "not active", update_page(), (int)strlen(update_typed())); return; }   /* never the text: it may be the saved passphrase */
        ESP_LOGI(TAG, "v%s %s (build %s) running from %s, next boot %s | manifest %s | ota check (= CHECK FOR UPDATES) | ota url <url>|default",
                 PT_RELEASE, PT_RELEASE_STAGE, version_port_string(), run ? run->label : "?", boot ? boot->label : "?", net_port_manifest_url());
    }
    else if (!strcmp(c, "hungry")) {
        int n = argc > 1 ? atoi(argv[1]) : t->n_fish;
        float lvl = argc > 2 ? atof(argv[2]) : 9.0f;
        if (n > t->n_fish) n = t->n_fish;
        for (int i = 0; i < n; i++) t->fish[i].hunger = lvl;
        clear_pellets(t);
        t->trickle_off = true;
        ESP_LOGI(TAG, "%d fish at hunger %.1f, pellets cleared, trickle held (feed / fed / trickle on releases)", n, lvl);
        show_state(t);
    } else if (!strcmp(c, "fed")) {
        float lvl = argc > 1 ? atof(argv[1]) : 1.0f;
        for (int i = 0; i < t->n_fish; i++) t->fish[i].hunger = lvl;
        t->trickle_off = false;
        ESP_LOGI(TAG, "everyone at hunger %.1f, trickle on", lvl);
    } else if (!strcmp(c, "set") && argc >= 3) {
        float lvl = atof(argv[2]);
        int who = argc > 3 ? find_fish(t, argv[3]) : -1;
        if (argc > 3 && who < 0) { ESP_LOGW(TAG, "no fish '%s'", argv[3]); return; }
        int hit = 0;
        for (int i = 0; i < t->n_fish; i++) {
            if (who >= 0 && i != who) continue;
            float *d = drive_of(&t->fish[i], argv[1]);
            if (!d) { ESP_LOGW(TAG, "no drive '%s'", argv[1]); return; }
            *d = lvl < 0 ? 0 : lvl > 10 ? 10 : lvl; hit++;
        }
        ESP_LOGI(TAG, "%s = %.1f for %d fish", argv[1], lvl, hit);
    } else if (!strcmp(c, "feed")) {
        int n = argc > 1 ? atoi(argv[1]) : 3;
        float x = argc > 2 ? atof(argv[2]) : (t->feed_spot_x >= 0 ? t->feed_spot_x : TANK_W * 0.5f);
        t->trickle_off = false;
        tank_feed(t, x, n);
        ESP_LOGI(TAG, "%d pellets at x %.0f (keeper feeding #%d), trickle on", n, x, t->player_feedings);
    } else if (!strcmp(c, "trickle") && argc > 1) {
        t->trickle_off = !strcmp(argv[1], "off");
        ESP_LOGI(TAG, "trickle %s", t->trickle_off ? "OFF" : "on");
    } else if (!strcmp(c, "pellets")) {
        clear_pellets(t); ESP_LOGI(TAG, "pellets cleared");
    } else if (!strcmp(c, "algae") && argc > 1) {
        if (!strcmp(argv[1], "clear")) { memset(t->algae, 0, sizeof t->algae); ESP_LOGI(TAG, "glass clean"); }
        else { int s = atoi(argv[1]); tank_grow_algae(t, s); ESP_LOGI(TAG, "algae +%d steps", s); }
    } else if (!strcmp(c, "veg") && argc > 2) {
        float g = atof(argv[2]);
        if (!strcmp(argv[1], "all")) for (int b = 0; b < tank_veg_beds(t); b++) tank_veg_set(t, b, g);
        else { int b = atoi(argv[1]); if (b >= 0 && b < tank_veg_beds(t)) tank_veg_set(t, b, g); }
        ESP_LOGI(TAG, "veg %s -> %.2f", argv[1], g);
    } else if (!strcmp(c, "light")) {
        tank_toggle_light(t); ESP_LOGI(TAG, "light %s (manual)", t->light_on ? "on" : "off");
    } else if (!strcmp(c, "auto")) {
        tank_light_auto(t); ESP_LOGI(TAG, "light back on the idle rule");
    } else if (!strcmp(c, "sleep") && argc > 1) {
        float h = atof(argv[1]);
        progression_slept(t, h * 3600.0f);      /* growth + the full-night badge, as a real wake would */
        ESP_LOGI(TAG, "slept %.1f h", h);
        show_state(t);
    } else if (!strcmp(c, "tanks")) {
        if (argc == 1) touch_port_show_aquariums(t);
        else if (argc == 2 && !strcmp(argv[1], "demo")) {
            staging_disabled(); return;
        } else if (argc == 4 && !strcmp(argv[1], "tap")) {
            float x, y; char extra;
            if (sscanf(argv[2], "%f%c", &x, &extra) != 1 || sscanf(argv[3], "%f%c", &y, &extra) != 1 ||
                !isfinite(x) || !isfinite(y) || x < 0 || y < 0 || x >= TANK_W || y >= TANK_H || !touch_port_aquariums()) {
                ESP_LOGW(TAG, "tanks tap requires an open aquarium page and framebuffer x y"); return;
            }
            int result = render_aquariums_tap(t, x, y);
            if (result == 1) touch_port_show_shop(true);
            else if (result == 2) {
                touch_port_dismiss(); setup_cancel(t); notice_sync(t); render_fb_primed(NULL, 0);
                if (progression_setup_pending()) setup_begin(t);
            }
        } else { ESP_LOGW(TAG, "tanks | tanks tap <x> <y> | tanks demo"); return; }
        ESP_LOGI(TAG, "aquariums %u/%d | active %u | global sand dollars %ld | ID %08lx%08lx",
                 t->aquariums.count, AQUARIUM_CAP, t->aquariums.active + 1, (long)t->sd_balance,
                 (unsigned long)t->aquariums.id[0], (unsigned long)t->aquariums.id[1]);
        ESP_LOGI(TAG, "active tank name: %s", t->aquariums.name[0] ? t->aquariums.name : "TANK 1");
    } else if (!strcmp(c, "bank")) {
        if (argc == 1) touch_port_show_bank();
        else if (argc == 2 && !strcmp(argv[1], "demo")) {
            staging_disabled(); return;
        } else if (argc == 2 && !strcmp(argv[1], "verify")) {
            render_work_caches(false);
            char error[96]; char *json = progression_backup_json(t);
            if (!json) { ESP_LOGE(TAG, "bank backup allocation failed"); return; }
            size_t len = strlen(json);
            bool ok = progression_restore_json(json, len, false, error, sizeof error);
            free(json);
            if (!ok) { ESP_LOGE(TAG, "bank JSON validation failed: %s", error); return; }
            ESP_LOGI(TAG, "bank JSON round-trip verified (%u bytes, %u stored)", (unsigned)len, t->bank_count);
        }
        else if (argc == 4 && !strcmp(argv[1], "tap")) {
            float x, y; char extra;
            if (sscanf(argv[2], "%f%c", &x, &extra) != 1 || sscanf(argv[3], "%f%c", &y, &extra) != 1 ||
                !isfinite(x) || !isfinite(y) || x < 0 || y < 0 || x >= TANK_W || y >= TANK_H || !touch_port_bank()) {
                ESP_LOGW(TAG, "bank tap requires an open bank and framebuffer x y"); return;
            }
            if (render_bank_tap(t, x, y)) touch_port_show_milestones(true);
        } else { ESP_LOGW(TAG, "bank | bank tap <framebuffer x> <y>"); return; }
        ESP_LOGI(TAG, "bank %u/%d stored | %d/%d active", t->bank_count, FISH_BANK_CAP, t->n_fish, POP_CAP);
    } else if (!strcmp(c, "games")) {
        if (argc == 1) { touch_port_show_games(); tank_handled(t); }
        else if (argc == 4 && !strcmp(argv[1], "tap")) {
            float x, y; char extra;
            if (sscanf(argv[2], "%f%c", &x, &extra) != 1 || sscanf(argv[3], "%f%c", &y, &extra) != 1 ||
                !isfinite(x) || !isfinite(y) || x < 0 || y < 0 || x >= TANK_W || y >= TANK_H || !touch_port_games()) {
                ESP_LOGW(TAG, "games tap requires an open games page and framebuffer x y"); return;
            }
            if (render_games_tap(t, x, y)) touch_port_show_milestones(true);
        } else { ESP_LOGW(TAG, "games | games tap <framebuffer x> <y>"); return; }
        ESP_LOGI(TAG, "games page %s; aquarium care %s", touch_port_games() ? "open" : "closed", touch_port_games() ? "paused" : "resumed");
    } else if (!strcmp(c, "rpg")) {
        if (argc != 1) { ESP_LOGW(TAG, "rpg (read-only campaign status)"); return; }
        ESP_LOGI(TAG, "Lost Current chapter %u/6 encounter %u/3 | courage %u curiosity %u kindness %u | guide %lu",
                 t->rpg.chapter, t->rpg.step, t->rpg.skills[0], t->rpg.skills[1], t->rpg.skills[2],
                 (unsigned long)t->rpg.participant);
        for (int chapter = 0; chapter < t->rpg.chapter; chapter++)
            ESP_LOGI(TAG, "%s | contributor %u | %s", companion_rpg_chapter_title(chapter),
                     t->rpg.contributors[chapter], companion_rpg_route(t, chapter));
    } else if (!strcmp(c, "life") || !strcmp(c, "home")) {
        if (argc == 1) touch_port_show_life();
        else if (argc == 4 && !strcmp(argv[1], "tap")) {
            float x, y; char extra;
            if (sscanf(argv[2], "%f%c", &x, &extra) != 1 || sscanf(argv[3], "%f%c", &y, &extra) != 1 ||
                !isfinite(x) || !isfinite(y) || x < 0 || y < 0 || x >= TANK_W || y >= TANK_H || !touch_port_life()) {
                ESP_LOGW(TAG, "life tap requires an open LIFE page and framebuffer x y"); return;
            }
            touch_port_life_tap(t, x, y);
        } else { ESP_LOGW(TAG, "life | life tap <framebuffer x> <y>"); return; }
        ESP_LOGI(TAG, "desk companion page %s", touch_port_life() ? "open" : "closed");
    } else if (!strcmp(c, "journal")) {
        if (argc == 2 && !strcmp(argv[1], "demo")) {
            staging_disabled(); return;
        } else if (argc == 4 && !strcmp(argv[1], "tap")) {
            float x, y; char extra;
            if (sscanf(argv[2], "%f%c", &x, &extra) != 1 || sscanf(argv[3], "%f%c", &y, &extra) != 1 ||
                !isfinite(x) || !isfinite(y) || x < 0 || y < 0 || x >= TANK_W || y >= TANK_H || !touch_port_journal()) {
                ESP_LOGW(TAG, "journal tap requires an open journal and framebuffer x y"); return;
            }
            if (render_journal_tap(t, x, y)) touch_port_show_milestones(true);
        } else if (argc == 1) touch_port_show_journal();
        else { ESP_LOGW(TAG, "journal | journal tap <framebuffer x> <y>"); return; }
        ESP_LOGI(TAG, "journal level %d XP %lu goals %04lx album %04lx adults %04lx theme %s",
                 journal_level(t), (unsigned long)t->journal.xp, (unsigned long)t->journal.claimed,
                 (unsigned long)t->journal.seen, (unsigned long)t->journal.grown, JOURNAL_THEME_NAMES[t->journal.theme]);
    } else if (!strcmp(c, "settings")) {
        if (argc == 4 && !strcmp(argv[1], "tap")) {
            float x, y; char extra;
            if (sscanf(argv[2], "%f%c", &x, &extra) != 1 || sscanf(argv[3], "%f%c", &y, &extra) != 1 ||
                !touch_port_settings_tap(t, x, y))
                ESP_LOGW(TAG, "settings tap requires an open settings page and valid framebuffer x y");
        } else if (argc == 1 || (argc == 2 && !strcmp(argv[1], "off"))) {
            bool on = argc == 1;
            touch_port_show_settings(on); ESP_LOGI(TAG, "settings page %s", on ? "up (CLOSE ends it)" : "closed");
        } else ESP_LOGW(TAG, "settings | settings tap <framebuffer x> <y> | settings off");
    } else if (!strcmp(c, "milestones")) {
        bool on = argc < 2 || strcmp(argv[1], "off");
        touch_port_show_milestones(on); ESP_LOGI(TAG, "milestones page %s", on ? "up (CLOSE button ends it)" : "closed");
    } else if (!strcmp(c, "shop")) {                 /* the sand dollar page */
        bool on = argc < 2 || strcmp(argv[1], "off");
        touch_port_show_shop(on); ESP_LOGI(TAG, "shop page %s", on ? "up (CLOSE ends it)" : "closed");
    } else if (!strcmp(c, "fry")) {                  /* fry [open|roll|buy <0-2>]: the shop's fry (0.3.5) */
        if (argc > 1 && !strcmp(argv[1], "open")) progression_fry_open(t);
        else if (argc > 1 && !strcmp(argv[1], "roll")) progression_fry_roll(t);
        else if (argc > 2 && !strcmp(argv[1], "buy")) {
            if (progression_buy_fry(t, atoi(argv[2]))) ESP_LOGI(TAG, "fry %d bought: the welcome opens", atoi(argv[2]));
            else ESP_LOGW(TAG, "fry %d refused: not for sale, bought, no room (%d of %d fish), a welcome owed, or %d < %d",
                          atoi(argv[2]), t->n_fish, POP_CAP, (int)t->sd_balance, SD_PRICE_FRY);
        }
        ESP_LOGI(TAG, "fry: %s | %d left, a fresh three in %.1f h | room %s | balance %d", progression_fry_for_sale(t) ? "FOR SALE" : "not yet (sell a fish, or `fry open`)",
                 progression_fry_left(), progression_fry_new_in_s() / 3600, progression_fry_room(t) ? "yes" : "no", (int)t->sd_balance);
        for (int k = 0; k < FRY_OFFER_N; k++) { fry_offer_t o; progression_fry_offer(t, k, &o);
            ESP_LOGI(TAG, "  fry %d: %s / %s, body %06x markings %06x%s", k,
                     FISH_VARIETY_NAMES[o.variety], FISH_PATTERN_NAMES[o.pattern], (unsigned)o.body, (unsigned)o.accent, o.sold ? " - SOLD" : ""); }
    } else if (!strcmp(c, "dollars")) {              /* dollars [n]: grant n (negative takes), or just the balance */
        if (argc > 1) progression_sd_grant(t, atoi(argv[1]));
        ESP_LOGI(TAG, "sand dollars %d (earned %d) | colonies %d | %.0f cm trimmed", (int)t->sd_balance, (int)t->sd_earned,
                 (int)t->algae_colonies, t->trim_px / PX_PER_CM);
    } else if (!strcmp(c, "buy") && argc > 1) {      /* buy plant|snail|...|shrimp: the shop's sale, at the price */
        int item = shop_item(argv[1]);
        if (item < 0) ESP_LOGW(TAG, "buy plant|snail|castle|coral|cluster|shrimp|urchin|seahorse|pumpkin|chest|shell|starfish|anchor|buoy|sand|lights|wreck|urn|crystal|fan|ruins|jelly|pebbles|pearl|log|sponges|barrel|helmet|lilies|duck|filter|garden|current|shelter|play|training");
        else if (progression_buy(t, item)) ESP_LOGI(TAG, "%s unlocked, %d sand dollars left%s", SD_ITEMS[item].name, (int)t->sd_balance,
                                                    tank_decor_placeable(item) ? " (`place` opens the placement page)" : "");
        else ESP_LOGW(TAG, "%s refused: already owned, unavailable, insufficient funds or save failed (%d / %d)", SD_ITEMS[item].name, (int)t->sd_balance, SD_ITEMS[item].price);
    } else if (!strcmp(c, "order")) {                 /* order [plant|castle|coral|cluster|pumpkin forward|back]: the pieces' stack, back to front; a step moves one */
        if (argc > 2) {
            int item = shop_item(argv[1]);
            if (item < 0 || !tank_decor_placeable(item)) { ESP_LOGW(TAG, "order <decoration name> forward|back"); return; }
            if (tank_decor_shift(t, item, !strcmp(argv[2], "back") ? -1 : 1)) progression_save(t);
            else ESP_LOGW(TAG, "%s: nothing to pass that way at its depth", SD_ITEMS[item].name);
        }
        int items[DECOR_SLOTS], n = tank_decor_order(t, items); char line[DECOR_SLOTS * 64] = ""; size_t l = 0;
        for (int i = 0; i < n && l < sizeof line; i++) l += snprintf(line + l, sizeof line - l, "%s%s (%s, rank %d)", i ? " < " : "", SD_ITEMS[items[i]].name,
                                                    tank_decor_z(t, items[i]) == DECOR_Z_BACK ? "behind" : tank_decor_z(t, items[i]) == DECOR_Z_FRONT ? "in front" : "among", tank_decor_rank(t, items[i]));
        if (l >= sizeof line) ESP_LOGW(TAG, "decoration order report truncated");
        ESP_LOGI(TAG, "order, back to front: %s", n ? line : "(no pieces)");
    } else if (!strcmp(c, "style")) {
        bool sand = argc > 1 && !strcmp(argv[1], "sand");
        bool water = argc > 1 && !strcmp(argv[1], "water");
        int choice = -1; char extra;
        if (argc != 3 || (!sand && !water) || sscanf(argv[2], "%d%c", &choice, &extra) != 1 ||
            choice < 0 || choice >= (sand ? SAND_STYLE_COUNT : WATER_STYLE_COUNT)) {
            ESP_LOGW(TAG, "style sand <0..7> | style water <0..7>"); return;
        }
        if (!(t->sd_unlocks & (sand ? SD_ITEM_SAND : SD_ITEM_LIGHTS))) {
            ESP_LOGW(TAG, "unlock %s in the shop first", sand ? "SAND PALETTE" : "MOOD LIGHTS"); return;
        }
        if (sand) t->sand_style = (uint8_t)choice; else t->water_style = (uint8_t)choice;
        progression_settings_changed();
        if (progression_save(t)) ESP_LOGI(TAG, "%s palette %d saved", sand ? "sand" : "water", choice);
        else ESP_LOGE(TAG, "palette change could not be saved");
    } else if (!strcmp(c, "pumpkin")) {               /* pumpkin [on|off]: drawn and lit or not (not saved) - the frame-rate A/B; and its season */
        if (argc > 1) render_set_pumpkin_hidden(!strcmp(argv[1], "off"));
        ESP_LOGI(TAG, "pumpkin: %s, %s | %s | the display line every 10 s has the fps and the render ms",
                 (t->sd_unlocks & SD_ITEM_PUMPKIN) ? "in the tank" : "not bought", render_pumpkin_hidden() ? "HIDDEN - not drawn, no light" : "drawn",
                 SD_PUMPKIN_IN_SHOP ? "this build sells it" : "this build does not sell it");
    } else if (!strcmp(c, "seahorse") && argc > 1) {  /* seahorse on|off: drawn or not (it still lives; not saved) - the frame-rate A/B */
        t->sh_hidden = !strcmp(argv[1], "off");
        ESP_LOGI(TAG, "seahorse %s (the display line every 10 s has the fps and the render ms)", t->sh_hidden ? "HIDDEN - not drawn" : "drawn");
    } else if (!strcmp(c, "coral") && argc > 1) {    /* coral <0..7|rrggbb> (its colour) | coral grow <0.12..1.25> (its growth, staged; 1 = the fan, 1.25 = the crown) */
        if (!(t->sd_unlocks & SD_ITEM_CORAL)) { ESP_LOGW(TAG, "no coral in the tank (`buy coral`)"); return; }
        if (!strcmp(argv[1], "grow") && argc > 2) {
            float g = (float)atof(argv[2]); if (g < CORAL_START) g = CORAL_START; if (g > CORAL_FULL) g = CORAL_FULL;
            t->coral_growth = g; progression_save(t);
            ESP_LOGI(TAG, "coral growth %.2f (a month to 1.00, the crown by %.2f), saved", g, CORAL_FULL); return;
        }
        uint32_t rgb = strlen(argv[1]) >= 6 ? (uint32_t)strtoul(argv[1], NULL, 16) : CORAL_PAL[atoi(argv[1]) < 0 ? 0 : atoi(argv[1]) >= CORAL_N ? CORAL_N - 1 : atoi(argv[1])];
        tank_coral_set_rgb(t, rgb); progression_save(t);
        ESP_LOGI(TAG, "coral colour %06x, saved", (unsigned)tank_coral_rgb(t));
    } else if (!strcmp(c, "fish")) {                 /* fish | fish card <n> | fish rename <n> [name] | fish sell <n>: the milestones card's buttons */
        int n = argc > 2 ? atoi(argv[2]) : -1;
        if (argc > 2 && (n < 0 || n >= t->n_fish)) { ESP_LOGW(TAG, "fish %d: the tank has 0..%d", n, t->n_fish - 1); return; }
        if (argc > 2 && !strcmp(argv[1], "card")) { touch_port_show_milestones(true); render_milestones_show_fish(t, n); ESP_LOGI(TAG, "%s's card up", t->fish[n].name); }
        else if (argc == 3 && !strcmp(argv[1], "customize")) { touch_port_dismiss(); setup_begin_customize(t, n); ESP_LOGI(TAG, "customizing fish %d", n); }
        else if (argc > 3 && !strcmp(argv[1], "rename")) { tank_set_name(t, n, argv[3]); progression_save(t); ESP_LOGI(TAG, "fish %d is %s now, saved", n, t->fish[n].name); }
        else if (argc > 2 && !strcmp(argv[1], "rename")) { touch_port_dismiss(); setup_begin_rename(t, n); ESP_LOGI(TAG, "renaming %s: the wheel is up (CANCEL / DONE)", t->fish[n].name); }
        else if (argc > 2 && !strcmp(argv[1], "sell")) {
            int worth = progression_fish_value(t, n); char nm[FISH_NAME_MAX + 1]; snprintf(nm, sizeof nm, "%s", t->fish[n].name);
            if (progression_sell_fish(t, n)) ESP_LOGI(TAG, "%s sold for %d: %d fish left, balance %d", nm, worth, t->n_fish, (int)t->sd_balance);
            else ESP_LOGW(TAG, "%s stays: the tank keeps %d fish, and a fry owed its welcome holds every sale", nm, FISH_KEEP_MIN);
        } else {
            for (int i = 0; i < t->n_fish; i++)
                ESP_LOGI(TAG, "fish %d: %s (%s / %s), %s, trust %.1f, worth %d%s", i, t->fish[i].name,
                         FISH_VARIETY_NAMES[t->fish[i].variety % FISH_VARIETY_COUNT], FISH_PATTERN_NAMES[t->fish[i].pattern % FISH_PATTERN_COUNT], STAGE_NAMES[t->fish[i].stage & 3], t->fish[i].trust,
                         progression_fish_value(t, i), progression_fish_sellable(t, i) ? "" : " (not for sale)");
            fry_req_t rq[FRY_REQ_MAX]; bool staged; int k = progression_next_fry(t, rq, &staged);
            for (int i = 0; i < k; i++) ESP_LOGI(TAG, "next fry: %s - %s %s (%s)%s", rq[i].title, rq[i].words, rq[i].words2, rq[i].progress, rq[i].met ? " MET" : "");
        }
    } else if (!strcmp(c, "sell") && argc > 1) {     /* sell plant|castle|coral|cluster: the sale back at 20% (never the snail) */
        int item = shop_item(argv[1]);
        if (item < 0 || !tank_decor_placeable(item)) ESP_LOGW(TAG, "sell <decoration name> (residents and palette upgrades stay)");
        else if (progression_sell(t, item)) ESP_LOGI(TAG, "%s sold back for %d, balance %d", SD_ITEMS[item].name, progression_sell_value(item), (int)t->sd_balance);
        else ESP_LOGW(TAG, "%s: not in the tank", SD_ITEMS[item].name);
    } else if (!strcmp(c, "cluster") && argc > 1) {  /* cluster look <0-2> | cluster grow <0..2> (1 = full size, 2 = every tentacle) */
        if (!(t->sd_unlocks & SD_ITEM_CLUSTER)) { ESP_LOGW(TAG, "no cluster in the tank (`buy cluster`)"); return; }
        if (!strcmp(argv[1], "look") && argc > 2) { tank_cluster_set_scheme(t, atoi(argv[2])); progression_save(t); ESP_LOGI(TAG, "cluster look %s, saved", CLUSTER_SCHEMES[tank_cluster_scheme(t)].name); }
        else if (!strcmp(argv[1], "grow") && argc > 2) {
            float g = (float)atof(argv[2]); if (g < CLUSTER_START + 1e-4f) g = CLUSTER_START + 1e-4f; if (g > CLUSTER_FULL) g = CLUSTER_FULL;
            t->cluster_growth = g; progression_save(t); ESP_LOGI(TAG, "cluster growth %.2f (1 = full size, %.0f = every tentacle), saved", g, CLUSTER_FULL); }
        else ESP_LOGW(TAG, "cluster look <0-2> | cluster grow <g>");
    } else if (!strcmp(c, "place")) {                /* place [plant|castle|coral|cluster|pumpkin] [x [behind|among|front]]: the piece's spot; no x = the page */
        int item = 0, a = 1;
        int named = argc > 1 ? shop_item(argv[1]) : -1;
        if (named >= 0) {
            if (!tank_decor_placeable(named)) { ESP_LOGW(TAG, "place <decoration name> [x [behind|front]]"); return; }
            item = named; a = 2;
        }
        const char *what = SD_ITEMS[item].name;
        if (!tank_decor_owned(t, item)) { ESP_LOGW(TAG, "no %s in the tank (`buy %s`)", what, what); return; }
        if (argc <= a) { touch_port_show_shop(false); setup_begin_place(t, item); ESP_LOGI(TAG, "placement page up (drag on the glass, DEPTH, DONE)"); return; }
        int z = tank_decor_z(t, item);
        if (argc > a + 1) z = !strcmp(argv[a + 1], "behind") || !strcmp(argv[a + 1], "back") ? DECOR_Z_BACK : !strcmp(argv[a + 1], "front") ? DECOR_Z_FRONT : DECOR_Z_MIDDLE;
        tank_decor_set(t, item, (float)atof(argv[a]), z); progression_save(t); z = tank_decor_z(t, item);
        ESP_LOGI(TAG, "%s at x %.0f, %s, saved", what, tank_decor_x(t, item),
                 item == 2 ? (z == DECOR_Z_BACK ? "BEHIND the grass" : "IN FRONT of the grass") : z == DECOR_Z_BACK ? "BEHIND the fish" : z == DECOR_Z_FRONT ? "IN FRONT of the fish" : "AMONG the fish");
    } else if (!strcmp(c, "led")) {
        if (argc >= 4) {
            ambient_led((uint8_t)atoi(argv[1]), (uint8_t)atoi(argv[2]), (uint8_t)atoi(argv[3]));
            ambient_led_override(argc > 4 ? atoi(argv[4]) : 10000);
        } else if (argc == 2) ambient_set_led_mode(atoi(argv[1]));
        ambient_log();
    } else if (!strcmp(c, "ldr")) {
        if (argc == 3) ambient_calibrate(atoi(argv[1]), atoi(argv[2]));
        else if (argc == 2) ambient_set_room_mode(atoi(argv[1]));
        ambient_log();
    } else if (!strcmp(c, "visit")) {            /* visit [0-4]: a visitor on the next tick (drifter/turtle/ghost/manta/squid) */
        companion_debug_visit(t, argc > 1 ? atoi(argv[1]) : -1);
        ESP_LOGI(TAG, "visitor due (seen %u/%u/%u/%u/%u)", t->story.seen[0], t->story.seen[1], t->story.seen[2], t->story.seen[3], t->story.seen[4]);
    } else if (!strcmp(c, "season")) {           /* season <0-4, +8 lights | -1 clock> */
        companion_debug_season(t, argc > 1 ? atoi(argv[1]) : -1);
        ESP_LOGI(TAG, "season %u", t->season);
    } else if (!strcmp(c, "genes")) {            /* genes: every fish's shown/hidden alleles */
        for (int i = 0; i < t->n_fish; i++) {
            const fish_lineage_t *l = &t->story.active[i];
            ESP_LOGI(TAG, "%d %s: gen %u pattern %s hidden %d color2 %d traits 0x%02x mutation %u", i, t->fish[i].name, l->generation + 1u,
                     FISH_PATTERN_NAMES[t->fish[i].pattern % FISH_PATTERN_COUNT], l->pattern2 - 1, l->color2 - 1, l->traits, l->mutation);
        }
        ESP_LOGI(TAG, "away %u%s%s, bank %d, births %u, story v%u", t->companion.away, t->companion.away ? ": " : "",
                 t->companion.away ? t->companion.traveler.name : "", t->bank_count, t->story.births, t->story.version);
    } else if (!strcmp(c, "photo")) {
        device_photo(false);
        ESP_LOGI(TAG, "photo requested");
    } else if (!strcmp(c, "autobackup")) {
        if (argc == 2) ambient_set_auto_backup(atoi(argv[1]) != 0);
        else sd_port_auto_backup_now(t);
        ambient_log();
    } else if (!strcmp(c, "bright") && argc > 1) {
        int v = atoi(argv[1]); if (v < 0) v = 0; if (v > 255) v = 255;
        display_port_set_brightness((uint8_t)v);
        ESP_LOGI(TAG, "brightness %d/255", v);
    } else if (!strcmp(c, "clock")) {
        device_clock_log();
    } else if (!strcmp(c, "deepsleep")) {
        int n = argc > 1 ? atoi(argv[1]) : 0;
        ESP_LOGI(TAG, "%s - the USB port vanishes until the wake", n > 0 ? "5 s grace, then deep sleep with the timer" : "the keeper's sleep: the grace, then power-off (the PWR key boots it)");
        vTaskDelay(pdMS_TO_TICKS(50));
        device_sleep(n);
    } else if (!strcmp(c, "poweroff")) {
        ESP_LOGI(TAG, "power-off now (the PWR key or USB boots it) - the USB port vanishes");
        vTaskDelay(pdMS_TO_TICKS(50));
        device_poweroff();
    } else if (!strcmp(c, "level") && argc > 1) {
        if (!brightness_set_level(atoi(argv[1]))) ESP_LOGW(TAG, "level must be 10..100; check NVS if saving fails");
    } else if (!strcmp(c, "reset")) {
        if (argc < 2) { touch_port_confirm_open(); return; }
        int ans = !strcasecmp(argv[1], "yes") ? 1 : !strcasecmp(argv[1], "no") ? -1 : 0;
        if (!ans) { ESP_LOGW(TAG, "reset [yes|no]"); return; }
        if (!touch_port_confirm_answer(ans)) ESP_LOGW(TAG, "no reset prompt is up (`reset` first)");
        else ESP_LOGI(TAG, "reset prompt: %s", ans > 0 ? "YES - the tank task wipes it this frame" : "NO");
    } else if (!strcmp(c, "setup")) {
        if (argc > 1 && !strcmp(argv[1], "tap")) {
            float x, y; char extra;
            if (argc != 4 || sscanf(argv[2], "%f%c", &x, &extra) != 1 || sscanf(argv[3], "%f%c", &y, &extra) != 1 ||
                !isfinite(x) || !isfinite(y) || x < 0 || y < 0 || x >= TANK_W || y >= TANK_H || !setup_active()) {
                ESP_LOGW(TAG, "setup tap <x> <y> requires valid framebuffer coordinates and an open setup page"); return;
            }
            int hit = setup_hit(x, y);
            if (!hit) { ESP_LOGW(TAG, "no setup control at %.1f,%.1f", x, y); return; }
            setup_activate(t, hit); ESP_LOGI(TAG, "setup tap %.1f,%.1f -> control %d (page %d)", x, y, hit, setup_page());
        } else if (argc > 1 && !strcmp(argv[1], "off")) { setup_cancel(t); ESP_LOGI(TAG, "setup panel dropped%s", progression_setup_pending() || progression_newborn() >= 0 ? " (still owed: it returns at the next boot)" : ""); }
        else { setup_begin(t); ESP_LOGI(TAG, "setup: welcome page up (tap through on the glass)"); }
    } else if (!strcmp(c, "kbd")) {
        ESP_LOGW(TAG, "Prototype name keyboards removed; fish names use the letter wheel.");
    } else if (!strcmp(c, "touch")) {
        if (argc > 1 && !strcmp(argv[1], "calibrate")) { touch_port_calibrate(); return; }
        if (argc > 2 && !strcmp(argv[1], "bias")) touch_port_set_bias(atoi(argv[2]));
        if (argc > 2 && !strcmp(argv[1], "log")) { touch_port_set_log(!strcmp(argv[2], "on")); ESP_LOGI(TAG, "touch log %s (a line per press, at its release)", argv[2]); }
        ESP_LOGI(TAG, "touch bias %d px (reported y - %d)", touch_port_bias(), touch_port_bias());
        int x0, x1, y0, y1; touch_port_raw_seen(&x0, &x1, &y0, &y1);
        if (x1 >= x0) ESP_LOGI(TAG, "touch raw seen since boot: x %d..%d of 0..%d, y %d..%d of 0..%d (scrub the four walls first: the stroke's reach assumes the full span)", x0, x1, TANK_W - 1, y0, y1, TANK_H - 1);
    } else if (!strcmp(c, "name") && argc > 2) {
        int who = find_fish(t, argv[1]); if (who < 0) { ESP_LOGW(TAG, "no fish '%s'", argv[1]); return; }
        tank_set_name(t, who, argv[2]); progression_save(t);
        ESP_LOGI(TAG, "fish %d is now %s (saved)", who, t->fish[who].name);
    } else if (!strcmp(c, "save")) {
        if (progression_save(t)) ESP_LOGI(TAG, "saved");
        else ESP_LOGE(TAG, "aquarium save failed");
    } else if (!strcmp(c, "stash")) {
        staging_disabled();
    } else if (!strcmp(c, "restore")) {
        ESP_LOGE(TAG, "NVS restore disabled: it cannot restore expanded saves. Use 'sd restore'; any legacy 'bk' is left untouched.");
    } else if (!strcmp(c, "fresh") || !strcmp(c, "stages") || !strcmp(c, "stage") ||
               !strcmp(c, "overgrown") || !strcmp(c, "court") || !strcmp(c, "arrive") ||
               !strcmp(c, "age")) {
        staging_disabled();
    } else {
        ESP_LOGW(TAG, "unknown: '%s' (help)", c);
    }
}

/* ---- Improv Wi-Fi over serial (docs/OTA.md, 2026-09-30) ----
 * The web installer (ESP Web Tools) speaks Improv on the same USB port after
 * a flash: it asks the device's state and info, then sends a network and a
 * passphrase. The tank STORES them (net_port_creds_set) and reports
 * "provisioned": the radio never runs beside the tank, so the first CHECK
 * FOR UPDATES is what connects (a wrong passphrase ends on the keyboard
 * there). Packets: "IMPROV" 0x01 <type> <len> <data> <checksum = the sum of
 * every byte before it>. Types: 1 state, 2 error, 3 RPC command, 4 RPC
 * result. RPC: 1 send settings (ssid_len ssid pass_len pass) -> the result
 * carries a URL; 2 request state; 3 request info -> name, version, chip,
 * device name; 4 scan -> an empty list beside the tank (no radio there), the
 * real list in provisioning mode (below). The parser rides the
 * console's byte stream: the six header bytes are matched as they arrive
 * and pulled back out of the command line when they complete. */
#define IMPROV_URL "https://pocketank.com/updates/"
/* The handshake's flight recorder (2026-10-04): what the page asked and what the tank answered,
 * across resets in RTC memory, read back with the director's `improv`. The
 * page's connect could not be reproduced from a script - this is the tank's own account of it. */
typedef struct { uint32_t ms; uint8_t kind, a, b, c; } imp_ev_t;   /* kind: 'B' boot (a = reset reason), 'R' ask (a = rpc cmd),
                                                                     'X' dropped (a: 1 stale, 2 checksum, 3 restarted by a header),
                                                                     'T' sent (a = type, b = cmd / state / error), 'F' NOT sent whole, 'H' the poll took over */
#define IMP_EVS 64
RTC_NOINIT_ATTR static imp_ev_t s_ev[IMP_EVS]; RTC_NOINIT_ATTR static uint32_t s_ev_n, s_ev_magic;
static void imp_ev(uint8_t kind, uint8_t a, uint8_t b) {
    if (s_ev_magic != 0x494d5056u || s_ev_n > 1000000u) { s_ev_magic = 0x494d5056u; s_ev_n = 0; }
    imp_ev_t *e = &s_ev[s_ev_n++ % IMP_EVS];
    e->ms = (uint32_t)(esp_timer_get_time() / 1000); e->kind = kind; e->a = a; e->b = b; e->c = 0;
}
static void imp_dump(void) {
    uint32_t n = s_ev_magic == 0x494d5056u ? s_ev_n : 0, from = n > IMP_EVS ? n - IMP_EVS : 0;
    ESP_LOGI(TAG, "improv trace: %lu events (the last %d kept) | B boot(reset reason) R ask(cmd) T sent(type,what) X dropped(1 stale 2 checksum 3 new header) H poll took over", (unsigned long)n, IMP_EVS);
    for (uint32_t i = from; i < n; i++) { const imp_ev_t *e = &s_ev[i % IMP_EVS];
        ESP_LOGI(TAG, "  %7lu ms  %c %d %d", (unsigned long)e->ms, e->kind, e->a, e->b); }
}
#define IMPROV_STALE_US 300000              /* a packet is a few dozen bytes sent at once: 0.3 s without its end = cut short */
static int s_imp_match, s_imp_len, s_imp_n; static bool s_imp_in; static uint8_t s_imp[128];   /* a 32-byte name + a 63-byte passphrase fit */
/* Provisioning mode (2026-10-03, update_mode.c provision_mode_run): right
 * after an install, before the tank exists, the radio is free - the page's
 * scan gets the real list, and the network it sends is CONNECTED to before
 * it is saved, so a wrong password shows on the page, at install. */
static bool s_imp_radio, s_imp_done; static int s_imp_job;   /* job: 0 idle, 1 scanning, 2 connecting */
static bool s_imp_form;                                      /* the page's Wi-Fi form has opened (a scan or a network came) */
static bool s_imp_tank;                                      /* the poll came from the running tank (not update mode) */
static int64_t s_imp_last;                                   /* the last packet from the page (0 = none yet) */
static char s_imp_ssid[NET_SSID_MAX + 1], s_imp_pass[NET_PASS_MAX + 1];
void net_port_esp_connect_budget(int attempts, int wait_ms);
static void improv_send(uint8_t type, const uint8_t *data, int n) {
#if CONFIG_SOC_USB_SERIAL_JTAG_SUPPORTED
    /* Every packet starts on a NEW LINE and ends one (2026-10-04). The page's parser takes a packet
     * only at the start of a line, and skips everything up to the next newline once a line began
     * with anything else. At a connect Chrome first hands it what sat in the USB FIFO - the tail
     * of some log line, cut off, no newline - so the page was "inside a line" and threw every
     * answer away until the tank's next log line ended (seconds apart on a quiet tank): asked
     * four times, answered four times, not one answer taken. Seen in the bytes Chrome received
     * (a diagnostic page) after the tank's own trace showed it answering every ask. */
    uint8_t p[120]; int k = 0;
    if (n < 0 || n > (int)sizeof p - 12) return;
    p[k++] = '\n';
    memcpy(p + k, "IMPROV", 6); k += 6; p[k++] = 1; p[k++] = type; p[k++] = (uint8_t)n;
    memcpy(p + k, data, n); k += n;
    uint8_t sum = 0; for (int i = 1; i < k; i++) sum += p[i];
    p[k++] = sum;
    p[k++] = '\n';
    int wrote = s_ok ? usb_serial_jtag_write_bytes(p, k, pdMS_TO_TICKS(50)) : -1;
    imp_ev(wrote == k ? 'T' : 'F', type, n ? data[0] : 0);   /* 'F': the driver did not take the whole packet */
#else
    (void)type; (void)data; (void)n;
#endif
}
static void improv_state(void) {
    char ssid[NET_SSID_MAX + 1], pass[NET_PASS_MAX + 1];
    uint8_t st = net_port_creds_get(ssid, pass) ? 4 : 2;     /* provisioned : ready */
    improv_send(1, &st, 1);
}
static void improv_result(uint8_t cmd, const char *const *strs, int n) {   /* an RPC result: cmd, len, then length-prefixed strings */
    uint8_t d[96]; int k = 2;
    for (int i = 0; i < n; i++) { int l = (int)strlen(strs[i]); if (k + 1 + l > (int)sizeof d) break; d[k++] = (uint8_t)l; memcpy(d + k, strs[i], l); k += l; }
    d[0] = cmd; d[1] = (uint8_t)(k - 2);
    improv_send(4, d, k);
}
static void improv_handle(void) {
    if (s_imp[7] != 3) return;                               /* only RPC commands come in */
    uint8_t cmd = s_imp[9], n = s_imp[10]; const uint8_t *a = s_imp + 11;
    (void)n;
    s_imp_last = esp_timer_get_time();
    imp_ev('R', cmd, 0);
    if (cmd == 1) {
        int sl = a[0]; const char *sp = (const char *)a + 1; int pl = a[1 + sl]; const char *pp = (const char *)a + 2 + sl;
        char ssid[NET_SSID_MAX + 1], pass[NET_PASS_MAX + 1];
        if (sl > NET_SSID_MAX || pl > NET_PASS_MAX || !sl) { uint8_t e = 1; improv_send(2, &e, 1); return; }
        snprintf(ssid, sizeof ssid, "%.*s", sl, sp); snprintf(pass, sizeof pass, "%.*s", pl, pp);
        uint8_t st = 3; improv_send(1, &st, 1);
        if (s_imp_radio) {                                   /* try it first: the answer comes from director_provision_tick */
            s_imp_form = true;
            snprintf(s_imp_ssid, sizeof s_imp_ssid, "%s", ssid); snprintf(s_imp_pass, sizeof s_imp_pass, "%s", pass);
            if (s_imp_job == 1) net_port_abort();
            net_port_esp_connect_budget(2, 15000);           /* inside the page's 45 s */
            net_port_connect_start(s_imp_ssid, s_imp_pass); s_imp_job = 2;
            ESP_LOGI(TAG, "Improv: connecting to %s", ssid);
            return;
        }
        net_port_creds_set(ssid, pass);
        ESP_LOGI(TAG, "Improv: network %s stored (the first CHECK FOR UPDATES connects)", ssid);
        st = 4; improv_send(1, &st, 1);
        const char *url[1] = { IMPROV_URL }; improv_result(1, url, 1);
        uint8_t e = 0; improv_send(2, &e, 1);
    } else if (cmd == 2) {
        improv_state();
        char ssid[NET_SSID_MAX + 1], pass[NET_PASS_MAX + 1];
        if (net_port_creds_get(ssid, pass)) { const char *url[1] = { IMPROV_URL }; improv_result(2, url, 1); }
    } else if (cmd == 3) {
        /* the version is the installer manifest's own string (make_installer.py release_version:
           "v0.3.0 alpha (build 1a2b3c4)"): the page hides its update item when the two are equal */
        char ver[48]; snprintf(ver, sizeof ver, "v" PT_RELEASE " " PT_RELEASE_STAGE " (build %s)", version_port_string());
        const char *info[4] = { "Pocket Tank", ver, "esp32-s3", "Pocket Tank" };
        improv_result(3, info, 4);
    } else if (cmd == 4) {
        if (s_imp_radio) s_imp_form = true;
        if (s_imp_radio && s_imp_job == 0) { net_port_scan_start(); s_imp_job = 1; }   /* the list follows from director_provision_tick */
        else if (!s_imp_radio && s_imp_tank) {               /* no radio beside the tank: restart into provisioning mode, which answers this scan */
            ESP_LOGI(TAG, "Improv: the page asked for the networks - saving, restarting into provisioning mode");
            device_provision_request();
        } else if (!s_imp_radio || s_imp_job == 2) improv_result(4, NULL, 0);   /* update mode, or mid-connect: an empty list */
    } else { uint8_t e = 2; improv_send(2, &e, 1); }
}
/* provisioning mode's side of the conversation (update_mode.c) */
void director_provision_begin(void) { s_imp_radio = true; s_imp_done = false; s_imp_job = 0; s_imp_form = false; }
bool director_provision_form(void) { return s_imp_form; }
void director_provision_end(void) { s_imp_radio = false; s_imp_job = 0; }
void director_provision_scan(void) { s_imp_form = true; if (s_imp_radio && s_imp_job == 0) { net_port_scan_start(); s_imp_job = 1; s_imp_last = esp_timer_get_time(); } }
bool director_provision_done(void) { return s_imp_done; }
bool director_provision_busy(void) { return s_imp_job != 0; }
int64_t director_provision_last_us(void) { return s_imp_last; }
void director_provision_tick(void) {
    if (s_imp_job == 1) {
        int st = net_port_scan_state();
        if (st == NET_BUSY) return;
        if (st == NET_DONE) {
            net_ap_t aps[NET_SCAN_MAX]; int n = net_port_scan_results(aps, NET_SCAN_MAX);
            for (int i = 0; i < n; i++) {
                char rssi[8]; snprintf(rssi, sizeof rssi, "%d", aps[i].rssi);
                const char *row[3] = { aps[i].ssid, rssi, aps[i].secured ? "YES" : "NO" };
                improv_result(4, row, 3);
            }
        }
        improv_result(4, NULL, 0);                           /* the end of the list (an empty one after a failed scan) */
        s_imp_job = 0;
        s_imp_last = esp_timer_get_time();                   /* the quiet clock starts when the answer is out */
    } else if (s_imp_job == 2) {
        int st = net_port_connect_state();
        if (st == NET_BUSY) return;
        s_imp_job = 0;
        s_imp_last = esp_timer_get_time();                   /* a refused password: the keeper gets the whole quiet window to type it again */
        if (st == NET_DONE) {
            net_port_creds_set(s_imp_ssid, s_imp_pass);
            ESP_LOGI(TAG, "Improv: connected to %s - stored", s_imp_ssid);
            uint8_t s4 = 4; improv_send(1, &s4, 1);
            const char *url[1] = { IMPROV_URL }; improv_result(1, url, 1);
            s_imp_done = true;
        } else {
            ESP_LOGW(TAG, "Improv: could not connect to %s (reason %d) - nothing stored", s_imp_ssid, net_port_fail_reason());
            uint8_t e = 3; improv_send(2, &e, 1);            /* "unable to connect": the page asks again */
            uint8_t s2 = 2; improv_send(1, &s2, 1);
        }
        memset(s_imp_pass, 0, sizeof s_imp_pass);
    }
}
/* one byte of the console stream; true = it belonged to an Improv packet */
static bool improv_feed(uint8_t ch) {
    static const char HDR[6] = { 'I', 'M', 'P', 'R', 'O', 'V' };
    /* A packet CUT SHORT must not eat the next one (2026-10-04). The page's connect resets the
     * board and its first ask can arrive with its tail lost in the reset; the old parser then read
     * the next packet's bytes as the rest of the first - a length byte out of the letters, tens of
     * bytes "still to come" - and swallowed every ask that followed: the page saw no tank ("Install
     * Pocket Tank" on a tank that has it, about one connect in three). So: a header seen INSIDE a
     * packet starts a new packet; a packet not finished in IMPROV_STALE_US is dropped; and a packet
     * that fails its checksum is dropped in silence (an error packet makes the page give up at once). */
    static int64_t t_in; static int inner;
    if (s_imp_in && esp_timer_get_time() - t_in > IMPROV_STALE_US) { s_imp_in = false; s_imp_n = 0; s_imp_match = 0; imp_ev('X', 1, 0); }
    if (s_imp_in) {
        inner = ch == HDR[inner] ? inner + 1 : ch == HDR[0] ? 1 : 0;
        if (inner == 6) { inner = 0; s_imp_n = 6; t_in = esp_timer_get_time(); imp_ev('X', 3, 0); return true; }   /* a fresh header: start over from it */
        if (s_imp_n < (int)sizeof s_imp) s_imp[s_imp_n++] = ch;
        if (s_imp_n == 9) s_imp_len = 10 + ch;               /* header 6 + version + type + len byte + data + checksum */
        if (s_imp_n >= 10 && s_imp_n >= s_imp_len) {
            uint8_t sum = 0; for (int i = 0; i < s_imp_len - 1; i++) sum += s_imp[i];
            if (s_imp_len <= (int)sizeof s_imp && sum == s_imp[s_imp_len - 1] && s_imp[6] == 1) improv_handle();
            else imp_ev('X', 2, 0);
            s_imp_in = false; s_imp_n = 0; s_imp_match = 0;
        }
        return true;
    }
    s_imp_match = ch == HDR[s_imp_match] ? s_imp_match + 1 : ch == HDR[0] ? 1 : 0;
    if (s_imp_match == 6) {                                  /* a packet: pull the header back out of the command line */
        s_imp_in = true; s_imp_n = 6; memcpy(s_imp, HDR, 6); t_in = esp_timer_get_time(); inner = 0;
        if (s_len >= 5) s_len -= 5;
        return true;
    }
    return false;
}

void director_init(void) {
#if CONFIG_SOC_USB_SERIAL_JTAG_SUPPORTED
    if (s_ok) return;                                        /* update mode brought it up already */
    usb_serial_jtag_driver_config_t cfg = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    s_ok = usb_serial_jtag_driver_install(&cfg) == ESP_OK;
    ESP_LOGI(TAG, "%s", s_ok ? "console ready (type help)" : "USB serial driver failed: console off");
    if (nvs_has("bk")) ESP_LOGW(TAG, "Legacy NVS 'bk' is present. It is not the expanded active save; automatic NVS stash/restore is disabled.");
    improv_state();                                          /* the installer page listens for this after a flash */
#else
    if (s_ok) return;
    esp_err_t err = uart_driver_install(UART_NUM_0, 512, 0, 0, NULL, 0);
    s_ok = err == ESP_OK;
    if (s_ok) ESP_LOGI(TAG, "UART console ready (type help)");
    else ESP_LOGE(TAG, "UART console failed: %s", esp_err_to_name(err));
#endif
}

/* The installer page gives a board it connects to 1.5 s to answer Improv,
 * and its connect RESETS the board: its first ask dies in the bootloader,
 * its second comes 1.0 s in. The console used to come up ~1.3-1.6 s into
 * the boot (after the display, the glass, the PMIC), so a tank with a
 * network saved - which must ANSWER, not just announce itself - was never
 * recognized: the page showed "Install" and no Wi-Fi item. director_early
 * runs at the top of app_main: the console, and a small task that answers
 * Improv (and nothing else) until the first director_poll takes over. */
#if CONFIG_SOC_USB_SERIAL_JTAG_SUPPORTED
static volatile bool s_early_run; static volatile TaskHandle_t s_early;
static void early_task(void *arg) {
    (void)arg;
    uint8_t buf[32]; int n;
    while (s_early_run) {
        while ((n = usb_serial_jtag_read_bytes(buf, sizeof buf, 0)) > 0)
            for (int i = 0; i < n; i++) improv_feed(buf[i]);   /* a command typed this early is dropped */
        vTaskDelay(pdMS_TO_TICKS(15));
    }
    s_len = 0;
    s_early = NULL;
    vTaskDelete(NULL);
}
#endif
void director_early(void) {
#if CONFIG_SOC_USB_SERIAL_JTAG_SUPPORTED
    imp_ev('B', (uint8_t)esp_reset_reason(), 0);
    director_init();
    if (!s_ok || s_early) return;
    s_early_run = true;
    TaskHandle_t h = NULL;
    if (xTaskCreate(early_task, "improv", 3072, NULL, 5, &h) == pdPASS) s_early = h; else s_early_run = false;
#endif
}

void director_poll(tank_t *t) {
    if (!s_ok) return;
#if CONFIG_SOC_USB_SERIAL_JTAG_SUPPORTED
    if (s_early_run) { s_early_run = false; while (s_early) vTaskDelay(1); imp_ev('H', t != NULL, 0); }   /* the boot's Improv task hands over */
#endif
    s_imp_tank = t != NULL;
    uint8_t buf[32]; int n;
#if CONFIG_SOC_USB_SERIAL_JTAG_SUPPORTED
    while ((n = usb_serial_jtag_read_bytes(buf, sizeof buf, 0)) > 0) {
#else
    while ((n = uart_read_bytes(UART_NUM_0, buf, sizeof buf, 0)) > 0) {
#endif
        for (int i = 0; i < n; i++) {
            char ch = (char)buf[i];
            if (improv_feed(buf[i])) continue;              /* an Improv packet from the installer page */
            if (ch == '\n' || ch == '\r') {
                if (s_len) { s_line[s_len] = 0; run(t, s_line); }
                s_len = 0;
            } else if (s_len < (int)sizeof s_line - 1) s_line[s_len++] = ch;
        }
    }
}
