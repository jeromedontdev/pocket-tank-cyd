/* XPT2046 sampling, persistent affine calibration and touch gestures. */
#include "touch_port.h"
#include "sd_port_cyd.h"
#include "display_port.h"
#include "update.h"
#include "board_pins.h"
#include "board_hardware.h"
#include "tank.h"
#include "render.h"
#include "home_navigation.h"
#include "setup.h"
#include "notice.h"
#include "progression.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <math.h>
#include <string.h>
#include "nvs.h"

static const char *TAG = "touch";
static bool s_xpt_ready;
static bool s_xpt_down;
static int64_t s_xpt_last_idle_log;
static bool s_down; static int64_t s_press_us; static float s_px, s_py;
static int64_t s_seen_us;
static float s_lx, s_ly;                          /* LAST touched position (release classification) */
static float s_fx[N_FISH_MAX], s_fy[N_FISH_MAX];  /* fish positions at press time */
static int s_sel = -1; static int64_t s_sel_us;   /* tapped fish -> stats card */
static bool s_ms;                                 /* milestones page up (its CLOSE button ends it) */
static bool s_from_home, s_home_tanks, s_child_from_keeper; /* BACK route, separate from active overlays */
static bool s_journal, s_bank, s_games, s_life;
static bool s_aquariums;
static bool s_cf; static int64_t s_cf_us; static int s_cf_ans;   /* reset confirm prompt */
static bool s_set;                                /* settings page up (CLOSE returns to the milestones page) */
static bool s_shop;                               /* the shop page up (CLOSE returns to the milestones page) */
static bool s_back;                               /* the settings page's CLOSE just brought the milestones page back: that
                                                     release must not reach the page as a tap on ITS CLOSE (same spot) */
static int  s_shop_act;                           /* an UNLOCK / MOVE / SELL tapped: the raw tap code, for main (one-shot) */
static bool s_held_page;                          /* this press opened a piece's page by holding on it (2026-09-24) */
static int  s_set_what, s_set_val;                /* a segment tapped: SET_TAP_* + value, for main */
#define CONFIRM_TIMEOUT_US (20LL * 1000000)
static bool s_inverted;                           /* screen 180-flipped: mirror into tank space */
/* Fingers land a little BELOW where the eye aims - the pad rolls onto the
 * glass under the fingertip (phones shift their hit targets down for the
 * same reason; Strato saw it on the swatch rows, 2026-09-13). Reported
 * points move UP by this many px in displayed space; director `touch bias
 * <px>` tunes it live. */
static int s_bias_y;
void touch_port_set_bias(int px) { s_bias_y = px; }
int  touch_port_bias(void) { return s_bias_y; }

typedef struct { float x[3], y[3]; } cyd_cal_t;
static cyd_cal_t s_calibration = {{1, 0, 0}, {0, 1, 0}};
static void cyd_cal_default(void) {
    /* Preserve the raw coordinate basis used by existing saved calibrations. */
    s_calibration = board_hardware_current()->default_touch_flip_x ?
        (cyd_cal_t){{-1, 0, 239}, {0, 1, 0}} : (cyd_cal_t){{1, 0, 0}, {0, 1, 0}};
}
static int s_cal_target = -1;
static float s_cal_x[5], s_cal_y[5], s_cal_sum_x, s_cal_sum_y;
static unsigned s_cal_samples;
static bool s_cal_wait_release, s_cal_inverted;
static int64_t s_cal_deadline;
static const float s_cal_targets[5][2] = {{30, 30}, {209, 30}, {30, 149}, {209, 149}, {120, 90}};
static bool cyd_cal_valid(const cyd_cal_t *cal) {
    for (int i = 0; i < 3; i++)
        if (!isfinite(cal->x[i]) || !isfinite(cal->y[i])) return false;
    float det = cal->x[0] * cal->y[1] - cal->x[1] * cal->y[0];
    return fabsf(det) > 0.1f && fabsf(det) < 10;
}
static void cyd_cal_load(void) {
    nvs_handle_t h;
    esp_err_t err = nvs_open("cyd_touch", NVS_READONLY, &h);
    if (err == ESP_ERR_NVS_NOT_FOUND) return;
    if (err != ESP_OK) { ESP_LOGE(TAG, "calibration open: %s", esp_err_to_name(err)); return; }
    cyd_cal_t cal; size_t size = sizeof cal;
    err = nvs_get_blob(h, "affine_v1", &cal, &size);
    nvs_close(h);
    if (err == ESP_ERR_NVS_NOT_FOUND) return;
    if (err != ESP_OK || size != sizeof cal || !cyd_cal_valid(&cal)) {
        ESP_LOGE(TAG, "invalid saved touch calibration: %s", esp_err_to_name(err)); return;
    }
    s_calibration = cal;
    ESP_LOGI(TAG, "saved CYD touch calibration loaded");
}
static void cyd_cal_finish(void) {
    float ax = s_cal_x[1] - s_cal_x[0], ay = s_cal_y[1] - s_cal_y[0];
    float bx = s_cal_x[2] - s_cal_x[0], by = s_cal_y[2] - s_cal_y[0];
    float det = ax * by - ay * bx;
    if (fabsf(det) < 100) { ESP_LOGE(TAG, "calibration targets too close; restarting"); s_cal_target = 0; return; }
    float dx = s_cal_inverted ? -179 : 179, dy = s_cal_inverted ? -119 : 119;
    cyd_cal_t cal = {{dx * by / det, -dx * bx / det, 0},
                     {-dy * ay / det, dy * ax / det, 0}};
    cal.x[2] = (s_cal_inverted ? 209 : 30) - cal.x[0] * s_cal_x[0] - cal.x[1] * s_cal_y[0];
    cal.y[2] = (s_cal_inverted ? 149 : 30) - cal.y[0] * s_cal_x[0] - cal.y[1] * s_cal_y[0];
    float error = 0;
    for (int i = 3; i < 5; i++) {
        float x = cal.x[0] * s_cal_x[i] + cal.x[1] * s_cal_y[i] + cal.x[2];
        float y = cal.y[0] * s_cal_x[i] + cal.y[1] * s_cal_y[i] + cal.y[2];
        if (s_cal_inverted) { x = 239 - x; y = 179 - y; }
        error = fmaxf(error, hypotf(x - s_cal_targets[i][0], y - s_cal_targets[i][1]));
    }
    if (!cyd_cal_valid(&cal) || error > 6) {
        ESP_LOGE(TAG, "touch calibration failed validation (%.1f px); restarting", error);
        s_cal_target = 0; return;
    }
    nvs_handle_t h;
    esp_err_t err = nvs_open("cyd_touch", NVS_READWRITE, &h);
    if (err == ESP_OK) {
        err = nvs_set_blob(h, "affine_v1", &cal, sizeof cal);
        if (err == ESP_OK) err = nvs_commit(h);
        nvs_close(h);
    }
    if (err != ESP_OK) { ESP_LOGE(TAG, "saving calibration failed: %s; retrying", esp_err_to_name(err)); s_cal_target = 0; return; }
    s_calibration = cal; s_cal_target = -1;
    ESP_LOGI(TAG, "touch calibration saved; validation error %.1f px", error);
}
static void cyd_cal_poll(bool touched, float x, float y) {
    if (esp_timer_get_time() > s_cal_deadline || s_inverted != s_cal_inverted) {
        ESP_LOGW(TAG, "touch calibration canceled: timeout or orientation change");
        s_cal_target = -1; s_down = false; return;
    }
    if (s_cal_wait_release) { if (!touched) s_cal_wait_release = false; return; }
    if (touched) { s_cal_sum_x += x; s_cal_sum_y += y; s_cal_samples++; return; }
    if (!s_cal_samples) return;
    if (s_cal_samples >= 3) {
        s_cal_x[s_cal_target] = s_cal_sum_x / s_cal_samples;
        s_cal_y[s_cal_target] = s_cal_sum_y / s_cal_samples;
        if (++s_cal_target == 5) cyd_cal_finish();
        s_cal_deadline = esp_timer_get_time() + 90000000;
    }
    s_cal_samples = 0; s_cal_sum_x = s_cal_sum_y = 0;
}
bool touch_port_calibrating(void) {
    return s_cal_target >= 0;
}
void touch_port_calibrate(void) {
    touch_port_dismiss(); s_down = false;
    s_cal_target = 0; s_cal_samples = 0; s_cal_sum_x = s_cal_sum_y = 0;
    s_cal_wait_release = true; s_cal_inverted = s_inverted;
    s_cal_deadline = esp_timer_get_time() + 90000000;
    ESP_LOGI(TAG, "touch calibration: hold each cross briefly, then release; timeout 90 s");
}
void touch_port_render_calibration(uint16_t *fb, int stride) {
    if (s_cal_target < 0) return;
    for (int y = render_tile_top(); y < render_tile_bottom(); y++) memset(fb + (y - render_tile_top()) * stride, 0, TANK_W * sizeof *fb);
    render_text(fb, stride, 70, 146, 2, 0xffffff, "HOLD CROSS THEN RELEASE");
    render_text(fb, stride, 105, 220, 2, 0x80c080, "5 TARGETS - AUTO SAVE");
    int x = (int)(s_cal_targets[s_cal_target][0] * TANK_W / 240), y = (int)(s_cal_targets[s_cal_target][1] * TANK_H / 180);
    for (int d = -8; d <= 8; d++) {
        if (y >= render_tile_top() && y < render_tile_bottom())
            fb[(y - render_tile_top()) * stride + x + d] = 0xffff;
        if (y + d >= render_tile_top() && y + d < render_tile_bottom())
            fb[(y + d - render_tile_top()) * stride + x] = 0xffff;
    }
}
void touch_port_set_inverted(bool inverted) { s_inverted = inverted; }

