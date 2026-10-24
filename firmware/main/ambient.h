/* ambient.h - the CYD's back RGB LED and room-light sensor (1.3).
 * LED: GPIO 4/16/17, active low, LEDC channels 1-3 on the backlight's timer.
 * LDR: GPIO 34 (ADC1 channel 6); the raw reading rises as the room darkens.
 * Modes live in NVS "tank" ("room", "led", "autobk") beside "bright". */
#ifndef AMBIENT_H
#define AMBIENT_H
#include <stdbool.h>
#include <stdint.h>
#include "tank.h"

enum { ROOM_LIGHT_OFF, ROOM_LIGHT_DIM, ROOM_LIGHT_NIGHT, ROOM_LIGHT_MODES };
enum { BACK_LED_OFF, BACK_LED_GLOW, BACK_LED_ALERTS, BACK_LED_MODES };

void ambient_init(void);                 /* after nvs_flash_init and display_port_init */
void ambient_led(uint8_t r, uint8_t g, uint8_t b);
void ambient_led_override(int ms);       /* `led r g b`: hold a manual colour this long */
void ambient_off_for_sleep(void);
int  ambient_ldr_raw(void);              /* 0..4095, -1 if unavailable */
int  ambient_room_pct(void);             /* smoothed room light 0 (dark) .. 100 (bright) */
bool ambient_room_dark(void);            /* with hysteresis */
int  ambient_room_mode(void);
int  ambient_led_mode(void);
bool ambient_auto_backup(void);
bool ambient_set_room_mode(int mode);
bool ambient_set_led_mode(int mode);
bool ambient_set_auto_backup(bool on);
bool ambient_save_all(void);             /* re-save after a wipe */
int  ambient_brightness_pct(int pct);    /* scales the user level for the room */
bool ambient_force_night(void);          /* DIM+NIGHT and the room is dark */
void ambient_tick(const tank_t *t, float dt);   /* once per frame */
void ambient_calibrate(int dark_raw, int bright_raw);
void ambient_log(void);
#endif
