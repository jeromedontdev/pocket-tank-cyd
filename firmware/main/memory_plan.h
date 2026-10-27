#ifndef MEMORY_PLAN_H
#define MEMORY_PLAN_H
#include "tank.h"
#define PLAN_FB_BYTES (240 * 180 * 2)
#define PLAN_FB_ALLOC ((PLAN_FB_BYTES + 63) & ~63)
#define PLAN_FB_COUNT 1
#define PLAN_PIPELINE_ROWS 40
#define PLAN_PIPELINE_BUFFERS 2
#define PLAN_TILE_BYTES (320 * PLAN_PIPELINE_ROWS * 2)
_Static_assert(PLAN_PIPELINE_BUFFERS * PLAN_TILE_BYTES <= PLAN_FB_BYTES, "pipeline must fit the existing framebuffer");
#endif
