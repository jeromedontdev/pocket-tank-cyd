/* tank.c — reflex layer. See tank.h. Faithful port of the browser prototype's
 * updateFish/targetForGoal/wall handling, with prototype px values scaled by
 * ~0.55 for the 448-wide tank. */
#include "tank.h"
#include "companion.h"
#include <stdio.h>
#include "tank_events.h"
#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

int tank_width = 240, tank_height = 180;
void tank_resolution(bool compact) { tank_width = compact ? 320 : 240; tank_height = compact ? 240 : 180; }
void tank_reframe(tank_t *t, bool compact) {
    int old_w = TANK_W;
    if (old_w == (compact ? 320 : 240)) return;
    tank_resolution(compact);
    float scale = (float)TANK_W / old_w;
#define SCALE(member) t->member *= scale
    SCALE(bubble_x); SCALE(bubble_y); SCALE(reef_x); SCALE(reef_y);
    if (t->feed_spot_x >= 0) SCALE(feed_spot_x);
    if (t->snail_x >= 0) { SCALE(snail_x); SCALE(snail_y); }
    if (t->urchin_x >= 0) SCALE(urchin_x);
    if (t->urchin_to >= 0) SCALE(urchin_to);
    if (t->sh_x >= 0) { SCALE(sh_x); SCALE(sh_y); SCALE(sh_aref); }
    SCALE(castle_x); SCALE(coral_x); SCALE(cluster_x); SCALE(pumpkin_x); SCALE(chest_x); SCALE(plant_x);
    SCALE(stage_x); SCALE(stage_y); SCALE(startle_x); SCALE(startle_y); SCALE(tap_x); SCALE(tap_y);
    SCALE(reef_flow_x); SCALE(reef_flow_y);
    for (int i = 0; i < 10; i++) SCALE(ornament_x[i]);
    for (int i = 0; i < DECOR_EXTRA_CAP; i++) SCALE(inventory.extra[i].x);
    for (int i = 0; i < t->n_fish; i++) {
        SCALE(fish[i].x); SCALE(fish[i].y); SCALE(fish[i].explore_x); SCALE(fish[i].explore_y);
        SCALE(fish[i].dart_x); SCALE(fish[i].dart_y);
        t->fish[i].explore_set = false;
    }
    for (int i = 0; i < MAX_FOOD; i++) { SCALE(food[i].x); SCALE(food[i].y); }
    for (int i = 0; i < MAX_BUBBLE; i++) { SCALE(bubble[i].x); SCALE(bubble[i].y); }
    for (int i = 0; i < t->shrimp_n; i++) { SCALE(shrimp[i].x); SCALE(shrimp[i].y); }
#undef SCALE
    memset(t->algae, 0, sizeof t->algae);
    t->snail_cell = -1; t->drag_active = t->drag_has_prev = t->hold_active = false;
}
#define TAU 6.2831853f
static void veg_sync(tank_t *t);   /* veg_growth[] = per-bed mean of veg_h[][] */

const char *const GOAL_NAMES[GOAL_COUNT] = {
    "seek_food", "flee_shadow", "visit_bubbles", "follow_friend",
    "explore", "rest", "dart_play", "inspect_reef",
};

/* ---- the event bus (tank_events.h): one listener, synchronous ---- */
const char *const TANK_EVENT_NAMES[TEV_COUNT] = {
    "tap", "feed", "light_on", "light_off", "wipe", "snip", "eat", "spook", "investigate", "bubbles",
    "welcome", "wheel_tick", "confirm",
};
static tank_event_fn s_ev_fn; static void *s_ev_ud;
void tank_events_set(tank_event_fn fn, void *ud) { s_ev_fn = fn; s_ev_ud = ud; }
void tank_emit(int ev, int fish) { if (s_ev_fn) s_ev_fn(ev, fish, s_ev_ud); }

const char *const STAGE_NAMES[4] = { "fry", "juv", "adult", "elder" };
const char *const TRAINED_NAMES[N_TRAINED_NAMES] = { "mira", "bolt", "kelp", "nori" };

int tank_reflex_overrides = 0;   /* starvation-ignored episodes (diagnostic) */

/* deterministic xorshift32 */
static uint32_t xr(tank_t *t) {
    uint32_t x = t->rng;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return t->rng = x;
}
float tank_randf(tank_t *t, float lo, float hi) {
    return lo + (hi - lo) * (float)(xr(t) & 0xffffff) / 16777215.0f;
}
static float clampf(float v, float lo, float hi) {
    return v < lo ? lo : v > hi ? hi : v;
}
static float norm_ang(float a) {
    while (a >  3.14159265f) a -= TAU;
    while (a < -3.14159265f) a += TAU;
    return a;
}
static float lerpf(float a, float b, float p) { return a + (b - a) * p; }

float tank_dist(float ax, float ay, float bx, float by) {
    float dx = ax - bx, dy = ay - by;
    return sqrtf(dx * dx + dy * dy);
}

/* ---- the glass (tank.h). The rectangle's is its frame; the bowl's is the
 * circle, with the flat bottom at TANK_BOT. ---- */
float tank_glass_x0(float y) { (void)y; return 0; }
float tank_glass_x1(float y) { (void)y; return TANK_W; }
float tank_glass_top(float x) { (void)x; return 0; }
void tank_glass_clamp(float *x, float *y, float m) {
    *x = clampf(*x, m, TANK_W - m); *y = clampf(*y, m, TANK_H - m);
}
#define corner_clamp(x, y, m) ((void)0)            /* the plain rectangle (and the bowl, which has its own): no corners */
float tank_fish_pitch(const fish_t *f) {
    /* Clearance queries project one pose in four directions consecutively. */
    static float heading, speed, pitch;
    static bool valid;
    if (valid && heading == f->heading && speed == f->speed) return pitch;
    float limit = 0.61f + 0.35f * clampf((f->speed - 35) / 40, 0, 1);
    pitch = limit * tanhf(0.8f * atan2f(sinf(f->heading), fabsf(cosf(f->heading))) / limit);
    heading = f->heading; speed = f->speed; valid = true;
    return pitch;
}
static int wearable_shape(const fish_t *f, int part, float x[8], float y[8],
                          uint32_t *color, float head, float neck) {
    int n = 4;
    if (f->wearable >= WEAR_SAILOR) {
        if (f->wearable != WEAR_VIKING && f->wearable != WEAR_SCARF && part > 1) return 0;
        float xx[8], yy[8];
        switch (f->wearable) {
        case WEAR_SAILOR:
            if (!part) {
                float a[6] = {-9, -7, -5, 7, 9, 11}, b[6] = {head-2, head-7, head-9, head-9, head-7, head-2};
                memcpy(xx,a,sizeof a); memcpy(yy,b,sizeof b); n=6; *color=0xf2eee0;
            } else {
                float a[4]={-9,11,11,-9}, b[4]={head-3,head-3,head,head};
                memcpy(xx,a,sizeof a); memcpy(yy,b,sizeof b); *color=0x385caa;
            } break;
        case WEAR_PIRATE:
            if (!part) {
                float a[7]={-12,-7,-4,2,8,11,14}, b[7]={head,head-5,head-11,head-13,head-10,head-5,head};
                memcpy(xx,a,sizeof a); memcpy(yy,b,sizeof b); n=7; *color=0x313647;
            } else {
                float a[4]={0,4,4,0}, b[4]={head-8,head-8,head-4,head-4};
                memcpy(xx,a,sizeof a); memcpy(yy,b,sizeof b); *color=0xf3e6ba;
            } break;
        case WEAR_BERET:
            if (!part) {
                float a[7]={-10,-9,-3,7,12,9,-6}, b[7]={head-2,head-6,head-9,head-10,head-6,head-2,head};
                memcpy(xx,a,sizeof a); memcpy(yy,b,sizeof b); n=7; *color=0xbe526b;
            } else {
                float a[4]={3,5,5,3}, b[4]={head-13,head-13,head-9,head-9};
                memcpy(xx,a,sizeof a); memcpy(yy,b,sizeof b); *color=0x693e5b;
            } break;
        case WEAR_SUNHAT:
            if (!part) {
                float a[8]={-14,-6,-5,7,9,16,14,-12}, b[8]={head-1,head-4,head-10,head-10,head-4,head-1,head+1,head+1};
                memcpy(xx,a,sizeof a); memcpy(yy,b,sizeof b); n=8; *color=0xecc977;
            } else {
                float a[4]={-6,9,10,-7}, b[4]={head-5,head-5,head-3,head-3};
                memcpy(xx,a,sizeof a); memcpy(yy,b,sizeof b); *color=0xe68c7f;
            } break;
        case WEAR_VIKING:
            if (!part) {
                float a[7]={-11,-10,-6,6,11,12,11};
                float b[7]={head+1,head-4,head-10,head-11,head-7,head-3,head+1};
                memcpy(xx,a,sizeof a); memcpy(yy,b,sizeof b); n=7; *color=0x91a9b5;
            } else if (part == 1) {
                float a[4]={-10,-16,-14,-6}, b[4]={head-4,head-8,head-17,head-10};
                memcpy(xx,a,sizeof a); memcpy(yy,b,sizeof b); *color=0xf2dfac;
            } else if (part == 2) {
                float a[4]={6,14,16,10}, b[4]={head-10,head-17,head-8,head-4};
                memcpy(xx,a,sizeof a); memcpy(yy,b,sizeof b); *color=0xf2dfac;
            } else {
                float a[4]={-11,11,10,-10}, b[4]={head-2,head-2,head+1,head+1};
                memcpy(xx,a,sizeof a); memcpy(yy,b,sizeof b); *color=0xf2dfac;
            } break;
        default: /* a solid band around the gills, a front knot, and two hanging tails */
            if (!part) {
                float a[4]={-3,3,5,-1}, b[4]={head+1,head+1,neck,neck};
                memcpy(xx,a,sizeof a); memcpy(yy,b,sizeof b); *color=0xd2463a;
            } else if (part == 1) {
                float a[4]={1,2,-9,-10}, b[4]={neck-2,neck+2,neck+5,neck+1};
                memcpy(xx,a,sizeof a); memcpy(yy,b,sizeof b); *color=0xf0715a;
            } else if (part == 2) {
                float a[4]={3,5,-3,-5}, b[4]={neck-1,neck+2,neck+6,neck+3};
                memcpy(xx,a,sizeof a); memcpy(yy,b,sizeof b); *color=0xf0715a;
            } else {
                float a[4]={-1,5,6,0}, b[4]={neck-3,neck-3,neck+1,neck+1};
                memcpy(xx,a,sizeof a); memcpy(yy,b,sizeof b); *color=0xf0715a;
            } break;
        }
        memcpy(x,xx,(size_t)n*sizeof *x); memcpy(y,yy,(size_t)n*sizeof *y);
        return n;
    }
    if (part > 1) return 0;
    if (part && f->wearable != WEAR_TOP_HAT && f->wearable != WEAR_BOWTIE) {
        if (f->wearable == WEAR_PARTY_HAT || f->wearable == WEAR_CROWN) {
            float cx = f->wearable == WEAR_CROWN ? 1 : 2, width = f->wearable == WEAR_CROWN ? 2 : 4;
            x[0] = cx - width; x[1] = cx; x[2] = cx + width;
            y[0] = y[2] = head - 4; y[1] = head - (f->wearable == WEAR_CROWN ? 7 : 10);
            *color = 0xffef9a; return 3;
        }
        if (f->wearable == WEAR_TIE) {
            x[0] = 6; x[1] = 8; x[2] = 6; x[3] = 4;
            y[0] = neck - 3; y[1] = y[3] = neck - 1; y[2] = neck + 1;
        } else {
            x[0] = x[3] = -2; x[1] = x[2] = 12;
            y[0] = y[1] = neck - 2; y[2] = y[3] = neck;
        }
        *color = 0xffe8b0; return 4;
    }
    if (f->wearable == WEAR_TOP_HAT) {
        float xx[4] = {-6, 8, 8, -6}, yy[4] = {head - 9, head - 9, head - 1, head - 1};
        if (part) { xx[0] = xx[3] = -9; xx[1] = xx[2] = 11; yy[0] = yy[1] = head - 2; yy[2] = yy[3] = head; }
        memcpy(x, xx, sizeof xx); memcpy(y, yy, sizeof yy); *color = part ? 0xf5cd65 : 0x673cab;
    } else if (f->wearable == WEAR_PARTY_HAT) {
        if (part) return 0;
        x[0] = -7; x[1] = 2; x[2] = 10; y[0] = y[2] = head; y[1] = head - 13;
        n = 3; *color = 0xff65b5;
    } else if (f->wearable == WEAR_CROWN) {
        if (part) return 0;
        float xx[7] = {-8, -9, -4, 1, 6, 11, 10};
        float yy[7] = {head, head - 9, head - 5, head - 12, head - 5, head - 9, head};
        memcpy(x, xx, sizeof xx); memcpy(y, yy, sizeof yy); n = 7; *color = 0xffda46;
    } else if (f->wearable == WEAR_TIE) {
        if (part) return 0;
        float xx[4] = {6, 9, 6, 3}, yy[4] = {neck - 3, neck + 7, neck + 10, neck + 7};
        memcpy(x, xx, sizeof xx); memcpy(y, yy, sizeof yy); *color = 0xff4c68;
    } else if (f->wearable == WEAR_BOWTIE) {
        x[0] = 5; x[1] = part ? 11 : -1; x[2] = x[1];
        y[0] = neck; y[1] = neck - 4; y[2] = neck + 4;
        n = 3; *color = part ? 0x86f1f3 : 0x45c3de;
    } else {
        if (part) return 0;
        x[0] = -2; x[1] = 12; x[2] = 5;
        y[0] = y[1] = neck - 2; y[2] = neck + 8;
        n = 3; *color = 0x7cdd76;
    }
    return n;
}
int tank_wearable_polygon(const fish_t *f, int part, float x[8], float y[8], uint32_t *color) {
    if (!f->wearable || !companion_wearable_fits(f->variety, f->wearable) ||
        part < 0 || part > 3) return 0;
    /* Body-space anchors follow the forehead/gill outline, not the dorsal fin.
     * The renderer and clearance queries consume these same fitted polygons.
     * Stage size, yaw and pitch are applied once by their existing callers. */
    static const struct { float x, top, bottom, width; } fit[FISH_VARIETY_COUNT] = {
        {5, -8.7f, 8, .75f}, {5, -13.5f, 12, .8f},
        {5, -8.7f, 8, .75f}, {5, -7, 5, .8f},
        {3, -12, 11, .75f}, {6, -7.5f, 6, .7f},
        {0, -15.5f, 0, .75f}, {6, -7, 7, .65f},
        {4, -10.2f, 9, .7f}, {5, -8.7f, 8, .75f}
    };
    bool collar = f->wearable == WEAR_TIE || f->wearable == WEAR_BOWTIE ||
                  f->wearable == WEAR_BANDANA || f->wearable == WEAR_SCARF;
    int n = wearable_shape(f, part, x, y, color, fit[f->variety].top, fit[f->variety].bottom);
    for (int i = 0; i < n; i++) {
        if (collar) {
            x[i] = (x[i] - 3) * .75f;
            if (f->wearable != WEAR_SCARF)
                y[i] = fit[f->variety].bottom + (y[i] - fit[f->variety].bottom) * .75f;
        } else {
            x[i] = fit[f->variety].x + (x[i] - 1) * fit[f->variety].width;
            y[i] = fit[f->variety].top + (y[i] - fit[f->variety].top) * .8f;
        }
    }
    return n;
}
static float fish_projected_extent(const fish_t *f, float direction, bool horizontal) {
    float p = tank_fish_pitch(f), sp = (horizontal ? cosf(p) : sinf(p)) * direction;
    float cp = (horizontal ? -sinf(p) : cosf(p)) * direction, extent = 0;
#define EXTENT(x, y) extent = fmaxf(extent, (x) * sp + (y) * cp)
    float bx = f->variety == FISH_SHARK ? 1.4f : f->variety == FISH_TETRA ? 1.15f :
               f->variety == FISH_PUFFER ? 0.85f : f->variety == FISH_CLOWNFISH ? 0.9f : 1;
    float by = f->variety == FISH_ANGEL ? 1.55f : f->variety == FISH_TETRA ? 0.65f :
               f->variety == FISH_PUFFER ? 1.35f : f->variety == FISH_SHARK ? 0.7f :
               f->variety == FISH_CLOWNFISH ? 1.15f : 1;
    static const float body_x[] = {16, 10, 2, -8, -15, -18, -15, -8, 2, 10};
    static const float shark_x[] = {20, 9, 0, -9, -17, -21, -17, -9, 0, 12};
    static const float ray_x[] = {13, 9, 2, -5, -10, -13, -10, -5, 2, 9};
    static const float body_y[] = {0, -7, -10, -9, -6, 0, 6, 9, 10, 7};
    static const float harbor_x[] = {18, 12, 3, -7, -13, -17, -12, -5, 6, 14};
    static const float harbor_y[] = {0, -4, -8, -8, -4, 0, 3, 7, 7, 3};
    if (f->variety == FISH_JELLYFISH) {
        static const float x[] = {-16, -15, -11, -6, 0, 6, 11, 15, 16, 8, -8};
        static const float y[] = {1, -5, -11, -15, -17, -15, -11, -5, 1, 3, 3};
        for (int i = 0; i < 11; i++) { EXTENT(x[i] * 1.07f, y[i]); }
        for (int strand = 0; strand < 6; strand++)
            for (int segment = 0; segment <= 7; segment++) {
                float y = 2 + segment * (strand % 3 == 1 ? 2.85f : 2.7f);
                float x = (-13.5f + strand * 5.4f) * (1 - segment * .035f), sway = segment * 0.5f + 0.5f;
                EXTENT(x - sway, y); EXTENT(x + sway, y);
            }
        for (int strand = 6; strand < 9; strand += 2)
            for (int segment = 0; segment <= 5; segment++) {
                float y = 2 + segment * 3.3f, x = (strand == 6 ? -1 : 1) * (2.2f + segment * .62f);
                float sway = segment * 0.2f + 2.6f;
                EXTENT(x - sway, y); EXTENT(x + sway, y);
            }
    } else {
        for (int i = 0; i < 10; i++) {
            float x = f->variety == FISH_CLASSIC ? harbor_x[i] : f->variety == FISH_SHARK ? shark_x[i] : f->variety == FISH_STINGRAY ? ray_x[i] : body_x[i];
            float y = f->variety == FISH_CLASSIC ? harbor_y[i] : body_y[i];
            EXTENT(x * bx, y * by);
        }
        extent = fmaxf(extent, horizontal ? 6.5f : 2 * sp + 9.5f * by);
        float tail = (0.32f + f->speed * 0.007f) * 8;
        if (f->variety == FISH_STINGRAY) {
            EXTENT(2, -28); EXTENT(2, 28); EXTENT(-9, -20); EXTENT(-9, 20);
            EXTENT(-38, tail * 3 / 8); EXTENT(-38, -tail * 3 / 8);
        } else {
            float tx = (f->variety == FISH_CLASSIC ? -29 : -26) *
                       (f->stage == STAGE_ELDER ? 1.22f : 1) * (f->variety == FISH_GUPPY ? 1.3f : 1);
            float spread = f->variety == FISH_GUPPY ? 17 : f->variety == FISH_PUFFER ? 6 :
                           f->variety == FISH_BETTA ? 20 : f->variety == FISH_SHARK ? 15 : 10;
            EXTENT(f->variety == FISH_SHARK ? -32 : tx, -spread - tail);
            EXTENT(f->variety == FISH_SHARK ? -30 : tx, f->variety == FISH_SHARK ? 10 + tail / 2 : spread + tail);
            EXTENT(-11, 0);
            if (f->variety == FISH_CLASSIC) {
                EXTENT(10, -5); EXTENT(3, -17); EXTENT(-4, -19);
                EXTENT(-12, -10); EXTENT(-9, -7); EXTENT(5, -7);
            }
            if (f->stage >= STAGE_ADULT || f->variety == FISH_SHARK || f->variety == FISH_BETTA || f->variety == FISH_CLOWNFISH) {
                float h = f->variety == FISH_ANGEL ? -27 : f->variety == FISH_SHARK ? -23 :
                          f->variety == FISH_BETTA ? -21 : f->stage == STAGE_ELDER ? -17 : -13;
                EXTENT(6, -8); EXTENT(-2, h); EXTENT(-9, -8);
            }
            if (f->variety == FISH_ANGEL) { EXTENT(5, 9); EXTENT(-8, 25); EXTENT(-11, 7); }
            if (f->variety == FISH_SHARK) { EXTENT(7, 3); EXTENT(-5, 17); EXTENT(-10, 5); }
            if (f->variety == FISH_BETTA) {
                EXTENT(8, 6); EXTENT(-3, 22 + tail / 4); EXTENT(-24, 25);
                EXTENT(-22, 11); EXTENT(-8, 7); EXTENT(4, 5);
            }
        }
    }
    for (int part = 0; part < 4; part++) {
        float x[8], y[8]; uint32_t color;
        int n = tank_wearable_polygon(f, part, x, y, &color);
        for (int i = 0; i < n; i++) { EXTENT(x[i], y[i]); }
    }
#undef EXTENT
    return extent * f->size + 1.5f;
}
float tank_fish_clearance(const fish_t *f) { return fish_projected_extent(f, 1, false); }
float tank_fish_top_clearance(const fish_t *f) { return fish_projected_extent(f, -1, false); }
float tank_fish_half_width(const fish_t *f) {
    return fmaxf(fish_projected_extent(f, 1, true), fish_projected_extent(f, -1, true));
}
/* a random point of open water: y in [y0, y1], x at least mx off the side glass at that height */
static void rand_water(tank_t *t, float mx, float y0, float y1, float *x, float *y) {
    *x = tank_randf(t, mx, TANK_W - mx); *y = tank_randf(t, y0, y1);
}

int tank_nearest_food(const tank_t *t, const fish_t *f, float *dist_out) {
    int best = -1; float bd = 1e9f;
    for (int i = 0; i < MAX_FOOD; i++) {
        if (!t->food[i].alive) continue;
        float d = tank_dist(f->x, f->y, t->food[i].x, t->food[i].y);
        if (d < bd) { bd = d; best = i; }
    }
    if (dist_out) *dist_out = bd;
    return best;
}

int tank_nearest_friend(const tank_t *t, int fish_idx, float *dist_out) {
    const fish_t *f = &t->fish[fish_idx];
    int best = -1; float bd = 1e9f;
    for (int i = 0; i < t->n_fish; i++) {
        if (i == fish_idx) continue;
        float d = tank_dist(f->x, f->y, t->fish[i].x, t->fish[i].y);
        if (d < bd) { bd = d; best = i; }
    }
    if (dist_out) *dist_out = bd;
    return best;
}

/* ---- roster: six presets (the prototype's four + two), colors chosen for
 * contrast on AMOLED black. A preset is a look + temperament; bold/social are
 * rolled per tank (starting pair) or inherited (arrivals). ---- */
typedef struct {
    const char *name; uint32_t color, fin, accent;
    float size, curiosity, lazy, turn_rate;
} preset_t;
static const preset_t ROSTER[] = {
    /* name    color     fin       accent    size  cur  lazy  turn */
    { "mira", 0x38dcc7, 0x1d9f98, 0xffbd59, 1.08f, 7.5f, 0.2f, 3.2f },
    { "bolt", 0xff725c, 0xb83a43, 0xffe08a, 0.94f, 6.0f, 0.2f, 4.2f },
    { "kelp", 0x78d67d, 0x3f8b55, 0xa799ff, 0.86f, 4.0f, 0.1f, 3.2f },
    { "nori", 0xa799ff, 0x6b5ed6, 0x78d67d, 1.00f, 5.0f, 0.7f, 2.4f },
    { "pip",  0xffd166, 0xc98a1e, 0x38dcc7, 0.90f, 6.5f, 0.3f, 3.8f },
    { "sol",  0xf48fb1, 0xb0456f, 0xffe08a, 1.04f, 5.5f, 0.4f, 2.9f },
};
#define ROSTER_N ((int)(sizeof ROSTER / sizeof ROSTER[0]))
int tank_roster_count(void) { return ROSTER_N; }
const char *tank_roster_name(int preset) { return preset >= 0 && preset < ROSTER_N ? ROSTER[preset].name : "?"; }

/* the keeper's palettes (setup.c): the six roster bodies + a blue and a
 * silver; the roster's five accents + white, the stress red and a dark ink */
const uint32_t LOOK_BODY[LOOK_N]   = { 0x38dcc7, 0xff725c, 0x78d67d, 0xa799ff, 0xffd166, 0xf48fb1, 0x4da3ff, 0xe8f1f2 };
const uint32_t LOOK_ACCENT[LOOK_N] = { 0xffbd59, 0xffe08a, 0xa799ff, 0x78d67d, 0x38dcc7, 0xffffff, 0xf25b65, 0x1a2a30 };
const uint32_t FISH_BODY_COLORS[FISH_COLOR_N] = {
    0x38dcc7, 0xff725c, 0x78d67d, 0xa799ff, 0xffd166, 0xf48fb1, 0x4da3ff, 0xe8f1f2,
    0xff1493, 0xff3030, 0xff7a00, 0xffef00, 0x9dff00, 0x00ff66, 0x00ffff, 0x007bff,
    0x483dff, 0x9f32ff, 0xff00ff, 0xffa8df, 0x97ffe0, 0xc8b8ff, 0xffffff, 0x263d50
};
const char *const CREATURE_NAMES[CREATURE_COUNT] = {"SHRIMP", "SNAIL", "URCHIN", "SEAHORSE", "PEARL MUSSEL"};
const char *const COSMETIC_COLOR_NAMES[COSMETIC_COLOR_COUNT] = {
    "NATURAL", "WHITE", "SLATE", "RED", "ORANGE", "GOLD", "YELLOW", "LIME",
    "GREEN", "MINT", "CYAN", "BLUE", "PURPLE", "PINK", "ROSE", "LAVENDER"
};
uint32_t tank_cosmetic_color(unsigned color, uint32_t natural) {
    static const uint8_t palette[COSMETIC_COLOR_COUNT] = {0, 22, 23, 9, 10, 4, 11, 12, 13, 20, 14, 15, 17, 8, 5, 21};
    if (color >= COSMETIC_COLOR_COUNT) fprintf(stderr, "Invalid cosmetic color: %u\n", color);
    return color && color < COSMETIC_COLOR_COUNT ? FISH_BODY_COLORS[palette[color]] : natural;
}
const uint32_t FISH_MARKING_COLORS[FISH_COLOR_N] = {
    0xffbd59, 0xffe08a, 0xa799ff, 0x78d67d, 0x38dcc7, 0xffffff, 0xf25b65, 0x1a2a30,
    0xff1493, 0xff3030, 0xff7a00, 0xffef00, 0x9dff00, 0x00ff66, 0x00ffff, 0x007bff,
    0x483dff, 0x9f32ff, 0xff00ff, 0xffa8df, 0x97ffe0, 0xc8b8ff, 0xe8f1f2, 0x263d50
};
/* the coral's swatches (2026-09-23): the reference art's orange first, then
 * reef colours - pink, magenta, violet, blue, teal, green, gold */
