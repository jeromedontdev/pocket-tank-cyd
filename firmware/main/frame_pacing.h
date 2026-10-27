#ifndef FRAME_PACING_H
#define FRAME_PACING_H
#include <stdint.h>
#include <string.h>

typedef struct {
    uint32_t bins[128], frames, over_50, over_80, over_100;
    uint64_t total_us;
    uint32_t max_us;
} frame_pacing_t;

typedef struct { int64_t next_us; } frame_schedule_t;

/* Preserve the fractional 30-Hz phase while on time; never queue catch-up frames. */
static inline uint32_t frame_schedule_delay_ticks(frame_schedule_t *s, int64_t started_us,
                                                 int64_t finished_us, uint32_t tick_us) {
    int64_t target = s->next_us ? s->next_us : started_us + 33334;
    if (finished_us >= target) { s->next_us = 0; return 1; }
    s->next_us = target + 33334;
    return (uint32_t)((target - finished_us + tick_us - 1) / tick_us);
}

static inline void frame_pacing_add(frame_pacing_t *p, uint32_t us) {
    unsigned bin = us / 2000;
    p->bins[bin < 128 ? bin : 127]++;
    p->frames++; p->total_us += us;
    if (us > p->max_us) p->max_us = us;
    p->over_50 += us > 50000;
    p->over_80 += us > 80000;
    p->over_100 += us > 100000;
}

/* Histogram percentiles are conservative 2-ms upper bounds; overflow uses max. */
static inline uint32_t frame_pacing_percentile(const frame_pacing_t *p, unsigned percent) {
    if (!p->frames) return 0;
    uint32_t target = (uint32_t)(((uint64_t)p->frames * percent + 99) / 100), count = 0;
    for (unsigned i = 0; i < 128; i++) {
        count += p->bins[i];
        if (count >= target) return i == 127 ? p->max_us : (i + 1) * 2000;
    }
    return p->max_us;
}
#endif
