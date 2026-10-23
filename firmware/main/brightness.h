/* brightness.h - the panel brightness policy (2026-09-11, the battery pass).
 * A user level, 10..100 %, dragged on the settings page (2026-09-15;
 * before that a row at the foot of the milestones page) or `level N` on
 * the director, and kept in NVS
 * ("tank"/"bright"); x0.6 at the tank's night, where the pixels are already
 * dimmed to 45% so the panel can follow unnoticed. Deliberately NO idle dim:
 * people watch the tank without touching it. The director's `bright N`
 * writes the panel directly and holds until the next policy change. */
#ifndef BRIGHTNESS_H
#define BRIGHTNESS_H
#include <stdbool.h>
void brightness_init(void);              /* after nvs_flash_init */
int  brightness_level(void);             /* 10..100 percent */
bool brightness_preview_level(int pct);  /* live drag, no NVS writes */
bool brightness_set_level(int pct);      /* saved once on release; rollback on failure */
void brightness_cycle(void);             /* 100 -> 60 -> 30 -> 100; saved */
bool brightness_save(void);              /* re-save after an NVS erase (reset) */
void brightness_apply(bool night);       /* once per frame: pushes a changed target to the panel */
#endif
