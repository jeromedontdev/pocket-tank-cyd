/* update.h - over-the-air updates: the pages and the flow (docs/OTA.md,
 * 2026-09-30). Platform-independent, like every page in common/: the
 * radio, the credential store and the download are PORTS (net_port_*),
 * implemented by firmware/main/net_port_esp.c on the tank and by
 * sim/net_port_sim.c with pretend networks on the desktop, so the pages are
 * built and self-tested in the sim like everything else.
 *
 * Two things live here:
 *
 * 1. THE UPDATES PAGE in the tank (from the settings page's UPDATES
 *    button): the installed version, SD package status, CHECK SD CARD /
 *    INSTALL AND RESTART, CLOSE. No radio. UPD_TAP_CHECK hands validation
 *    and installation to the platform. The legacy Wi-Fi flow below is
 *    accessible through the serial console only. The Wi-Fi-only variant is
 *    also opened by Settings > CLOCK, without any firmware check.
 *
 * 2. UPDATE / WI-FI SETUP MODE, at boot, before the tank exists (main.c): the display,
 *    the glass and the radio only, so the Wi-Fi driver has the whole
 *    internal heap. update_begin chooses a saved network or the wizard,
 *    update_tick polls the port and
 *    advances, update_touch takes the finger every frame, render_update
 *    draws. The platform loops until update_outcome says BACK (radio off,
 *    fall through to the normal boot) or RESTART (the new image is in the
 *    other slot; restart into it). Nothing here turns the radio off by
 *    itself: net_port_off is the platform's, called on the way out.
 *
 * The wizard: LOOKING FOR NETWORKS -> the list (name, signal bars, OPEN) ->
 * the password keyboard (all QWERTY letters on one page, a case toggle,
 * and separate number/symbol layers: a WPA passphrase
 * is up to 63 of any printable character) -> CONNECTING -> CHECKING -> the
 * offer (VERSION X IS READY, the note, YES / NOT NOW) -> DOWNLOADING with a
 * bar and CANCEL -> RESTARTING. Every failure is a short message with TRY
 * AGAIN / OTHER NETWORK / BACK TO TANK, and leaves the old tank as it was. */
#ifndef UPDATE_H
#define UPDATE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ---- the net port (platform) ---- */
#define NET_SSID_MAX   32
#define NET_PASS_MAX   63
#define NET_SCAN_MAX   12
#define NET_URL_MAX    160
typedef struct { char ssid[NET_SSID_MAX + 1]; int rssi; bool secured; } net_ap_t;
typedef struct {
    char     release[16];        /* "0.3.0" */
    uint32_t release_num;        /* PT_RELEASE_NUM's encoding */
    char     build[24];          /* the git build id */
    uint32_t min_from;           /* the oldest release this one updates from (0 = any) */
    char     note[72];           /* one plain line for the offer page */
    uint32_t app_size;           /* bytes */
    bool     needs_cable;
    char     board[24];          /* the board the image is for (PT_BOARD); another board's manifest is refused */
} update_manifest_t;
bool update_parse_manifest(const char *json, update_manifest_t *manifest, char *app_url, size_t url_size);

enum { NET_IDLE = 0, NET_BUSY, NET_DONE, NET_FAILED };
enum { NET_ERR_NONE = 0, NET_ERR_NO_SIGNAL, NET_ERR_PASSWORD, NET_ERR_NO_NET, NET_ERR_BAD_MANIFEST,
       NET_ERR_DOWNLOAD, NET_ERR_VERIFY, NET_ERR_SPACE, NET_ERR_ABORTED, NET_ERR_RADIO,
       NET_ERR_BOARD, NET_ERR_CHANNEL };

bool net_port_creds_get(char *ssid, char *pass);            /* the saved network (buffers NET_*_MAX + 1); false = none */
bool net_port_creds_set(const char *ssid, const char *pass);
bool net_port_creds_forget(void);
bool net_port_connected(void);                          /* snapshot only; never starts/connects the radio */
/* Frame-safe background lease. Foreground scan/connect/update always wins;
 * releasing the lease powers down only background-owned Wi-Fi. */
void net_port_weather(bool wanted);
bool net_port_weather_active(void); /* includes asynchronous radio teardown */
/* the radio. Every call returns at once; the flow polls the *_state calls
 * each frame (NET_BUSY / NET_DONE / NET_FAILED, the reason in
 * net_port_fail_reason). One operation at a time. */
