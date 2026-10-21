#ifndef DISPLAY_PORT_H
#define DISPLAY_PORT_H
#include <stdint.h>
#include <stdbool.h>
bool display_port_init(void);
void display_port_flush(const uint16_t *fb);
void display_port_flush_prof(int64_t *wait_us, int64_t *send_us);
void display_port_sleep(void);
void display_port_wake(void);
void display_port_set_inverted(bool inverted);
void display_port_set_brightness(uint8_t level);
uint8_t display_port_brightness(void);
#endif
