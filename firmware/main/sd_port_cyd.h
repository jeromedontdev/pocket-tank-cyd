#ifndef SD_PORT_CYD_H
#define SD_PORT_CYD_H
#include <stdbool.h>
#include "tank.h"
bool sd_port_init(void);
void sd_port_command(tank_t *tank, int argc, char **argv);
void sd_port_backup_action(tank_t *tank, bool restore, bool confirmed);
void sd_port_update_action(tank_t *tank);
void sd_port_update_reset(void);
bool sd_port_auto_backup_now(tank_t *tank);
void sd_port_auto_backup_tick(tank_t *tank, float dt);
bool sd_port_photo_begin(int width, int height);
bool sd_port_photo_rows(const uint16_t *rows, int first, int count, int width);
bool sd_port_photo_end(bool ok);
#endif