void net_port_scan_start(void);
int  net_port_scan_state(void);
int  net_port_scan_results(net_ap_t *out, int max);          /* after NET_DONE: strongest first, names unique */
void net_port_connect_start(const char *ssid, const char *pass);
int  net_port_connect_state(void);
void net_port_check_start(void);                            /* fetch the manifest (the platform knows the URL) */
int  net_port_check_state(const update_manifest_t **m);     /* NET_DONE: *m valid until the next call */
void net_port_install_start(void);                          /* download, verify, arm the other slot */
int  net_port_install_state(int *percent);                  /* NET_BUSY with the percent; NET_DONE = ready to restart */
void net_port_abort(void);                                  /* stop a download (the flow reports NET_ERR_ABORTED) */
void net_port_off(void);                                    /* the radio off, the driver gone: the platform's way out */
int  net_port_fail_reason(void);
/* the message page's title (tests ask which message is up) */
const char *update_message_title(void);

/* ---- the UPDATES page in the tank ---- */
enum { UPD_TAP_NONE = 0, UPD_TAP_CLOSE, UPD_TAP_CHECK, UPD_TAP_FORGET };
void render_updates_page(uint16_t *fb, int stride);
void updates_sd_status(const char *message, bool ready);
int  updates_page_tap(float x, float y);                    /* the bare hit test */
int  updates_page_touch(float x, float y, bool down);       /* every frame: UPD_TAP_* on a tap's release */

/* the pages' layout (PAGE coordinates, render.h; 2026-10-01: out of update.c,
 * setup.h's way, so the sim's selftest taps where the layout says on every board) */
#define UPD_PANEL_W   384                       /* the pages' panel: x 32..416 */
#define UPD_CHECK_X   32
#define UPD_CHECK_Y   206
#define UPD_CHECK_W   384
#define UPD_CHECK_H   44
#define UPD_FORGET_X  32
#define UPD_FORGET_Y  264
#define UPD_FORGET_W  212
#define UPD_FORGET_H  34
#define UPD_CLOSE_X   324
#define UPD_CLOSE_Y   312
#define UPD_CLOSE_W   92
#define UPD_CLOSE_H   30
/* update mode: staggered QWERTY rows, then a wide action row */
#define UPD_KB_COLS 10
#define UPD_KB_ROWS 3
#define UPD_KB_PX   42
#define UPD_KB_PY   60
#define UPD_KB_W    38
#define UPD_KB_H    54
#define UPD_KB_X    14
#define UPD_KB_Y    100
#define UPD_BTN_H     38
#define UPD_BTN_Y     296
#define UPD_BTN_L_X   32
#define UPD_BTN_R_X   240
#define UPD_BTN_W     176
/* a button on its own: CANCEL, a lone BACK TO TANK (the PLUG IN page's too,
                                                      Strato 2026-10-01; it stood at the left) - the page's middle
                                                      (until 2026-10-01 it was measured from the left button, which the bowl
                                                      draws in: its CANCEL sat 30 px right of center) */
#define UPD_BTN_MID_X ((PAGE_W - UPD_BTN_W) / 2)
#define UPD_ROW_Y0    58
#define UPD_ROW_H     38
#define UPD_ROWS_PER  6
#define UPD_ARROW_X   404

/* ---- update mode ---- */
enum { UPD_RUNNING = 0, UPD_BACK, UPD_RESTART };
enum { UPD_PG_NONE = 0, UPD_PG_BUSY = 2, UPD_PG_SCAN, UPD_PG_PASSWORD, UPD_PG_OFFER, UPD_PG_MESSAGE, UPD_PG_CALIB };
void update_begin(void);
void update_wifi_begin(void);                              /* Wi-Fi only: never checks or installs firmware */
void update_tick(float dt);
void update_touch(float x, float y, bool down);
void render_update(uint16_t *fb, int stride, float clock);
/* the clockless board's wake (2026-10-03): the glass while the boot asks the
 * internet for the time - a few seconds that would otherwise be dark, and a
 * dark glass after a press gets pressed again. Nothing to tap. */
void render_clock_sync(uint16_t *fb, int stride, float clock);
int  update_outcome(void);
bool update_active(void);
int  update_page(void);                                     /* UPD_PG_* (tests) */
int  update_hit(float x, float y);                          /* the page's element under (x,y), 0 = none (tests) */
const char *update_hit_name(int id);
const char *update_typed(void);                             /* the password field (tests) */
void update_debug_keyboard(void);                           /* the keyboard page for a pretend network, the store untouched (touch calibration) */
void update_debug_calib(void);                              /* nine crosshairs, a press each: the platform logs where the panel says the finger was */
int  update_calib_target(int *x, int *y);                   /* the crosshair up (0..8, -1 = none), its centre */
#define UPD_UP_TO_DATE_S    8.0f                            /* the up-to-date page goes back by itself */

/* the case-sensitive 5x7 text (SSIDs and passwords are not upper case):
 * every printable ASCII character; the tank's own font stays as it is */
int  update_text_w(const char *s, int scale);
void update_text(uint16_t *fb, int stride, int x, int y, int scale, uint32_t rgb, const char *s);

/* the platform's one-line reason for the log */
const char *net_err_name(int err);
#endif
