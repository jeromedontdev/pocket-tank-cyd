#ifndef TOUCH_PORT_H
#define TOUCH_PORT_H
#include <stdbool.h>
#include <stdint.h>
#include "tank.h"
bool touch_port_init(void);
void touch_port_poll(tank_t *t);
int  touch_port_selected(void);   /* tapped fish for the stats card, RENDER_CARD_SNAIL for the snail's, -1 = none */
/* milestones page: a tap ON the open stats card (its MORE button, or any of
 * it) flips to it; it has no
 * auto-dismiss - the next tap anywhere closes it (and the card, if still up).
 * touch_port_show_milestones is the director's cue (needs no card). */
bool touch_port_milestones(void);
bool touch_port_journal(void);
void touch_port_show_journal(void);
bool touch_port_bank(void);
bool touch_port_games(void);
bool touch_port_life(void);
void touch_port_show_life(void);
void touch_port_life_tap(tank_t *t, float x, float y);
void touch_port_show_games(void);
void touch_port_show_bank(void);
void touch_port_show_milestones(bool on);
void touch_port_dismiss(void);    /* drop the card and the page: a flow (the birth flow) took the glass */
void touch_port_set_inverted(bool inverted);   /* mirror coords when the screen is flipped */
/* reset confirm prompt (render_confirm_reset): opened by main.c's chord -
 * BOOT held, then a finger lands on the glass - or the director's `reset`.
 * While it is up every other gesture is swallowed; a press AND release on
 * the same button answers it, and it answers NO by itself after 20 s (or
 * when the tank goes to sleep). The answer is one-shot: main.c takes it
 * once per frame and a YES wipes the tank. */
void touch_port_confirm_open(void);
bool touch_port_confirm_answer(int ans);       /* +1 yes / -1 no; false = no prompt up */
bool touch_port_confirm_up(void);
float touch_port_confirm_frac(void);           /* time left before it gives up, 1 -> 0 */
int  touch_port_confirm_take(void);            /* +1 / -1 once, then 0 */
bool touch_port_pressed_since(int64_t us);     /* a finger is down and landed after `us` */
/* the settings page (2026-09-15): opened from the milestones page's SETTINGS
 * button; slider moves yield SET_TAP_BRIGHT_PREVIEW without flash writes;
 * release yields SET_TAP_BRIGHT to persist the final value. CLOSE ends it. */
bool touch_port_settings(void);
void touch_port_show_settings(bool on);
bool touch_port_settings_tap(tank_t *t, float x, float y);
int  touch_port_take_setting(int *value);       /* SET_TAP_* or 0 */
/* the shop page (2026-09-15): up / show it; an UNLOCK or MOVE tapped hands
 * the raw tap code (SHOP_TAP_BUY / SHOP_TAP_MOVE + item; 0 = none) to main
 * once, which buys (progression_buy) or opens the placement page */
bool touch_port_shop(void);
bool touch_port_aquariums(void);
void touch_port_show_aquariums(const tank_t *t);
void touch_port_show_shop(bool on);
int  touch_port_take_shop(void);
/* the UPDATES page (2026-09-30, docs/OTA.md): from the settings page's
 * UPDATES button; its CHECK is handed to main once (UPD_TAP_CHECK), which
 * saves and restarts into update mode; CLOSE returns to the settings page */
bool touch_port_updates(void);
void touch_port_show_updates(bool on);
int  touch_port_take_update(void);              /* UPD_TAP_CHECK once, else 0 */
/* update mode's raw read (no tank, no pages): the finger in tank space, true while down */
bool touch_port_read_raw(float *x, float *y);
void touch_port_calibrate(void);
bool touch_port_calibrating(void);
void touch_port_render_calibration(uint16_t *fb, int stride);
void touch_port_set_bias(int px);              /* finger-landing correction: reported y moves up by px */
int  touch_port_bias(void);
void touch_port_set_log(bool on);              /* one log line per press, at its release (director `touch log on|off`) */
void touch_port_raw_seen(int *x0, int *x1, int *y0, int *y1);   /* the raw report's extremes since boot, in tank space (x1 < x0: no touch yet) */
#endif
