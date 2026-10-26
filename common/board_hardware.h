#ifndef BOARD_HARDWARE_H
#define BOARD_HARDWARE_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    BOARD_HARDWARE_CYD = 1,
    BOARD_HARDWARE_ELEGOO = 2,
} board_hardware_id_t;

typedef struct {
    board_hardware_id_t id;
    const char *name;
    bool has_ldr;
    int led_red_pin;
    int audio_enable_pin;
    bool panel_mirror_columns;
    bool default_touch_flip_x;
} board_hardware_t;

static inline const board_hardware_t *board_hardware_lookup(unsigned id) {
    static const board_hardware_t cyd = {
        BOARD_HARDWARE_CYD, "cyd2432s028", true, 4, -1, false, false,
    };
    static const board_hardware_t elegoo = {
        BOARD_HARDWARE_ELEGOO, "elegoo28", false, 22, 4, true, true,
    };
    switch (id) {
    case BOARD_HARDWARE_CYD: return &cyd;
    case BOARD_HARDWARE_ELEGOO: return &elegoo;
    default: return 0;
    }
}

static inline board_hardware_id_t board_hardware_build_default(void) {
#if PT_BOARD_UNIFIED
    return (board_hardware_id_t)0;
#elif PT_BOARD_ELEGOO
    return BOARD_HARDWARE_ELEGOO;
#else
    return BOARD_HARDWARE_CYD;
#endif
}

#if PT_RUNTIME_HARDWARE
const board_hardware_t *board_hardware_current(void);
bool board_hardware_init(void);
bool board_hardware_save(unsigned id);
#else
static inline const board_hardware_t *board_hardware_current(void) {
    return board_hardware_lookup(board_hardware_build_default());
}
#endif

#endif