const uint32_t CORAL_PAL[CORAL_N] = { 0xff7a1e, 0xf2698f, 0xd24bb4, 0x8a5be0, 0x3f8ff0, 0x2ec9b4, 0x7fc64a, 0xf2c23a };
/* the reef cluster's three looks (2026-09-24): REEF is the art's - an
 * orange coral, purple tubes, a cyan brain; LAGOON pink / blue / lime;
 * DUSK magenta / teal / gold */
const cluster_scheme_t CLUSTER_SCHEMES[CLUSTER_SCHEME_N] = {
    { "REEF",   0xff6a2a, 0x9b4fe0, 0x5fd8e8 },
    { "LAGOON", 0xf25c8a, 0x3f8ff0, 0xb9f04a },
    { "DUSK",   0xd24bb4, 0x2ec9b4, 0xf2c23a },
};

void tank_set_name(tank_t *t, int slot, const char *name) {
    if (slot < 0 || slot >= N_FISH_MAX) return;
    fish_t *f = &t->fish[slot];
    if (!name || !*name) name = tank_roster_name(f->preset);
    int n = 0;
    /* canonical lowercase (2026-09-15): the roster, the director and the
     * trained names are lowercase, the display uppercases at draw, and the
     * wheel used to write capitals into the slots it spun ("FeZ", "LArRY") */
    while (name[n] && n < FISH_NAME_MAX) {
        char c = name[n];
        f->name[n] = c >= 'A' && c <= 'Z' ? (char)(c - 'A' + 'a') : c;
        n++;
    }
    f->name[n] = 0;
}
/* Suggested names (2026-10-30): a wide pool of short originals, none of
 * them a roster name, so a suggestion is always saved as the fish's own. */
static const char *const NAME_POOL[] = {
    "ash", "basil", "bean", "birch", "bixby", "blink", "bloop", "bonnie", "brook", "bubba",
    "buttons", "cairo", "cashew", "cedar", "chai", "chip", "cinder", "clove", "cobalt", "coco",
    "comet", "cosmo", "crumb", "dash", "dewdrop", "dill", "dot", "drift", "dune", "echo",
    "ember", "fable", "fennel", "fig", "finn", "flint", "fluke", "flurry", "fudge", "gizmo",
    "ginger", "gumbo", "halo", "hazel", "honey", "indigo", "inky", "iris", "ivy", "jasper",
    "jiggy", "juno", "kiwi", "koi", "lark", "latte", "lemon", "lilo", "loki", "lotus",
    "luna", "lyric", "mango", "maple", "marble", "mimi", "minnow", "misty", "mochi", "moss",
    "muffin", "nacho", "nimbus", "nova", "nugget", "nutmeg", "olive", "onyx", "opal", "otis",
    "pebble", "peanut", "pepper", "pickle", "pixel", "plum", "poppy", "puddle", "quill", "rain",
    "reed", "remy", "ripple", "river", "rocket", "rosie", "rusty", "saffron", "sage", "sandy",
    "scout", "shadow", "skipper", "sky", "slate", "smudge", "sorbet", "sparky", "speck", "splash",
    "sprout", "squid", "storm", "sunny", "taffy", "tango", "tansy", "tide", "toffee", "twig",
    "violet", "waffle", "wasabi", "willow", "wisp", "yuzu", "zephyr", "ziggy", "zinnia", "zuzu",
};
#define NAME_POOL_N ((int)(sizeof NAME_POOL / sizeof NAME_POOL[0]))
static uint32_t name_hash(uint32_t x) {
    x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15; x *= 0x846ca68bu; x ^= x >> 16; return x;
}
static bool name_taken(const tank_t *t, int slot, const char *name) {
    for (int i = 0; i < t->n_fish && i < N_FISH_MAX; i++)
        if (i != slot && !strncmp(t->fish[i].name, name, FISH_NAME_MAX)) return true;
    return false;
}
/* A stable hash picks the name, so suggesting draws nothing from the tank's
 * rng and seeded runs keep their sequence. Names already in the tank and the
 * slot's current name are skipped. */
const char *tank_suggest_name(const tank_t *t, int slot, uint32_t salt) {
    const char *current = slot >= 0 && slot < N_FISH_MAX ? t->fish[slot].name : "";
    uint32_t h = name_hash(t->rng ^ salt * 0x9e3779b1u ^ (uint32_t)slot * 0x632be5abu ^
                           (uint32_t)t->story.births * 0x85ebca6bu);
    int first = (int)(h % NAME_POOL_N);
    for (int step = 0; step < NAME_POOL_N; step++) {
        const char *name = NAME_POOL[(first + step * 37) % NAME_POOL_N];
        if (!name_taken(t, slot, name) && strncmp(current, name, FISH_NAME_MAX)) return name;
    }
    return NAME_POOL[first];
}
void tank_give_name(tank_t *t, int slot, uint32_t salt) {
    if (slot < 0 || slot >= N_FISH_MAX) return;
    t->fish[slot].name[0] = 0;
    tank_set_name(t, slot, tank_suggest_name(t, slot, salt));
}
static uint32_t fin_for(uint32_t body) {
    for (int i = 0; i < ROSTER_N; i++) if (ROSTER[i].color == body) return ROSTER[i].fin;
    /* a body colour of the keeper's own: the fin is that body at 58% */
    uint32_t r = (body >> 16 & 255) * 58 / 100, g = (body >> 8 & 255) * 58 / 100, b = (body & 255) * 58 / 100;
    return (r << 16) | (g << 8) | b;
}
uint32_t tank_fin_for(uint32_t body) { return fin_for(body); }
void tank_fish_set_look(fish_t *f, uint32_t body, uint32_t accent) {
    if (body)   { f->color = body; f->fin = fin_for(body); }
    if (accent) f->accent = accent;
    if (f->accent == f->color)                        /* stripes the body's own colour would vanish */
        for (int i = 0; i < LOOK_N; i++) if (LOOK_ACCENT[i] != f->color) { f->accent = LOOK_ACCENT[i]; break; }
}
void tank_set_look(tank_t *t, int slot, uint32_t body, uint32_t accent) {
    if (slot < 0 || slot >= N_FISH_MAX) return;
    tank_fish_set_look(&t->fish[slot], body, accent);
}

void tank_fish_face(fish_t *f) {
    f->facing = cosf(f->heading) < 0 ? -1 : 1;
    f->yaw = f->yaw_tail = f->facing; f->behind = 0; f->turn_vel = 0;
}

void tank_make_fish(tank_t *t, int slot, int preset, float sociable, float bold, stage_t stage) {
    memset(&t->companion.active[slot], 0, sizeof t->companion.active[slot]);
    memset(&t->polish.active[slot], 0, sizeof t->polish.active[slot]);
    memset(&t->story.active[slot], 0, sizeof t->story.active[slot]);
    t->fish[slot].traits = 0;
    fish_t *f = &t->fish[slot];
    const preset_t *p = &ROSTER[preset];
    f->preset = preset; tank_set_name(t, slot, p->name);
    f->model_name = TRAINED_NAMES[slot % N_TRAINED_NAMES];   /* names carry no signal */
    f->x = tank_randf(t, TANK_FX0 + 90, TANK_FX1 - 90); f->y = tank_randf(t, 80, TANK_BOT - 90);
    f->heading = tank_randf(t, 0, TAU); tank_fish_face(f);
    f->speed = 0; f->target_speed = 0; f->speed_vel = 0; f->wander = f->x * 0.05f; f->swim_phase = f->wander;
    f->base_size = p->size; f->size = p->size; f->turn_rate = p->turn_rate;
    f->hunger = tank_randf(t, 3, 6); f->energy = tank_randf(t, 5, 8);
    f->stress = tank_randf(t, 4.5f, 6.5f); f->curiosity = p->curiosity;   /* a newcomer arrives unsettled: calm is cultivated */
    f->sociable = sociable; f->bold = bold; f->lazy = p->lazy;
    f->bold0 = bold; f->sociable0 = sociable; f->drift_acc = 0;
    f->stage = stage;
    f->starve_flagged = false;
    f->trust = 5.0f;
    f->goal.id = GOAL_EXPLORE; f->goal.urgency = 3;
    f->goal_age = 0; f->ask_age = 99; f->dart_timer = 0; f->dart_x = f->x; f->dart_y = f->y;
    f->bored = 0; f->goal_prev = GOAL_COUNT; f->zone_last = -1; f->explore_set = false; f->breather = 0;
    /* its own order of first visits - from the slot, not the tank's RNG, so a
     * seeded run (the selftests) draws the same numbers it did before */
    for (int z = 0; z < 6; z++) f->zone_seen[z] = -(float)((slot * 37 + z * 13) % 60);
    f->eaten = 0; f->eaten_player = 0; f->at_bubbles = false;
    /* its own spot by the reef: bolder fish rest a little further out */
    f->rest_dx = 8 + slot * 24 + bold * 14; f->rest_dy = -slot * 9 - tank_randf(t, 0, 10);   /* a body apart (was 9 px per slot) */
    memset(f->familiar, 0, sizeof f->familiar);
    memset(f->familiar_id, 0, sizeof f->familiar_id);
    static uint32_t next_social_id;
    f->social_id = ++next_social_id;
    f->social_cooldown = 0;
    f->sig = 0xffffffffu; f->ms_bits = 0; f->ms_seen = 0;
    f->color = p->color; f->fin = p->fin; f->accent = p->accent;
    f->variety = FISH_CLASSIC; f->pattern = PATTERN_STRIPES; f->wearable = WEAR_NONE; f->wear_colors = 0;
    f->parent_a = f->parent_b = -1;
    if (t->story.version) tank_seed_genes(t, slot, t->rng ^ (uint32_t)preset * 0x27d4eb2du);
}

void tank_set_bubble_x(tank_t *t, float x) {
    /* clear of the grass corner's spot (the fish's other landmark) and of the glass;
       grass is fine - the beds cover most of the width and the default spot
       already rises through one */
    float lo = t->reef_x + 50, hi = TANK_FX1 - 30;
    if (x < lo) x = lo;
    if (x > hi) x = hi;
    float dx = x - t->bubble_x;
    t->bubble_x = x;
    for (int i = 0; i < MAX_BUBBLE; i++) if (t->bubble[i].column) t->bubble[i].x += dx;   /* the column moves as one */
}

static void place_near_reef(tank_t *t, fish_t *f) {
    f->x = t->reef_x + tank_randf(t, -10, 30); f->y = t->reef_y - 30 + tank_randf(t, -10, 10);
    f->heading = tank_randf(t, -0.6f, 0.6f); tank_fish_face(f);
}

void tank_new_population(tank_t *t) {
    /* two random presets */
    int a = (int)tank_randf(t, 0, ROSTER_N - 0.001f);
    int b = (int)tank_randf(t, 0, ROSTER_N - 1.001f); if (b >= a) b++;
    /* personalities rolled with a guaranteed contrast so the pair reads as
     * two characters at a glance */
    float ba, bb, sa, sb;
    do { ba = tank_randf(t, 0.1f, 0.9f); bb = tank_randf(t, 0.1f, 0.9f); } while (fabsf(ba - bb) < 0.45f);
    do { sa = tank_randf(t, 0.1f, 0.9f); sb = tank_randf(t, 0.1f, 0.9f); } while (fabsf(sa - sb) < 0.3f);
    /* both start as FRY: the founding pair grows up on the keeper's watch
     * (progression halved the stage clocks to match - 0.5/3/24 tended hours) */
    tank_make_fish(t, 0, a, sa, ba, STAGE_FRY);
    tank_make_fish(t, 1, b, sb, bb, STAGE_FRY);
    /* New aquariums begin with the simple first-tier Tetra; existing residents keep their saved variety. */
    t->fish[0].variety = t->fish[1].variety = FISH_TETRA;
    t->n_fish = 2;
    tank_give_name(t, 0, 0x51u);
    tank_give_name(t, 1, 0x52u);
}

/* ---- genetics (1.3) ----------------------------------------------------
 * Cosmetic only. A stable hash drives every roll, so breeding draws nothing
 * from the tank's rng and older behaviour keeps its sequence. */
static uint32_t gene_hash(uint32_t x) {
    x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15; x *= 0x846ca68bu; x ^= x >> 16; return x;
}
int tank_body_index(uint32_t rgb) {
    int best = 0; long best_d = -1;
    for (int i = 0; i < FISH_COLOR_N; i++) {
        long dr = (long)((rgb >> 16) & 255) - (long)((FISH_BODY_COLORS[i] >> 16) & 255);
        long dg = (long)((rgb >> 8) & 255) - (long)((FISH_BODY_COLORS[i] >> 8) & 255);
        long db = (long)(rgb & 255) - (long)(FISH_BODY_COLORS[i] & 255);
        long d = dr * dr + dg * dg + db * db;
        if (best_d < 0 || d < best_d) { best_d = d; best = i; }
    }
    return best;
}
void tank_seed_line(fish_lineage_t *l, uint32_t salt) {
    uint32_t h = gene_hash(salt * 0x9e3779b1u ^ 0x85ebca6bu);
    l->pattern2 = (h & 0xff) < 90 ? (uint8_t)(1 + (h >> 8) % FISH_PATTERN_COUNT) : 0;          /* ~35% */
    h = gene_hash(h);
    l->color2 = (h & 0xff) < 77 ? (uint8_t)(1 + (h >> 8) % FISH_COLOR_N) : 0;                   /* ~30% */
    h = gene_hash(h);
    l->traits = 0;
    for (int k = 0; k < TRAIT_COUNT; k++) if (((h >> (k * 8)) & 0xff) < 51) l->traits |= (uint8_t)(16u << k);  /* ~20% carriers */
    l->mutation = 0;
}
void tank_seed_genes(tank_t *t, int slot, uint32_t salt) {
    if (slot < 0 || slot >= N_FISH_MAX) return;
    tank_seed_line(&t->story.active[slot], salt ^ (uint32_t)slot * 0x85ebca6bu ^ t->fish[slot].color);
    t->fish[slot].traits = 0;
}
void tank_sync_traits(tank_t *t) {
    for (int i = 0; i < N_FISH_MAX; i++) t->fish[i].traits = i < t->n_fish ? (uint8_t)(t->story.active[i].traits & 15) : 0;
}
/* Vigor 0..10 is hybrid vigor: each heterozygous locus (a hidden marking, a
 * hidden body color or a carried trait) adds diversity. Line-bred pairs that
 * share alleles converge toward homozygous, lower-vigor offspring. */
int tank_line_vigor(const fish_lineage_t *l) {
    if (!l) return 0;
    int carried = 0;
    for (int k = 0; k < TRAIT_COUNT; k++) carried += !!(l->traits & (16u << k));
    int vigor = 2 + (l->pattern2 ? 3 : 0) + (l->color2 ? 3 : 0) + carried;
    return vigor > 10 ? 10 : vigor;
}
static int pattern_rank(int p) {                  /* the classic marks dominate; plain is recessive */
    return p == PATTERN_PLAIN ? 0 : p <= PATTERN_BAND ? 2 : 1;
}
static uint32_t blend_rgb(uint32_t a, uint32_t b) {
    return (((a >> 16 & 255) + (b >> 16 & 255)) / 2) << 16 | (((a >> 8 & 255) + (b >> 8 & 255)) / 2) << 8 | ((a & 255) + (b & 255)) / 2;
}
static int pass_trait(int shown, int carried, uint32_t roll) {
    return shown ? 1 : carried ? (int)(roll & 1) : 0;
}
/* called once the coin has picked which parent lends the body (a) and the markings (b) */
static void inherit(tank_t *t, int slot, int a, int b) {
    fish_t *child = &t->fish[slot];
    const fish_t *fa = &t->fish[a], *fb = &t->fish[b];
    const fish_lineage_t *la = &t->story.active[a], *lb = &t->story.active[b];
    fish_lineage_t *l = &t->story.active[slot];
    uint32_t h = gene_hash(t->rng ^ (uint32_t)t->story.births * 0x9e3779b1u ^ (uint32_t)slot * 0x632be5abu);
    /* marking: one allele from each parent, the higher rank shows */
    int pa = (h & 1) && la->pattern2 ? la->pattern2 - 1 : fa->pattern;
    int pb = (h & 2) && lb->pattern2 ? lb->pattern2 - 1 : fb->pattern;
    int shown = pattern_rank(pa) > pattern_rank(pb) ? pa : pattern_rank(pb) > pattern_rank(pa) ? pb : (h & 4) ? pa : pb;
    int hidden = shown == pa ? pb : pa;
    l->mutation = 0;
    if ((h >> 3) % 14 == 0) {                     /* ~7%: a brand-new marking shows */
        hidden = shown;
        shown = PATTERN_SADDLE + (int)((h >> 8) % (FISH_PATTERN_COUNT - PATTERN_SADDLE));
        l->mutation |= GENE_MUTANT_PATTERN;
    }
    child->pattern = (uint8_t)shown;
    l->pattern2 = hidden == shown ? 0 : (uint8_t)(hidden + 1);
    /* body colour: one allele from each, a coin shows one; sometimes they blend */
    h = gene_hash(h);
    uint32_t ca = (h & 1) && la->color2 ? FISH_BODY_COLORS[(la->color2 - 1) % FISH_COLOR_N] : fa->color;
    uint32_t cb = (h & 2) && lb->color2 ? FISH_BODY_COLORS[(lb->color2 - 1) % FISH_COLOR_N] : fb->color;
    uint32_t body = (h & 4) ? cb : ca, other = (h & 4) ? ca : cb;
    if ((h >> 3) % 5 == 0) body = blend_rgb(ca, cb);
    if ((h >> 12) % 16 == 0) {
        int first = (int)((h >> 16) % FISH_COLOR_N);
        for (int step = 0; step < FISH_COLOR_N; step++) {
            uint32_t color = FISH_BODY_COLORS[(first + step) % FISH_COLOR_N];
            if (color == ca || color == cb || color == body) continue;
            other = body; body = color; l->mutation |= GENE_MUTANT_COLOR;
            break;
        }
    }
    int oi = tank_body_index(other);
    l->color2 = FISH_BODY_COLORS[oi] == body ? 0 : (uint8_t)(oi + 1);
    tank_set_look(t, slot, body, (h & 8) ? fb->accent : fa->accent);
    /* traits: recessive - shown only with an allele from both parents */
    h = gene_hash(h);
    uint8_t traits = 0;
    for (int k = 0; k < TRAIT_COUNT; k++) {
        int got = pass_trait(fa->traits & (1 << k) || la->traits & (1 << k), la->traits & (16 << k), h >> (k * 2)) +
                  pass_trait(fb->traits & (1 << k) || lb->traits & (1 << k), lb->traits & (16 << k), h >> (k * 2 + 1));
        if (got == 2) traits |= (uint8_t)(1u << k); else if (got == 1) traits |= (uint8_t)(16u << k);
    }
    h = gene_hash(h);
    if (h % 12 == 0) {                            /* ~8%: a new trait allele appears */
        int k = (int)((h >> 8) % TRAIT_COUNT);
        if (traits & (16u << k)) traits = (uint8_t)((traits & ~(16u << k)) | (1u << k)); else traits |= (uint8_t)(16u << k);
        l->mutation |= GENE_MUTANT_TRAIT;
    }
    l->traits = traits;
    child->traits = traits & 15;
}

int tank_add_fish(tank_t *t, int parent_a, int parent_b) {
    if (t->n_fish + t->companion.away >= N_FISH_MAX || t->n_fish + t->companion.away >= ROSTER_N) return -1;
    if (parent_a < 0 || parent_b < 0 || parent_a >= t->n_fish ||
        parent_b >= t->n_fish || parent_a == parent_b ||
        t->fish[parent_a].variety != t->fish[parent_b].variety) return -1;
    int used[ROSTER_N] = {0};
    for (int i = 0; i < t->n_fish; i++) used[t->fish[i].preset] = 1;
    int free_n = 0, free_idx[ROSTER_N];
    for (int i = 0; i < ROSTER_N; i++) if (!used[i]) free_idx[free_n++] = i;
    if (!free_n) return -1;
    int preset = free_idx[(int)tank_randf(t, 0, free_n - 0.001f)];
    int ia = parent_a, ib = parent_b;
    const fish_t *pa = &t->fish[ia], *pb = &t->fish[ib];
    float bold = clampf((pa->bold + pb->bold) * 0.5f + tank_randf(t, -0.15f, 0.15f), 0.05f, 0.95f);
    float soc  = clampf((pa->sociable + pb->sociable) * 0.5f + tank_randf(t, -0.15f, 0.15f), 0.05f, 0.95f);
    int slot = t->n_fish;
    tank_make_fish(t, slot, preset, soc, bold, STAGE_FRY);
    /* its look is the family's, not the preset's (2026-09-14): the body from
       one parent, the markings from the other - a coin decides which is
       which; tank_set_look keeps the markings off a matching body */
    if (tank_randf(t, 0, 1) < 0.5f) { int x = ia; ia = ib; ib = x; pa = &t->fish[ia]; pb = &t->fish[ib]; }
    tank_set_look(t, slot, pa->color, pb->accent);
    t->fish[slot].variety = pa->variety; t->fish[slot].pattern = pb->pattern;
    t->fish[slot].parent_a = (int8_t)ia; t->fish[slot].parent_b = (int8_t)ib;
    if (t->story.version) {                       /* 1.3 family tree: the parents' life identities */
        fish_lineage_t *line = &t->story.active[slot];
        inherit(t, slot, ia, ib);
        uint32_t id_a = t->polish.active[ia].id, id_b = t->polish.active[ib].id;
        line->parent[0] = id_a <= 0xffff ? (uint16_t)id_a : 0;
        line->parent[1] = ib != ia && id_b <= 0xffff ? (uint16_t)id_b : 0;
        int gen = t->story.active[ia].generation > t->story.active[ib].generation ?
                  t->story.active[ia].generation : t->story.active[ib].generation;
        line->generation = (uint8_t)(gen < 254 ? gen + 1 : 255);
        if (t->story.births < 0xffff) t->story.births++;
        if (t->fish[slot].variety < FISH_VARIETY_COUNT)
            t->story.collection |= (uint32_t)(line->mutation & 7u) << (3 * t->fish[slot].variety);
    }
    place_near_reef(t, &t->fish[slot]);
    t->fish[slot].hunger = 4; t->fish[slot].trust = 4;
    t->n_fish++;
    tank_give_name(t, slot, 0xb0u);
    return slot;
}

/* a fry from the shop (0.3.5): no parents - a free roster slot, the look it
 * was offered in, a temperament of its own (nobody's to inherit), and in at
 * the surface under the middle of the glass, as if poured from the bag */
int tank_add_bought_fry(tank_t *t, uint32_t body, uint32_t accent) {
    if (t->n_fish + t->companion.away >= N_FISH_MAX || t->n_fish + t->companion.away >= ROSTER_N) return -1;
    int used[ROSTER_N] = {0};
    for (int i = 0; i < t->n_fish; i++) used[t->fish[i].preset] = 1;
    int free_n = 0, free_idx[ROSTER_N];
    for (int i = 0; i < ROSTER_N; i++) if (!used[i]) free_idx[free_n++] = i;
    if (!free_n) return -1;
    int preset = free_idx[(int)tank_randf(t, 0, free_n - 0.001f)];
    int slot = t->n_fish;
    tank_make_fish(t, slot, preset, tank_randf(t, 0.25f, 0.75f), tank_randf(t, 0.25f, 0.75f), STAGE_FRY);
    tank_set_look(t, slot, body, accent);
    fish_t *f = &t->fish[slot];
    f->parent_a = f->parent_b = -1;
    f->x = TANK_W * 0.5f + tank_randf(t, -30, 30); f->y = tank_glass_top(f->x) + 30;
    f->heading = tank_randf(t, 1.2f, 1.9f); tank_fish_face(f);   /* heading down into the water */
    f->hunger = 4; f->trust = 4;
    t->n_fish++;
    tank_give_name(t, slot, 0xf1u);
    return slot;
}

/* a departing parent's name stays in the story's small name book while a
 * family line still points at it; the oldest unreferenced entry makes room */
static bool story_referenced(const tank_story_t *s, uint16_t id) {
    for (int i = 0; i < N_FISH_MAX + FISH_BANK_CAP + 1; i++) {
        const fish_lineage_t *l = i < N_FISH_MAX ? &s->active[i] :
            i < N_FISH_MAX + FISH_BANK_CAP ? &s->bank[i - N_FISH_MAX] : &s->traveler;
        if (id && (l->parent[0] == id || l->parent[1] == id)) return true;
    }
    return false;
}
static bool rpg_referenced(const tank_rpg_t *s, uint16_t id) {
    for (int i = 0; id && i < s->chapter && i < 6; i++)
        if (s->contributors[i] == id) return true;
    return false;
}
void tank_story_remember(tank_t *t, uint32_t id, const char *name) {
    tank_story_t *s = &t->story;
    if (!s->version || !id || id > 0xffff ||
        (!story_referenced(s, (uint16_t)id) && !rpg_referenced(&t->rpg, (uint16_t)id))) return;
    for (int i = 0; i < t->bank_count; i++) if (t->polish.bank[i].id == id) return;   /* only moving */
    if (t->companion.away && t->polish.traveler.id == id) return;
    int slot = -1;
    for (int i = 0; i < STORY_NAME_BOOK && slot < 0; i++) if (s->names[i].id == id) slot = i;
    for (int i = 0; i < STORY_NAME_BOOK && slot < 0; i++) if (!s->names[i].id) slot = i;
    for (int i = 0; i < STORY_NAME_BOOK && slot < 0; i++)
        if (!story_referenced(s, s->names[i].id) && !rpg_referenced(&t->rpg, s->names[i].id)) slot = i;
    if (slot < 0) slot = 0;
    if (slot < STORY_NAME_BOOK - 1 && s->names[slot].id != id) {     /* newest last: shift the book */
        memmove(&s->names[slot], &s->names[slot + 1], (STORY_NAME_BOOK - 1 - slot) * sizeof s->names[0]);
        slot = STORY_NAME_BOOK - 1;
    }
    s->names[slot].id = (uint16_t)id;
    memset(s->names[slot].name, 0, sizeof s->names[slot].name);
    snprintf(s->names[slot].name, sizeof s->names[slot].name, "%s", name);
}

