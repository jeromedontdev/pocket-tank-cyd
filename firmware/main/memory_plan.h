#ifndef MEMORY_PLAN_H
#define MEMORY_PLAN_H
#include "tank.h"
#define PLAN_FB_BYTES (TANK_W * TANK_H * 2)
#define PLAN_FB_ALLOC ((PLAN_FB_BYTES + 63) & ~63)
#define PLAN_FB_COUNT 1
#endif
