#ifndef SD_PORT_CYD_H
#define SD_PORT_CYD_H
#include <stdbool.h>
#include "tank.h"
/* SD entry points are serialized by the tank task. Only the busy query is
 * cross-task safe; the private daily worker never calls a public SD operation. */
bool sd_port_init(void);
void sd_port_command(tank_t *tank, int argc, char **argv);
void sd_port_backup_action(tank_t *tank, bool restore, bool confirmed);
void sd_port_update_action(tank_t *tank);
void sd_port_update_reset(void);
/* Tank-task APIs: now queues a request; snapshot/validation runs only in service.
 * Tick must run even when automatic backups are disabled to collect completion.
 * Keep optional render caches released while busy to leave room for SD/TLS. */
bool sd_port_auto_backup_now(tank_t *tank);
bool sd_port_auto_backup_busy(void);
void sd_port_auto_backup_tick(tank_t *tank, float dt);
/* Always collects completion; allow_start=false suppresses new daily jobs.
 * Network/UI admission must be decided on the tank task, before weather_tick.
 * Tick is the equivalent of service(tank, dt, true), with built-in net guards.
 * Call service from the shallow main loop, never from the console/director.
 * Busy covers allocated jobs only, not pending requests: network work may
 * finish before a request is admitted. Explicit requests ignore the daily setting. */
void sd_port_auto_backup_service(tank_t *tank, float dt, bool allow_start);
bool sd_port_photo_begin(int width, int height);
bool sd_port_photo_rows(const uint16_t *rows, int first, int count, int width);
bool sd_port_photo_end(bool ok);
#endif
