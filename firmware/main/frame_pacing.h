#ifndef FRAME_PACING_H
#define FRAME_PACING_H
#include <stdint.h>
#include <string.h>

typedef struct {
    uint32_t bins[128], frames, over_50, over_80, over_100;
    uint64_t total_us;
    uint32_t max_us;
} frame_pacing_t;

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