bool tank_remove_fish(tank_t *t, int idx) {
    if (idx < 0 || idx >= t->n_fish) return false;
    tank_story_remember(t, t->polish.active[idx].id, t->fish[idx].name);
    for (int i = idx; i < t->n_fish - 1; i++) {
        t->fish[i] = t->fish[i + 1];
        t->sd_paid_fish[i] = t->sd_paid_fish[i + 1];
        t->companion.active[i] = t->companion.active[i + 1];
        t->polish.active[i] = t->polish.active[i + 1];
        t->story.active[i] = t->story.active[i + 1];
        t->profile.starvation_s[i] = t->profile.starvation_s[i + 1];
        /* its spot by the reef is its slot's (tank_make_fish): a slot nearer,
           so the next arrival's spot is free again */
        t->fish[i].rest_dx -= 24; t->fish[i].rest_dy += 9;
    }
    t->n_fish--;
    t->sd_paid_fish[t->n_fish] = 0;
    t->profile.starvation_s[t->n_fish] = 0;
    memset(&t->companion.active[t->n_fish], 0, sizeof t->companion.active[0]);
    memset(&t->polish.active[t->n_fish], 0, sizeof t->polish.active[0]);
    memset(&t->story.active[t->n_fish], 0, sizeof t->story.active[0]);
    for (int i = 0; i < t->n_fish; i++) {
        fish_t *f = &t->fish[i];
        if (f->parent_a == idx) f->parent_a = -1; else if (f->parent_a > idx) f->parent_a--;
        if (f->parent_b == idx) f->parent_b = -1; else if (f->parent_b > idx) f->parent_b--;
    }
    t->courting = false; t->court_a = t->court_b = -1; t->court_active = 0;   /* progression picks the pair again */
    t->spawning = false; t->spawn_danced = 0;
    t->stage_fish = -1;
    t->roster_gen++;
    return true;
}

void tank_init(tank_t *t, uint32_t seed) {
    memset(&t->companion, 0, sizeof t->companion);
    memset(&t->games, 0, sizeof t->games);
    memset(&t->polish, 0, sizeof t->polish);
    memset(&t->story, 0, sizeof t->story);
    memset(&t->rpg, 0, sizeof t->rpg);
    memset(&t->creative, 0, sizeof t->creative);
    memset(&t->games_extra, 0, sizeof t->games_extra);
    memset(&t->profile, 0, sizeof t->profile);
    memset(&t->care, 0, sizeof t->care);
    memset(&t->reef, 0, sizeof t->reef);
    memset(&t->creatures, 0, sizeof t->creatures); t->creature_tick_s = 0;
    memset(&t->progression, 0, sizeof t->progression);
    memset(&t->foliage, 0, sizeof t->foliage); t->foliage.version = 1;
    t->care_fraction = 0;
    t->season = SEASON_NONE;
    t->visitor_left = 0;
    t->room_dark = false; t->visitor_species = 0;
    t->companion.version = 2;
    t->companion.cozy = t->companion.captions = 1;
    t->companion.lively = 0;
    t->companion.decor = 0;
    t->companion_fraction = 0;
    memset(&t->journal, 0, sizeof t->journal);
    memset(t->bank, 0, sizeof t->bank); t->bank_count = 0;
    memset(&t->aquariums, 0, sizeof t->aquariums); t->aquariums.count = 1;
    t->rng = seed ? seed : 0xC0FFEE;
    t->n_fish = 0;                 /* progression_boot restores or calls tank_new_population */
    for (int i = 0; i < MAX_FOOD; i++) t->food[i].alive = false;
    t->bubble_x = BUBBLE_X_DEFAULT; t->bubble_y = TANK_BOT * 0.5f;   /* matches gen_traces.py; setup may move x */
    for (int i = 0; i < MAX_BUBBLE; i++) {
        bubble_t *b = &t->bubble[i];
        b->column = i < 10;
        b->x = b->column ? t->bubble_x + tank_randf(t, -10, 10) : tank_randf(t, TANK_FX0 + 12, TANK_FX1 - 12);
        b->y = tank_randf(t, 0, TANK_BOT);
        b->vy = tank_randf(t, 14, 30);
        b->wobble = tank_randf(t, 0, TAU);
    }
    t->reef_x   = TANK_FX0 + TANK_FW * 0.15f; t->reef_y   = TANK_BOT * 0.85f;
    t->clock = 0; t->night = false; t->idle_s = 0;
    t->light_idle_s = LIGHT_IDLE_S; t->light_auto = false; t->light_manual_off = false; t->light_tip_seen = false;
    t->orient_lock = false; t->autofeed_off = false;
    t->screen_turned = false;
    t->light_override = false; t->light_on = true;
    t->hold_active = false; t->hold_time = 0; t->hold_approached = false;
    t->tap_count = 0; t->tap_burst_t = 99; t->startled = false;
    t->startle_cooldown = 0;
    t->feed_spot_x = -1; t->player_feedings = 0; t->feed_open = false; t->hold_approaches = 0; t->greet_timer = 0;
    t->courting = false; t->court_a = t->court_b = -1;
    t->court_cool = 30; t->court_active = 0;
    t->spawning = false; t->spawn_danced = 0; t->ui_cover = false;
    t->clock_idle_s = 0; t->clock_requested = false;
    t->ravenous = false; t->trickle_off = false;
    t->stage_fish = -1; t->hold_light = false;
    t->drag_active = false; t->drag_has_prev = false; t->drag_dist = 0;
    t->reef_currents = t->reef_visits = 0;
    t->reef_flow_s = t->reef_flow_x = t->reef_flow_y = t->reef_flow_dir = t->reef_flow_cooldown = 0;
    t->reef_flow_gain = 0;
    t->reef_flow_seen = 0;
    for (int b = 0; b < VEG_BEDS_MAX; b++) {
        /* a fresh tank's canopy has a natural profile: fronds within +-0.04
           of VEG_START, seeded per slot so it's the same tank every boot */
        for (int i = 0; i < VEG_FRONDS_MAX; i++) {
            uint32_t h = (uint32_t)((b * 17 + i + 1) * 2654435761u);
            t->veg_h[b][i] = VEG_START + ((int)(h >> 8 & 255) - 128) / 128.0f * 0.04f;
        }
    }
    veg_sync(t);
    t->slash_armed = t->slash_engaged = t->slash_cut = false;
    t->slash_h = t->slash_v = 0;
    t->tool = TOOL_HAND; t->tool_idle = 0; t->tools_locked = false;
    for (int i = 0; i < ALGAE_CELLS; i++) t->algae[i] = 0;
    t->algae_acc = 0; t->trims = 0; t->cells_cleaned = 0;
    t->algae_colonies = 0; t->trim_px = 0;
    t->sd_balance = t->sd_earned = 0; t->sd_unlocks = 0;
    for (int i = 0; i < N_FISH_MAX; i++) t->sd_paid_fish[i] = 0;
    t->sd_colonies_paid = t->sd_inches_paid = 0;
    t->snail_x = -1; t->snail_y = -1; t->snail_heading = 0; t->snail_cell = -1; t->snail_graze = 0;
    t->snail_grazed = 0; t->snail_sleep_acc = 0;
    t->shrimp_n = 0; t->shrimp_food = 0; t->shrimp_cool = 0; t->shrimp_eaten = 0;
    t->shrimp_taps = 0; t->shrimp_tap_t = 99;
    t->urchin_x = -1; t->urchin_bed = t->urchin_frond = -1; t->urchin_appetite = 0; t->urchin_chew = 0;
    t->urchin_to = -1; t->urchin_rest = 0; t->urchin_grazed_px = 0;
    t->sh_x = -1; t->sh_y = 0; t->sh_bed = t->sh_frond = -1; t->sh_mode = SH_DRIFT; t->sh_facing = 1;
    t->sh_akind = SH_ANCHOR_NONE; t->sh_aitem = -1; t->sh_ax = t->sh_ay = t->sh_ahw = t->sh_aref = 0; t->sh_front = false;
    t->sh_hitched = t->sh_credited = false; t->sh_tx = t->sh_ty = 0; t->sh_stay = 0; t->sh_look = 0; t->sh_vx = t->sh_vy = 0;
    t->sh_pose = SH_POSE_BLADE; t->sh_land = 0;
    t->sh_s = 0; t->sh_hitches = 0; t->sh_trust_passed = 0;
    t->sh_eaten = 0; t->sh_snack_pay = 0; t->sh_snack_cool = 0; t->sd_snacks_paid = 0;
    t->sh_snap = -1; t->sh_snap_food = -1; t->sh_turn = 0;
    t->plant_x = 0; t->plant_z = DECOR_Z_MIDDLE;
    t->castle_x = 0; t->castle_z = DECOR_Z_FRONT;
    t->coral_x = 0; t->coral_z = DECOR_Z_FRONT; t->coral_rgb = 0; t->coral_growth = 0; t->coral_acc = 0;
    t->cluster_x = 0; t->cluster_z = DECOR_Z_FRONT; t->cluster_scheme = 0; t->cluster_growth = 0; t->cluster_acc = 0;
    t->pumpkin_x = 0; t->pumpkin_z = DECOR_Z_BACK;
    t->chest_x = 0; t->chest_z = DECOR_Z_BACK;
    memset(t->ornament_x, 0, sizeof t->ornament_x);
    memset(&t->inventory, 0, sizeof t->inventory);
    memset(&t->mussel, 0, sizeof t->mussel);
    t->mussel_fraction = 0;
    t->mussel_glint = 0;
    memset(&t->game_income, 0, sizeof t->game_income);
    memset(t->ornament_z, DECOR_Z_BACK, sizeof t->ornament_z);
    t->sand_style = t->water_style = 0;
    memset(t->decor_rank, 0, sizeof t->decor_rank);
    memset(&t->content, 0, sizeof t->content); t->content.version = 1; memset(&t->fixed, 0, sizeof t->fixed); t->fixed.version = 1;
    t->tank_ms_bits = 0; t->tank_ms_seen = 0; t->ask_rr = 0; t->advisor_asks = 0;
    tank_scatter_food(t, 2);
}

#define TAP_WINDOW      0.5f    /* taps closer than this form a burst */
#define STARTLE_RADIUS  140.0f  /* fish this close to an aggressive tap bolt */
#define STARTLE_COOLDOWN 6.0f   /* calm seconds before the spook wears off */
/* a resting finger is seen from anywhere on the
                                 * glass (2026-09-04, was 160: a fish only came
                                 * when you held next to it, which hid the
                                 * trust tell) */
#define HOLD_RADIUS     1000.0f
/* hold_time before fish approach; platforms
                                 * report holds ~0.3 s in, so ≈3 s of contact */
#define HOLD_ATTRACT_S  2.7f
#define HOLD_HUNGER_VETO 7.5f   /* this hungry, a fish ignores the finger */
#define SPAWN_NEAR_PX   20.0f   /* past the courtship loop's reach: a parent this close is circling in the fronds */
/* a hold-approach must START at least this far out:
                                 * a fish already under the finger earns nothing */
#define HOLD_APPROACH_FROM 60.0f
#define HOLD_APPROACH_AT   30.0f /* ... and come in this close */
/* trust a fish earns a second under a resting finger (0.3.6: was 0.02;
                                   * Strato: "a bit slower" so the seahorse's double is worth having) */
#define TRUST_PER_S        0.012f
#define SEAHORSE_TRUST_X   2.0f   /* ... times this while the seahorse hangs on the finger */

/* ---- hunger economy (2026-09-01) ----
 * The prototype's per-second metabolism (0.15 + 0.12*bold: fed to starving
 * in under a minute) shipped unchanged into a tank the keeper tends for a
 * few minutes at a time - and the trickle that fed the prototype's single
 * fish was scaled per FRAME, so at the device's 25 fps it dropped 2.4x fewer
 * pellets than the 60 fps sim and could never keep up with four fish: the
 * school lived at hunger 9-10, begging under the surface (Strato: "ravenous
 * too often, hovering near the top, breaking the experience").
 * Now: a meal lasts ~5-6 minutes of awake time, and the tank's own trickle is
 * HUNGER-GATED and per-second - it drops a pellet only while somebody is
 * really hungry (a few seconds' wait), so an untended tank cycles between
 * "peckish" and "just fed" on its own and never reaches the ravenous
 * threshold (8.5, progression.c) awake. Ravenous begging is once again the
 * after-sleep / long-absence event it was designed to be (tank_tick_sleep's
 * 0.8/h), and the keeper's pellets are what make a fish FULL - play, bubbles,
 * the reef, a friend: the fed-fish repertoire. The advisor sees only banded
 * hunger, so its training distribution is untouched. */
#define HUNGER_PER_S       0.010f   /* fed -> starving in ~17 min (shy fish) */
/* the breather (2026-10-21, Strato's 1.8 after a reset: "sluggish", no "first
 * rest in the seagrass"; a keeper's tank the same for days): the v4
 * brain rests at NIGHT - its teacher rested a lit fish only at energy <= 1,
 * one roll in twenty - while the energy economy below (09-04) was tuned for a
 * brain that rested by day. So a lit tank ran its fish down to 0 in ten
 * minutes and kept them there, crawling at 45%, and a keeper who never turns
 * the light off never saw the seagrass badge. Physiology, like the famine's
 * begging: a spent fish ducks into the nearest canopy and hovers until it has
 * its wind back (the rest rate, ~10 s). The advisor still owns the goal. */
#define BREATHER_AT   1.0f    /* energy under this by day: a breather */
#define BREATHER_TO   4.0f    /* ... until this */
#define BREATHER_NEAR 22.0f   /* settled in the fronds within this: the wind comes back */
#define HUNGER_BOLD_PER_S  0.008f   /* bold fish burn hotter: ~9 min */
#define HUNGER_DART_PER_S  0.012f   /* play costs extra */
#define TRICKLE_HUNGER     7.5f     /* the tank feeds itself only past this (someone) */
#define TRICKLE_RATE       0.15f    /* pellets/s while the gate is open (~7 s wait) */
#define CURIOSITY_SPEND_PER_S 0.25f /* spent at the bubbles / the reef: 9 -> 3 in ~24 s */
#define CURIOSITY_SPEND_RADIUS 60.0f

#define WIPE_ENGAGE_PX 18.0f

/* Sword geometry is shared by the renderer and seahorse anchors. */
static int veg_segs(float h) { return 2 + (int)(h * (VEG_SEGS_FULL - 2)); }
static int veg_pitch(int b) { (void)b; return 14; }
static void veg_bed_base(const tank_t *t, int b, float *bx0, int *n) {
    *bx0 = b == 3 ? tank_decor_x(t, 0) - PLANT_HALF_W : t->reef_x;
    *n = b == 3 ? 4 : 0;
}
int tank_veg_beds(const tank_t *t) { return (t->sd_unlocks & SD_ITEM_PLANT) ? VEG_BEDS_MAX : 0; }
veg_kind_t tank_veg_kind(const tank_t *t, int b) { (void)t; return b == 3 ? VEG_KIND_SWORD : VEG_KIND_GRASS; }
void tank_veg_bed(const tank_t *t, int b, float *x0, float *x1, float *top_y, int *fronds) {
    if (b != 3 || !(t->sd_unlocks & SD_ITEM_PLANT)) {
        if (x0) *x0 = t->reef_x;
        if (x1) *x1 = t->reef_x;
        if (top_y) *top_y = TANK_BOT;
        if (fronds) *fronds = 0;
        return;
    }
    float bx0; int n;
    veg_bed_base(t, b, &bx0, &n);
    float hmax = 0;
    for (int i = 0; i < n; i++) if (t->veg_h[b][i] > hmax) hmax = t->veg_h[b][i];
    if (x0) *x0 = bx0 - 6;
    if (x1) *x1 = bx0 + n * veg_pitch(b) + 6;
    if (top_y) *top_y = TANK_BOT - 16 - veg_segs(hmax) * VEG_SEG_PX - 4;
    if (fronds) *fronds = n;
}
int tank_nursery_bed(const tank_t *t) {
    /* A bare tank can breed over open sand; zero is a legacy site sentinel. */
    return (t->sd_unlocks & SD_ITEM_PLANT) ? 3 : 0;
}
/* where the pair circles: low inside the nursery bed, a flat loop that weaves
 * the fronds (body centre ~14 px off the floor line) */
static void court_site(const tank_t *t, float *cx, float *cy, float *rx) {
    *cx = (t->sd_unlocks & SD_ITEM_PLANT) ? tank_decor_x(t, 0) : t->reef_x;
    *cy = TANK_BOT - 42; *rx = 24;
}
void tank_court_puff(tank_t *t, int n) {
    float sx, sy, sr; court_site(t, &sx, &sy, &sr);
    for (int i = 0; i < MAX_BUBBLE && n > 0; i++) {
        if (t->bubble[i].column) continue;
        t->bubble[i].x = sx + tank_randf(t, -10, 10);
        t->bubble[i].y = sy - 6;
        n--;
    }
}
/* the courtship circle, shared by the tell's episodes and the spawning: the
 * pair weaves a low loop through the nursery on opposite sides - performed
 * rather than printed (reflex presentation; the advisor still owns both
 * goals). Down in the fronds, not mid-water: Strato (2026-09-04) - by the
 * reef it read as two friends at the bubbles. */
static bool reef_shelter_at(const tank_t *t, int slot, float *x, float *half) {
    int item;
    if (slot < SD_ITEM_COUNT) {
        item = slot;
        if (!tank_decor_owned(t, item)) return false;
        *x = tank_decor_x(t, item);
    } else {
        const inventory_extra_t *copy = &t->inventory.extra[slot - SD_ITEM_COUNT];
        if (!copy->item || copy->item > SD_ITEM_COUNT) return false;
        item = copy->item - 1;
        *x = copy->x;
    }
    *half = tank_decor_half_w(item);
    return tank_decor_movable(item) && !tank_decor_surface(item) && *half >= 18;
}

/* Rest near the closest authored shelter, including placed extra copies. */
static void breather_spot(const tank_t *t, const fish_t *f, int idx, float *x, float *y) {
    float bx = TANK_W * 0.5f, bd = TANK_W;
    for (int slot = 0; slot < SD_ITEM_COUNT + DECOR_EXTRA_CAP; slot++) {
        float cx, half;
        if (!reef_shelter_at(t, slot, &cx, &half)) continue;
        float d = fabsf(cx - f->x);
        if (d < bd) { bd = d; bx = cx; }
    }
    *x = bx + (idx % 2 ? 14 : -14) + (idx / 2) * 10;
    *y = TANK_BOT - 16 - 14 - (idx % 3) * 6;
    tank_glass_clamp(x, y, 20);
}
static void court_steer(const tank_t *t, const fish_t *f, int idx, float *desired, float *speed) {
    float ph = t->clock * 1.7f + (idx == t->court_b ? 3.14159f : 0);
    float cx, cy, rx; court_site(t, &cx, &cy, &rx);
    float wx = cx + cosf(ph) * rx, wy = cy + sinf(ph) * 6;
    float to = atan2f(wy - f->y, wx - f->x);
    *desired = norm_ang(*desired + norm_ang(to - *desired) * 0.85f);
    *speed = tank_dist(f->x, f->y, cx, cy) > 90 ? 46 : 30;
}
int tank_veg_frond(const tank_t *t, int b, int i, float *x) {
    if (b != 3 || !(t->sd_unlocks & SD_ITEM_PLANT)) {
        if (x) *x = t->reef_x;
        return 0;
    }
    float bx0; int n;
    veg_bed_base(t, b, &bx0, &n);
    if (x) *x = bx0 + i * veg_pitch(b);
    return veg_segs(t->veg_h[b][i]);
}
/* veg_growth[b] is the bed's mean frond height - the comfort band, the save
 * and the firmware log read it; recomputed after anything moves a frond */
static void veg_sync(tank_t *t) {
    for (int b = 0; b < VEG_BEDS_MAX; b++) {
        if (b != 3) {
            memset(t->veg_h[b], 0, sizeof t->veg_h[b]);
            t->veg_growth[b] = 0;
            continue;
        }
        float bx0; int n; veg_bed_base(t, b, &bx0, &n);
        float sum = 0;
        for (int i = 0; i < n; i++) {
            t->veg_h[b][i] = 0.44f + (float)((i * 13 + 7) % 17) / 170.0f;
            sum += t->veg_h[b][i];
        }
        t->veg_growth[b] = sum / n;
    }
}
/* a frond slot's own number in 0..1, one per salt. A full mix: the plain
 * multiply this replaced stepped ~0.05 per frond, so every grown bed was a
 * smooth slope and not a ragged skyline (2026-10-05) */
static float veg_slot_rand(int b, int i, uint32_t salt) {
    uint32_t h = (uint32_t)(b * VEG_FRONDS_MAX + i) * 0x9E3779B9u ^ salt;
    h ^= h >> 16; h *= 0x7FEB352Du; h ^= h >> 15; h *= 0x846CA68Bu; h ^= h >> 16;
    return (float)(h >> 8) / 16777215.0f;
}
/* frond i of bed b grows toward its own ceiling: a hash of the slot spread
 * over VEG_CAP_LO..VEG_CAP_HI (a different hash than the fresh tank's start
 * profile, so a tall start does not mean a tall ceiling) */
float tank_veg_cap(int b, int i) {
    float cap = VEG_CAP_LO + veg_slot_rand(b, i, 0x5EA62A55u) * (VEG_CAP_HI - VEG_CAP_LO);
/* the bowl closes in over the outer fronds: a tip stops 12 px under the glass
                                                              (the reef bed taken at its widest, the sword plant wherever it stands);
                                                              the watch's and the square's upper corners do the same to the beds by its walls */
    return cap;
}

void tank_veg_sync(tank_t *t) { veg_sync(t); }
void tank_veg_set(tank_t *t, int b, float g) {
    if (b < 0 || b >= VEG_BEDS_MAX) return;
    for (int i = 0; i < VEG_FRONDS_MAX; i++) t->veg_h[b][i] = g;
    veg_sync(t);
}

int tank_reef_shelters(const tank_t *t) {
    int n = 0;
    for (int slot = 0; slot < SD_ITEM_COUNT + DECOR_EXTRA_CAP; slot++) {
        float x, half;
        if (reef_shelter_at(t, slot, &x, &half)) n++;
    }
    return n;
}
float tank_reef_open_lane(const tank_t *t) {
    int open = 0;
    for (int lane = 0; lane < 12; lane++) {
        float x = TANK_FX0 + TANK_FW * (lane + 0.5f) / 12;
        bool blocked = false;
        for (int slot = 0; slot < SD_ITEM_COUNT + DECOR_EXTRA_CAP && !blocked; slot++) {
            float cx, half;
            if (reef_shelter_at(t, slot, &cx, &half) && fabsf(x - cx) < half)
                blocked = true;
        }
        if (!blocked) open++;
    }
    return open / 12.0f;
}
bool tank_reef_calm_near(const tank_t *t, float x, float y) {
    if (y < TANK_BOT - 100 || y > TANK_BOT) return false;
    for (int slot = 0; slot < SD_ITEM_COUNT + DECOR_EXTRA_CAP; slot++) {
        float cx, half;
        if (reef_shelter_at(t, slot, &cx, &half) && fabsf(x - cx) < half + 10)
            return true;
    }
    return false;
}
bool tank_reef_current(tank_t *t, float x0, float y0, float x1, float y1) {
    (void)t; (void)x0; (void)y0; (void)x1; (void)y1;
    return false; /* Reserved API; historical current counters stay readable. */
}

bool tank_in_grass(const tank_t *t, float x, float y) {
    return tank_reef_calm_near(t, x, y);
}

void tank_reef_spot(const tank_t *t, float *x, float *y) {
    if (t->sd_unlocks & SD_ITEM_CLUSTER) {         /* the cluster stands ~75 px off the floor (more with its bloom): the
                                                      orbit, REEF_ORBIT_UP above this, rides round its crown */
        *x = tank_decor_x(t, 4); *y = TANK_BOT - 50;
    } else {
        *x = TANK_W * 0.5f; *y = TANK_BOT - 50;
        for (int slot = 0; slot < SD_ITEM_COUNT + DECOR_EXTRA_CAP; slot++) {
            float cx, half;
            if (reef_shelter_at(t, slot, &cx, &half)) { *x = cx; break; }
        }
    }
}

/* Reserved diagnostic entry points for old console/save consumers. */
int tank_algae_cells(void) { return ALGAE_CELLS; }
float tank_algae_cover(const tank_t *t) { (void)t; return 0; }
void tank_grow_algae(tank_t *t, int steps) { (void)t; (void)steps; }