static uint16_t xpt2046_sample(uint8_t command) {
    uint16_t value = 0;
    gpio_set_level(CYD_PIN_TP_SCLK, 0);
    gpio_set_level(CYD_PIN_TP_CS, 0);
    for (int bit = 7; bit >= 0; bit--) {
        gpio_set_level(CYD_PIN_TP_SCLK, 0);
        gpio_set_level(CYD_PIN_TP_MOSI, (command >> bit) & 1);
        esp_rom_delay_us(1);
        gpio_set_level(CYD_PIN_TP_SCLK, 1);
        esp_rom_delay_us(1);
    }
    for (int bit = 0; bit < 16; bit++) {
        gpio_set_level(CYD_PIN_TP_SCLK, 0);
        esp_rom_delay_us(1);
        value = (uint16_t)((value << 1) | gpio_get_level(CYD_PIN_TP_MISO));
        gpio_set_level(CYD_PIN_TP_SCLK, 1);
        esp_rom_delay_us(1);
    }
    gpio_set_level(CYD_PIN_TP_SCLK, 0);
    gpio_set_level(CYD_PIN_TP_CS, 1);
    return (value >> 3) & 0x0FFF;
}

static uint16_t xpt2046_median(uint8_t command) {
    /* The first conversion after changing channels settles the panel ADC. */
    xpt2046_sample(command);
    uint16_t a = xpt2046_sample(command);
    uint16_t b = xpt2046_sample(command);
    uint16_t c = xpt2046_sample(command);
    if (a > b) { uint16_t t = a; a = b; b = t; }
    if (b > c) { uint16_t t = b; b = c; c = t; }
    return a > b ? a : b;
}

static int xpt2046_scale(uint16_t value, int minimum, int maximum, int extent) {
    if (value < minimum) value = minimum;
    if (value > maximum) value = maximum;
    return (value - minimum) * (extent - 1) / (maximum - minimum);
}

