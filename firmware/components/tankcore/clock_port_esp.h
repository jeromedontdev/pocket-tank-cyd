#ifndef CLOCK_PORT_ESP_H
#define CLOCK_PORT_ESP_H
#include <stdint.h>
/* After a power cut, seed the ESP32 clock no earlier than the saved tank. */
void clock_port_seed(int64_t not_before);
#endif