/* Ornamental wanderers do not maintain the habitat or earn chore credit. */
#define SNAIL_MARGIN 30.0f
#define SNAIL_TAP_RADIUS 48.0f
#define URCHIN_TAP_RADIUS 40.0f
#define URCHIN_HALF_W 16.0f
static float urchin_lo(void) { return TANK_FX0 + DECOR_MARGIN + URCHIN_HALF_W; }
static float urchin_hi(void) { return TANK_FX1 - DECOR_MARGIN - URCHIN_HALF_W; }
bool tank_creature_partner_pose(const tank_t *t, int creature, float *x, float *y) {
    if (creature < CREATURE_SNAIL || creature > CREATURE_SEAHORSE ||
        t->creatures.version != 1 ||
        t->creatures.stock[creature - CREATURE_SNAIL].adults < 2) return false;
    uint32_t bit = creature == CREATURE_SNAIL ? SD_ITEM_SNAIL :
        creature == CREATURE_URCHIN ? SD_ITEM_URCHIN : SD_ITEM_SEAHORSE;
    if (!(t->sd_unlocks & bit)) return false;
    float primary = creature == CREATURE_SNAIL ? t->snail_x :
        creature == CREATURE_URCHIN ? t->urchin_x : t->sh_x;
    if (!isfinite(primary) || primary < 0 ||
        (creature == CREATURE_SEAHORSE && t->sh_hidden)) return false;
    float offset = creature == CREATURE_SEAHORSE ? 36 : 42;
    float px = primary + (primary < TANK_W * .5f ? offset : -offset);
    float py = creature == CREATURE_SNAIL ? SNAIL_FLOOR_Y :
        creature == CREATURE_URCHIN ? URCHIN_FLOOR_Y :
        clampf(t->sh_y - SH_BODY_UP + 12, 30, TANK_BOT - 30);
    if (x) *x = clampf(px, TANK_FX0 + 26, TANK_FX1 - 26);
    if (y) *y = py;
    return true;
}
static bool creature_partner_hit(const tank_t *t, int creature, float x, float y, float radius) {
    float px, py;
    return tank_creature_partner_pose(t, creature, &px, &py) &&
        tank_dist(px, py, x, y) <= radius;
}
bool tank_snail_upright(const tank_t *t) { (void)t; return true; }
bool tank_snail_hit(const tank_t *t, float x, float y) {
    return (t->sd_unlocks & SD_ITEM_SNAIL) && t->snail_x >= 0 &&
        (tank_dist(t->snail_x, t->snail_y, x, y) <= SNAIL_TAP_RADIUS ||
         creature_partner_hit(t, CREATURE_SNAIL, x, y, SNAIL_TAP_RADIUS));
}
void tank_snail_place(tank_t *t) {
    t->snail_x = TANK_FX0 + SNAIL_MARGIN + 10;
    t->snail_y = SNAIL_FLOOR_Y; t->snail_heading = 0;
    t->snail_cell = -1; t->snail_graze = 0; t->snail_front = true;
}
static void snail_tick(tank_t *t, float dt) {
    if (!(t->sd_unlocks & SD_ITEM_SNAIL)) return;
    if (t->snail_x < 0) tank_snail_place(t);
    t->snail_cell = -1; t->snail_graze = 0; t->snail_y = SNAIL_FLOOR_Y;
    if (fmodf(t->clock, 24) < 5) return;
    float dir = cosf(t->snail_heading) < 0 ? -1 : 1;
    t->snail_x += dir * 3 * dt;
    if (t->snail_x <= TANK_FX0 + SNAIL_MARGIN) {
        t->snail_x = TANK_FX0 + SNAIL_MARGIN; t->snail_heading = 0;
        t->snail_front = !t->snail_front;
    } else if (t->snail_x >= TANK_FX1 - SNAIL_MARGIN) {
        t->snail_x = TANK_FX1 - SNAIL_MARGIN; t->snail_heading = 3.14159f;
        t->snail_front = !t->snail_front;
    }
}
bool tank_urchin_chewing(const tank_t *t) { (void)t; return false; }
bool tank_urchin_hit(const tank_t *t, float x, float y) {
    return (t->sd_unlocks & SD_ITEM_URCHIN) && t->urchin_x >= 0 &&
        (tank_dist(t->urchin_x, URCHIN_FLOOR_Y, x, y) <= URCHIN_TAP_RADIUS ||
         creature_partner_hit(t, CREATURE_URCHIN, x, y, URCHIN_TAP_RADIUS));
}
void tank_urchin_place(tank_t *t) {
    t->urchin_x = clampf(t->reef_x + 45, urchin_lo(), urchin_hi());
    t->urchin_bed = t->urchin_frond = -1;
    t->urchin_chew = t->urchin_appetite = 0;
    t->urchin_to = -1; t->urchin_rest = 20;
}
static void urchin_tick(tank_t *t, float dt) {
    if (!(t->sd_unlocks & SD_ITEM_URCHIN)) return;
    if (t->urchin_x < 0) tank_urchin_place(t);
    t->urchin_bed = t->urchin_frond = -1;
    t->urchin_chew = t->urchin_appetite = 0;
    if (t->urchin_rest > 0) { t->urchin_rest = fmaxf(0, t->urchin_rest - dt); return; }
    if (t->urchin_to < 0) t->urchin_to = tank_randf(t, urchin_lo(), urchin_hi());
    float d = t->urchin_to - t->urchin_x;
    t->urchin_x += (d > 0 ? 1 : -1) * fminf(fabsf(d), 1.12f * dt);
    if (fabsf(d) < 0.5f) { t->urchin_to = -1; t->urchin_rest = tank_randf(t, 40, 120); }
}

/* ---- the seahorse (0.3.6, SD_ITEM_SEAHORSE; tank.h has the story) ----
 * Three things it does. PERCH: its tail round an ANCHOR (tank.h: a blade, a
 * post on a decoration) for a while, then it lets go and DRIFTs
 * to another at SH_DRIFT_PX_S - upright, slow, its dorsal fin doing the work.
 * A finger resting on the glass (the settled hold the fish answer,
 * HOLD_ATTRACT_S) makes it let go at once and swim to the finger's edge at
 * SH_SWIM_PX_S (the one hurry it has); within SH_AT_PX of the spot it is
 * HITCHED - a hitch counts once per hold, and every fish earns trust at
 * SEAHORSE_TRUST_X the base rate until the finger lifts. It lingers
 * SH_LINGER_S where the finger was, then drifts home. An anchor that goes
 * (a blade cut under SH_MIN_H, a piece moved or sold) drops it; it looks
 * again after a moment. In the dark it stays where it is. */
#define SH_DRIFT_PX_S   18.0f
#define SH_SWIM_PX_S    42.0f
#define SH_AT_PX        2.5f
#define SH_STAY_MIN     45.0f     /* on a blade (Strato 10-15: "move from spot to spot, blade to blade") */
#define SH_STAY_MAX     140.0f
#define SH_POST_STAY_MIN 60.0f    /* on a decoration */
#define SH_POST_STAY_MAX 180.0f
#define SH_HOVER_STAY_MIN 2.0f
#define SH_HOVER_STAY_MAX 5.0f
#define SH_LINGER_S     3.0f
/* the water (Strato 10-15: "it moves to a new location and stops abruptly
 * with no easing ... it should continue to slightly drift the direction it
 * was moving due to momentum"): it swims by velocity. The fin's thrust pulls
 * the velocity toward what it wants (a cruise toward the spot, easing off
 * inside SH_EASE_PX of it) with a lag of ~SH_LAG_S, so it builds speed,
 * coasts past a stop and settles. */
#define SH_LAG_S        0.55f
#define SH_EASE_PX      34.0f
#define SH_TAP_RADIUS   26.0f
#define SH_GRIP_SWORD   0.72f                /* a sword leaf is gripped higher, where it is narrow */
bool tank_kelp_anchor(const tank_t *t, int patch, float *x, float *y) {
    if (!t || patch < 0 || patch >= KELP_PATCH_COUNT ||
        !(t->foliage.planted & (1u << FOLIAGE_KELP))) return false;
    if (x) *x = TANK_W * (patch ? .74f : .26f);
    if (y) *y = TANK_SAND_TOP + 10 - KELP_HEIGHT * .6f;
    return true;
}
int tank_kelp_sway(const tank_t *t, int patch) {
    static const int sway[] = {0, 1, 1, 0, -1, -1};
    int phase = t->clock >= 0 && t->clock < 1e9f
        ? (int)(t->clock * 2) % 6
        : (int)fmodf(floorf(fmaxf(t->clock, 0) * 2), 6);
    return sway[(phase + patch * 2) % 6];
}
typedef struct { uint8_t kind, front; int8_t bed, frond, item; float x, y, hw; } sh_cand_t;
/* a frond it may hold: any frond tall enough. Its LANE comes from where the
 * frond is drawn: a grass bed is woven (even fronds behind the fish, odd in
 * front), the sword plant by its depth (BEHIND: all behind; AMONG: woven;
 * IN FRONT: all in front) */
static bool sh_frond_ok(const tank_t *t, int b, int i) {
    if (b != 3 || !(t->sd_unlocks & SD_ITEM_PLANT)) return false;
    float bx0; int n; veg_bed_base(t, b, &bx0, &n);
    return i >= 0 && i < n && t->veg_h[b][i] >= SH_MIN_H;
}
static bool sh_frond_front(const tank_t *t, int b, int i) {
    int z = tank_veg_kind(t, b) == VEG_KIND_SWORD ? tank_decor_z(t, 0) : DECOR_Z_MIDDLE;
    return z == DECOR_Z_FRONT || (z == DECOR_Z_MIDDLE && (i & 1));
}
/* the coil's spot on a blade: the spine's x (no sway: the renderer adds it)
 * and part of the way up the blade from the root line */
static void sh_grip(const tank_t *t, int b, int i, float *x, float *y) {
    int segs = tank_veg_frond(t, b, i, x);
    float frac = tank_veg_kind(t, b) == VEG_KIND_SWORD ? SH_GRIP_SWORD : SH_GRIP_FRAC;
    *y = TANK_BOT - 16 - segs * frac * VEG_SEG_PX;
}
/* the POSTS: solid parts of the decorations, at any depth - a piece IN FRONT
 * puts it in its front lane. Offsets from each piece's centre on its floor
 * line, hand-placed on the geometry in render.c. */
static tank_grip_source_t s_grip_source;
void tank_set_grip_source(tank_grip_source_t fn) { s_grip_source = fn; }
#define SH_POSTS_MAX 48
#define SH_GRIPS_PER_PIECE 3
static int sh_posts(const tank_t *t, sh_cand_t *out, int max) {
    int n = 0;
    #define POST(it, fr, px, py, phw) do { if (n < max) { out[n].kind = SH_ANCHOR_POST; out[n].front = (fr); out[n].bed = out[n].frond = -1; out[n].item = (it); out[n].x = (px); out[n].y = (py); out[n].hw = (phw); n++; } } while (0)
    if (t->sd_unlocks & SD_ITEM_CASTLE) {                                                /* the towers (castle_front_row / draw_castle): the coil round
                                                                                          * each CONE just under its peak (Strato 10-15: on the tower's
                                                                                          * body it blended with the stone - "curled around the peak"),
                                                                                          * so the body stands over the roofline in open water; the
                                                                                          * crenellated tower at its merlons */
        float cx = tank_decor_x(t, 2), fy = TANK_BOT - 16; bool fr = tank_decor_z(t, 2) == DECOR_Z_FRONT;
        POST(2, 0, cx + 39, fy - 128, 5);                                                /* the rear tower's cone (peak fy - 142): behind the wall at
                                                                                          * EITHER depth, never in the front row */
        POST(2, fr, cx - 48, fy - 96, 5);                                                /* the left tower's cone (peak fy - 108) */
        POST(2, fr, cx - 73, fy - 60, 5);                                                /* the far-left cone (peak fy - 72) */
        POST(2, fr, cx + 71, fy - 64, 10);                                               /* the far-right tower: its merlons (top fy - 70) */
    }
    if (t->sd_unlocks & SD_ITEM_CORAL) {                                                 /* the trunk (CORAL_SEGS, 2 px cells, y scale 0.88) */
        float cx = tank_decor_x(t, 3), fy = TANK_BOT - 14 + DECOR_SINK, g = tank_coral_growth(t); bool fr = tank_decor_z(t, 3) != DECOR_Z_BACK;
        POST(3, fr, cx + 1, fy - 18, 6);
        if (g >= 0.6f) POST(3, fr, cx + 3, fy - 47, 5);
    }
    if (t->sd_unlocks & SD_ITEM_CLUSTER) {                                               /* the tube sponges (CL_TUBES, 2 px cells about x 36) */
        float cx = tank_decor_x(t, 4), fy = TANK_BOT - 14 + DECOR_SINK; bool fr = tank_decor_z(t, 4) != DECOR_Z_BACK;
        float g = t->cluster_growth <= 0 ? 1 : t->cluster_growth > 1 ? 1 : t->cluster_growth;
        float sz = CLUSTER_SIZE_MIN + (1.0f - CLUSTER_SIZE_MIN) * g;
        POST(4, fr, cx + 33 * sz, fy - 40 * sz, 11 * sz); POST(4, fr, cx + 52 * sz, fy - 32 * sz, 10 * sz); POST(4, fr, cx + 17 * sz, fy - 27 * sz, 9 * sz);
    }
    /* every other standing piece: the peaks of its own art (render.c finds them: spires, masts,
     * towers, the tips of fronds), its lane its depth, mirrored with it */
    if (s_grip_source) for (int item = 9; item < SD_ITEM_COUNT; item++) {
        if (item == 13 || tank_decor_surface(item) || !tank_decor_movable(item) || !tank_decor_owned(t, item)) continue;
        uint8_t gx[SH_GRIPS_PER_PIECE], gy[SH_GRIPS_PER_PIECE], ghw[SH_GRIPS_PER_PIECE];
        int k = s_grip_source(item, gx, gy, ghw, SH_GRIPS_PER_PIECE);
        int w = tank_ornament_w(item), h = tank_ornament_h(item);
        float x0 = (float)((int)tank_decor_x(t, item) - w / 2);
        float y0 = tank_fixed_index(item) >= 0 ? TANK_BOT - (SD_IS_SCENERY(item) ? 52 : 48) : TANK_BOT - 14 + DECOR_SINK - h;
        bool fr = tank_decor_z(t, item) == DECOR_Z_FRONT, mir = tank_decor_mirrored(t, item);
        for (int j = 0; j < k; j++) POST(item, fr, x0 + (mir ? w - 1 - gx[j] : gx[j]) + 0.5f, y0 + gy[j], ghw[j]);
    }
    #undef POST
    return n;
}
/* is what it holds still there? (a piece or plant the keeper moved to another
 * depth under it keeps it: it goes with it to that lane) */
static bool sh_hold_ok(tank_t *t) {
    if (t->sh_akind == SH_ANCHOR_KELP) {
        t->sh_front = false;
        return tank_kelp_anchor(t, t->sh_frond, NULL, NULL);
    }
    if (t->sh_akind == SH_ANCHOR_BLADE) {
        if (!sh_frond_ok(t, t->sh_bed, t->sh_frond)) return false;
        t->sh_front = sh_frond_front(t, t->sh_bed, t->sh_frond);
        return true;
    }
    if (t->sh_akind == SH_ANCHOR_POST) {
        sh_cand_t c[SH_POSTS_MAX]; int n = sh_posts(t, c, SH_POSTS_MAX);
        for (int i = 0; i < n; i++)
            if (c[i].item == t->sh_aitem && fabsf(c[i].x - t->sh_ax) < 0.01f && fabsf(c[i].y - t->sh_ay) < 0.01f) { t->sh_front = c[i].front != 0; return true; }
        return false;
    }
    return false;
}
static void sh_take(tank_t *t, const sh_cand_t *c) {
    t->sh_akind = c->kind; t->sh_bed = c->bed; t->sh_frond = c->frond; t->sh_aitem = c->item;
    t->sh_ax = c->x; t->sh_ay = c->y; t->sh_ahw = c->hw;
    t->sh_aref = c->kind == SH_ANCHOR_POST ? tank_decor_x(t, c->item) : 0;
    t->sh_front = c->front != 0;                                           /* its lane switches as it lets go, not mid-hold */
    t->sh_tx = c->x; t->sh_ty = c->y;
}
/* an anchor to hold, from here: the NEAREST wins, a blade before a post (and
 * a taller blade a little sooner), with a little chance in it, so it hops
 * blade to blade through a bed and crosses to the next now and then (Strato
 * 10-15: after a cut "it shouldn't be out in open water for very long").
 * With no real anchor, it cruises upright between open-water hover points. */
static bool sh_pick(tank_t *t, float from_x, float from_y) {
    sh_cand_t best = { 0, 0, -1, -1, -1, 0, 0, 0 }; bool have = false; float bs = 0;
    for (int patch = 0; patch < KELP_PATCH_COUNT; patch++) {
        sh_cand_t c = {SH_ANCHOR_KELP, 0, -1, (int8_t)patch, -1, 0, 0, 1};
        if (!tank_kelp_anchor(t, patch, &c.x, &c.y) ||
            (t->sh_akind == SH_ANCHOR_KELP && t->sh_frond == patch)) continue;
        float score = -tank_dist(c.x, c.y, from_x, from_y) + 60 + tank_randf(t, 0, 30);
        if (!have || score > bs) { bs = score; best = c; have = true; }
    }
    for (int b = 0; b < tank_veg_beds(t); b++) {
        float bx0; int n; veg_bed_base(t, b, &bx0, &n);
        for (int i = 0; i < n; i++) {
            if (!sh_frond_ok(t, b, i) || (t->sh_akind == SH_ANCHOR_BLADE && b == t->sh_bed && i == t->sh_frond)) continue;
            sh_cand_t c = { SH_ANCHOR_BLADE, sh_frond_front(t, b, i), (int8_t)b, (int8_t)i, -1, 0, 0, 1.5f };
            sh_grip(t, b, i, &c.x, &c.y);
            float score = -tank_dist(c.x, c.y, from_x, from_y) + 60 + (t->veg_h[b][i] - SH_MIN_H) * 90 + tank_randf(t, 0, 30) - (c.front != t->sh_front ? 15 : 0);
            if (!have || score > bs) { bs = score; best = c; have = true; }
        }
    }
    sh_cand_t posts[SH_POSTS_MAX]; int np = sh_posts(t, posts, SH_POSTS_MAX);
    for (int i = 0; i < np; i++) {
        if (t->sh_akind == SH_ANCHOR_POST && fabsf(posts[i].x - t->sh_ax) < 0.01f && fabsf(posts[i].y - t->sh_ay) < 0.01f) continue;
        float score = -tank_dist(posts[i].x, posts[i].y, from_x, from_y) + tank_randf(t, 0, 30) - (posts[i].front != t->sh_front ? 15 : 0);   /* a lane change a little later */
        if (!have || score > bs) { bs = score; best = posts[i]; have = true; }
    }
    if (!have) {
        best.kind = SH_ANCHOR_NONE; best.front = t->sh_front;
        best.bed = best.frond = -1; best.item = -1;
        best.x = tank_randf(t, TANK_FX0 + 40, TANK_FX1 - 40);
        best.y = tank_randf(t, 70, TANK_BOT - 50); best.hw = 0;
    }
    sh_take(t, &best);
    return true;
}
/* anything to hold at all (a blade tall enough, a post on a piece behind)? */
static bool sh_any_hold(const tank_t *t) {
    if (tank_kelp_anchor(t, 0, NULL, NULL)) return true;
    sh_cand_t c[SH_POSTS_MAX];
    if (sh_posts(t, c, SH_POSTS_MAX) > 0) return true;
    for (int b = 0; b < tank_veg_beds(t); b++) {
        float bx0; int n; veg_bed_base(t, b, &bx0, &n);
        for (int i = 0; i < n; i++) if (sh_frond_ok(t, b, i)) return true;
    }
    return false;
}
/* a hold other than the one it has? (its stay over, with nowhere else to go
 * it keeps the one it has rather than dropping to the sand and climbing back) */
static bool sh_another_hold(const tank_t *t) {
    for (int patch = 0; patch < KELP_PATCH_COUNT; patch++)
        if (tank_kelp_anchor(t, patch, NULL, NULL) &&
            !(t->sh_akind == SH_ANCHOR_KELP && t->sh_frond == patch)) return true;
    sh_cand_t c[SH_POSTS_MAX]; int n = sh_posts(t, c, SH_POSTS_MAX);
    for (int i = 0; i < n; i++)
        if (!(t->sh_akind == SH_ANCHOR_POST && fabsf(c[i].x - t->sh_ax) < 0.01f && fabsf(c[i].y - t->sh_ay) < 0.01f)) return true;
    for (int b = 0; b < tank_veg_beds(t); b++) {
        float bx0; int nn; veg_bed_base(t, b, &bx0, &nn);
        for (int i = 0; i < nn; i++)
            if (sh_frond_ok(t, b, i) && !(t->sh_akind == SH_ANCHOR_BLADE && b == t->sh_bed && i == t->sh_frond)) return true;
    }
    return false;
}
static float sh_stay_for(tank_t *t) {
    return t->sh_akind == SH_ANCHOR_BLADE || t->sh_akind == SH_ANCHOR_KELP ? tank_randf(t, SH_STAY_MIN, SH_STAY_MAX)
         : t->sh_akind == SH_ANCHOR_POST  ? tank_randf(t, SH_POST_STAY_MIN, SH_POST_STAY_MAX)
         : tank_randf(t, SH_HOVER_STAY_MIN, SH_HOVER_STAY_MAX);
}
void tank_seahorse_place(tank_t *t) {
    float x0, x1; tank_veg_bed(t, 0, &x0, &x1, NULL, NULL);
    t->sh_x = clampf(x1 + 30, TANK_FX0 + 30, TANK_FX1 - 30); t->sh_y = TANK_BOT - 110;
    t->sh_akind = SH_ANCHOR_NONE; t->sh_bed = t->sh_frond = -1; t->sh_aitem = -1; t->sh_mode = SH_DRIFT; t->sh_facing = 1;
    t->sh_hitched = t->sh_credited = false; t->sh_stay = 0; t->sh_vx = t->sh_vy = 0;
    t->sh_pose = SH_POSE_BLADE; t->sh_land = 0;
    sh_pick(t, t->sh_x, t->sh_y);
    if (t->sh_akind != SH_ANCHOR_NONE) {
        t->sh_x = t->sh_tx; t->sh_y = t->sh_ty; t->sh_mode = SH_PERCH;
        t->sh_stay = sh_stay_for(t); t->sh_land = SH_LAND_S;
    }
}
const char *tank_seahorse_holding(const tank_t *t) {
    if (t->sh_akind == SH_ANCHOR_KELP) return "KELP";
    if (t->sh_akind == SH_ANCHOR_BLADE) return t->sh_bed >= 0 && tank_veg_kind(t, t->sh_bed) == VEG_KIND_SWORD ? "A LEAF" : "A BLADE";
    if (t->sh_akind == SH_ANCHOR_POST) return t->sh_aitem == 2 ? "THE CASTLE" : t->sh_aitem == 3 ? "THE CORAL" : t->sh_aitem == 4 ? "THE REEF" : "DECOR";
    return "OPEN WATER";
}
bool tank_seahorse_hit(const tank_t *t, float x, float y) {
    if (!(t->sd_unlocks & SD_ITEM_SEAHORSE) || t->sh_x < 0) return false;
    return tank_dist(t->sh_x + 4 * t->sh_facing, t->sh_y - SH_BODY_UP, x, y) <= SH_TAP_RADIUS ||
        creature_partner_hit(t, CREATURE_SEAHORSE, x, y, SH_TAP_RADIUS);
}
float tank_seahorse_days(const tank_t *t) { return t->sh_s / 86400.0f; }
/* the finger's edge: SH_HITCH_OFF px from the hold toward the middle of the
 * glass, kept inside the water */
static void sh_finger_spot(const tank_t *t, float *x, float *y) {
    float mid = (TANK_FX0 + TANK_FX1) * 0.5f;
    float dir = t->hold_x < mid ? 1.0f : -1.0f;
    *x = clampf(t->hold_x + dir * SH_HITCH_OFF, TANK_FX0 + 24, TANK_FX1 - 24);
    *y = clampf(t->hold_y, 70, TANK_BOT - 60);
}
/* a swim step: thrust toward the spot with the water's lag, coast, settle.
 * Inside SH_AT_PX it is drawn onto the spot (no snap: a pull that closes
 * the last pixels in ~0.4 s); arrived = on it, still. */
#define SH_ON_PX 0.3f
static bool sh_at(const tank_t *t) { return tank_dist(t->sh_x, t->sh_y, t->sh_tx, t->sh_ty) <= SH_ON_PX && t->sh_vx == 0 && t->sh_vy == 0; }
static void sh_move(tank_t *t, float dt, float px_s) {
    float dx = t->sh_tx - t->sh_x, dy = t->sh_ty - t->sh_y, d = sqrtf(dx * dx + dy * dy);
    if (d <= SH_AT_PX) {                                                 /* the last pixels: a pull, the thrust off */
        float k2 = 1 - expf(-dt * 9.0f);
        t->sh_x += dx * k2; t->sh_y += dy * k2; t->sh_vx *= 1 - k2; t->sh_vy *= 1 - k2;
        if (d < SH_ON_PX) { t->sh_x = t->sh_tx; t->sh_y = t->sh_ty; t->sh_vx = t->sh_vy = 0; }
        return;
    }
    float want = d < SH_EASE_PX ? px_s * d / SH_EASE_PX : px_s;          /* ease off into the spot */
    float wx = d > 0.01f ? dx / d * want : 0, wy = d > 0.01f ? dy / d * want : 0;
    float k = 1 - expf(-dt / SH_LAG_S);                                  /* the lag: speed builds and bleeds off */
    t->sh_vx += (wx - t->sh_vx) * k; t->sh_vy += (wy - t->sh_vy) * k;
    t->sh_x += t->sh_vx * dt; t->sh_y += t->sh_vy * dt;
    if (fabsf(t->sh_vx) > 4) t->sh_facing = t->sh_vx > 0 ? 1 : -1;      /* it faces the way it swims */
}
/* the hold's morph clock: up while settled on something, down once it lets go */
static void sh_land_tick(tank_t *t, float dt, bool settled) {
    t->sh_land = settled ? fminf(SH_LAND_S, t->sh_land + dt) : fmaxf(0, t->sh_land - dt);
}
/* it lets go of what it holds and heads for the next anchor */
static void sh_let_go(tank_t *t, float wait) {
    t->sh_mode = SH_DRIFT; t->sh_stay = wait;
    sh_pick(t, t->sh_x, t->sh_y);
}
/* a snack: a pellet within SH_SENSE of its head starts a SNATCH - first a
 * turn if the pellet is on its other side (a beat), then the head and neck
 * jut forward over SH_SNAP_S and the pellet is taken at the furthest point
 * if it is still within reach of the jutted snout (the snout sits ~16 px
 * ahead of the anchor and ~21 up), with the fish's eat cue. It never swims
 * for one. */
