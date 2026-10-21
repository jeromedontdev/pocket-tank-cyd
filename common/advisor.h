/* Personality-aware, synchronous goal selection for the CYD tank. */
#ifndef ADVISOR_H
#define ADVISOR_H
#include "tank.h"
goal_t advisor_rules(const tank_t *t, int fish_idx, bool request);
#endif
