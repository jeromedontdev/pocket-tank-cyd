/* notice.c - the announcement queue. See notice.h. */
#include "notice.h"
#include "progression.h"
#include <stdio.h>
#include <string.h>

static bool     s_synced;
static bool     s_tip;                       /* the tank's light_tip_seen, last frame */
static notice_t s_q[NOTICE_QUEUE];
static int      s_qn;
static notice_t s_cur;
static bool     s_up;
static float    s_gap;                       /* seconds until the next may come up */
static char     s_care_status[80];

static void push(int kind, int fish, uint32_t bit) {
    if (s_qn >= NOTICE_QUEUE) return;
    s_q[s_qn++] = (notice_t){ kind, fish, bit, 0 };
}

void notice_sync(const tank_t *t) {
    s_synced = true; s_tip = t->light_tip_seen;
    s_qn = 0; s_up = false; s_gap = 0;
    s_care_status[0] = 0;
}

void notice_tick(const tank_t *t, float dt, bool blocked) {
    if (!s_synced) { notice_sync(t); return; }
    const char *status = progression_hard_status(t);
    if (!status) {
        s_care_status[0] = 0;
        for (int i = 0; i < s_qn; ) {
            if (s_q[i].kind != NOTICE_CARE_STATUS) { i++; continue; }
            memmove(&s_q[i], &s_q[i + 1], (size_t)(s_qn - i - 1) * sizeof s_q[0]); s_qn--;
        }
        if (s_up && s_cur.kind == NOTICE_CARE_STATUS) s_up = false;
    } else if (strcmp(status, s_care_status)) {
        snprintf(s_care_status, sizeof s_care_status, "%s", status);
        if (s_up && s_cur.kind == NOTICE_CARE_STATUS) s_cur.age = 0;
        else {
            /* Care/storage warnings take precedence over informational notices. */
            for (int i = s_qn - 1; i >= 0; i--)
                if (s_q[i].kind == NOTICE_CARE_STATUS) {
                    memmove(&s_q[i], &s_q[i + 1], (size_t)(s_qn - i - 1) * sizeof s_q[0]); s_qn--;
                }
            if (s_qn == NOTICE_QUEUE) s_qn--;
            memmove(&s_q[1], &s_q[0], (size_t)s_qn * sizeof s_q[0]);
            s_q[0] = (notice_t){NOTICE_CARE_STATUS, -1, 0, 0}; s_qn++;
        }
    }
    if (t->light_tip_seen && !s_tip) push(NOTICE_LIGHTS_OUT, -1, 0);   /* the first double-tap that turned the light off */
    s_tip = t->light_tip_seen;

    if (s_up) {
        if (blocked) {                       /* something opened over it (2026-09-29: the birth flow over
                                                a fry's population badge): it steps aside and comes back
                                                after, whole and quiet - its cue has played */
            if (s_qn < NOTICE_QUEUE) {
                memmove(&s_q[1], &s_q[0], sizeof(notice_t) * (size_t)s_qn);
                s_q[0] = s_cur; s_q[0].age = -1; s_qn++;
            }
            s_up = false; s_gap = 0;
            return;
        }
        s_cur.age += dt;
        if (s_cur.age >= NOTICE_UP_S) { s_up = false; s_gap = NOTICE_GAP_S; }
        return;
    }
    if (s_gap > 0) { s_gap -= dt; return; }
    if (blocked || !s_qn) return;
    s_cur = s_q[0]; s_cur.age = 0;
    memmove(&s_q[0], &s_q[1], sizeof(notice_t) * (size_t)(s_qn - 1)); s_qn--;
    s_up = true;
}

const notice_t *notice_current(void) { return s_up ? &s_cur : NULL; }
bool notice_dismiss(void) {
    if (!s_up) return false;
    s_up = false; s_gap = NOTICE_GAP_S;
    return s_cur.kind != NOTICE_LIGHTS_OUT;   /* its tap goes on to the tank: the double-tap it asks for */
}
void notice_updated(void) { push(NOTICE_UPDATED, -1, 0); }
int notice_pending(void) { return s_qn; }