float tank_seahorse_jut(const tank_t *t) {
    if (t->sh_snap < 0) return 0;
    return SH_JUT * sinf(3.14159265f * fminf(1, t->sh_snap / SH_SNAP_S));
}
static void sh_snack(tank_t *t, float dt) {
    if (t->sh_snack_cool > 0) t->sh_snack_cool -= dt;
    if (t->sh_snap >= 0) {                                             /* a snatch under way */
        t->sh_snap += dt;
        if (t->sh_snap_food >= 0 && t->sh_snap >= SH_SNAP_AT) {        /* the head at its furthest: the bite */
            food_t *p = &t->food[t->sh_snap_food];
            float mx = t->sh_x + (16.0f + SH_JUT) * t->sh_facing, my = t->sh_y - 21.0f;
            if (p->alive && tank_dist(mx, my, p->x, p->y) <= SH_REACH + 3) {
                p->alive = false;                                      /* snapped: no fish meal, no shrimp pellet */
                t->sh_eaten++;
                if (t->sh_snack_cool <= 0) { t->sh_snack_pay++; t->sh_snack_cool = SH_SNACK_COOL_S; }
                for (int k = 0; k < MAX_BUBBLE; k++) {                 /* a bubble from the snout */
                    if (t->bubble[k].column) continue;
                    t->bubble[k].x = mx; t->bubble[k].y = my - 2;
                    break;
                }
                tank_emit(TEV_EAT, -1);                                /* the fish's eat cue, at the plain pitch */
            }
            t->sh_snap_food = -1;
        }
        if (t->sh_snap >= SH_SNAP_S) { t->sh_snap = -1; t->sh_turn = SH_SNAP_REST; }   /* back; a rest before the next */
        return;
    }
    if (t->sh_turn > 0) { t->sh_turn -= dt; if (t->sh_turn > 0) return; }
    float hx = t->sh_x + 6.0f * t->sh_facing, hy = t->sh_y - 24.0f;    /* the head's centre */
    for (int i = 0; i < MAX_FOOD; i++) {
        if (!t->food[i].alive || tank_dist(hx, hy, t->food[i].x, t->food[i].y) > SH_SENSE) continue;
        int side = t->food[i].x > t->sh_x + 2 ? 1 : t->food[i].x < t->sh_x - 2 ? -1 : t->sh_facing;
        if (side != t->sh_facing) { t->sh_facing = (int8_t)side; t->sh_turn = SH_TURN_S; return; }   /* it turns to it first */
        /* a snatch only for a pellet the jutted snout will reach: one neck
         * movement per pellet taken, never a jut at a cluster drifting past */
        float mx = t->sh_x + (16.0f + SH_JUT) * t->sh_facing, my = t->sh_y - 21.0f;
        if (tank_dist(mx, my, t->food[i].x, t->food[i].y) > SH_REACH + 1) continue;
        t->sh_snap = 0; t->sh_snap_food = (int8_t)i;
        return;
    }
}
static void seahorse_tick(tank_t *t, float dt) {
    if (!(t->sd_unlocks & SD_ITEM_SEAHORSE)) return;
    if (t->sh_x < 0) tank_seahorse_place(t);
    t->sh_s += dt;
    sh_snack(t, dt);                                               /* in the dark too: the fish eat then (0.3.7) */
    bool finger = t->hold_active && t->hold_time >= HOLD_ATTRACT_S;
    if (finger) {                                                  /* the one thing that moves it at once */
        if (t->sh_mode != SH_HITCH) { t->sh_mode = SH_HITCH; t->sh_akind = SH_ANCHOR_NONE; t->sh_bed = t->sh_frond = -1; t->sh_hitched = false; t->sh_credited = false; }
        sh_finger_spot(t, &t->sh_tx, &t->sh_ty);
        sh_move(t, dt, SH_SWIM_PX_S);
        bool at = sh_at(t);                                            /* settled on the spot, not passing through it */
        if (at && !t->sh_credited) { t->sh_credited = true; t->sh_hitches++; }
        t->sh_hitched = at;
        if (at && t->sh_pose != SH_POSE_GRIP) { t->sh_pose = SH_POSE_GRIP; t->sh_land = 0; }   /* the tail closes on the finger */
        sh_land_tick(t, dt, at);
        t->sh_facing = t->hold_x >= t->sh_x ? 1 : -1;              /* it faces the finger */
        t->sh_stay = SH_LINGER_S;
        return;
    }
    if (t->sh_mode == SH_HITCH) {                                  /* the finger lifted: a moment, then home */
        t->sh_hitched = false; t->sh_credited = false;
        sh_land_tick(t, dt, false);                                    /* it lets go of the finger */
        t->sh_stay -= dt;
        if (t->sh_stay > 0) return;
        sh_let_go(t, 0);
    }
    if (t->sh_mode == SH_PERCH) {
        if (!sh_hold_ok(t)) {
            /* a SHORT wait before it looks again: the blade's stay timer used
             * to carry over here and it hung in the water for minutes
             * (Strato 10-15: "it moved a little bit but now it looks frozen") */
            sh_let_go(t, tank_randf(t, 0.4f, 1.2f));
            return;
        }
        if (t->sh_akind == SH_ANCHOR_BLADE) {
            float gx, gy; sh_grip(t, t->sh_bed, t->sh_frond, &gx, &gy);
            if (fabsf(gy - t->sh_ay) > 3.0f || fabsf(gx - t->sh_ax) > 3.0f) {   /* the scissors took the blade down under it: not a teleport to
                                                                                  * the new grip (Strato 10-15, the watch) - it is knocked off, sinks
                                                                                  * a little and swims back to the blade's new grip */
                t->sh_ax = gx; t->sh_ay = gy; t->sh_tx = gx; t->sh_ty = gy;
                t->sh_mode = SH_DRIFT; t->sh_stay = 0; t->sh_vy = 14; t->sh_vx = 0;
                return;
            }
            t->sh_ax = gx; t->sh_ay = gy;                                   /* growth: it rides the blade */
        }
        t->sh_x = t->sh_ax; t->sh_y = t->sh_ay;
        t->sh_vx = t->sh_vy = 0;
        sh_land_tick(t, dt, true);                                     /* the tail closes round the anchor */
        if (t->sh_akind == SH_ANCHOR_SAND) {                           /* resting: a new plant or piece takes it off the bottom at once
                                                                        * (Strato 10-15: "i added a sword plant, i would have expected
                                                                        * the seahorse to move to it right away") */
            t->sh_look -= dt;
            if (t->sh_look <= 0) { t->sh_look = 2.0f; if (sh_any_hold(t)) { sh_let_go(t, 0); return; } }
        }
        /* the dark: it stays on its hold till the light (it rests like the
         * fish). Only this - the night used to stop every move, so a seahorse
         * let off the finger in the dark hung in open water where the finger
         * was until the light came on (Strato 2026-10-18); a trip home, a
         * knock-off and a new plant all still run now. */
        if (t->night) return;
        t->sh_stay -= dt;
        if (t->sh_stay <= 0) {                                     /* off to another (from the sand: a look around) */
            if (t->sh_akind != SH_ANCHOR_SAND && !sh_another_hold(t)) { t->sh_stay = sh_stay_for(t); return; }   /* the only hold there is: it stays */
            sh_let_go(t, 0);
        }
        return;
    }
    /* Adrift: a real anchor, or a short open-water hover when none exists. */
    if (t->sh_akind != SH_ANCHOR_NONE && t->sh_akind != SH_ANCHOR_SAND && !sh_hold_ok(t)) sh_pick(t, t->sh_x, t->sh_y);
    if (t->sh_akind == SH_ANCHOR_BLADE) { sh_grip(t, t->sh_bed, t->sh_frond, &t->sh_ax, &t->sh_ay); t->sh_tx = t->sh_ax; t->sh_ty = t->sh_ay; }
    if (t->sh_akind == SH_ANCHOR_NONE && sh_any_hold(t)) {
        sh_pick(t, t->sh_x, t->sh_y); t->sh_stay = 0;
    }
    if (t->sh_stay > 0) {
        t->sh_stay -= dt; sh_land_tick(t, dt, false);
        if (t->sh_stay <= 0 && t->sh_akind == SH_ANCHOR_NONE && !t->night)
            sh_pick(t, t->sh_x, t->sh_y);
        return;
    }
    if (t->sh_akind == SH_ANCHOR_NONE && t->night) {
        sh_land_tick(t, dt, false);
        t->sh_vx = t->sh_vy = 0;
        return;
    }
    sh_move(t, dt, SH_DRIFT_PX_S);
    sh_land_tick(t, dt, false);                                        /* whatever it held, it has let go */
    if (sh_at(t)) {
        if (t->sh_akind != SH_ANCHOR_NONE) t->sh_mode = SH_PERCH;
        t->sh_stay = sh_stay_for(t); t->sh_pose = SH_POSE_BLADE; t->sh_land = 0;
    }
}
/* ---- the shrimp school (SD_ITEM_SHRIMP; tank.h has the rules) ----
 * A loose school steering to one goal - a spot near available shelter most
 * of the time, now and then out on the sand or up in open water - each shrimp
 * at its own offset from it, keeping about a body length from its neighbours
 * (Strato: "space them out a bit more so they overlap a tad less"). A pellet
 * resting on the floor becomes the goal and they crowd in to peck it. They
 * turn like the fish: a committed turn, then a settle. Tuned against the
 * 2026-09-29 mockup Strato approved. */
#define SHRIMP_CRUISE    12.0f        /* px/s */
#define SHRIMP_DART      58.0f        /* px/s, the tail-flick escape */
#define SHRIMP_SEP_X     18.0f        /* personal space, side to side (a body and a bit) ... */
#define SHRIMP_SEP_Y     11.0f        /* ... and up and down (7 stacked them in towers on the glass) */
#define SHRIMP_HEAD      7.0f         /* the head's offset from the body's centre, along the facing */
static float shrimp_hash(int i, int k) {  /* a fixed 0..1 per shrimp: its place in the school (no RNG draw) */
    uint32_t h = (uint32_t)i * 2654435761u ^ (uint32_t)k * 40503u;
    h ^= h >> 13; h *= 0x5bd1e995u; h ^= h >> 15;
    return (h & 0xffff) / 65535.0f;
}
bool tank_shrimp_refusing(const tank_t *t) { (void)t; return false; }
int tank_shrimp_display_count(const tank_t *t) {
    return t->companion.display_compact ? (t->shrimp_n + 1) / 2 : t->shrimp_n;
}
static void shrimp_spawn(tank_t *t, int i, float x) {
    shrimp_t *q = &t->shrimp[i];
    memset(q, 0, sizeof *q);
    q->x = x; q->y = FOOD_FLOOR_Y - 6 - shrimp_hash(i, 3) * 14;
    q->facing = shrimp_hash(i, 4) < 0.5f ? -1 : 1; q->yaw = q->facing;
    q->ph = shrimp_hash(i, 5) * 6.28f;
}
void tank_shrimp_place(tank_t *t, int n) {
    if (n < 0) n = 0;
    if (n > SHRIMP_MAX) n = SHRIMP_MAX;
    float cx = clampf(t->reef_x, TANK_FX0 + 60, TANK_FX1 - 60);
    for (int i = 0; i < n; i++) shrimp_spawn(t, i, cx + (shrimp_hash(i, 1) - 0.5f) * (40 + n * 9));
    t->shrimp_n = (uint8_t)n;
    t->shrimp_tx = cx; t->shrimp_ty = FOOD_FLOOR_Y - 12; t->shrimp_tt = 4;
}
/* Mostly reef/sand goals, with occasional slow trips through open water. */
#define SHRIMP_RISE_P    0.06f         /* of the goals picked (0.10: up off the bottom ~45% of the time) */
#define SHRIMP_RISE_MIN  60.0f         /* px above the floor, at the least */
static void shrimp_goal(tank_t *t) {
    float r = tank_randf(t, 0, 1);
    if (r < SHRIMP_RISE_P) {
        t->shrimp_tx = tank_randf(t, TANK_FX0 + 40, TANK_FX1 - 40);
        t->shrimp_ty = FOOD_FLOOR_Y - tank_randf(t, SHRIMP_RISE_MIN, SHRIMP_RISE_MIN + 30);
        t->shrimp_tt = tank_randf(t, 22, 32);
        return;
    }
    if (r < SHRIMP_RISE_P + 0.62f) {
        t->shrimp_tx = clampf(t->reef_x + tank_randf(t, -30, 30), TANK_FX0 + 40, TANK_FX1 - 40);
    } else t->shrimp_tx = tank_randf(t, TANK_FX0 + 40, TANK_FX1 - 40);         /* out on the sand */
    t->shrimp_ty = tank_randf(t, 0, 1) < 0.8f ? FOOD_FLOOR_Y - tank_randf(t, 12, 26)
                                                          : FOOD_FLOOR_Y - tank_randf(t, 30, 64);   /* a swim up into open water */
    t->shrimp_tt = tank_randf(t, 6, 11);
}
static bool shrimp_food_ok(const food_t *p) {
    return p->alive && (p->floor_s > 0 || p->y >= FOOD_FLOOR_Y - 36);
}
static void shrimp_tick(tank_t *t, float dt) {
    if (!(t->sd_unlocks & SD_ITEM_SHRIMP)) return;
    if (t->shrimp_n < SHRIMP_START) tank_shrimp_place(t, SHRIMP_START);
    if (t->shrimp_cool > 0) t->shrimp_cool -= dt;
    int n = t->shrimp_n;
    float cx = 0, cy = 0;                                          /* the school's middle */
    for (int i = 0; i < n; i++) { cx += t->shrimp[i].x; cy += t->shrimp[i].y; }
    cx /= n; cy /= n;
    /* A newcomer joins the school after ten pellets and the cooldown. */
    if (t->shrimp_food >= SHRIMP_PER_JOIN && t->shrimp_cool <= 0 && n < SHRIMP_MAX) {
        shrimp_spawn(t, n, cx + (shrimp_hash(n, 1) - 0.5f) * 20);
        t->shrimp_n = (uint8_t)++n;
        t->shrimp_food = 0; t->shrimp_cool = SHRIMP_COOLDOWN_S;
    }
    t->shrimp_tt -= dt;
    if (t->shrimp_tt <= 0) shrimp_goal(t);
    int fp = -1; float best = 1e9f;                                /* the pellet in reach nearest the school */
    for (int k = 0; k < MAX_FOOD; k++) {
        const food_t *p = &t->food[k];
        if (!shrimp_food_ok(p)) continue;
        float d = fabsf(p->x - cx) + fabsf(p->y - cy) * 0.5f;
        if (d < best) { best = d; fp = k; }
    }
    float gx = fp >= 0 ? t->food[fp].x : t->shrimp_tx;
    float gy = fp < 0 ? t->shrimp_ty : t->food[fp].floor_s > 0 ? FOOD_FLOOR_Y - 2 : t->food[fp].y;   /* on the floor, or still sinking */
    float spread = 40 + n * 9;                                    /* the school's width: 76 px for 4, 130 for 10 */
    if (t->companion.display_compact) spread *= 0.6f;
    /* the whole school fits between the side glass: a goal by the wall (a bed
       against it, a pellet in the corner) keeps them spread, not piled up on
       the glass; the ones nearest a corner pellet still reach it */
    float half = (fp >= 0 ? spread * 0.45f : spread) * 0.5f + 16;
    gx = clampf(gx, TANK_FX0 + half, TANK_FX1 - half);
    float dart_decay = 0;
    bool have_dart_decay = false;
    for (int i = 0; i < n; i++) {
        shrimp_t *q = &t->shrimp[i];
        q->ph += dt; q->pecking = 0;
        if (q->dart > 0) {
            q->dart -= dt;
            if (!have_dart_decay) { dart_decay = powf(0.2f, dt); have_dart_decay = true; }
            q->vx *= dart_decay; q->vy *= dart_decay;
        } else {
            float ox = (shrimp_hash(i, 1) - 0.5f) * spread;
            float oy = (shrimp_hash(i, 2) - 0.5f) * (t->companion.display_compact ? 16 : 24);
            float tx = gx + (fp >= 0 ? ox * 0.45f : ox), ty = gy + (fp >= 0 ? 0 : oy);
            float dx = tx - q->x, dy = ty - q->y, d = sqrtf(dx * dx + dy * dy) + 1e-3f;
            bool chase = fp >= 0;                                       /* food in reach: they hurry, up or down alike */
            float sp = fminf(SHRIMP_CRUISE * (chase ? 1.6f : 1.0f), d * 0.9f);
            float wx = dx / d * sp, wy = dy / d * sp * (chase ? 1.0f : 0.7f);
            float sx = fp >= 0 ? SHRIMP_SEP_X * 0.6f : SHRIMP_SEP_X, sy = fp >= 0 ? SHRIMP_SEP_Y * 0.6f : SHRIMP_SEP_Y;
            if (t->companion.display_compact) { sx *= 0.75f; sy *= 0.75f; }
            for (int j = 0; j < n; j++) {
                if (j == i) continue;
                float nx = (q->x - t->shrimp[j].x) / sx, ny = (q->y - t->shrimp[j].y) / sy;
                if (fabsf(nx) >= 1 || fabsf(ny) >= 1) continue;
                float e = sqrtf(nx * nx + ny * ny);
                if (e < 1 && e > 0) { wx += nx / e * (1 - e) * 22; wy += ny / e * (1 - e) * 9; }
            }
            float a = fminf(1, dt * 2.5f);
            q->vx += (wx - q->vx) * a; q->vy += (wy - q->vy) * a;
            float hx = q->x + SHRIMP_HEAD * q->facing;
            for (int k = 0; k < MAX_FOOD; k++) {                       /* a pellet at its head */
                food_t *p = &t->food[k];
                if (!shrimp_food_ok(p) || fabsf(p->x - hx) > 8 || fabsf(p->y - q->y) > 9) continue;
                if (k == fp) {
                    q->pecking = 1; q->vx *= 0.5f;
                    q->vy = p->floor_s > 0 ? q->vy * 0.5f : 8.0f;           /* a falling pellet: it sinks with it */
                    p->nibbled += dt;
                }
                break;
            }
        }
        q->x += q->vx * dt;
        q->y += q->vy * dt + sinf(q->ph * 2.1f) * dt * 1.2f;
        q->x = clampf(q->x, TANK_FX0 + 12, TANK_FX1 - 12);
        q->y = clampf(q->y, 40, FOOD_FLOOR_Y - 1);
        if (q->dart <= 0) {                                             /* the committed turn */
            int8_t want = q->vx > 1.5f ? 1 : q->vx < -1.5f ? -1 : q->facing;
            if (want != q->facing) { q->behind += dt; if (q->behind > 0.35f) { q->facing = want; q->behind = -0.6f; } }
            else q->behind = fminf(0, q->behind + dt);
        }
        q->yaw += clampf(q->facing - q->yaw, -5 * dt, 5 * dt);
    }
    if (fp >= 0 && t->food[fp].nibbled >= SHRIMP_PECK_S) {             /* eaten: one toward the next shrimp */
        t->food[fp].alive = false;
        if (t->shrimp_food < SHRIMP_PER_JOIN) t->shrimp_food++;       /* stops at ten while the cooldown runs */
        t->shrimp_eaten++;
    }
}
/* asleep: nothing is eaten, so nothing joins; the cooldown runs on lived time */
static void shrimp_sleep(tank_t *t, float seconds) {
    if (!(t->sd_unlocks & SD_ITEM_SHRIMP)) return;
    t->shrimp_cool = fmaxf(0, t->shrimp_cool - seconds);
}
static void shrimp_scatter(tank_t *t, float x, float y, float radius) {
    if (!(t->sd_unlocks & SD_ITEM_SHRIMP)) return;
    bool any = false;
    for (int i = 0; i < t->shrimp_n; i++) {
        shrimp_t *q = &t->shrimp[i];
        float dx = q->x - x, dy = q->y - y, d = sqrtf(dx * dx + dy * dy);
        if (d >= radius) continue;
        if (d < 1) { dx = q->facing ? -q->facing : 1; dy = 0; d = 1; }
        q->dart = 0.55f; q->vx = dx / d * SHRIMP_DART; q->vy = dy / d * SHRIMP_DART * 0.7f;
        q->facing = (int8_t)(q->vx > 0 ? -1 : 1); q->yaw = q->facing;   /* tail first: they flick backward */
        q->behind = -0.8f; any = true;
    }
    if (any) t->shrimp_tt = fminf(t->shrimp_tt, 3.0f);                /* and regroup somewhere new */
}
void tank_plant_place(tank_t *t) {
    tank_veg_set(t, 3, VEG_START);                          /* a young plant; it grows from here */
}
void tank_castle_place(tank_t *t) {
    t->castle_x = 0; t->castle_z = DECOR_Z_FRONT;          /* the default spot, the fish swim through */
}
void tank_coral_place(tank_t *t) {
    t->coral_x = 0; t->coral_z = DECOR_Z_FRONT;            /* the default spot, in front of the reef bed's grass; the colour stays the keeper's */
    t->coral_growth = CORAL_START;                          /* young: it grows from here */
}
uint32_t tank_coral_rgb(const tank_t *t) { return t->coral_rgb ? t->coral_rgb : CORAL_PAL[0]; }
float    tank_coral_growth(const tank_t *t) { return t->coral_growth <= 0 ? CORAL_FULL : t->coral_growth > CORAL_FULL ? CORAL_FULL : t->coral_growth; }
static void coral_grow(tank_t *t, float seconds) {          /* the same pace awake and asleep */
    if (!(t->sd_unlocks & SD_ITEM_CORAL) || t->coral_growth <= 0 || t->coral_growth >= CORAL_FULL) return;
    t->coral_growth = fminf(CORAL_FULL, t->coral_growth + seconds / CORAL_GROW_S);
}
void tank_cluster_place(tank_t *t) {
    t->cluster_x = 0; t->cluster_z = DECOR_Z_FRONT;         /* the default spot; the look stays the keeper's */
    t->cluster_growth = CLUSTER_START + 1e-4f;              /* > 0: growing (0 in a save = full, tank_cluster_growth) */
}
void tank_pumpkin_place(tank_t *t) {
    t->pumpkin_x = 0; t->pumpkin_z = DECOR_Z_BACK;          /* the default spot, behind the grass: the glow through the blades */
}
float tank_cluster_growth(const tank_t *t) { return t->cluster_growth <= 0 ? CLUSTER_FULL : t->cluster_growth > CLUSTER_FULL ? CLUSTER_FULL : t->cluster_growth; }
int   tank_cluster_scheme(const tank_t *t) { return t->cluster_scheme < CLUSTER_SCHEME_N ? t->cluster_scheme : 0; }
void  tank_cluster_set_scheme(tank_t *t, int i) { t->cluster_scheme = (uint8_t)(i < 0 ? 0 : i >= CLUSTER_SCHEME_N ? CLUSTER_SCHEME_N - 1 : i); }
static void cluster_grow(tank_t *t, float seconds) {
    if (!(t->sd_unlocks & SD_ITEM_CLUSTER) || t->cluster_growth <= 0 || t->cluster_growth >= CLUSTER_FULL) return;
    t->cluster_growth = fminf(CLUSTER_FULL, t->cluster_growth + seconds / CLUSTER_GROW_S);
}
void     tank_coral_set_rgb(tank_t *t, uint32_t rgb) { t->coral_rgb = rgb & 0xffffff; }
/* the decor's spot and layer (see tank.h): item 0 is the plant, item 2 the castle, item 3 the coral, item 4 the cluster, item 8 the pumpkin */
const char *const FISH_VARIETY_NAMES[FISH_VARIETY_COUNT] = {
    "HARBORFIN", "ANGEL", "GUPPY", "TETRA", "PUFFER",
    "SHARK", "JELLYFISH", "STINGRAY", "CLOWNFISH", "BETTA"
};
const char *const FISH_PATTERN_NAMES[FISH_PATTERN_COUNT] = { "STRIPES", "SPOTS", "BAND", "PLAIN",
    "SADDLE", "TIGER", "SPECKLE", "FADE", "MASK" };
const char *const FISH_TRAIT_NAMES[TRAIT_COUNT] = { "SHIMMER", "PAINTED FINS", "PALE BELLY", "GOLD EYE" };
int tank_ornament_index(int item) { return item >= 10 && item <= 13 ? item - 10 : item >= 16 && item < 22 ? item - 12 : -1; }
/* the statement pieces: as tall or as wide as the castle (render.c grand_pixel) */
static const struct { uint8_t item, w, h; } GRAND_DECOR[] = {
    {SD_DECOR_FIRST + 0, 56, 132},                      /* lighthouse on its rock */
    {SD_DECOR_FIRST + 6, 136, 72},                      /* stone bridge */
    {SD_DECOR_FIRST + 7, 88, 118},                      /* pagoda */
    {SD_DECOR_FIRST + 10, 112, 96},                     /* torii gate */
};
static int grand_index(int item) {
    for (int i = 0; i < (int)(sizeof GRAND_DECOR / sizeof *GRAND_DECOR); i++) if (GRAND_DECOR[i].item == item) return i;
    return -1;
}
bool tank_decor_grand(int item) { return grand_index(item) >= 0; }
int tank_ornament_w(int item) {
    static const int widths[6] = {64, 30, 42, 48, 58, 36};
    int g = grand_index(item);
    if (g >= 0) return GRAND_DECOR[g].w;
    return item >= 22 && item < SD_ITEM_COUNT ? 40 : item >= 16 && item < 22 ? widths[item - 16] : item == 9 ? CHEST_W : item == 10 ? 36 : item == 11 ? 34 : item == 12 ? 32 : item == 13 ? 36 : 0;
}
int tank_ornament_h(int item) {
    static const int heights[6] = {52, 40, 46, 68, 64, 110};
    int g = grand_index(item);
    if (g >= 0) return GRAND_DECOR[g].h;
    return item >= 22 && item < SD_ITEM_COUNT ? 40 : item >= 16 && item < 22 ? heights[item - 16] : item == 9 ? CHEST_H : item == 10 ? 24 : item == 11 ? 32 : item == 12 ? 40 : item == 13 ? 104 : 0;
}
bool  tank_decor_placeable(int item) { return SD_IS_NEW_DECOR(item) || item == 0 || item == 2 || item == 3 || item == 4 || item == 8 || item == 9 || tank_ornament_index(item) >= 0; }
int   tank_fixed_index(int item) { return SD_IS_SCENERY(item) ? item - SD_SCENERY_FIRST : item >= 22 && item <= 29 ? 6 + item - 22 : -1; }
bool  tank_decor_movable(int item) { return item != 8 && (tank_decor_placeable(item) || tank_fixed_index(item) >= 0); }
bool  tank_decor_surface(int item) { return item == 24 || item == 28 || item == 29; }
/* the fixed scenery's historical spots: the sand set in two staggered rows, the floats in quarters */
static float fixed_default_x(int item) {
    if (SD_IS_SCENERY(item)) return (float)((2 * (item - SD_SCENERY_FIRST) + 1) * TANK_W / 12);
    if (item == 28) return (float)(TANK_W / 4);
    if (item == 24) return (float)(2 * TANK_W / 4);
    if (item == 29) return (float)(3 * TANK_W / 4);
    static const int floor_items[5] = {22, 23, 25, 26, 27};
    for (int k = 0; k < 5; k++) if (floor_items[k] == item) return (float)((k + 1) * TANK_W / 6);
    return TANK_W * 0.5f;
}
float tank_decor_half_w(int item) { return item == 0 ? PLANT_HALF_W : item == 2 ? CASTLE_HALF_W : item == 3 ? CORAL_HALF_W : item == 4 ? CLUSTER_HALF_W : item == 8 ? PUMPKIN_HALF_W : tank_ornament_w(item) / 2; }
/* only the plant weaves AMONG the fish; the castle and both corals are
 * BEHIND or IN FRONT (Strato, 2026-09-24: "remove the among option for
 * corals, it doesn't really make as much sense") */
