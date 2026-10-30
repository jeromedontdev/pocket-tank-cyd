/* notice.h - care/storage warnings, release updates and the lights-out tip.
 * Legacy milestone and growth-notice enum IDs remain reserved; they never
 * enter the queue or draw, even when historical save bits change.
 *
 * One notice is up at a time for NOTICE_UP_S, or until a tap; the next
 * waits NOTICE_GAP_S. Nothing shows while `blocked` (setup / birth flow /
 * reset prompt up, or a fry's welcome due - setup_birth_due):
 * the queue holds, and a notice already up steps aside and comes back afterward.
 * NOTICE_LIGHTS_OUT (2026-10-03, Strato: "in case it surprises someone the
 * first time it happens"): the first double-tap that turns the light off
 * (tank.h light_tip_seen, saved) says so, once, without a chime. A tap on it
 * closes it AND reaches the tank (notice_dismiss returns false), so the
 * double-tap it asks for works with the notice still up. */
#ifndef NOTICE_H
#define NOTICE_H
#include <stdbool.h>
#include <stdint.h>
#include "tank.h"

enum { NOTICE_MILESTONE, NOTICE_TANK_MILESTONE, NOTICE_STAGE, NOTICE_UPDATED = 4, NOTICE_LIGHTS_OUT, NOTICE_CARE_STATUS };
typedef struct { int kind; int fish; uint32_t bit; float age; } notice_t;

#define NOTICE_UP_S   6.0f
#define NOTICE_GAP_S  1.0f
#define NOTICE_QUEUE  8

void notice_sync(const tank_t *t);
void notice_tick(const tank_t *t, float dt, bool blocked);
const notice_t *notice_current(void);      /* NULL = none up */
bool notice_dismiss(void);                 /* a tap: true = the notice took it, false = it goes on to the tank */
void notice_updated(void);                 /* platform: the first boot of a new release (after notice_sync) */
int  notice_pending(void);                 /* queued, not counting the one up */

#endif
