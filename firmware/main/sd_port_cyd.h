#ifndef SD_PORT_CYD_H
#define SD_PORT_CYD_H
#include <stdbool.h>
#include "tank.h"
bool sd_port_init(void);
void sd_port_command(tank_t *tank, int argc, char **argv);
void sd_port_backup_action(tank_t *tank, bool restore, bool confirmed);
void sd_port_update_action(tank_t *tank);
void sd_port_update_reset(void);
#endif