int   tank_decor_z_count(int item) { return item == 0 ? DECOR_Z_N : 2; }   /* a float too (2026-10-28): under or over the fish */
int   tank_decor_z_at(int item, int i) {
    if (item != 0) return i <= 0 ? DECOR_Z_BACK : DECOR_Z_FRONT;
    return i < 0 ? 0 : i >= DECOR_Z_N ? DECOR_Z_N - 1 : i;
}
int   tank_decor_z_index(int item, int z) { return item != 0 ? (z == DECOR_Z_BACK ? 0 : 1) : z; }
float tank_decor_x(const tank_t *t, int item) {
    int fi = tank_fixed_index(item);
    if (fi >= 0) return t->fixed.x[fi] > 0 ? t->fixed.x[fi] : fixed_default_x(item);
    if (SD_IS_NEW_DECOR(item)) {
        float x = t->content.x[item - SD_DECOR_FIRST];
        return x > 0 ? x : TANK_FLOOR_X(80 + ((item - SD_DECOR_FIRST) % 4) * 85);
    }
    if (item == 2) return t->castle_x > 0 ? t->castle_x : CASTLE_X_DEFAULT;
    if (item == 3) return t->coral_x > 0 ? t->coral_x : CORAL_X_DEFAULT;
    if (item == 4) return t->cluster_x > 0 ? t->cluster_x : CLUSTER_X_DEFAULT;
    if (item == 8) return t->pumpkin_x > 0 ? t->pumpkin_x : PUMPKIN_X_DEFAULT;
    if (item == 9) return t->chest_x > 0 ? t->chest_x : TANK_FLOOR_X(320);
    int oi = tank_ornament_index(item);
    if (oi >= 0) return t->ornament_x[oi] > 0 ? t->ornament_x[oi] : TANK_FLOOR_X(80 + (oi % 4) * 85);
    if (item != 0) return 0;
    return t->plant_x > 0 ? t->plant_x : PLANT_X_DEFAULT;
}
int tank_decor_z(const tank_t *t, int item) {
    int oi = tank_ornament_index(item), fi = tank_fixed_index(item);
    if (fi >= 0) return t->fixed.z[fi] == DECOR_Z_FRONT ? DECOR_Z_FRONT : DECOR_Z_BACK;
    return SD_IS_NEW_DECOR(item) ? t->content.z[item - SD_DECOR_FIRST] : item == 0 ? t->plant_z : item == 2 ? t->castle_z : item == 3 ? t->coral_z : item == 4 ? t->cluster_z : item == 8 ? t->pumpkin_z : item == 9 ? t->chest_z : oi >= 0 ? t->ornament_z[oi] : DECOR_Z_MIDDLE;
}
static float decor_top(const tank_t *t, int item) {         /* the piece's highest pixel */
    if (tank_decor_surface(item)) return 0;
    if (tank_fixed_index(item) >= 0) return TANK_BOT - (SD_IS_SCENERY(item) ? 52 : 48);
    if (item == 0) { float top; tank_veg_bed(t, 3, NULL, NULL, &top, NULL); return top; }
    if (item == 2) return TANK_BOT - 16 - 146;
    if (item == 3) return TANK_BOT - 14 + DECOR_SINK - 92;
    if (item == 8) return TANK_BOT - 14 + DECOR_SINK - 82;
    if (tank_ornament_h(item)) return TANK_BOT - 14 + DECOR_SINK - tank_ornament_h(item);
    return TANK_BOT - 14 + DECOR_SINK - 120;
}
int tank_decor_hit(const tank_t *t, float x, float y) {
    int order[DECOR_SLOTS], n = tank_decor_order(t, order);
    for (int k = n - 1; k >= 0; k--) {
        int item = order[k];
        if (!tank_decor_owned(t, item)) continue;
        float cx = tank_decor_x(t, item), half = tank_decor_half_w(item) + 8;
        float bottom = tank_decor_surface(item) ? 48 : TANK_BOT;
        if (x >= cx - half && x <= cx + half && y >= decor_top(t, item) - 8 && y <= bottom) return item;
    }
    return -1;
}
/* ---- the order of the pieces (2026-10-17; tank.h) ---- */
static const int DECOR_SLOT_ITEM[DECOR_SLOTS] = { 2, 3, 4, 8, 0, 9, 10, 11, 12, 13, 16, 17, 18, 19, 20, 21,
    43,44,45,46,47,48,49,50,51,52,53,54,55,56,57,58,59,60,61,62,
    36,37,38,39,40,41,22,23,24,25,26,27,28,29 };   /* fixed scenery: slot DECOR_FIXED_SLOT0 + tank_fixed_index */
int  tank_decor_slot(int item) { for (int s = 0; s < DECOR_SLOTS; s++) if (DECOR_SLOT_ITEM[s] == item) return s; return -1; }
int  tank_decor_slot_item(int slot) { return slot >= 0 && slot < DECOR_SLOTS ? DECOR_SLOT_ITEM[slot] : -1; }
bool tank_decor_owned(const tank_t *t, int item) {
    if (item == 8) return false;
    if (SD_IS_SCENERY(item)) return (t->polish.scenery_unlocks & (1u << (item - SD_SCENERY_FIRST))) != 0;
    return SD_IS_NEW_DECOR(item) ? (t->content.owned & (1u << (item - SD_DECOR_FIRST))) != 0 :
        tank_decor_slot(item) >= 0 && (t->sd_unlocks & (1u << item));
}
static bool decor_before(const tank_t *t, int ia, int ib) {       /* a is painted before (under) b at one depth */
    int sa = tank_decor_slot(ia), sb = tank_decor_slot(ib);
    int ra = t->decor_rank[sa] & DECOR_RANK_MASK, rb = t->decor_rank[sb] & DECOR_RANK_MASK;
    /* on a tie the fixed scenery stays under the rest, where its old bake had it */
    int ka = sa >= DECOR_FIXED_SLOT0 ? sa - DECOR_SLOTS : sa, kb = sb >= DECOR_FIXED_SLOT0 ? sb - DECOR_SLOTS : sb;
    return ra != rb ? ra < rb : ka < kb;
}
int  tank_decor_rank(const tank_t *t, int item) { int s = tank_decor_slot(item); return s < 0 ? 0 : t->decor_rank[s] & DECOR_RANK_MASK; }
static void decor_rank_set(tank_t *t, int slot, int rank) {
    t->decor_rank[slot] = (uint8_t)((t->decor_rank[slot] & DECOR_RANK_MIRROR) | (rank & DECOR_RANK_MASK));
}
bool tank_decor_mirrorable(int item) { return item >= 9 && tank_decor_movable(item); }
bool tank_decor_mirrored(const tank_t *t, int item) {
    int s = tank_decor_slot(item);
    return s >= 0 && tank_decor_mirrorable(item) && (t->decor_rank[s] & DECOR_RANK_MIRROR);
}
void tank_decor_set_mirrored(tank_t *t, int item, bool mirrored) {
    int s = tank_decor_slot(item);
    if (s < 0 || !tank_decor_mirrorable(item)) return;
    t->decor_rank[s] = (uint8_t)((t->decor_rank[s] & DECOR_RANK_MASK) | (mirrored ? DECOR_RANK_MIRROR : 0));
}
int  tank_decor_order(const tank_t *t, int items[DECOR_SLOTS]) {
    int n = 0;
    for (int s = 0; s < DECOR_SLOTS; s++) if (tank_decor_owned(t, DECOR_SLOT_ITEM[s])) items[n++] = DECOR_SLOT_ITEM[s];
    for (int i = 1; i < n; i++) {                                 /* an insertion sort of five */
        int v = items[i], j = i;
        while (j > 0 && decor_before(t, v, items[j - 1])) { items[j] = items[j - 1]; j--; }
        items[j] = v;
    }
    return n;
}
bool tank_decor_above(const tank_t *t, int a, int b) {
    int za = tank_decor_z(t, a), zb = tank_decor_z(t, b);
    return za != zb ? za > zb : decor_before(t, b, a);
}
static void decor_normalize(tank_t *t) {                          /* ranks 1..n in the current order (ties resolved) */
    int items[DECOR_SLOTS], n = tank_decor_order(t, items);
    for (int i = 0; i < n; i++) decor_rank_set(t, tank_decor_slot(items[i]), i + 1);
}
void tank_decor_raise(tank_t *t, int item) {
    int s = tank_decor_slot(item); if (s < 0) return;
    decor_normalize(t);
    int top = 0; for (int k = 0; k < DECOR_SLOTS; k++) if ((int)(t->decor_rank[k] & DECOR_RANK_MASK) > top) top = t->decor_rank[k] & DECOR_RANK_MASK;
    decor_rank_set(t, s, top + 1);
}
int  tank_decor_neighbor(const tank_t *t, int item, int dir) {
    if (!tank_decor_owned(t, item) || tank_decor_z(t, item) == DECOR_Z_MIDDLE || tank_decor_surface(item)) return -1;   /* the woven plant and the floats have no stack */
    int items[DECOR_SLOTS], n = tank_decor_order(t, items), at = -1;
    for (int i = 0; i < n; i++) if (items[i] == item) at = i;
    float cx = tank_decor_x(t, item), half = tank_decor_half_w(item);
    /* Only a piece whose footprint overlaps this one can visibly change places with it. */
    for (int i = at + dir; i >= 0 && i < n; i += dir) {
        if (tank_decor_z(t, items[i]) != tank_decor_z(t, item) || tank_decor_surface(items[i])) continue;
        if (fabsf(tank_decor_x(t, items[i]) - cx) < half + tank_decor_half_w(items[i])) return items[i];
    }
    return -1;
}
bool tank_decor_shift(tank_t *t, int item, int dir) {
    int other = tank_decor_neighbor(t, item, dir);
    if (other < 0) return false;
    /* Reinsert just past the neighbor so the pieces in between keep their order. */
    int items[DECOR_SLOTS], n = tank_decor_order(t, items), m = 0, order[DECOR_SLOTS];
    for (int i = 0; i < n; i++) {
        if (items[i] == item) continue;
        if (items[i] == other && dir < 0) order[m++] = item;
        order[m++] = items[i];
        if (items[i] == other && dir > 0) order[m++] = item;
    }
    for (int i = 0; i < m; i++) decor_rank_set(t, tank_decor_slot(order[i]), i + 1);
    return true;
}
void tank_decor_reset(tank_t *t, int item) {
    { int s = tank_decor_slot(item); if (s >= 0) t->decor_rank[s] = 0; }
    { int fi = tank_fixed_index(item); if (fi >= 0) { t->fixed.x[fi] = 0; t->fixed.z[fi] = DECOR_Z_BACK; return; } }
    if (SD_IS_NEW_DECOR(item)) { t->content.x[item - SD_DECOR_FIRST] = 0; t->content.z[item - SD_DECOR_FIRST] = DECOR_Z_BACK; return; }
    if (item == 0) { t->plant_x = 0; t->plant_z = DECOR_Z_MIDDLE; tank_veg_set(t, 3, VEG_START); }
    if (item == 2) { t->castle_x = 0; t->castle_z = DECOR_Z_FRONT; }
    if (item == 3) { t->coral_x = 0; t->coral_z = DECOR_Z_FRONT; t->coral_rgb = 0; t->coral_growth = 0; t->coral_acc = 0; }
    if (item == 4) { t->cluster_x = 0; t->cluster_z = DECOR_Z_FRONT; t->cluster_scheme = 0; t->cluster_growth = 0; t->cluster_acc = 0; }
    if (item == 8) tank_pumpkin_place(t);
    if (item == 9) { t->chest_x = 0; t->chest_z = DECOR_Z_BACK; }
    int oi = tank_ornament_index(item);
    if (oi >= 0) { t->ornament_x[oi] = 0; t->ornament_z[oi] = DECOR_Z_BACK; }
}
void tank_decor_set(tank_t *t, int item, float x, int z) {
    if (!tank_decor_movable(item)) return;
    float half = tank_decor_half_w(item), lo = TANK_FX0 + DECOR_MARGIN + half, hi = TANK_FX1 - DECOR_MARGIN - half;
    if (x < lo) x = lo;
    if (x > hi) x = hi;
    if (z < 0) z = 0;
    if (z >= DECOR_Z_N) z = DECOR_Z_N - 1;
    uint8_t z2 = (uint8_t)(z == DECOR_Z_BACK ? DECOR_Z_BACK : DECOR_Z_FRONT);   /* no AMONG for these */
    { int fi = tank_fixed_index(item); if (fi >= 0) { t->fixed.x[fi] = x; t->fixed.z[fi] = z2; return; } }   /* a float BEHIND (0, the default) lets the fish swim over it */
    if (SD_IS_NEW_DECOR(item)) { t->content.x[item - SD_DECOR_FIRST] = x; t->content.z[item - SD_DECOR_FIRST] = z2; return; }
    if (item == 2) { t->castle_x = x; t->castle_z = z2; return; }
    if (item == 3) { t->coral_x = x; t->coral_z = z2; return; }
    if (item == 4) { t->cluster_x = x; t->cluster_z = z2; return; }
    if (item == 8) { t->pumpkin_x = x; t->pumpkin_z = z2; return; }
    if (item == 9) { t->chest_x = x; t->chest_z = z2; return; }
    int oi = tank_ornament_index(item);
    if (oi >= 0) { t->ornament_x[oi] = x; t->ornament_z[oi] = z2; return; }
    t->plant_x = x; t->plant_z = (uint8_t)z;
}

void tank_touch_hold(tank_t *t, float x, float y) {
    tank_handled(t);
    t->hold_active = true; t->hold_x = x; t->hold_y = y;
}

void tank_touch_drag(tank_t *t, float x, float y) {
    tank_handled(t);
    t->drag_active = true;
    if (!t->drag_has_prev) {
        t->slash_x0 = x; t->slash_y0 = y; t->slash_cut = false;
    } else {
        t->drag_dist += tank_dist(x, y, t->drag_px, t->drag_py);
    }
    t->drag_px = x; t->drag_py = y; t->drag_has_prev = true;
}
void tank_set_tool(tank_t *t, int tool) {
    (void)tool; t->tool = TOOL_HAND; t->tool_idle = 0;
}

void tank_lock_tools(tank_t *t, bool lock) { t->tools_locked = lock; }
bool tank_stroking(const tank_t *t) { return t->drag_has_prev && t->drag_dist >= WIPE_ENGAGE_PX; }

void tank_feed(tank_t *t, float x, int n) {
    tank_handled(t);
    x = clampf(x, TANK_FX0 + 25, TANK_FX1 - 25);
    int dropped = 0;
    for (int i = 0; i < MAX_FOOD && n > 0; i++) {
        if (t->food[i].alive) continue;
        t->food[i].alive = true; t->food[i].from_player = true;
        t->food[i].x = clampf(x + tank_randf(t, -14, 14), TANK_FX0 + 20, TANK_FX1 - 20);
        t->food[i].y = tank_glass_top(t->food[i].x) + tank_randf(t, 6, 18);
        t->food[i].age = 0; t->food[i].floor_s = 0; t->food[i].nibbled = 0;
        n--; dropped++;
    }
    if (dropped) tank_emit(TEV_FEED, -1);        /* the plink is for pellets, not for the tap (a full tank drops none) */
    t->feed_spot_x = t->feed_spot_x < 0 ? x : t->feed_spot_x + (x - t->feed_spot_x) * 0.3f;
    t->feed_open = true;                         /* a meal once somebody eats from it */
}

/* the triple tap: fish near it bolt (stress, a little trust lost) and the
 * shrimp scatter - from the water or from the school alike */
static void tank_startle(tank_t *t, float x, float y) {
    tank_emit(TEV_SPOOK, -1);
    t->startled = true; t->startle_x = x; t->startle_y = y; t->startle_cooldown = STARTLE_COOLDOWN;
    for (int i = 0; i < t->n_fish; i++) {
        fish_t *f = &t->fish[i];
        if (tank_dist(f->x, f->y, x, y) < STARTLE_RADIUS) {
            float sensitivity = 1.25f - 0.5f * clampf(f->bold, 0, 1);
            f->stress = fminf(10, f->stress + 2.5f * sensitivity);
            f->trust = fmaxf(0, f->trust - 0.4f * sensitivity);
        }
    }
    shrimp_scatter(t, x, y, STARTLE_RADIUS);
}
bool tank_shrimp_hit(const tank_t *t, float x, float y) {
    if (!(t->sd_unlocks & SD_ITEM_SHRIMP)) return false;
    for (int i = 0; i < t->shrimp_n; i++)
        if (tank_dist(t->shrimp[i].x, t->shrimp[i].y, x, y) <= SHRIMP_TAP_R) return true;
    return false;
}
int tank_shrimp_tap(tank_t *t, float x, float y) {
    tank_handled(t);
    if (t->shrimp_tap_t > TAP_WINDOW) t->shrimp_taps = 0;
    if (t->shrimp_taps < 255) t->shrimp_taps++;
    t->shrimp_tap_t = 0;
    if (t->shrimp_taps == 3) tank_startle(t, x, y);
    return t->shrimp_taps;
}
void tank_touch_tap(tank_t *t, float x, float y) {
    tank_handled(t);
    if (y - tank_glass_top(x) < FEED_ZONE_Y) { tank_feed(t, x, 3); return; }     /* surface tap = feed (the bowl's surface is its top glass) */
    if (t->tap_burst_t > TAP_WINDOW) t->tap_count = 0;
    t->tap_count++; t->tap_burst_t = 0; t->tap_x = x; t->tap_y = y;
    tank_emit(TEV_TAP, -1);
    if (t->startled) {                              /* chasing: keep them spooked */
        t->startle_x = x; t->startle_y = y; t->startle_cooldown = STARTLE_COOLDOWN;
        for (int i = 0; i < t->n_fish; i++) {
            fish_t *f = &t->fish[i];
            float sensitivity = 1.25f - 0.5f * clampf(f->bold, 0, 1);
            f->stress = fminf(10, f->stress + 0.6f * sensitivity);
        }
        shrimp_scatter(t, x, y, STARTLE_RADIUS);
    } else if (t->tap_count >= 3) tank_startle(t, x, y);   /* aggressive: engage */
}

/* per-frame bookkeeping for the touch state machine */
static void touch_tick(tank_t *t, float dt) {
    t->tap_burst_t += dt;
    t->shrimp_tap_t += dt;
    /* two taps then a pause: the light (MANUAL, the default; in settings'
       AUTO the idle rule owns it - 2026-09-15: the old always-on override
       rode in the save and froze a tank in permanent day) */
    if (!t->startled && t->tap_count == 2 && t->tap_burst_t > TAP_WINDOW) {
        if (!t->light_auto) {
            t->light_manual_off = !t->light_manual_off;
            if (t->light_manual_off) t->light_tip_seen = true;   /* the first time: the notice says what happened (2026-10-03) */
        }
        t->tap_count = 0;
    }
    if (t->tap_count >= 3 && t->tap_burst_t > TAP_WINDOW) t->tap_count = 0;
    if (t->startled) {
        t->startle_cooldown -= dt;
        if (t->startle_cooldown <= 0) { t->startled = false; t->tap_count = 0; }
    }
    if (t->hold_active) {                           /* calm presence earns trust */
        float was = t->hold_time;
        t->hold_time += dt;
        bool draw_begins = was < HOLD_ATTRACT_S && t->hold_time >= HOLD_ATTRACT_S;
        for (int i = 0; i < t->n_fish; i++) {
            fish_t *f = &t->fish[i];
            float d = tank_dist(f->x, f->y, t->hold_x, t->hold_y);
            if (d < HOLD_RADIUS) {
                /* 0.3.6: the base rate is slower than it was (0.02/s, 2026-09-04
                 * .. 0.3.5) and a hitched seahorse doubles it - "twice as fast"
                 * on its shop row. What it adds over the base is its tally. */
                float base_rate = TRUST_PER_S * ((t->aquariums.stat_upgrades & STAT_TRAINING) ? 1.25f : 1);
                float rate = base_rate + (t->sh_hitched ? TRUST_PER_S * (SEAHORSE_TRUST_X - 1) : 0);
                float was_t = f->trust;
                f->trust = fminf(10, f->trust + dt * rate);
                if (t->sh_hitched) t->sh_trust_passed += fmaxf(0, (f->trust - was_t) - fminf(10 - was_t, dt * base_rate));
            }
            /* a hold-approach = a fish that was out in the tank when the
             * settled hold began its draw and then came all the way in.
             * Per FISH (2026-09-15: it was gated on the per-hold flag below,
             * so only the first arrival - always the fastest, highest-trust
             * pair - could ever get it; the other two came in hold after
             * hold and were never credited), and only for a fish that
             * actually travelled (same day: a fish already sitting under the
             * finger used to be credited at 1.5 s without moving). The
             * tank's hold_approaches counter still counts the hold once. */
            if (draw_begins) f->hold_far = d >= HOLD_APPROACH_FROM;
            if (f->hold_far && d < HOLD_APPROACH_AT) {
                if (!t->hold_approached) { t->hold_approached = true; t->hold_approaches++; tank_emit(TEV_INVESTIGATE, i); }
            }
        }
    } else {
        t->hold_time = 0; t->hold_approached = false;
        for (int i = 0; i < t->n_fish; i++) t->fish[i].hold_far = false;
    }
    if (t->greet_timer > 0) t->greet_timer -= dt;
    /* courtship episodes: while progression says an arrival is close, the
     * pair circles the reef for a few seconds every minute or so - frequent
     * enough to notice across a couple of check-ins, rare enough to feel
     * like something glimpsed rather than an indicator */
    if (t->spawning && t->court_a >= 0 && t->court_b >= 0) {
        /* No episode clock: the pair circles the nursery until the fry
         * comes, lit or dark. The clock that counts is the time BOTH spend
         * near shelter (a parent still swimming down,
         * or begging, holds it); the bubbles mark their arrival. */
        float sx, sy, sr; court_site(t, &sx, &sy, &sr);
        const fish_t *a = &t->fish[t->court_a], *b = &t->fish[t->court_b];
        if (tank_dist(a->x, a->y, sx, sy) < sr + SPAWN_NEAR_PX && tank_dist(b->x, b->y, sx, sy) < sr + SPAWN_NEAR_PX) {
            if (t->spawn_danced == 0) tank_court_puff(t, 2);
            t->spawn_danced += dt;
        }
        t->court_active = 0;
    } else if (t->courting && !t->night) {
        if (t->court_active > 0) t->court_active -= dt;
        else if ((t->court_cool -= dt) <= 0) {
            t->court_active = tank_randf(t, 6, 9);
            t->court_cool = tank_randf(t, 40, 90);
            tank_court_puff(t, 2);
        }
    } else t->court_active = 0;
    t->tool = TOOL_HAND;
    /* A lifted finger ends the current stroke (platform reasserts while down). */
    if (!t->drag_active) {
        t->drag_has_prev = false; t->drag_dist = 0;
        t->slash_armed = t->slash_engaged = t->slash_cut = false;
        t->slash_h = t->slash_v = 0;
    }
    t->drag_active = false;
}

void tank_handled(tank_t *t) { t->idle_s = t->clock_idle_s = 0; t->clock_requested = false; }

void tank_toggle_light(tank_t *t) {
    /* the first toggle takes over from the idle rule AND flips the light
       (the on/off cue comes from tank_tick, where the flip lands) */
    if (!t->light_override) { t->light_override = true; t->light_on = t->night; }
    else t->light_on = !t->light_on;
}

void tank_light_auto(tank_t *t) { t->light_override = false; }

bool tank_orient(tank_t *t, bool live_inverted) {
    (void)live_inverted;
    t->orient_inv = false;
    return t->orient_inv;
}

const int LIGHT_IDLE_CHOICES[LIGHT_IDLE_N] = { 5, 15, 30, 60, 180, 300, 600, 1800 };
int tank_light_choice(const tank_t *t) {
    if (!t->light_auto) return 0;
    int best = 0;
    for (int i = 1; i < LIGHT_IDLE_N; i++)
        if (abs(LIGHT_IDLE_CHOICES[i] - t->light_idle_s) < abs(LIGHT_IDLE_CHOICES[best] - t->light_idle_s)) best = i;
    return best + 1;
}
void tank_light_choice_set(tank_t *t, int choice) {
    if (choice < 0) choice = 0;
    if (choice > LIGHT_IDLE_N) choice = LIGHT_IDLE_N;
    t->light_auto = choice > 0;
    if (choice > 0) t->light_idle_s = LIGHT_IDLE_CHOICES[choice - 1];
    t->light_manual_off = false;
}