static bool xpt2046_init(void) {
    gpio_config_t outputs = {
        .pin_bit_mask = (1ULL << CYD_PIN_TP_SCLK) | (1ULL << CYD_PIN_TP_MOSI) | (1ULL << CYD_PIN_TP_CS),
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config_t inputs = {
        .pin_bit_mask = (1ULL << CYD_PIN_TP_MISO) | (1ULL << CYD_PIN_TP_IRQ),
        .mode = GPIO_MODE_INPUT,
    };
    if (gpio_config(&outputs) != ESP_OK || gpio_config(&inputs) != ESP_OK) return false;
    gpio_set_level(CYD_PIN_TP_CS, 1);
    gpio_set_level(CYD_PIN_TP_SCLK, 0);
    gpio_set_level(CYD_PIN_TP_MOSI, 0);
    s_xpt_ready = true;
    ESP_LOGI(TAG, "XPT2046 ready (CYD)");
    return true;
}

static bool xpt2046_read(uint16_t *x, uint16_t *y) {
    if (!s_xpt_ready) return false;
    bool irq_down = gpio_get_level(CYD_PIN_TP_IRQ) == 0;
    uint16_t raw_x = xpt2046_median(0x91);
    uint16_t raw_y = xpt2046_median(0xD1);
    uint16_t raw_z1 = xpt2046_median(0xB1);
    uint16_t raw_z2 = xpt2046_median(0xC1);
    /* PD=00 powers down between polls and restores PENIRQ. */
    xpt2046_sample(0xD0);
    int pressure = (int)raw_z1 + 4095 - (int)raw_z2;
    bool touched = pressure >= (s_xpt_down ? 100 : 150);
    if (!touched) {
        if (s_xpt_down) ESP_LOGI(TAG, "XPT2046 release pressure=%d", pressure);
        else if (esp_timer_get_time() - s_xpt_last_idle_log > 2000000) {
            ESP_LOGI(TAG, "XPT2046 idle IRQ=%d raw %u,%u pressure=%d", gpio_get_level(CYD_PIN_TP_IRQ), (unsigned)raw_x, (unsigned)raw_y, pressure);
            s_xpt_last_idle_log = esp_timer_get_time();
        }
        s_xpt_down = false;
        return false;
    }
#if CYD_TOUCH_SWAP_XY
    int px = xpt2046_scale(raw_y, CYD_TOUCH_Y_MIN, CYD_TOUCH_Y_MAX, 240);
    int py = xpt2046_scale(raw_x, CYD_TOUCH_X_MIN, CYD_TOUCH_X_MAX, 180);
#else
    int px = xpt2046_scale(raw_x, CYD_TOUCH_X_MIN, CYD_TOUCH_X_MAX, 240);
    int py = xpt2046_scale(raw_y, CYD_TOUCH_Y_MIN, CYD_TOUCH_Y_MAX, 180);
#endif
#if CYD_TOUCH_MIRROR_X
    px = 239 - px;
#endif
#if CYD_TOUCH_MIRROR_Y
    py = 179 - py;
#endif
    *x = (uint16_t)px;
    *y = (uint16_t)py;
    int64_t now = esp_timer_get_time();
    if (!s_xpt_down) ESP_LOGI(TAG, "XPT2046 press IRQ=%d raw %u,%u mapped %u,%u pressure=%d", irq_down, (unsigned)raw_x, (unsigned)raw_y, (unsigned)px, (unsigned)py, pressure);
    s_xpt_down = true;
    s_seen_us = now;
    return true;
}

static bool panel_read(uint16_t *x, uint16_t *y) { return xpt2046_read(x, y); }
bool touch_port_init(void) { cyd_cal_default(); cyd_cal_load(); return xpt2046_init(); }

static void cal_point(float rx, float ry, float *tx, float *ty);
/* an upright raw report -> the finger's point on the picture as shown: calibrated, then turned with the picture */
static void cal_view(float rx, float ry, float *tx, float *ty) {
    cal_point(rx, ry, tx, ty);
    if (s_inverted) { *tx = TANK_W - 1 - *tx; *ty = TANK_H - 1 - *ty; }
}
static void cal_point(float rx, float ry, float *tx, float *ty) {                 /* a raw tank-space report -> where the finger is */
    float cx = s_calibration.x[0] * rx + s_calibration.x[1] * ry + s_calibration.x[2];
    ry = s_calibration.y[0] * rx + s_calibration.y[1] * ry + s_calibration.y[2];
    rx = cx;
    ry -= s_inverted ? -s_bias_y : s_bias_y;
    if (rx < 0) rx = 0;
    if (rx > 239) rx = 239;
    if (ry < 0) ry = 0;
    if (ry > 179) ry = 179;
    *tx = rx * (TANK_W - 1) / 239; *ty = ry * (TANK_H - 1) / 179;
}
/* the panel's raw reach, as seen since boot (director `touch`): the stroke's
 * reach below takes it for 0..max on both axes - scrub the four walls, then ask */
static int s_raw_x0 = 240, s_raw_x1 = -1, s_raw_y0 = 180, s_raw_y1 = -1;
void touch_port_raw_seen(int *x0, int *x1, int *y0, int *y1) { *x0 = s_raw_x0; *x1 = s_raw_x1; *y0 = s_raw_y0; *y1 = s_raw_y1; }
static void map_touch(uint16_t px, uint16_t py, float *tx, float *ty) {
    int rx = px, ry = py;
    if (rx < s_raw_x0) s_raw_x0 = rx;
    if (rx > s_raw_x1) s_raw_x1 = rx;
    if (ry < s_raw_y0) s_raw_y0 = ry;
    if (ry > s_raw_y1) s_raw_y1 = ry;
    cal_view((float)rx, (float)ry, tx, ty);
}
/* The STROKE's reach to the glass (2026-10-01). The panel's report runs out
 * before the glass does: its raw 0..max lands, through the map above, on x
 * 29..420 and y 11..333 - a finger on the outer 29 px (the bottom 35) reads
 * as the limit, never beyond. Taps want exactly that map (the crosshairs),
 * but a scrub could no longer reach the film in the edge cells and a slash
 * stopped short of a bed's outer frond and of the floor (Strato: "difficult
 * if not impossible to scrub algae which sits too close to the glass edge",
 * "the farthest left and most far right seagrass fronds cannot be clipped").
 * Before the calibration the stretched report overshot to the edge on its
 * own. So the stream to tank_touch_drag - and nothing else - is stretched
 * over the last EDGE_BAND_* px inside each limit so the limit lands on the
 * glass (EDGE_SNAP_* short of it already does: a pad at the bezel does not
 * drive the report to its very end). A stroke keeps the finger's own
 * travel for its first EDGE_RAMP0_PX and eases into the full stretch by
 * EDGE_RAMP1_PX: a tap's roll is not magnified into a wipe or a snip (tank.c
 * WIPE_ENGAGE_PX / SLASH_PX), a real stroke runs to the glass. It LANDS
 * fully stretched in y (a sweep along the floor is on the floor from its
 * first frame, and stays level) but only EDGE_LAND_X of the way in x: a
 * finger put down on a bed's outer frond, 24 px from the wall, reads as the
 * panel's limit - landed at the glass it would be a whole pad's width off
 * the spine and arm no scissors (tank.c SLASH_START_SIDE_PX); landed half
 * way it is beside the spine with the frond inside its reach.
 * One exception to the ramp, x only: travel TOWARD a side wall is stretched
 * from its first px. A swipe at a wall frond is a short thing in the report -
 * the pad runs into the bezel and its center stops 15..25 px short of the
 * limit - so it never travelled far enough to open the ramp, or to be a
 * slash at all (Strato's log, 2026-10-01: starts at x 43..71, lifts at
 * 39..53, 4..20 px apart). The price: within the band, a tap that rolls 8 px
 * toward the wall on a tall frond can snip it. */
#define EDGE_BAND_X   44.0f
#define EDGE_SNAP_X    8.0f
#define EDGE_BAND_Y   44.0f
#define EDGE_SNAP_Y    8.0f
#define EDGE_RAMP0_PX 18.0f
#define EDGE_RAMP1_PX 42.0f
#define EDGE_LAND_X    0.5f
static float s_sx, s_sy, s_reach;                 /* this stroke: its landing point, how far the stretch is in (0..1) */
static float s_ex0;                               /* ... and its landing x fully stretched */
static bool  s_log;                               /* director `touch log on`: one line per press, at its release */
static float s_dx0, s_dx1, s_dlx, s_dly; static int s_frames, s_raw_px, s_raw_py;   /* ... what it reports */
void touch_port_set_log(bool on) { s_log = on; }
static float edge_stretch1(float v, float lo, float hi, float max, float band, float snap) {
    if (lo > 0 && v < lo + band) {
        float e = v - lo - snap;
        return e <= 0 ? 0 : e * (lo + band) / (band - snap);
    }
    if (hi < max && v > hi - band) {
        float e = hi - v - snap;
        return e <= 0 ? max : max - e * (max - hi + band) / (band - snap);
    }
    return v;
}
static void edge_stretch(float x, float y, float *ox, float *oy) {
    float x0, y0, x1, y1;                         /* the panel's reach: its raw extremes through the same map (the bias is live) */
    cal_view(0, 0, &x0, &y0); cal_view(239, 179, &x1, &y1);
    if (x0 > x1) { float k = x0; x0 = x1; x1 = k; }   /* turned: the far corner is the near one */
    if (y0 > y1) { float k = y0; y0 = y1; y1 = k; }
    *ox = edge_stretch1(x, x0, x1, TANK_W - 1, EDGE_BAND_X, EDGE_SNAP_X); *oy = edge_stretch1(y, y0, y1, TANK_H - 1, EDGE_BAND_Y, EDGE_SNAP_Y);
}
/* the calibrated point of a touched frame -> the stroke's point for tank_touch_drag */
static void stroke_point(bool landed, float tx, float ty, float *dx, float *dy) {
    float ex, ey; edge_stretch(tx, ty, &ex, &ey);
    if (landed) { s_sx = tx + (ex - tx) * EDGE_LAND_X; s_sy = ey; s_ex0 = ex; s_reach = 0; }
    float d = hypotf(tx - s_px, ty - s_py);
    float r = (d - EDGE_RAMP0_PX) / (EDGE_RAMP1_PX - EDGE_RAMP0_PX);
    if (r > 1) r = 1;
    if (r > s_reach) s_reach = r;                 /* latched: scrubbing back past the landing point stays stretched */
    float ux = s_sx + tx - s_px;                  /* the finger's own travel from its landing ... */
    if ((ex - tx) * (tx - s_px) > 0) ux = s_sx + ex - s_ex0;   /* ... but stretched at once on its way TO a wall (below) */
    *dx = ux * (1 - s_reach) + ex * s_reach;
    *dy = (s_sy + ty - s_py) * (1 - s_reach) + ey * s_reach;
    if (*dx < 0) *dx = 0;
    if (*dx > TANK_W - 1) *dx = TANK_W - 1;
    if (*dy < 0) *dy = 0;
    if (*dy > TANK_H - 1) *dy = TANK_H - 1;
}
static bool s_upd; static int s_upd_act;                 /* the UPDATES page (2026-09-30) */
bool touch_port_read_raw(float *x, float *y) {
    uint16_t px[1], py[1];
    if (!panel_read(px, py)) return false;
    map_touch(px[0], py[0], x, y);
    return true;
}

/* call every frame from the tank task */
static void settings_touch(tank_t *t, float x, float y, bool down) {
    int v = 0, r = render_settings_touch(t, x, y, down, &v);
    if (r) ESP_LOGI(TAG, "settings action %d value %d", r, v);
    if (r == SET_TAP_CLOSE) { s_set = false; s_ms = true; s_life = s_from_home && !s_child_from_keeper; s_back = true; }
    else if (r == SET_TAP_RECOVERED) {
        touch_port_dismiss(); setup_cancel(t); notice_sync(t); render_fb_primed(NULL, 0); s_back = true;
        if (progression_setup_pending()) setup_begin(t);
    }
    else if (r == SET_TAP_CLOCK_SHOW) {
        s_set = s_ms = s_shop = s_upd = false; s_sel = -1; s_back = true;
        t->ui_cover = false; t->clock_requested = true;
    }
    else if (r == SET_TAP_TOUCH) touch_port_calibrate();
    else if (r == SET_TAP_WIPE) touch_port_confirm_open();
    else if (r == SET_TAP_UPDATES) { s_set = false; s_upd = true; s_back = true; sd_port_update_reset(); ESP_LOGI(TAG, "SD updates page up"); }
    else if (r == SET_TAP_BRIGHT || r == SET_TAP_BRIGHT_PREVIEW || r == SET_TAP_LIGHT || r == SET_TAP_IDLE || r == SET_TAP_FEED
             || r == SET_TAP_BACKUP || r == SET_TAP_RESTORE || r == SET_TAP_SAND || r == SET_TAP_WATER || r == SET_TAP_WIFI
             || r == SET_TAP_ROOM || r == SET_TAP_LED || r == SET_TAP_AUTO_BACKUP || r == SET_TAP_PHOTO || r == SET_TAP_DETAIL) { s_set_what = r; s_set_val = v; }
}
void touch_port_life_tap(tank_t *t, float x, float y) {
    int action = render_life_tap(t, x, y);
    if (action == HOME_CLOSE) { s_ms = s_life = s_from_home = false; s_sel = -1; }
    else if (action) {
        s_life = false; s_from_home = true; s_child_from_keeper = false; s_sel = -1;
        switch (home_open(t, action)) {
        case HOME_DEST_BANK: s_bank = true; break;
        case HOME_DEST_JOURNAL: s_journal = true; break;
        case HOME_DEST_GAMES: s_games = true; break;
        case HOME_DEST_SETTINGS: s_ms = false; s_set = true; break;
        case HOME_DEST_SHOP:
        case HOME_DEST_TANKS:
            s_ms = false;
            s_shop = true;
            s_aquariums = s_home_tanks = action == HOME_TANKS;
            break;
        case HOME_DEST_KEEPER:
        case HOME_DEST_NONE:
            break;
        }
    }
}
bool touch_port_settings_tap(tank_t *t, float x, float y) {
    if (!s_set || s_cf || setup_active() || !isfinite(x) || !isfinite(y) ||
        x < 0 || y < 0 || x >= TANK_W || y >= TANK_H) return false;
    settings_touch(t, x, y, true);
    settings_touch(t, x, y, false);
    return true;
}
void touch_port_poll(tank_t *t) {
    static bool clock_wake;
    int64_t now = esp_timer_get_time();
    if (s_cf && now - s_cf_us > CONFIRM_TIMEOUT_US) touch_port_confirm_answer(-1);   /* nobody answered: keep the tank */
    if (!s_xpt_ready) return;
    uint16_t x[1] = {0}, y[1] = {0};
    bool touched = panel_read(x, y);
    if (touch_port_calibrating()) { cyd_cal_poll(touched, x[0], y[0]); return; }
    if (clock_wake || (touched && !s_down && render_clock_active(t))) {
        clock_wake = touched; tank_handled(t); s_down = touched; return;
    }
    /* portrait panel (px,py) -> landscape tank (tx,ty): tx = TANK_W-1-py, ty = px;
     * flipped screen: mirror both, so downstream gestures live in displayed space */
    float tx = s_lx, ty = s_ly;
    if (touched) map_touch(x[0], y[0], &tx, &ty);           /* calibrated (above); the release keeps the last position */
    if (touched && !s_down) {
        s_press_us = now; s_px = tx; s_py = ty;
        /* snapshot the school: the user aims at where a fish WAS - by release
           a darting fish has moved and the finger hid it the whole time */
        for (int i = 0; i < t->n_fish && i < N_FISH_MAX; i++) { s_fx[i] = t->fish[i].x; s_fy[i] = t->fish[i].y; }
    }
    bool su = setup_active();                                /* before the touch: BEGIN's release is not a tank tap */
    if (s_set && !s_cf && !su) {
        settings_touch(t, tx, ty, touched);
        if (touch_port_calibrating()) return;
    }
    if (s_upd && !s_cf && !su) {                             /* the UPDATES page: CHECK (main restarts), FORGET, CLOSE */
        int r = updates_page_touch(tx, ty, touched);
        if (r == UPD_TAP_CLOSE) { s_upd = false; s_set = true; s_back = true; ESP_LOGI(TAG, "updates page: CLOSE -> settings"); }
        else if (r == UPD_TAP_CHECK || r == UPD_TAP_WIFI) { s_upd_act = r; ESP_LOGI(TAG, "updates page: %s", r == UPD_TAP_WIFI ? "CHECK GITHUB VIA WI-FI" : "CHECK SD CARD"); }
        else if (r == UPD_TAP_FORGET) ESP_LOGI(TAG, "updates page: network forgotten");
    }
    if (su && !s_cf) {
        bool birth = setup_is_birth(), rename = setup_is_rename(); int who = setup_fish(), place = setup_item();
        setup_touch(t, tx, ty, touched);                     /* taps and the letter wheel, classified in setup.c */
        if (!setup_active()) {
            if (rename) ESP_LOGI(TAG, "rename closed: the fish is %s", who >= 0 && who < t->n_fish ? t->fish[who].name : "?");
            else if (birth) ESP_LOGI(TAG, "birth flow done: %s named and saved", who >= 0 && who < t->n_fish ? t->fish[who].name : "?");
            else if (place >= 0) ESP_LOGI(TAG, "placed: %s at x %.0f, %s layer, saved", SD_ITEMS[place].name, tank_decor_x(t, place),
                                          tank_decor_z(t, place) == DECOR_Z_BACK ? "BEHIND" : tank_decor_z(t, place) == DECOR_Z_FRONT ? "IN FRONT" : "AMONG");
            else ESP_LOGI(TAG, "setup done: %s + %s", t->fish[0].name, t->fish[1].name);
        }
    }
    { int rf = setup_take_renamed();                         /* a rename closed (2026-10-01): back to the milestones page, the fish's card up */
      if (rf >= 0) { s_ms = true; s_set = false; s_shop = false; s_sel = -1; render_milestones_show_fish(t, rf); } }
    bool modal = s_ms || s_set || s_shop || s_cf || su || s_upd;   /* a page or a prompt owns the glass */
    static bool card_next;
    static int card_color = -1;
    if (touched && !s_down) {
        card_next = !modal && render_stats_next_hit(t, s_sel, s_px, s_py);
        card_color = modal ? -1 : render_creature_colors_hit(t, s_sel, s_px, s_py);
    }
    bool card_control = card_next || card_color >= 0;
    if (touched) {                                           /* stroke = wipe/slash, reaching the glass (stroke_point above) */
        float dx, dy; stroke_point(!s_down, tx, ty, &dx, &dy);
        if (!s_down) { s_dx0 = s_dx1 = dx; s_frames = 0; s_raw_px = x[0]; s_raw_py = y[0]; }
        if (dx < s_dx0) s_dx0 = dx;
        if (dx > s_dx1) s_dx1 = dx;
        s_dlx = dx; s_dly = dy; s_frames++;
        s_lx = tx; s_ly = ty; if (!modal && !card_control) tank_touch_drag(t, dx, dy);
    }
    /* tap-and-hold on a decoration (2026-09-24, Strato: "right now it's like 5
       taps to get back to the edit"): a still finger 700 ms on an owned
       piece opens its placement page (MOVE / DEPTH / REMOVE) right there */
    if (touched && !modal && !card_control && !s_held_page && t->tool == TOOL_HAND && now - s_press_us > 700000 && fabsf(tx - s_px) < 24 && fabsf(ty - s_py) < 24) {
        if (setup_begin_place_at(t, s_px, s_py)) {
            s_held_page = true; s_sel = -1;
            ESP_LOGI(TAG, "held on the %s: placement page up", SD_ITEMS[setup_item()].name);
        }
    }
    if (touched && !modal && !card_control && !s_held_page && now - s_press_us > 300000 && fabsf(ty - s_py) < 30) tank_touch_hold(t, tx, ty);
    if (!touched && s_down) {
        /* release: classify with the LAST touched position (the old code fell
           back to the PRESS position here, so dx/dy were always 0 - every
           quick swipe read as a tap and the drag-feed could never fire) */
        float dx = s_lx - s_px, dy = s_ly - s_py;
        int64_t held_us = s_seen_us - s_press_us;      /* the finger's own time on the glass: the lift's bridge is not the keeper's */
        if (s_log) ESP_LOGI(TAG, "stroke: raw %d,%d | press %.0f,%.0f -> lift %.0f,%.0f | lands %.0f,%.0f, drag x %.0f..%.0f, ends %.0f,%.0f | %d frames %d ms%s | scissors %s%s%s, travel %.0f",
                            s_raw_px, s_raw_py, s_px, s_py, s_lx, s_ly, s_sx, s_sy, s_dx0, s_dx1, s_dlx, s_dly, s_frames, (int)(held_us / 1000),
                            modal ? " (a page had the glass)" : "", t->slash_armed ? "ARMED" : "not armed", t->slash_engaged ? " ENGAGED" : "", t->slash_cut ? " CUT" : "", t->drag_dist);
        if (s_cf) {                     /* the prompt owns the glass: a press AND release on the
                                           same button answers it, nothing else counts - not
                                           even the tap that opened it (it began before) */
            int h = s_press_us > s_cf_us ? render_confirm_hit(s_px, s_py) : 0;
            if (h && h == render_confirm_hit(s_lx, s_ly)) touch_port_confirm_answer(h);
            goto released;
        }
        if (su) {                       /* the setup had the glass (setup_touch above); just the log:
                                           where the finger landed vs what it hit, in case this panel
                                           reports fingers offset from where they feel */
            ESP_LOGI(TAG, "setup touch press %.0f,%.0f release %.0f,%.0f -> %s", s_px, s_py, s_lx, s_ly,
                     setup_hit_name(setup_active() ? setup_hit(s_px, s_py) : 0));
            s_sel = -1; goto released;
        }
        if (held_us < 350000 && dx * dx + dy * dy < 24 * 24) {
            if (notice_current()) {                                 /* (the lights-out notice lets its tap through: notice.h) */
                bool took = notice_dismiss(); ESP_LOGI(TAG, "tap closed the announcement"); if (took) goto released; }
            if (s_set || s_upd || s_back) { s_back = false; goto released; }   /* the settings / updates page had the glass (their touch calls above) */
            if (s_shop) {                                           /* the shop: a row's modal, UNLOCK, HOW TO EARN, CLOSE */
                if (s_aquariums) {
                    int result = render_aquariums_tap(t, s_px, s_py);
                    if (result == 1) {
                        s_aquariums = false;
                        if (s_home_tanks) { s_shop = false; s_ms = s_life = true; s_home_tanks = false; }
                    }
                    else if (result == 2) {
                        touch_port_dismiss(); setup_cancel(t); notice_sync(t); render_fb_primed(NULL, 0);
                        if (progression_setup_pending()) setup_begin(t);
                    }
                    goto released;
                }
                int r = render_shop_tap(t, s_px, s_py);
                ESP_LOGI(TAG, "shop tap at %.0f,%.0f -> %s", s_px, s_py, r == SHOP_TAP_CLOSE ? "CLOSE" : r >= SHOP_TAP_INVENTORY ? "INVENTORY" : r >= SHOP_TAP_FRY ? "BUY A FRY" : r >= SHOP_TAP_SELL ? "SELL" : r >= SHOP_TAP_MOVE ? "MOVE" : r >= SHOP_TAP_BUY ? "UNLOCK" : r == SHOP_TAP_KEPT ? "modal" : "nothing");
                if (r == SHOP_TAP_TANKS) { s_aquariums = true; render_aquariums_open(t); }
                else if (r == SHOP_TAP_CLOSE) { s_shop = false; render_shop_leave(); s_ms = true; s_life = s_from_home && !s_child_from_keeper; }   /* back to the milestones page (2026-09-16) */
                else if (r >= SHOP_TAP_BUY) s_shop_act = r;   /* main.c buys (and plays the cue) or opens the placement page */
                goto released;
            }
            if (s_ms) {
                if (s_life) {
                    touch_port_life_tap(t, s_px, s_py);
                    goto released;
                }
                if (s_games) {
                    if (render_games_tap(t, s_px, s_py)) { s_games = false; s_life = s_from_home && !s_child_from_keeper; }
                    goto released;
                }
                if (s_bank) {
                    if (render_bank_tap(t, s_px, s_py)) { s_bank = false; s_life = s_from_home && !s_child_from_keeper; }
                    goto released;
                }
                if (s_journal) {
                    if (render_journal_tap(t, s_px, s_py)) { s_journal = false; s_life = s_from_home && !s_child_from_keeper; }
                    goto released;
                }
                int r = render_milestones_tap(t, s_px, s_py);
                if (r == MS_TAP_CLOSE && s_from_home) { s_life = true; goto released; }
                if (r == MS_TAP_BANK || r == MS_TAP_JOURNAL || r == MS_TAP_GAMES ||
                    r == MS_TAP_SETTINGS || r == MS_TAP_SHOP) s_child_from_keeper = true;
                if (r == MS_TAP_SETTINGS) render_settings_open();
                if (r == MS_TAP_BANK) { s_bank = true; s_sel = -1; render_bank_open(); goto released; }
                if (r == MS_TAP_JOURNAL) { s_journal = true; render_journal_open(); goto released; }     /* CLOSE / SETTINGS / the sand dollar, detail modal, nothing */
                if (r == MS_TAP_GAMES) { s_games = true; s_sel = -1; render_games_open(); tank_handled(t); goto released; }
                if (r == MS_TAP_LIFE) { s_life = true; s_sel = -1; render_life_open(); tank_handled(t); goto released; }
                ESP_LOGI(TAG, "page tap at %.0f,%.0f (release %.0f,%.0f) -> %s", s_px, s_py, s_lx, s_ly,
                         r >= MS_TAP_CUSTOM ? "CUSTOMIZE" : r >= MS_TAP_SELL ? "SELL (a fish's card)" : r >= MS_TAP_RENAME ? "RENAME (a fish's card)" :
                         r == MS_TAP_CLOSE ? "CLOSE" : r == MS_TAP_SETTINGS ? "SETTINGS" : r == MS_TAP_SHOP ? "SHOP" : r == MS_TAP_KEPT ? "detail" : "nothing");
                if (r >= MS_TAP_CUSTOM) {
                    s_ms = false; s_sel = -1; progression_ack_milestones(t); render_milestones_leave();
                    setup_begin_customize(t, r - MS_TAP_CUSTOM); goto released;
                }
                if (r >= MS_TAP_SELL) {                             /* a fish's card: SELL, confirmed - the page stays, a row shorter */
                    int fi = r - MS_TAP_SELL, worth = progression_fish_value(t, fi);
                    char nm[FISH_NAME_MAX + 1]; snprintf(nm, sizeof nm, "%s", fi < t->n_fish ? t->fish[fi].name : "?");
                    if (progression_sell_fish(t, fi)) { s_sel = -1; ESP_LOGI(TAG, "%s sold for %d: %d fish left, balance %d", nm, worth, t->n_fish, (int)t->sd_balance); }
                    goto released;
                }
                if (r >= MS_TAP_RENAME) {                           /* ... RENAME: the letter wheel over the live tank */
                    s_ms = false; s_sel = -1; progression_ack_milestones(t); render_milestones_leave();
                    setup_begin_rename(t, r - MS_TAP_RENAME);
                    goto released;
                }
                if (r != MS_TAP_CLOSE && r != MS_TAP_SETTINGS && r != MS_TAP_SHOP) goto released;   /* only a button leaves the page */
                s_ms = false; s_sel = -1; s_set = r == MS_TAP_SETTINGS; s_shop = r == MS_TAP_SHOP;
                progression_ack_milestones(t); render_milestones_leave();   /* everything shown is now "seen" */
                goto released;
            }
            if (t->tool != TOOL_HAND) {                      /* a tool in hand owns the glass (2026-10-04): only its chip's DONE answers a tap */
                s_sel = -1;
                if (render_tool_chip_hit(t, s_px, s_py)) { tank_set_tool(t, TOOL_HAND); ESP_LOGI(TAG, "tool chip tapped at %.0f,%.0f: back to bare hands", s_px, s_py); }
                else ESP_LOGI(TAG, "tap at %.0f,%.0f ignored: a tool is in hand (DONE first)", s_px, s_py);
                goto released;
            }
            if (card_next) {
                if (render_stats_next_hit(t, s_sel, s_lx, s_ly)) {
                    s_sel = render_stats_next(t, s_sel); s_sel_us = now;
                }
                goto released;
            }
            if (card_color >= 0) {
                if (render_creature_colors_hit(t, s_sel, s_lx, s_ly) == card_color) {
                    render_life_creature_open(card_color);
                    s_ms = s_life = true; s_from_home = false; s_sel = -1;
                }
                goto released;
            }
            if (s_sel >= 0 && s_sel < t->n_fish && render_tools_hit(s_px, s_py) >= 0) {   /* the toolbox under the card (2026-10-01):
                                                                                     pick a tool up, or put the one in hand back */
                int tl = render_tools_hit(s_px, s_py);
                tank_set_tool(t, t->tool == tl ? TOOL_HAND : tl); s_sel = -1;
                ESP_LOGI(TAG, "toolbox tap at %.0f,%.0f -> %s", s_px, s_py, t->tool == TOOL_SPONGE ? "SPONGE" : t->tool == TOOL_SCISSORS ? "SCISSORS" : "bare hands");
                goto released;
            }
            if (s_sel >= 0 && s_sel < t->n_fish && render_lock_hit(s_px, s_py)) {   /* the LOCK beside it (0.3.5): bare fingers
                                                                                 wipe and cut, or only the tools do; the card stays */
                tank_lock_tools(t, !t->tools_locked); progression_settings_changed(); s_sel_us = now;
                ESP_LOGI(TAG, "lock tap at %.0f,%.0f -> %s", s_px, s_py, t->tools_locked ? "LOCKED: only the tools wipe and cut" : "unlocked: bare fingers wipe and cut");
                goto released;
            }
            if ((s_sel < 0 || s_sel >= t->n_fish) && render_tool_chip_hit(t, s_px, s_py)) {   /* the tool chip's DONE */
                tank_set_tool(t, TOOL_HAND);
                ESP_LOGI(TAG, "tool chip tapped at %.0f,%.0f: back to bare hands", s_px, s_py);
                goto released;
            }
            if (s_sel >= 0 && s_sel < t->n_fish && RENDER_CARD_HIT(s_px, s_py)) {   /* a tap ON the card (or the slop
                ESP_LOGI(TAG, "card tap at %.0f,%.0f -> milestones", s_px, s_py);         under its MORE button) = milestones page */
                s_ms = s_life = true; s_from_home = false; render_life_open(); goto released;
            }
            /* fish first; only an empty tap reaches the water. 38 px radius
               (a fingertip on this 322 ppi panel covers ~60 px) against BOTH
               the press-time snapshot and the current position - whichever is
               closer - so a fish that moved mid-tap still registers. */
            int best = -1; float bd = 38 * 38;
            for (int i = 0; i < t->n_fish; i++) {
                float ax = s_fx[i] - s_px, ay = s_fy[i] - s_py;
                float bx = t->fish[i].x - s_px, by = t->fish[i].y - s_py;
                float d2a = ax * ax + ay * ay, d2b = bx * bx + by * by;
                float d2 = d2a < d2b ? d2a : d2b;
                if (d2 < bd) { bd = d2; best = i; }
            }
            int helper = render_creature_card_hit(t, s_px, s_py);
            if (best >= 0) { s_sel = (best == s_sel) ? -1 : best; s_sel_us = now; }
            else if (helper >= 0) {
                if (helper == RENDER_CARD_SHRIMP) {
                    int taps = tank_shrimp_tap(t, s_px, s_py);
                    if (taps == 1) { s_sel = s_sel == helper ? -1 : helper; s_sel_us = now; }
                    else if (taps >= 3) s_sel = -1;
                } else { s_sel = s_sel == helper ? -1 : helper; s_sel_us = now; }
                ESP_LOGI(TAG, "creature %d tapped: card %s", helper, s_sel >= 0 ? "up" : "down");
            }
            else if (s_sel >= 0) s_sel = -1;   /* card up: a tap on empty glass just
                                                  dismisses it - it is NOT a tank tap
                                                  (no feed, no light-toggle burst) */
            else tank_touch_tap(t, s_px, s_py);
        }
        else if (s_ms && !s_journal && !s_bank && !s_games && !s_life && !s_back && fabsf(dx) >= 40 && fabsf(dx) > 2 * fabsf(dy)) {   /* sideways along the TANK row: its page (2026-09-30) */
            if (render_milestones_swipe(t, s_px, s_py, dx)) ESP_LOGI(TAG, "page swipe %.0f px on the tank row", dx);
        }
        else if (!card_control && !s_ms && t->tool == TOOL_HAND && s_py - tank_glass_top(s_px) < 60 && dy >= 40) tank_feed(t, s_lx, 3);  /* drag down from the top = feed (not with a tool in hand) */
    }
released:
    if (!touched) s_held_page = false;
    s_down = touched;
    if (s_sel >= t->n_fish && s_sel != RENDER_CARD_SNAIL && s_sel != RENDER_CARD_SHRIMP && s_sel != RENDER_CARD_URCHIN && s_sel != RENDER_CARD_SEAHORSE && s_sel != RENDER_CARD_MUSSEL) s_sel = -1;   /* fresh tank / save load */
    if (s_sel >= 0 && now - s_sel_us > 10 * 1000000) s_sel = -1; /* auto-dismiss */
}

int touch_port_selected(void) { return s_sel; }
bool touch_port_milestones(void) { return s_ms; }
bool touch_port_journal(void) { return s_ms && s_journal; }
bool touch_port_bank(void) { return s_ms && s_bank; }
bool touch_port_games(void) { return s_ms && s_games; }
bool touch_port_life(void) { return s_ms && s_life; }
void touch_port_show_life(void) { touch_port_dismiss(); s_ms = true; s_life = true; render_life_open(); }
void touch_port_show_games(void) { touch_port_dismiss(); s_ms = true; s_games = true; render_games_open(); }
void touch_port_show_bank(void) { touch_port_dismiss(); s_ms = true; s_bank = true; render_bank_open(); }
void touch_port_show_journal(void) { touch_port_dismiss(); s_ms = true; s_journal = true; render_journal_open(); }
void touch_port_show_milestones(bool on) { if (s_ms && !on) render_milestones_leave(); s_ms = on; s_life = s_games = s_journal = s_bank = false; }
void touch_port_dismiss(void) { s_from_home = s_home_tanks = s_child_from_keeper = false; s_aquariums = false; s_life = s_games = s_journal = s_bank = false; s_sel = -1; if (s_ms) render_milestones_leave(); if (s_shop) render_shop_leave(); s_ms = false; s_set = false; s_shop = false; s_upd = false; }
bool touch_port_updates(void) { return s_upd; }
void touch_port_show_updates(bool on) { s_life = s_games = s_journal = s_bank = false; s_upd = on; if (on) { s_ms = false; s_set = false; s_shop = false; s_sel = -1; } }
int  touch_port_take_update(void) { int r = s_upd_act; s_upd_act = 0; return r; }

/* ---- reset confirm prompt ---- */
void touch_port_confirm_open(void) {
    s_life = s_journal = s_bank = false;
    s_cf = true; s_cf_us = esp_timer_get_time(); s_cf_ans = 0;
    s_life = s_games = false; s_sel = -1; s_ms = false; s_set = false; s_shop = false; s_upd = false; render_shop_leave();   /* it replaces the card / the pages */
    ESP_LOGI(TAG, "reset prompt up (YES / NO on the glass; NO by itself in %d s)", (int)(CONFIRM_TIMEOUT_US / 1000000));
}
bool touch_port_confirm_answer(int ans) {
    if (!s_cf) return false;
    s_cf = false; s_cf_ans = ans > 0 ? 1 : -1;
    return true;
}
bool  touch_port_confirm_up(void)   { return s_cf; }
float touch_port_confirm_frac(void) {
    if (!s_cf) return 0;
    float f = 1.0f - (esp_timer_get_time() - s_cf_us) / (float)CONFIRM_TIMEOUT_US;
    return f < 0 ? 0 : f;
}
int  touch_port_confirm_take(void)  { int a = s_cf_ans; s_cf_ans = 0; return a; }
bool touch_port_pressed_since(int64_t us) { return s_down && s_press_us > us; }
bool touch_port_settings(void) { return s_set; }
void touch_port_show_settings(bool on) { s_life = s_games = s_journal = s_bank = false; s_set = on; if (on) { s_ms = false; s_sel = -1; } }
int  touch_port_take_setting(int *value) { int w = s_set_what; *value = s_set_val; s_set_what = 0; return w; }
bool touch_port_shop(void) { return s_shop; }
bool touch_port_aquariums(void) { return s_shop && s_aquariums; }
void touch_port_show_aquariums(const tank_t *t) { touch_port_dismiss(); s_shop = s_aquariums = true; render_aquariums_open(t); }
void touch_port_show_shop(bool on) { s_aquariums = false; s_life = s_games = s_journal = s_bank = false; if (s_shop && !on) render_shop_leave(); s_shop = on; if (on) { s_ms = false; s_set = false; s_sel = -1; } }
int  touch_port_take_shop(void) { int r = s_shop_act; s_shop_act = 0; return r; }