void tank_scatter_food(tank_t *t, int n) {
    for (int i = 0; i < MAX_FOOD && n > 0; i++) {
        if (t->food[i].alive) continue;
        t->food[i].alive = true; t->food[i].from_player = false;
        t->food[i].x = tank_randf(t, TANK_FX0 + 25, TANK_FX1 - 25);
        t->food[i].y = tank_glass_top(t->food[i].x) + tank_randf(t, 6, 20);
        t->food[i].age = 0; t->food[i].floor_s = 0; t->food[i].nibbled = 0;
        n--;
    }
}

/* see tank.h: dark-screen physiology only, rates per real hour of sleep */
#define SLEEP_HUNGER_PER_H 0.8f    /* fed -> ravenous over ~7 h of sleep */
#define SLEEP_ENERGY_PER_H 2.0f
#define SLEEP_STRESS_PER_H 2.0f
#define SLEEP_SLICE_S      300.0f  /* the night's growth and grazing interleave at this grain */
void tank_tick_sleep(tank_t *t, float seconds) {
    if (seconds <= 0) return;
    float h = seconds / 3600.0f;
    t->clock += seconds;
    for (int i = 0; i < t->n_fish; i++) {
        fish_t *f = &t->fish[i];
        f->hunger = clampf(f->hunger + SLEEP_HUNGER_PER_H * h, 0, t->companion.cozy ? 6 : 10);
        f->energy = clampf(f->energy + SLEEP_ENERGY_PER_H * h * ((t->aquariums.stat_upgrades & STAT_CURRENT) ? 1.25f : 1), 0, 10);
        f->stress = clampf(f->stress - SLEEP_STRESS_PER_H * h * ((t->aquariums.stat_upgrades & STAT_SHELTER) ? 1.25f : 1), 0, 10);
        if (tank_fish_comfort(t, i) < 0 && f->stress < 2) f->stress = 2;   /* missing its favorite piece */
        if (f->stress < tank_calm_floor(t, i)) f->stress = tank_calm_floor(t, i);
        f->speed = 0; f->target_speed = 0; f->speed_vel = 0; f->turn_vel = 0; f->bored = 0; f->breather = 0;  /* a night's sleep is a fresh start */
        f->goal_age += seconds; f->ask_age += seconds;  /* wake re-asks the advisor at once */
    }
    t->idle_s = 0;                                      /* the wake press is handling: lights up */
    for (int i = 0; i < MAX_FOOD; i++)                  /* overnight pellets go stale */
        if (t->food[i].alive && (t->food[i].age += seconds) > 45) t->food[i].alive = false;
    coral_grow(t, seconds);
    cluster_grow(t, seconds);
    shrimp_sleep(t, seconds);
    if (t->sd_unlocks & SD_ITEM_SEAHORSE) { t->sh_s += seconds; t->sh_snack_cool = fmaxf(0, t->sh_snack_cool - seconds); }   /* days with the keeper run through the night */
    t->reef_flow_s = 0; t->reef_flow_cooldown = 0;
    t->reef_flow_seen = 0;


}

/* the schema's 3 x 2 zone grid (advisor_core.c encodes the same), 0..5 */
static int zone_of(float x, float y) {
    int col = (int)(x / (TANK_W / 3.0f)); if (col > 2) col = 2; if (col < 0) col = 0;
    int row = (int)(y / (TANK_BOT / 2.0f)); if (row > 1) row = 1; if (row < 0) row = 0;
    return row * 3 + col;
}

int tank_familiar_friend(const tank_t *t, int idx) {
    if (idx < 0 || idx >= t->n_fish) return -1;
    const fish_t *f = &t->fish[idx];
    int best = -1; unsigned score = 29;
    for (int i = 0; i < t->n_fish; i++) {
        if (i != idx && f->familiar_id[i] == t->fish[i].social_id && f->familiar[i] > score) {
            best = i; score = f->familiar[i];
        }
    }
    return best;
}

int tank_favorite_decor(const tank_t *t, int idx) {
    if (idx < 0 || idx >= t->n_fish) return -1;
    uint32_t best = UINT32_MAX;
    int item = -1;
    for (int i = 0; i < SD_ITEM_COUNT; i++) {
        if (!tank_decor_placeable(i) || !tank_decor_owned(t, i)) continue;
        uint32_t score = (uint32_t)(t->fish[idx].preset + 1) * 2654435761u ^ (uint32_t)(i + 1) * 2246822519u;
        score ^= score >> 16; score *= 3266489917u; score ^= score >> 13;
        if (score < best) { best = score; item = i; }
    }
    return item;
}

/* A fish's loved piece is part of its nature: fixed from its life id and
 * variety, so it costs no save bytes. Present -> calmer and faster growing;
 * missing -> a little restless. */
static const uint8_t FAVORITE_ITEMS[] = {
    1, 2, 3, 9, 10, 11, 12, 16, 18, 19, 20, 23, 27,
    SD_SCENERY_BRAIN, SD_SCENERY_ANEMONE, SD_DECOR_FIRST + 7, SD_DECOR_FIRST + 10,
    SD_FOLIAGE_FIRST, SD_FOLIAGE_FIRST + 1, SD_FOLIAGE_FIRST + 2, SD_FOLIAGE_FIRST + FOLIAGE_KELP,
};
int tank_fish_favorite_item(const tank_t *t, int idx) {
    if (!t || idx < 0 || idx >= t->n_fish) return -1;
    uint32_t h = (t->polish.active[idx].id + 1u) * 2654435761u ^ (uint32_t)(t->fish[idx].variety + 7) * 40503u;
    h ^= h >> 15; h *= 2246822519u; h ^= h >> 13;
    return FAVORITE_ITEMS[h % (uint32_t)(sizeof FAVORITE_ITEMS)];
}
bool tank_item_in_tank(const tank_t *t, int item) {
    if (!t) return false;
    if (SD_IS_FOLIAGE(item))
        return t->foliage.version == 1 && (t->foliage.planted & (1u << (item - SD_FOLIAGE_FIRST))) != 0;
    return tank_decor_owned(t, item);
}
int tank_fish_comfort(const tank_t *t, int idx) {
    int item = tank_fish_favorite_item(t, idx);
    return item < 0 ? 0 : tank_item_in_tank(t, item) ? 1 : -1;
}
/* The lowest stress a fish settles to (2026-10-30): calm is earned. A
 * newcomer's trust leaves it ~6 calm at best; patient holds raise trust and
 * with it the ceiling, and its favorite decor lowers the floor a quarter. */
float tank_calm_floor(const tank_t *t, int idx) {
    if (idx < 0 || idx >= t->n_fish) return 0;
    float settle = (10 - clampf(t->fish[idx].trust, 0, 10)) * 0.7f;
    return tank_fish_comfort(t, idx) > 0 ? settle * 0.75f : settle;
}

void tank_mussel_pose(const tank_t *t, float *x, float *y, float *crawl) {
    static const float anchors[] = {.82f, .68f, .48f, .30f, .46f, .64f};
    const float interval = 360, travel = 16;
    const int stops = sizeof anchors / sizeof *anchors;
    float clock = fmodf(fmaxf(t->clock, 0), interval * stops);
    int stop = (int)(clock / interval);
    float phase = clock - stop * interval, progress = fmaxf(phase - (interval - travel), 0) / travel;
    float ease = progress * progress * (3 - 2 * progress);
    float from = anchors[stop], to = anchors[(stop + 1) % stops];
    *x = (from + (to - from) * ease) * TANK_W;
    *crawl = progress > 0 ? to > from ? 1 : -1 : 0;
    *y = TANK_BOT - 11 + (*crawl ? sinf(phase * 3) * .6f : 0);
}

bool tank_mussel_owned(const tank_t *t) {
    return t && t->aquariums.active < 8 &&
        (t->mussel.version < 2 ? t->mussel.owned != 0 :
         (t->mussel.owned & (1u << t->aquariums.active)) != 0);
}
bool tank_interest_spot(const tank_t *t, int idx, float *x, float *y) {
    if (idx < 0 || idx >= t->n_fish) return false;
    const fish_t *f = &t->fish[idx];
    float phase = fmodf(fmaxf(t->clock, 0) + idx * 19, 90);
    if (tank_mussel_owned(t) &&
        (phase < 8 || (t->mussel_glint > 0 && idx == (int)(t->clock / 90) % t->n_fish)) &&
        f->hunger < 7 && f->stress < 5) {
        float crawl;
        tank_mussel_pose(t, x, y, &crawl);
        *y -= 36;
        return true;
    }
    int item = tank_favorite_decor(t, idx);
    if (item < 0) return false;
    *x = tank_decor_x(t, item); *y = TANK_BOT - 42;
    return true;
}

void tank_rest_spot(const tank_t *t, int idx, float *x, float *y) {
    const fish_t *f = &t->fish[idx];
    *x = t->reef_x + 13 + f->rest_dx; *y = TANK_BOT - 50 + f->rest_dy;
    int item = tank_favorite_decor(t, idx);
    if (item >= 0) *x = tank_decor_x(t, item) + (f->bold - .5f) * 24;
    int friend = tank_familiar_friend(t, idx);
    /* Only followers settle beside an earlier fish; mutual resting cannot drift. */
    if (friend >= 0 && friend < idx && t->fish[friend].goal.id == GOAL_REST &&
        t->fish[friend].hunger < 7 && t->fish[friend].stress < 5) {
        *x = t->fish[friend].x + tank_fish_half_width(f) +
            tank_fish_half_width(&t->fish[friend]) + 10;
        *y = t->fish[friend].y + (idx & 1 ? -8 : 8);
    }
    tank_glass_clamp(x, y, tank_fish_half_width(f) + 4);
}

/* ---- goal → target point + cruise speed (prototype targetForGoal, x0.55) ---- */
typedef struct { float x, y, speed; bool valid; } target_t;

static target_t target_for_goal(tank_t *t, int idx, goal_id_t goal) {
    fish_t *f = &t->fish[idx];
    float tm = t->clock;
    target_t tg = {
        f->x + cosf(f->heading + sinf(f->wander) * 0.8f) * 50,
        f->y + sinf(f->heading + cosf(f->wander * 0.7f) * 0.45f) * 39,
        lerpf(12, 23, f->bold) * (1 - f->lazy * 0.35f), true,
    };
    switch (goal) {
    case GOAL_SEEK_FOOD: {
        float d; int i = tank_nearest_food(t, f, &d);
        if (i >= 0) {
            /* hunger governs pursuit aggression regardless of which brain set
             * the goal: a peckish fish saunters (~0.75x), a starving one
             * charges (~1.3x) and barely brakes on approach */
            float h = clampf(f->hunger / 10.0f, 0, 1);
            float slow = clampf(d / 60, 0.4f + h * 0.35f, 1);
            tg.x = t->food[i].x; tg.y = t->food[i].y;
            tg.y = fminf(tg.y, TANK_SAND_TOP - tank_fish_clearance(f));
            tg.speed = lerpf(29, 62, f->bold) * slow * (0.75f + h * 0.55f) *
                       (1 - 0.18f * f->lazy);
            return tg;   /* skip the margin clamp: pellets rest at the floor */
        }
        tg.valid = false;
        break;
    }
    case GOAL_FLEE_SHADOW: {
        /* the roaming shadow was removed 2026-09-13; the token stays in the
         * frozen schema (the state line always reads `shadow none`, which the
         * model was trained on). If it still says flee, the fish bolts away
         * from the surface centre - the old no-shadow fallback. */
        float sx = TANK_W * 0.5f, sy = -60;
        float away = atan2f(f->y - sy, f->x - sx);
        tg.x = f->x + cosf(away) * 105; tg.y = f->y + sinf(away) * 83;
        tg.speed = lerpf(51, 83, f->bold);
        break;
    }
    case GOAL_VISIT_BUBBLES:
        /* each visitor keeps its own lane: a per-fish phase and radius, so
         * four fish orbit the column instead of stacking on one point */
        tg.x = t->bubble_x + sinf(tm * 1.2f + f->wander + idx * 1.9f) * (26 + idx * 6);
        tg.y = t->bubble_y - 74 + cosf(tm * 0.8f + f->wander + idx * 1.3f) * (34 + idx * 6);
        tg.speed = 23;
        break;
    case GOAL_FOLLOW_FRIEND: {
        float d; int i = tank_familiar_friend(t, idx);
        if (i < 0) i = tank_nearest_friend(t, idx, &d);
        else d = tank_dist(f->x, f->y, t->fish[i].x, t->fish[i].y);
        if (i >= 0) {
            const fish_t *fr = &t->fish[i];
            if (fr->goal.id == GOAL_SEEK_FOOD || fr->goal.id == GOAL_REST || fr->goal.id == GOAL_FLEE_SHADOW) break;
            /* One leader per pair, with a steady trailing lane rather than mutual orbiting. */
            if (fr->goal.id == GOAL_FOLLOW_FRIEND && idx < i) break;
            float spacing = tank_fish_half_width(f) + tank_fish_half_width(fr) + 12;
            tg.x = fr->x - cosf(fr->heading) * spacing;
            tg.y = fr->y + (idx < i ? -12 : 12);
            tg.speed = clampf(fr->speed + (d - spacing) * .3f, 8, 30);
        } else tg.valid = false;
        break;
    }
    case GOAL_REST:
        tank_rest_spot(t, idx, &tg.x, &tg.y);
        tg.speed = 6 + f->bold * 4;
        break;
    case GOAL_DART_PLAY:
        if (f->dart_timer <= 0) {
            rand_water(t, 38, 40, TANK_BOT - 64, &f->dart_x, &f->dart_y);
            f->dart_timer = tank_randf(t, 0.8f, 1.5f);
        }
        tg.x = f->dart_x; tg.y = f->dart_y;
        tg.speed = lerpf(42, 58, f->bold);
        break;
    case GOAL_INSPECT_REEF: {
        float rx, ry; tank_reef_spot(t, &rx, &ry);   /* the reef cluster, once there is one */
        tank_interest_spot(t, idx, &rx, &ry);
        tg.x = rx + sinf(tm * 0.65f + f->wander + idx * 1.7f) * (39 + idx * 4);
        tg.y = ry - REEF_ORBIT_UP + cosf(tm * 0.8f + f->wander + idx * 1.1f) * (15 + idx * 4);
        tg.speed = (10 + f->curiosity * 1.2f) * (.65f + f->bold * .35f);
        break;
    }
    case GOAL_EXPLORE: {
        /* a destination, not a drift (2026-09-14). Before this, explore was
         * the wander target above - 50 px ahead of the nose with a wobble -
         * a random walk that never left the neighbourhood, so an explore
         * decision looked like idling (Strato: "they don't explore the tank
         * very much"). Now the fish picks the zone it has seen least recently
         * (one of the two stalest, so two explorers don't take the same line),
         * cruises to a point inside it, and on arrival picks the next. The
         * model still owns the goal; this is the reflex layer resolving the
         * concrete target, as it does for every other goal. */
        if (!f->explore_set || tank_dist(f->x, f->y, f->explore_x, f->explore_y) < 10) {
            int cur = zone_of(f->x, f->y), best = -1, second = -1;
            for (int z = 0; z < 6; z++) {
                if (z == cur) continue;
                if (best < 0 || f->zone_seen[z] < f->zone_seen[best]) { second = best; best = z; }
                else if (second < 0 || f->zone_seen[z] < f->zone_seen[second]) second = z;
            }
            int z = (second >= 0 && (xr(t) % 3) == 0) ? second : best;
            int col = z % 3, row = z / 3;
            float cell = TANK_W / 3.0f;
            float margin = 15 * f->size + 3;
            float x0 = col ? col * cell + 12 : margin;
            float x1 = col < 2 ? (col + 1) * cell - 12 : TANK_W - margin;
            f->explore_x = tank_randf(t, x0, x1);
            f->explore_y = tank_randf(t, row * (TANK_BOT / 2.0f) + 42, (row + 1) * (TANK_BOT / 2.0f) - 40);
            f->explore_set = true;
        }
        /* the wander wobble bends the line so it reads as a swim, not a bee-line */
        tg.x = f->explore_x + sinf(f->wander) * 18;
        tg.y = f->explore_y + cosf(f->wander * 0.7f) * 12;
        break;
    }
    default: break;
    }
    float m = 23;
    float side = 15 * f->size + 3;
    tg.x = clampf(tg.x, side, TANK_W - side);
    tg.y = clampf(tg.y, m + 9, TANK_H - m);
    corner_clamp(&tg.x, &tg.y, m + 4);
    return tg;
}

static float wall_avoidance(const fish_t *f, float *weight) {
    const float m = 34;
    float vx = 0, vy = 0;
    float side = 15 * f->size + 6;
    if (f->x < side)          vx += 1 - f->x / side;
    if (f->x > TANK_W - side) vx -= 1 - (TANK_W - f->x) / side;
    if (f->y < m + 6)      vy += 1 - (f->y - 6) / m;
    float bottom = TANK_SAND_TOP - tank_fish_clearance(f);
    if (f->y > bottom - 4) vy -= (f->y - (bottom - 4)) / m;
    /* A normalized angle alone made a subpixel boundary crossing apply the
     * full steer, then remove it next frame: the fish seesawed along the edge. */
    float blend = clampf(fmaxf(fabsf(vx) * side, fabsf(vy) * m) / 4, 0, 1);
    *weight = 0.62f * blend * blend * (3 - 2 * blend);
    return blend > 0 ? atan2f(vy, vx) : 0;
}

static void eat_nearby_food(tank_t *t, fish_t *f) {
    for (int i = 0; i < MAX_FOOD; i++) {
        if (!t->food[i].alive) continue;
        float food_y = t->food[i].y;
        if (food_y >= TANK_SAND_TOP)
            food_y = fminf(food_y, TANK_SAND_TOP - tank_fish_clearance(f));
        if (tank_dist(f->x, f->y, t->food[i].x, food_y) < 12 * f->size + 4) {
            t->food[i].alive = false;
            if (t->food[i].from_player && f->hunger > 3) f->trust = fminf(10, f->trust + 0.05f);   /* a hungry fish fed by hand learns the keeper */
            f->hunger = clampf(f->hunger - 4.3f, 0, 10);
            f->energy = clampf(f->energy + 1.0f, 0, 10);
            f->curiosity = clampf(f->curiosity + 0.5f, 0, 10);   /* was 0.8: a trickle burst
                                                                     re-synced the school's curiosity */
            f->eaten++;
            tank_emit(TEV_EAT, (int)(f - t->fish));
            if (t->food[i].from_player) {
                f->eaten_player++;
                if (t->feed_open) { t->player_feedings++; t->feed_open = false; }   /* the gesture became a meal */
            }
            /* post-meal reflex from the prototype */
            if (f->hunger < 2.2f && f->goal.id == GOAL_SEEK_FOOD)
                f->goal.id = (xr(t) & 1) ? GOAL_VISIT_BUBBLES : GOAL_EXPLORE;
            return;   /* one bite per frame: a fish in a cloud of pellets takes
                         them one at a time, so a tank-mate gets a look in */
        }
    }
}

static void update_fish(tank_t *t, int idx, float dt) {
    fish_t *f = &t->fish[idx];
    if (f->goal.id == GOAL_VISIT_BUBBLES) {
        f->goal.id = GOAL_EXPLORE;
        f->goal_age = f->ask_age = 0;
        f->at_bubbles = false;
    }
    f->wearable = t->companion.active[idx].wearable;
    f->wear_colors = t->companion.active[idx].wear_colors;
    /* drives (prototype rates, speed rescaled by the same 0.55) */
    f->hunger    = clampf(f->hunger + dt * (HUNGER_PER_S + f->bold * HUNGER_BOLD_PER_S +
                          (f->goal.id == GOAL_DART_PLAY ? HUNGER_DART_PER_S : 0)), 0, 10);
    /* curiosity: exploring feeds it (prototype) and - new 2026-09-01 -
     * satisfying it SPENDS it: a fish that has reached the bubbles or the
     * reef uses curiosity up there, the way rest restores energy and a meal
     * drops hunger. Before this a fed fish had no sink at all (+0.8 per
     * pellet, nothing ever took it back), so after days the whole school sat
     * at curiosity 9 and the advisor - reading "fed, calm, curious" exactly
     * as trained - parked all four at the bubble column for good. The model
     * still owns the goal; it just sees an honest drive, and the curiosity
     * band is in the re-ask signature so it gets to react. */
    {
        float dc = f->goal.id == GOAL_EXPLORE ? 0.08f : -0.025f;
        if (f->goal.id == GOAL_VISIT_BUBBLES || f->goal.id == GOAL_INSPECT_REEF) {
            bool bub = f->goal.id == GOAL_VISIT_BUBBLES;
            float sx = t->bubble_x, sy = t->bubble_y - 74;
            if (!bub) { tank_reef_spot(t, &sx, &sy); sy -= REEF_ORBIT_UP; }
            bool at = tank_dist(f->x, f->y, sx, sy) < CURIOSITY_SPEND_RADIUS;
            if (at) dc = -CURIOSITY_SPEND_PER_S;
            if (bub && at && !f->at_bubbles) tank_emit(TEV_BUBBLES, idx);   /* arrival at the column: the play cue */
            f->at_bubbles = bub && at;
        } else f->at_bubbles = false;
        if (dc > 0 && (t->aquariums.stat_upgrades & STAT_ENRICHMENT)) dc *= 1.25f;
        f->curiosity = clampf(f->curiosity + dt * dc, 0, 10);
    }
    {
        int comfort = tank_fish_comfort(t, idx);
        bool rest = f->goal.id == GOAL_REST;
        /* creature roles: the snail keeps the glass clear (calm +10%); the
           urchin grazes the floor quiet so resting fish settle 20% faster */
        float roles = ((t->sd_unlocks & SD_ITEM_SNAIL) ? 1.10f : 1) * (rest && (t->sd_unlocks & SD_ITEM_URCHIN) ? 1.20f : 1);
        f->stress = clampf(f->stress - dt * (rest ? 0.48f : 0.18f) * roles *
                           ((t->aquariums.stat_upgrades & STAT_SHELTER) ? 1.25f : 1) * (comfort > 0 ? 1.3f : 1), 0, 10);
        if (comfort < 0 && f->stress < 2.5f) f->stress = fminf(2.5f, f->stress + dt * 0.02f);
        float settle = tank_calm_floor(t, idx);      /* calm is cultivated: it settles only as far as trust allows */
        if (f->stress < settle) f->stress = fminf(settle, f->stress + dt * 0.02f);
    }
    /* energy: the prototype's per-second drain (full to empty in ~3 min of
     * cruising) kept the school at energy ~3 - permanently tired, so the
     * advisor never had a fish fit enough to dart and read every content
     * fish as "relaxed": bubbles, reef, follow (2026-09-01 census). Now a
     * fish cruises ~10 min on a full tank and a rest refills it in ~35 s. */
    bool settled = false;                        /* a breather, settled in the fronds */
    if (f->breather > 0) { float bx, by; breather_spot(t, f, idx, &bx, &by); settled = tank_dist(f->x, f->y, bx, by) < BREATHER_NEAR; }
    f->energy    = clampf(f->energy + dt * ((f->goal.id == GOAL_REST || settled) ? 0.30f * ((t->aquariums.stat_upgrades & STAT_CURRENT) ? 1.25f : 1)
                          : -0.012f - f->speed / 4000.0f), 0, 10);
    /* the breather (see BREATHER_AT): in when spent by day, out with the wind
       back, never over a flight, the famine's begging or the night's own rest */
    if (f->breather > 0) {
        if (f->energy >= BREATHER_TO || t->night || f->goal.id == GOAL_FLEE_SHADOW || (t->ravenous && f->hunger > 6.5f)) f->breather = 0;
        else f->breather += dt;
    } else if (!t->night && f->energy < BREATHER_AT && f->goal.id != GOAL_FLEE_SHADOW && !(t->ravenous && f->hunger > 6.5f))
        f->breather = dt;
    bool hidden = tank_reef_calm_near(t, f->x, f->y);
    if (hidden) f->stress = clampf(f->stress - dt * 0.22f, 0, 10);

    f->wander += dt * (0.65f + f->curiosity * 0.04f) + sinf(t->clock + f->x * 0.01f) * dt * 0.12f;
    if (f->dart_timer > 0) f->dart_timer -= dt;
    f->goal_age += dt; f->ask_age += dt;
    if (f->social_cooldown > 0) f->social_cooldown = fmaxf(0, f->social_cooldown - dt);
    /* boredom: the same pastime goes stale (a minute to the top); a meal or a
     * night's rest is never boring. A zone the fish has not seen for a while
     * is a small relief - so a real explore across the tank roughly pays for
     * itself, while an orbit at the bubble column or a tail-chase behind a
     * friend only accrues. (A zone boundary under the orbit gives nothing:
     * the zone has to be BORED_ZONE_STALE_S stale.) The new-goal relief is in
     * tank_tick where goals change. The band is in state_signature, so a fish
     * crossing into "bored" is re-asked - and the v4 model reads the value. */
    {
        goal_id_t g = f->goal.id;
        bool leisure = g == GOAL_VISIT_BUBBLES || g == GOAL_FOLLOW_FRIEND || g == GOAL_INSPECT_REEF ||
                       g == GOAL_DART_PLAY || g == GOAL_EXPLORE || (g == GOAL_REST && !t->night);
        f->bored = clampf(f->bored + dt * (leisure ? BORED_PER_S : -BORED_RELIEF_PER_S), 0, 10);
        int z = zone_of(f->x, f->y);
        if (z != f->zone_last) {
            if (f->zone_last >= 0 && t->clock - f->zone_seen[z] > BORED_ZONE_STALE_S)
                f->bored = clampf(f->bored - BORED_NEW_ZONE, 0, 10);
            f->zone_last = (int8_t)z;
        }
        f->zone_seen[z] = t->clock;
    }

    target_t tg = target_for_goal(t, idx, f->goal.id);
    /* fish prefer shallow climb/dive angles while cruising; full vertical
     * agility stays available for urgent goals */
    bool agile = f->goal.id == GOAL_FLEE_SHADOW || f->goal.id == GOAL_DART_PLAY;
    float vertical = agile ? 1.0f : 0.72f;
    /* A floor target is clamped to this fish's clearance. Don't promote
     * sub-pixel rounding at that shared boundary into a close-dive steer. */
    if (f->goal.id == GOAL_SEEK_FOOD && tg.y - f->y > 0.5f) {
        float approach = clampf(1 - tank_dist(f->x, f->y, tg.x, tg.y) / 80, 0, 1);
        vertical = lerpf(vertical, 1.0f, approach);
    }
    float desired = atan2f((tg.y - f->y) * vertical, tg.x - f->x);
    /* final food approach: let the fish dip to the floor for the pellet
     * instead of hovering above it on wall-avoidance (the "staring" bug) */
    bool final_approach = f->goal.id == GOAL_SEEK_FOOD &&
                          tank_dist(f->x, f->y, tg.x, tg.y) < 40;
    if (f->goal.id == GOAL_REST && tg.valid) {
        float arrival = clampf(tank_dist(f->x, f->y, tg.x, tg.y) / 36, 0, 1);
        float hover = 1.2f + f->bold * 2.4f + (1 - f->lazy) * 0.5f;
        tg.speed = lerpf(hover, tg.speed, arrival);
    }
    float wall_weight;
    float push = wall_avoidance(f, &wall_weight);
    if (wall_weight > 0 && !final_approach)
        desired = norm_ang(desired + norm_ang(push - desired) * wall_weight);

    /* touch: spooked fish bolt from the tap site; trusting fish drift to a
     * resting finger (reflex-layer, independent of the advisor's goal) */
    float touch_speed = -1;
    if (t->startled && f->goal.id != GOAL_FLEE_SHADOW) {
        float d = tank_dist(f->x, f->y, t->startle_x, t->startle_y);
        if (d < STARTLE_RADIUS * 1.6f) {
            float away = atan2f(f->y - t->startle_y, f->x - t->startle_x);
            desired = norm_ang(desired + norm_ang(away - desired) * 0.85f);
            touch_speed = lerpf(55, 90, f->bold);
        }
    } else if (idx == t->stage_fish) {
        /* on stage (setup): a lazy figure-of-eight round the page's clear
         * spot, turning at each end so both flanks show; a fish far from it
         * cruises over first. Above every other presentation: the keeper is
         * looking at THIS fish. */
        float ph = t->clock * 1.1f + f->wander;
        float wx = t->stage_x + cosf(ph) * 24, wy = t->stage_y + sinf(ph * 2) * 6;
        float to = atan2f(wy - f->y, wx - f->x);
        desired = norm_ang(desired + norm_ang(to - desired) * 0.85f);
        touch_speed = tank_dist(f->x, f->y, t->stage_x, t->stage_y) > 80 ? 44 : 20;
    } else if (t->spawning && (idx == t->court_a || idx == t->court_b) &&
               f->goal.id != GOAL_FLEE_SHADOW && !(t->ravenous && f->hunger > 6.5f)) {
        /* the spawning (2026-09-24): the courtship that ends in a fry. It
         * outranks a resting finger and the greeting - this is the moment the
         * keeper is here for - and hunger short of the famine doesn't veto
         * it; a starving parent still begs first (feed it, it comes back). */
        court_steer(t, f, idx, &desired, &touch_speed);
    } else if (t->hold_active && t->hold_time >= HOLD_ATTRACT_S &&
               f->goal.id != GOAL_FLEE_SHADOW && f->trust >= 4.0f &&
               f->hunger < HOLD_HUNGER_VETO) {
        /* only a settled hold (~3 s of contact) draws fish in - quick taps and
         * card-taps never twitch the school - and a strongly hungry fish has
         * better things to do than visit a finger */
        float d = tank_dist(f->x, f->y, t->hold_x, t->hold_y);
        if (d < HOLD_RADIUS) {
            float w = (f->trust - 4.0f) / 6.0f;          /* 0..1 with trust */
            float to = atan2f(t->hold_y - f->y, t->hold_x - f->x);
            if (d > 28) desired = norm_ang(desired + norm_ang(to - desired) * (0.5f + 0.45f * w));
            /* far fish cross the tank at a cruise, not a drift; close in
             * they slow to arrive and hover */
            float far = d > 120 ? 1.0f : d / 120.0f;
            touch_speed = d > 28 ? lerpf(14, 30, w) + far * lerpf(10, 20, w) : 4;
        }
    } else if (t->greet_timer > 0 && f->trust >= 7.0f && f->goal.id != GOAL_FLEE_SHADOW &&
               f->goal.id != GOAL_SEEK_FOOD) {
        /* light-on greeting: trusting fish come up front to see who's there */
        float gx = TANK_W * 0.5f + (idx - t->n_fish * 0.5f) * 34, gy = 70;
        float d = tank_dist(f->x, f->y, gx, gy);
        if (d > 24) {
            float to = atan2f(gy - f->y, gx - f->x);
            desired = norm_ang(desired + norm_ang(to - desired) * 0.7f);
            touch_speed = 26;
        } else touch_speed = 5;
    } else if (t->ravenous && f->goal.id != GOAL_FLEE_SHADOW && f->hunger > 6.5f) {
        /* starving tank: reflex presentation of the famine, like the greet/
         * hold overrides above - the advisor still owns the goal. Two phases:
         * empty water = beg where meals come from (quick darts back and forth
         * under the feed spot, unmistakably "feed me"); live pellets = the
         * DASH - a real starving fish bolts the moment food hits the water,
         * it doesn't wait to think it over (the advisor's seek_food arrives
         * seconds later and takes back a fish that is already eating). A fish
         * that has eaten (hunger <= 6.5) drops out of the frenzy at once. */
        float fd; int fi = tank_nearest_food(t, f, &fd);
        if (fi >= 0) {
            float to = atan2f(t->food[fi].y - f->y, t->food[fi].x - f->x);
            desired = norm_ang(desired + norm_ang(to - desired) * 0.92f);
            touch_speed = fd > 14 ? lerpf(85, 115, clampf(f->hunger / 10.0f, 0, 1)) : 30;
        } else {
            float spot = t->feed_spot_x >= 0 ? t->feed_spot_x : TANK_W * 0.5f;
            float wx = spot + sinf(t->clock * (1.6f + f->bold * 0.9f) + idx * 2.1f) * (34 + idx * 6);
            float wy = 30 + idx * 4 + sinf(t->clock * 3.1f + f->wander) * 6;
            float to = atan2f(wy - f->y, wx - f->x);
            desired = norm_ang(desired + norm_ang(to - desired) * 0.85f);
            touch_speed = tank_dist(f->x, f->y, wx, wy) > 18
                        ? lerpf(40, 68, clampf(f->hunger / 10.0f, 0, 1)) : 22;
        }
    } else if (f->breather > 0) {
        /* the breather: to the fronds at a tired cruise, then a hover (the
         * canopy's own slowing below keeps it gentle); the goal stays */
        float bx, by; breather_spot(t, f, idx, &bx, &by);
        float d = tank_dist(f->x, f->y, bx, by);
        if (d > 10) { float to = atan2f(by - f->y, bx - f->x); desired = norm_ang(desired + norm_ang(to - desired) * 0.85f); }
        touch_speed = d > BREATHER_NEAR ? 18 : 3;
    } else if (t->court_active > 0 && !t->ravenous &&
               (idx == t->court_a || idx == t->court_b) &&
               f->goal.id != GOAL_FLEE_SHADOW && f->hunger < HOLD_HUNGER_VETO) {
        /* the tell: an episode of the courtship circle, the arrival close */
        court_steer(t, f, idx, &desired, &touch_speed);
    }

    /* separation - personal space. Fish are ~40 px long; the old 16 px
     * radius let four content fish stack on one landmark like a pile of
     * cards (2026-09-01). Now a body length, weighted by how close they are;
     * a fleeing fish keeps its line. */
    if (f->goal.id != GOAL_FLEE_SHADOW)
        for (int i = 0; i < t->n_fish; i++) {
            if (i == idx) continue;
            float r = 36 * f->size;
            if (fabsf(f->x - t->fish[i].x) >= r || fabsf(f->y - t->fish[i].y) >= r) continue;
            float d = tank_dist(f->x, f->y, t->fish[i].x, t->fish[i].y);
            if (d < r) {
                float away = atan2f(f->y - t->fish[i].y, f->x - t->fish[i].x);
                desired = norm_ang(desired + norm_ang(away - desired) * (0.35f + 0.5f * (1 - d / r)));
            }
        }

    /* urgency scales cruise speed a touch (0..9 → 0.8..1.25) */
    float ugain = 0.8f + f->goal.urgency * 0.05f;
    /* a U-turn is a YAW (2026-09-29): a fish whose way lies behind it no longer
     * loops over through vertical and rolls flat - it turns round where it is,
     * toward the glass. (A YouTube viewer on the old roll: "that looks bad".)
     * It swims on one SIDE (facing) and changes side only by a committed turn:
     * the way must stay behind it for a moment - ~2 s hovering (3 at its
     * spot), a third of a second cruising, at once when it matters (a dash,
     * a startle, darting play) - and a turn is followed by ~1.3 s of settling. Until it commits
     * it glances back (a part turn) and eases off, steering on its own side:
     * the way mirrored across vertical, its climb or dive kept. Strato, the
     * same night, on the first cut (a turn the moment the way fell behind):
     * "too much time in a vertical position or flip flopping back and forth" -
     * 21 flips a fish-minute, 34 at the bubble column, where the lane swings
     * side to side every ~5 s. */
    const float BEHIND_BAND  = 0.15f;    /* |cos| past vertical before the way is "behind" */
    const float TURN_SETTLE  = 1.3f;     /* s after a turn before the next can begin */
    if (!f->facing) f->facing = cosf(f->heading) < 0 ? -1 : 1;
    bool urgent = agile || (t->ravenous && f->hunger > 6.5f) || (t->startled && touch_speed >= 55);
    float desired_cos = cosf(desired);
    bool behind = desired_cos * f->facing < -BEHIND_BAND;
    bool settling = f->behind < 0;
    if (settling) f->behind = fminf(0, f->behind + dt);
    else if (behind) f->behind += dt;
    else f->behind = 0;
    /* hanging about its spot (the goal's point within ~30 px) a fish is in
     * no hurry: the lane swinging past it is a glance, not a turn */
    bool at_spot = tg.valid && tank_dist(f->x, f->y, tg.x, tg.y) < 30;
    float commit = urgent ? 0 : lerpf(2.0f, 0.3f, clampf((f->speed - 10) * (1 / 25.0f), 0, 1)) + (at_spot ? 1.2f : 0);
    if (!settling && behind && f->behind >= commit) {
        f->facing = (int8_t)-f->facing;
        f->heading = norm_ang(3.14159f - f->heading);   /* the climb or dive kept */
        f->turn_vel = -f->turn_vel;
        f->behind = -(urgent ? 0.65f : TURN_SETTLE);
    }
    bool glancing = behind && f->behind > 0;
    if (desired_cos * f->facing < 0)   /* on its own side until it turns */
        desired = atan2f(sinf(desired), f->facing * fabsf(desired_cos));
    /* the turn eases in and out (2026-11): the heading's angular velocity
     * ramps toward the fastest rate that can still stop on the way, so a
     * fish rolls into a curve and straightens out of it instead of snapping
     * between full lock and none. Urgent swims ramp ~2.5x harder */
    float err = norm_ang(desired - f->heading);
    float turn_accel = f->turn_rate * (urgent ? 24.0f : 9.0f), brake_q = turn_accel * dt * 0.5f;
    /* the fastest rate that still brakes onto the way in whole ticks */
    float reach = fminf(f->turn_rate, sqrtf(2 * turn_accel * fabsf(err) + brake_q * brake_q) - brake_q);
    f->turn_vel += clampf(copysignf(reach, err) - f->turn_vel, -turn_accel * dt, turn_accel * dt);
    float turn = f->turn_vel * dt;
    if (turn * err > 0 && fabsf(turn) > fabsf(err)) { turn = err; f->turn_vel = dt > 0 ? err / dt : 0; }
    f->heading = norm_ang(f->heading + turn);
    /* the yaw follows the side: a full turn in ~0.5 s at mira's turn rate
     * (bolt 0.4, nori 0.7), quicker with urgency; a glance is 40% of one.
     * The tail follows the head a beat behind */
    float hc = cosf(f->heading);
    float face = f->facing * (glancing ? 0.6f : 1.0f);
    float yrate = f->turn_rate * 1.2f * (1 + f->goal.urgency * 0.06f) * dt;
    f->yaw += clampf(face - f->yaw, -yrate, yrate);
    f->yaw_tail += (f->yaw - f->yaw_tail) * clampf(dt * 10, 0, 1);
    float want = touch_speed >= 0 ? touch_speed : tg.speed * ugain;
    f->target_speed = want * (f->energy < 1.2f && f->breather <= 0 ? 0.45f : 1) * (glancing ? 0.6f : 1);
    /* swimming through a canopy is slow going (a fleeing fish crashes through) */
    if (f->goal.id != GOAL_FLEE_SHADOW)
        if (hidden) f->target_speed *= 0.80f;
    /* the pace is sprung, critically damped (implicit, stable at any dt): a
     * glance, a canopy or a recovery eases in and out of its new speed
     * instead of kicking the fish's acceleration. Never past the target */
    {
        float omega = urgent ? 10.0f : 5.5f, before = f->speed;
        f->speed_vel = (f->speed_vel + dt * omega * omega * (f->target_speed - f->speed)) /
                       (1 + 2 * omega * dt + omega * omega * dt * dt);
        f->speed += f->speed_vel * dt;
        if ((f->speed - f->target_speed) * (before - f->target_speed) < 0) { f->speed = f->target_speed; f->speed_vel = 0; }
        if (f->speed < 0) { f->speed = 0; f->speed_vel = 0; }
    }
    f->swim_phase += dt * (8 + f->speed * 0.055f);
    if (f->swim_phase >= 6.2831853f) f->swim_phase = fmodf(f->swim_phase, 6.2831853f);
    /* the swim follows the facing: side-on it is the heading's own, and as
     * the fish turns head-on its level travel crosses zero and it brakes to
     * 40%. Only a LEVEL turn: steep, the heading steers as it always did (a
     * diver's small corrections at the pellet don't wait on a flip) */
    float ahc = fabsf(hc), w = clampf((ahc - 0.2f) * (1 / 0.4f), 0, 1);
    float brake = 1 - 0.6f * (1 - fabsf(f->yaw)) * ahc * w;
    f->x += (hc + (f->yaw * ahc - hc) * w) * f->speed * brake * dt;
    f->y += sinf(f->heading) * f->speed * brake * dt + sinf(t->clock * 1.4f + f->wander) * dt * 1.7f;

    /* hard bounds. The side glass mirrors a heading only while it points
     * INTO the glass: the facing lags the mirror, and a second bounce on the
     * next frame would send the fish back into the glass */
    float m = 15 * f->size;
/* the bowl's glass: back onto the circle m inside it, the heading mirrored
                                                              in the glass while it points out, the body turned to swim back in */
    if (f->x < m)          { f->x = m; f->behind = -TURN_SETTLE;
                                                if (cosf(f->heading) < 0) { f->heading = norm_ang(3.14159f - f->heading); f->turn_vel = -f->turn_vel; }
                                                if (f->facing < 0) { f->facing = 1; f->behind = -TURN_SETTLE; } }
    if (f->x > TANK_W - m) { f->x = TANK_W - m; f->behind = -TURN_SETTLE;
                                                if (cosf(f->heading) > 0) { f->heading = norm_ang(3.14159f - f->heading); f->turn_vel = -f->turn_vel; }
                                                if (f->facing > 0) { f->facing = -1; f->behind = -TURN_SETTLE; } }
    float top = fmaxf(m, tank_fish_top_clearance(f));
    if (f->y < top)        { f->y = top; if (sinf(f->heading) < 0) { f->heading = -f->heading; f->turn_vel = -f->turn_vel; }
                            f->y = fmaxf(f->y, tank_fish_top_clearance(f)); }
    float bottom = TANK_SAND_TOP - tank_fish_clearance(f);
    if (f->y > bottom)     { f->y = bottom; if (sinf(f->heading) > 0) { f->heading = -f->heading; f->turn_vel = -f->turn_vel; }
                            f->y = fminf(f->y, TANK_SAND_TOP - tank_fish_clearance(f)); }
/* the watch's corners: back onto the arc m inside it, the heading mirrored
                                                              in the glass while it points out (the bowl's rule, a corner at a time) */

    eat_nearby_food(t, f);
}

/* Coarse state signature (the browser prototype's "blunt" gate): banded
 * drives plus bucketed sightings. Excludes clock bearings and exact drive
 * digits, which only steer - a change here is a reason to re-decide. */
static int band3(float v) { return v < 3.5f ? 0 : v < 7 ? 1 : 2; }
static uint32_t state_signature(const tank_t *t, int idx) {
    const fish_t *f = &t->fish[idx];
    /* Reconsider on meaningful events, not every change in pellet distance.
     * Food matters only to a hungry fish: absent, available or within reach.
     * Wall avoidance remains steering rather than a goal-change trigger. */
    float fd = 1e9f; int fi = tank_nearest_food(t, f, &fd);
    int food = (fi < 0 || band3(f->hunger) == 0) ? 0 : fd < 70 ? 2 : 1;
    return (uint32_t)band3(f->hunger) | (uint32_t)band3(f->energy) << 2 | (uint32_t)band3(f->stress) << 4
         | (uint32_t)food << 6
         | (uint32_t)t->night << 10 | (uint32_t)band3(f->curiosity) << 11
         | (uint32_t)band3(f->bored) << 13;      /* a fish going stale is asked again (2026-09-14) */
}

int64_t (*tank_profile_clock_us)(void);
int64_t tank_profile_us[3];
static int64_t tick_profile_finish(int stage, int64_t started) {
    if (!tank_profile_clock_us) return 0;
    int64_t now = tank_profile_clock_us();
    tank_profile_us[stage] += now - started;
    return now;
}
void tank_tick(tank_t *t, float dt, advisor_fn advise) {
    int64_t profile_started = tank_profile_clock_us ? tank_profile_clock_us() : 0;
    t->clock += dt;
    /* the light: on while the device is handled, off LIGHT_IDLE_S after the
     * last touch or movement (tank_handled) - a tank left on the desk goes
     * dark and the fish sleep. A setup page or a prompt holds it on; the
     * director's / sim's manual override wins over both. */
    t->idle_s += dt;
    if (t->ui_cover) t->clock_idle_s = 0; else t->clock_idle_s += dt;
    bool was_night = t->night;
    t->night = t->light_override ? !t->light_on
             : t->hold_light ? false
             : t->room_dark ? true
             : !t->light_auto ? t->light_manual_off              /* MANUAL (default): the double-tap's state */
             : t->idle_s > (float)t->light_idle_s;                      /* AUTO: the idle rule */
    if (t->night != was_night) tank_emit(t->night ? TEV_LIGHT_OFF : TEV_LIGHT_ON, -1);

    touch_tick(t, dt);

    /* food sinks, settles, decays */
    int live_food = 0;
    for (int i = 0; i < MAX_FOOD; i++) {
        food_t *p = &t->food[i];
        if (!p->alive) continue;
        live_food++;
        p->age += dt;
        if (p->y < FOOD_FLOOR_Y) {
            p->y += FOOD_SINK_PX_S * dt;
            p->x += sinf(p->age * 1.5f) * dt * 2;
        } else p->floor_s += dt;                        /* resting on the floor (FOOD_FLOOR_S) */
        if (p->floor_s > 0 ? p->floor_s > FOOD_FLOOR_S : p->age > FOOD_LIFE_S) p->alive = false;
    }
    /* the tank's own trickle keeps fish alive when nobody is home (never
     * ruined by absence); the keeper's pellets are what progression counts.
     * Hunger-gated (see the hunger economy notes above): a pellet only when
     * somebody is really hungry, at a per-SECOND rate, so it's the same tank
     * at 25 fps and 60 fps and it never over-feeds a school that isn't asking.
     * While the tank is ravenous-begging it holds off, so the keeper's first
     * pellets are the event that ends the wait (progression re-arms it) -
     * but once the keeper HAS fed this episode it helps again: a quick fish
     * can gobble every pellet, and the slow one shouldn't beg out the whole
     * give-up valve for it. */
    float hungriest = 0;
    for (int i = 0; i < t->n_fish; i++)
        if (t->fish[i].hunger > hungriest) hungriest = t->fish[i].hunger;
    bool hold = (t->ravenous && !t->ravenous_fed) || t->trickle_off || t->autofeed_off;   /* (AUTO FEED off: the keeper's alone, 0.3.2) */
    if (!hold && live_food < 2 && hungriest >= TRICKLE_HUNGER &&
        tank_randf(t, 0, 1) < TRICKLE_RATE * dt)
        tank_scatter_food(t, 1);

    t->reef_flow_s = t->reef_flow_cooldown = 0;
    t->reef_flow_seen = 0;
    t->coral_acc += dt;
    if (t->coral_acc >= 60) { coral_grow(t, t->coral_acc); t->coral_acc = 0; }
    t->cluster_acc += dt;
    if (t->cluster_acc >= 60) { cluster_grow(t, t->cluster_acc); t->cluster_acc = 0; }

    snail_tick(t, dt);
    urchin_tick(t, dt);
    seahorse_tick(t, dt);
    shrimp_tick(t, dt);
    profile_started = tick_profile_finish(0, profile_started);
    if ((int)t->clock != (int)(t->clock - dt)) {
        for (int i = 0; i < t->n_fish; i++)
            for (int j = 0; j < t->n_fish; j++) {
                fish_t *a = &t->fish[i], *b = &t->fish[j];
                if (i == j) continue;
                if (a->familiar_id[j] != b->social_id) {
                    a->familiar_id[j] = b->social_id; a->familiar[j] = 0;
                }
                if (a->stress < 5 && b->stress < 5 && tank_dist(a->x, a->y, b->x, b->y) < 90 &&
                    a->familiar[j] < UINT16_MAX)
                    a->familiar[j]++;
            }
    }

    /* bubbles rise */
    for (int i = 0; i < MAX_BUBBLE; i++) {
        bubble_t *b = &t->bubble[i];
        if (b->column) continue;
        b->wobble += dt * 2.5f;
        b->y -= b->vy * dt;
        b->x += sinf(b->wobble) * dt * (b->column ? 9 : 4);
        if (b->y < -6) {
            b->y = TANK_BOT + tank_randf(t, 4, 24);
            b->x = b->column ? t->bubble_x + tank_randf(t, -10, 10)
                             : tank_randf(t, TANK_FX0 + 12, TANK_FX1 - 12);
        }
    }

    /* advisor: polled every frame (async decisions land the moment they're
     * ready). A (re)decision is REQUESTED need-based, not on a fixed cadence:
     * the coarse signature changed and ADVISOR_MIN_INTERVAL passed, or the
     * idle ceiling hit, or something urgent (starving with food in view).
     * Effective cadence therefore scales with how much is
     * happening, not with how many fish live here. The start index rotates
     * so no slot is structurally favoured when several fish ask at once. */
    if (advise && t->n_fish > 0) {
        for (int k = 0; k < t->n_fish; k++) {
            int i = (t->ask_rr + k) % t->n_fish;
            fish_t *f = &t->fish[i];
            uint32_t sig = state_signature(t, i);
            /* prototype's urgent path: starving with food in view gets
             * asked NOW instead of waiting its turn */
            bool urgent = f->hunger > 8.0f && f->goal.id != GOAL_SEEK_FOOD &&
                          f->goal_age > 0.8f && tank_nearest_food(t, f, 0) >= 0;
            bool changed = sig != f->sig && f->ask_age > ADVISOR_MIN_INTERVAL;
            bool idle = f->ask_age > ADVISOR_IDLE_CEILING;
            bool want = changed || idle || (urgent && f->ask_age > 0.5f);
            goal_t g = advise(t, i, want);
            if (want) {            /* async advisors queue it; rules answer now */
                f->sig = sig; f->ask_age = 0; t->advisor_asks++;
                t->ask_rr = (i + 1) % t->n_fish;
            }
            if (g.id < GOAL_COUNT && g.id != f->goal.id) {
                if (f->goal.id == GOAL_FOLLOW_FRIEND) f->social_cooldown = 12;
                /* a genuinely new pastime relieves boredom; bouncing back to
                 * the one just left (bubbles -> friend -> bubbles) does not */
                if (g.id != f->goal_prev) f->bored = clampf(f->bored - BORED_NEW_GOAL, 0, 10);
                f->goal_prev = f->goal.id;
                if (g.id == GOAL_EXPLORE) f->explore_set = false;   /* pick a fresh destination */
                f->goal = g;
                f->goal_age = 0;
            } else if (g.id == f->goal.id) {
                f->goal.urgency = g.urgency;
            }
        }
    }

    /* Count moments a starving fish ignores available food for >4s.
     * Pure diagnostic, never changes a goal. */
    for (int i = 0; i < t->n_fish; i++) {
        fish_t *f = &t->fish[i];
        if (f->hunger > 8.5f && f->goal.id != GOAL_SEEK_FOOD &&
            f->goal.id != GOAL_FLEE_SHADOW && f->goal_age > 4.0f &&
            !f->starve_flagged && tank_nearest_food(t, f, 0) >= 0) {
            f->starve_flagged = true;          /* count once per episode */
            tank_reflex_overrides++;
        }
        if (f->goal.id == GOAL_SEEK_FOOD || f->hunger < 8.0f) f->starve_flagged = false;
    }

    profile_started = tick_profile_finish(1, profile_started);
    for (int i = 0; i < t->n_fish; i++) {
        update_fish(t, i, dt);
        if (t->companion.cozy) {
            /* Comfort is not a player meal, a hold, trust, or an achievement. */
            t->fish[i].hunger = fminf(t->fish[i].hunger, 6);
            t->fish[i].energy = fmaxf(t->fish[i].energy, 4);
            t->fish[i].stress = fminf(t->fish[i].stress, 4);
        }
    }

    /* consume the hold AFTER the fish have seen it (the platform re-asserts
     * every frame the finger stays down). Clearing it at the top of the tick
     * was the old bug that kept the drift-to-finger reflex from ever firing. */
    t->hold_active = false;
    tick_profile_finish(2, profile_started);
}
