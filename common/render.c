/* render.c — porthole look: deep gradient water, AMOLED-black floor, procedural
 * fish (body polygon + animated tail + earned markings), bubbles, seagrass
 * the keeper trims. Everything is drawn
 * into a bare RGB565 buffer; night dims the palette. */
#include "render.h"
#include "icons.h"
#include "progression.h"
#include "tank_events.h"
#include "setup.h"
#include "version.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>

#define TAU 6.2831853f
static bool g_clock_synchronized;
void render_clock_synchronized(bool synchronized) { g_clock_synchronized = synchronized; }

/* a draw target: fb + stride + night dim, plus the window it covers in tank
 * coordinates (ox,oy,w,h) - the frame is (0,0,TANK_W,TANK_H); the stats card
 * cache is a small sprite that still gets drawn in tank coordinates */
typedef struct { uint16_t *fb; int stride; float dim; int ox, oy, w, h; bool scaled; } ctx_t;
static ctx_t ctx_full(uint16_t *fb, int stride, float dim) {
    ctx_t c = { fb, stride, dim, 0, 0, TANK_W, TANK_H, false }; return c;
}
/* PAGE space (render.h): the full-screen pages, the prompts and the exported
 * drawing helpers put their 448 x 368 layout in the middle of the frame - a
 * ctx whose origin is the page's corner. In the rectangle that IS the frame. */
static bool g_dirty_hold;                          /* below, with the dirty mask */
static ctx_t ctx_page(uint16_t *fb, int stride) {
    ctx_t c = { fb, stride, 1.0f, -PAGE_X, -PAGE_Y, PAGE_FRAME_W, PAGE_FRAME_H, false };
    c.scaled = true;
    g_dirty_hold = true;                           /* a page's coordinates must never reach the mask */
    return c;
}
static ctx_t ctx_overlay(uint16_t *fb, int stride) {
    return ctx_page(fb, stride);
}
#define CTX_IN(c, x, y) ((unsigned)((x) - (c)->ox) < (unsigned)(c)->w && (unsigned)((y) - (c)->oy) < (unsigned)(c)->h)
static inline int ctx_offset(const ctx_t *c, int x, int y) {
    if (c->scaled) return (y * TANK_H / PAGE_H) * c->stride + x * TANK_W / PAGE_W;
    return (y - c->oy) * c->stride + x - c->ox;
}
#define CTX_PX(c, x, y) ((c)->fb[ctx_offset(c, x, y)])

/* Dirty mask (2026-09-01): one bit per pixel, set by everything drawn over
 * the baked scene (px, span, blends), cleared every frame. The porthole
 * vignette re-apply walks the mask instead of comparing each pixel with the
 * scene cache (32 pixels per word, no scene reads), and a pixel that already
 * had its vignette applied inline (frond spans, span_final) is simply not
 * marked, so no later rect darkens it twice. (A first version tagged the
 * green LSB instead and cleared it across the scene - that halved the color
 * steps of the dark vignette falloff into visible contour rings. Colors are
 * untouched now.) */
#define DIRTY_WORDS_PER_ROW ((TANK_W + 31) / 32)   /* 14 (15 on the 466 px bowl) */
static uint32_t *g_dirty = NULL;
void render_set_dirty_mask(uint32_t *buf) { g_dirty = buf; }
/* A PAGE draws in its own coordinates (ctx_page: on the bowl its origin is
 * not the frame's, and its y runs negative above the page) - marked as they
 * are, those wrote in front of the mask (2026-10-01: the settings page's
 * fill took the round board down). Nothing reads the mask after render_tank's
 * sweep, so from the first page ctx of a frame until the next render_tank
 * nothing is marked. */
static bool g_dirty_hold;
static inline void dirty_px(int x, int y) {
    if (g_dirty && !g_dirty_hold) g_dirty[y * DIRTY_WORDS_PER_ROW + (x >> 5)] |= 1u << (x & 31);
}
static inline void dirty_span(int x0, int x1, int y) {
    if (!g_dirty || g_dirty_hold) return;
    uint32_t *row = g_dirty + y * DIRTY_WORDS_PER_ROW;
    int w0 = x0 >> 5, w1 = x1 >> 5;
    if (w0 == w1) { row[w0] |= (0xFFFFFFFFu >> (31 - (x1 & 31))) & (0xFFFFFFFFu << (x0 & 31)); return; }
    row[w0] |= 0xFFFFFFFFu << (x0 & 31);
    for (int w = w0 + 1; w < w1; w++) row[w] = 0xFFFFFFFFu;
    row[w1] |= 0xFFFFFFFFu >> (31 - (x1 & 31));
}

static inline void dirty_clear_span(int x0, int x1, int y) {   /* the span's marks off, a word at a time */
    if (!g_dirty || g_dirty_hold) return;
    uint32_t *row = g_dirty + y * DIRTY_WORDS_PER_ROW;
    int w0 = x0 >> 5, w1 = x1 >> 5;
    if (w0 == w1) { row[w0] &= ~((0xFFFFFFFFu >> (31 - (x1 & 31))) & (0xFFFFFFFFu << (x0 & 31))); return; }
    row[w0] &= ~(0xFFFFFFFFu << (x0 & 31));
    for (int w = w0 + 1; w < w1; w++) row[w] = 0;
    row[w1] &= ~(0xFFFFFFFFu >> (31 - (x1 & 31)));
}

static uint16_t rgb565(uint32_t rgb, float dim) {
    uint32_t r = (uint32_t)(((rgb >> 16) & 255) * dim);
    uint32_t g = (uint32_t)(((rgb >> 8) & 255) * dim);
    uint32_t b = (uint32_t)((rgb & 255) * dim);
    return (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

/* bounding box of everything drawn while g_bb_on (render_tank wraps each
 * fish in it, so its vignette rect is exactly the pixels it touched instead
 * of a fixed 88 px box - fewer PSRAM reads in the re-apply sweep) */
static bool g_bb_on; static int g_bb_x0, g_bb_y0, g_bb_x1, g_bb_y1;
static inline void bb_add(int x0, int x1, int y) {
    if (!g_bb_on) return;
    if (x0 < g_bb_x0) g_bb_x0 = x0;
    if (x1 > g_bb_x1) g_bb_x1 = x1;
    if (y < g_bb_y0) g_bb_y0 = y;
    if (y > g_bb_y1) g_bb_y1 = y;
}
static void px(ctx_t *c, int x, int y, uint16_t col) {
    if (CTX_IN(c, x, y)) {
        CTX_PX(c, x, y) = col;
        bb_add(x, x, y);
        dirty_px(x, y);
    }
}

/* A source color dimmed ONCE per shape (night palette), never per pixel:
 * the float multiply + conversions were the bulk of every blended pixel's
 * cost (vegetation, algae film, light shafts, the stats card backdrop). */
typedef struct { int r, g, b; uint16_t v; } src_t;
static inline src_t src_color(uint32_t rgb, float dim) {
    src_t s;
    s.r = (int)(((rgb >> 16) & 255) * dim);
    s.g = (int)(((rgb >> 8) & 255) * dim);
    s.b = (int)((rgb & 255) * dim);
    s.v = (uint16_t)(((s.r >> 3) << 11) | ((s.g >> 2) << 5) | (s.b >> 3));
    return s;
}
/* alpha 0..255 blend of a pre-dimmed source onto one framebuffer pixel */
static inline void blend565(uint16_t *p, const src_t *s, int a) {
    int dr = (*p >> 11) << 3, dg = ((*p >> 5) & 63) << 2, db = (*p & 31) << 3;
    int r = dr + ((s->r - dr) * a >> 8), g = dg + ((s->g - dg) * a >> 8), b = db + ((s->b - db) * a >> 8);
    *p = (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}
static inline void px_blend_s(ctx_t *c, int x, int y, const src_t *s, int a) {
    if (CTX_IN(c, x, y)) { blend565(&CTX_PX(c, x, y), s, a); dirty_px(x, y); }
}
/* alpha 0..255 blend onto existing pixel (one-off pixels; shapes hoist) */
static void px_blend(ctx_t *c, int x, int y, uint32_t rgb, int a) {
    src_t s = src_color(rgb, c->dim);
    px_blend_s(c, x, y, &s, a);
}
/* horizontal span [x0,x1] on row y, clipped to the frame, pre-dimmed source */
static inline void span(ctx_t *c, int x0, int x1, int y, const src_t *s, int alpha) {
    if ((unsigned)(y - c->oy) >= (unsigned)c->h) return;
    if (x0 < c->ox) x0 = c->ox;
    if (x1 > c->ox + c->w - 1) x1 = c->ox + c->w - 1;
    if (x0 > x1) return;
    if (c->scaled) {
        uint16_t *p = &CTX_PX(c, x0, y);
        int n = x1 * TANK_W / PAGE_W - x0 * TANK_W / PAGE_W + 1;
        for (int i = 0; i < n; i++, p++) {
            if (alpha >= 255) *p = s->v; else blend565(p, s, alpha);
        }
        return;
    }
    bb_add(x0, x1, y);
    dirty_span(x0, x1, y);
    uint16_t *p = &CTX_PX(c, x0, y);
    if (alpha >= 255) for (int x = x0; x <= x1; x++) *p++ = s->v;
    else              for (int x = x0; x <= x1; x++) blend565(p++, s, alpha);
}

/* row half-width of a unit circle at |t| = k/64: sqrt(1 - t^2), tabled -
 * the ESP32-S3 computes sqrtf in software and a fouled glass alone asks for
 * ~7000 of them per frame (algae blobs); the quantisation is sub-pixel */
static float ell_half(float t) {
    static float lut[66]; static bool filled;
    if (!filled) { for (int k = 0; k <= 65; k++) { float u = k / 64.0f; lut[k] = u < 1 ? sqrtf(1 - u * u) : 0; } filled = true; }
    if (t < 0) t = -t;
    return t >= 1 ? 0 : lut[(int)(t * 64 + 0.5f)];
}
static void fill_ellipse(ctx_t *c, float cx, float cy, float rx, float ry,
                         uint32_t rgb, int alpha) {
    src_t s = src_color(rgb, c->dim);
    int y0 = (int)(cy - ry), y1 = (int)(cy + ry);
    for (int y = y0; y <= y1; y++) {
        float w = ell_half((y - cy) / ry);
        if (w <= 0) continue;
        float half = rx * w;
        span(c, (int)(cx - half), (int)(cx + half), y, &s, alpha);
    }
}

/* sine from a 256-entry table: the frond sway only has to look continuous,
 * and the ESP32-S3 computes sinf in software (~1000 per frame at full
 * canopy). Phases here are non-negative; & 255 keeps any sign honest. */
static float fast_sin(float x) {
    static float lut[256]; static bool filled;
    if (!filled) { for (int i = 0; i < 256; i++) lut[i] = sinf(i * (TAU / 256)); filled = true; }
    return lut[(int)(x * (256.0f / TAU)) & 255];
}

/* filled convex polygon, points in world space */
static void fill_poly(ctx_t *c, const float *xs, const float *ys, int n, uint32_t rgb) {
    float miny = 1e9f, maxy = -1e9f;
    for (int i = 0; i < n; i++) { if (ys[i] < miny) miny = ys[i]; if (ys[i] > maxy) maxy = ys[i]; }
    src_t s = src_color(rgb, c->dim);
    for (int y = (int)miny; y <= (int)maxy; y++) {
        float x0 = 1e9f, x1 = -1e9f;
        for (int i = 0; i < n; i++) {
            int j = (i + 1) % n;
            float ay = ys[i], by = ys[j];
            if ((ay <= y && by > y) || (by <= y && ay > y)) {
                float x = xs[i] + (xs[j] - xs[i]) * (y - ay) / (by - ay);
                if (x < x0) x0 = x;
                if (x > x1) x1 = x;
            }
        }
        span(c, (int)x0, (int)x1, y, &s, 255);
    }
}

static uint32_t mix(uint32_t a, uint32_t b, float p) {
    int ar = a >> 16 & 255, ag = a >> 8 & 255, ab = a & 255;
    int br = b >> 16 & 255, bg = b >> 8 & 255, bb = b & 255;
    return ((uint32_t)(ar + (br - ar) * p) << 16) |
           ((uint32_t)(ag + (bg - ag) * p) << 8) |
            (uint32_t)(ab + (bb - ab) * p);
}


/* the turn (2026-09-29, was a roll that flattened the fish to a line): the
 * fish's yaw (tank.c) FORESHORTENS it along its length - side-on at +-1,
 * head-on at 0 - and the sign mirrors it, so its back stays up. The head's
 * yaw leads and the tail's follows, blended along the body, so mid-turn it
 * bends. Eased here so the swing starts and lands softly. */
static float yaw_ease(float y) { return y * (1.5f - 0.5f * y * y); }
/* the outline never folds to a line (the body's
                                         * thickness is drawn on its own: FISH_THICK) */
#define YAW_MIN 0.06f
/* half the body's thickness, local px: turned toward
                                         * the glass a fish shows a rounded oval, not a card */
#define FISH_THICK 4.2f
/* the yaw at body point lx: the tail's at the tail tip (-26), the head's at the nose (16) */
static float yaw_at(float yh, float yt, float lx) {
    float u = (lx + 26) * (1.0f / 42);
    u = u < 0 ? 0 : u > 1 ? 1 : u;
    float s = yt + (yh - yt) * u;
    if (s > -YAW_MIN && s < YAW_MIN) s = s < 0 ? -YAW_MIN : YAW_MIN;
    return s;
}

/* The fish's body is a ledger of its life: stage sets size (tank.c) and fin
 * elaboration, hunger drains saturation, and earned markings stay:
 *   elder -> a longer tail and a dorsal crest. All procedural, zero flash. */
/* `mark` scales the accent stripes: 1 in the tank (every stage wears the
 * same 2 x 5 px marks), the preview's size so they read on a big fish */
static void draw_fish_core(ctx_t *c, const fish_t *f, float clock, bool asleep, float mark) {
    float tail = sinf(clock * (8 + f->speed * 0.055f) + f->wander) *
                 (0.32f + f->speed * 0.007f);
    float stress = f->stress / 10.0f;
    float pale = f->hunger > 7 ? (f->hunger - 7) / 3.0f * 0.35f : 0;   /* hungry = washed out */
    /* pitch: the heading folded to face right (the climb or dive), then the
     * yaw scales x: X = s(lx) * (lx cos p - ly sin p), Y = lx sin p + ly cos p.
     * The BODY tilts less than the path: a fish rises and sinks on its fins
     * with its back near level, so the travel angle is eased toward a cap of
     * ~35 deg (~55 at a dash) - never nose-straight-up (Strato 09-29: "too
     * much time in a vertical position") */
    float pmax = 0.61f + 0.35f * fminf(fmaxf((f->speed - 35) * (1 / 40.0f), 0), 1);
    float p = pmax * tanhf(0.8f * atan2f(sinf(f->heading), fabsf(cosf(f->heading))) / pmax);
    float cp = cosf(p), sp = sinf(p);
    float yh = yaw_ease(f->yaw), yt = yaw_ease(f->yaw_tail);
    bool elder = f->stage == STAGE_ELDER, grown = f->stage >= STAGE_ADULT;
    float tail_len = elder ? 1.22f : 1.0f;
    bool shark = f->variety == FISH_SHARK, ray = f->variety == FISH_STINGRAY;
    bool betta = f->variety == FISH_BETTA, clown = f->variety == FISH_CLOWNFISH;
    float body_x = shark ? 1.4f : f->variety == FISH_TETRA ? 1.15f : f->variety == FISH_PUFFER ? 0.85f : clown ? 0.9f : 1;
    float body_y = f->variety == FISH_ANGEL ? 1.55f : f->variety == FISH_TETRA ? 0.65f : f->variety == FISH_PUFFER ? 1.35f : shark ? 0.7f : clown ? 1.15f : 1;
    if (f->variety == FISH_GUPPY) tail_len *= 1.3f;
    uint32_t body = mix(mix(f->color, 0xf25b65, stress * 0.22f), 0x7a8a8e, pale);
    uint32_t fin  = mix(mix(f->fin, 0xf25b65, stress * 0.18f), 0x5a6a6e, pale);
#define TX(lx, ly) (f->x + yaw_at(yh, yt, lx) * ((lx) * cp - (ly) * sp) * f->size)
#define TY(lx, ly) (f->y + ((lx) * sp + (ly) * cp) * f->size)

    if (f->variety == FISH_JELLYFISH) {
        float pulse = sinf(clock * 3 + f->wander), width = 1 + pulse * 0.07f;
        for (int strand = 0; strand < 5; strand++) {
            float base = (strand - 2) * 5, last_x = base, last_y = 1;
            for (int segment = 1; segment <= 6; segment++) {
                float y = segment * (strand % 2 ? 3.0f : 3.6f);
                float x = base + sinf(clock * 3 + segment * 0.65f + strand) * segment * 0.55f;
                float lx[4] = {last_x - 0.6f, last_x + 0.6f, x + 0.6f, x - 0.6f};
                float ly[4] = {last_y, last_y, y, y}, xs[4], ys[4];
                for (int i = 0; i < 4; i++) { xs[i] = TX(lx[i], ly[i]); ys[i] = TY(lx[i], ly[i]); }
                fill_poly(c, xs, ys, 4, strand % 2 ? fin : f->accent);
                last_x = x; last_y = y;
            }
        }
        static const float bx[11] = {-16, -15, -11, -6, 0, 6, 11, 15, 16, 8, -8};
        static const float by[11] = {1, -5, -11, -15, -17, -15, -11, -5, 1, 3, 3};
        float xs[11], ys[11];
        for (int i = 0; i < 11; i++) { xs[i] = TX(bx[i] * width, by[i]); ys[i] = TY(bx[i] * width, by[i]); }
        fill_poly(c, xs, ys, 11, body);
        if (f->stage >= STAGE_JUV && f->pattern != PATTERN_PLAIN) {
            for (int i = -1; i <= 1; i++) {
                float x = i * 7 * width;
                float y = f->pattern == PATTERN_BAND ? -2 : -6;
                fill_ellipse(c, TX(x, y), TY(x, y),
                             (f->pattern == PATTERN_BAND ? 4.5f : 1.5f) * f->size * fabsf(yh),
                             (f->pattern == PATTERN_STRIPES ? 4 : 1.5f) * f->size, f->accent, 210);
            }
        }
        fill_ellipse(c, TX(6, -5), TY(6, -5), 1.4f, asleep ? 0.6f : 1.4f, 0x031015, 255);
        return;
    }
    if (ray) {
        float flap = sinf(clock * 4 + f->wander) * 4;
        float lx[8] = {15, 2, -9, -15, -9, 2, 15, 7};
        float ly[8] = {0, -24 + flap, -16, 0, 16 - flap, 24 - flap, 0, 0}, xs[8], ys[8];
        for (int i = 0; i < 8; i++) { xs[i] = TX(lx[i], ly[i]); ys[i] = TY(lx[i], ly[i]); }
        fill_poly(c, xs, ys, 8, fin);
        float tx[3] = {-10, -38, -12}, ty[3] = {-1, tail * 3, 2};
        for (int i = 0; i < 3; i++) { xs[i] = TX(tx[i], ty[i]); ys[i] = TY(tx[i], ty[i]); }
        fill_poly(c, xs, ys, 3, fin);
    }
    /* tail fin (triangle, flaps with tail phase) */
    if (!ray) {
        float spread = f->variety == FISH_GUPPY ? 17 : f->variety == FISH_PUFFER ? 6 : 10;
        if (shark) spread = 15;
        if (betta) spread = 20;
        float lx[3] = {-11, -26 * tail_len, -26 * tail_len}, ly[3] = {0, -spread - tail * 8, spread + tail * 8};
        if (shark) { lx[1] = -32; lx[2] = -30; ly[2] = 10 + tail * 4; }
        float xs[3], ys[3];
        for (int i = 0; i < 3; i++) { xs[i] = TX(lx[i], ly[i]); ys[i] = TY(lx[i], ly[i]); }
        fill_poly(c, xs, ys, 3, fin);
    }
    if (betta) {
        float lx[6] = {8, -3, -24, -22, -8, 4}, ly[6] = {6, 22 + tail * 2, 25, 11, 7, 5};
        float xs[6], ys[6];
        for (int i = 0; i < 6; i++) { xs[i] = TX(lx[i], ly[i]); ys[i] = TY(lx[i], ly[i]); }
        fill_poly(c, xs, ys, 6, fin);
    }
    if (shark) {
        float lx[3] = {7, -5, -10}, ly[3] = {3, 17, 5}, xs[3], ys[3];
        for (int i = 0; i < 3; i++) { xs[i] = TX(lx[i], ly[i]); ys[i] = TY(lx[i], ly[i]); }
        fill_poly(c, xs, ys, 3, fin);
    }
    if (f->variety == FISH_ANGEL) {
        float lx[3] = {5, -8, -11}, ly[3] = {9, 25, 7}, xs[3], ys[3];
        for (int i = 0; i < 3; i++) { xs[i] = TX(lx[i], ly[i]); ys[i] = TY(lx[i], ly[i]); }
        fill_poly(c, xs, ys, 3, fin);
    }
    /* dorsal crest: adults a small fin, elders a taller one */
    if (!ray && (grown || shark || betta || clown)) {
        float h = f->variety == FISH_ANGEL ? -27 : shark ? -23 : betta ? -21 : elder ? -17 : -13;
        float lx[3] = {6, -2, -9}, ly[3] = {-8, h, -8};
        float xs[3], ys[3];
        for (int i = 0; i < 3; i++) { xs[i] = TX(lx[i], ly[i]); ys[i] = TY(lx[i], ly[i]); }
        fill_poly(c, xs, ys, 3, fin);
    }
    /* body: sampled outline of the prototype's quadratic silhouette */
    {
        static const float blx[10] = { 16, 10, 2, -8, -15, -18, -15, -8, 2, 10 };
        static const float shark_x[10] = {20, 9, 0, -9, -17, -21, -17, -9, 0, 12};
        static const float ray_x[10] = {13, 9, 2, -5, -10, -13, -10, -5, 2, 9};
        static const float bly[10] = { 0, -7, -10, -9, -6, 0, 6, 9, 10, 7 };
        float xs[10], ys[10];
        for (int i = 0; i < 10; i++) { float lx = (shark ? shark_x[i] : ray ? ray_x[i] : blx[i]) * body_x, ly = bly[i] * body_y; xs[i] = TX(lx, ly); ys[i] = TY(lx, ly); }
        fill_poly(c, xs, ys, 10, body);
        /* its thickness: the cross-section at the deepest point, widening as
         * the fish turns toward the glass (sin of the yaw angle) */
        float sm = yaw_at(yh, yt, 2), rx = FISH_THICK * f->size * sqrtf(fmaxf(0, 1 - sm * sm));
        if (rx >= 1) fill_ellipse(c, TX(2, 0), TY(2, 0), rx, 9.5f * body_y * f->size, body, 255);
    }
    /* accent stripes: on the flank, so they narrow with the turn and fade as
     * the fish comes head-on (seen edge-on, a flank shows nothing).
     * A FRY has none yet: its markings come in with its first growth spurt
     * (the setup's colour page keeps the accent a "?" for that reason) */
    if (f->stage >= STAGE_JUV && f->pattern == PATTERN_STRIPES) for (int i = -1; i <= 1; i++) {
        float lx = -3 + i * 6, s = fabsf(yaw_at(yh, yt, lx));
        if (s > 0.25f)
            fill_ellipse(c, TX(lx, 0), TY(lx, 0), (clown ? 2.8f : 2) * mark * s, (clown ? 8 : 5) * mark,
                         f->accent, (int)(150 * (s - 0.25f) / 0.75f));
    }
    if (f->stage >= STAGE_JUV && f->pattern == PATTERN_SPOTS) for (int i = 0; i < 6; i++) {
        float lx = -10 + (i % 3) * 7, ly = (i < 3 ? -3 : 3) * body_y, s = fabsf(yaw_at(yh, yt, lx));
        if (s > 0.25f) fill_ellipse(c, TX(lx, ly), TY(lx, ly), 1.7f * mark * s, 1.7f * mark, f->accent, 230);
    }
    if (f->stage >= STAGE_JUV && f->pattern == PATTERN_BAND) {
        float lx[4] = {-13, 11, 11, -13}, ly[4] = {-1.5f, -1.5f, 1.5f, 1.5f}, xs[4], ys[4];
        for (int i = 0; i < 4; i++) { xs[i] = TX(lx[i], ly[i]); ys[i] = TY(lx[i], ly[i]); }
        fill_poly(c, xs, ys, 4, f->accent);
    }
    if (shark) for (int gill = 0; gill < 3; gill++) {
        float lx = 3 + gill * 2;
        fill_ellipse(c, TX(lx, 2), TY(lx, 2), 0.6f * f->size, 2 * f->size, fin, 230);
    }
    /* eye — closed to a lid line when asleep (resting at night) */
    if (asleep) {
        fill_ellipse(c, TX(9, -3), TY(9, -3), 2.2f, 0.7f, 0x9fb4b8, 200);
    } else {
        fill_ellipse(c, TX(9, -3), TY(9, -3), 2.2f, 2.2f, 0xffffff, 255);
        fill_ellipse(c, TX(9.7f, -3), TY(9.7f, -3), 1.1f, 1.1f, 0x031015, 255);
    }
#undef TX
#undef TY
}
/* the tank's fish (the previews below draw a fish of their own) */
static void draw_fish(ctx_t *c, const tank_t *t, const fish_t *f) {
    draw_fish_core(c, f, t->clock, t->night && f->goal.id == GOAL_REST, 1.0f);
}

/* ---- the shrimp (2026-09-29): cherry shrimp from 18 x 8 pixel grids,
 * facing right, drawn pixel by pixel through the fish's turn - the yaw
 * foreshortens x - so they stay procedural (Strato: "all aquatic marine life
 * sprites should be procedurally drawn. raster icons only for menus").
 * What makes six pixels a shrimp: the hunched back, the body thinning to a
 * fanned tail, one eye up front, antennae longer than the body; the tail,
 * legs and antennae are see-through. Character is in the motion: paddling
 * legs, a peck at the floor, the tail-flick escape. (working-assets/shrimp/
 * has the approved drafts.) Tones: o belly edge, r cherry, h the back's
 * light, s a glint, e the eye, a antenna, l legs, t tail fan. */
#define SHRIMP_TONES 8
static const char SHRIMP_KEYS[SHRIMP_TONES + 1] = "orhsealt";
static const uint32_t SHRIMP_RGB[SHRIMP_TONES]  = { 0x6e0f1c, 0xd0262e, 0xff5a4a, 0xffb8a0, 0x100408, 0xff7a70, 0xc0303a, 0xff7060 };
static const uint8_t  SHRIMP_ALPHA[SHRIMP_TONES] = { 255, 255, 255, 255, 255, 140, 190, 205 };
static const char *const SHRIMP_IDLE_A[8] = {
    "..................", "......ohhs......aa", "....ohrrrhhhs..a..", "..ohrrrrrrrrrea...",
    ".trrrrrrrrrrrrrr..", "tto..oorrrrrroo...", "t......l.l.l.l.aa.", "................a." };
static const char *const SHRIMP_IDLE_B[8] = {
    "................a.", "......ohhs......a.", "....ohrrrhhhs..a..", "..ohrrrrrrrrrea...",
    ".trrrrrrrrrrrrrr..", "tto..oorrrrrroo...", "t.....l.l.l.l..aa.", ".................." };
static const char *const SHRIMP_PECK[8] = {      /* nose to the sand */
    "..................", "......ohhs........", "....ohrrrhhhs.....", "..ohrrrrrrrrrr.aa.",
    ".trrrrrrrrrrrrea..", "tto..oorrrrrrrrr..", "t......l.l.l.lo.a.", "................a." };
static const char *const SHRIMP_DART[8] = {      /* the escape: tail snapped under, antennae streaming back */
    "..................", "aaaa..ohhs........", "....ohrrrhhhs.....", "...orrrrrrrrrea...",
    "...ttrrrrrrrrrr...", "...tt.oorrrroo....", "..................", ".................." };
static void draw_shrimp(ctx_t *c, const shrimp_t *q, const src_t *pal) {
    float sp2 = q->vx * q->vx + q->vy * q->vy;
    const char *const *fr = q->dart > 0 ? SHRIMP_DART
                          : q->pecking ? (fmodf(q->ph * 3, 1) < 0.5f ? SHRIMP_PECK : SHRIMP_IDLE_A)
                          : (fmodf(q->ph * (sp2 > 9 ? 8 : 2.5f), 1) < 0.5f ? SHRIMP_IDLE_A : SHRIMP_IDLE_B);
    float s = q->yaw;                                   /* head-on it is a sliver, never nothing */
    if (s > -0.3f && s < 0.3f) s = s < 0 ? -0.3f : 0.3f;
    int oy = (int)floorf(q->y + 0.5f) - 4;
    for (int y = 0; y < 8; y++)
        for (int x = 0; x < 18; x++) {
            char ch = fr[y][x];
            if (ch == '.') continue;
            const char *k = strchr(SHRIMP_KEYS, ch);
            if (!k) continue;
            int ti = (int)(k - SHRIMP_KEYS);
            px_blend_s(c, (int)floorf(q->x + (x - 9) * s + 0.5f), oy + y, &pal[ti], SHRIMP_ALPHA[ti]);
        }
}

/* a bed of swaying seaweed fronds; seed varies phase/heights between beds.
 * n and max_seg come from tank_veg_bed (growth-driven: the beds keep growing
 * up and out until the keeper trims them; at VEG_NUB they are green stubble).
 * layer: 0 = the fronds drawn BEHIND the fish, 1 = the fronds drawn in FRONT
 * (alternating), so a fish that dips into a canopy swims woven through it
 * instead of floating on top. */

/* algae film on the glass, drawn over everything: dappled blobs per covered
 * grid cell, thicker film = bigger and greener. The keeper wipes it off. */
static void draw_algae(ctx_t *c, const tank_t *t) {
    for (int cy = 0; cy < ALGAE_ROWS; cy++)
        for (int cx = 0; cx < ALGAE_COLS; cx++) {
            int cov = t->algae[cy * ALGAE_COLS + cx];
            if (!cov) continue;
            uint32_t h = (uint32_t)((cx * 73856093u) ^ (cy * 19349663u));
            float bx = cx * ALGAE_CELL, by = cy * ALGAE_CELL;
            for (int b = 0; b < 3; b++) {
                uint32_t hb = h ^ (b * 2654435761u);
                float ox = (float)(hb % ALGAE_CELL);
                float oy = (float)((hb >> 5) % ALGAE_CELL);
                float r = (1.5f + (float)((hb >> 10) % 3)) * (0.55f + 0.45f * cov / 255.0f)
                        + 2.2f * cov / 255.0f;
                fill_ellipse(c, bx + ox, by + oy, r, r * 0.85f,
                             (hb & 4) ? 0x3f7a45 : 0x35663d, 34 + cov * 96 / 255);
            }
        }
}

/* the snail: drawn procedurally (2026-09-16), defined after the castle
 * block below - it shares the castle's position hash */
static void draw_snail(ctx_t *c, const tank_t *t, bool upright_pass);

/* optional per-stage frame profiling (render.h) */
int64_t (*render_clock_us)(void) = NULL;
int64_t render_prof_us[7];
#define PROF_MARK() (render_clock_us ? render_clock_us() : 0)
#define PROF_ADD(i, t0) do { if (render_clock_us) { int64_t _n = render_clock_us(); render_prof_us[i] += _n - (t0); (t0) = _n; } } while (0)

/* ---- static scene: gradient, pebbles, backdrop decor (cacheable) ---- */
static uint16_t *g_scene = NULL;
static float     g_scene_dim = -1;
static unsigned  g_scene_epoch = 0;
static const uint16_t *g_primed_fb = NULL;
static unsigned  g_primed_epoch = 0;
static uint8_t  *g_vig = NULL;                  /* per-pixel vignette alpha (static) */
static bool      g_vig_filled = false;
void render_set_scene_cache(uint16_t *buf) { g_scene = buf; g_scene_dim = -1; }
void render_set_vignette_cache(uint8_t *buf) { g_vig = buf; g_vig_filled = false; }

const uint16_t *render_scene_buf(unsigned *epoch) {
    if (epoch) *epoch = g_scene_epoch;
    return g_scene_epoch ? g_scene : NULL;
}
void render_fb_primed(const uint16_t *fb, unsigned epoch) { g_primed_fb = fb; g_primed_epoch = epoch; }

/* vignette alpha at (x,y); matches the classic per-pixel loop */
static inline int vig_alpha(int x, int y) {
    float dx = (x - TANK_W * 0.5f) / (TANK_W * 0.5f);
    float dy = (y - TANK_H * 0.5f) / (TANK_H * 0.5f);
    float d2 = dx * dx + dy * dy;
    if (d2 <= 0.72f) return 0;
    int a = (int)((d2 - 0.72f) * 220);
    return a > 255 ? 255 : a;
}

/* pure darkening (blend toward black), independent of ctx dim */
static inline void px_darken(uint16_t *p, int a) {
    int inv = 256 - a;
    *p = (uint16_t)(((((*p >> 11) * inv) >> 8) << 11) |
                    ((((((*p) >> 5) & 63) * inv) >> 8) << 5) |
                    (((*p & 31) * inv) >> 8));
}

/* ---- the pumpkin lantern's night light (0.3.6; Strato 2026-10-17: "its
 * glow should gently illuminate the other decorations and critters in the
 * tank") ----
 * With the tank light off the lantern's candle lights everything near it,
 * not only the water behind: the light at a pixel is one table lookup by its
 * squared distance (g_pk_lut: warm at the heart, going gold-green with
 * distance - the water drinks the red first), added before the vignette.
 * Where it lands:
 *   - the baked scene: the water and the floor once, dithered (pk_halo),
 *     and every piece painted into the bake - the final-paint paths (the
 *     castle, the corals, the mounds) call PK_LIGHT_PX before their vignette;
 *   - every frame, what is painted live: each frond span (one light per
 *     span, as its vignette) and - in one pass over the dirty mask before
 *     the vignette sweep (pk_light_dirty) - the fish, the shrimp, the snail,
 *     the urchin, the seahorse, the food, the bubbles, the crowns.
 * g_pk_lx < 0 = no light (the light on, no pumpkin, a page drawing):
 * render_tank sets it for its own frame and clears it on the way out. */
#define PK_HALO_R   176                       /* the reach along the floor, px (a castle's half width and more) */
#define PK_HALO_RY  150                       /* and up: it spreads along the floor a little more */
#define PK_LUT_K    (16777216 / (PK_HALO_R * PK_HALO_R))   /* d2 -> table index (256 = the edge) */
#define PK_DY_K     (256 * PK_HALO_R * PK_HALO_R / (PK_HALO_RY * PK_HALO_RY))   /* dy^2 -> its share of d2, x256 (no divide per pixel) */
static int g_pk_lx = -1, g_pk_ly;
/* the light by squared distance, 128 steps: its RGB in 8 bits (the bake's
 * dithered water) and packed as 565 increments in g_pk_lut - 256 bytes of
 * internal RAM, looked up thousands of times a frame (a PSRAM table cost
 * cache misses across the whole render) */
static uint16_t g_pk_lut[128], g_pk_lut_shade[128], g_pk_lut_half[128];   /* ... the spill where something stands in its way, and a half step at a shadow's soft edge */
#define PK_HALF 0.55f
static bool g_pk_lut_ok;
/* the castle in the way (2026-10-17, Strato: "the castle should block the
 * pumpkin's glow"): with the lantern BEHIND a castle IN FRONT, the castle's
 * silhouette is a shadow - its stone takes PK_SPILL of the light (the water
 * scatters a little round it) and so does anything drawn inside it - but the
 * arch's opening and the windows let the light through, so the gateway glows
 * and a fish passing it is lit. pk_shaded says which, in the frame's
 * coordinates (defined with the castle, below). */
#define PK_SPILL 0.22f
static bool g_pk_under_castle;                /* the castle stands over the lantern: its silhouette's rules */
/* THE PIECES (2026-10-17): the castle, the coral, the cluster and the pumpkin
 * are one list, back to front (tank_decor_order: depth, then rank); render.c
 * paints that list and nothing else decides who covers whom. Declared here,
 * defined with the pieces below. */
static int  g_npc;                            /* this frame's pieces */
static int  g_pk_k = -1;                      /* the lantern's index in the list, -1 = none */
static int  g_clip_k = -1;                    /* an overlay (a crown, the candle) of piece g_clip_k is being drawn: the pieces above it cover it */
static int  g_castle_cx = -1;                 /* the castle's centre whatever its depth (-1 = none): its shadow's geometry */
static bool pc_covered_above(int k, int x, int y);
/* and everything else in front of a lantern BEHIND (Strato 2026-10-17: "add
 * the same shadow drawing to other things that fall in front of the pumpkin
 * like blades of sea grass and the sword plant and corals"): a blade, a
 * leaf, a coral or cluster IN FRONT is backlit - its own pixels take the
 * spill (g_pk_shade_all while they are drawn) - and casts a shadow on the
 * sand under its footprint (g_pk_cast, read by pk_shade): dark under a
 * solid piece, the spill under a grass bed (light leaks between the
 * blades), and never on the lantern's own patch of sand */
static bool g_pk_shadows;                     /* the lantern is BEHIND and lit: pk_shade has a say */
static bool g_pk_shade_all;                   /* what is being drawn stands in front of the lantern */
#define PK_CAST_MAX (VEG_BEDS_MAX * 4 + 2)
static struct { short x0, x1; signed char level; } g_pk_cast[PK_CAST_MAX];
static int g_pk_cast_n;
#define PK_FLOOR_Y (TANK_BOT - 18)            /* below this the sand begins (its top varies by a few px) */
static int  pk_shade(int x, int y);           /* 0 = lit, 1 = half, 2 = the spill, 3 = dark */
static void pk_rgb8(int i, int *r, int *g, int *b) {
    float d = sqrtf((i + 0.5f) / 128.0f), a = 0.88f * powf(1 - d, 1.7f);   /* a long soft tail: the neighbours catch it */
    *r = (int)(a * (196 - 84 * d)); *g = (int)(a * (116 - 4 * d)); *b = (int)(a * (34 + 22 * d));
}
static void pk_lut_fill(void) {
    if (g_pk_lut_ok) return;
    for (int i = 0; i < 128; i++) {
        int r, g, b; pk_rgb8(i, &r, &g, &b);
        g_pk_lut[i] = (uint16_t)((r >> 3) << 11 | (g >> 2) << 5 | (b >> 3));
        int hr = (int)(r * PK_HALF), hg = (int)(g * PK_HALF), hb = (int)(b * PK_HALF);
        g_pk_lut_half[i] = (uint16_t)((hr >> 3) << 11 | (hg >> 2) << 5 | (hb >> 3));
        r = (int)(r * PK_SPILL); g = (int)(g * PK_SPILL); b = (int)(b * PK_SPILL);
        g_pk_lut_shade[i] = (uint16_t)((r >> 3) << 11 | (g >> 2) << 5 | (b >> 3));
    }
    g_pk_lut_ok = true;
}
static inline int pk_light_i(int x, int y) {      /* the table step at (x, y), -1 = outside the light */
    int dx = x - g_pk_lx, dy = y - g_pk_ly;
    if (dx <= -PK_HALO_R || dx >= PK_HALO_R || dy <= -PK_HALO_RY || dy >= PK_HALO_RY) return -1;
    int i = (dx * dx + (dy * dy * PK_DY_K >> 8)) * PK_LUT_K >> 17;
    return i < 128 ? i : -1;
}
static inline uint32_t pk_light_at(int x, int y) {
    int i = pk_light_i(x, y);
    if (i < 0) return 0;
    int sh = g_pk_shadows ? pk_shade(x, y) : 0;
    if (g_pk_shade_all && sh < 2) sh = 2;
    return sh == 0 ? g_pk_lut[i] : sh == 1 ? g_pk_lut_half[i] : sh == 2 ? g_pk_lut_shade[i] : 0;
}
static inline uint16_t px_lit(uint16_t v, uint32_t l) {   /* a 565 colour plus the light's 565 increments, saturated */
    int r = (v >> 11) + (int)(l >> 11), g = ((v >> 5) & 63) + (int)((l >> 5) & 63), b = (v & 31) + (int)(l & 31);
    return (uint16_t)((r > 31 ? 31 : r) << 11 | (g > 63 ? 63 : g) << 5 | (b > 31 ? 31 : b));
}
#define PK_LIGHT_PX(p, x, y) do { if (g_pk_lx >= 0) { uint32_t _l = pk_light_at((x), (y)); if (_l) *(p) = px_lit(*(p), _l); } } while (0)

/* a span that is FINAL: blended, then vignetted right here, and NOT marked
 * dirty, so the re-apply sweep never touches it (scene cache mode only) */
static inline void span_final(ctx_t *c, int x0, int x1, int y, const src_t *s, int alpha) {
    if ((unsigned)(y - c->oy) >= (unsigned)c->h) return;
    if (x0 < c->ox) x0 = c->ox;
    if (x1 > c->ox + c->w - 1) x1 = c->ox + c->w - 1;
    if (x0 > x1) return;
    uint16_t *p = &CTX_PX(c, x0, y);
    /* ONE vignette alpha per span, computed (a 5 px frond span changes it by
       < 4%, invisible) rather than read: the PSRAM LUT costs a cache line
       per span and fronds are vertical, which was most of the frond cost */
    int a = vig_alpha((x0 + x1) >> 1, y);
    uint32_t l = g_pk_lx >= 0 ? pk_light_at((x0 + x1) >> 1, y) : 0;   /* the lantern's light, one per span likewise */
    if (l) dirty_clear_span(x0, x1, y);         /* lit here: a fish under it must not be lit again by the dirty pass */
    if (alpha >= 255) {                         /* opaque: a store, no PSRAM read */
        uint16_t v = l ? px_lit(s->v, l) : s->v;
        for (int x = x0; x <= x1; x++, p++) { *p = v; if (a) px_darken(p, a); }
        return;
    }
    for (int x = x0; x <= x1; x++, p++) {
        blend565(p, s, alpha);
        if (l) *p = px_lit(*p, l);
        if (a) px_darken(p, a);
    }
}

/* water gradient colour of row y (shared by the scene bake and the frond
 * pre-tint below) */
static uint8_t g_water_style, g_sand_style, g_journal_theme;
static const uint32_t WATER_PALETTE[WATER_STYLE_COUNT][3] = {
    {0x0a3c46, 0x08272f, 0x031015}, {0x164e80, 0x102f56, 0x060f24},
    {0x0b655c, 0x073c39, 0x031b21}, {0x493775, 0x292344, 0x100d21},
    {0x705029, 0x403222, 0x19150f},
    {0xe63b52, 0x26c985, 0x843ee8},
    {0xfa299c, 0xb51c84, 0x42134f},
    {0x9aef24, 0x33ac42, 0x104f37}
};
static const uint32_t RAINBOW_STOPS[] = {
    0xe63b52, 0xf38c27, 0xe6cc32, 0x26c985, 0x21aecb, 0x4166e8, 0x843ee8
};
static const uint32_t SAND_PALETTE[SAND_STYLE_COUNT][4] = {
    {0x2e3b2c, 0x22301f, 0x1a2418, 0x101a12},
    {0xc7a76b, 0x9e804c, 0x75603b, 0x51412b},
    {0xe7dfc5, 0xbfbba9, 0x938f81, 0x66665e},
    {0x555665, 0x373947, 0x262935, 0x171b25},
    {0xe63b52, 0xf38c27, 0x26c985, 0x4166e8},
    {0xff7cce, 0xf52aa1, 0xc61b7c, 0x8f185f},
    {0x8df5ff, 0x27cfe6, 0x159caf, 0x126b81},
    {0xd4ff65, 0x9ce92c, 0x6eae1b, 0x466f15}
};
static uint32_t rainbow_rgb(float p) {
    if (p < 0) p = 0;
    if (p > 1) p = 1;
    float stop = p * 6;
    int i = (int)stop;
    return i >= 6 ? RAINBOW_STOPS[6] : mix(RAINBOW_STOPS[i], RAINBOW_STOPS[i + 1], stop - i);
}
static inline uint32_t water_rgb(int y) {
    float p = (float)y / TANK_BOT;                 /* the gradient runs surface to floor; under a bowl's sand line it holds */
    if (p > 1) p = 1;
    if (p < 0) p = 0;
    if (g_water_style == WATER_STYLE_RAINBOW) return rainbow_rgb(p);
    const uint32_t *colors = WATER_PALETTE[g_water_style];
    return p < 0.45f ? mix(colors[0], colors[1], p / 0.45f)
                     : mix(colors[1], colors[2], (p - 0.45f) / 0.55f);
}

/* Fronds are drawn OPAQUE from a per-row pre-tinted palette (2026-09-04):
 * the 220/255 blend against the water that gave them their depth tint is
 * folded in here, once per row per colour, when the day/night dim changes -
 * so each frond pixel is a store instead of a PSRAM read-modify-write
 * (a ceiling-high jungle was ~16 of 19.5 ms on the device). Only what a
 * frond covers OTHER than water changes: a fish behind a front-layer frond
 * no longer shows through at 14%. */
#define VEG_ALPHA 220
static uint16_t g_veg_row[4][TANK_H];   /* grass pair, then the sword plant's yellow-green pair */
static float    g_veg_row_dim = -1;
static void veg_tint_fill(float dim) {
    static const uint32_t frond[4] = { 0x3f8b55, 0x2e7d4f, 0x8dbb48, 0x6c9d38 };
    if (g_veg_row_dim == dim) return;
    for (int y = 0; y < TANK_H; y++) {
        uint32_t w = water_rgb(y);
        for (int k = 0; k < 4; k++)
            g_veg_row[k][y] = rgb565(mix(w, frond[k], VEG_ALPHA / 255.0f), dim);
    }
    g_veg_row_dim = dim;
}

#define VEG_SEG_DY   VEG_SEG_PX           /* segment pitch, px of height (tank.h: 3.2, the watch 4.3) */
#define VEG_SEG_RY   2.2f                 /* the old segment ellipse's half-height */
#define VEG_MAX_SEGS (VEG_SEGS_FULL + 5)  /* tank_veg_bed tops out at VEG_SEGS_FULL */
/* final: the scene cache is live, so each frond span applies its own vignette
 * (see span_final) and needs no re-apply rect - a full canopy used to hand
 * the sweep three bed-sized boxes, the largest PSRAM traffic in the frame. */
/* a bed's depth (2026-09-16): the grass beds are woven with the fish
 * (alternate fronds behind and in front); the plant is wherever the keeper
 * put it - all behind, woven, or all in front (tank_decor_z) */
static int bed_z(const tank_t *t, int b) { return b == 3 ? tank_decor_z(t, 0) : DECOR_Z_MIDDLE; }
/* a frond span - minus the castle's silhouette while the castle stands IN
 * FRONT of the grass (the pieces outside it are drawn; the mask and
 * g_veg_mask_cx live with the castle, below). Off the castle it is one span. */
#define CASTLE_FY   (TANK_BOT - 16)
#define CASTLE_ROWS 164
#define CASTLE_ARCH_R 26                     /* the opening's half-width (and the vault's radius) */
#define CASTLE_ARCH_S 24                     /* the spring line: straight jambs below, the vault above */
#define CASTLE_TRIM   5                      /* the brick trim's width */
static uint32_t g_castle_mask[CASTLE_ROWS + 12][224 / 32];        /* its silhouette, centre-relative (shape only: valid at any x) */
/* ... and its FRONT ROW alone (what stands over the fish when it is IN FRONT): castle_front() in the PSRAM decor scratch (internal RAM is spoken for) */
static uint32_t (*castle_front(void))[224 / 32];
static int      g_veg_mask_cx;
/* the arch passes (2026-10-17): with the castle IN FRONT its arch shows the
 * back of the tank, so what the frame draws AFTER the fish (the odd fronds,
 * a bed IN FRONT) must go BEFORE them inside the arch - a fish in the
 * gateway is in front of what it frames. -1: only the arch's pixels (the
 * early pass); +1: everything but them (the late pass); 0: no rule. */
static int      g_veg_arch_pass;
static bool     castle_arch(int x, int y);
/* the pieces a bed's spans leave alone (2026-10-17): a bit per piece in the
 * list - every piece IN FRONT of the plant layer for a grass bed, and for
 * the sword plant also the pieces at its own depth ranked above it
 * (render_tank sets it per bed: veg_skip_for) */
static uint32_t g_veg_skip;
static bool pc_covers(int k, int x, int y);
static bool pc_in_box(int k, int x0, int x1, int y);
static uint32_t pc_row_bits(int k, int x0, int n, int y);   /* piece k's mask over n (<= 32) pixels from x0 on row y */
static inline void veg_span(ctx_t *c, int x0, int x1, int y, const src_t *s, bool final) {
    uint32_t near = 0;
    for (int k = 0; k < g_npc; k++) if (((g_veg_skip >> k) & 1) && pc_in_box(k, x0, x1, y)) near |= 1u << k;
    bool arch_box = g_veg_mask_cx >= 0 && y <= CASTLE_FY && y >= CASTLE_FY - CASTLE_ARCH_S - CASTLE_ARCH_R
                    && x1 >= g_veg_mask_cx - CASTLE_ARCH_R && x0 <= g_veg_mask_cx + CASTLE_ARCH_R;
    if (g_veg_arch_pass < 0 && !arch_box) return;      /* the early arch pass: nothing outside the arch */
    if (!near && !(g_veg_arch_pass && arch_box)) {      /* the usual frond: one span */
        if (final) span_final(c, x0, x1, y, s, 255); else span(c, x0, x1, y, s, 255);
        return;
    }
    /* near a piece: the pieces' bits over the span, one look per piece
       (a frond is a few px wide: one or two mask words), then the runs they
       leave. A span over 32 px (none today) goes pixel by pixel. */
    int n = x1 - x0 + 1;
    uint32_t m = 0, full = n < 32 ? (1u << n) - 1 : 0xffffffffu;
    if (n <= 32) {
        for (int k = 0; k < g_npc; k++) if ((near >> k) & 1) m |= pc_row_bits(k, x0, n, y);
        if (g_veg_arch_pass && arch_box)                 /* the arch: the early pass draws only it, the late one skips it */
            for (int i = 0; i < n; i++) { bool a = castle_arch(x0 + i, y); if (g_veg_arch_pass > 0 ? a : !a) m |= 1u << i; }
        if (!m) { if (final) span_final(c, x0, x1, y, s, 255); else span(c, x0, x1, y, s, 255); return; }
        if (m == full) return;
    }
    int run = -1;
    for (int i = 0; i <= n; i++) {
        bool in = i == n;
        if (!in) {
            if (n <= 32) in = (m >> i) & 1;
            else {
                for (int k = 0; k < g_npc && !in; k++) if (((near >> k) & 1) && pc_covers(k, x0 + i, y)) in = true;
                if (!in && g_veg_arch_pass && arch_box) { bool a = castle_arch(x0 + i, y); if (g_veg_arch_pass > 0 ? a : !a) in = true; }
            }
        }
        if (!in) { if (run < 0) run = x0 + i; }
        else if (run >= 0) {
            if (final) span_final(c, run, x0 + i - 1, y, s, 255); else span(c, run, x0 + i - 1, y, s, 255);
            run = -1;
        }
    }
}
/* layer: 0 = the even fronds, 1 = the odd ones, -1 = every frond */
static const int VEG_SEED[VEG_BEDS_MAX] = { 0, 7, 3, 5 };   /* each bed's sway phase (the seahorse's blade reads it too) */
static void draw_veg(ctx_t *c, const tank_t *t, int b, int seed, int layer, bool final) {
    veg_tint_fill(c->dim);
    int n; tank_veg_bed(t, b, NULL, NULL, NULL, &n);
    /* the sword plant (bed 3, the shop): broad lanceolate leaves - a narrow
       stem, 7 px half-width at 40% of the way up, a point at the tip - a
       lazier sway, its own yellow-green pair */
    bool sword = tank_veg_kind(t, b) == VEG_KIND_SWORD;
    float root = 2.4f, tip = 0.4f, amp = sword ? 2.5f : 4.0f;
    for (int i = 0; i < n; i++) {
        if (layer >= 0 && (i & 1) != layer) continue;
        float bx;
        /* each frond's own height (tank_t.veg_h): what the keeper cut is
           exactly what shows - no render-side variation on top */
        int segs = tank_veg_frond(t, b, i, &bx);
        if (g_veg_arch_pass < 0 && (bx + 14 < g_veg_mask_cx - CASTLE_ARCH_R || bx - 14 > g_veg_mask_cx + CASTLE_ARCH_R)) continue;   /* the early arch pass: only the fronds that can reach the arch (sway + a leaf's half width) */
        if (segs < 1) segs = 1;                    /* nubs: always a bit of green */
        if (segs > VEG_MAX_SEGS) segs = VEG_MAX_SEGS;
        float sway = fast_sin(t->clock * (sword ? 0.6f : 0.9f) + (i + seed) * 1.7f) * amp;
        /* the swaying chain of segment centres (the frond's spine) */
        float xs[VEG_MAX_SEGS + 1];
        for (int seg = 0; seg <= segs; seg++)
            xs[seg] = bx + sway * seg / (float)segs * fast_sin(seg * 0.4f + t->clock * 0.6f + i + seed);
        /* one span per pixel row, centred on the spine, half-width tapering
         * toward the tip: the same silhouette the old chain of overlapping
         * ellipses drew (their union was a 5 px ribbon), at ~a quarter fewer
         * pixels and without a sqrt per row. Full canopy = ~1000 segments. */
        const uint16_t *pal = g_veg_row[(sword ? 2 : 0) + ((i + seed) & 1)];
        int y_bot = (int)(TANK_BOT - 16 + VEG_SEG_RY);
        int y_top = (int)(TANK_BOT - 16 - (segs - 1) * VEG_SEG_DY - VEG_SEG_RY);
        for (int y = y_bot; y >= y_top; y--) {
            float sp = (TANK_BOT - 16 - y) / VEG_SEG_DY;          /* fractional segment */
            if (sp < 0) sp = 0;
            if (sp > segs - 1) sp = (float)(segs - 1);
            /* taper relative to the frond's own length (2.4 px at the root,
             * 0.4 at the tip): the old fixed 0.07/segment thinned every frond
             * to nothing at 29 segments, a hidden height cap now that fronds
             * grow to the ceiling (VEG_SEGS_FULL) */
            float u = sp / (segs > 1 ? segs - 1 : 1);
            float half = sword ? 1.2f + 6.0f * (u < 0.4f ? u / 0.4f : (1 - u) / 0.6f)
                               : root - (root - tip) * u;
            if (half < tip) half = tip;
            int k = (int)sp; float fr = sp - k;
            float cx = xs[k] + (xs[k + 1] - xs[k]) * fr;
            src_t s; s.v = pal[y];                              /* opaque: only .v is read */
            veg_span(c, (int)(cx - half), (int)(cx + half), y, &s, final);
        }
    }
}


/* ---- the castle (2026-09-16, from Strato's castle-v2 mockup) ----
 * A swim-through decoration drawn the way the fish and the fronds are -
 * spans and fills from a little geometry, no bitmap - so it sits in the same
 * water as everything else instead of a pixel-art sprite clashing with it.
 * Two layers: the KEEP (towers, battlements, the base, the dark courtyard
 * behind the arch) is static and bakes into the scene cache with the floor;
 * the GATE WALL (the curtain wall round the arch, its brick trim and jambs)
 * draws after the fish every frame, so a fish crossing the arch is tucked
 * behind the jambs and seen through the opening - it swims THROUGH. Stone
 * is a brick-course pattern from a position hash (the pebbled floor's
 * trick), golden-lit from the upper left like the mockup, mossed toward the
 * base, and pre-tinted with the water of its row like the fronds - the gate
 * a touch less than the keep, so the keep reads farther back. Geometry is
 * in px about the centre x on the floor line; ~176 x 150 px, the arch
 * opening 52 x 50 (an adult fish is ~37 x 22). */
/* CASTLE_FY (the floor line the keep stands on) and CASTLE_ROWS (rows above
 * it the palette covers) are defined with veg_span above */
enum { CT_LIGHT, CT_MID, CT_MIDDK, CT_DARK, CT_MORTAR, CT_MOSS_A, CT_MOSS_B,
       CT_BRICK_L, CT_BRICK, CT_BRICK_D, CT_ROOF_L, CT_ROOF, CT_ROOF_D, CT_INSIDE, CT_N };
static const uint32_t CASTLE_RGB[CT_N] = {
    0xd9c58c, 0xa99b7b, 0x87795f, 0x5d5446, 0x6a6151, 0x7aa33c, 0x527f2e,
    0xe08a58, 0xc25f38, 0x8b3d24, 0xe4cd8a, 0xc4a95e, 0x8f7a42, 0x061219 };
/* [keep | gate][tone][row], row 0 = CASTLE_FY - CASTLE_ROWS */
static uint16_t g_castle_row[2][CT_N][CASTLE_ROWS + 12];
static float    g_castle_dim = -1;
/* the castle's silhouette, one bit per pixel in local coords (x - cx + 112,
 * 0..223; row 0 = CASTLE_FY - CASTLE_ROWS): every pixel castle_put writes
 * sets its bit, so the first full draw fills it. While the castle stands IN
 * FRONT of the grass, frond spans skip the pixels inside it (veg_span). */
#define CASTLE_MASK_W 224
static void castle_tint_fill(float dim) {
    if (g_castle_dim == dim) return;
    for (int r = 0; r < CASTLE_ROWS + 12; r++) {
        int y = CASTLE_FY - CASTLE_ROWS + r;
        uint32_t w = water_rgb(y < 0 ? 0 : y >= TANK_H ? TANK_H - 1 : y);
        for (int k = 0; k < CT_N; k++) {
            g_castle_row[0][k][r] = rgb565(mix(w, CASTLE_RGB[k], k == CT_INSIDE ? 0.6f : 0.70f), dim);
            g_castle_row[1][k][r] = rgb565(mix(w, CASTLE_RGB[k], k == CT_INSIDE ? 0.6f : 0.82f), dim);
        }
    }
    g_castle_dim = dim;
}
static inline uint32_t chash(int a, int b) {
    uint32_t h = (uint32_t)a * 73856093u ^ (uint32_t)b * 19349663u;
    h ^= h >> 13; h *= 0x5bd1e995u; h ^= h >> 15;
    return h;
}
/* moss creeps up from the base: 4 px cells, a chance that fades with height,
 * feathered per pixel so the patches have ragged edges */
static inline int castle_moss(int lx, int y, int extra_pct) {
    int d = CASTLE_FY - y;
    int pct = (d < 36 ? 26 - d * 26 / 36 : 0) + extra_pct;
    if (pct <= 0) return -1;
    uint32_t h = chash((lx + 4096) >> 2, y >> 2);
    if ((int)(h % 100) >= pct) return -1;
    if ((chash(lx, y) & 7) == 0) return -1;
    return (h >> 8) & 1 ? CT_MOSS_A : CT_MOSS_B;
}
/* a pixel's tone. mode 0 = WALL (5 px brick courses, 9 px bricks), 1 = ROOF
 * (4 px tile courses), 2 = RUBBLE (the base: no courses, mossy), 3 = the
 * brick JAMBS (courses), 4 = INSIDE. u = 0..1 across the element: the left
 * fifth catches the light, the right fifth falls into shadow. */
static int castle_tone(int mode, int lx, int y, float u) {
    if (mode == 4) return CT_INSIDE;
    int d = CASTLE_FY - y;
    int lit = u < 0.22f ? -1 : u > 0.78f ? 1 : 0;
    if (mode == 2) {
        int m = castle_moss(lx, y, 18); if (m >= 0) return m;
        static const int8_t pick[8] = { CT_MIDDK, CT_DARK, CT_DARK, CT_MIDDK, CT_MID, CT_DARK, CT_MIDDK, CT_DARK };
        int t = pick[chash(lx >> 1, y) & 7] + (lit < 0 ? -1 : 0);
        return t < CT_MID ? CT_MID : t;
    }
    int pitch = mode == 1 ? 4 : 5, len = mode == 1 ? 6 : 9;
    int course = d / pitch;
    if (d % pitch == 0) return mode == 3 ? CT_BRICK_D : mode == 1 ? CT_ROOF_D : CT_MORTAR;
    int bx = lx + 4096 + ((course & 1) ? len / 2 : 0);
    if (bx % len == 0) return mode == 3 ? CT_BRICK_D : mode == 1 ? CT_ROOF_D : CT_MORTAR;
    if (mode == 3) {
        static const int8_t pick[4] = { CT_BRICK, CT_BRICK, CT_BRICK_L, CT_BRICK_D };
        int t = pick[chash(course, bx / len) & 3] + lit;
        return t < CT_BRICK_L ? CT_BRICK_L : t > CT_BRICK_D ? CT_BRICK_D : t;
    }
    if (mode == 1) {
        static const int8_t pick[4] = { CT_ROOF, CT_ROOF, CT_ROOF_L, CT_ROOF_D };
        int t = pick[chash(course, bx / len) & 3] + lit;
        return t < CT_ROOF_L ? CT_ROOF_L : t > CT_ROOF_D ? CT_ROOF_D : t;
    }
    int m = castle_moss(lx, y, 0); if (m >= 0) return m;
    static const int8_t pick[8] = { CT_MID, CT_MID, CT_MID, CT_MIDDK, CT_MID, CT_MIDDK, CT_LIGHT, CT_MIDDK };
    int t = pick[chash(course, bx / len) & 7] + lit;
    return t < CT_LIGHT ? CT_LIGHT : t > CT_DARK ? CT_DARK : t;
}
typedef struct { ctx_t *c; int cx; int layer; bool final; } cst_t;
static inline void castle_put(const cst_t *k, int x, int y, uint16_t v) {
    ctx_t *c = k->c;
    if (!CTX_IN(c, x, y)) return;
    { int lx = x - k->cx + CASTLE_MASK_W / 2, r = y - (CASTLE_FY - CASTLE_ROWS);
      if ((unsigned)lx < CASTLE_MASK_W && (unsigned)r < CASTLE_ROWS + 12) {
          g_castle_mask[r][lx >> 5] |= 1u << (lx & 31);
          if (k->layer == 1) castle_front()[r][lx >> 5] |= 1u << (lx & 31);
      } }
    uint16_t *p = &CTX_PX(c, x, y);
    *p = v;
    if (k->final) {                          /* vignetted here, and untagged: the sweep must not darken it again */
        PK_LIGHT_PX(p, x, y);
        int a = g_vig ? g_vig[y * TANK_W + x] : vig_alpha(x, y);
        if (a) px_darken(p, a);
        if (g_dirty) g_dirty[y * DIRTY_WORDS_PER_ROW + (x >> 5)] &= ~(1u << (x & 31));
    } else dirty_px(x, y);
}
/* one row of an element: local x lx0..lx1 on tank row y; ux0..ux1 = the
 * element's full width on that row, for the lighting */
static void castle_span(const cst_t *k, int lx0, int lx1, int y, int mode, float ux0, float ux1) {
    int r = y - (CASTLE_FY - CASTLE_ROWS);
    if (r < 0 || r >= CASTLE_ROWS + 12) return;
    const ctx_t *c = k->c;                       /* clip to the window first: tones cost */
    if ((unsigned)(y - c->oy) >= (unsigned)c->h) return;
    if (k->cx + lx0 < c->ox) lx0 = c->ox - k->cx;
    if (k->cx + lx1 > c->ox + c->w - 1) lx1 = c->ox + c->w - 1 - k->cx;
    float uw = ux1 > ux0 ? ux1 - ux0 : 1;
    for (int lx = lx0; lx <= lx1; lx++)
        castle_put(k, k->cx + lx, y, g_castle_row[k->layer][castle_tone(mode, lx, y, (lx - ux0) / uw)][r]);
}
static void castle_rect(const cst_t *k, int lx0, int lx1, int ytop, int ybot, int mode) {
    for (int y = ytop; y <= ybot; y++) castle_span(k, lx0, lx1, y, mode, (float)lx0, (float)lx1);
}
/* a conical roof: half_base wide at ybase, a point at yapex, plus an eave
 * a pixel wider than the tower */
static void castle_cone(const cst_t *k, int lxc, int half_base, int ybase, int yapex) {
    for (int y = yapex; y <= ybase; y++) {
        float t = (y - yapex) / (float)(ybase - yapex);
        int half = (int)(half_base * t + 0.5f);
        castle_span(k, lxc - half, lxc + half, y, 1, (float)(lxc - half), (float)(lxc + half));
    }
}
/* merlons along a top edge: 7 wide, 7 tall, on a 12 px pitch */
static void castle_merlons(const cst_t *k, int lx0, int lx1, int ytop) {
    for (int x = lx0; x + 6 <= lx1; x += 12) castle_rect(k, x, x + 6, ytop - 7, ytop - 1, 0);
}
/* an arched window: w wide, from ybot up to ytop, the vault a half-disc */
static void castle_window(const cst_t *k, int lxc, int w, int ytop, int ybot) {
    float r = w / 2.0f;
    for (int y = ytop; y <= ybot; y++) {
        float dy = (ytop + r) - y;
        float half = dy > 0 ? r * ell_half(dy / r) : r;
        castle_span(k, (int)(lxc - half + 0.5f), (int)(lxc + half - 0.5f), y, 4, 0, 1);
    }
}
/* the gate: the arch's brick trim (a ring of voussoirs over the vault, jambs
 * down the sides) and the opening. A static tone map for the ring: the
 * voussoir index needs an angle, and the gate redraws every frame. */
static int8_t g_ring[CASTLE_ARCH_R + CASTLE_TRIM + 1][2 * (CASTLE_ARCH_R + CASTLE_TRIM) + 1];
static bool   g_ring_filled = false;
static void castle_ring_fill(void) {
    if (g_ring_filled) return;
    int R = CASTLE_ARCH_R + CASTLE_TRIM;
    for (int j = 0; j <= R; j++)
        for (int i = 0; i <= 2 * R; i++) {
            float dx = i - R, dy = (float)j;                 /* dy up from the spring line */
            float d = sqrtf(dx * dx + dy * dy);
            int8_t t = -1;
            if (d >= CASTLE_ARCH_R && d < R + 0.5f) {
                float ang = atan2f(dy, dx) / 3.14159265f * 9;   /* 9 voussoirs across the half-turn */
                int v = (int)ang; float fr = ang - v;
                t = fr < 0.14f ? CT_BRICK_D : (chash(v, 3) & 3) == 0 ? CT_BRICK_L : (chash(v, 3) & 3) == 1 ? CT_BRICK_D : CT_BRICK;
                if (v < 3 && t == CT_BRICK) t = CT_BRICK_L;    /* the lit side */
                if (v > 5 && t == CT_BRICK) t = CT_BRICK_D;
            }
            g_ring[j][i] = t;
        }
    g_ring_filled = true;
}
static void castle_gate_trim(const cst_t *k) {
    int R = CASTLE_ARCH_R + CASTLE_TRIM, ys = CASTLE_FY - CASTLE_ARCH_S;
    castle_ring_fill();
    for (int j = 0; j <= R; j++) {
        int y = ys - j, r = y - (CASTLE_FY - CASTLE_ROWS);
        if (r < 0) continue;
        for (int i = 0; i <= 2 * R; i++)
            if (g_ring[j][i] >= 0) castle_put(k, k->cx + i - R, y, g_castle_row[k->layer][g_ring[j][i]][r]);
    }
    castle_rect(k, -R, -CASTLE_ARCH_R - 1, ys + 1, CASTLE_FY, 3);
    castle_rect(k,  CASTLE_ARCH_R + 1, R,  ys + 1, CASTLE_FY, 3);
}
/* the gate wall: the curtain between the left tower and the right, minus the
 * opening. Drawn in the keep pass (baked) and again after the fish. */
#define CASTLE_GATE_X0 (-34)
#define CASTLE_GATE_X1  55
#define CASTLE_WALL_TOP (CASTLE_FY - 58)
/* the keep's curtain wall behind the gate, minus the opening (2026-10-17,
 * Strato: "let visibility pass straight through the castle arch so we see to
 * the back of the tank"): the arch paints NOTHING, so the water, the floor
 * and the pieces baked behind the castle show in it, the grass and the plant
 * draw through it (veg_span: the opening is not in the castle's mask), and a
 * fish in the gateway stays in front of them (render_tank's arch passes).
 * castle_opening's dark courtyard is gone with it. */
static void castle_back_wall(const cst_t *k) {
    int ys = CASTLE_FY - CASTLE_ARCH_S, R = CASTLE_ARCH_R;
    for (int y = CASTLE_WALL_TOP; y <= CASTLE_FY; y++) {
        float half = y > ys ? R : (ys - y) < R ? R * ell_half((ys - y) / (float)R) : 0;
        int h = (int)(half - 0.5f);
        if (half < 1) castle_span(k, -44, 44, y, 0, -44, 44);
        else { castle_span(k, -44, -h - 1, y, 0, -44, 44); castle_span(k, h + 1, 44, y, 0, -44, 44); }
    }
}
static void castle_gate_wall(const cst_t *k) {
    int ys = CASTLE_FY - CASTLE_ARCH_S, R = CASTLE_ARCH_R + CASTLE_TRIM;
    for (int y = CASTLE_WALL_TOP; y <= CASTLE_FY; y++) {
        float half = y > ys ? R : (ys - y) < R ? R * ell_half((ys - y) / (float)R) : 0;
        int h = (int)half;
        if (h < 1) castle_span(k, CASTLE_GATE_X0, CASTLE_GATE_X1, y, 0, -44, 44);
        else {
            castle_span(k, CASTLE_GATE_X0, -h - 1, y, 0, -44, 44);
            castle_span(k, h + 1, CASTLE_GATE_X1, y, 0, -44, 44);
        }
    }
    castle_merlons(k, -44, 44, CASTLE_WALL_TOP);
    castle_gate_trim(k);
}
/* the FRONT ROW: the gate wall and the three towers flush with it. Drawn in
 * the keep pass (baked) and again over the fish, clipped to their rects. */
static void castle_front_row(const cst_t *k) {
    int FY = CASTLE_FY;
    castle_gate_wall(k);
    /* the left tower, pointed */
    castle_rect(k, -62, -34, FY - 78, FY, 0);
    castle_cone(k, -48, 18, FY - 78, FY - 108);
    castle_window(k, -48, 7, FY - 62, FY - 48);
    /* the short far-left tower */
    castle_rect(k, -86, -60, FY - 46, FY, 0);
    castle_cone(k, -73, 17, FY - 46, FY - 72);
    castle_window(k, -73, 6, FY - 30, FY - 19);
    /* the open far-right tower, crenellated */
    castle_rect(k, 56, 86, FY - 70, FY, 0);
    castle_merlons(k, 56, 86, FY - 70);
    castle_window(k, 71, 7, FY - 44, FY - 30);
}
/* the whole castle (front = 0: the keep, then the front row over it - the
 * scene bake), or the front row alone (front = 1: over the fish) */
static void draw_castle(ctx_t *c, int cx, int front, bool final) {
    castle_tint_fill(c->dim);
    cst_t k = { c, cx, 1, final };
    if (front) { castle_front_row(&k); return; }
    k.layer = 0;
    int FY = CASTLE_FY;
    /* the base: a low rubble mound the towers stand on */
    for (int y = FY - 9; y <= FY + 6; y++) {
        float w = ell_half((y - (FY - 1)) / 8.0f);
        if (w <= 0) continue;
        int half = (int)(92 * w);
        castle_span(&k, -half, half, y, 2, (float)-half, (float)half);
    }
    /* the rear tower, tallest, right of centre: behind the wall */
    castle_rect(&k, 24, 54, FY - 108, FY, 0);
    castle_cone(&k, 39, 19, FY - 108, FY - 142);
    castle_window(&k, 39, 7, FY - 92, FY - 78);
    /* the curtain wall behind the gate's opening, then the courtyard's dark */
    castle_back_wall(&k);                    /* the arch is OPEN (2026-10-17): the tank behind shows through it */
    k.layer = 1;
    castle_front_row(&k);
}

/* ---- the coral, procedural (2026-09-23; Strato's coral-single.png: a
 * branching orange fan in chunky pixel art - rounded branches, a dark red
 * rim, sunlit yellow tips). Built ONCE as a tone sprite on a 2 px cell grid
 * (CORAL_CW x CORAL_CH cells): a skeleton of capsules (the trunk and its
 * branches), each cell classed by its signed distance to the nearest one -
 * the rim, a lit edge up-left, the tips, and a hashed speckle inside. The
 * colour is the keeper's (tank_coral_rgb): five tones derived from it and
 * hazed with the water per row, like the castle's stone. ~60 x 92 px. */
#define CORAL_CELL 2
#define CORAL_CW   32
#define CORAL_CH   46
#define CORAL_W    (CORAL_CW * CORAL_CELL)
#define CORAL_H    (CORAL_CH * CORAL_CELL)
#define CORAL_FY   (TANK_BOT - 14 + DECOR_SINK)   /* the base, sunk into the pebbles (a mound covers the joint) */
enum { CO_NONE = 0, CO_RIM, CO_BODY, CO_SHADE, CO_LIT, CO_TIP, CO_N };
static int      g_coral_q = -1;               /* the growth step the sprite was built for (CORAL_Q steps) */
#define CORAL_Q 128                           /* ~5.6 h per step over the 30 days: a rebuild each */
static int      g_coral_cells;                /* filled cells in the sprite (the selftest) */
static uint32_t g_coral_rgb = 1;              /* what the rows hold (1 = never) */
static float    g_coral_dim = -1;
/* the decor scratch (2026-09-24): every table the coral and the reef cluster
 * build - sprites, tinted rows, the distance grids - lives in ONE block the
 * platform hands over (render_set_decor_scratch: the firmware gives PSRAM,
 * since internal RAM is down to ~23 KB free and the display driver needs
 * 57 KB of DMA memory at boot - the first cluster build's statics cost it
 * that and the tank booted black). Without one it is calloc'd (the sim). */
#define CL_CW   72
#define CL_CH   60
#define CL_H    (CL_CH * 2)
#define CL_TONES (1 + 5 * 5)
#define PK_CELL 2                                 /* the pumpkin lantern (below): a 2 px cell sprite like the coral's */
#define PK_CW   44
#define PK_CH   41
#define PK_N    16
typedef struct {
    uint8_t  co_sprite[CORAL_CH][CORAL_CW];
    uint16_t co_row[CO_N][CORAL_H];           /* [tone][row], row 0 = CORAL_FY - CORAL_H */
    int8_t   co_sd[CORAL_CH][CORAL_CW];
    uint8_t  co_tp[CORAL_CH][(CORAL_CW + 7) / 8];
    uint8_t  cl_sprite[CL_CH][CL_CW];
    uint16_t cl_row[CL_TONES][CL_H];
    int8_t   cl_sd[CL_CH][CL_CW];
    uint8_t  cl_dp[CL_CH][(CL_CW + 7) / 8];
    uint32_t pm[4][CASTLE_ROWS + 12][(CASTLE_MASK_W + 31) / 32];   /* the pieces' masks (2026-10-17): what piece k of the list covers
                                                                    * in the baked scene (sprite + mound); row 0 = its box's top, bit 0 its left */
    uint32_t castle_front[CASTLE_ROWS + 12][(CASTLE_MASK_W + 31) / 32];   /* the castle's front row, centre-relative (castle_put) */
    /* the seahorse's frame scratch (0.3.6): its sprite (a byte a pixel), the
     * tail's distance map and its path - in PSRAM, never the stack or .bss */
    uint8_t  sh_spr[39 * 23], sh_d[39 * 23], sh_fr[39 * 23], sh_fl[39 * 23];
    uint8_t  sh_spr_card[39 * 23];                          /* the card's portrait: its own, so the tank's cached sprite
                                                             * is not overwritten under it (the tail wiggled, 10-15) */
    float    sh_rowx[39], sh_yoff;                          /* the upright draw's snapped row shifts and row offset (hysteresis) */
    float    sh_pts[140][3];                                /* x, y, arc length */
    uint8_t  sh_behind[140];
    uint8_t  sn_glass[35 * 35];                             /* the snail on the glass: what its heading alone decides (draw_snail) */
    uint8_t  pk_sprite[PK_CH][PK_CW];                       /* the pumpkin lantern's tones, built once */
    uint16_t pk_row[PK_N][PK_CH * PK_CELL];                 /* its rind tones per row, hazed and dimmed */
    uint8_t  pk_body[PK_CH][PK_CW], pk_hole[PK_CH][PK_CW];  /* the build's grids (internal RAM is spoken for) */
} decor_scratch_t;
static decor_scratch_t *g_ds;
static bool  g_pk_built;                          /* the pumpkin's sprite is in the scratch */
static float g_pk_dim = -1;                       /* the dim its rind rows were tinted for */
size_t render_decor_scratch_size(void) { return sizeof(decor_scratch_t); }
static float g_sng_heading;
void   render_set_decor_scratch(void *buf) { g_ds = (decor_scratch_t *)buf; if (g_ds) memset(g_ds, 0, sizeof *g_ds); g_coral_q = -1; g_coral_rgb = 1; g_sng_heading = 1e9f; g_pk_built = false; g_pk_dim = -1; }
static inline decor_scratch_t *ds(void) { if (!g_ds) g_ds = (decor_scratch_t *)calloc(1, sizeof(decor_scratch_t)); return g_ds; }
static uint32_t (*castle_front(void))[224 / 32] { return ds()->castle_front; }
/* the bake of a piece IN FRONT records what it covers (bake_scene sets these) */
static uint32_t *g_rec; static int g_rec_x0, g_rec_y0, g_rec_w, g_rec_rows, g_rec_wpr;
static inline void rec_px(int x, int y) {
    if (!g_rec) return;
    int lx = x - g_rec_x0, r = y - g_rec_y0;
    if ((unsigned)lx < (unsigned)g_rec_w && (unsigned)r < (unsigned)g_rec_rows) g_rec[r * g_rec_wpr + (lx >> 5)] |= 1u << (lx & 31);
}
typedef struct { float x0, y0, x1, y1, r; uint8_t tip; float g0, g1; } coral_seg_t;
/* cell coordinates, x from the left edge, y UP from the base row. g0..g1 is
 * the segment's GROWTH window (tank_coral_growth, 2026-09-23): absent below
 * g0, reaching out along its line until g1 - a child branch's window opens
 * where its parent's closes, so the fan comes in trunk first, low branches,
 * then the crown over the month. */
static const coral_seg_t CORAL_SEGS[] = {
    { 16, 0, 17, 20, 3.0f, 0, 0.00f, 0.30f },   /* the trunk: thick, like the art's */
    { 17, 20, 18, 33, 2.7f, 0, 0.30f, 0.60f },
    { 18, 33, 18, 41, 2.3f, 1, 0.60f, 0.85f },  /* its tip */
    { 16, 7, 6, 15, 2.5f, 0, 0.12f, 0.40f },    /* left, low */
    { 6, 15, 3, 22, 2.2f, 1, 0.40f, 0.62f },
    { 17, 12, 26, 18, 2.4f, 0, 0.20f, 0.48f },  /* right, low */
    { 26, 18, 29, 25, 2.1f, 1, 0.48f, 0.70f },
    { 17, 20, 9, 28, 2.4f, 0, 0.36f, 0.62f },   /* left, mid */
    { 9, 28, 7, 35, 2.1f, 1, 0.62f, 0.82f },
    { 18, 26, 25, 33, 2.3f, 0, 0.45f, 0.72f },  /* right, mid */
    { 25, 33, 27, 40, 2.0f, 1, 0.72f, 0.92f },
    { 18, 31, 12, 39, 2.2f, 1, 0.62f, 1.00f },  /* left, high: the last to finish */
    { 17, 3, 22, 7, 1.9f, 1, 0.05f, 0.20f },    /* a nub */
};
#define CORAL_NSEG ((int)(sizeof CORAL_SEGS / sizeof CORAL_SEGS[0]))
/* signed distance to the skeleton AT growth g, in cells; *tip = within a
 * lit cap (a finished tip's, or the reaching end of a branch still growing) */
#define CORAL_Y_SCALE 0.88f                                  /* the fan tops out ~76 px up (Strato: a modest height, never the grass's) */
#define CORAL_TOP_X   18.0f                                  /* the crown's root: the trunk tip's end, in cells */
#define CORAL_TOP_Y   (41.0f * CORAL_Y_SCALE)
static float coral_sd(float px, float py, float g, bool *tip) {
    float best = 1e9f; *tip = false;
    if (g > 1) g = 1;
    float thin = 0.72f + 0.28f * g;                          /* a young coral is slimmer all over */
    for (int i = 0; i < CORAL_NSEG; i++) {
        const coral_seg_t *s = &CORAL_SEGS[i];
        float f = (g - s->g0) / (s->g1 - s->g0);             /* how far along its line it has reached */
        if (f <= 0) continue;
        if (f > 1) f = 1;
        float sy0 = s->y0 * CORAL_Y_SCALE, sy1 = s->y1 * CORAL_Y_SCALE;
        float x1 = s->x0 + (s->x1 - s->x0) * f, y1 = sy0 + (sy1 - sy0) * f;
        float r = s->r * thin * (0.75f + 0.25f * f);
        float dx = x1 - s->x0, dy = y1 - sy0, len2 = dx * dx + dy * dy;
        float u = len2 > 1e-6f ? ((px - s->x0) * dx + (py - sy0) * dy) / len2 : 0;
        if (u < 0) u = 0;
        if (u > 1) u = 1;
        float ex = px - (s->x0 + dx * u), ey = py - (sy0 + dy * u);
        float d = sqrtf(ex * ex + ey * ey) - r;
        if (d < best) best = d;
        if (s->tip || f < 1) {                               /* the lit cap: the last r + 1 cells of a tip */
            float tx = px - x1, ty = py - y1;
            if (tx * tx + ty * ty <= (r + 0.6f) * (r + 0.6f)) *tip = true;
        }
    }
    return best;
}
/* the distance grids are int8 eighths of a cell (the sign and the few
 * thresholds are all the classing needs): internal RAM is tight on the
 * board - the float grids of 2026-09-24 morning cost the display driver its
 * semaphore and the tank booted black */
static inline int8_t sd_q(float d) { d *= 8; return (int8_t)(d < -127 ? -127 : d > 127 ? 127 : d); }
static void coral_build(float g) {
    int8_t  (*sd)[CORAL_CW] = ds()->co_sd;
    uint8_t (*tp)[(CORAL_CW + 7) / 8] = ds()->co_tp;
    g_coral_cells = 0;
    for (int j = 0; j < CORAL_CH; j++)
        for (int i = 0; i < CORAL_CW; i++) {
            bool tip; sd[j][i] = sd_q(coral_sd(i + 0.5f, (CORAL_CH - 1 - j) + 0.5f, g, &tip));
            if (tip) tp[j][i >> 3] |= (uint8_t)(1 << (i & 7)); else tp[j][i >> 3] &= (uint8_t)~(1 << (i & 7));
        }
    for (int j = 0; j < CORAL_CH; j++)
        for (int i = 0; i < CORAL_CW; i++) {
            int d = sd[j][i];
            bool tip = (tp[j][i >> 3] >> (i & 7)) & 1;
            uint8_t t = CO_NONE;
            if (d < 0) {
                /* the light comes from the upper left (the art's): an edge cell
                   with open water above or to its left is LIT, one with open
                   water below or to its right is the dark rim - the outline
                   that keeps the branches apart */
                bool out_l = i == 0 || sd[j][i - 1] >= 0, out_u = j == 0 || sd[j - 1][i] >= 0;
                bool out_r = i == CORAL_CW - 1 || sd[j][i + 1] >= 0, out_d = j == CORAL_CH - 1 || sd[j + 1][i] >= 0;
                bool edge = out_l || out_u || out_r || out_d;
                if (tip) t = (out_r || out_d) && !(out_l || out_u) ? CO_RIM : CO_TIP;
                else if (edge) t = (out_r || out_d) ? CO_RIM : CO_LIT;
                else {
                    uint32_t h = chash(i, j) % 100;
                    t = h < 22 ? CO_SHADE : h < 28 ? CO_LIT : CO_BODY;
                }
            }
            ds()->co_sprite[j][i] = t;
            g_coral_cells += t != CO_NONE;
        }
}
int render_coral_cells(float growth) {                       /* the selftest: how much coral there is at a growth */
    coral_build(growth); g_coral_q = -1;
    return g_coral_cells;
}
static void coral_tint_fill(uint32_t rgb, float dim) {
    if (g_coral_rgb == rgb && g_coral_dim == dim) return;
    uint32_t tone[CO_N];
    tone[CO_NONE]  = 0;
    tone[CO_RIM]   = mix(rgb, 0x30060e, 0.58f);
    tone[CO_BODY]  = rgb;
    tone[CO_SHADE] = mix(rgb, 0x000000, 0.24f);
    tone[CO_LIT]   = mix(rgb, 0xfff0b0, 0.36f);
    tone[CO_TIP]   = mix(rgb, 0xffe8a0, 0.58f);
    for (int r = 0; r < CORAL_H; r++) {
        int y = CORAL_FY - CORAL_H + r;
        uint32_t w = water_rgb(y < 0 ? 0 : y >= TANK_H ? TANK_H - 1 : y);
        for (int k = 1; k < CO_N; k++) ds()->co_row[k][r] = rgb565(mix(w, tone[k], k == CO_TIP ? 0.94f : 0.90f), dim);   /* a light haze: the art's colour stays saturated */
    }
    g_coral_rgb = rgb; g_coral_dim = dim;
}
/* the crown (2026-09-23, Strato: "after it's fully grown it should sprout a
 * ring of delicate tentacles out of the top"): CORAL_TENT one-pixel
 * filaments fanning up and out from the trunk's tip once growth passes 1,
 * reaching CORAL_TENT_L px at CORAL_FULL, each swaying on its own phase with
 * the tank clock and ending in a bright bead. Drawn every frame over the
 * body (dynamic: the caller's rect covers the vignette), so the crown moves
 * while the fan stands still. Returns its bounding box through the pointers. */
#define CORAL_TENT   9
#define CORAL_TENT_L 15.0f
/* a fan of n one-pixel tentacles from (ox, oy), angles a0..a1 (screen
 * radians: -pi/2 is straight up), each `len` px, swaying on the tank clock
 * with its own phase (seed), a bright bead at the end; the bounding box
 * through the pointers (x1 < x0 = nothing drawn). The coral's crown and the
 * cluster's bloom both use it (2026-09-24). */
static void draw_crown(ctx_t *c, float ox, float oy, int n_t, float a0, float a1, float len, uint32_t pale,
                       int seed, float clock, int *bx0, int *by0, int *bx1, int *by1) {
    *bx0 = *by0 = 1 << 20; *bx1 = *by1 = -1;
    if (n_t < 1) return;
    uint32_t bead = mix(pale, 0xffffff, 0.55f);
    for (int k = 0; k < n_t; k++) {
        float a = n_t == 1 ? (a0 + a1) * 0.5f : a0 + (a1 - a0) * k / (n_t - 1);
        float sway = 0.22f * fast_sin(clock * 1.3f + (k + seed) * 0.9f);
        float lx = ox, ly = oy;
        int n = (int)(len + 0.5f);
        for (int i = 1; i <= n; i++) {
            float s = (float)i / n;
            float ang = a + sway * s + (k - (n_t - 1) * 0.5f) * 0.05f * s;   /* the outer ones curl outward */
            float x = ox + cosf(ang) * len * s, y = oy + sinf(ang) * len * s;
            int ix = (int)(x + 0.5f), iy = (int)(y + 0.5f);
            if (ix == (int)(lx + 0.5f) && iy == (int)(ly + 0.5f)) continue;
            if (g_clip_k >= 0 && pc_covered_above(g_clip_k, ix, iy)) continue;   /* under a piece above its own */
            px_blend(c, ix, iy, i == n ? bead : pale, i == n ? 255 : 190);
            if (ix < *bx0) *bx0 = ix;
            if (ix > *bx1) *bx1 = ix;
            if (iy < *by0) *by0 = iy;
            if (iy > *by1) *by1 = iy;
            lx = x; ly = y;
        }
    }
    if (*bx1 >= *bx0) { *bx0 -= 1; *by0 -= 1; *bx1 += 1; *by1 += 1; }
}
static void draw_coral_crown(ctx_t *c, int cx, uint32_t rgb, float growth, float clock,
                             int *bx0, int *by0, int *bx1, int *by1) {
    float phase = (growth - 1.0f) / (CORAL_FULL - 1.0f);
    *bx0 = *by0 = 1 << 20; *bx1 = *by1 = -1;
    if (phase <= 0) return;
    if (phase > 1) phase = 1;
    float ox = cx - CORAL_W / 2 + CORAL_TOP_X * CORAL_CELL + 1, oy = CORAL_FY - CORAL_TOP_Y * CORAL_CELL - 1;
    float len = CORAL_TENT_L * (0.35f + 0.65f * phase);
    /* a fan from ~29 deg left of up to 29 deg right, past the horizontal */
    draw_crown(c, ox, oy, CORAL_TENT, -3.14159f * 0.16f, -3.14159f * 0.84f, len, mix(rgb, 0xffffff, 0.55f), 0, clock, bx0, by0, bx1, by1);
}
/* a low mound of floor stones round a piece's base (2026-09-24): the same
 * pebble tones and position hash as the floor, an ellipse half_w wide whose
 * crest sits 6 px above the base line and whose skirt runs 3 px below it,
 * drawn AFTER the piece so its bottom rows are buried - the coral's trunk
 * and the cluster's rock grow out of the floor instead of standing on it.
 * final: vignetted and untagged here (the scene-cache path). */
static inline uint32_t floor_rgb(int x, int y);
static void draw_floor_mound(ctx_t *c, int cx, int half_w, int rise, bool final) {
    const int fl = TANK_BOT - 14;                                          /* the floor line: the pebbles' highest top */
    for (int y = fl - rise; y <= fl + 4; y++) {
        if (y < 0 || y >= TANK_H) continue;
        float w = ell_half((y - (fl + 2)) / (float)(rise + 2));           /* an ellipse whose crest is `rise` px above the floor line */
        if (w <= 0) continue;
        int half = (int)(half_w * w);
        for (int x = cx - half; x <= cx + half; x++) {
            if (!CTX_IN(c, x, y)) continue;
            uint32_t col = floor_rgb(x, y);
            uint32_t lit = g_sand_style ? mix(col, 0xffffff, 0.35f) : 0x56684f;
            float up = (float)(fl - y) / rise;                           /* 0 at the floor line, 1 at the crest */
            if (up > 0) col = mix(col, lit, 0.25f + 0.40f * up);
            if (up > 0.15f && (chash(x, y) & 15) == 0) col = g_sand_style ? lit : 0x6e7f66;
            uint16_t *p = &CTX_PX(c, x, y);
            *p = rgb565(col, c->dim);
            rec_px(x, y);
            if (final) {
                PK_LIGHT_PX(p, x, y);
                int a = g_vig ? g_vig[y * TANK_W + x] : vig_alpha(x, y);
                if (a) px_darken(p, a);
                if (g_dirty) g_dirty[y * DIRTY_WORDS_PER_ROW + (x >> 5)] &= ~(1u << (x & 31));
            }
        }
    }
}
/* the coral at centre x (its base on the floor), in the keeper's colour.
 * final: vignetted here and untagged (the scene-cache path); otherwise the
 * frame's own sweep does it */
static void draw_coral(ctx_t *c, int cx, uint32_t rgb, float growth, bool final) {
    if (growth > 1) growth = 1;
    int q = (int)(growth * (CORAL_Q - 0.01f));
    if (q != g_coral_q) { coral_build(growth); g_coral_q = q; }
    coral_tint_fill(rgb, c->dim);
    int x0 = cx - CORAL_W / 2, y0 = CORAL_FY - CORAL_H;
    for (int j = 0; j < CORAL_CH; j++)
        for (int i = 0; i < CORAL_CW; i++) {
            uint8_t t = ds()->co_sprite[j][i];
            if (!t) continue;
            for (int dy = 0; dy < CORAL_CELL; dy++) {
                int y = y0 + j * CORAL_CELL + dy;
                uint16_t v = ds()->co_row[t][j * CORAL_CELL + dy];
                for (int dx = 0; dx < CORAL_CELL; dx++) {
                    int x = x0 + i * CORAL_CELL + dx;
                    if (!CTX_IN(c, x, y)) continue;
                    uint16_t *p = &CTX_PX(c, x, y);
                    *p = v;
                    rec_px(x, y);
                    if (final) {
                        PK_LIGHT_PX(p, x, y);
                        int a = g_vig ? g_vig[y * TANK_W + x] : vig_alpha(x, y);
                        if (a) px_darken(p, a);
                        if (g_dirty) g_dirty[y * DIRTY_WORDS_PER_ROW + (x >> 5)] &= ~(1u << (x & 31));
                    }
                }
            }
        }
    draw_floor_mound(c, cx, CORAL_HALF_W - 4, 9, final);              /* anchored: the trunk grows out of a hump of stones */
}

/* ---- the pumpkin lantern, procedural (0.3.6, SD_ITEM_PUMPKIN: the first
 * seasonal piece, in the shop in October; Strato's working-assets/
 * pumpkin-lantern, prototyped 2026-10-04) ----
 * A ribbed orange pumpkin with a crooked olive stem and a carved face - two
 * triangle eyes, a nose, a wide shouting mouth with two teeth - built ONCE
 * as a tone sprite on the coral's 2 px cell grid (PK_CW x PK_CH): five
 * overlapping lobes make the ribbed outline and the grooves between them,
 * the light comes from the upper left (the tank's, not the art's upper
 * right), and a mound of floor stones buries its base.
 * The candle is the point of it. Lights ON: the carved face shows a warm
 * lit inside and a little flame. Lights OFF: the face burns at full
 * brightness through the night's dim, the rind is a touch lit from within,
 * and its light falls on everything near it (the night light, above).
 * Per frame: BEHIND, the body is baked into the scene and only the carved
 * face and the flame are redrawn (~700 px of opaque stores) with one
 * flicker level for the frame - the candle; IN FRONT, the whole sprite
 * (~3.6k px) is drawn live over the grass and the fish. The light itself
 * stays still: a flickering light would be ~20k px of read-modify-write a
 * frame against the 25 fps / 4 s decisions budget, so the face does the
 * flickering. */
enum { PK_NONE = 0, PK_RIM, PK_SHADE, PK_BODY, PK_LIT, PK_GLEAM, PK_GROOVE,
       PK_STEM_D, PK_STEM, PK_STEM_L,
       PK_CUT, PK_CUT_HI, PK_IN0, PK_IN1, PK_IN2, PK_IN3 };      /* PK_CUT.. glow: candle-lit */
#define PK_EMIT  PK_CUT
#define PK_W     (PK_CW * PK_CELL)
#define PK_H     (PK_CH * PK_CELL)
#define PK_FY    (TANK_BOT - 14 + DECOR_SINK)                   /* the base, sunk into the pebbles */
/* the drawing's units (below) per cell: the art is laid
                                                                 * out on a 36 x 34 grid and scaled up to PK_CW x PK_CH */
#define PK_S     1.2f
#define PK_MID   18.0f                                          /* the body's centre column, in drawing units */
#define PK_FLAME_X 18.0f                                        /* the candle on the floor inside, in drawing units */
#define PK_FLAME_Y 3.6f
static const uint32_t PK_RGB[PK_N] = {
    0, 0x8a2406, 0xc2420e, 0xe8641a, 0xf88a2c, 0xffd8a0, 0xa83410,
    0x3c4214, 0x66702a, 0x9aa24a,
    0xf89a36, 0xffc04a, 0x4a1206, 0x8a2a0a, 0xd8601a, 0xffa83a };
static const uint32_t PK_RGB_NIGHT[PK_N] = {                    /* the glow tones with the light off: the candle has the tank to itself */
    [PK_CUT] = 0xffa83a, [PK_CUT_HI] = 0xffd470,
    [PK_IN0] = 0xa8380c, [PK_IN1] = 0xe86818, [PK_IN2] = 0xffa630, [PK_IN3] = 0xffe6a0 };
static int g_pk_cells, g_pk_glow_cells;                         /* the sprite's filled / glowing cells (the sim's numbers) */
/* the five lobes: centre x offset, radii, in cells (y up from the base) */
static const float PK_LOX[5] = { -10.5f, -5.4f, 0.0f, 5.4f, 10.5f };
static const float PK_LRX[5] = { 6.8f, 7.0f, 7.0f, 7.0f, 6.8f };
static const float PK_LRY[5] = { 9.6f, 11.2f, 11.8f, 11.2f, 9.6f };
static bool pk_in_tri(float px, float py, float ax, float ay, float bx, float by, float cx, float cy) {
    float d1 = (px - bx) * (ay - by) - (ax - bx) * (py - by);
    float d2 = (px - cx) * (by - cy) - (bx - cx) * (py - cy);
    float d3 = (px - ax) * (cy - ay) - (cx - ax) * (py - ay);
    bool neg = d1 < 0 || d2 < 0 || d3 < 0, pos = d1 > 0 || d2 > 0 || d3 > 0;
    return !(neg && pos);
}
/* a carved hole at cell point (px, py)? */
static bool pk_hole(float px, float py) {
    if (pk_in_tri(px, py, 8.4f, 14.4f, 15.6f, 14.4f, 13.6f, 20.4f)) return true;    /* the eyes, slanted in */
    if (pk_in_tri(px, py, 20.4f, 14.4f, 27.6f, 14.4f, 22.4f, 20.4f)) return true;
    if (pk_in_tri(px, py, 16.2f, 11.8f, 19.8f, 11.8f, 18.0f, 14.4f)) return true;    /* the nose */
    float ex = (px - PK_MID) / 10.6f, ey = (py - 7.0f) / 4.6f;                       /* the mouth, wide open */
    if (ex * ex + ey * ey > 1 || py > 11.0f) return false;
    if (pk_in_tri(px, py, 11.6f, 11.4f, 13.6f, 11.4f, 12.6f, 8.9f)) return false;    /* its two teeth */
    if (pk_in_tri(px, py, 22.4f, 11.4f, 24.4f, 11.4f, 23.4f, 8.9f)) return false;
    return true;
}
/* the stem: a crooked capsule chain from the crown, leaning left at the top */
static float pk_stem_d(float px, float py, float *side) {
    static const float SX[4] = { 18.6f, 19.2f, 17.8f, 15.0f }, SY[4] = { 21.0f, 26.0f, 30.0f, 32.2f }, SR[3] = { 2.5f, 2.1f, 1.7f };
    float best = 1e9f; *side = 0;
    for (int i = 0; i < 3; i++) {
        float dx = SX[i + 1] - SX[i], dy = SY[i + 1] - SY[i], len2 = dx * dx + dy * dy;
        float u = ((px - SX[i]) * dx + (py - SY[i]) * dy) / len2;
        if (u < 0) u = 0;
        if (u > 1) u = 1;
        float ex = px - (SX[i] + dx * u), ey = py - (SY[i] + dy * u);
        float d = sqrtf(ex * ex + ey * ey) - SR[i];
        if (d < best) { best = d; float l = sqrtf(len2); *side = (ex * dy - ey * dx) / l / SR[i]; }   /* -1 left .. +1 right of the line */
    }
    return best;
}
static void pk_build(void) {
    uint8_t (*sp)[PK_CW] = ds()->pk_sprite;
    uint8_t (*body)[PK_CW] = ds()->pk_body, (*hole)[PK_CW] = ds()->pk_hole;
    for (int j = 0; j < PK_CH; j++)
        for (int i = 0; i < PK_CW; i++) {
            float px = (i + 0.5f) / PK_S - 0.33f, py = ((PK_CH - 1 - j) + 0.5f) / PK_S;
            float e1 = 9, e2 = 9; int k1 = -1;
            for (int k = 0; k < 5; k++) {
                float ex = (px - PK_MID - PK_LOX[k]) / PK_LRX[k], ey = (py - PK_LRY[k] - 0.2f) / PK_LRY[k];
                float e = ex * ex + ey * ey;
                if (e < e1) { e2 = e1; e1 = e; k1 = k; } else if (e < e2) e2 = e;
            }
            body[j][i] = e1 <= 1.0f ? (uint8_t)(k1 + 1) : 0;
            hole[j][i] = body[j][i] && pk_hole(px, py);
            uint8_t t = PK_NONE;
            if (body[j][i]) {
                int k = k1;
                float nx = (px - PK_MID - PK_LOX[k]) / PK_LRX[k], ny = (py - PK_LRY[k] - 0.2f) / PK_LRY[k];
                float lit = -nx * 0.55f + ny * 0.83f - 0.25f * (px - PK_MID) / 18.0f;   /* the right of the whole pumpkin falls off */
                t = lit > 0.50f ? PK_LIT : lit < -0.40f ? PK_SHADE : PK_BODY;
                bool face = py < 21.4f && py > 1.4f && fabsf(px - PK_MID) < 11.2f;   /* the carving: no rib lines across it */
                if (e2 <= 1.0f && e2 - e1 < 0.15f && !face) t = PK_GROOVE;                /* where two ribs meet */
                if (k == 1 && fabsf(nx + 0.52f) < 0.13f && ny > 0.38f && ny < 0.78f) t = PK_GLEAM;   /* a wet streak on the lit rib */
                if (t != PK_GLEAM && !face && (chash(i + 90, j + 90) & 31) == 0) t = t == PK_LIT ? PK_BODY : t == PK_BODY ? PK_SHADE : t;   /* a mottle */
            }
            float side, sd = pk_stem_d(px, py, &side);
            if (sd < 0 && (!body[j][i] || py > 20.0f))
                t = side > 0.35f ? PK_STEM_D : side < -0.30f || (chash(i, j + 7) & 7) == 0 ? PK_STEM_L : PK_STEM;
            sp[j][i] = t;
        }
    /* the outline, the cuts and the inside, from the neighbours */
    for (int j = 0; j < PK_CH; j++)
        for (int i = 0; i < PK_CW; i++) {
            uint8_t t = sp[j][i];
            if (!body[j][i] || (t >= PK_STEM_D && t <= PK_STEM_L)) continue;
            float px = (i + 0.5f) / PK_S - 0.33f, py = ((PK_CH - 1 - j) + 0.5f) / PK_S;
            if (hole[j][i]) {                                     /* the inside: lit by the candle on its floor */
                float dx = fabsf(px - PK_FLAME_X), dy = (py - PK_FLAME_Y) * 0.62f, d = sqrtf(dx * dx + dy * dy);
                if (py < 11.4f)                                   /* the mouth: a lit floor across it, the back wall darker up high */
                    sp[j][i] = py < 4.8f ? (dx < 4.0f ? PK_IN3 : PK_IN2) : py < 7.2f ? (dx < 6.5f ? PK_IN2 : PK_IN1) : py < 9.4f ? PK_IN1 : PK_IN0;
                else sp[j][i] = d < 10.5f ? PK_IN1 : PK_IN0;      /* the eyes and the nose: the lit dome inside */
                continue;
            }
            bool hu = j > 0 && hole[j - 1][i], hd = j < PK_CH - 1 && hole[j + 1][i];
            bool hl = i > 0 && hole[j][i - 1], hr = i < PK_CW - 1 && hole[j][i + 1];
            if (hu || hd || hl || hr) { sp[j][i] = hu ? PK_CUT_HI : PK_CUT; continue; }   /* the carved lip: the one under a hole catches the flame */
            bool ol = i == 0 || !body[j][i - 1], ou = j == 0 || (!body[j - 1][i] && sp[j - 1][i] == PK_NONE);
            bool orr = i == PK_CW - 1 || !body[j][i + 1], od = j == PK_CH - 1 || !body[j + 1][i];
            if (orr || od) sp[j][i] = PK_RIM;
            else if (ol || ou) sp[j][i] = PK_LIT;
        }
    g_pk_cells = g_pk_glow_cells = 0;
    for (int j = 0; j < PK_CH; j++)
        for (int i = 0; i < PK_CW; i++) { g_pk_cells += sp[j][i] != PK_NONE; g_pk_glow_cells += sp[j][i] >= PK_EMIT; }
    g_pk_built = true;
}
int render_pumpkin_px(int *glow) {                               /* its pixels, and how many of them are redrawn a frame (+ the flame's ~22) */
    if (!g_pk_built) pk_build();
    if (glow) *glow = g_pk_glow_cells * PK_CELL * PK_CELL + 22;
    return g_pk_cells * PK_CELL * PK_CELL;
}
/* the rind's rows: hazed with the water like the coral's, dimmed - and at
 * night dimmed less (the shell is lit a little from within) and warmed */
static void pk_tint_fill(float dim) {
    if (g_pk_dim == dim) return;
    bool night = dim < 1.0f;
    float rd = night ? 0.62f : 1.0f;
    for (int r = 0; r < PK_H; r++) {
        int y = PK_FY - PK_H + r;
        uint32_t w = water_rgb(y < 0 ? 0 : y >= TANK_H ? TANK_H - 1 : y);
        for (int k = 1; k < PK_EMIT; k++) {
            uint32_t col = PK_RGB[k];
            if (night && k < PK_STEM_D) col = mix(col, 0xff6a10, 0.18f);
            ds()->pk_row[k][r] = rgb565(mix(w, col, 0.90f), k >= PK_STEM_D ? dim : rd);
        }
    }
    g_pk_dim = dim;
}
/* the candle this frame: a level 0.78..1.0 from a few incommensurate beats,
 * with now and then a deeper gutter */
static float pk_flicker(float clock) {
    float f = 0.90f + 0.045f * fast_sin(clock * 11.3f + 8 * TAU) + 0.035f * fast_sin(clock * 17.9f + 1.3f + 8 * TAU)
                    + 0.025f * fast_sin(clock * 4.7f + 0.4f + 8 * TAU);
    float g = fast_sin(clock * 0.37f + 8 * TAU);
    if (g > 0.93f) f -= (g - 0.93f) * 1.6f;
    return f;
}
/* the pumpkin BEHIND under a castle IN FRONT (2026-10-17, Strato's 1.8: a
 * fish in the arch showed as a patch of castle over the pumpkin): the castle
 * is over it - the bake paints the castle after it, and every frame the face
 * and the flame leave the castle's pixels alone (the castle's silhouette,
 * g_castle_mask, relative to its centre) */
static inline bool castle_covers(int x, int y) {
    if (g_castle_cx < 0) return false;
    int r = y - (CASTLE_FY - CASTLE_ROWS), lx = x - g_castle_cx + CASTLE_MASK_W / 2;
    return (unsigned)r < CASTLE_ROWS + 12 && (unsigned)lx < CASTLE_MASK_W && ((g_castle_mask[r][lx >> 5] >> (lx & 31)) & 1);
}
/* the castle's openings, in the frame's coordinates: the arch (castle_opening's
 * shape) and the four windows (castle_window's: a half-disc vault over a
 * slot), where the light behind it comes through */
static bool castle_arch_at(int cx, int x, int y) {   /* inside the arch's opening of a castle centred at cx */
    int lx = x - cx, ys = CASTLE_FY - CASTLE_ARCH_S;
    if (y > CASTLE_FY || y < ys - CASTLE_ARCH_R) return false;
    float half = y > ys ? CASTLE_ARCH_R : CASTLE_ARCH_R * ell_half((ys - y) / (float)CASTLE_ARCH_R);
    return half >= 1 && lx >= (int)(-half + 0.5f) && lx <= (int)(half - 0.5f);
}
static bool castle_arch(int x, int y) { return castle_arch_at(g_veg_mask_cx, x, y); }   /* (the castle IN FRONT: the grass's arch passes) */
/* the arch's SHADE (2026-10-17, Strato: "anything seen through the arch
 * should appear a bit darker, shadowed"): the gateway is a tunnel, so what
 * shows in it - the water and the floor behind, the grass, a fish passing,
 * the rubble - is darkened, more under the vault than at the foot. A castle
 * BEHIND: in the bake, right after it (what is painted over it later is not
 * in the tunnel); IN FRONT: every frame after the fish, skipping the pieces
 * stacked over the castle. Lighter at night, and none while the lantern
 * behind the castle lights the tunnel - that glow is the point of it. */
static int arch_shade_alpha(int y, bool night) {
    int top = CASTLE_FY - CASTLE_ARCH_S - CASTLE_ARCH_R;
    float t = (float)(y - top) / (float)(CASTLE_ARCH_S + CASTLE_ARCH_R);
    if (t < 0) t = 0;
    if (t > 1) t = 1;
    return night ? (int)(48 - 22 * t) : (int)(105 - 50 * t);
}
static bool pc_covered_above(int k, int x, int y);
static void arch_shade(ctx_t *c, int kc, int cx, bool night, bool over_check) {
    if (night && g_pk_under_castle && g_pk_lx >= 0) return;
    for (int y = CASTLE_FY - CASTLE_ARCH_S - CASTLE_ARCH_R; y <= CASTLE_FY; y++) {
        int a = arch_shade_alpha(y, night);
        for (int x = cx - CASTLE_ARCH_R; x <= cx + CASTLE_ARCH_R; x++) {
            if (!CTX_IN(c, x, y) || !castle_arch_at(cx, x, y)) continue;
            if (over_check && pc_covered_above(kc, x, y)) continue;
            px_darken(&CTX_PX(c, x, y), a);
        }
    }
}
static bool castle_open(int x, int y) {
    int lx = x - g_castle_cx, ys = CASTLE_FY - CASTLE_ARCH_S;
    if (y <= CASTLE_FY && y >= ys - CASTLE_ARCH_R) {                        /* the arch, at the castle's own centre */
        float half = y > ys ? CASTLE_ARCH_R : CASTLE_ARCH_R * ell_half((ys - y) / (float)CASTLE_ARCH_R);
        if (half >= 1 && lx >= (int)(-half + 0.5f) && lx <= (int)(half - 0.5f)) return true;
    }
    static const struct { int lxc, w, top, bot; } W[4] = {                 /* castle_front_row's and the keep's windows */
        { 39, 7, CASTLE_FY - 92, CASTLE_FY - 78 }, { -48, 7, CASTLE_FY - 62, CASTLE_FY - 48 },
        { -73, 6, CASTLE_FY - 30, CASTLE_FY - 19 }, { 71, 7, CASTLE_FY - 44, CASTLE_FY - 30 } };
    for (int k = 0; k < 4; k++) {
        if (y < W[k].top || y > W[k].bot) continue;
        float r = W[k].w / 2.0f, dy = (W[k].top + r) - y, half = dy > 0 ? r * ell_half(dy / r) : r;
        if (lx >= (int)(W[k].lxc - half + 0.5f) && lx <= (int)(W[k].lxc + half - 0.5f)) return true;
    }
    return false;
}
/* the castle's shadow: its stone takes the spill, its openings the whole
 * light - and below its base line the SAND in front of it is dark (Strato
 * 2026-10-17: "light would never reach the sand below / in front of the
 * castle"), its footprint plus a 3 px step of spill at either edge, but for
 * the pool the gate throws on it: the arch's width, fanning out with the
 * distance and leaning away from the lantern's side (the light comes through
 * the gate at a slant), never more than the arch's own width off centre */
static int pk_shade(int x, int y) {
    if (g_pk_under_castle) {
        int lx = x - g_castle_cx;
        if (y > CASTLE_FY) {
            int alx = lx < 0 ? -lx : lx;
            if (alx <= CASTLE_HALF_W + 3) {
                float d = (float)(y - CASTLE_FY), pool = CASTLE_ARCH_R + d * 0.6f;
                float c = -(float)(g_pk_lx - g_castle_cx) * d / 80.0f;
                if (c > CASTLE_ARCH_R) c = CASTLE_ARCH_R;
                if (c < -CASTLE_ARCH_R) c = -CASTLE_ARCH_R;
                if (fabsf(lx - c) <= pool) return 0;              /* the gate's pool: the castle's word */
                return alx > CASTLE_HALF_W ? 2 : 3;
            }
        } else if (castle_covers(x, y)) return castle_open(x, y) ? 0 : 2;
    }
    if (y <= PK_FLOOR_Y) return 0;
    int lvl = 0;                                                  /* the sand: under what stands in front of the lantern */
    for (int k = 0; k < g_pk_cast_n && lvl < 3; k++) {
        if (x < g_pk_cast[k].x0 - 3 || x > g_pk_cast[k].x1 + 3) continue;
        int l = g_pk_cast[k].level;
        if (l == 3 && (x < g_pk_cast[k].x0 || x > g_pk_cast[k].x1)) l = 2;   /* a solid piece's edge: a step of spill */
        if (l > lvl) lvl = l;
    }
    return lvl;
}
/* the floor casts for this frame: a (clamped) span of sand each, in front of
 * the lantern BEHIND. A bed's is the spill, minus the lantern's own patch. */
static void pk_cast_add(int x0, int x1, int level) {
    if (x1 < x0 || g_pk_cast_n >= PK_CAST_MAX) return;
    g_pk_cast[g_pk_cast_n].x0 = (short)x0; g_pk_cast[g_pk_cast_n].x1 = (short)x1; g_pk_cast[g_pk_cast_n].level = (signed char)level; g_pk_cast_n++;
}
static void pk_casts_pieces(const tank_t *t);          /* (with the pieces, below) */
static void pk_casts(const tank_t *t, int pkx) {
    g_pk_cast_n = 0;
    int px0 = pkx - PUMPKIN_HALF_W, px1 = pkx + PUMPKIN_HALF_W;      /* the lantern's own patch of sand: lit, a 5 px half step either side */
    bool grass_above = g_pk_k >= 0 && tank_decor_z(t, 8) == DECOR_Z_BACK;   /* the grass beds stand in front of a lantern BEHIND */
    for (int b = 0; b < tank_veg_beds(t); b++) {
        if (b == 3 ? !tank_decor_above(t, 0, 8) : !grass_above) continue;    /* (the sword plant: by its own rank) */
        float x0, x1; tank_veg_bed(t, b, &x0, &x1, NULL, NULL);
        int a = (int)x0, z = (int)x1;
        if (z < px0 - 5 || a > px1 + 5) { pk_cast_add(a, z, 2); continue; }
        if (a < px0 - 5) pk_cast_add(a, px0 - 6, 2);
        pk_cast_add(px0 - 5 > a ? px0 - 5 : a, px0 - 1, 1);
        pk_cast_add(px1 + 1, px1 + 5 < z ? px1 + 5 : z, 1);
        if (z > px1 + 5) pk_cast_add(px1 + 6, z, 2);
    }
    pk_casts_pieces(t);
}
static inline void pk_store(ctx_t *c, int x, int y, uint16_t v, bool final) {
    if (!CTX_IN(c, x, y)) return;
    if (g_pk_k >= 0 && pc_covered_above(g_pk_k, x, y)) return;   /* under a piece above it (the castle's stone, a coral IN FRONT, ...) */
    uint16_t *p = &CTX_PX(c, x, y);
    rec_px(x, y);
    *p = v;
    if (final) {
        static int ky = -1, kx = -1, ka;       /* one vignette alpha per 8 px of a row, computed (the PSRAM LUT costs a cache line a read) */
        if (y != ky || (x >> 3) != kx) { ky = y; kx = x >> 3; ka = vig_alpha((x & ~7) + 4, y); }
        if (ka) px_darken(p, ka);
        if (g_dirty) g_dirty[y * DIRTY_WORDS_PER_ROW + (x >> 5)] &= ~(1u << (x & 31));
    } else dirty_px(x, y);
}
/* the glowing part: the carved face's cells and the flame. Every frame,
 * over the baked body (and in the bake, at the steady level). night: the
 * candle's own tones, which ignore the night's dim */
static void draw_pumpkin_glow(ctx_t *c, int cx, float clock, bool night, bool steady, bool final) {
    if (!g_pk_built) pk_build();
    float f = steady ? 0.92f : pk_flicker(clock);
    uint16_t v[PK_N];
    for (int k = PK_EMIT; k < PK_N; k++) {
        uint32_t col = night ? PK_RGB_NIGHT[k] : PK_RGB[k];
        float lv = f * (k == PK_IN3 ? 1.04f : 1.0f);
        if (!night && k == PK_CUT) lv = 1.0f;                     /* by day the lip is mostly daylight */
        v[k] = rgb565(col, lv > 1 ? 1 : lv);
    }
    int x0 = cx - PK_W / 2, y0 = PK_FY - PK_H;
    for (int j = PK_CH - (int)(22 * PK_S); j < PK_CH; j++)       /* the face's rows only: the stem and the crown carry none */
        for (int i = (int)(6 * PK_S); i < PK_CW - (int)(6 * PK_S); i++) {
            uint8_t t = ds()->pk_sprite[j][i];
            if (t < PK_EMIT) continue;
            for (int dy = 0; dy < PK_CELL; dy++)
                for (int dx = 0; dx < PK_CELL; dx++) pk_store(c, x0 + i * PK_CELL + dx, y0 + j * PK_CELL + dy, v[t], final);
        }
    /* the flame: a small teardrop on a stub of candle, leaning with the flicker */
    int fx = x0 + (int)((PK_FLAME_X + 0.33f) * PK_S * PK_CELL), fy = PK_FY - (int)(PK_FLAME_Y * PK_S * PK_CELL);
    uint16_t wax = rgb565(night ? 0xf8e2b0 : 0xe8d4a8, f), core = rgb565(0xfffbe8, 1.0f),
             mid = rgb565(0xffd860, f > 0.95f ? 1.0f : f + 0.05f), tip = rgb565(0xff9a28, f);
    for (int y = fy - 1; y <= fy + 2; y++) for (int x = fx - 2; x <= fx + 1; x++) pk_store(c, x, y, wax, final);
    int h = steady ? 6 : 5 + (f > 0.93f) + (f > 0.86f);
    int lean = steady ? 0 : (int)floorf(1.4f * fast_sin(clock * 6.1f + 8 * TAU) + 0.5f);
    for (int k = 1; k <= h; k++) {
        int y = fy - 1 - k, x = fx - 1 + (k > h / 2 ? lean : 0);
        uint16_t a = k <= 2 ? core : k <= h - 2 ? mid : tip;
        pk_store(c, x, y, a, final);
        if (k <= h - 2) pk_store(c, x + 1, y, k <= 2 ? mid : tip, final);
    }
}
/* the light over the water and the floor, in the bake: the pixels already
 * carry the porthole vignette, so the light is darkened by it too, and it
 * is dithered so its falloff never bands. (The decor painted after it lights
 * itself - PK_LIGHT_PX in its final paint.) */
static void pk_halo(uint16_t *sc) {
    static const uint8_t bayer[4][4] = { {0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5} };
    uint8_t rgb[128][3];                           /* the light in 8 bits, for the dither */
    for (int i = 0; i < 128; i++) { int r, g, b; pk_rgb8(i, &r, &g, &b); rgb[i][0] = (uint8_t)r; rgb[i][1] = (uint8_t)g; rgb[i][2] = (uint8_t)b; }
    for (int y = g_pk_ly - PK_HALO_RY + 1; y < g_pk_ly + PK_HALO_RY; y++)
        for (int x = g_pk_lx - PK_HALO_R + 1; x < g_pk_lx + PK_HALO_R; x++) {
            if ((unsigned)x >= (unsigned)TANK_W || (unsigned)y >= (unsigned)TANK_H) continue;
            int i = pk_light_i(x, y);
            if (i < 0) continue;
            int inv = 256 - (g_vig ? g_vig[y * TANK_W + x] : vig_alpha(x, y));
            if (g_pk_shadows) { int sh = pk_shade(x, y); if (sh == 3) continue; if (sh == 2) inv = (int)(inv * PK_SPILL); else if (sh == 1) inv = (int)(inv * PK_HALF); }   /* in a shadow */
            int lr = rgb[i][0] * inv >> 8, lg = rgb[i][1] * inv >> 8, lb = rgb[i][2] * inv >> 8;
            uint16_t *p = &sc[y * TANK_W + x];
            int d = bayer[y & 3][x & 3];
            int rr = (((*p >> 11) & 31) << 3) + 4 + lr, gg = (((*p >> 5) & 63) << 2) + 2 + lg, bb = ((*p & 31) << 3) + 4 + lb;
            int r5 = (rr + (d >> 1) - 4) >> 3, g6 = (gg + (d >> 2) - 2) >> 2, b5 = (bb + (d >> 1) - 4) >> 3;
            if (r5 > 31) r5 = 31;
            if (g6 > 63) g6 = 63;
            if (b5 > 31) b5 = 31;
            *p = (uint16_t)((r5 << 11) | (g6 << 5) | b5);
        }
}
/* every frame, before the vignette sweep: what was painted live in the
 * light's box and is still marked (fish, critters, food, bubbles, crowns)
 * takes the light; the sweep then vignettes it as before */
static void pk_light_dirty(ctx_t *c) {
    int y0 = g_pk_ly - PK_HALO_RY + 1, y1 = g_pk_ly + PK_HALO_RY - 1, x0 = g_pk_lx - PK_HALO_R + 1, x1 = g_pk_lx + PK_HALO_R - 1;
    if (y0 < 0) y0 = 0;
    if (y1 > TANK_H - 1) y1 = TANK_H - 1;
    if (x0 < 0) x0 = 0;
    if (x1 > TANK_W - 1) x1 = TANK_W - 1;
    for (int y = y0; y <= y1; y++) {
        const uint32_t *drow = g_dirty + y * DIRTY_WORDS_PER_ROW;
        for (int w = x0 >> 5; w <= x1 >> 5; w++) {
            uint32_t bits = drow[w];
            while (bits) {
                int x = (w << 5) + __builtin_ctz(bits); bits &= bits - 1;
                if (x < x0 || x > x1) continue;
                uint32_t l = pk_light_at(x, y);
                if (l) { uint16_t *p = &c->fb[y * c->stride + x]; *p = px_lit(*p, l); }
            }
        }
    }
}
/* the lantern at centre x: the rind, the face (steady in the bake, the
 * candle's flicker live), and with mound the floor stones round its base
 * (lit by the light while it is on) */
static void draw_pumpkin(ctx_t *c, int cx, bool night, float clock, bool steady, bool mound, bool final) {
    if (!g_pk_built) pk_build();
    pk_tint_fill(c->dim);
    int x0 = cx - PK_W / 2, y0 = PK_FY - PK_H;
    for (int j = 0; j < PK_CH; j++)
        for (int i = 0; i < PK_CW; i++) {
            uint8_t t = ds()->pk_sprite[j][i];
            if (!t || t >= PK_EMIT) continue;
            for (int dy = 0; dy < PK_CELL; dy++) {
                uint16_t v = ds()->pk_row[t][j * PK_CELL + dy];
                for (int dx = 0; dx < PK_CELL; dx++) pk_store(c, x0 + i * PK_CELL + dx, y0 + j * PK_CELL + dy, v, final);
            }
        }
    draw_pumpkin_glow(c, cx, clock, night, steady, final);
    if (mound) draw_floor_mound(c, cx, PK_W / 2 - 4, 5, final);
}

/* ---- the snail, procedural (2026-09-16; Strato: "explore doing the same
 * for the snail so the aesthetic matches") ----
 * Drawn the castle's way in place of the two sprites: a little geometry with
 * a per-pixel tone rule, the castle's position hash for the foot's mottle,
 * golden light from the upper left (the lower right of the shell falls into
 * the dark tones, the rim shades, a wet gleam sits high on the left), mixed
 * with the water of its row like the fronds - a touch stronger on the pane
 * than on the floor, so the glass snail reads closer - and no outline. And
 * it moves (Strato: "the fish animations are so nice, maybe you can add a
 * subtle animation"): PEDAL WAVES run along the foot, head-ward on the glass
 * and back along the belly upright, the eye stalks sway against each other,
 * and the shell heaves a pixel with the wave.
 * UPRIGHT faces +x, the origin at the sole under the shell (snail_y +
 * SNAIL_SOLE_DY), mirrored to the heading; ~34 x 17 px. ON THE GLASS the
 * head points +y from the foot's centre, turned to the heading like the
 * sprite was; ~22 x 25 px. The tone rule runs over ~750 px per pose. */
enum { SN_S0, SN_S1, SN_S2, SN_S3, SN_S4, SN_F0, SN_F1, SN_F2, SN_F3, SN_TIP, SN_EYE, SN_NONE };
static const uint32_t SNAIL_RGB[SN_NONE] = {
    0xf8c868, 0xe6953c, 0xc4641e, 0x8c3d16, 0x4a2010,      /* the shell: lit .. the groove */
    0xf2ebe4, 0xd8cec6, 0xb3a7a6, 0x847a80,                /* the foot: lit .. its edge */
    0x1a1c34, 0x0a0c18 };                                  /* stalk tips; the eye and the mouth */
#define SNAIL_SOLE_DY   6.0f                               /* the sole below snail_y (the sprite's centre) */
#define SNAIL_TINT_FLOOR 0.84f
#define SNAIL_TINT_GLASS 0.90f
static inline int sn_clamp(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
/* within w of the segment (x0,y0)-(x1,y1); *u = 0..1 along it */
static inline bool sn_seg(float lx, float ly, float x0, float y0, float x1, float y1, float w, float *u) {
    float vx = x1 - x0, vy = y1 - y0, l2 = vx * vx + vy * vy;
    float t = ((lx - x0) * vx + (ly - y0) * vy) / l2;
    t = t < 0 ? 0 : t > 1 ? 1 : t;
    float ex = lx - (x0 + t * vx), ey = ly - (y0 + t * vy);
    *u = t;
    return ex * ex + ey * ey < w * w;
}
/* the shell's tone from its lighting terms: band = the whorl's lit / mid /
 * groove, lit = -1..1 (upper left .. lower right), rim = on the edge */
static inline int sn_shell_tone(int band, float lit, bool rim) {
    int tone = band;
    if (lit > 0.35f) tone--; else if (lit < -0.15f) tone++;
    if (lit < -0.55f) tone++;
    if (rim) tone = lit < 0.25f ? (tone > 3 ? tone : 3) : (tone < 1 ? tone : 1);
    return tone;
}
/* UPRIGHT: the tone at local (lx, ly), or SN_NONE. top = the foot's ridge at
 * this column (computed once per column by the caller) */
typedef struct { float heave, sw1, sw2; } sn_sway_t;       /* this frame's heave and stalk sways: once, not per pixel (2026-10-03) */
static int snail_upright_tone(int lx, int ly, float top, float clock, const sn_sway_t *sw) {
    float heave = sw->heave;
    float sw1 = sw->sw1, sw2 = sw->sw2;
    float u;
    /* the eye stalks, swaying against each other */
    if (sn_seg(lx, ly, 12.5f, -7.5f, 17.5f + sw1, -13.5f, 0.7f, &u)) return u > 0.78f ? SN_TIP : SN_F1;
    if (sn_seg(lx, ly, 13.5f, -6.5f, 18.5f + sw2, -9.5f, 0.7f, &u)) return u > 0.78f ? SN_TIP : SN_F1;
    /* the shell: a disc, the whorl a spiral about an apex left of centre */
    const float cx = -3.5f, cy = -9.5f + heave, R = 8.6f;
    float dx = lx - cx, dy = ly - cy;
    float r = sqrtf(dx * dx + dy * dy * 1.0816f);
    if (r <= R) {
        float ax = lx - (cx - 1.5f), ay = ly - (cy + 0.5f);
        float ar = sqrtf(ax * ax + ay * ay), th = atan2f(ay, ax);
        const float pitch = 2.25f;                                 /* ~4 turns: Strato asked for one more groove */
        float w = fmodf(ar - th / TAU * pitch, pitch); if (w < 0) w += pitch; w /= pitch;
        int tone = sn_shell_tone(w < 0.5f ? 1 : w < 0.74f ? 2 : 3, (-dx * 0.55f - dy * 0.83f) / R, r > R - 1.3f);
        if (ar < 2.2f) tone = 3;                                   /* the apex whorl is tight and dark */
        { float gx = (dx + 3.4f) * 0.8f, gy = dy + 4.6f; if (gx * gx + gy * gy < 4.0f) tone = 0; }   /* a wet gleam */
        return SN_S0 + sn_clamp(tone, 0, 4);
    }
    /* the foot: a low lens under the shell, the head rising at the front */
    if (lx >= -15 && lx <= 13 && ly >= top && ly <= 0) {
        int tone = 1;
        if (ly < top + 1.4f) tone = 0;                             /* the lit ridge */
        if (ly > -0.9f) tone = 2;                                  /* the belly line */
        if (r < R + 1.8f && dy > 0 && tone < 3) tone++;            /* the shell's shadow */
        if (ly > -2.2f && tone <= 1 && fast_sin(lx * 1.1f + clock * 3.0f + TAU) > 0.7f) tone = 0;   /* a pedal wave runs back */
        if ((chash(lx, ly) & 7) == 0) tone = sn_clamp(tone + 1, 0, 3);
        return SN_F0 + tone;
    }
    /* the head */
    { float hx = (lx - 11.5f) * 1.05f, hy = ly + 5.2f;
      if (hx * hx + hy * hy <= 9.0f) {
          float ex = lx - 12.6f, ey = ly + 5.6f;
          if (ex * ex + ey * ey < 0.64f) return SN_EYE;
          return (-(lx - 11.5f) * 0.5f - hy) > 0.9f ? SN_F0 : SN_F1;
      } }
    return SN_NONE;
}
/* ON THE GLASS: local (lx, ly) is turned to the heading; (sx, sy) is the
 * screen offset, which the light and the shadow side follow (the light
 * stays upper-left however it crawls) */
/* (the body under the stalks: the foot, whose pedal waves move with the
 * clock - *foot says so - or the shell, which depends on the place alone) */
static int snail_glass_body(float lx, float ly, float sx, float sy, float clock, bool *foot) {
    *foot = false;
    float lit = (-sx * 0.55f - sy * 0.83f) / 11.0f;
    /* the foot: an egg, broad under the shell, narrowing to the head */
    float ey = ly / 11.0f;
    if (ey >= -1 && ey <= 1) {
        float rx = 6.6f * sqrtf(1 - ey * ey) * (1 - 0.18f * ey);
        float alx = lx < 0 ? -lx : lx;
        if (rx > 0.3f && alx <= rx) {
            *foot = true;
            int tone = 1;
            if (fast_sin(ly * 0.75f - clock * 2.6f + 64 * TAU) > 0.6f) tone = 0;   /* pedal waves crawl head-ward */
            float edge = alx / rx;
            if (edge > 0.8f) tone = 2;                             /* the foot's edge */
            else if (edge > 0.62f && lit < 0 && tone < 1) tone = 1; /* the shadow side falls off */
            if (alx < 1.2f && ly > 2 && tone < 2) tone = 2;        /* the pedal groove */
            if (ly < -5 && edge < 0.5f && tone > 1) tone = 1;
            if ((chash((int)floorf(lx), (int)floorf(ly)) & 7) == 0) tone = sn_clamp(tone + 1, 0, 3);
            if (alx < 1.5f && ly >= 7.4f && ly <= 8.8f) return SN_EYE;   /* the mouth */
            return SN_F0 + tone;
        }
    }
    /* the shell behind: a crescent of whorl lobes round the top and sides */
    float qx = lx / 11.0f, qy = (ly + 2.5f) / 10.6f;
    float sr = sqrtf(qx * qx + qy * qy);
    if (sr <= 1 && ly < 6.5f) {
        float lobe = fast_sin(atan2f(ly + 2.5f, lx) * 5.0f - 0.3f + 8 * TAU);
        int tone = sn_shell_tone(lobe > 0.15f ? 1 : 2, lit, sr > 0.88f);
        if (sr < 0.78f) tone++;                                    /* shadow where the foot meets it */
        { float gx = sx + 5.0f, gy = sy + 7.8f; if (gx * gx + gy * gy < 2.9f) tone = 0; }   /* gleam */
        return SN_S0 + sn_clamp(tone, 0, 4);
    }
    return SN_NONE;
}
/* stalk_y[0] / [1]: the left and right stalk's tip height this frame */
static int snail_glass_tone(float lx, float ly, float sx, float sy, float clock, const float *stalk_y) {
    float u;
    for (int sgn = -1; sgn <= 1; sgn += 2)                        /* the stalks: stubs by the head */
        if (sn_seg(lx, ly, sgn * 2.2f, 10.0f, sgn * 3.6f, stalk_y[sgn > 0], 0.6f, &u))
            return u > 0.7f ? SN_TIP : SN_F2;
    bool foot;
    return snail_glass_body(lx, ly, sx, sy, clock, &foot);
}
/* a row of snail pixels: the water of the row and each tone's colour over it
 * are worked out once per row and tone, not per pixel */
typedef struct { int y; uint32_t w; uint16_t have; src_t s[SN_NONE]; } sn_row_t;
static inline void snail_put(ctx_t *c, int x, int y, int tone, float tint, sn_row_t *r) {
    if (tone == SN_NONE || !CTX_IN(c, x, y)) return;
    if (r->y != y) { r->y = y; r->have = 0; r->w = water_rgb(y < 0 ? 0 : y >= TANK_H ? TANK_H - 1 : y); }
    if (!(r->have >> tone & 1)) { r->s[tone] = src_color(mix(r->w, SNAIL_RGB[tone], tint), c->dim); r->have |= (uint16_t)(1u << tone); }
    px_blend_s(c, x, y, &r->s[tone], 255);
}
/* ON THE GLASS the shell and the empty corners depend only on the heading:
 * kept per pixel of the 35 x 35 box until the snail turns (the decor
 * scratch, PSRAM). SNG_DYN = the foot, or within reach of a stalk: asked
 * every frame. 2026-10-03: on the glass it cost the 1.8 ~4.5 ms a frame - a
 * sqrtf and an atan2f for every shell pixel, two divides and two sines for
 * every pixel of the box. */
#define SNG_N    35
#define SNG_NONE 0xff
#define SNG_DYN  0xfe
static float g_sng_heading = 1e9f;                     /* the heading sn_glass was worked out for */
/* UPRIGHT on the floor - a side view, mirrored to face the way it walks,
 * drawn in the scene with the fish (vignetted, behind the front fronds) -
 * or flat ON THE GLASS, its underside to the viewer, drawn after the algae
 * like the film itself (it is on the pane). */
static void draw_snail(ctx_t *c, const tank_t *t, bool upright_pass) {
    if (!(t->sd_unlocks & SD_ITEM_SNAIL) || t->snail_x < 0) return;
    bool upright = tank_snail_upright(t);
    if (upright != upright_pass) return;
    int ox = (int)t->snail_x, oy = (int)(t->snail_y + SNAIL_SOLE_DY);
    sn_row_t row = { .y = -(1 << 20) };
    if (upright) {
        bool flip = cosf(t->snail_heading) < 0;
        sn_sway_t sw = { 0.6f * fast_sin(t->clock * 2.4f), fast_sin(t->clock * 1.7f), fast_sin(t->clock * 1.7f + 2.2f) };
        float top[37];                                             /* the foot's ridge per column, then row by row (one water colour a row) */
        for (int lx = -16; lx <= 20; lx++) {
            float xf = (lx + 15) / 28.5f;
            top[lx + 16] = -(2.4f + 3.2f * powf(sinf(xf < 0 ? 0 : xf > 1 ? 1 : xf * 3.14159f), 0.6f));
            if (lx > 8) { float h = -(4.8f + (lx - 8) * 0.55f); if (h < top[lx + 16]) top[lx + 16] = h; }
        }
        for (int ly = -18; ly <= 0; ly++)
            for (int lx = -16; lx <= 20; lx++)
                snail_put(c, flip ? ox - lx : ox + lx, oy + ly, snail_upright_tone(lx, ly, top[lx + 16], t->clock, &sw), SNAIL_TINT_FLOOR, &row);
    } else {
        float a = t->snail_heading - 1.5708f;                      /* the head points +y locally */
        float ca = cosf(a), sa = sinf(a);
        int cx = (int)t->snail_x, cy = (int)t->snail_y;
        float stalk_y[2] = { 11.6f + 0.4f * fast_sin(t->clock * 2 + -1 + TAU), 11.6f + 0.4f * fast_sin(t->clock * 2 + 1 + TAU) };
        uint8_t *cls = ds()->sn_glass;
        if (g_sng_heading != t->snail_heading) {                   /* it turned: what the heading alone decides, again */
            g_sng_heading = t->snail_heading;
            for (int dy = -17; dy <= 17; dy++)
                for (int dx = -17; dx <= 17; dx++) {
                    float lx = ca * dx + sa * dy, ly = -sa * dx + ca * dy;
                    float alx = lx < 0 ? -lx : lx;
                    bool foot = false; int tone = SNG_DYN;
                    if (!(alx >= 1.5f && alx <= 4.3f && ly >= 9.3f && ly <= 12.7f))   /* out of both stalks' reach, wherever they sway */
                        tone = snail_glass_body(lx, ly, (float)dx, (float)dy, 0, &foot);
                    cls[(dy + 17) * SNG_N + dx + 17] = foot ? SNG_DYN : tone == SN_NONE ? SNG_NONE : (uint8_t)tone;
                }
        }
        for (int dy = -17; dy <= 17; dy++)
            for (int dx = -17; dx <= 17; dx++) {
                int k = cls[(dy + 17) * SNG_N + dx + 17];
                if (k == SNG_NONE) continue;
                if (k == SNG_DYN) {
                    float lx = ca * dx + sa * dy, ly = -sa * dx + ca * dy;   /* inverse rotation */
                    k = snail_glass_tone(lx, ly, (float)dx, (float)dy, t->clock, stalk_y);
                }
                snail_put(c, cx + dx, cy + dy, k, SNAIL_TINT_GLASS, &row);
            }
    }
}
/* ---- the urchin, procedural (2026-10-02, SD_ITEM_URCHIN) ----
 * After Strato's sea-urchin-v1 (working-assets): a domed purple urchin seen
 * a little from above, a crown of short pointed spines with pale tips, warm
 * light from the upper left, no outline. Drawn the snail's way - a tone rule
 * per pixel over a ~37 x 24 box, mixed with the water of its row - in his six
 * tones. The spines are its life: they wave slowly, each on its own beat,
 * and work faster while it chews. The dome's centre is (urchin_x,
 * URCHIN_FLOOR_Y); its flat base sits on the root line, UR_BASE below. */
enum { UR_T0, UR_T1, UR_T2, UR_T3, UR_T4, UR_T5, UR_NONE };
static const uint32_t URCHIN_RGB[UR_NONE] = { 0x320746, 0x66107d, 0x9732b1, 0xbe53bc, 0xf88ab8, 0xfff0d3 };
#define UR_SPINES 11
#define UR_FACE    5
#define UR_RX     10.0f
#define UR_RY     8.0f
#define UR_BASE   7.0f
#define UR_TINT   0.93f
typedef struct { float dx[UR_SPINES + UR_FACE], dy[UR_SPINES + UR_FACE], len[UR_SPINES + UR_FACE];
                 float rim[UR_SPINES], end[UR_SPINES], den[UR_SPINES]; } ur_pose_t;   /* per crown spine: where it leaves the dome, its tip, the wedge's taper -
                                                                                        once a frame here, not per pixel (2026-10-03: a sqrtf and two
                                                                                        divides per spine per pixel cost the 1.8 milliseconds a frame) */
/* the spines this frame: a fan from just under the left side, over the top,
 * to just under the right (the floor hides the rest), long and short in
 * turn; then a few short ones on the dome's face, pointing at the viewer
 * and down - the pale streaks of the art */
static void urchin_pose(ur_pose_t *p, float clock, bool chewing) {
    float amp = chewing ? 0.13f : 0.07f, sp = chewing ? 2.6f : 0.8f;
    for (int k = 0; k < UR_SPINES; k++) {
        float a = -3.14159f - 0.32f + k * (3.14159f + 0.64f) / (UR_SPINES - 1);
        a += amp * fast_sin(clock * sp + k * 1.7f + 8 * TAU);
        p->dx[k] = cosf(a); p->dy[k] = sinf(a);
        p->len[k] = (k & 1) ? 4.0f : 6.0f;
        p->rim[k] = 1.0f / sqrtf(p->dx[k] * p->dx[k] / (UR_RX * UR_RX) + p->dy[k] * p->dy[k] / (UR_RY * UR_RY));
        p->end[k] = p->rim[k] + p->len[k];
        p->den[k] = p->end[k] - p->rim[k] + 2;
    }
    static const float FACE_A[UR_FACE] = { -2.4f, -1.9f, 2.2f, 1.2f, 0.5f };
    for (int k = 0; k < UR_FACE; k++) {
        float a = FACE_A[k] + 0.6f * amp * fast_sin(clock * sp + k * 2.3f + 8 * TAU);
        p->dx[UR_SPINES + k] = cosf(a); p->dy[UR_SPINES + k] = sinf(a);
        p->len[UR_SPINES + k] = 4.5f;
    }
}
/* the tone at local (lx, ly) from the dome's centre, or UR_NONE */
static int urchin_tone(float lx, float ly, const ur_pose_t *p) {
    if (ly > UR_BASE) return UR_NONE;
    float lit = (-lx * 0.55f - ly * 0.83f) / UR_RX;                /* -1..1: lower right .. upper left */
    float ex = lx / UR_RX, ey = ly / UR_RY, er = ex * ex + ey * ey;
    /* the face spines first: they stand over the dome */
    for (int k = UR_SPINES; k < UR_SPINES + UR_FACE; k++) {
        static const float FX[UR_FACE] = { -4.5f, 0.5f, -5.0f, 2.0f, 5.5f }, FY[UR_FACE] = { -2.5f, -4.5f, 3.0f, 2.5f, 0.0f };
        float vx = lx - FX[k - UR_SPINES], vy = ly - FY[k - UR_SPINES];
        float u = vx * p->dx[k] + vy * p->dy[k], w = vx * p->dy[k] - vy * p->dx[k];
        if (u < 0 || u > p->len[k]) continue;
        float half = 1.6f * (1 - u / p->len[k]) + 0.35f;
        if (w * w > half * half) continue;
        return u > p->len[k] - 1.6f ? UR_T5 : lit > 0 ? UR_T4 : UR_T3;
    }
    if (er <= 1.0f) {                                               /* the dome */
        int tone = 2;
        if (lit > 0.15f) tone = 3;
        if (lit > 0.55f && er > 0.20f) tone = 4;
        if (lit < -0.30f) tone = 1;
        if (lit < -0.75f || ly > UR_BASE - 1.2f) tone = 0;
        int ix = (int)floorf(lx), iy = (int)floorf(ly);
        uint32_t h = chash(ix + 40, iy + 40);
        if ((h & 7) == 0) tone = tone > 0 ? tone - 1 : 0;           /* the mottle */
        else if ((h & 15) == 1 && tone < 4) tone++;
        return UR_T0 + tone;
    }
    /* the crown: a spine is a wedge from inside the dome out to a fine point */
    for (int k = 0; k < UR_SPINES; k++) {
        float u = lx * p->dx[k] + ly * p->dy[k], w = lx * p->dy[k] - ly * p->dx[k];
        float rim = p->rim[k], end = p->end[k];
        if (u < rim - 2 || u > end) continue;
        float half = 2.6f * (end - u) / p->den[k] + 0.3f;
        if (w * w > half * half) continue;
        float slit = -p->dx[k] * 0.55f - p->dy[k] * 0.83f;          /* the spine's own side of the light */
        if (u > end - 1.8f) return slit > -0.5f ? UR_T5 : UR_T4;    /* pale tips */
        if (u > end - 3.8f) return slit > -0.3f ? UR_T4 : UR_T3;
        return slit > 0.2f ? UR_T3 : slit > -0.5f ? UR_T2 : UR_T1;
    }
    return UR_NONE;
}
static void draw_urchin(ctx_t *c, const tank_t *t) {
    if (!(t->sd_unlocks & SD_ITEM_URCHIN) || t->urchin_x < 0) return;
    ur_pose_t p; urchin_pose(&p, t->clock, tank_urchin_chewing(t));
    int cx = (int)floorf(t->urchin_x + 0.5f), cy = (int)URCHIN_FLOOR_Y;
    for (int ly = -16; ly <= (int)UR_BASE; ly++) {
        int y = cy + ly;
        uint32_t w = water_rgb(y < 0 ? 0 : y >= TANK_H ? TANK_H - 1 : y);
        for (int lx = -18; lx <= 18; lx++) {
            int tone = urchin_tone((float)lx + 0.5f, (float)ly + 0.5f, &p);
            if (tone == UR_NONE || !CTX_IN(c, cx + lx, y)) continue;
            px_blend(c, cx + lx, y, mix(w, URCHIN_RGB[tone], UR_TINT), 255);
        }
    }
}

/* ---- the seahorse (0.3.6; the prototype is working-assets/seahorse/
 * seahorse_proto.py, built from Strato's sprite - the local frame IS that
 * sprite's 23 x 39 grid, facing right, 1 cell = 1 px; the same geometry +
 * tone rule as the snail and the castle, lit from the upper left, grain
 * from a position hash, tinted with the row's water). The tail is a sampled
 * spine - a lead-in and a spiral - stamped into a distance map, so its rings
 * and edges come from the nearest sample instead of a search per pixel. Round
 * a blade the coil is centred on the blade's spine and the upper half of
 * every loop is BEHIND it: those pixels are skipped where they land on the
 * ribbon, so the blade shows through the coil, and a contact shadow sits on
 * the ribbon under each front crossing. That is the wrap (Strato: "it's
 * important we get the tail-wrapping and anchoring right"). */
#define SH_W 23
#define SH_H 39
#define SH_PTS_MAX 140
enum { SH_FREE = 0, SH_ONBLADE = 1, SH_GRIP = 2 };   /* the tail's pose: the open curl, round a blade, the tight grip */
enum { SHT_0, SHT_1, SHT_2, SHT_3, SHT_4, SHT_5, SHT_EYE, SHT_GLINT, SHT_FINM, SHT_FINR, SHT_N };
static const uint32_t SH_RGB[SHT_N]   = { 0xfff1b8, 0xfcd055, 0xfa9b19, 0xf3640e, 0xcb2f15, 0x8e1e0b, 0x1b3a63, 0xe4ecff, 0xffd98a, 0xf3640e };
static const uint8_t  SH_ALPHA[SHT_N] = { 255, 255, 255, 255, 255, 255, 255, 255, 150, 190 };
#define SH_TINT 0.82f
#define SHB_ON     0x80        /* a sprite byte: bits 0-3 the tone, bit 4 behind the blade, bit 5 the tail, bit 7 a pixel at all */
#define SHB_BEHIND 0x10
#define SHB_TAIL   0x20
static inline float sh_seg(float px, float py, float x0, float y0, float x1, float y1, float *u) {
    float vx = x1 - x0, vy = y1 - y0, L2 = vx * vx + vy * vy; if (L2 < 1e-6f) L2 = 1e-6f;
    float uu = ((px - x0) * vx + (py - y0) * vy) / L2; uu = uu < 0 ? 0 : uu > 1 ? 1 : uu;
    float dx = px - (x0 + uu * vx), dy = py - (y0 + uu * vy);
    if (u) *u = uu;
    return sqrtf(dx * dx + dy * dy);
}
static inline float sh_tail_hw(float frac) { return 2.4f - 1.8f * powf(frac, 0.8f); }
static inline int sh_clamp(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
/* the tail's spine: a bezier lead-in from the body's base and a spiral; on a
 * blade the spiral is centred on the blade (local x SX, half-width SW) and
 * the upper half of each loop is flagged behind it */
/* the three poses' spirals (the coil's centre, radii, turns, squash, the
 * lead-in's control point); a hold is reached by LERPING the open curl's
 * numbers into the hold's over u = 0..1 (tank_t.sh_land / SH_LAND_S), so
 * the tail closes round a blade or a finger and opens again instead of
 * snapping between two drawings (Strato 10-15) */
typedef struct { float cx, cy, r0, r1, turns, ky, c0x, c0y; } sh_spiral_t;
static void sh_spiral(sh_spiral_t *o, int pose, float SX, float SW) {
    if (pose == SH_ONBLADE)   { o->cx = SX; o->cy = 33.4f; o->r0 = SW + 3.2f; o->r1 = SW + 0.5f; o->turns = 2.05f; o->ky = 0.86f; o->c0x = SX - o->r0 - 0.4f; o->c0y = 29.2f; }
    else if (pose == SH_GRIP) { o->cx = 10.5f; o->cy = 32.6f; o->r0 = 4.6f; o->r1 = 0.9f; o->turns = 2.1f; o->ky = 0.9f; o->c0x = 5.0f; o->c0y = 29.4f; }
    else                      { o->cx = 11.0f; o->cy = 33.2f; o->r0 = 6.2f; o->r1 = 1.3f; o->turns = 1.55f; o->ky = 0.78f; o->c0x = 4.8f; o->c0y = 29.6f; }
}
static inline float sh_lerp(float a, float b, float u) { return a + (b - a) * u; }
static int sh_tail_path(decor_scratch_t *sc, int pose, float u, float t, float SX, float SW) {
    sh_spiral_t a, b; sh_spiral(&a, SH_FREE, SX, SW); sh_spiral(&b, pose, SX, SW);
    float cx = sh_lerp(a.cx, b.cx, u), cy = sh_lerp(a.cy, b.cy, u), r0 = sh_lerp(a.r0, b.r0, u), r1 = sh_lerp(a.r1, b.r1, u);
    float turns = sh_lerp(a.turns, b.turns, u), ky = sh_lerp(a.ky, b.ky, u), c0x = sh_lerp(a.c0x, b.c0x, u), c0y = sh_lerp(a.c0y, b.c0y, u);
    bool wrap = pose == SH_ONBLADE && u > 0.5f;                          /* past half way the blade is inside the coil */
    float breathe = 0.18f * sinf(t * 1.1f);
    const float P0x = 7.0f, P0y = 26.0f, P2x = cx - r0, P2y = cy;
    int n = 0; float s = 0, lx = 0, ly = 0;
    for (int i = 0; i < 14; i++) {
        float u = i / 13.0f, a = (1 - u) * (1 - u), b = 2 * (1 - u) * u, c = u * u;
        float x = a * P0x + b * c0x + c * P2x, y = a * P0y + b * c0y + c * P2y;
        if (i) s += sqrtf((x - lx) * (x - lx) + (y - ly) * (y - ly));
        sc->sh_pts[n][0] = x; sc->sh_pts[n][1] = y; sc->sh_pts[n][2] = s; sc->sh_behind[n] = 0; n++; lx = x; ly = y;
    }
    float th_max = turns * 2 * 3.14159265f; int m = (int)(th_max / 0.17f);
    for (int i = 1; i <= m && n < SH_PTS_MAX; i++) {
        float th = th_max * i / m, f = th / th_max;
        float r = r0 + (r1 - r0) * powf(f, 0.9f) + breathe * f;
        float sn = sinf(th), x = cx - r * cosf(th), y = cy + ky * r * sn;
        s += sqrtf((x - lx) * (x - lx) + (y - ly) * (y - ly));
        sc->sh_pts[n][0] = x; sc->sh_pts[n][1] = y; sc->sh_pts[n][2] = s; sc->sh_behind[n] = wrap && sn < 0; n++; lx = x; ly = y;
    }
    return n;
}
/* the body, row-wise: the spine's x and the half-width of the bean at a row */
static bool sh_body(float ly, float *sx, float *w, float *tb) {
    if (ly < 13.2f || ly > 26.4f) return false;
    float tt = (26.0f - ly) / 12.8f; tt = tt < 0 ? 0 : tt > 1 ? 1 : tt;
    float sp = sinf(3.14159265f * tt);
    *sx = 7.0f + 1.0f * tt + 1.6f * sp; *w = 3.0f + 3.0f * powf(sp, 0.75f); *tb = tt;
    return true;
}
/* one local pixel's byte (0 = water). nod and the row's body numbers come
 * from the caller (once a frame / once a row, not once a pixel: the S3 has
 * no hardware sine or power) */
typedef struct { bool body; float sx, w, tb; } sh_row_t;
static uint8_t sh_tone(float lx, float ly, float t, float flutter, const decor_scratch_t *sc, int k, float nod, const sh_row_t *row, float jut) {
    const float ex = 13.2f + jut, ey = 9.8f + nod;                        /* the snatch: the head and the snout jut forward, the neck stretches */
    const bool head_rows = ly < 15.5f;                                     /* the head, its crown, the snout and the eye live above row 15:
                                                                            * the body and tail rows skip their seven distance tests */
    float de = head_rows ? sqrtf((lx - ex) * (lx - ex) + (ly - ey) * (ly - ey)) : 99;
    if (de <= 1.75f) {                                                     /* the eye, its glint upper-left */
        float gx = lx - (ex - 0.7f), gy = ly - (ey - 0.7f);
        return SHB_ON | (gx * gx + gy * gy < 0.49f ? SHT_GLINT : SHT_EYE);
    }
    float u = 0, d = head_rows && lx > 13 ? sh_seg(lx, ly, 15.0f + jut, 11.0f + nod, 21.6f + jut, 12.5f + nod, &u) : 99;   /* the snout */
    float hw = 1.55f - 0.5f * u;
    if (d <= hw) {
        int tone = SHT_2;
        if (ly > 11.0f + nod + 0.6f + u * 1.2f) tone = SHT_3;
        if (d > hw - 0.7f && ly > 11.0f + nod) tone = SHT_4;
        if (u > 0.9f) tone = SHT_4;
        if ((chash((int)lx, (int)ly) & 7) == 0) tone = sh_clamp(tone + 1, 1, 5);
        return SHB_ON | tone;
    }
    /* the coronet: short blunt nubs, the middle one tallest (Strato 10-15: the
     * first pass's sharp points read as spikes); one short cheek point */
    static const float SPIKES[5][5] = { { 8.9f, 4.0f, 8.9f, 1.7f, 1.25f }, { 10.3f, 4.0f, 10.5f, 2.0f, 1.15f }, { 6.8f, 4.6f, 6.2f, 3.3f, 0.95f },
                                        { 12.6f, 4.6f, 13.3f, 3.5f, 0.85f }, { 5.2f, 7.6f, 3.6f, 7.4f, 0.9f } };
    if (ly < 10.5f) for (int i = 0; i < 5; i++) {
        const float *q = SPIKES[i];
        d = sh_seg(lx, ly, q[0] + jut, q[1] + nod, q[2] + jut, q[3] + nod, &u);
        if (d <= q[4] * (1 - 0.15f * u)) return SHB_ON | ((u > 0.75f || d > q[4] * 0.55f) ? SHT_3 : SHT_2);
    }
    float dx = (lx - 10.4f - jut) / 5.5f, dy = (ly - 8.5f - nod) / 5.4f, r = head_rows ? sqrtf(dx * dx + dy * dy) : 99;   /* the head: a round ellipse */
    if (r <= 1.0f) {
        float lit = -dx * 0.6f - dy * 0.8f;
        int tone = SHT_2;
        if (lit > 0.45f) tone = SHT_1;
        if (lit < -0.45f) tone = SHT_3;
        if (r > 0.82f && lit < 0.1f) tone = SHT_4;
        if (de < 2.75f && (lx < ex || ly < ey)) tone = SHT_1;            /* the pale ring over the eye */
        if ((chash((int)lx, (int)ly) & 7) == 0) tone = sh_clamp(tone + 1, 0, 5);
        return SHB_ON | tone;
    }
    d = ly > 10 && ly < 17.5f && lx > 4 && lx < 13 + jut ? sh_seg(lx, ly, 8.6f + jut, 11.8f + nod, 8.2f, 14.2f, &u) : 99;   /* the throat joins head and body (its top goes with the head) */
    bool throat = d <= 3.1f;
    float sx = row->sx, w = row->w;
    bool body = row->body;
    if (body || throat) {
        float un, ridge; bool inside;
        if (body) {
            un = (lx - sx) / w; inside = fabsf(un) <= 1.0f;
            ridge = fmodf(ly - 14.0f + un * 1.2f + 40.0f, 4.0f);
            if (!inside && un < 0 && fabsf(un) <= 1.0f + 1.3f / w && ridge < 1.4f) return SHB_ON | SHT_4;   /* a dorsal bump */
        } else { sx = 8.4f; w = 3.1f; un = (lx - 8.4f) / 3.1f; inside = true; ridge = 9; }
        if (inside) {
            int tone = SHT_2;
            if (un > 0.28f) {                                              /* the belly plates */
                tone = SHT_1;
                if (fmodf(ly - 15.0f + 30.0f, 3.0f) < 0.9f) tone = SHT_2;
                if (un > 0.86f) tone = SHT_3;
            } else {
                if (ridge < 1.0f) tone = SHT_3;                            /* the ring ridges */
                if (un < -0.78f) tone = SHT_4; else if (un < -0.45f) tone = SHT_3;
                if (un > -0.45f && un < 0.1f && ridge > 1.6f && ly < 20) tone = SHT_2;
            }
            if ((chash((int)lx, (int)ly + 7) & 7) == 0) tone = sh_clamp(tone + 1, 0, 5);
            return SHB_ON | tone;
        }
    }
    float vx = lx - 5.2f, vy = ly - 20.3f, rr = lx < 11.5f && ly > 13.5f && ly < 27.5f ? sqrtf(vx * vx + vy * vy) : 0;   /* the dorsal fin: a fan of rays, fluttering (its corner only) */
    if (rr > 0.6f) {
        float ang = atan2f(vy, vx) * (180.0f / 3.14159265f); if (ang < 0) ang += 360;
        if (ang >= 118 && ang <= 242) {
            float a = (ang - 180) / 62.0f;
            float L = 5.6f * (1 - 0.45f * a * a) + 0.5f * sinf(t * 9.0f * flutter + a * 2.5f);
            if (rr <= L) {
                float ray = fmodf((ang - 180) / 14.0f + 0.25f * sinf(t * 9.0f * flutter + rr) + 8.0f, 1.0f);
                return SHB_ON | ((ray < 0.3f || rr > L - 0.8f) ? SHT_FINR : SHT_FINM);
            }
        }
    }
    if (sc->sh_d[k] != 255) {                                              /* the tail, from the distance map */
        float dd = sc->sh_d[k] / 32.0f, frac = sc->sh_fr[k] / 255.0f;
        hw = sh_tail_hw(frac);
        if (dd <= hw) {
            int tone = SHT_2;
            if (fmodf(frac * 60.0f, 2.6f) < 0.8f) tone = SHT_3;             /* the rings down the tail */
            if (dd > hw - 0.75f) tone = (sc->sh_fl[k] & 2) ? SHT_4 : SHT_3;  /* the shaded edge */
            if (frac > 0.93f) tone = SHT_4;
            if ((chash((int)lx, (int)ly + 3) & 7) == 0) tone = sh_clamp(tone + 1, 0, 5);
            return SHB_ON | SHB_TAIL | ((sc->sh_fl[k] & 1) ? SHB_BEHIND : 0) | tone;
        }
    }
    return 0;
}
/* the sprite for this frame: the tail stamped into the distance map first,
 * then a byte per pixel */
static void sh_sprite(decor_scratch_t *sc, uint8_t *out, float t, int pose, float u, float SX, float SW, float flutter, float jut) {
    int n = sh_tail_path(sc, pose, u, t, SX, SW);
    float L = sc->sh_pts[n - 1][2];
    memset(sc->sh_d, 255, SH_W * SH_H);
    float hwb = sh_tail_hw(0);
    for (int i = 0; i + 1 < n; i++) {
        const float *a = sc->sh_pts[i], *b = sc->sh_pts[i + 1];
        float hwa = hwb; hwb = sh_tail_hw(b[2] / L);
        float hw = hwa + 0.6f;
        int x0 = sh_clamp((int)floorf(fminf(a[0], b[0]) - hw), 0, SH_W - 1), x1 = sh_clamp((int)ceilf(fmaxf(a[0], b[0]) + hw), 0, SH_W - 1);
        int y0 = sh_clamp((int)floorf(fminf(a[1], b[1]) - hw), 0, SH_H - 1), y1 = sh_clamp((int)ceilf(fmaxf(a[1], b[1]) + hw), 0, SH_H - 1);
        for (int y = y0; y <= y1; y++)
            for (int x = x0; x <= x1; x++) {
                float px = x + 0.5f, py = y + 0.5f, u;
                float d = sh_seg(px, py, a[0], a[1], b[0], b[1], &u);
                float frac = (a[2] + (b[2] - a[2]) * u) / L;
                if (d > hwa + (hwb - hwa) * u + 0.6f) continue;
                int di = (int)(d * 32); if (di > 254) di = 254;
                int k = y * SH_W + x;
                if (di < sc->sh_d[k]) {
                    float cross = (b[0] - a[0]) * (py - a[1]) - (b[1] - a[1]) * (px - a[0]);
                    sc->sh_d[k] = (uint8_t)di; sc->sh_fr[k] = (uint8_t)(frac * 255);
                    sc->sh_fl[k] = (uint8_t)((sc->sh_behind[i] ? 1 : 0) | (cross > 0 ? 2 : 0));
                }
            }
    }
    float nod = 0.35f * sinf(t * 1.3f);
    for (int y = 0; y < SH_H; y++) {
        sh_row_t row; row.body = sh_body(y + 0.5f, &row.sx, &row.w, &row.tb);
        for (int x = 0; x < SH_W; x++) out[y * SH_W + x] = sh_tone(x + 0.5f, y + 0.5f, t, flutter, sc, y * SH_W + x, nod, &row, jut);
    }
}
/* the sprite is rebuilt at ~10 Hz, not every frame (its animation - the nod,
 * the fin, the tail's breathing - is slow; a landing's morph is 0.7 s), and
 * at once when the pose or the morph moves: the build is the dear part on
 * the S3 (a sine, a power or a root for most pixels) */
static float g_sh_built_t = -1, g_sh_built_u = -1, g_sh_built_sw = -1, g_sh_built_jut = 0; static int g_sh_built_pose = -1;
static void sh_sprite_cached(decor_scratch_t *sc, float t, int pose, float u, float SX, float SW, float flutter, float jut) {
    bool stale = g_sh_built_pose != pose || fabsf(g_sh_built_u - u) > 0.04f || fabsf(g_sh_built_sw - SW) > 0.1f
              || fabsf(g_sh_built_jut - jut) > 0.4f || t - g_sh_built_t >= 0.1f || t < g_sh_built_t;
    if (!stale) return;
    sh_sprite(sc, sc->sh_spr, t, pose, u, SX, SW, flutter, jut);
    g_sh_built_t = t; g_sh_built_u = u; g_sh_built_sw = SW; g_sh_built_pose = pose; g_sh_built_jut = jut;
}
/* a blade's ribbon at a tank row, exactly as draw_veg lays it (its sway and
 * taper): the centre x and the half-width; false off the blade */
static bool veg_ribbon_at(const tank_t *t, int b, int i, int y, float clock, float *cx, float *half) {
    float bx; int segs = tank_veg_frond(t, b, i, &bx);
    if (segs < 1) segs = 1;
    if (segs > VEG_MAX_SEGS) segs = VEG_MAX_SEGS;
    bool sword = tank_veg_kind(t, b) == VEG_KIND_SWORD;
    int seed = VEG_SEED[b];
    float sway = fast_sin(clock * (sword ? 0.6f : 0.9f) + (i + seed) * 1.7f) * (sword ? 2.5f : 4.0f);
    float sp = (TANK_BOT - 16 - y) / VEG_SEG_DY;
    if (sp < -0.7f || sp > segs - 1 + 0.7f) return false;
    if (sp < 0) sp = 0;
    if (sp > segs - 1) sp = (float)(segs - 1);
    int k = (int)sp, k1 = k + 1 > segs ? segs : k + 1; float fr = sp - k;
    float xk  = bx + sway * k  / (float)segs * fast_sin(k  * 0.4f + clock * 0.6f + i + seed);
    float xk1 = bx + sway * k1 / (float)segs * fast_sin(k1 * 0.4f + clock * 0.6f + i + seed);
    *cx = xk + (xk1 - xk) * fr;
    float u = sp / (segs > 1 ? segs - 1 : 1);
    *half = sword ? 1.2f + 6.0f * (u < 0.4f ? u / 0.4f : (1 - u) / 0.6f) : fmaxf(0.4f, 2.4f - 2.0f * u);
    return true;
}
/* its pitch this frame, radians (the renderer's rule, below): forward with its speed */
static float seahorse_pitch(const tank_t *t) {
    if (t->sh_mode == SH_PERCH && t->sh_akind != SH_ANCHOR_NONE) return 0;
    float speed = sqrtf(t->sh_vx * t->sh_vx + t->sh_vy * t->sh_vy);
    float f = fabsf(t->sh_vx) / 42.0f + speed / 120.0f; if (f > 1) f = 1;
    return 30.0f * f * 3.14159265f / 180.0f;
}
/* its box this frame (the draw's loop and the dynamic rect): the sprite's
 * rectangle through its pitch about the anchor, plus the lean, the bob and a
 * margin - upright (most of the time) that is the 23 x 39 cells and little
 * more, not a box sized for the full pitch */
static void seahorse_box(const tank_t *t, int *x0, int *y0, int *x1, int *y1) {
    float th = seahorse_pitch(t), cs = cosf(th), sn = fabsf(sinf(th));
    float hw = (SH_W * 0.5f + 1) * cs + (SH_H * 0.5f) * sn + 6;        /* the rotated rect's half extents, about its middle */
    float hh = (SH_H * 0.5f + 1) * cs + (SH_W * 0.5f) * sn + 4;
    float cx = t->sh_x, cy = t->sh_y - 33.0f + SH_H * 0.5f;             /* its middle: the anchor row is ~33 cells down */
    *x0 = (int)floorf(cx - hw); *x1 = (int)ceilf(cx + hw);
    *y0 = (int)floorf(cy - hh); *y1 = (int)ceilf(cy + hh);
}
/* the pitch (Strato 10-15, from real seahorses: "sometimes the head will
 * pitch forward when it wants speed; slower movements are more vertical and
 * erect"): the body tips forward with its speed through the water, up to
 * SH_PITCH_MAX degrees at a full swim, about its middle, and stands up
 * again as it eases into a spot. On a blade and at the finger it is erect. */
#define SH_PITCH_MAX 30.0f
#define SH_PIVOT_X 9.0f
#define SH_PIVOT_Y 19.0f
static void draw_seahorse(ctx_t *c, const tank_t *t) {
    if (!(t->sd_unlocks & SD_ITEM_SEAHORSE) || t->sh_x < 0 || t->sh_hidden) return;
    decor_scratch_t *sc = ds();
    bool held = t->sh_mode == SH_PERCH && t->sh_akind != SH_ANCHOR_NONE;
    bool onblade = held && t->sh_akind == SH_ANCHOR_BLADE;             /* the one anchor that sways */
    bool onpost = held && t->sh_akind == SH_ANCHOR_POST;
    bool onsand = held && t->sh_akind == SH_ANCHOR_SAND;
    /* the hold it is closing on (or opening from): the pose and how far along.
     * On the sand the open curl IS the pose: the tail curls under it. */
    int pose = t->sh_pose == SH_POSE_GRIP ? SH_GRIP : onsand ? SH_FREE : SH_ONBLADE;
    float u = t->sh_land / SH_LAND_S; u = u < 0 ? 0 : u > 1 ? 1 : u;
    u = u * u * (3 - 2 * u);                                             /* eased */
    float SX = 5.0f, SW = 1.5f, ax = t->sh_x, ay = t->sh_y, lean = 1.2f * fast_sin(t->clock * 0.7f), pitch = 0;
    float speed = sqrtf(t->sh_vx * t->sh_vx + t->sh_vy * t->sh_vy);
    float flutter = t->sh_mode == SH_HITCH && !t->sh_hitched ? 1.6f : speed > 2 ? 1.0f + speed / 40.0f : 1.0f;
    float free_u = held ? 1 - u : 1;                                     /* how much of the open water is still in it */
    if (onblade) {                                                       /* the blade's sway and lean come in with the hold */
        float cx0 = ax, h0 = SW, cx1 = ax, h1;
        if (veg_ribbon_at(t, t->sh_bed, t->sh_frond, (int)ay, t->clock, &cx0, &h0)) { ax += (cx0 - ax) * u; SW = fmaxf(1.0f, fminf(3.5f, h0)); }
        veg_ribbon_at(t, t->sh_bed, t->sh_frond, (int)ay, t->clock + 0.05f, &cx1, &h1);
        float l = (cx1 - cx0) / 0.05f * 0.9f; l = l < -3 ? -3 : l > 3 ? 3 : l;
        lean = lean * (1 - u) + l * u;
    } else if (onpost) { SW = fmaxf(1.0f, fminf(3.5f, t->sh_ahw)); lean *= 1 - u; }   /* a post stands still: the coil hugs its middle */
    else if (onsand) lean *= 1 - u * 0.6f;
    if (free_u > 0) {                                                    /* off a blade it bobs in the water and pitches with speed */
        ay += free_u * 1.6f * fast_sin(t->clock * 1.7f); ax += free_u * 0.8f * fast_sin(t->clock * 0.9f + 1.0f);
        float f = fabsf(t->sh_vx) / 42.0f + speed / 120.0f; if (f > 1) f = 1;
        pitch = SH_PITCH_MAX * f * 3.14159265f / 180.0f;                /* forward = the way it faces */
    }
    sh_spiral_t pa, pb; sh_spiral(&pa, SH_FREE, SX, SW); sh_spiral(&pb, pose, SX, SW);
    float coilx = sh_lerp(pa.cx, pb.cx, u), coily = sh_lerp(pa.cy, pb.cy, u);
    sh_sprite_cached(sc, t->clock, pose, u, SX, SW, flutter, tank_seahorse_jut(t));
    int fac = t->sh_facing >= 0 ? 1 : -1;
    /* local (mirrored) -> tank: p = (lxm - coilx_m, ly - coily) sheared by the lean, rotated
     * by the pitch about the body's middle, then the anchor. Pitched, it is drawn by the
     * INVERSE: every tank pixel of the box maps back to a local cell, so a rotated sprite
     * has no holes. UPRIGHT (held: most of the time) each row is a whole-pixel shift of the
     * cells, and that shift has HYSTERESIS: a row keeps its snapped place until the true
     * one has pulled 0.75 px away (Strato 10-15: with the slow sway a row hovered at a
     * half-pixel and "fought between two pixels" while its neighbours stayed put). */
    float coilx_m = fac > 0 ? coilx : SH_W - coilx, pivx_m = (fac > 0 ? SH_PIVOT_X : SH_W - SH_PIVOT_X) - coilx_m, pivy = SH_PIVOT_Y - coily;
    float th = fac > 0 ? pitch : -pitch, cs = cosf(th), sn = sinf(th);
    int bx0, by0, bx1, by1; seahorse_box(t, &bx0, &by0, &bx1, &by1);
    src_t pal[SHT_N]; int pal_y = -1;
    bool upright = pitch == 0;
    if (upright) {                                                       /* the row offset, snapped with hysteresis */
        float want = ay - coily;
        if (fabsf(want - sc->sh_yoff) > 0.75f) sc->sh_yoff = roundf(want);
        if (sc->sh_yoff == 0 && want != 0) sc->sh_yoff = roundf(want);  /* the first frame */
    }
    for (int py = by0; py <= by1; py++) {
        if (!CTX_IN(c, bx0 + 1, py) && !CTX_IN(c, bx1 - 1, py)) continue;
        float rcx = 0, rhalf = 0; bool ribbon = onblade ? veg_ribbon_at(t, t->sh_bed, t->sh_frond, py, t->clock, &rcx, &rhalf)
                                              : onpost && fabsf(py - t->sh_ay) <= 14 && (rcx = t->sh_ax, rhalf = t->sh_ahw, true);
        int row_ly = -1, row_xs = 0;
        if (upright) {
            row_ly = py - (int)sc->sh_yoff;                               /* this row's cell row */
            if (row_ly < 0 || row_ly >= SH_H) continue;
            float want = ax - coilx_m + lean * (33.0f - (row_ly + 0.5f)) / 33.0f;   /* where the row's cell 0 lands, exactly */
            if (fabsf(want - sc->sh_rowx[row_ly]) > 0.75f) sc->sh_rowx[row_ly] = roundf(want);
            row_xs = (int)sc->sh_rowx[row_ly];
        }
        for (int px = bx0; px <= bx1; px++) {
            if (!CTX_IN(c, px, py)) continue;
            int ilx, ily;
            if (upright) {
                int lxm = px - row_xs; ily = row_ly;
                if (lxm < 0 || lxm >= SH_W) continue;
                ilx = fac > 0 ? lxm : SH_W - 1 - lxm;
            } else {
                float qx = px + 0.5f - ax - pivx_m, qy = py + 0.5f - ay - pivy;          /* about the pivot */
                float rx = qx * cs + qy * sn + pivx_m, ry = -qx * sn + qy * cs + pivy;   /* un-rotated */
                float ly = ry + coily;
                float lxm = rx - lean * (33.0f - ly) / 33.0f + coilx_m;                  /* un-sheared */
                ilx = (int)floorf(fac > 0 ? lxm : SH_W - lxm); ily = (int)floorf(ly);
                if (ilx < 0 || ilx >= SH_W || ily < 0 || ily >= SH_H) continue;
            }
            uint8_t b = sc->sh_spr[ily * SH_W + ilx];
            if (!(b & SHB_ON)) continue;
            bool on_ribbon = ribbon && fabsf(px + 0.5f - rcx) <= rhalf + 0.3f;
            if ((b & SHB_BEHIND) && on_ribbon) continue;                   /* the blade shows through the coil */
            int tone = b & 15;
            if (pal_y != py) {                                             /* this row's tones, tinted with its water */
                uint32_t w = water_rgb(py < 0 ? 0 : py >= TANK_H ? TANK_H - 1 : py);
                for (int k = 0; k < SHT_N; k++) pal[k] = src_color(mix(w, SH_RGB[k], SH_TINT), c->dim);
                pal_y = py;
            }
            px_blend_s(c, px, py, &pal[tone], SH_ALPHA[tone]);
            /* a contact shadow on the ribbon under a front crossing of the tail */
            if ((b & SHB_TAIL) && on_ribbon && ily > 27 && ily + 1 < SH_H && !(sc->sh_spr[(ily + 1) * SH_W + ilx] & SHB_TAIL)) {
                float r2cx = rcx, r2half = rhalf;
                if ((onpost || veg_ribbon_at(t, t->sh_bed, t->sh_frond, py + 1, t->clock, &r2cx, &r2half)) && fabsf(px + 0.5f - r2cx) <= r2half + 0.3f)
                    px_blend(c, px, py + 1, 0x000000, 115);
            }
        }
    }
}

/* where the castle stands this frame: its centre x (-1 = not bought), its
 * depth, and whether the keeper is dragging it on the placement page (then
 * it is drawn live over a scene baked WITHOUT it, instead of a rebake per
 * frame) */
/* the coral likewise (2026-09-23): BEHIND it is baked into the scene; AMONG
 * and IN FRONT it is drawn every frame (a 60 x 92 sprite - cheap) */

/* ---- the reef cluster, procedural (2026-09-24; Strato's coral-cluster.png:
 * a branching coral, three purple tube sponges, a cyan brain coral and green
 * weed on a pile of grey stones). The same idiom as the coral, bigger: a
 * CL_CW x CL_CH grid of 2 px cells, five ELEMENTS drawn in painter's order
 * (rock, weed, the coral, the tubes, the brain, weed in front), each classed
 * by its own signed distance - rim below / right, lit above / left, a deep
 * variant per element (moss on the rock, the coral's tips, the tube mouths,
 * the brain's grooves, the weed's tips) - into one tone sprite. The size is
 * the growth's first phase: CLUSTER_SIZE_MIN of full on the day it is bought
 * (it must look mature at once), full at 1. The look is a scheme (three
 * presets: the coral / the tubes / the brain each), tinted per row. */
#define CL_CELL 2
#define CL_W    (CL_CW * CL_CELL)
#define CL_FY   (TANK_BOT - 14 + DECOR_SINK)
#define CL_Q    64                                           /* sprite rebuilds over the size phase */
enum { CLE_ROCK, CLE_WEED, CLE_CORAL, CLE_TUBE, CLE_BRAIN, CLE_N };
enum { CLV_BODY, CLV_LIT, CLV_RIM, CLV_SHADE, CLV_DEEP, CLV_N };
#define CL_TONE(e, v) (1 + (e) * CLV_N + (v))
static int      g_cl_q = -1, g_cl_cells;
static int      g_cl_scheme = -1; static float g_cl_dim = -1;
/* geometry in unscaled cells: x from the left, y UP from the base row; the
 * size scales it about (36, 0) */
#define CL_CX 36.0f
typedef struct { float x0, y0, x1, y1, r; } cl_cap_t;
static const cl_cap_t CL_ROCKS[] = {                         /* stones, as capsules (a circle = a zero-length one) */
    { 8, 3, 8, 3, 6 }, { 20, 4, 20, 4, 7 }, { 33, 3, 33, 3, 6 }, { 46, 4, 46, 4, 7 }, { 58, 3, 58, 3, 6 }, { 66, 4, 66, 4, 5 },
    { 14, 7, 14, 7, 4 }, { 40, 7, 40, 7, 4 }, { 52, 8, 52, 8, 4 }, { 4, 6, 70, 6, 2.5f },
};
static const cl_cap_t CL_TUBES[] = {                         /* back to front */
    { 45, 5, 44, 22, 4.5f }, { 61, 4, 63, 27, 5.0f }, { 52, 4, 53, 34, 5.5f }, { 58, 3, 59, 13, 3.5f },
};
static const cl_cap_t CL_BRAIN[] = {
    { 35, 9, 35, 9, 7 }, { 28, 7, 28, 7, 5.5f }, { 42, 7, 42, 7, 5.5f }, { 35, 14, 35, 14, 5 }, { 31, 12, 31, 12, 4 }, { 39, 12, 39, 12, 4 },
};
static const cl_cap_t CL_WEED_BACK[] = {
    { 13, 3, 11, 11, 1.1f }, { 14, 3, 15, 12, 1.1f }, { 12, 3, 9, 9, 1.0f },
    { 30, 3, 29, 10, 1.1f }, { 31, 3, 33, 11, 1.0f },
    { 47, 3, 48, 12, 1.1f }, { 46, 3, 44, 10, 1.0f },
    { 66, 3, 68, 11, 1.1f }, { 65, 3, 63, 9, 1.0f }, { 67, 3, 67, 12, 1.0f },
};
static const cl_cap_t CL_WEED_FRONT[] = {
    { 21, 2, 19, 9, 1.1f }, { 22, 2, 24, 8, 1.0f }, { 41, 2, 43, 8, 1.0f }, { 40, 2, 39, 7, 1.0f }, { 56, 2, 57, 8, 1.0f },
};
#define CL_CORAL_X  24.0f                                    /* the branching coral's base, and its scale */
#define CL_CORAL_Y  5.0f
#define CL_CORAL_SX 0.95f
#define CL_CORAL_SY 0.85f
static inline float cl_cap_sd(float px, float py, const cl_cap_t *k, float rs) {
    float dx = k->x1 - k->x0, dy = k->y1 - k->y0, len2 = dx * dx + dy * dy;
    float u = len2 > 1e-6f ? ((px - k->x0) * dx + (py - k->y0) * dy) / len2 : 0;
    if (u < 0) u = 0;
    if (u > 1) u = 1;
    float ex = px - (k->x0 + dx * u), ey = py - (k->y0 + dy * u);
    return sqrtf(ex * ex + ey * ey) - k->r * rs;
}
/* an element's signed distance at an unscaled point; *deep = the element's
 * deep variant here (a tube's mouth, a brain groove, the coral's tip cap) */
static float cl_sd(int e, int pass, float px, float py, bool *deep) {
    float best = 1e9f; *deep = false;
    if (e == CLE_ROCK) {
        for (int i = 0; i < (int)(sizeof CL_ROCKS / sizeof CL_ROCKS[0]); i++) { float d = cl_cap_sd(px, py, &CL_ROCKS[i], 1); if (d < best) best = d; }
        *deep = best < -1.5f && py > 4 && (chash((int)(px * 0.7f), (int)(py * 0.7f)) % 100) < 22;   /* moss between the stones */
    } else if (e == CLE_WEED) {
        const cl_cap_t *w = pass ? CL_WEED_FRONT : CL_WEED_BACK; int n = pass ? (int)(sizeof CL_WEED_FRONT / sizeof w[0]) : (int)(sizeof CL_WEED_BACK / sizeof w[0]);
        for (int i = 0; i < n; i++) { float d = cl_cap_sd(px, py, &w[i], 1); if (d < best) best = d;
            float tx = px - w[i].x1, ty = py - w[i].y1; if (tx * tx + ty * ty < 2.2f) *deep = true; }
    } else if (e == CLE_CORAL) {
        float qx = (px - CL_CORAL_X) / CL_CORAL_SX + 16.0f, qy = (py - CL_CORAL_Y) / CL_CORAL_SY;   /* into the coral's own cells */
        bool tip; best = coral_sd(qx, qy, 1.0f, &tip) * CL_CORAL_SY; *deep = tip;
    } else if (e == CLE_TUBE) {
        const cl_cap_t *k = &CL_TUBES[pass];
        best = cl_cap_sd(px, py, k, 1);
        float mx = (px - k->x1) / (k->r - 1.3f), my = (py - k->y1) / 1.7f;
        *deep = mx * mx + my * my < 1;                           /* the mouth */
    } else if (e == CLE_BRAIN) {
        float second = 1e9f;                                     /* the lobes: the seam where two meet is a groove */
        for (int i = 0; i < (int)(sizeof CL_BRAIN / sizeof CL_BRAIN[0]); i++) {
            float d = cl_cap_sd(px, py, &CL_BRAIN[i], 1);
            if (d < best) { second = best; best = d; } else if (d < second) second = d;
        }
        float v = fast_sin(px * 1.3f + py * 0.9f) + 0.6f * fast_sin(py * 2.1f - px * 0.5f);
        *deep = best < -0.8f && ((second < 0.5f && second > -1.2f) || v > 0.95f);   /* the seams, and a few winding grooves */
    }
    return best;
}
/* one element pass over the sprite: classed by its own distance field and
 * painted over what is there */
static void cl_paint(int e, int pass, float s) {
    int8_t  (*sd)[CL_CW] = ds()->cl_sd;                         /* eighths of a cell; the deep flags as bits (see sd_q) */
    uint8_t (*dp)[(CL_CW + 7) / 8] = ds()->cl_dp;
    for (int j = 0; j < CL_CH; j++)
        for (int i = 0; i < CL_CW; i++) {
            float px = CL_CX + (i + 0.5f - CL_CX) / s, py = (CL_CH - 1 - j + 0.5f) / s;   /* unscale the query */
            bool deep; sd[j][i] = sd_q(cl_sd(e, pass, px, py, &deep) * s);
            if (deep) dp[j][i >> 3] |= (uint8_t)(1 << (i & 7)); else dp[j][i >> 3] &= (uint8_t)~(1 << (i & 7));
        }
    for (int j = 0; j < CL_CH; j++)
        for (int i = 0; i < CL_CW; i++) {
            int d = sd[j][i];
            if (d >= 0) continue;
            bool deep = (dp[j][i >> 3] >> (i & 7)) & 1;
            bool out_l = i == 0 || sd[j][i - 1] >= 0, out_u = j == 0 || sd[j - 1][i] >= 0;
            bool out_r = i == CL_CW - 1 || sd[j][i + 1] >= 0, out_d = j == CL_CH - 1 || sd[j + 1][i] >= 0;
            int v;
            if (deep && e != CLE_ROCK) v = CLV_DEEP;
            else if (out_r || out_d) v = CLV_RIM;
            else if (out_l || out_u) v = CLV_LIT;
            else if (deep) v = CLV_DEEP;                        /* the rock's moss: never on its edge */
            else { uint32_t h = chash(i + 97 * e, j) % 100; v = h < 20 ? CLV_SHADE : h < 26 ? CLV_LIT : CLV_BODY; }
            if (e == CLE_TUBE && deep && (out_u || out_l)) v = CLV_RIM;   /* the mouth's far lip */
            ds()->cl_sprite[j][i] = (uint8_t)CL_TONE(e, v);
        }
}
static void cl_build(float g) {
    if (g > 1) g = 1;
    if (g < 0) g = 0;
    float s = CLUSTER_SIZE_MIN + (1.0f - CLUSTER_SIZE_MIN) * g;
    memset(ds()->cl_sprite, 0, sizeof ds()->cl_sprite);
    cl_paint(CLE_ROCK, 0, s);
    cl_paint(CLE_WEED, 0, s);
    cl_paint(CLE_CORAL, 0, s);
    for (int k = 0; k < 4; k++) cl_paint(CLE_TUBE, k, s);
    cl_paint(CLE_BRAIN, 0, s);
    cl_paint(CLE_WEED, 1, s);
    g_cl_cells = 0;
    for (int j = 0; j < CL_CH; j++) for (int i = 0; i < CL_CW; i++) g_cl_cells += ds()->cl_sprite[j][i] != 0;
}
int render_cluster_cells(float growth) { cl_build(growth); g_cl_q = -1; return g_cl_cells; }
static void cl_tint_fill(int scheme, float dim) {
    if (g_cl_scheme == scheme && g_cl_dim == dim) return;
    const cluster_scheme_t *sc = &CLUSTER_SCHEMES[scheme];
    uint32_t base[CLE_N] = { 0x9a9284, 0x8fc63a, sc->coral, sc->tube, sc->brain };
    uint32_t tone[CL_TONES]; tone[0] = 0;
    for (int e = 0; e < CLE_N; e++) {
        uint32_t c = base[e];
        tone[CL_TONE(e, CLV_BODY)]  = c;
        tone[CL_TONE(e, CLV_LIT)]   = mix(c, 0xfff0b0, e == CLE_ROCK ? 0.22f : 0.36f);
        tone[CL_TONE(e, CLV_RIM)]   = mix(c, e == CLE_ROCK ? 0x2a2620 : 0x20060e, 0.55f);
        tone[CL_TONE(e, CLV_SHADE)] = mix(c, 0x000000, 0.24f);
        tone[CL_TONE(e, CLV_DEEP)]  = e == CLE_ROCK ? 0x6f9a3a : e == CLE_CORAL ? mix(c, 0xffe8a0, 0.58f)
                                    : e == CLE_TUBE ? mix(c, 0x100418, 0.65f) : e == CLE_BRAIN ? mix(c, 0x0a3040, 0.45f) : mix(c, 0xe8ff80, 0.40f);
    }
    for (int r = 0; r < CL_H; r++) {
        int y = CL_FY - CL_H + r;
        uint32_t w = water_rgb(y < 0 ? 0 : y >= TANK_H ? TANK_H - 1 : y);
        for (int k = 1; k < CL_TONES; k++) ds()->cl_row[k][r] = rgb565(mix(w, tone[k], 0.90f), dim);
    }
    g_cl_scheme = scheme; g_cl_dim = dim;
}
static void draw_cluster(ctx_t *c, int cx, int scheme, float growth, bool final) {
    if (growth > 1) growth = 1;
    int q = (int)(growth * (CL_Q - 0.01f));
    if (q != g_cl_q) { cl_build(growth); g_cl_q = q; }
    cl_tint_fill(scheme, c->dim);
    int x0 = cx - CL_W / 2, y0 = CL_FY - CL_H;
    for (int j = 0; j < CL_CH; j++)
        for (int i = 0; i < CL_CW; i++) {
            uint8_t t = ds()->cl_sprite[j][i];
            if (!t) continue;
            for (int dy = 0; dy < CL_CELL; dy++) {
                int y = y0 + j * CL_CELL + dy;
                uint16_t v = ds()->cl_row[t][j * CL_CELL + dy];
                for (int dx = 0; dx < CL_CELL; dx++) {
                    int x = x0 + i * CL_CELL + dx;
                    if (!CTX_IN(c, x, y)) continue;
                    uint16_t *p = &CTX_PX(c, x, y);
                    *p = v;
                    rec_px(x, y);
                    if (final) {
                        PK_LIGHT_PX(p, x, y);
                        int a = g_vig ? g_vig[y * TANK_W + x] : vig_alpha(x, y);
                        if (a) px_darken(p, a);
                        if (g_dirty) g_dirty[y * DIRTY_WORDS_PER_ROW + (x >> 5)] &= ~(1u << (x & 31));
                    }
                }
            }
        }
    draw_floor_mound(c, cx, CLUSTER_HALF_W - 2, 6, final);            /* anchored: the rock sits in a low hump of the floor's stones */
}
/* the bloom (growth 1 -> CLUSTER_FULL): more and more tentacles, dealt one
 * at a time round the hosts - the four tube mouths, the coral's seven tips,
 * the brain's top - up to CL_TENT_MAX, each host's fan widening as it fills.
 * Per frame, swaying (draw_crown); the bounding box for the caller's rect. */
#define CL_TENT_MAX 48
#define CL_HOSTS    12
static void draw_cluster_crown(ctx_t *c, int cx, int scheme, float growth, float clock, int *bx0, int *by0, int *bx1, int *by1) {
    *bx0 = *by0 = 1 << 20; *bx1 = *by1 = -1;
    float phase = (growth - 1.0f) / (CLUSTER_FULL - 1.0f);
    if (phase <= 0) return;
    if (phase > 1) phase = 1;
    int total = (int)(phase * CL_TENT_MAX + 0.5f);
    if (total < 1) return;
    const cluster_scheme_t *sc = &CLUSTER_SCHEMES[scheme];
    float s = 1.0f;                                          /* full size by the time it blooms */
    float x0 = cx - CL_W / 2;
    for (int k = 0; k < CL_HOSTS; k++) {
        int n = total / CL_HOSTS + (k < total % CL_HOSTS ? 1 : 0);
        if (!n) continue;
        float hx, hy, len; uint32_t rgb;
        if (k < 4) { const cl_cap_t *t = &CL_TUBES[k]; hx = t->x1; hy = t->y1 + 0.5f; len = 11 + t->r; rgb = sc->tube; }
        else if (k < 11) {                                   /* the coral's tips, in CORAL_SEGS order */
            int ti = 0; const coral_seg_t *sg = NULL;
            for (int i = 0; i < CORAL_NSEG && !sg; i++) if (CORAL_SEGS[i].tip && ti++ == k - 4) sg = &CORAL_SEGS[i];
            if (!sg) continue;
            hx = CL_CORAL_X + (sg->x1 - 16.0f) * CL_CORAL_SX; hy = CL_CORAL_Y + sg->y1 * CORAL_Y_SCALE * CL_CORAL_SY; len = 9; rgb = sc->coral;
        } else { hx = 35; hy = 20; len = 8; rgb = sc->brain; }
        float ox = x0 + (CL_CX + (hx - CL_CX) * s) * CL_CELL + 1, oy = CL_FY - hy * s * CL_CELL - 1;
        float spread = 0.30f + 0.08f * n;                    /* radians each side of up, widening as the fan fills */
        int qx0, qy0, qx1, qy1;
        draw_crown(c, ox, oy, n, -1.5708f - spread, -1.5708f + spread, len, mix(rgb, 0xffffff, 0.55f), k * 7, clock, &qx0, &qy0, &qx1, &qy1);
        if (qx1 >= qx0) { if (qx0 < *bx0) *bx0 = qx0; if (qy0 < *by0) *by0 = qy0; if (qx1 > *bx1) *bx1 = qx1; if (qy1 > *by1) *by1 = qy1; }
    }
}

/* ---- THE PIECES (2026-10-17, Strato: a stack of the pumpkin, the grass,
 * the sword plant and a coral broke - "we need some global layering
 * mechanic") ----
 * The castle, the coral, the reef cluster and the pumpkin are ONE list, back
 * to front: tank_decor_order (depth BEHIND / IN FRONT of the plant layer,
 * then the keeper's rank; the sword plant is grass - bed 3 - and takes part
 * by its rank where it matters). Everything about who covers whom follows
 * from the list:
 *   - the bake paints the pieces in list order (the ones BEHIND, then - with
 *     the scene cache and no placement page up - the ones IN FRONT), each
 *     recording the pixels it covers in its own MASK (ds()->pm[k]);
 *   - a grass bed's spans skip the masks of the pieces above the bed
 *     (veg_skip_for): every piece IN FRONT for the grass, plus the pieces at
 *     its own depth ranked above it for the sword plant;
 *   - after the fish, every piece IN FRONT comes back over what was drawn on
 *     it this frame from the scene (pc_restore: the dirty mask says where, the
 *     scene holds the pixel, already lit and vignetted) - the castle's front
 *     row included (its keep stays behind the fish: the swim-through), so its
 *     old per-rect repaint is gone;
 *   - an overlay drawn every frame for a piece - a crown's tentacles, the
 *     candle's flicker - is clipped by the masks of the pieces above it
 *     (pc_covered_above, through g_clip_k / the lantern's g_pk_k);
 *   - the lantern's light: the pieces above it shade (the castle by its own
 *     rules) and cast on the sand; those below are lit.
 * A piece being dragged on its page is not in the scene: it is drawn live
 * (BEHIND: right over the scene; IN FRONT: after the grass), unmasked. The
 * castle's masks are static and centre-relative (castle_put), so it covers
 * even while dragged. */
static uint32_t chest_pixel(int x, int y) {
    if (x < 0 || x >= CHEST_W || y < 0 || y >= CHEST_H) return 0;
    if (y < 4 && (x < 5 || x >= CHEST_W - 5)) return 0;
    if (y < 12) {
        if (y < 2 || x < 2 || x >= CHEST_W - 2) return 0xd9aa40;
        return y % 5 == 0 ? 0x39231b : 0x80502b;
    }
    if (y < 18) return ((x + 3 * y) % 9 < 5) ? 0xffd55a : 0x8a5f22;
    if (x < 3 || x >= CHEST_W - 3 || y == 18 || y >= CHEST_H - 3) return 0xc79b39;
    if (x >= 20 && x <= 27 && y <= 27) return y < 24 ? 0xffd55a : 0x36231d;
    return y % 7 == 0 ? 0x40271d : 0x976039;
}
static int g_buoy_sway, g_buoy_bob;
static uint32_t ornament_pixel(int item, int x, int y) {
    if (x < 0 || y < 0 || x >= tank_ornament_w(item) || y >= tank_ornament_h(item)) return 0;
    if (item >= 32 && item < SD_ITEM_COUNT) {
        int dx = x - 20, dy = y - 20;
        if (item == 32) return x > 3 && x < 37 && (y == 10 + x % 9 || y == 22 + x % 9) ? 0x8bdce5 : 0;
        if (item == 33) return y > 7 && y < 37 && abs(dx) < 17 && !(abs(dx) < 8 && y > 18) ? (y < 14 ? 0xa3c89a : 0x789d8b) : 0;
        if (item == 34) return dx * dx + dy * dy < 196 ? (abs(dx) < 3 || abs(dy) < 3 ? 0xffdc73 : 0xc18be4) : 0;
        return dx * dx + dy * dy >= 100 && dx * dx + dy * dy <= 225 ? (x < 20 ? 0xffdc73 : 0xd99b4e) : 0;
    }
    if (item == 9) return chest_pixel(x, y);
    if (item >= 22) {
        int dx = x - 20, dy = y - 22;
        switch (item) {
        case 22: {
            int cy = y < 13 ? 9 : y < 25 ? 20 : 32, rx = y < 13 ? 8 : y < 25 ? 13 : 18;
            if (dx * dx * 25 + (y - cy) * (y - cy) * rx * rx > rx * rx * 25) return 0;
            return y < cy - 1 ? 0xc2d5c1 : x < 20 ? 0x809e94 : 0x526f70;
        }
        case 23:
            if (dx * dx * 4 + (y - 29) * (y - 29) * 9 <= 1156)
                return (x + y) % 7 < 2 ? 0xc080b6 : 0xe6b7d4;
            if (dx * dx + (y - 20) * (y - 20) <= 49)
                return x < 19 && y < 20 ? 0xffffff : 0xc5e9ed;
            if (y > 3 && y < 18 && abs(dx) < y) return y % 4 ? 0xbda0cf : 0xf1c7e4;
            return 0;
        case 24:
            if (y >= 19 && y <= 32 && x >= 2 && x <= 37) {
                if ((x - 7) * (x - 7) + (y - 25) * (y - 25) < 25)
                    return abs(x - 7) == 3 || abs(y - 25) == 3 ? 0xe4b57e : 0x69452e;
                return y % 4 == 0 ? 0x795038 : y < 25 ? 0xc39159 : 0x97633c;
            }
            if (x >= 23 && x < 29 && y >= 10 && y < 22) return 0x97633c;
            return 0;
        case 25: {
            int tube = x / 13, cx = tube * 13 + 6, top = tube == 1 ? 3 : tube == 0 ? 14 : 9;
            if (y < top || y > 37 || abs(x - cx) > 5) return 0;
            if (y < top + 4) return abs(x - cx) < 3 ? 0x795337 : 0xffd86b;
            return (x + y * 2) % 11 == 0 ? 0xa67e33 : x < cx ? 0xffc75c : 0xc99739;
        }
        case 26:
            if (y < 5 || y > 37 || abs(dx) > 12 + (y > 11 && y < 31 ? 4 : 0)) return 0;
            if (y == 9 || y == 12 || y == 29 || y == 32) return x < 20 ? 0x9aafae : 0x4d6c74;
            return x % 7 == 0 ? 0x66422c : x < 20 ? 0xbc8755 : 0x95633e;
        case 27:
            if (y >= 32 && y <= 37 && abs(dx) < 18) return 0xb48d49;
            if (dx * dx + dy * dy > 225 || y < 7) return 0;
            if (dx * dx + dy * dy < 49) return (x + y) % 7 == 0 ? 0x8edbd9 : 0x264653;
            if (dx * dx + dy * dy < 81) return 0xf1d084;
            return (x + 3 * y) % 23 == 0 ? 0xe7cf84 : x < 20 ? 0xd8af5a : 0x96793d;
        case 28:
            if (dx * dx * 4 + (y - 29) * (y - 29) * 16 < 1024 &&
                !(x > 20 && abs(y - 29) <= (x - 20) / 3))
                return y == 29 || x == 20 ? 0xa0dc83 : x < 20 ? 0x4fba83 : 0x298c6f;
            if (abs(dx) + abs(y - 17) < 10) return abs(dx) < 2 && abs(y - 17) < 3 ? 0xffdd72 : y < 17 ? 0xffb4de : 0xdb73bd;
            return 0;
        case 29:
            if (x >= 29 && x < 39 && y >= 14 && y <= 18) return 0xf79b40;
            if ((x - 26) * (x - 26) + (y - 13) * (y - 13) < 64) {
                if (x == 29 && y == 11) return 0x16272d;
                return x < 24 ? 0xffed8b : 0xffd052;
            }
            if (dx * dx * 4 + (y - 26) * (y - 26) * 9 < 784 || (x < 10 && y > 17 && y < 27))
                return y < 26 ? 0xffdc62 : 0xdba43b;
            return 0;
        case 30:
            if (x < 8 || x > 31 || y < 4 || y > 36) return 0;
            if (y < 9 || y > 31) return 0x8faeb5;
            return y % 5 < 2 ? 0x274d50 : x < 20 ? 0x57a591 : 0x357a72;
        case 31:
            if (y > 30 && abs(dx) < 15) return 0xa37150;
            if (y > 9 && y < 32 && (abs(dx) < 2 || abs(abs(dx) - (31 - y) / 2) < 3))
                return x < 20 ? 0x91d68b : 0x439c77;
            return 0;
        default: return 0;
        }
    }
    if (item == 16) {
        if (y >= 32 && y < 49 && x >= (y - 32) / 2 && x < 64 - (y - 32))
            return y % 6 == 0 || x % 17 == 0 ? 0x3d2926 : x < 30 ? 0x9d6443 : 0x704832;
        if (x >= 29 && x <= 31 && y < 36) return 0x9e8054;
        if (y >= 3 && y < 29 && x > 32 && x < 57 - y / 2)
            return (x + y) % 13 == 0 ? 0 : x < 40 ? 0xe8d4a3 : 0xb9aa87;
        return 0;
    }
    if (item == 17) {
        int dx = x - 15;
        if (y < 7 && abs(dx) < 9) return (x + y) % 7 < 3 ? 0xffd55a : 0xb48932;
        if (y >= 7 && y < 13 && abs(dx) <= 6) return 0xc27a47;
        if (y >= 13 && y < 35 && dx * dx * 100 + (y - 23) * (y - 23) * 121 <= 12100)
            return y == 16 || y == 30 ? 0xf1c47b : x < 14 ? 0xdb9561 : 0x8a4d36;
        return y >= 35 && abs(dx) < 9 ? 0x9b613f : 0;
    }
    if (item == 18) {
        static const int cx[3] = {9, 23, 34}, top[3] = {16, 0, 23};
        for (int i = 2; i >= 0; i--) {
            int d = abs(x - cx[i]), tip = top[i];
            if (y >= tip && y < 44 && d <= 6 && (y - tip >= 9 || d <= (y - tip) * 2 / 3))
                return x < cx[i] ? 0xe3b8ff : x == cx[i] ? 0xa9ecff : 0x8455d9;
        }
        return y >= 43 && abs(x - 21) < 18 ? 0x68577f : 0;
    }
    if (item == 19) {
        int dx = x - 24;
        if (y >= 51 && abs(dx) <= 2) return 0xc66b79;
        int height = 54 - y;
        if (height >= 0 && height <= 51 && abs(dx) <= height / 2 + 2) {
            bool branch = abs(dx) <= 1 || abs(abs(dx) * 2 - height) < 2 ||
                (height + abs(dx) * 2) % 14 < 2 || (y % 9 == 0 && abs(dx) < height / 2);
            if (branch) return x < 24 ? 0xf0a0bd : 0xb55294;
        }
        return y > 63 && abs(dx) < 9 ? 0x8b727b : 0;
    }
    if (item == 20) {
        int dx = x - 29, dy = y - 25, ring = dx * dx + dy * dy;
        bool stone = (y >= 25 && y < 59 && abs(dx) >= 18 && abs(dx) <= 25) ||
            (y < 26 && ring >= 324 && ring <= 625) || (y >= 59 && y <= 63 && abs(dx) <= 28);
        if (!stone) return 0;
        return y % 9 == 0 || (x + (y / 9 % 2) * 7) % 14 == 0 ? 0x526a65 : y < 15 ? 0xb9c5a4 : x < 29 ? 0x91a38d : 0x6b8278;
    }
    if (item == 21) {
        int dx = x - 18 - g_buoy_sway, dy = y - 16 - g_buoy_bob;
        if (dy <= 3 && dx * dx * 100 + dy * dy * 225 <= 22500)
            return dy > 0 ? 0xffb5e7 : dx < -3 ? 0xb5e5ff : 0xb984ea;
        if (dy >= 4 && dy < 36 && (abs(dx - (dy % 8 < 4 ? 1 : -1)) == 6 || dx == 0))
            return 0xdb9eea;
        if (y >= 105 && abs(x - 18) < 6) return 0x7b7d8b;
        int start = 52 + g_buoy_bob;
        if (y >= start && y < 105 && x == 18 + g_buoy_sway * (105 - y) / (105 - start)) return 0x8d92b1;
        return 0;
    }
    if (item == 10) {
        int dx = x - 17, dy = y - 18;
        if (y < 19) {
            if (dx * dx * 256 + dy * dy * 289 > 73984) return 0;
            bool rib = (abs(dx) * 18 / (19 - y + 3)) % 5 == 0;
            return rib ? 0xc58b8e : y < 7 ? 0xffe8cc : 0xe8bcac;
        }
        return abs(dx) <= 8 - (y - 19) ? 0xf4d8bf : 0;
    }
    if (item == 11) {
        static const int vx[10] = {16,20,33,23,27,16,5,9,0,12};
        static const int vy[10] = {0,11,11,19,31,24,31,19,11,11};
        bool inside = false;
        for (int i = 0, j = 9; i < 10; j = i++) {
            if ((vy[i] > y) != (vy[j] > y) &&
                x < (vx[j] - vx[i]) * (y - vy[i]) / (vy[j] - vy[i]) + vx[i]) inside = !inside;
        }
        if (!inside) return 0;
        return (x + y * 3) % 11 == 0 ? 0xffd1a0 : x < 16 ? 0xf49b69 : 0xc9614c;
    }
    if (item == 13) {
        int dx = x - 17 - g_buoy_sway, dy = y - 17 - g_buoy_bob;
        if (dx * dx * 100 + dy * dy * 81 <= 8100)
            return dy < -2 || dy > 3 ? (dx < 0 ? 0xf7a15a : 0xd56c35) : 0xffebc4;
        if (y >= 98) return abs(x - 17) <= 7 - abs(101 - y) ? 0x70766e : 0;
        int start = 27 + g_buoy_bob;
        if (y >= start && x == 17 + g_buoy_sway * (98 - y) / (98 - start)) return 0xb7ae87;
        return 0;
    }
    int dx = x - 15, dy = y - 6, ring = dx * dx + dy * dy;
    bool metal = (ring >= 12 && ring <= 36) || (abs(dx) <= 2 && y >= 10 && y <= 36)
        || (y >= 15 && y <= 18 && abs(dx) <= 10)
        || (y >= 25 && y <= 37 && abs(abs(dx) - (37 - y)) <= 2)
        || (y >= 23 && y <= 29 && abs(dx) >= 10 && abs(dx) <= 14);
    if (!metal) return 0;
    return (x + 2 * y) % 13 < 3 ? 0x9e704e : x < 15 ? 0x9dafad : 0x526f79;
}
static uint32_t locked_rgb(uint16_t v);
static uint32_t shop_art_color(uint32_t rgb, bool lit, uint32_t background) {
    return lit ? rgb : mix(background, locked_rgb(rgb565(rgb, 1)), 179 / 255.0f);
}
static void draw_ornament(ctx_t *c, int item, int x0, int y0, int scale, bool final, bool lit, uint32_t background) {
    for (int y = 0; y < tank_ornament_h(item); y++) for (int x = 0; x < tank_ornament_w(item); x++) {
        uint32_t rgb = ornament_pixel(item, x, y);
        if (!rgb) continue;
        rgb = shop_art_color(rgb, lit, background);
        src_t color = src_color(rgb, c->dim);
        int left = x0 + x * scale, top = y0 + y * scale;
        for (int row = 0; row < scale; row++) {
            if (final) span_final(c, left, left + scale - 1, top + row, &color, 255);
            else span(c, left, left + scale - 1, top + row, &color, 255);
        }
    }
}
static void rect_fill(ctx_t *c, int x, int y, int w, int h, uint32_t rgb);
static void draw_buoy_icon(ctx_t *c, int x0, int y0, int scale, bool lit, uint32_t background) {
    for (int y = 0; y < 32; y++) for (int x = 0; x < 36; x++) {
        uint32_t rgb = ornament_pixel(13, x, y);
        if (rgb) rect_fill(c, x0 + x * scale, y0 + y * scale, scale, scale, shop_art_color(rgb, lit, background));
    }
    rect_fill(c, x0 + (17 + g_buoy_sway) * scale, y0 + 30 * scale, scale, 6 * scale, shop_art_color(0xb7ae87, lit, background));
    rect_fill(c, x0 + (12 + g_buoy_sway) * scale, y0 + 36 * scale, 10 * scale, 3 * scale, shop_art_color(0x70766e, lit, background));
}
static void draw_ornament_icon(ctx_t *c, int item, int x0, int y0, int size, bool lit, uint32_t background) {
    int w = tank_ornament_w(item), h = tank_ornament_h(item);
    if (item == 21) h = 56;
    int extent = w > h ? w : h;
    for (int y = 0; y < size; y++) for (int x = 0; x < size; x++) {
        int sx = x * extent / size, sy = y * extent / size;
        if (sx >= w || sy >= h) continue;
        uint32_t rgb = ornament_pixel(item, sx, sy);
        if (rgb) rect_fill(c, x0 + x, y0 + y, 1, 1, shop_art_color(rgb, lit, background));
    }
}
void render_ornament_preview(uint16_t *fb, int stride, int item, int cx, int bottom) {
    ctx_t c = ctx_page(fb, stride);
    if (item == 13) { draw_buoy_icon(&c, cx - 18, bottom - 39, 1, true, 0); return; }
    if (item >= 16) { draw_ornament_icon(&c, item, cx - 24, bottom - 48, 48, true, 0); return; }
    draw_ornament(&c, item, cx - tank_ornament_w(item) / 2, bottom - tank_ornament_h(item), 1, false, true, 0);
}
#define PIECES_MAX (DECOR_SLOTS - 1)
typedef struct {
    int  item;                            /* castle, coral, cluster, pumpkin, or chest */
    int  cx, z;                           /* its centre on the floor, its depth */
    bool placing;                         /* dragged on its page this frame */
    bool baked;                           /* in the scene this frame (its mask is this frame's) */
    int  x0, y0, w, rows;                 /* its box: the mask's frame */
    int  mask;                           /* only procedural corals/pumpkin need scratch masks */
} piece_t;
static piece_t g_pc[PIECES_MAX];
static bool g_scene_front;                /* the baked scene holds the pieces IN FRONT */
/* the scene's signature: what the bake depends on (render_tank rebakes on a change) */
typedef struct { int n, item[PIECES_MAX], cx[PIECES_MAX], z[PIECES_MAX], q[PIECES_MAX]; uint32_t col[PIECES_MAX], scenery; float dim; bool front; int lx; } scene_sig_t;
static scene_sig_t g_sig; static bool g_sig_ok;
static bool g_pk_hidden;                  /* director `pumpkin off`: the frame-rate A/B */
void render_set_pumpkin_hidden(bool hidden) { g_pk_hidden = hidden; }
bool render_pumpkin_hidden(void) { return g_pk_hidden; }
static float pc_half_w(int item) { return tank_decor_half_w(item); }
static int   pc_index(int item) { for (int k = 0; k < g_npc; k++) if (g_pc[k].item == item) return k; return -1; }
/* the solid pieces above the lantern cast on the sand (pk_casts; the castle has its own rules) */
static void pk_casts_pieces(const tank_t *t) {
    (void)t;
    for (int k = g_pk_k + 1; k < g_npc; k++) {
        if (g_pc[k].item == 2 || g_pc[k].placing) continue;
        int hw = (int)pc_half_w(g_pc[k].item) - 3;
        pk_cast_add(g_pc[k].cx - hw, g_pc[k].cx + hw, 3);
    }
}
/* this frame's list, from the tank's order; front: the pieces IN FRONT ride in the scene */
static void pieces_build(const tank_t *t, bool cached, bool front) {
    int items[DECOR_SLOTS], n = tank_decor_order(t, items);
    g_npc = 0; g_pk_k = -1; g_castle_cx = -1;
    int mask = 0;
    for (int i = 0; i < n && g_npc < PIECES_MAX; i++) {
        int item = items[i];
        if (item == 0 || (item == 8 && g_pk_hidden)) continue;
        piece_t *p = &g_pc[g_npc];
        p->item = item; p->cx = (int)tank_decor_x(t, item); p->z = tank_decor_z(t, item);
        p->mask = item == 2 || item >= 9 ? -1 : mask++;
        p->placing = setup_is_place() && setup_item() == item;
        p->baked = cached && item != 13 && item != 21 && !p->placing && (p->z != DECOR_Z_FRONT || front);
        switch (item) {
            case 2:  p->x0 = p->cx - CASTLE_MASK_W / 2; p->y0 = CASTLE_FY - CASTLE_ROWS; p->w = CASTLE_MASK_W; p->rows = CASTLE_ROWS + 12; g_castle_cx = p->cx; break;
            case 3:  p->x0 = p->cx - CORAL_W / 2; p->y0 = CORAL_FY - CORAL_H; p->w = CORAL_W; p->rows = CORAL_H + 4; break;
            case 4:  p->x0 = p->cx - CL_W / 2; p->y0 = CL_FY - CL_H; p->w = CL_W; p->rows = CL_H + 4; break;
            case 9: case 10: case 11: case 12: case 13: case 16: case 17: case 18: case 19: case 20: case 21:
                p->w = tank_ornament_w(item); p->rows = tank_ornament_h(item);
                p->x0 = p->cx - p->w / 2; p->y0 = TANK_BOT - 14 + DECOR_SINK - p->rows; break;
            default: p->x0 = p->cx - PK_W / 2; p->y0 = PK_FY - PK_H; p->w = PK_W; p->rows = PK_H + 4; break;
        }
        g_npc++;
    }
    /* depth first: every piece BEHIND, then every piece IN FRONT, each in its rank order */
    for (int i = 1; i < g_npc; i++) {
        piece_t v = g_pc[i]; int j = i;
        while (j > 0 && g_pc[j - 1].z > v.z) { g_pc[j] = g_pc[j - 1]; j--; }
        g_pc[j] = v;
    }
    g_pk_k = pc_index(8);
}
/* piece k's silhouette at (x, y): its recorded mask (this frame's scene), or the castle's static one */
static bool pc_covers(int k, int x, int y) {
    const piece_t *p = &g_pc[k];
    int lx = x - p->x0, r = y - p->y0;
    if ((unsigned)lx >= (unsigned)p->w || (unsigned)r >= (unsigned)p->rows) return false;
    if (p->item == 2) return (g_castle_mask[r][lx >> 5] >> (lx & 31)) & 1;
    if (p->item >= 9) return p->baked && ornament_pixel(p->item, lx, r) != 0;
    return p->baked && ((ds()->pm[p->mask][r][lx >> 5] >> (lx & 31)) & 1);
}
/* ... and the part of it that stands over the fish (a piece IN FRONT whole; the castle's front row) */
static bool pc_front_covers(int k, int x, int y) {
    const piece_t *p = &g_pc[k];
    if (p->z != DECOR_Z_FRONT) return false;
    if (p->item == 2) {
        int lx = x - p->x0, r = y - p->y0;
        return (unsigned)lx < (unsigned)p->w && (unsigned)r < (unsigned)p->rows && ((castle_front()[r][lx >> 5] >> (lx & 31)) & 1);
    }
    return pc_covers(k, x, y);
}
static bool pc_in_box(int k, int x0, int x1, int y) {
    const piece_t *p = &g_pc[k];
    return y >= p->y0 && y < p->y0 + p->rows && x1 >= p->x0 && x0 < p->x0 + p->w;
}
/* n (<= 32) bits of a bit row from bit l on; bits outside 0..nw*32-1 read 0 */
static inline uint32_t row_bits(const uint32_t *row, int nw, int l, int n) {
    int sh = 0;
    if (l < 0) { sh = -l; if (sh >= n) return 0; n -= sh; l = 0; }
    int w = l >> 5;
    if (w >= nw) return 0;
    uint64_t v = row[w];
    if (w + 1 < nw) v |= (uint64_t)row[w + 1] << 32;
    uint32_t out = (uint32_t)(v >> (l & 31));
    if (n < 32) out &= (1u << n) - 1;
    return out << sh;
}
static uint32_t pc_row_bits(int k, int x0, int n, int y) {
    const piece_t *p = &g_pc[k]; int r = y - p->y0;
    if ((unsigned)r >= (unsigned)p->rows) return 0;
    if (p->item >= 9) {
        uint32_t bits = 0;
        if (p->baked) for (int i = 0; i < n; i++) if (ornament_pixel(p->item, x0 + i - p->x0, r)) bits |= 1u << i;
        return bits;
    }
    const uint32_t *row = p->item == 2 ? g_castle_mask[r] : p->baked ? ds()->pm[p->mask][r] : NULL;
    return row ? row_bits(row, (p->w + 31) / 32, x0 - p->x0, n) : 0;
}
static bool pc_covered_above(int k, int x, int y) {
    for (int j = k + 1; j < g_npc; j++) if ((g_pc[j].baked || g_pc[j].item == 2) && pc_covers(j, x, y)) return true;
    return false;
}
/* the pieces a bed's spans leave alone (veg_span) */
static uint32_t veg_skip_for(const tank_t *t, int b) {
    int bz = bed_z(t, b); uint32_t m = 0;
    for (int k = 0; k < g_npc; k++) {
        if (!g_pc[k].baked && g_pc[k].item != 2) continue;              /* drawn live this frame: over the grass anyway, or dragged */
        if (g_pc[k].z == DECOR_Z_FRONT && bz != DECOR_Z_FRONT) m |= 1u << k;             /* in front of the plant layer */
        else if (b == 3 && bz != DECOR_Z_MIDDLE && g_pc[k].z == bz && tank_decor_above(t, g_pc[k].item, 0)) m |= 1u << k;   /* at the plant's depth, ranked above it */
    }
    return m;
}
/* a piece IN FRONT back over what was drawn on it this frame (the scene has it) */
static void pc_restore(uint16_t *fb, int k) {
    const piece_t *p = &g_pc[k];
    int xa = p->x0 < 0 ? 0 : p->x0, xb = p->x0 + p->w - 1 > TANK_W - 1 ? TANK_W - 1 : p->x0 + p->w - 1;
    int ya = p->y0 < 0 ? 0 : p->y0, yb = p->y0 + p->rows - 1 > TANK_H - 1 ? TANK_H - 1 : p->y0 + p->rows - 1;
    for (int y = ya; y <= yb; y++) {
        uint32_t *drow = g_dirty + y * DIRTY_WORDS_PER_ROW;
        for (int w = xa >> 5; w <= xb >> 5; w++) {
            uint32_t bits = drow[w];
            while (bits) {
                int x = (w << 5) + __builtin_ctz(bits); bits &= bits - 1;
                if (x < xa || x > xb || !pc_front_covers(k, x, y)) continue;
                fb[y * TANK_W + x] = g_scene[y * TANK_W + x];
                drow[w] &= ~(1u << (x & 31));
            }
        }
    }
}
/* one piece, whole: in the bake (final, recording its mask) or live */
static void pc_draw(ctx_t *c, const tank_t *t, int k, bool final, bool record) {
    piece_t *p = &g_pc[k];
    if (record && p->mask >= 0) {
        memset(ds()->pm[p->mask], 0, sizeof ds()->pm[p->mask]);
        g_rec = &ds()->pm[p->mask][0][0]; g_rec_x0 = p->x0; g_rec_y0 = p->y0; g_rec_w = p->w; g_rec_rows = p->rows; g_rec_wpr = (CASTLE_MASK_W + 31) / 32;
    }
    g_pk_shade_all = g_pk_lx >= 0 && g_pk_k >= 0 && k > g_pk_k && p->item != 2;   /* above the lantern: backlit (the castle has its own rules: the light through its arch and windows) */
    switch (p->item) {
        case 2:  draw_castle(c, p->cx, 0, final); if (p->z == DECOR_Z_BACK) arch_shade(c, k, p->cx, t->night, false); break;
        case 3:  draw_coral(c, p->cx, tank_coral_rgb(t), tank_coral_growth(t), final); break;
        case 4:  draw_cluster(c, p->cx, tank_cluster_scheme(t), tank_cluster_growth(t), final); break;
        case 9: case 10: case 11: case 12: case 13: case 16: case 17: case 18: case 19: case 20: case 21: draw_ornament(c, p->item, p->x0, p->y0, 1, final, true, 0); break;
        default: draw_pumpkin(c, p->cx, t->night, t->clock, final, true, final); break;
    }
    g_pk_shade_all = false;
    g_rec = NULL;
}
/* a piece's overlay, drawn every frame over the baked body: a crown's
 * tentacles - clipped by the pieces above it (g_clip_k); its rect joins the
 * vignette sweep */
#define PC_CROWN(k) do { int qx0 = 0, qy0 = 0, qx1 = -1, qy1 = 0; \
        if (g_pc[k].item == 3) draw_coral_crown(&c, g_pc[k].cx, tank_coral_rgb(t), tank_coral_growth(t), t->clock, &qx0, &qy0, &qx1, &qy1); \
        else if (g_pc[k].item == 4) draw_cluster_crown(&c, g_pc[k].cx, tank_cluster_scheme(t), tank_cluster_growth(t), t->clock, &qx0, &qy0, &qx1, &qy1); \
        if (qx1 >= qx0) DYN_RECT(qx0, qy0, qx1, qy1); } while (0)
#define PK_LIGHT_Y (PK_FY - (int)(8.0f * PK_S * PK_CELL))   /* the face's middle: where the light leaves */

/* the floor's colour at (x,y), from a position hash so it is stable every
 * frame. The rectangle: a 14 px strip of speckled stones in four tones. The
 * bowl: the same speckle in 2 px grains over a bed that runs ~70 px down to
 * the glass, pebbles scattered through it, darkening with depth. */
static inline uint32_t floor_rgb(int x, int y) {
    uint32_t h2 = (uint32_t)((x * 73856093u) ^ (y * 19349663u));
    uint32_t tone = (h2 >> 4) % 16;
    if (g_sand_style == SAND_STYLE_RAINBOW) {
        uint32_t color = rainbow_rgb((float)x / (TANK_W - 1));
        return mix(color, tone < 5 ? 0xffffff : 0x000000, tone < 2 ? 0.25f : tone < 8 ? 0.1f : 0.25f);
    }
    return SAND_PALETTE[g_sand_style][tone < 2 ? 0 : tone < 5 ? 1 : tone < 8 ? 2 : 3];
}
static uint32_t backdrop_rgb(int x, int y, uint32_t water) {
    int floor = TANK_BOT - 14;
    if (g_journal_theme == 1) {
        water = mix(water, 0x075b45, 0.6f);
        int stem = x % 32, bend = (int)(sinf(y * 0.08f) * 3);
        int height = 75 + (x / 32 % 4) * 21;
        int leaf_x = stem - 15 - bend - (y / 24 % 2 ? 6 : -6), leaf_y = y % 24 - 12;
        if (y > floor - height && (abs(stem - 15 - bend) <= 2 ||
            (leaf_x * leaf_x * 2 + leaf_y * leaf_y < 80)))
            return mix(water, x / 32 % 2 ? 0x3ebd71 : 0x167c51, 0.8f);
        if ((x + y / 2) % 51 < 5) return mix(water, 0x9adf83, 0.12f);
    } else if (g_journal_theme == 2) {
        water = mix(water, 0x125b79, 0.65f);
        int dx = x % 54 - 27, rise = floor - y, peak = 50 + (x / 54 % 3) * 17;
        if (rise > 0 && rise < peak && (abs(dx) < 3 ||
            (rise > 12 && abs(abs(dx) - (rise - 12) / 2) <= 3 && abs(dx) < 25) ||
            (rise > 32 && abs(abs(dx) - (rise - 32) / 2) <= 2 && abs(dx) < 17) ||
            (rise < 12 && dx * dx + rise * rise < 190)))
            return mix(water, x / 54 % 2 ? 0xef77b8 : 0xff9c74, 0.85f);
    } else if (g_journal_theme == 3) {
        water = mix(water, 0x140c40, 0.85f);
        uint32_t hash = (uint32_t)(x * 73856093u) ^ (uint32_t)(y * 19349663u);
        if (y > 22 && y < floor && hash % 277u < 3) return mix(water, hash & 1 ? 0x6eefff : 0xe18cff, 0.9f);
        int dx = x % 43 - 21, rise = floor - y;
        if (rise > 0 && rise < 22 + x / 43 % 3 * 7 && abs(dx) < (28 - rise) / 4)
            return mix(water, 0xa275ef, 0.8f);
        int jx = x % 83 - 41, jy = y - (32 + x / 83 % 3 * 18);
        if (jy >= -11 && jy <= 0 && jx * jx + jy * jy < 144)
            return mix(water, 0x64e8ef, 0.75f);
        if (jy > 0 && jy < 28 && abs(jx) < 10 && (jx + jy / 5) % 5 == 0)
            return mix(water, 0xc388ef, 0.65f);
    }
    return water;
}
static void draw_floor_scenery(ctx_t *c, const tank_t *t, bool final) {
    static const int items[] = {22, 23, 25, 26, 27};
    for (int i = 0; i < 5; i++) if (t->sd_unlocks & (UINT32_C(1) << items[i]))
        draw_ornament(c, items[i], (i + 1) * TANK_W / 6 - 20, TANK_BOT - 48, 1, final, true, 0);
}
static void draw_scene(const tank_t *t, uint16_t *fb, int stride, float dim) {
    ctx_t c = ctx_full(fb, stride, dim);
    /* water gradient #0a3c46 → #08272f → #031015 */
    for (int y = 0; y < TANK_H; y++) {
        uint32_t water = water_rgb(y);
        if (!g_journal_theme) {
            uint16_t color = rgb565(water, dim);
            for (int x = 0; x < TANK_W; x++) fb[y * stride + x] = color;
        } else for (int x = 0; x < TANK_W; x++) fb[y * stride + x] = rgb565(backdrop_rgb(x, y, water), dim);
    }
    /* pebbled bottom: irregular top edge, speckled stones in three tones.
       All variation comes from a position hash so it is stable every frame. */
    for (int x = 0; x < TANK_W; x++) {
        uint32_t h = (uint32_t)(x * 2654435761u);
        int top = TANK_BOT - 14 - (int)((h >> 8) % 5);
        for (int y = top; y < TANK_H; y++) {
            uint32_t col = floor_rgb(x, y);
            fb[y * stride + x] = rgb565(col, dim);
        }
    }
    /* (the dark oval under the left bed - the "reef rock" - is gone, 2026-10-02:
       it stood for nothing; the reef is the cluster the keeper buys) */
    draw_floor_scenery(&c, t, false);
    for (int k = 0; k < g_npc; k++) if (!g_pc[k].placing) pc_draw(&c, t, k, false, false);   /* uncached: every piece, in order, every frame */
}


/* The baked scene (scene cache mode). Water gradient x porthole vignette
 * computed in 8-bit and ORDERED-DITHERED to RGB565 (4x4 Bayer: at 322 ppi
 * the pattern is invisible, the 5/6-bit banding of a dark gradient and of
 * the vignette falloff is not - Strato saw it at the edges), then the floor
 * and the backdrop decor on top, vignetted per pixel. Fills the vignette LUT on the
 * way. Bake-only: fidelity here costs nothing per frame. The light shafts
 * are gone (2026-09-01, Strato: they never looked good on this screen). */
static void bake_scene(const tank_t *t, uint16_t *sc, float dim) {
    static const uint8_t bayer[4][4] = { {0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5} };
    for (int y = 0; y < TANK_H; y++) {
        uint32_t water = water_rgb(y);
        for (int x = 0; x < TANK_W; x++) {
            uint32_t col = backdrop_rgb(x, y, water);
            int r = (int)(((col >> 16) & 255) * dim), g = (int)(((col >> 8) & 255) * dim), b = (int)((col & 255) * dim);
            int a = vig_alpha(x, y);
            if (g_vig) g_vig[y * TANK_W + x] = (uint8_t)a;
            int inv = 256 - a, d = bayer[y & 3][x & 3];
            int rr = (r * inv) >> 8, gg = (g * inv) >> 8, bb = (b * inv) >> 8;
            int r5 = (rr + (d >> 1)) >> 3, g6 = (gg + (d >> 2)) >> 2, b5 = (bb + (d >> 1)) >> 3;
            if (r5 > 31) r5 = 31;
            if (g6 > 63) g6 = 63;
            if (b5 > 31) b5 = 31;
            sc[y * TANK_W + x] = (uint16_t)((r5 << 11) | (g6 << 5) | b5);
        }
    }
    /* pebbled bottom: irregular top edge, speckled stones in three tones,
       stable position hash; vignetted in 8-bit before quantising */
    for (int x = 0; x < TANK_W; x++) {
        uint32_t h = (uint32_t)(x * 2654435761u);
        int top = TANK_BOT - 14 - (int)((h >> 8) % 5);
        for (int y = top; y < TANK_H; y++) {
            uint32_t col = floor_rgb(x, y);
            int inv = 256 - (g_vig ? g_vig[y * TANK_W + x] : vig_alpha(x, y));
            int r = (int)(((col >> 16) & 255) * dim) * inv >> 8;
            int g = (int)(((col >> 8) & 255) * dim) * inv >> 8;
            int b = (int)((col & 255) * dim) * inv >> 8;
            sc[y * TANK_W + x] = (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
        }
    }
    if (g_pk_lx >= 0) pk_halo(sc);                 /* the lantern's light on the water and the floor (the decor lights itself) */
    ctx_t c = ctx_full(sc, TANK_W, dim);
    draw_floor_scenery(&c, t, true);
    for (int k = 0; k < g_npc; k++)                 /* the pieces, back to front, each recording its mask */
        if (g_pc[k].baked) pc_draw(&c, t, k, true, true);
}

void render_tank(const tank_t *t, uint16_t *fb, int stride) {
    uint8_t water = (t->sd_unlocks & SD_ITEM_LIGHTS) && t->water_style < WATER_STYLE_COUNT ? t->water_style : 0;
    uint8_t sand = (t->sd_unlocks & SD_ITEM_SAND) && t->sand_style < SAND_STYLE_COUNT ? t->sand_style : 0;
    uint8_t theme = journal_theme_unlocked(t, t->journal.theme) ? t->journal.theme : 0;
    if (water != g_water_style || sand != g_sand_style || theme != g_journal_theme) {
        g_water_style = water; g_sand_style = sand;
        g_journal_theme = theme;
        g_sig_ok = false;
        g_veg_row_dim = g_castle_dim = g_coral_dim = g_pk_dim = g_cl_dim = -1;
    }
    g_buoy_sway = (int)(sinf(t->clock * 0.8f) * 4);
    g_buoy_bob = (int)(sinf(t->clock * 1.2f) * 3);
    float dim = t->night ? 0.45f : 1.0f;
    ctx_t c = ctx_full(fb, stride, dim);
    int64_t p0 = PROF_MARK();
    bool cached = g_scene && g_dirty && stride == TANK_W;
    g_dirty_hold = false;                          /* the tank draws in the frame's coordinates: its marks count */
    if (cached) memset(g_dirty, 0, TANK_H * DIRTY_WORDS_PER_ROW * sizeof(uint32_t));
    struct { short x0, y0, x1, y1; } rects[9 + MAX_FOOD + MAX_BUBBLE + N_FISH_MAX + VEG_BEDS_MAX + SHRIMP_MAX];
    int nr = 0;
#define DYN_RECT(cx0, cy0, cx1, cy1) do { if (cached && nr < (int)(sizeof rects / sizeof rects[0])) { \
        rects[nr].x0 = (short)(cx0); rects[nr].y0 = (short)(cy0); \
        rects[nr].x1 = (short)(cx1); rects[nr].y1 = (short)(cy1); nr++; } } while (0)

    bool front = cached && !setup_is_place();      /* the pieces IN FRONT ride in the scene, but for a placement page's drag */
    pieces_build(t, cached, front);
    int pkx = g_pk_k >= 0 ? g_pc[g_pk_k].cx : -1, pkz = g_pk_k >= 0 ? g_pc[g_pk_k].z : DECOR_Z_BACK;
    bool pk_in_scene = g_pk_k >= 0 && !g_pc[g_pk_k].placing;
    g_pk_lx = t->night && pk_in_scene ? pkx : -1; g_pk_ly = PK_LIGHT_Y;     /* the lantern's light, this frame */
    if (g_pk_lx >= 0) pk_lut_fill();
    { int kc = pc_index(2); g_veg_mask_cx = kc >= 0 && g_pc[kc].z == DECOR_Z_FRONT ? g_pc[kc].cx : -1;   /* the castle IN FRONT: its arch's passes */
      g_pk_under_castle = kc >= 0 && g_pk_k >= 0 && kc > g_pk_k; }                                        /* the castle over the lantern: its shadow's rules */
    g_pk_shadows = g_pk_lx >= 0 && (pkz == DECOR_Z_BACK || g_pk_k < g_npc - 1);                           /* something stands in front of the lantern */
    if (g_pk_shadows) pk_casts(t, pkx); else g_pk_cast_n = 0;
    if (cached) {
        /* the scene's signature: what the bake depends on; a change rebakes */
        scene_sig_t sig; memset(&sig, 0, sizeof sig);
        for (int k = 0; k < g_npc; k++) {
            if (!g_pc[k].baked) continue;
            int n = sig.n++;
            sig.item[n] = g_pc[k].item; sig.cx[n] = g_pc[k].cx; sig.z[n] = g_pc[k].z;
            if (g_pc[k].item == 3) { sig.q[n] = (int)(tank_coral_growth(t) * (CORAL_Q - 0.01f)); sig.col[n] = tank_coral_rgb(t); }
            if (g_pc[k].item == 4) { float lg = tank_cluster_growth(t); sig.q[n] = (int)((lg > 1 ? 1 : lg) * (CL_Q - 0.01f)); sig.col[n] = (uint32_t)tank_cluster_scheme(t); }
        }
        sig.dim = dim; sig.front = front; sig.lx = g_pk_lx;
        sig.scenery = t->sd_unlocks & (SD_ITEM_PEBBLES | SD_ITEM_PEARL | SD_ITEM_SPONGES | SD_ITEM_BARREL | SD_ITEM_HELMET);
        if (!g_sig_ok || memcmp(&sig, &g_sig, sizeof sig) || g_scene_dim != dim) {
            g_sig = sig; g_sig_ok = true; g_scene_front = front;
            /* rebuild the static scene with the vignette baked in (the
               per-frame pass then only re-darkens dynamic patches) */
            bake_scene(t, g_scene, dim);
            if (g_vig) g_vig_filled = true;
            g_scene_dim = dim; g_scene_epoch++;
        }
        if (!(g_primed_fb == fb && g_primed_epoch == g_scene_epoch))
            memcpy(fb, g_scene, TANK_W * TANK_H * sizeof(uint16_t));
        g_primed_fb = NULL;
        /* a piece BEHIND that is not in the scene (dragged): live, right over it;
           the castle too, whole, when it is not baked (its front row again after
           the fish, below) */
        for (int k = 0; k < g_npc; k++)
            if (!g_pc[k].baked && (g_pc[k].z == DECOR_Z_BACK || g_pc[k].item == 2)) pc_draw(&c, t, k, true, false);
    } else draw_scene(t, fb, stride, dim);
    {
        static const int items[] = {28, 24, 29};
        for (int i = 0; i < 3; i++) if (t->sd_unlocks & (UINT32_C(1) << items[i])) {
            int x = (i + 1) * TANK_W / 4 - 20;
            int y = 2 + (int)(sinf(t->clock * 0.9f + i * 2) * 2);
            draw_ornament(&c, items[i], x, y, 1, false, true, 0);
            DYN_RECT(x, y, x + 39, y + 39);
        }
    }
    /* the overlays of the pieces BEHIND: a crown over its baked fan, the
       candle's face over the lantern's body - under the grass */
    for (int k = 0; k < g_npc; k++) {
        if (g_pc[k].z != DECOR_Z_BACK) continue;
        g_clip_k = k;
        if (g_pc[k].item == 3 || g_pc[k].item == 4) PC_CROWN(k);
        else if (g_pc[k].item == 8 && g_pc[k].baked) draw_pumpkin_glow(&c, g_pc[k].cx, t->clock, t->night, false, true);
        g_clip_k = -1;
    }
    PROF_ADD(0, p0);

    PROF_ADD(1, p0);   /* stage 1 (light shafts) retired 2026-09-01 */
    /* vegetation, BACK layer: the reef bed (left - milestone lushness widens
       its base, never withers) and two decor beds; all three keep growing up
       and out with tank_t.veg_growth until the keeper trims them (slash the
       canopy). Geometry comes from tank_veg_bed so physics and pixels agree.
       The alternating FRONT fronds draw after the fish, below. Each bed's
       spans skip the pieces above it (veg_skip_for), and a bed in front of
       the lantern is backlit (g_pk_shade_all). */
    const int *veg_seed = VEG_SEED;
#define VEG_PASS(b, layer) do { g_veg_skip = veg_skip_for(t, (b)); \
        g_pk_shade_all = g_pk_lx >= 0 && ((b) == 3 ? tank_decor_above(t, 0, 8) : pkz == DECOR_Z_BACK); \
        draw_veg(&c, t, (b), veg_seed[(b)], (layer), cached); g_pk_shade_all = false; } while (0)
    for (int b = 0; b < tank_veg_beds(t); b++)          /* a BACK-layer piece first: behind the grass too */
        if (bed_z(t, b) == DECOR_Z_BACK) VEG_PASS(b, -1);
    for (int b = 0; b < tank_veg_beds(t); b++)
        if (bed_z(t, b) == DECOR_Z_MIDDLE) VEG_PASS(b, 0);
        /* no DYN_RECT: with the scene cache each frond span vignettes itself */
    if (g_veg_mask_cx >= 0) {                        /* a castle IN FRONT: inside its open arch the late fronds go before the fish */
        g_veg_arch_pass = -1;
        for (int b = 0; b < tank_veg_beds(t); b++)
            if (bed_z(t, b) == DECOR_Z_MIDDLE) VEG_PASS(b, 1);
        for (int b = 0; b < tank_veg_beds(t); b++)
            if (bed_z(t, b) == DECOR_Z_FRONT) VEG_PASS(b, -1);
        g_veg_arch_pass = 0;
    }
    PROF_ADD(2, p0);
    /* the airstone the column rises from, on the floor where the keeper put
       it (setup): three stones and a glint - dynamic, since it can move */
    {
        float ax = t->bubble_x, ay = TANK_BOT - 17;
        fill_ellipse(&c, ax - 6, ay + 1, 7, 4, 0x2a3634, 255);
        fill_ellipse(&c, ax + 5, ay + 2, 6, 3.5f, 0x22302c, 255);
        fill_ellipse(&c, ax, ay - 2, 6, 3.5f, 0x3a4a48, 255);
        fill_ellipse(&c, ax - 1, ay - 3, 2, 1.2f, 0x5a6a68, 200);
        DYN_RECT((int)ax - 14, (int)ay - 7, (int)ax + 12, (int)ay + 7);
    }
    /* food pellets */
    for (int i = 0; i < MAX_FOOD; i++)
        if (t->food[i].alive) {
            fill_ellipse(&c, t->food[i].x, t->food[i].y, 2.6f, 2.6f, 0xffbd59, 255);
            fill_ellipse(&c, t->food[i].x - 0.8f, t->food[i].y - 0.8f, 1.0f, 1.0f, 0xffe9bd, 255);
            DYN_RECT((int)t->food[i].x - 5, (int)t->food[i].y - 5, (int)t->food[i].x + 5, (int)t->food[i].y + 5);
        }
    /* bubbles */
    for (int i = 0; i < MAX_BUBBLE; i++) {
        const bubble_t *b = &t->bubble[i];
        float r = b->column ? 2.6f : 1.8f;
        fill_ellipse(&c, b->x, b->y, r, r, 0x9fd8e2, 60);
        px_blend(&c, (int)(b->x - r * 0.4f), (int)(b->y - r * 0.4f), 0xffffff, 120);
        DYN_RECT((int)b->x - 5, (int)b->y - 5, (int)b->x + 5, (int)b->y + 5);
    }
    /* the shrimp school: on the sand and in the grass, under the fish and
       the front fronds (the grass is their cover) */
    if (t->sd_unlocks & SD_ITEM_SHRIMP) {
        src_t pal[SHRIMP_TONES];
        for (int k = 0; k < SHRIMP_TONES; k++) pal[k] = src_color(SHRIMP_RGB[k], c.dim);
        for (int i = 0; i < t->shrimp_n; i++) {
            draw_shrimp(&c, &t->shrimp[i], pal);
            DYN_RECT((int)t->shrimp[i].x - 10, (int)t->shrimp[i].y - 5, (int)t->shrimp[i].x + 10, (int)t->shrimp[i].y + 5);
        }
    }
    /* the seahorse's BACK lane, before the fish (2026-10-17, Strato: a fish
       passed between the seahorse and the castle it held): it holds something
       behind them - an even frond, a piece BEHIND - so the fish pass in front
       of it, as they pass its hold. Its FRONT lane (an odd frond, a piece IN
       FRONT) is drawn after the pieces in front, below. Its rect either way. */
    if ((t->sd_unlocks & SD_ITEM_SEAHORSE) && t->sh_x >= 0) {
        if (!t->sh_front) draw_seahorse(&c, t);
        int bx0, by0, bx1, by1; seahorse_box(t, &bx0, &by0, &bx1, &by1);
        DYN_RECT(bx0, by0, bx1, by1);
    }
    PROF_ADD(3, p0);
    /* fish */
    for (int i = 0; i < t->n_fish; i++) {
        g_bb_on = true; g_bb_x0 = g_bb_y0 = 1 << 20; g_bb_x1 = g_bb_y1 = -1;
        draw_fish(&c, t, &t->fish[i]);
        g_bb_on = false;
        if (g_bb_x1 >= g_bb_x0) DYN_RECT(g_bb_x0, g_bb_y0, g_bb_x1, g_bb_y1);
    }
    /* the snail on the floor (upright): in the scene, under the front fronds -
       unless it walks its front lane (tank_t.snail_front), drawn below, over
       the pieces placed IN FRONT */
    if (tank_snail_upright(t)) {
        if (!t->snail_front) draw_snail(&c, t, true);
        DYN_RECT((int)t->snail_x - 21, (int)t->snail_y - 13, (int)t->snail_x + 21, (int)t->snail_y + 7);
    }
    /* the urchin on the floor: with the fish, under the front fronds - at a
       bed's foot the grass weaves over it */
    if ((t->sd_unlocks & SD_ITEM_URCHIN) && t->urchin_x >= 0) {
        draw_urchin(&c, t);
        DYN_RECT((int)t->urchin_x - 19, (int)URCHIN_FLOOR_Y - 17, (int)t->urchin_x + 19, (int)(URCHIN_FLOOR_Y + UR_BASE) + 1);
    }
    /* the pieces IN FRONT back over the fish (and the snail, the food, the
       bubbles), only where they were drawn: the scene holds them. The castle's
       front row comes back the same way - a fish in the arch swims THROUGH,
       its keep stays behind the fish. A castle IN FRONT that is not in the
       scene (dragged, or a page up): its front row again, live. */
    for (int k = 0; k < g_npc; k++) {
        if (g_pc[k].z != DECOR_Z_FRONT) continue;
        if (g_pc[k].baked) pc_restore(fb, k);
        else if (g_pc[k].item == 2) draw_castle(&c, g_pc[k].cx, 1, cached);
    }
    /* vegetation, FRONT layer: the alternating fronds drawn over the fish,
       so a fish inside a canopy is woven through it (each span applies its
       own vignette and untags itself, so the fish rects below skip it) */
    if (g_veg_mask_cx >= 0) g_veg_arch_pass = 1;     /* (the arch's pixels were drawn before the fish) */
    for (int b = 0; b < tank_veg_beds(t); b++)
        if (bed_z(t, b) == DECOR_Z_MIDDLE) VEG_PASS(b, 1);
    for (int b = 0; b < tank_veg_beds(t); b++)          /* a FRONT-layer piece last: over the grass and the fish */
        if (bed_z(t, b) == DECOR_Z_FRONT) VEG_PASS(b, -1);
    g_veg_arch_pass = 0;
#undef VEG_PASS
    /* the pieces IN FRONT that are not in the scene (a placement page is up,
       or no scene cache): live, in order, over everything; then every piece
       IN FRONT's overlay - its crown, the candle - clipped by the pieces
       above it */
    for (int k = 0; k < g_npc; k++)
        if (g_pc[k].z == DECOR_Z_FRONT && !g_pc[k].baked && g_pc[k].item != 2) pc_draw(&c, t, k, cached, false);
    for (int k = 0; k < g_npc; k++) {
        if (g_pc[k].z != DECOR_Z_FRONT) continue;
        g_clip_k = k;
        if (g_pc[k].item == 3 || g_pc[k].item == 4) PC_CROWN(k);
        else if (g_pc[k].item == 8 && g_pc[k].baked) draw_pumpkin_glow(&c, g_pc[k].cx, t->clock, t->night, false, true);
        g_clip_k = -1;
    }
    if (tank_snail_upright(t) && t->snail_front) draw_snail(&c, t, true);   /* its front lane: over them (its rect is marked above) */
    if ((t->sd_unlocks & SD_ITEM_SEAHORSE) && t->sh_x >= 0 && t->sh_front) draw_seahorse(&c, t);   /* the seahorse's front lane (its rect is marked above) */
    { int kc = pc_index(2); if (kc >= 0 && g_pc[kc].z == DECOR_Z_FRONT) arch_shade(&c, kc, g_pc[kc].cx, t->night, true); }   /* the tunnel through a castle IN FRONT: shaded, whatever passes it */
    PROF_ADD(4, p0);
    /* porthole vignette: darken corners toward AMOLED black. With a scene
       cache the full-frame pass is baked into the scene and only the dynamic
       patches are re-darkened; without one, the classic per-pixel pass runs. */
    if (cached && g_pk_lx >= 0) pk_light_dirty(&c);   /* the lantern lights what moved near it (before its vignette) */
    if (cached) {
        /* one row sweep over the union of the dynamic rects: pixels marked
           in the dirty mask were drawn this frame (and not already vignetted
           inline) and get the vignette re-applied exactly once. */
        for (int y = 0; y < TANK_H; y++) {
            float dyf = (y - TANK_H * 0.5f) / (TANK_H * 0.5f);
            float rem = 0.72f - dyf * dyf;
            int x_in = rem > 0 ? (int)(TANK_W * 0.5f * (1 - sqrtf(rem))) : TANK_W / 2;
            if (x_in <= 0) continue;
            short iv[sizeof rects / sizeof rects[0]][2]; int ni = 0;
            for (int i = 0; i < nr; i++)
                if (y >= rects[i].y0 && y <= rects[i].y1) {
                    int a0 = rects[i].x0 < 0 ? 0 : rects[i].x0;
                    int a1 = rects[i].x1 >= TANK_W ? TANK_W - 1 : rects[i].x1;
                    if (a0 > a1) continue;
                    int j = ni++;                       /* insertion sort by x0 */
                    while (j > 0 && iv[j - 1][0] > a0) { iv[j][0] = iv[j - 1][0]; iv[j][1] = iv[j - 1][1]; j--; }
                    iv[j][0] = (short)a0; iv[j][1] = (short)a1;
                }
            int end = -1;                               /* merged sweep */
            for (int i = 0; i < ni; i++) {
                int a0 = iv[i][0] > end + 1 ? iv[i][0] : end + 1;
                int a1 = iv[i][1];
                if (a1 > end) end = a1;
                for (int s = 0; s < 2; s++) {           /* clip to the two ring spans */
                    int r0 = s ? (TANK_W - x_in > a0 ? TANK_W - x_in : a0) : a0;
                    int r1 = s ? a1 : (x_in - 1 < a1 ? x_in - 1 : a1);
                    const uint32_t *drow = g_dirty + y * DIRTY_WORDS_PER_ROW;
                    for (int x = r0; x <= r1; x++) {
                        uint32_t w = drow[x >> 5] >> (x & 31);
                        if (!w) { x |= 31; continue; }         /* nothing else in this word */
                        if (!(w & 1)) continue;
                        uint16_t *p = &c.fb[y * c.stride + x];
                        int a = (g_vig && g_vig_filled) ? g_vig[y * TANK_W + x] : vig_alpha(x, y);
                        if (a) px_darken(p, a);
                    }
                }
            }
        }
    } else {
        for (int y = 0; y < TANK_H; y++) {
            float dy = (y - TANK_H * 0.5f) / (TANK_H * 0.5f);
            float rem = 0.72f - dy * dy;
            int x_in = rem > 0 ? (int)(TANK_W * 0.5f * (1 - sqrtf(rem))) : TANK_W / 2;
            for (int side = 0; side < 2; side++)
                for (int k = 0; k < x_in; k++) {
                    int x = side ? TANK_W - 1 - k : k;
                    float dx = (x - TANK_W * 0.5f) / (TANK_W * 0.5f);
                    float d2 = dx * dx + dy * dy;
                    if (d2 > 0.72f) {
                        int a = (int)((d2 - 0.72f) * 220);
                        if (a > 0) px_blend(&c, x, y, 0x000000, a > 255 ? 255 : a);
                    }
                }
        }
    }
    /* algae film sits ON the glass - over the water, the fish, even the
     * vignette (which is why it draws after the re-darken pass: nothing
     * behind it needs repair, and next frame's scene restore erases wiped
     * cells for free) */
    PROF_ADD(5, p0);
    draw_algae(&c, t);
    draw_snail(&c, t, false);                    /* on the glass, over the film */
    g_pk_lx = -1; g_pk_under_castle = false; g_pk_shadows = false;   /* the light is this frame's: no page is ever lit by it */
    PROF_ADD(6, p0);
    if (render_clock_active(t)) render_clock_overlay(t, fb, stride, g_clock_synchronized ? clock_port_now_unix() : 0);
#undef DYN_RECT
}

/* ---- stats overlay (selection ring + visual card) ---- */

static void ring(ctx_t *c, float cx, float cy, float r, uint32_t rgb) {
    for (int i = 0; i < 64; i++) {
        float a = i * (TAU / 64);
        px_blend(c, (int)(cx + cosf(a) * r), (int)(cy + sinf(a) * r), rgb, 180);
    }
}
static void ring_tank(ctx_t *c, float cx, float cy, float r, uint32_t rgb) {
    ctx_t target = ctx_full(c->fb, c->stride, c->dim);
    ring(&target, cx, cy, r, rgb);
}

/* blend one native RGB565 pixel (icon art is pre-colored; no dim - the card
 * ignores night, matching px_blend's use with c->dim = 1) */
static void px565_blend(ctx_t *c, int x, int y, uint16_t v, int a) {
    if (!CTX_IN(c, x, y)) return;
    uint16_t *p = &CTX_PX(c, x, y);
    int r = (*p >> 11)       + (((v >> 11)       - (*p >> 11))       * a >> 8);
    int g = ((*p >> 5) & 63) + ((((v >> 5) & 63) - ((*p >> 5) & 63)) * a >> 8);
    int b = (*p & 31)        + (((v & 31)        - (*p & 31))        * a >> 8);
    *p = (uint16_t)((r << 11) | (g << 5) | b);
}

/* draw a baked icon (icons.h, generated from Strato's pixel art) at x,y.
 * alpha scales the icon's own alpha plane: 255 = as drawn, lower = dimmed
 * (unrevealed traits, torn thought bubbles). */
static void blit_icon(ctx_t *c, int x, int y, const icon_t *ic, int alpha) {
    ic = icons_resolve(ic);
    for (int j = 0; j < ic->h; j++)
        for (int i = 0; i < ic->w; i++) {
            int a = ic->a[j * ic->w + i];
            if (!a) continue;
            px565_blend(c, x + i, y + j, ic->rgb[j * ic->w + i], a * alpha >> 8);
        }
}

/* a segmented meter: value 0..1 over `segs` segments of `sw` x 10 px. The
 * last partial segment fills proportionally, so it still reads analog up
 * close but "3 of 5" at a glance. */
static void meter(ctx_t *c, int x, int y, int segs, int sw, float frac, uint32_t rgb) {
    if (frac < 0) frac = 0;
    if (frac > 1) frac = 1;
    float per = 1.0f / segs;
    src_t on = src_color(rgb, c->dim), off = src_color(0x2a3f45, c->dim);
    for (int s = 0; s < segs; s++) {
        int sx = x + s * (sw + 2);
        float have = (frac - s * per) / per;                   /* 0..1 of this segment */
        int fw = have >= 1 ? sw : have <= 0 ? 0 : (int)(have * sw + 0.5f);
        for (int yy = 0; yy < 10; yy++) {
            if (fw > 0)  span(c, sx, sx + fw - 1, y + yy, &on, 235);
            if (fw < sw) span(c, sx + fw, sx + sw - 1, y + yy, &off, 150);
        }
    }
}

/* a trait spectrum: pole icons at both ends, the fish sits at `frac` between
 * them. Unrevealed = dimmed poles, dashed line, a "?" instead of the dot -
 * you learn who a fish is by watching it, not by reading it. */
static void slider(ctx_t *c, int x, int y, int w, float frac, uint32_t rgb,
                   const icon_t *lo, const icon_t *hi, bool revealed) {
    blit_icon(c, x, y, lo, revealed ? 255 : 70);
    blit_icon(c, x + w - 16, y, hi, revealed ? 255 : 70);
    int lx = x + 20, lw = w - 40, ly = y + 8;                  /* the line between poles */
    for (int xx = 0; xx < lw; xx++)
        if (revealed || (xx & 4))
            for (int yy = -1; yy <= 0; yy++) px_blend(c, lx + xx, ly + yy, 0x2a3f45, revealed ? 200 : 110);
    if (revealed) {
        if (frac < 0) frac = 0;
        if (frac > 1) frac = 1;
        fill_ellipse(c, lx + lw * frac, ly - 0.5f, 3.4f, 3.4f, rgb, 255);
        fill_ellipse(c, lx + lw * frac - 1, ly - 1.5f, 1.0f, 1.0f, 0xffffff, 200);
    } else
        blit_icon(c, lx + lw / 2 - 8, y, &icon_unknown_16, 220);
}

static void button(ctx_t *c, int x, int y, int W, int H, uint32_t fill, uint32_t edge, const char *label, int scale);   /* below */
static void draw_text(ctx_t *c, int x, int y, int scale, uint32_t rgb, const char *s);                                  /* the pixel font, below */
#define CARD_MORE_H 22
static void card_draw(ctx_t c, const tank_t *t, int fish_idx) {
    const fish_t *f = &t->fish[fish_idx];
    /* card: top-left, bordered in the fish's own color (that's its "name").
     * Two zones: NEEDS (things you can act on now - icon + segmented meter)
     * above the divider, WHO THEY ARE (slow traits - pole-to-pole spectrum
     * sliders) below it. Iconified 2026-08-30 with Strato's pixel art. */
    const int X = RENDER_CARD_X, Y = RENDER_CARD_Y, W = RENDER_CARD_W, H = RENDER_CARD_H; /* x clear of the curved bezel */
    /* backdrop: 28k blended pixels - hoisted (this alone was most of the
     * card's ~12 ms/frame on the device through per-pixel px_blend) */
    src_t bg = src_color(0x04141a, 1.0f);
    for (int y = Y; y < Y + H; y++) span(&c, X, X + W - 1, y, &bg, 215);
    for (int x = X; x < X + W; x++) { px(&c, x, Y, rgb565(f->color, 1)); px(&c, x, Y + H - 1, rgb565(f->color, 1)); }
    for (int y = Y; y < Y + H; y++) { px(&c, X, y, rgb565(f->color, 1)); px(&c, X + W - 1, y, rgb565(f->color, 1)); }

    /* identity row: the fish's NAME and the certainty dot - bright = the
     * model was sure of its last decision, dim = torn. (2026-09-29, Strato:
     * the name at the top; the stage pips and growth bar are gone - "a bit
     * ambiguous", the keeper sees the fish's size, and the milestones page a
     * tap deeper names the stage. The colour swatch went with them: the
     * border is the fish's colour, and a 7-letter name needs the row.) */
    draw_text(&c, X + 8, Y + 8, 2, 0xffffff, f->name);
    fill_ellipse(&c, X + W - 14, Y + 14, 3.2f, 3.2f, 0xffffff, 240);

    /* needs: 5 segments each. Hunger is shown as FULLNESS - a full belly is
     * a full meter, and it drains as the fish gets hungry (a food icon next
     * to a growing bar read backwards) */
    struct { const icon_t *ic; float v; uint32_t rgb; } needs[4] = {
        { &icon_hunger, 10.0f - f->hunger, 0xffbd59 },
        { &icon_energy, f->energy,         0x78d67d },
        { &icon_stress, f->stress,         0xf25b65 },
        { &icon_trust,  f->trust,          0xffd166 },
    };
    for (int i = 0; i < 4; i++) {
        int ry = Y + 30 + i * 27;
        blit_icon(&c, X + 8, ry, needs[i].ic, 255);
        meter(&c, X + 38, ry + 7, 5, 14, needs[i].v / 10.0f, needs[i].rgb);
    }

    /* divider between the zones */
    for (int x = X + 8; x < X + W - 8; x++) px_blend(&c, x, Y + 142, 0x2a3f45, 200);

    /* who they are: spectrum sliders, revealed by behaviour you have seen
       this fish do (docs/progression.md habits) */
    bool saw_bold = f->ms_bits & MS_FIRST_DART;
    bool saw_social = f->ms_bits & MS_FIRST_FOLLOW;
    bool saw_curious = f->ms_bits & (MS_INSPECTED | MS_FIRST_BUBBLES);
    slider(&c, X + 8, Y + 152, W - 16, f->bold,             0xffffff, &icon_shy,      &icon_bold,    saw_bold);
    slider(&c, X + 8, Y + 178, W - 16, f->sociable,         0x38dcc7, &icon_solo,     &icon_social,  saw_social);
    slider(&c, X + 8, Y + 204, W - 16, f->curiosity / 10.0f, 0x6db9ff, &icon_cautious, &icon_curious, saw_curious);

    /* the way onward (2026-09-16): a MORE button in the pages' dress at the
       foot of the card. The whole card was already the tap that opens the
       milestones page (and from there SETTINGS / UPGRADES), but nothing said
       so - a keeper asked Strato how to get there. The button is the sign;
       the hit box is still the card (touch ports, RENDER_CARD_H). */
    button(&c, X + 8, Y + H - 8 - CARD_MORE_H, W - 16, CARD_MORE_H, 0x1c2f36, 0x9fd8e2, "MORE", 2);
}

/* ---- stats card cache (2026-09-01) ----
 * The card cost ~7 ms of every frame it was up (28k blended backdrop pixels
 * + icons + meters) - fps sagged whenever the keeper inspected a fish. With a
 * cache it is redrawn at most 4x a second over the STATIC water under it
 * (from the scene cache, so the translucent backdrop looks as before; only
 * things swimming behind the card stop showing through at 16%) and copied
 * into the frame otherwise. The selection ring follows the fish per frame. */
static uint16_t *g_card = NULL;
static int g_card_fish = -1; static float g_card_t = -1; static unsigned g_card_epoch;
void render_set_card_cache(uint16_t *buf) { g_card = buf; g_card_fish = -1; }

/* the snail's card (2026-09-16, Strato: "tapping the snail show a simple
   card with the upright snail image and how much algae has been grazed so
   far"): a ring on the snail, a centred box in the modal's dress - the
   upright sprite at 2x, SNAIL, the tally. Drawn every frame (no cache: a
   few hundred blended pixels, nothing like the fish card's meters). */
static int  text_w(const char *s, int scale);                                  /* the pixel font, below */
static void draw_text(ctx_t *c, int x, int y, int scale, uint32_t rgb, const char *s);
static void rect_edge(ctx_t *c, int x, int y, int w, int h, uint32_t rgb);
static void blit_icon_scaled(ctx_t *c, int x, int y, const icon_t *ic, int s, bool lit);
static void snail_card_draw(ctx_t *c, const tank_t *t) {
    ring_tank(c, t->snail_x, t->snail_y, 20, 0x9fd8e2);
    const int W = SNAIL_CARD_W, H = SNAIL_CARD_H, X = (c->w - W) / 2, Y = (c->h - H) / 2;
    src_t bg = src_color(0x04141a, 1.0f);
    for (int y = Y; y < Y + H; y++) span(c, X, X + W - 1, y, &bg, 235);
    rect_edge(c, X, Y, W, H, 0x9fd8e2); rect_edge(c, X + 1, Y + 1, W - 2, H - 2, 0x1c2f36);
    const icon_t *ic = &icon_snail_upright;
    blit_icon_scaled(c, X + (W - ic->w * 2) / 2, Y + 10, ic, 2, true);
    draw_text(c, X + (W - text_w("SNAIL", 3)) / 2, Y + 82, 3, 0xffffff, "SNAIL");
    const char *cap = "ALGAE GRAZED SO FAR";
    draw_text(c, X + (W - text_w(cap, 2)) / 2, Y + 112, 2, 0x9fd8e2, cap);
    char n[24];
    if (t->snail_grazed <= 0) snprintf(n, sizeof n, "NOTHING YET");
    else snprintf(n, sizeof n, "%d SPOT%s", (int)t->snail_grazed, t->snail_grazed == 1 ? "" : "S");
    draw_text(c, X + (W - text_w(n, 3)) / 2, Y + 134, 3, 0xffffff, n);
}

/* the urchin's card (2026-10-02), the snail's: a ring on it, the urchin
   at 3x with its spines waving, SEA URCHIN, the grass it has eaten so far
   (the trim's centimeters) and what it is up to - in words, never colour. */
static void urchin_card_draw(ctx_t *c, const tank_t *t) {
    ring_tank(c, t->urchin_x, URCHIN_FLOOR_Y - 3, 22, 0x9fd8e2);
    const int W = URCHIN_CARD_W, H = URCHIN_CARD_H, X = (c->w - W) / 2, Y = (c->h - H) / 2;
    src_t bg = src_color(0x04141a, 1.0f);
    for (int y = Y; y < Y + H; y++) span(c, X, X + W - 1, y, &bg, 235);
    rect_edge(c, X, Y, W, H, 0x9fd8e2); rect_edge(c, X + 1, Y + 1, W - 2, H - 2, 0x1c2f36);
    ur_pose_t p; urchin_pose(&p, t->clock, tank_urchin_chewing(t));
    const int S = 3, ox = X + W / 2, oy = Y + 10 + 16 * S;               /* the dome's centre at 3x */
    for (int ly = -16; ly <= (int)UR_BASE; ly++)
        for (int lx = -18; lx <= 18; lx++) {
            int tone = urchin_tone((float)lx + 0.5f, (float)ly + 0.5f, &p);
            if (tone == UR_NONE) continue;
            src_t sc = src_color(URCHIN_RGB[tone], 1.0f);
            for (int yy = 0; yy < S; yy++) span(c, ox + lx * S, ox + lx * S + S - 1, oy + ly * S + yy, &sc, 255);
        }
    draw_text(c, X + (W - text_w("SEA URCHIN", 3)) / 2, Y + 90, 3, 0xffffff, "SEA URCHIN");
    const char *cap = "GRASS GRAZED SO FAR";
    draw_text(c, X + (W - text_w(cap, 2)) / 2, Y + 118, 2, 0x9fd8e2, cap);
    char n[24]; int cm = (int)(t->urchin_grazed_px / PX_PER_CM);
    if (cm < 1) snprintf(n, sizeof n, "NOTHING YET");
    else if (cm < 1000) snprintf(n, sizeof n, "%d CM", cm);
    else snprintf(n, sizeof n, "%d.%dK CM", cm / 1000, cm % 1000 / 100);   /* 1.0K CM and up: the line never outgrows the card (0.3.2) */
    draw_text(c, X + (W - text_w(n, 3)) / 2, Y + 140, 3, 0xffffff, n);
    const char *now = tank_urchin_chewing(t) ? "CHEWING"
                    : t->urchin_frond >= 0 ? "OFF TO THE TALL GRASS"
                    : "RESTING";
    draw_text(c, X + (W - text_w(now, 2)) / 2, Y + 170, 2, 0x9fd8e2, now);
}

/* the shrimp school's card (2026-09-29, Strato: "one tap should bring up a
   card to show how much food they've eaten and progress to the next shrimp").
   The pips fill with pellets - filled vs hollow, never colour alone. */
static void shrimp_card_draw(ctx_t *c, const tank_t *t) {
    int n = t->shrimp_n;
    float cx = 0, cy = 0, r = 0;
    for (int i = 0; i < n; i++) { cx += t->shrimp[i].x; cy += t->shrimp[i].y; }
    cx /= n; cy /= n;
    for (int i = 0; i < n; i++) { float d = tank_dist(cx, cy, t->shrimp[i].x, t->shrimp[i].y); if (d > r) r = d; }
    ring_tank(c, cx, cy, fminf(fmaxf(r + 14, 22), 90), 0x9fd8e2);
    const int W = SHRIMP_CARD_W, H = SHRIMP_CARD_H, X = (c->w - W) / 2, Y = (c->h - H) / 2;
    src_t bg = src_color(0x04141a, 1.0f);
    for (int y = Y; y < Y + H; y++) span(c, X, X + W - 1, y, &bg, 235);
    rect_edge(c, X, Y, W, H, 0x9fd8e2); rect_edge(c, X + 1, Y + 1, W - 2, H - 2, 0x1c2f36);
    /* a shrimp at 4x, its legs paddling */
    src_t pal[SHRIMP_TONES];
    for (int k = 0; k < SHRIMP_TONES; k++) pal[k] = src_color(SHRIMP_RGB[k], 1.0f);
    const char *const *fr = fmodf(t->clock * 2.5f, 1) < 0.5f ? SHRIMP_IDLE_A : SHRIMP_IDLE_B;
    const int S = 4, sx = X + (W - 18 * S) / 2, sy = Y + 8;
    for (int y = 0; y < 8; y++)
        for (int x = 0; x < 18; x++) {
            const char *k = fr[y][x] == '.' ? NULL : strchr(SHRIMP_KEYS, fr[y][x]);
            if (!k) continue;
            int ti = (int)(k - SHRIMP_KEYS);
            for (int yy = 0; yy < S; yy++) span(c, sx + x * S, sx + x * S + S - 1, sy + y * S + yy, &pal[ti], SHRIMP_ALPHA[ti]);
        }
    char line[32];
    snprintf(line, sizeof line, "%d SHRIMP", n);
    draw_text(c, X + (W - text_w(line, 3)) / 2, Y + 46, 3, 0xffffff, line);
    snprintf(line, sizeof line, "%d PELLET%s EATEN", (int)t->shrimp_eaten, t->shrimp_eaten == 1 ? "" : "S");
    draw_text(c, X + (W - text_w(line, 2)) / 2, Y + 76, 2, 0x9fd8e2, line);
    const char *cap = "NEXT SHRIMP";
    draw_text(c, X + (W - text_w(cap, 2)) / 2, Y + 104, 2, 0x9fd8e2, cap);
    bool full = n >= SHRIMP_MAX;
    int have = full ? SHRIMP_PER_JOIN : t->shrimp_food;
    const int pitch = 22, px0 = X + (W - (SHRIMP_PER_JOIN - 1) * pitch) / 2, py = Y + 132;
    for (int i = 0; i < SHRIMP_PER_JOIN; i++) {
        float x = px0 + i * pitch;
        if (i < have) {
            fill_ellipse(c, x, py, 6.5f, 6.5f, 0xffbd59, 255);
            fill_ellipse(c, x - 2, py - 2, 2.2f, 2.2f, 0xffe9bd, 255);
        } else ring(c, x, py, 6, 0x5a6a6e);
    }
    if (full) snprintf(line, sizeof line, "THE SCHOOL IS FULL");
    else if (tank_shrimp_refusing(t)) snprintf(line, sizeof line, "TOO MUCH ALGAE TO EAT");
    else if (t->shrimp_food >= SHRIMP_PER_JOIN && t->shrimp_cool > 0) {
        int m = (int)ceilf(t->shrimp_cool / 60.0f);
        snprintf(line, sizeof line, "ARRIVES IN %d MIN", m < 1 ? 1 : m);
    } else {
        int more = SHRIMP_PER_JOIN - t->shrimp_food;
        snprintf(line, sizeof line, "%d MORE PELLET%s", more, more == 1 ? "" : "S");
    }
    draw_text(c, X + (W - text_w(line, 2)) / 2, Y + 158, 2, 0xffffff, line);
}

/* the seahorse's card (0.3.6): a ring on it and a centred card - the
 * seahorse at 2x in its open curl, its days with the keeper, its hitches,
 * the trust it passed on and what it is doing */
static void seahorse_card_draw(ctx_t *c, const tank_t *t) {
    ring_tank(c, t->sh_x + 4 * t->sh_facing, t->sh_y - SH_BODY_UP, 24, 0x9fd8e2);
    const int W = SEAHORSE_CARD_W, H = SEAHORSE_CARD_H, X = (c->w - W) / 2, Y = (c->h - H) / 2;
    src_t bg = src_color(0x04141a, 1.0f);
    for (int y = Y; y < Y + H; y++) span(c, X, X + W - 1, y, &bg, 235);
    rect_edge(c, X, Y, W, H, 0x9fd8e2); rect_edge(c, X + 1, Y + 1, W - 2, H - 2, 0x1c2f36);
    decor_scratch_t *sc = ds();
    sh_sprite(sc, sc->sh_spr_card, t->clock, SH_FREE, 0, 11.0f, 1.5f, 1.0f, 0);
    const int S = 2, ox = X + 14, oy = Y + (H - SH_H * S) / 2;
    for (int ly = 0; ly < SH_H; ly++)
        for (int lx = 0; lx < SH_W; lx++) {
            uint8_t b = sc->sh_spr_card[ly * SH_W + lx];
            if (!(b & SHB_ON)) continue;
            src_t s = src_color(SH_RGB[b & 15], 1.0f);
            for (int yy = 0; yy < S; yy++) span(c, ox + lx * S, ox + lx * S + S - 1, oy + ly * S + yy, &s, SH_ALPHA[b & 15]);
        }
    const int TX = X + 14 + SH_W * S + 12;   /* "+N.N TRUST PASSED ON" is 21 chars at scale 2: 252 px, inside the card from here */
    draw_text(c, TX, Y + 18, 3, 0xffffff, "SEAHORSE");
    char line[32];
    float days = tank_seahorse_days(t);
    if (days < 1) snprintf(line, sizeof line, "WITH YOU SINCE TODAY");
    else snprintf(line, sizeof line, "%d DAY%s WITH YOU", (int)days, (int)days == 1 ? "" : "S");
    draw_text(c, TX, Y + 52, 2, 0x9fd8e2, line);
    snprintf(line, sizeof line, "%d HITCH%s", (int)t->sh_hitches, t->sh_hitches == 1 ? "" : "ES");
    draw_text(c, TX, Y + 76, 2, 0xffffff, line);
    snprintf(line, sizeof line, "+%d.%d TRUST PASSED ON", (int)t->sh_trust_passed, (int)(t->sh_trust_passed * 10) % 10);
    draw_text(c, TX, Y + 100, 2, 0xffffff, line);
    snprintf(line, sizeof line, "%d PELLET%s SNAPPED", (int)t->sh_eaten, t->sh_eaten == 1 ? "" : "S");
    draw_text(c, TX, Y + 124, 2, 0xffffff, line);
    char now[32];
    if (t->sh_hitched) snprintf(now, sizeof now, "ON YOUR FINGER");
    else if (t->sh_mode == SH_HITCH) snprintf(now, sizeof now, "SWIMMING TO YOU");
    else if (t->night && t->sh_mode == SH_PERCH) snprintf(now, sizeof now, "ASLEEP");   /* only once it holds on (0.3.7: it said ASLEEP on the finger) */
    else if (t->sh_mode == SH_PERCH && t->sh_akind == SH_ANCHOR_SAND) snprintf(now, sizeof now, "RESTING ON THE SAND");
    else if (t->sh_mode == SH_PERCH) snprintf(now, sizeof now, "HOLDING %s", tank_seahorse_holding(t));
    else snprintf(now, sizeof now, "ADRIFT");
    draw_text(c, TX, Y + 156, 2, 0x9fd8e2, now);
}

static void tools_draw(ctx_t *c, const tank_t *t);   /* the toolbox, below */
bool render_stats_next_hit(const tank_t *t, int fish_idx, float x, float y) {
    x = render_page_x(x); y = render_page_y(y);
    return t->n_fish > 1 && fish_idx >= 0 && fish_idx < t->n_fish &&
           x >= RENDER_CARD_NEXT_X - 2 && x < RENDER_CARD_NEXT_X + RENDER_CARD_NEXT_W + 2 &&
           y >= RENDER_CARD_NEXT_Y - 2 && y < RENDER_CARD_NEXT_Y + RENDER_CARD_NEXT_H + 2;
}
void render_stats_card(const tank_t *t, int fish_idx, uint16_t *fb, int stride) {
    if (fish_idx == RENDER_CARD_SHRIMP) {
        if (!(t->sd_unlocks & SD_ITEM_SHRIMP) || t->shrimp_n <= 0) return;
        ctx_t sc = ctx_overlay(fb, stride);
        shrimp_card_draw(&sc, t);
        return;
    }
    if (fish_idx == RENDER_CARD_URCHIN) {
        if (!(t->sd_unlocks & SD_ITEM_URCHIN) || t->urchin_x < 0) return;
        ctx_t sc = ctx_overlay(fb, stride);
        urchin_card_draw(&sc, t);
        return;
    }
    if (fish_idx == RENDER_CARD_SEAHORSE) {
        if (!(t->sd_unlocks & SD_ITEM_SEAHORSE) || t->sh_x < 0) return;
        ctx_t sc = ctx_overlay(fb, stride);
        seahorse_card_draw(&sc, t);
        return;
    }
    if (fish_idx == RENDER_CARD_SNAIL) {
        if (!(t->sd_unlocks & SD_ITEM_SNAIL) || t->snail_x < 0) return;
        ctx_t sc = ctx_overlay(fb, stride);
        snail_card_draw(&sc, t);
        return;
    }
    if (fish_idx < 0 || fish_idx >= t->n_fish) return;
    ctx_t c = ctx_full(fb, stride, 1.0f);            /* card ignores night dimming */
    const fish_t *f = &t->fish[fish_idx];
    ring(&c, f->x, f->y, 17 * f->size, f->color);
    c = ctx_page(fb, stride);
    unsigned ep; const uint16_t *scene = render_scene_buf(&ep);
    if (g_card && scene) {
        if (fish_idx != g_card_fish || ep != g_card_epoch ||
            t->clock - g_card_t > 0.25f || t->clock < g_card_t) {
            for (int y = 0; y < RENDER_CARD_H; y++)
                memcpy(g_card + y * RENDER_CARD_W,
                       scene + (RENDER_CARD_Y + y) * TANK_W + RENDER_CARD_X, RENDER_CARD_W * 2);
            ctx_t cc = { g_card, RENDER_CARD_W, 1.0f, RENDER_CARD_X, RENDER_CARD_Y, RENDER_CARD_W, RENDER_CARD_H, false };
            card_draw(cc, t, fish_idx);
            g_card_fish = fish_idx; g_card_epoch = ep; g_card_t = t->clock;
        }
        for (int y = 0; y < RENDER_CARD_H; y++)
            memcpy(fb + (RENDER_CARD_Y + y) * stride + RENDER_CARD_X,
                   g_card + y * RENDER_CARD_W, RENDER_CARD_W * 2);
    } else card_draw(c, t, fish_idx);
    if (t->n_fish > 1)
        button(&c, RENDER_CARD_NEXT_X, RENDER_CARD_NEXT_Y, RENDER_CARD_NEXT_W, RENDER_CARD_NEXT_H,
               0x1c2f36, 0x9fd8e2, ">", 3);
    tools_draw(&c, t);
}

/* ---- the toolbox (2026-10-01) ----
 * Drawn every frame, uncached - so the backdrop is the card's look by bit
 * shifts, not blends: an eighth of the water through, the rest the card's
 * dark (~8.7k pixels for the price of a copy; blended, the card's 28k cost
 * ~7 ms on the device). */
static void shade_box(ctx_t *c, int x, int y, int w, int h) {
    const uint16_t base = rgb565(0x04141a, 0.875f);
    if (c->scaled) {
        int x0 = x * TANK_W / PAGE_W, x1 = (x + w) * TANK_W / PAGE_W;
        int y0 = y * TANK_H / PAGE_H, y1 = (y + h) * TANK_H / PAGE_H;
        if (x0 < 0) x0 = 0;
        if (y0 < 0) y0 = 0;
        if (x1 > TANK_W) x1 = TANK_W;
        if (y1 > TANK_H) y1 = TANK_H;
        for (int yy = y0; yy < y1; yy++)
            for (int xx = x0; xx < x1; xx++) {
                uint16_t *p = c->fb + yy * c->stride + xx;
                *p = (uint16_t)(((*p >> 3) & 0x18E3) + base);
            }
        return;
    }
    for (int yy = y; yy < y + h; yy++) {
        if ((unsigned)(yy - c->oy) >= (unsigned)c->h) continue;
        int x0 = x < c->ox ? c->ox : x, x1 = x + w - 1 > c->ox + c->w - 1 ? c->ox + c->w - 1 : x + w - 1;
        uint16_t *p = &CTX_PX(c, x0, yy);
        for (int xx = x0; xx <= x1; xx++, p++) *p = (uint16_t)(((*p >> 3) & 0x18E3) + base);
    }
}
static void rect_fill(ctx_t *c, int x, int y, int w, int h, uint32_t rgb);   /* below */
#define TOOL_BTN_W ((RENDER_TOOLS_W - 16 - 6) / 2)
#define TOOL_BTN_H (RENDER_TOOLS_H - 16)
static void tools_draw(ctx_t *c, const tank_t *t) {
    const int X = RENDER_TOOLS_X, Y = RENDER_TOOLS_Y, W = RENDER_TOOLS_W, H = RENDER_TOOLS_H;
    shade_box(c, X, Y, W, H);
    rect_edge(c, X, Y, W, H, 0x9fd8e2); rect_edge(c, X + 1, Y + 1, W - 2, H - 2, 0x1c2f36);
    static const icon_t *const ICON[2] = { &icon_tool_sponge, &icon_tool_scissors };
    for (int k = 0; k < 2; k++) {
        int bx = X + 8 + k * (TOOL_BTN_W + 6), by = Y + 8;
        bool held = t->tool == (k ? TOOL_SCISSORS : TOOL_SPONGE);
        rect_fill(c, bx, by, TOOL_BTN_W, TOOL_BTN_H, held ? 0x356670 : 0x14252b);
        uint32_t edge = held ? 0xffffff : 0x4d6a72;
        rect_edge(c, bx, by, TOOL_BTN_W, TOOL_BTN_H, edge);
        if (held) rect_edge(c, bx + 1, by + 1, TOOL_BTN_W - 2, TOOL_BTN_H - 2, edge);
        const icon_t *ic = ICON[k];
        blit_icon_scaled(c, bx + (TOOL_BTN_W - ic->w * 2) / 2, by + (TOOL_BTN_H - ic->h * 2) / 2, ic, 2, true);
    }
    /* the LOCK beside it (0.3.5) */
    const int LX = RENDER_LOCK_X, LY = RENDER_LOCK_Y, LW = RENDER_LOCK_W, LH = RENDER_LOCK_H;
    if (t->tools_locked) rect_fill(c, LX, LY, LW, LH, 0x356670); else shade_box(c, LX, LY, LW, LH);
    rect_edge(c, LX, LY, LW, LH, t->tools_locked ? 0xffffff : 0x9fd8e2);
    rect_edge(c, LX + 1, LY + 1, LW - 2, LH - 2, t->tools_locked ? 0xffffff : 0x1c2f36);
    const icon_t *lk = t->tools_locked ? &icon_tool_lock_closed : &icon_tool_lock_open;
    blit_icon(c, LX + (LW - lk->w) / 2, LY + (LH - lk->h) / 2, lk, 255);
}
bool render_lock_hit(float x, float y) {
    x = render_page_x(x); y = render_page_y(y);
    return y >= RENDER_TOOLS_Y && x >= RENDER_LOCK_X && x < RENDER_LOCK_X + RENDER_LOCK_W + 16;
}
int render_tools_hit(float x, float y) {
    x = render_page_x(x); y = render_page_y(y);
    const int X = RENDER_TOOLS_X, W = RENDER_TOOLS_W;
    if (y < RENDER_TOOLS_Y || x < X - RENDER_CARD_HIT_SIDE || x >= X + W + RENDER_CARD_HIT_SIDE) return -1;
    return x < X + W / 2 ? TOOL_SPONGE : TOOL_SCISSORS;
}

/* the chip: the tool at 1x and DONE, top left where the card would be */
#define TOOL_CHIP_X RENDER_CARD_X
#define TOOL_CHIP_Y RENDER_CARD_Y
#define TOOL_CHIP_H 40
static int tool_chip_w(void) { return 8 + 24 + 8 + text_w("DONE", 2) + 10; }
void render_tool_chip(const tank_t *t, int fish_idx, uint16_t *fb, int stride) {
    if (t->tool == TOOL_HAND || (fish_idx >= 0 && fish_idx < t->n_fish)) return;
    /* a stroke on the glass: the chip steps aside until the finger lifts, so
     * the film it covers shows as the sponge takes it (0.3.5, Strato: "the
     * sponge should be able to clean algae behind the DONE button") */
    if (tank_stroking(t)) return;                    /* (not a press: the tap on DONE itself keeps it in view) */
    ctx_t c = ctx_overlay(fb, stride);
    const int X = TOOL_CHIP_X, Y = TOOL_CHIP_Y, W = tool_chip_w(), H = TOOL_CHIP_H;
    shade_box(&c, X, Y, W, H);
    rect_edge(&c, X, Y, W, H, 0xffffff); rect_edge(&c, X + 1, Y + 1, W - 2, H - 2, 0x9fd8e2);
    const icon_t *ic = t->tool == TOOL_SCISSORS ? &icon_tool_scissors : &icon_tool_sponge;
    blit_icon(&c, X + 8, Y + (H - ic->h) / 2, ic, 255);
    draw_text(&c, X + 8 + ic->w + 8, Y + (H - 14) / 2, 2, 0xffffff, "DONE");
}
bool render_tool_chip_hit(const tank_t *t, float x, float y) {
    x = render_page_x(x); y = render_page_y(y);
    return t->tool != TOOL_HAND && x < TOOL_CHIP_X + tool_chip_w() + 16 && y < TOOL_CHIP_Y + TOOL_CHIP_H + 24;
}


/* ---- a small pixel font (2026-09-11) ----
 * 5x7 glyphs - upper case, digits, a little punctuation - drawn as scale x
 * scale blocks, so one table reads at 2x for a caption and 3x for a
 * button. The renderer's first text (the milestones page can borrow it for
 * labels). Lower case maps to upper; anything else advances a cell. */
static const uint8_t FONT5X7[][7] = {
    { 0x0e, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11 }, /* A */
    { 0x1e, 0x11, 0x11, 0x1e, 0x11, 0x11, 0x1e }, /* B */
    { 0x0e, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0e }, /* C */
    { 0x1e, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1e }, /* D */
    { 0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x1f }, /* E */
    { 0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x10 }, /* F */
    { 0x0e, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0f }, /* G */
    { 0x11, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11 }, /* H */
    { 0x0e, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0e }, /* I */
    { 0x07, 0x02, 0x02, 0x02, 0x02, 0x12, 0x0c }, /* J */
    { 0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11 }, /* K */
    { 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1f }, /* L */
    { 0x11, 0x1b, 0x15, 0x15, 0x11, 0x11, 0x11 }, /* M */
    { 0x11, 0x11, 0x19, 0x15, 0x13, 0x11, 0x11 }, /* N */
    { 0x0e, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e }, /* O */
    { 0x1e, 0x11, 0x11, 0x1e, 0x10, 0x10, 0x10 }, /* P */
    { 0x0e, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0d }, /* Q */
    { 0x1e, 0x11, 0x11, 0x1e, 0x14, 0x12, 0x11 }, /* R */
    { 0x0f, 0x10, 0x10, 0x0e, 0x01, 0x01, 0x1e }, /* S */
    { 0x1f, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04 }, /* T */
    { 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e }, /* U */
    { 0x11, 0x11, 0x11, 0x11, 0x11, 0x0a, 0x04 }, /* V */
    { 0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0a }, /* W */
    { 0x11, 0x11, 0x0a, 0x04, 0x0a, 0x11, 0x11 }, /* X */
    { 0x11, 0x11, 0x11, 0x0a, 0x04, 0x04, 0x04 }, /* Y */
    { 0x1f, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1f }, /* Z */
    { 0x0e, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0e }, /* 0 */
    { 0x04, 0x0c, 0x04, 0x04, 0x04, 0x04, 0x0e }, /* 1 */
    { 0x0e, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1f }, /* 2 */
    { 0x1f, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0e }, /* 3 */
    { 0x02, 0x06, 0x0a, 0x12, 0x1f, 0x02, 0x02 }, /* 4 */
    { 0x1f, 0x10, 0x1e, 0x01, 0x01, 0x11, 0x0e }, /* 5 */
    { 0x06, 0x08, 0x10, 0x1e, 0x11, 0x11, 0x0e }, /* 6 */
    { 0x1f, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08 }, /* 7 */
    { 0x0e, 0x11, 0x11, 0x0e, 0x11, 0x11, 0x0e }, /* 8 */
    { 0x0e, 0x11, 0x11, 0x0f, 0x01, 0x02, 0x0c }, /* 9 */
    { 0x0e, 0x11, 0x01, 0x02, 0x04, 0x00, 0x04 }, /* ? */
    { 0x04, 0x04, 0x04, 0x04, 0x04, 0x00, 0x04 }, /* ! */
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x0c, 0x0c }, /* . */
    { 0x00, 0x00, 0x00, 0x00, 0x0c, 0x04, 0x08 }, /* , */
    { 0x00, 0x00, 0x00, 0x1f, 0x00, 0x00, 0x00 }, /* - */
    { 0x00, 0x0c, 0x0c, 0x00, 0x0c, 0x0c, 0x00 }, /* : */
    { 0x0c, 0x04, 0x08, 0x00, 0x00, 0x00, 0x00 }, /* ' */
    { 0x01, 0x02, 0x02, 0x04, 0x08, 0x08, 0x10 }, /* / */
    { 0x00, 0x04, 0x04, 0x1f, 0x04, 0x04, 0x00 }, /* + */
    { 0x19, 0x1a, 0x02, 0x04, 0x08, 0x0b, 0x13 }, /* % */
    { 0x00, 0x00, 0x08, 0x15, 0x02, 0x00, 0x00 }, /* ~ (the battery page's "about") */
    { 0x02, 0x04, 0x08, 0x10, 0x08, 0x04, 0x02 }, /* < */
    { 0x08, 0x04, 0x02, 0x01, 0x02, 0x04, 0x08 }, /* > */
};
static const char FONT_CHARS[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789?!.,-:'/+%~<>";
static const uint8_t *glyph(char ch) {
    if (ch >= 'a' && ch <= 'z') ch -= 'a' - 'A';
    const char *p = ch ? strchr(FONT_CHARS, ch) : NULL;
    return p ? FONT5X7[p - FONT_CHARS] : NULL;
}
static int text_w(const char *s, int scale) {
    if (scale < 2) scale = 2;
    int n = (int)strlen(s); return n ? n * 6 * scale - scale : 0;
}
static void draw_page_glyph(ctx_t *c, int x, int y, int scale, const src_t *col, const uint8_t *g) {
    int left = (int)floorf((float)x * TANK_W / PAGE_W), top = (int)floorf((float)y * TANK_H / PAGE_H);
    int w = (5 * scale * TANK_W + PAGE_W / 2) / PAGE_W;
    int h = (7 * scale * TANK_H + PAGE_H / 2) / PAGE_H;
    if (w < 5) w = 5;
    if (h < 7) h = 7;
    /* Rasterize in framebuffer pixels: forward-mapping tiny logical spans merges strokes. */
    for (int yy = 0; yy < h; yy++) {
        int py = top + yy;
        if ((unsigned)py >= TANK_H) continue;
        for (int xx = 0; xx < w; xx++) {
            int px = left + xx;
            if ((unsigned)px < TANK_W && (g[yy * 7 / h] & (0x10 >> (xx * 5 / w))))
                c->fb[py * c->stride + px] = col->v;
        }
    }
}
static void draw_text(ctx_t *c, int x, int y, int scale, uint32_t rgb, const char *s) {
    if (c->scaled && scale < 2) scale = 2;
    if (rgb == 0x2a3f45 || rgb == 0x3f6a72) rgb = 0xa4c5ce;
    src_t col = src_color(rgb, c->dim);
    for (; *s; s++, x += 6 * scale) {
        const uint8_t *g = glyph(*s);
        if (!g) continue;
        if (c->scaled) { draw_page_glyph(c, x, y, scale, &col, g); continue; }
        for (int r = 0; r < 7; r++)
            for (int k = 0; k < 5; k++)
                if (g[r] & (0x10 >> k))
                    for (int yy = 0; yy < scale; yy++)
                        span(c, x + k * scale, x + k * scale + scale - 1, y + r * scale + yy, &col, 255);
    }
}

static int draw_text_wrap(ctx_t *c, int x, int y, int width, uint32_t rgb, const char *s) {
    int capacity = (width + 2) / 12, lines = 0;
    char line[96];
    if (capacity < 1) capacity = 1;
    if (capacity >= (int)sizeof line) capacity = sizeof line - 1;
    while (*s) {
        while (*s == ' ') s++;
        if (!*s) break;
        int n = 0, space = -1;
        while (s[n] && s[n] != '\n' && n < capacity) { if (s[n] == ' ') space = n; n++; }
        if (s[n] && s[n] != '\n' && space > 0) n = space;
        memcpy(line, s, (size_t)n); line[n] = 0;
        draw_text(c, x, y + lines * 16, 2, rgb, line);
        lines++; s += n;
        if (*s == '\n') s++;
    }
    return lines;
}

/* ---- reset confirm (2026-09-11) ----
 * The keeper's "start over" (device: hold BOOT, tap the glass; sim: X). A
 * modal panel over the live tank - the fish keep swimming behind it - that
 * asks before the save is wiped. Opaque stores only, so a frame with it up
 * costs about what the milestones page does. */
static void rect_fill(ctx_t *c, int x, int y, int w, int h, uint32_t rgb) {
    src_t f = src_color(rgb, 1.0f);
    for (int yy = y; yy < y + h; yy++) span(c, x, x + w - 1, yy, &f, 255);
}
static void rect_edge(ctx_t *c, int x, int y, int w, int h, uint32_t rgb) {
    src_t e = src_color(rgb, 1.0f);
    span(c, x, x + w - 1, y, &e, 255); span(c, x, x + w - 1, y + h - 1, &e, 255);
    for (int yy = y + 1; yy < y + h - 1; yy++) { px(c, x, yy, e.v); px(c, x + w - 1, yy, e.v); }
}
static void button(ctx_t *c, int x, int y, int W, int H, uint32_t fill, uint32_t edge, const char *label, int scale) {
    rect_fill(c, x, y, W, H, fill);
    rect_edge(c, x, y, W, H, edge); rect_edge(c, x + 1, y + 1, W - 2, H - 2, edge);
    draw_text(c, x + (W - text_w(label, scale)) / 2, y + (H - 7 * scale) / 2, scale, 0xffffff, label);
}
/* the same primitives for panels built elsewhere (render.h) */
int  render_text_w(const char *s, int scale) { return text_w(s, scale); }
void render_text(uint16_t *fb, int stride, int x, int y, int scale, uint32_t rgb, const char *s) {
    ctx_t c = ctx_page(fb, stride); draw_text(&c, x, y, scale, rgb, s);
}
int render_text_wrap(uint16_t *fb, int stride, int x, int y, int width, uint32_t rgb, const char *s) {
    ctx_t c = ctx_page(fb, stride); return draw_text_wrap(&c, x, y, width, rgb, s);
}
void render_rect(uint16_t *fb, int stride, int x, int y, int w, int h, uint32_t rgb) {
    ctx_t c = ctx_page(fb, stride); rect_fill(&c, x, y, w, h, rgb);
}
void render_rect_edge(uint16_t *fb, int stride, int x, int y, int w, int h, uint32_t rgb) {
    ctx_t c = ctx_page(fb, stride); rect_edge(&c, x, y, w, h, rgb);
}
void render_rect_blend(uint16_t *fb, int stride, int x, int y, int w, int h, uint32_t rgb, int alpha) {
    ctx_t c = ctx_page(fb, stride); src_t s = src_color(rgb, 1.0f);
    for (int yy = y; yy < y + h; yy++) span(&c, x, x + w - 1, yy, &s, alpha);
}
void render_ring(uint16_t *fb, int stride, float cx, float cy, float r, uint32_t rgb) {
    ctx_t c = ctx_page(fb, stride); ring(&c, cx, cy, r, rgb);
}
void render_button(uint16_t *fb, int stride, int x, int y, int w, int h, uint32_t fill, uint32_t edge, const char *label, int scale) {
    ctx_t c = ctx_page(fb, stride); button(&c, x, y, w, h, fill, edge, label, scale);
}
void render_glyph(uint16_t *fb, int stride, int x, int y, int scale, uint32_t rgb, const uint8_t *rows) {
    ctx_t c = ctx_page(fb, stride); src_t col = src_color(rgb, 1.0f);
    draw_page_glyph(&c, x, y, scale < 2 ? 2 : scale, &col, rows);
}
void render_fish_preview(uint16_t *fb, int stride, float x, float y, float size,
                         uint32_t body, uint32_t fin, uint32_t accent, float clock) {
    ctx_t c = ctx_page(fb, stride);
    fish_t f; memset(&f, 0, sizeof f);
    f.x = x; f.y = y; f.heading = 0; f.size = size; f.speed = 40;
    f.stage = STAGE_ADULT;                       /* grown: the crest shows the fin colour */
    f.hunger = 4; f.stress = 0; f.goal.id = GOAL_EXPLORE;
    f.color = body; f.fin = fin; f.accent = accent;
    tank_fish_face(&f);                          /* side-on, facing right */
    draw_fish_core(&c, &f, clock, false, size);
}
void render_fish_portrait(uint16_t *fb, int stride, float x, float y, float size, const fish_t *who, float clock) {
    ctx_t c = ctx_page(fb, stride);
    fish_t f; memset(&f, 0, sizeof f);
    f.x = x; f.y = y; f.heading = 0; f.size = size; f.speed = 40;
    f.stage = who->stage;
    f.hunger = 4; f.stress = 0; f.goal.id = GOAL_EXPLORE;
    f.color = who->color; f.fin = who->fin; f.accent = who->accent;
    f.variety = who->variety; f.pattern = who->pattern;
    tank_fish_face(&f);
    draw_fish_core(&c, &f, clock, false, size);
}
void render_confirm_reset(uint16_t *fb, int stride, float frac) {
    ctx_t c = ctx_page(fb, stride);              /* ignores night dimming, like the card */
    const int X = RENDER_CONFIRM_X, Y = RENDER_CONFIRM_Y, W = RENDER_CONFIRM_W, H = RENDER_CONFIRM_H;
    rect_fill(&c, X, Y, W, H, 0x04141a);
    rect_edge(&c, X, Y, W, H, 0x9fd8e2); rect_edge(&c, X + 1, Y + 1, W - 2, H - 2, 0x1c2f36);
    const char *title = "WIPE TANK?";
    draw_text(&c, X + (W - text_w(title, 3)) / 2, Y + 20, 3, 0xffffff, title);
    const char *l1 = "WIPE THIS TANK AND ITS BANK", *l2 = "OTHER TANKS / SD FILES KEPT";
    draw_text(&c, X + (W - text_w(l1, 2)) / 2, Y + 58, 2, 0x9fd8e2, l1);
    draw_text(&c, X + (W - text_w(l2, 2)) / 2, Y + 78, 2, 0x9fd8e2, l2);
    /* NO is the calm one; YES wears the stress red */
    button(&c, RENDER_CONFIRM_NO_X,  RENDER_CONFIRM_BTN_Y, RENDER_CONFIRM_BTN_W, RENDER_CONFIRM_BTN_H, 0x1c2f36, 0x9fd8e2, "CANCEL", 2);
    button(&c, RENDER_CONFIRM_YES_X, RENDER_CONFIRM_BTN_Y, RENDER_CONFIRM_BTN_W, RENDER_CONFIRM_BTN_H, 0x7a2028, 0xf25b65, "WIPE", 3);
    /* the prompt lets itself go: a bar draining toward the timeout */
    if (frac < 0) frac = 0;
    if (frac > 1) frac = 1;
    const int bw = W - 48, bx = X + 24, by = Y + H - 16;
    rect_fill(&c, bx, by, bw, 3, 0x2a3f45);
    int fw = (int)(bw * frac + 0.5f);
    if (fw > 0) rect_fill(&c, bx, by, fw, 3, 0x9fd8e2);
}
int render_confirm_hit(float x, float y) {
    x = render_page_x(x); y = render_page_y(y);
    const int m = 10;                                   /* a fingertip's slop around each button */
    if (y < RENDER_CONFIRM_BTN_Y - m || y >= RENDER_CONFIRM_BTN_Y + RENDER_CONFIRM_BTN_H + m) return 0;
    if (x >= RENDER_CONFIRM_NO_X - m  && x < RENDER_CONFIRM_NO_X  + RENDER_CONFIRM_BTN_W + m) return -1;
    if (x >= RENDER_CONFIRM_YES_X - m && x < RENDER_CONFIRM_YES_X + RENDER_CONFIRM_BTN_W + m) return 1;
    return 0;
}

/* ---- milestones page (2026-09-13 redesign: an achievement wall) ----
 * Rows of 40 px: the fish (render_fish_preview at its real size, so growth
 * shows), its name, a growth strip, then six 32 px badges at a 40 px pitch.
 * The tank row repeats the shape with the population strip. A locked badge
 * is the art as a flat grey silhouette (blit_icon_locked); a badge earned
 * since the keeper last closed the page wears a two-tone ring. Tapping a
 * badge / a name / a strip opens a small detail modal in the reset prompt's
 * dress (the art at 2x, a title, the words); the next tap anywhere closes
 * it back to the page. Render-local state, cleared by render_milestones_leave. */
/* The BOWL (TANK_ROUND; Strato on the glass, 2026-10-01: the top fish's
 * portrait and its last badge were cut by the circle - "nudge this down",
 * SETTINGS "to the top, dead center as it would fit in that opening",
 * UPGRADES and CLOSE "closer together centered"): the rows start where the
 * circle is wide enough for them and stand 34 px apart, SETTINGS sits alone
 * in the cap above them, the other two side by side at the foot. */
/* (the page's numbers, MSP_*: render.h) */
#define MSP_INK       0x031015
#define MSP_DIM       0x2a3f45
#define MSP_TEAL      0x9fd8e2

typedef struct { uint32_t bit; const icon_t *icon; } badge_t;
static const badge_t FISH_BADGES[6] = {
    { MS_FIRST_MEAL_FROM_YOU, &icon_ms_first_meal },  { MS_FIRST_HOLD_APPROACH, &icon_ms_first_hold_approach },
    { MS_FIRST_GRASS, &icon_ms_first_seagrass },      { MS_FIRST_BUBBLES, &icon_ms_first_bubbles },   /* (the third was "first reef" until 0.3.0) */
    { MS_FIRST_FOLLOW, &icon_ms_first_follow },       { MS_FIRST_DART, &icon_ms_first_dart },
};
#define TANK_BADGE_N 10
static const badge_t TANK_BADGES[TANK_BADGE_N] = {
    { TMS_FIRST_FEEDING, &icon_ms_first_feeding },    { TMS_FIRST_TRIM, &icon_ms_first_trimming },
    { TMS_FIRST_CLEANING, &icon_ms_first_glass_cleaning }, { TMS_FIRST_FULL_NIGHT, &icon_ms_first_quiet_night },
    { TMS_FIRST_PLAY_SESSION, &icon_ms_first_play_session }, { TMS_CHANGED_SOMEONE, &icon_ms_tank_changed_someone },
    { TMS_FULL_SCHOOL, &icon_ms_full_school },        /* 2026-09-30: only once the shrimp are bought (tank_badge_shown) */
    { TMS_FIRST_REEF, &icon_ms_first_reef },          /* 2026-10-02: only once the reef cluster is bought */
    { TMS_FIRST_SALE, &icon_ms_first_sale },          /* 0.3.5: a fish sold - the shop's fry */
    { TMS_SEAHORSE_FED, &icon_ms_seahorse_fed },      /* 0.3.6: only once the seahorse is bought (Strato: "visible and gray once purchased") */
};
/* the tank row's pages (2026-09-30, the seventh badge): six badges a page;
   with more, a small arrow at the row's right end (or a sideways swipe along
   the row) flips to the next page, pointing back from the last. */
static int g_ms_tpage;                  /* the tank row's page (kept while a modal opens and closes; 0 on leaving the page) */
/* which tank badges show: a bought thing's badge only once the thing is
   bought (the shrimp school's, the reef cluster's) - and, once earned, for
   good, whatever was sold since; the first sale's only once earned */
static bool tank_badge_shown(const tank_t *t, int i) {
    uint32_t bit = TANK_BADGES[i].bit;
    if (bit == TMS_FULL_SCHOOL) return (t->sd_unlocks & SD_ITEM_SHRIMP)  || (t->tank_ms_bits & bit);
    if (bit == TMS_FIRST_REEF)  return (t->sd_unlocks & SD_ITEM_CLUSTER) || (t->tank_ms_bits & bit);
    if (bit == TMS_FIRST_SALE)  return (t->tank_ms_bits & bit) != 0;   /* 0.3.5: once earned (its notice says what it opened) */
    if (bit == TMS_SEAHORSE_FED) return (t->sd_unlocks & SD_ITEM_SEAHORSE) || (t->tank_ms_bits & bit);
    return true;
}
static int tank_badge_n(const tank_t *t) {
    int n = 0;
    for (int i = 0; i < TANK_BADGE_N; i++) n += tank_badge_shown(t, i);
    return n;
}
/* the k-th badge that shows (k < tank_badge_n) */
static const badge_t *tank_badge(const tank_t *t, int k) {
    for (int i = 0; i < TANK_BADGE_N; i++)
        if (tank_badge_shown(t, i) && k-- == 0) return &TANK_BADGES[i];
    return &TANK_BADGES[0];
}
static int tank_pages(const tank_t *t) { return (tank_badge_n(t) + MSP_PER_ROW - 1) / MSP_PER_ROW; }
static const char *const STAGE_WORDS[4] = { "FRY", "JUVENILE", "ADULT", "ELDER" };
/* the detail modal (a tap on a badge / name / strip): what to show until
 * the next tap. caption[0] == 0 means no modal. */
static char g_ms_caption[72], g_ms_title[16];
static char g_ms_caption2[32];       /* the caption's second line (the fry checklist's sentences), or empty */
static char g_ms_sub[32];            /* a line under the caption (the fry checklist's progress), or empty */
static bool g_ms_lit;
static const icon_t *g_ms_icon;      /* the badge's art, or NULL */
static int  g_ms_fish = -1;          /* a fish's own sprite instead, or -1 */
static bool g_ms_fry;                /* the fry-to-be (a silhouette) instead */
static int  g_ms_kind = -1;          /* a gate's modal: its kind (the HOW? button shows), or -1 */
static bool g_ms_tip;                /* the gate's tip page is up instead of its modal */
/* where the modal came from (2026-09-16, Strato: arrows at the top corners
   to cycle without dropping back to the page): the row (a fish's index, or
   the TANK / NEW FRY row) and the column (-1 = the name / strip, else the
   badge or gate). ms_step moves along the group the modal belongs to: a
   fish's six badges, the tank's six, the fry's gates, or - from a fish's
   name - the fish themselves. A group of one (TANK's tally, NEW FRY's)
   shows no arrows. */
static int  g_ms_row = -1, g_ms_k = -1;
static bool g_ms_tankrow, g_ms_fryrow;
/* a fish's CARD (the modal behind its name) carries two buttons at its foot
   (2026-10-01, Strato): RENAME - the letter wheel over the live tank - and
   SELL. SELL is two taps on the placement page's idiom: the first arms it
   (the label turns to the price and OK?, the line above says TAP AGAIN), the
   second sells - but not within MSP_SELL_WAIT_S of the first, so a doubled
   tap cannot sell a fish. A fish the tank will not part with (the last pair,
   or a fry still owed its welcome) shows SELL dim, and the line above stays
   empty until the dim button is tapped - then it gives the reason (Strato,
   2026-10-01: the reason "only if the gray sell button is tapped"). The card stands where a gate's modal does (the taller one), inside
   the box every board's glass shows whole: no per-board layout. */
#define MSP_CARD_BTN_W   116
#define MSP_CARD_BTN_GAP 24
#define MSP_SELL_WAIT_S  0.6f
static bool  g_ms_sell_armed;
static bool  g_ms_sell_asked;        /* the dim SELL was tapped: the line says why it stays */
static float g_ms_sell_clock;        /* the tank clock when SELL was armed */

/* the TANK tally's picture (2026-10-02, Strato: the modal "feels bare"): the
 * school itself across the top of the box - every fish in its own colours,
 * at its own size and stage, in the order they arrived - then a dim
 * silhouette for each place still open. The count is in the words under it;
 * here it is seen (lit against dim, never by hue alone). */
#define MSP_SCHOOL_PITCH 52
#define MSP_SCHOOL_SCALE 0.74f
#define MSP_SCHOOL_MAX   1.02f            /* the size that still fits its place (a big elder) */
static void ms_school(const tank_t *t, uint16_t *fb, int stride, int X, int Y, int W) {
    for (int i = 0; i < POP_CAP; i++) {
        float cx = X + (W - POP_CAP * MSP_SCHOOL_PITCH) / 2 + MSP_SCHOOL_PITCH / 2 + 4 + i * MSP_SCHOOL_PITCH;   /* (+4: a fish's centre sits ahead of its middle) */
        if (i < t->n_fish) {
            float sz = t->fish[i].size * MSP_SCHOOL_SCALE;
            render_fish_portrait(fb, stride, cx, Y + 50, sz > MSP_SCHOOL_MAX ? MSP_SCHOOL_MAX : sz, &t->fish[i], t->clock + i * 0.9f);
        } else render_fish_preview(fb, stride, cx, Y + 50, 0.9f * MSP_SCHOOL_SCALE, MSP_DIM, MSP_DIM, MSP_DIM, t->clock + i * 0.9f);
    }
}

/* a locked badge: the same art as a flat grey silhouette - luminance keeps
 * the shapes readable, the low alpha keeps it quiet on the ink */
static uint32_t locked_rgb(uint16_t v) {
    int r = (v >> 11) << 3, g = ((v >> 5) & 63) << 2, b = (v & 31) << 3;
    int l = (r * 77 + g * 151 + b * 28) >> 8;                /* 0..255 luminance */
    int k = 140 + l * 115 / 255;                              /* 0.55 .. 1.0, in 1/255 */
    return (uint32_t)(20 + (0x2a * k >> 8)) << 16 | (uint32_t)(20 + (0x3f * k >> 8)) << 8
         | (uint32_t)(20 + (0x45 * k >> 8));
}
static void blit_icon_locked(ctx_t *c, int x, int y, const icon_t *ic) {
    ic = icons_resolve(ic);
    for (int j = 0; j < ic->h; j++)
        for (int i = 0; i < ic->w; i++) {
            int a = ic->a[j * ic->w + i];
            if (a) px_blend(c, x + i, y + j, locked_rgb(ic->rgb[j * ic->w + i]), a * 180 >> 8);
        }
}
/* the same art at an integer scale (the detail modal), lit or as the silhouette */
static void blit_icon_scaled(ctx_t *c, int x, int y, const icon_t *ic, int s, bool lit) {
    ic = icons_resolve(ic);
    for (int j = 0; j < ic->h; j++)
        for (int i = 0; i < ic->w; i++) {
            int a = ic->a[j * ic->w + i];
            if (!a) continue;
            uint16_t v = ic->rgb[j * ic->w + i];
            for (int yy = 0; yy < s; yy++)
                for (int xx = 0; xx < s; xx++) {
                    if (lit) px565_blend(c, x + i * s + xx, y + j * s + yy, v, a);
                    else     px_blend(c, x + i * s + xx, y + j * s + yy, locked_rgb(v), a * 180 >> 8);
                }
        }
}
/* a tiny fish glyph facing right (the growth and population strips) */
static void fish_glyph(ctx_t *c, float cx, float cy, float r, uint32_t rgb) {
    float xs[3] = { cx - r * 1.2f, cx - r * 2.3f, cx - r * 2.3f }, ys[3] = { cy, cy - r * 0.9f, cy + r * 0.9f };
    fill_poly(c, xs, ys, 3, rgb);
    fill_ellipse(c, cx, cy, r * 1.6f, r, rgb, 255);
}
static void badge(ctx_t *c, int x, int y, const icon_t *ic, bool on, bool fresh) {
    if (on) blit_icon(c, x, y, ic, 255); else blit_icon_locked(c, x, y, ic);
    if (on && fresh) {
        rect_edge(c, x - 3, y - 3, MSP_ICON + 6, MSP_ICON + 6, MSP_TEAL);
        rect_edge(c, x - 4, y - 4, MSP_ICON + 8, MSP_ICON + 8, 0x3f6a72);
    }
}
static int bit_index(uint32_t bit) { int i = 0; while (bit > 1u) { bit >>= 1; i++; } return i; }
/* the art for a gate of the fry checklist: the card's trust icon, the tank's
   own badges for meals / a hold / grass / a change / a wiped glass, or NULL = the youngest
   fish itself (the GROW gate) */
static const icon_t *fry_req_icon(int kind) {
    switch (kind) {
    case FRY_REQ_TRUST:  return &icon_trust;
    case FRY_REQ_FEED:   return &icon_ms_first_feeding;
    case FRY_REQ_HOLD:   return &icon_ms_first_hold_approach;
    case FRY_REQ_CHANGE: return &icon_ms_tank_changed_someone;
    case FRY_REQ_GRASS:  return &icon_ms_first_trimming;
    case FRY_REQ_GLASS:  return &icon_ms_first_glass_cleaning;
    default:             return NULL;
    }
}
/* a gate as a badge in a 32 px cell: lit when met, the silhouette until then */
static void fry_badge(ctx_t *c, uint16_t *fb, int stride, const tank_t *t, int x, int y, const fry_req_t *r) {
    const icon_t *ic = fry_req_icon(r->kind);
    if (ic) {
        int off = (MSP_ICON - ic->w) / 2;        /* the 24 px card icon sits centred */
        if (r->met) blit_icon(c, x + off, y + off, ic, 255); else blit_icon_locked(c, x + off, y + off, ic);
    } else {                                     /* GROW: the youngest fish, its own colours once grown */
        const fish_t *f = &t->fish[t->n_fish - 1];
        float sz = f->size > 0.7f ? 0.7f : f->size;
        if (r->met) render_fish_preview(fb, stride, x + MSP_ICON / 2, y + MSP_ICON / 2, sz, f->color, f->fin, f->accent, t->clock);
        else        render_fish_preview(fb, stride, x + MSP_ICON / 2, y + MSP_ICON / 2, sz, MSP_DIM, MSP_DIM, MSP_DIM, t->clock);
    }
}

/* a chevron pointing left or right, its tip at (tx, cy): 3 px steps, 24 px tall */
static void ms_chevron(ctx_t *c, int tx, int cy, bool left, uint32_t rgb) {
    for (int i = 0; i < 4; i++) {
        int xx = left ? tx + i * 3 : tx - 3 - i * 3;
        rect_fill(c, xx, cy - 3 - i * 3, 3, 3, rgb);
        rect_fill(c, xx, cy + i * 3, 3, 3, rgb);
    }
}
/* the modal's group: how many things the arrows cycle through and where
   this one sits. nreq = the fry checklist's gate count (the caller has it). */
static int ms_group(const tank_t *t, int nreq, int *idx) {
    if (g_ms_fryrow)  { *idx = g_ms_k; return g_ms_k < 0 ? 1 : nreq; }
    if (g_ms_tankrow) { *idx = g_ms_k; return g_ms_k < 0 ? 1 : tank_badge_n(t); }
    if (g_ms_k < 0)   { *idx = g_ms_row; return t->n_fish; }    /* a fish's name: the fish */
    *idx = g_ms_k; return 6;
}
static int ms_open(const tank_t *t, int row, bool tank_row, bool fry_row, int k,
                   const fry_req_t *req, int nreq, bool staged);
static void ms_close(void);
/* the modal's box: a gate's and a fish's card are taller (a button row at the
   foot) and stand higher, clear of the CLOSE button */
static bool ms_card(const tank_t *t) { return g_ms_caption[0] && !g_ms_tip && !g_ms_fryrow && !g_ms_tankrow && g_ms_k < 0 && g_ms_row >= 0 && g_ms_row < t->n_fish; }
static int  ms_modal_y(const tank_t *t) { return g_ms_kind >= 0 || ms_card(t) ? MSP_FRY_MODAL_Y : MSP_MODAL_Y; }
static int  ms_modal_h(const tank_t *t) {
    return MSP_MODAL_H + (g_ms_caption2[0] ? 20 : 0) + (g_ms_sub[0] || ms_card(t) ? 24 : 0) + (g_ms_kind >= 0 || ms_card(t) ? MSP_HOW_H + 14 : 0) + (ms_card(t) ? 44 : 0);
}
static int  ms_card_btn_y(const tank_t *t) { return ms_modal_y(t) + ms_modal_h(t) - 10 - MSP_HOW_H; }
static int  ms_card_btn_x(int which) {            /* 0 = RENAME, 1 = SELL: the pair centred */
    return MSP_MODAL_X + (MSP_MODAL_W - 2 * MSP_CARD_BTN_W - MSP_CARD_BTN_GAP) / 2 + which * (MSP_CARD_BTN_W + MSP_CARD_BTN_GAP);
}
/* the arrows: the previous / next thing in the modal's group, wrapping */
static void ms_step(const tank_t *t, int dir) {
    fry_req_t req[FRY_REQ_MAX]; bool staged;
    int nreq = progression_next_fry(t, req, &staged);
    int idx, n = ms_group(t, nreq, &idx);
    if (n < 2) return;
    g_ms_sell_armed = false; g_ms_sell_asked = false;
    idx = (idx + dir + n) % n;
    if (!g_ms_fryrow && !g_ms_tankrow && g_ms_k < 0) ms_open(t, idx, false, false, -1, req, nreq, staged);
    else ms_open(t, g_ms_row, g_ms_tankrow, g_ms_fryrow, idx, req, nreq, staged);
}

void render_milestones(const tank_t *t, uint16_t *fb, int stride) {
    ctx_t c = ctx_page(fb, stride);
    for (int y = 0; y < TANK_H; y++)
        for (int x = 0; x < TANK_W; x++) fb[y * stride + x] = rgb565(MSP_INK, 1);
    button(&c, 24, JOURNAL_ENTRY_Y, 128, 18, 0x1c2f36, MSP_TEAL, "JOURNAL", 2);
    button(&c, 160, JOURNAL_ENTRY_Y, 128, 18, 0x1c2f36, MSP_TEAL, "FISH BANK", 2);
    button(&c, 296, JOURNAL_ENTRY_Y, 128, 18, 0x1c2f36, MSP_TEAL, "GAMES", 2);
    for (int i = 0; i < t->n_fish; i++) {
        const fish_t *f = &t->fish[i];
        int top = MSP_ROW_Y0 + i * MSP_ROW_H;
        render_fish_portrait(fb, stride, MSP_FISH_X, top + MSP_ROW_MID, f->size, f, t->clock);
        draw_text(&c, 92, top + 2, 2, 0xffffff, f->name);
        static const int GX[4] = { 96, 111, 129, 151 };   /* each glyph's own pitch: a 4 px gap as they grow */
        for (int s = 0; s < 4; s++)          /* growth strip: fry -> elder, lit up to the stage reached */
            fish_glyph(&c, GX[s], top + MSP_ROW_STRIP, 2.2f + s * 0.9f, s <= (int)f->stage ? f->accent : MSP_DIM);
        for (int k = 0; k < 6; k++) {
            uint32_t bit = FISH_BADGES[k].bit;
            badge(&c, MSP_BADGE_X0 + k * MSP_BADGE_DX, top + MSP_ROW_BADGE, FISH_BADGES[k].icon,
                  (f->ms_bits & bit) != 0, (f->ms_seen & bit) == 0);
        }
    }
    /* the NEW FRY row (2026-09-14): under the last fish while the tank can
       still grow - what the next arrival needs, as badges that light up when
       met (the fry-to-be a silhouette at the left, a tick per gate under
       its name, a filling bar under each badge still owed) */
    fry_req_t req[FRY_REQ_MAX]; bool staged;
    int nreq = progression_next_fry(t, req, &staged);
    if (nreq > 0) {
        int top = MSP_ROW_Y0 + t->n_fish * MSP_ROW_H;
        uint32_t fry_rgb = staged ? MSP_TEAL : MSP_DIM;
        render_fish_preview(fb, stride, MSP_FISH_X, top + MSP_ROW_MID, 0.55f, fry_rgb, fry_rgb, fry_rgb, t->clock);
        draw_text(&c, 92, top + 2, 2, MSP_TEAL, "NEW FRY");
        for (int k = 0; k < nreq; k++)           /* one tick per gate, lit when met */
            rect_fill(&c, 96 + k * 14, top + MSP_ROW_STRIP - 3, 10, 6, req[k].met ? MSP_TEAL : MSP_DIM);
        for (int k = 0; k < nreq; k++) {
            int x = MSP_BADGE_X0 + k * MSP_BADGE_DX;
            fry_badge(&c, fb, stride, t, x, top + MSP_ROW_BADGE, &req[k]);
            if (!req[k].met) {                   /* the bar: how far along */
                int w = (int)(MSP_ICON * req[k].frac + 0.5f);
                rect_fill(&c, x, top + MSP_ROW_BAR, MSP_ICON, 2, MSP_DIM);
                if (w > 0) rect_fill(&c, x, top + MSP_ROW_BAR, w, 2, MSP_TEAL);
            }
        }
    }
    /* the tank's row: the sand dollar at the left (the fish rows' portrait
       slot) opens the shop (2026-09-15) */
    for (int x = 24; x < PAGE_W - 24; x++) px_blend(&c, x, MSP_TANK_Y - 4, MSP_DIM, 200);
    blit_icon(&c, MSP_SD_X, MSP_TANK_Y, &icon_ms_sand_dollar, 255);
    {   /* the balance under the coin (Strato, 2026-09-15), centred on it - the
           only room: the divider and the last row sit above, 12 px to the left */
        char bal[16]; snprintf(bal, sizeof bal, "%d", (int)t->sd_balance);
        draw_text(&c, MSP_SD_X + (MSP_ICON - text_w(bal, 2)) / 2, MSP_TANK_Y + 34, 2, 0xffffff, bal);
    }
    draw_text(&c, 92, MSP_TANK_Y + 2, 2, MSP_TEAL, "TANK");
    for (int k = 0; k < POP_CAP; k++)        /* population strip: who is here, who could still arrive */
        fish_glyph(&c, 96 + k * 14, MSP_TANK_Y + 30, 2.8f, k < t->n_fish ? MSP_TEAL : MSP_DIM);
    int nb = tank_badge_n(t), np = tank_pages(t);
    if (g_ms_tpage >= np) g_ms_tpage = 0;
    for (int j = 0; j < MSP_PER_ROW; j++) {
        int k = g_ms_tpage * MSP_PER_ROW + j;
        if (k >= nb) break;
        const badge_t *tb = tank_badge(t, k);
        badge(&c, MSP_BADGE_X0 + j * MSP_BADGE_DX, MSP_TANK_Y + 4, tb->icon,
              (t->tank_ms_bits & tb->bit) != 0, (t->tank_ms_seen & tb->bit) == 0);
    }
    if (np > 1) {   /* the page arrow: toward the next page, back from the last; the badges' two-tone
                       ring round it while another page holds a badge not seen yet */
        bool last = g_ms_tpage == np - 1, fresh = false;
        for (int k = 0; k < nb; k++)
            if (k / MSP_PER_ROW != g_ms_tpage && (t->tank_ms_bits & ~t->tank_ms_seen & tank_badge(t, k)->bit)) fresh = true;
        int ax = MSP_TPG_X + 4, ay = MSP_TANK_Y + 4, aw = 16;
        ms_chevron(&c, last ? ax + 2 : ax + aw - 2, ay + MSP_ICON / 2, last, 0xffffff);
        if (fresh) { rect_edge(&c, ax - 3, ay - 3, aw + 6, MSP_ICON + 6, MSP_TEAL);
                     rect_edge(&c, ax - 4, ay - 4, aw + 8, MSP_ICON + 8, 0x3f6a72); }
        for (int p = 0; p < np; p++)        /* a pip per page under it, the current one lit */
            rect_fill(&c, ax + aw / 2 - np * 3 + p * 6 + 1, MSP_TANK_Y + 40, 4, 2, p == g_ms_tpage ? MSP_TEAL : MSP_DIM);
    }
    /* the way out: a CLOSE button in the prompt's calm dress (a tap anywhere
       else never drops the page - too much to tap for that) */
    button(&c, MSP_CLOSE_X, MSP_CLOSE_Y, MSP_CLOSE_W, MSP_CLOSE_H, 0x1c2f36, MSP_TEAL, "CLOSE", 2);
    button(&c, MSP_SET_X, MSP_SET_Y, MSP_SET_W, MSP_CLOSE_H, 0x1c2f36, MSP_TEAL, "SETTINGS", 2);   /* bottom left (2026-09-15) */
    button(&c, MSP_UPG_X, MSP_CLOSE_Y, MSP_UPG_W, MSP_CLOSE_H, 0x1c2f36, MSP_TEAL, "UPGRADES", 2);   /* the shop, between them */
    /* a modal up: the page under it is out of reach (any tap only closes the
       modal), so it LOOKS out of reach - every pixel at half (Strato: with
       CLOSE lit it looked like you could still tap it). One shift per
       pixel, so cheap enough for every frame. */
    if (g_ms_caption[0])
        for (int y = 0; y < TANK_H; y++)
            for (int x = 0; x < TANK_W; x++) fb[y * stride + x] = (uint16_t)((fb[y * stride + x] >> 1) & 0x7bef);
    /* the detail modal, in the reset prompt's dress: the art at 2x, a title
       (the fish's name / TANK, or NOT YET), the milestone's words */
    if (g_ms_caption[0] && g_ms_tip) {
        /* the tip page: HOW, then the gate's lines; any tap closes it */
        const int X = MSP_MODAL_X, Y = MSP_FRY_MODAL_Y, W = MSP_MODAL_W;
        const char *const *tip = progression_fry_tip(g_ms_kind);
        int n = 0; while (tip[n]) n++;
        const int H = 56 + n * 20 + 16;
        rect_fill(&c, X, Y, W, H, 0x04141a);
        rect_edge(&c, X, Y, W, H, MSP_TEAL); rect_edge(&c, X + 1, Y + 1, W - 2, H - 2, 0x1c2f36);
        char title[24]; snprintf(title, sizeof title, "%s: HOW", g_ms_title);
        draw_text(&c, X + (W - text_w(title, 3)) / 2, Y + 16, 3, 0xffffff, title);
        for (int i = 0; i < n; i++) draw_text(&c, X + (W - text_w(tip[i], 2)) / 2, Y + 56 + i * 20, 2, MSP_TEAL, tip[i]);
    } else if (g_ms_caption[0]) {
        const int X = MSP_MODAL_X, W = MSP_MODAL_W;
        const int Y = ms_modal_y(t), H = ms_modal_h(t);
        const bool card = ms_card(t);
        if (card) {                                /* a fish's card is live: its name and stage as they are now, what it
                                                      would fetch (or why it stays), and the two buttons */
            const fish_t *f = &t->fish[g_ms_row];
            bool can = progression_fish_sellable(t, g_ms_row);
            int worth = progression_fish_value(t, g_ms_row);
            if (!can) g_ms_sell_armed = false; else g_ms_sell_asked = false;
            snprintf(g_ms_title, sizeof g_ms_title, "%s", f->name);
            snprintf(g_ms_caption, sizeof g_ms_caption, "%s", STAGE_WORDS[f->stage & 3]);
            if (g_ms_sell_armed) snprintf(g_ms_sub, sizeof g_ms_sub, "TAP AGAIN TO SELL");
            else if (can) snprintf(g_ms_sub, sizeof g_ms_sub, "WORTH %d SAND DOLLARS", worth);
            else if (g_ms_sell_asked) snprintf(g_ms_sub, sizeof g_ms_sub, t->n_fish <= FISH_KEEP_MIN ? "KEEP AT LEAST TWO FISH" : "NAME THE NEW FRY FIRST");
            else g_ms_sub[0] = 0;                  /* (the line's room stays: the buttons never move) */
        }
        rect_fill(&c, X, Y, W, H, 0x04141a);
        rect_edge(&c, X, Y, W, H, MSP_TEAL); rect_edge(&c, X + 1, Y + 1, W - 2, H - 2, 0x1c2f36);
        if (g_ms_kind >= 0)                        /* the way further in: HOW?, centred at the foot (Strato: bottom
                                                      right sat too close to CLOSE for comfort) */
            button(&c, X + (W - MSP_HOW_W) / 2, Y + H - 10 - MSP_HOW_H, MSP_HOW_W, MSP_HOW_H, 0x1c2f36, MSP_TEAL, "HOW?", 2);
        if (card) {
            const int by = ms_card_btn_y(t);
            button(&c, X + 24, by - 44, W - 48, MSP_HOW_H, 0x1c2f36, MSP_TEAL, "CUSTOMIZE", 2);
            button(&c, ms_card_btn_x(0), by, MSP_CARD_BTN_W, MSP_HOW_H, 0x1c2f36, MSP_TEAL, "RENAME", 2);
            if (!progression_fish_sellable(t, g_ms_row)) {           /* not for sale: the button in the page's dim ink, the words too */
                rect_fill(&c, ms_card_btn_x(1), by, MSP_CARD_BTN_W, MSP_HOW_H, 0x04141a);
                rect_edge(&c, ms_card_btn_x(1), by, MSP_CARD_BTN_W, MSP_HOW_H, MSP_DIM);
                draw_text(&c, ms_card_btn_x(1) + (MSP_CARD_BTN_W - text_w("SELL", 2)) / 2, by + (MSP_HOW_H - 14) / 2, 2, MSP_DIM, "SELL");
            } else if (g_ms_sell_armed) {
                char ok[16]; snprintf(ok, sizeof ok, "+%d OK?", progression_fish_value(t, g_ms_row));
                button(&c, ms_card_btn_x(1), by, MSP_CARD_BTN_W, MSP_HOW_H, 0x1c2f36, MSP_TEAL, ok, 2);
            } else button(&c, ms_card_btn_x(1), by, MSP_CARD_BTN_W, MSP_HOW_H, 0x1c2f36, MSP_TEAL, "SELL", 2);
        }
        if (g_ms_icon) blit_icon_scaled(&c, X + (W - g_ms_icon->w * 2) / 2, Y + 16 + (32 - g_ms_icon->w), g_ms_icon, 2, g_ms_lit);
        else if (g_ms_fish >= 0 && g_ms_fish < t->n_fish) {
            const fish_t *f = &t->fish[g_ms_fish];
            if (g_ms_lit) render_fish_portrait(fb, stride, X + W / 2, Y + 48, fminf(f->size * 1.6f, 1.5f), f, t->clock);
            else          render_fish_preview(fb, stride, X + W / 2, Y + 48, f->size * 1.6f, MSP_DIM, MSP_DIM, MSP_DIM, t->clock);
        } else if (g_ms_fry)
            render_fish_preview(fb, stride, X + W / 2, Y + 48, 0.9f, g_ms_lit ? MSP_TEAL : MSP_DIM, g_ms_lit ? MSP_TEAL : MSP_DIM, g_ms_lit ? MSP_TEAL : MSP_DIM, t->clock);
        else if (g_ms_tankrow && g_ms_k < 0) ms_school(t, fb, stride, X, Y, W);   /* TANK's tally: the school */
        draw_text(&c, X + (W - text_w(g_ms_title, 3)) / 2, Y + 92, 3, g_ms_lit ? 0xffffff : MSP_TEAL, g_ms_title);
        draw_text(&c, X + (W - text_w(g_ms_caption, 2)) / 2, Y + 124, 2, g_ms_lit ? MSP_TEAL : 0x5f8a92, g_ms_caption);
        int ly = Y + 144;
        if (g_ms_caption2[0]) { draw_text(&c, X + (W - text_w(g_ms_caption2, 2)) / 2, ly, 2, g_ms_lit ? MSP_TEAL : 0x5f8a92, g_ms_caption2); ly += 20; }
        if (g_ms_sub[0]) draw_text(&c, X + (W - text_w(g_ms_sub, 2)) / 2, ly + 4, 2, g_ms_lit ? 0xffffff : MSP_TEAL, g_ms_sub);
        {   /* the arrows (2026-09-16): the previous / next of the group at the
               top corners, in the buttons' dress, only when there is a group */
            int idx, n = ms_group(t, nreq, &idx);
            if (n > 1) {
                int ax = X + MSP_ARROW_IN, ay = Y + MSP_ARROW_IN, bx = X + W - MSP_ARROW_IN - MSP_ARROW_W;
                rect_fill(&c, ax, ay, MSP_ARROW_W, MSP_ARROW_H, 0x1c2f36);
                rect_edge(&c, ax, ay, MSP_ARROW_W, MSP_ARROW_H, MSP_TEAL); rect_edge(&c, ax + 1, ay + 1, MSP_ARROW_W - 2, MSP_ARROW_H - 2, MSP_TEAL);
                ms_chevron(&c, ax + 14, ay + MSP_ARROW_H / 2, true, 0xffffff);
                rect_fill(&c, bx, ay, MSP_ARROW_W, MSP_ARROW_H, 0x1c2f36);
                rect_edge(&c, bx, ay, MSP_ARROW_W, MSP_ARROW_H, MSP_TEAL); rect_edge(&c, bx + 1, ay + 1, MSP_ARROW_W - 2, MSP_ARROW_H - 2, MSP_TEAL);
                ms_chevron(&c, bx + MSP_ARROW_W - 14, ay + MSP_ARROW_H / 2, false, 0xffffff);
            }
        }
    }
}

int render_milestones_tap(const tank_t *t, float x, float y) {
    x = render_page_x(x); y = render_page_y(y);
    if (g_ms_caption[0]) {                       /* a modal is up */
        if (!g_ms_tip) {                         /* the arrows at its top corners: the previous / next of the group */
            fry_req_t req[FRY_REQ_MAX]; bool staged; int idx;
            int n = ms_group(t, progression_next_fry(t, req, &staged), &idx);
            const int Y = ms_modal_y(t);
            if (n > 1 && y >= Y - 12 && y < Y + MSP_ARROW_IN + MSP_ARROW_H + 24) {
                if (x < MSP_MODAL_X + MSP_ARROW_HIT)               { ms_step(t, -1); return MS_TAP_KEPT; }
                if (x >= MSP_MODAL_X + MSP_MODAL_W - MSP_ARROW_HIT) { ms_step(t, +1); return MS_TAP_KEPT; }
            }
        }
        if (ms_card(t)) {                        /* a fish's card: RENAME / SELL across its foot - each button's half of
                                                    the panel, the HOW? button's depth (a miss costs the modal) */
            const int by = ms_card_btn_y(t), fish = g_ms_row;
            if (x >= MSP_MODAL_X + 16 && x < MSP_MODAL_X + MSP_MODAL_W - 16 && y >= by - 48 && y < by - 8) {
                ms_close(); return MS_TAP_CUSTOM + fish;
            }
            if (x >= MSP_MODAL_X && x < MSP_MODAL_X + MSP_MODAL_W && y >= by - MSP_HOW_SLOP_UP && y < by + MSP_HOW_H + MSP_HOW_SLOP_DN) {
                if (x < MSP_MODAL_X + MSP_MODAL_W / 2) { ms_close(); return MS_TAP_RENAME + fish; }   /* the platform opens the wheel */
                if (!progression_fish_sellable(t, fish)) { g_ms_sell_asked = true; return MS_TAP_KEPT; }   /* the line says why, from now on */
                if (!g_ms_sell_armed) { g_ms_sell_armed = true; g_ms_sell_clock = t->clock; return MS_TAP_KEPT; }
                if (t->clock - g_ms_sell_clock < MSP_SELL_WAIT_S) return MS_TAP_KEPT;                /* a doubled tap: still armed */
                ms_close(); return MS_TAP_SELL + fish;                                               /* the platform sells it */
            }
        }
        if (g_ms_kind >= 0 && !g_ms_tip) {       /* a gate's: the HOW? button opens its tip page */
            const int H = ms_modal_h(t);
            const int bx = MSP_MODAL_X + (MSP_MODAL_W - MSP_HOW_W) / 2, by = MSP_FRY_MODAL_Y + H - 10 - MSP_HOW_H;
            if (x >= bx - MSP_HOW_SLOP_X && x < bx + MSP_HOW_W + MSP_HOW_SLOP_X && y >= by - MSP_HOW_SLOP_UP && y < by + MSP_HOW_H + MSP_HOW_SLOP_DN) {
                g_ms_tip = true; return MS_TAP_KEPT; }
        }
        ms_close(); return MS_TAP_KEPT;                  /* any other tap: back to the page */
    }
    if (y >= JOURNAL_ENTRY_Y && x >= 24 && x < 152) return MS_TAP_JOURNAL;
    if (y >= JOURNAL_ENTRY_Y && x >= 160 && x < 288) return MS_TAP_BANK;
    if (y >= JOURNAL_ENTRY_Y && x >= 296 && x < 424) return MS_TAP_GAMES;
    if (x >= MSP_CLOSE_X - 8 && y >= MSP_CLOSE_Y - 4) return MS_TAP_CLOSE;      /* slop out to the glass edge */
    if (x < MSP_SET_X + MSP_SET_W + 8 && y >= MSP_CLOSE_Y - 4) return MS_TAP_SETTINGS;   /* the settings page */
    if (y >= MSP_CLOSE_Y - 4) return MS_TAP_SHOP;                                        /* UPGRADES: the rest of the strip is the shop */
    int row = -1; bool tank_row = false, fry_row = false;
    fry_req_t req[FRY_REQ_MAX]; bool staged;
    int nreq = progression_next_fry(t, req, &staged);
    if (y >= MSP_ROW_Y0 - 2 && y < MSP_ROW_Y0 + N_FISH_MAX * MSP_ROW_H) {
        row = (int)((y - MSP_ROW_Y0) / MSP_ROW_H);
        if (row < 0) row = 0;
        if (row == t->n_fish && nreq > 0) fry_row = true;   /* the NEW FRY row */
        else if (row >= t->n_fish) return MS_TAP_NONE;      /* an empty row */
    } else if (y >= MSP_TANK_Y - 6 && y < MSP_CLOSE_Y - 4) tank_row = true;   /* down to the button strip: fingers near the
                                                                              bottom bezel report LOW */
    else return MS_TAP_NONE;
    if (tank_row && x >= MSP_TPG_X && tank_pages(t) > 1) {   /* the page arrow: the next page, wrapping */
        g_ms_tpage = (g_ms_tpage + 1) % tank_pages(t); return MS_TAP_KEPT; }
    int k;                                           /* badge column, or -1 for the name / strip cluster */
    if (x >= MSP_BADGE_X0 - 4 && x < MSP_BADGE_X0 + 6 * MSP_BADGE_DX) {
        k = (int)((x - MSP_BADGE_X0 + 4) / MSP_BADGE_DX);
        if (k > 5) k = 5;
    } else if (x >= 20 && x < MSP_BADGE_X0 - 4) k = -1;
    else return MS_TAP_NONE;
    if (tank_row && x < 88) return MS_TAP_SHOP;      /* the sand dollar: the shop page */
    if (tank_row && k >= 0) {                        /* this page's badge; an empty cell on the last page is nothing */
        k += g_ms_tpage * MSP_PER_ROW;
        if (k >= tank_badge_n(t)) return MS_TAP_NONE;
    }
    return ms_open(t, row, tank_row, fry_row, k, req, nreq, staged);
}
/* open the detail modal for a row's name / strip (k < 0) or its k-th badge
   or gate: the words, the art, and where it came from (the arrows' group) */
static int ms_open(const tank_t *t, int row, bool tank_row, bool fry_row, int k,
                   const fry_req_t *req, int nreq, bool staged) {
    g_ms_caption2[0] = 0; g_ms_sub[0] = 0; g_ms_fry = false; g_ms_kind = -1; g_ms_tip = false; g_ms_sell_armed = false; g_ms_sell_asked = false;
    g_ms_row = row; g_ms_k = k; g_ms_tankrow = tank_row; g_ms_fryrow = fry_row;
    if (fry_row) {
        if (k < 0) {                                 /* the name: the tally, and when it comes */
            int met = 0; for (int i = 0; i < nreq; i++) met += req[i].met;
            snprintf(g_ms_title, sizeof g_ms_title, "NEW FRY");
            if (staged) { snprintf(g_ms_caption, sizeof g_ms_caption, "EVERY STEP IS DONE. A FRY");   /* the spawning, 2026-09-24 */
                          snprintf(g_ms_caption2, sizeof g_ms_caption2, "WILL BE BORN IN THE GRASS");
                          snprintf(g_ms_sub, sizeof g_ms_sub, "ON ITS WAY"); }
            else { snprintf(g_ms_caption, sizeof g_ms_caption, "WHEN ALL NEEDS ARE MET, A");     /* Strato's words, 2026-09-14 */
                   snprintf(g_ms_caption2, sizeof g_ms_caption2, "NEW FRY IS READY TO BE BORN");
                   snprintf(g_ms_sub, sizeof g_ms_sub, "%d OF %d DONE", met, nreq); }
            g_ms_lit = staged; g_ms_icon = NULL; g_ms_fish = -1; g_ms_fry = true;
        } else if (k < nreq) {                       /* a gate: the words, and where it stands */
            const fry_req_t *r = &req[k];
            snprintf(g_ms_title, sizeof g_ms_title, "%s", r->title);
            snprintf(g_ms_caption, sizeof g_ms_caption, "%s", r->words);
            snprintf(g_ms_caption2, sizeof g_ms_caption2, "%s", r->words2);
            snprintf(g_ms_sub, sizeof g_ms_sub, "%s", r->progress);
            g_ms_lit = r->met; g_ms_icon = fry_req_icon(r->kind); g_ms_kind = r->kind;
            g_ms_fish = g_ms_icon ? -1 : t->n_fish - 1;
        } else return MS_TAP_NONE;
    } else if (tank_row) {
        if (k < 0) {
            snprintf(g_ms_title, sizeof g_ms_title, "TANK");
            snprintf(g_ms_caption, sizeof g_ms_caption, "%d OF %d FISH SO FAR", t->n_fish, POP_CAP);
            g_ms_lit = true; g_ms_icon = NULL; g_ms_fish = -1;
        } else {
            uint32_t bit = tank_badge(t, k)->bit; bool on = (t->tank_ms_bits & bit) != 0;
            g_ms_tpage = k / MSP_PER_ROW;            /* the arrows can cross pages: the row follows */
            snprintf(g_ms_title, sizeof g_ms_title, on ? "TANK" : "NOT YET");
            snprintf(g_ms_caption, sizeof g_ms_caption, "%s", TMS_NAMES[bit_index(bit)]);
            g_ms_lit = on; g_ms_icon = tank_badge(t, k)->icon; g_ms_fish = -1;
        }
    } else {
        const fish_t *f = &t->fish[row];
        if (k < 0) {
            snprintf(g_ms_title, sizeof g_ms_title, "%s", f->name);
            snprintf(g_ms_caption, sizeof g_ms_caption, "%s", STAGE_WORDS[f->stage & 3]);
            g_ms_lit = true; g_ms_icon = NULL; g_ms_fish = row;
        } else {
            uint32_t bit = FISH_BADGES[k].bit; bool on = (f->ms_bits & bit) != 0;
            snprintf(g_ms_title, sizeof g_ms_title, "%s", on ? f->name : "NOT YET");
            snprintf(g_ms_caption, sizeof g_ms_caption, "%s", MS_NAMES[bit_index(bit)]);
            g_ms_lit = on; g_ms_icon = FISH_BADGES[k].icon; g_ms_fish = -1;
        }
    }
    return MS_TAP_KEPT;
}
/* a sideways swipe along the TANK row: the next page (leftward) or the
   previous (rightward), no wrap. False = not a page swipe (the platforms then
   treat the release as they did before). */
bool render_milestones_swipe(const tank_t *t, float x, float y, float dx) {
    (void)x; y = render_page_y(y);
    if (g_ms_caption[0] || tank_pages(t) < 2 || y < MSP_TANK_Y - 6 || y >= MSP_CLOSE_Y - 4) return false;
    int p = g_ms_tpage + (dx < 0 ? 1 : -1);
    if (p >= 0 && p < tank_pages(t)) g_ms_tpage = p;
    return true;
}
/* the modal down, the page (and the tank row's page) kept */
static void ms_close(void) { g_ms_sell_armed = false; g_ms_sell_asked = false; g_ms_kind = -1; g_ms_tip = false; g_ms_caption[0] = 0; g_ms_caption2[0] = 0; g_ms_title[0] = 0; g_ms_sub[0] = 0; g_ms_icon = NULL; g_ms_fish = -1; g_ms_fry = false;
                                     g_ms_row = -1; g_ms_k = -1; g_ms_tankrow = false; g_ms_fryrow = false; }
void render_milestones_leave(void) { ms_close(); g_ms_tpage = 0; }
void render_milestones_show_fish(const tank_t *t, int fish) {
    if (fish >= 0 && fish < t->n_fish) ms_open(t, fish, false, false, -1, NULL, 0, false);
}
bool render_milestones_card(const tank_t *t, int *fish, int *rename_x, int *sell_x, int *btn_y) {
    if (!ms_card(t)) return false;
    if (fish) *fish = g_ms_row;
    if (rename_x) *rename_x = render_frame_x(ms_card_btn_x(0) + MSP_CARD_BTN_W / 2);
    if (sell_x)   *sell_x   = render_frame_x(ms_card_btn_x(1) + MSP_CARD_BTN_W / 2);
    if (btn_y)    *btn_y    = render_frame_y(ms_card_btn_y(t) + MSP_HOW_H / 2);
    return true;
}
bool render_milestones_arrow(const tank_t *t, bool right, int *x, int *y) {
    if (!g_ms_caption[0] || g_ms_tip) return false;
    if (x) *x = render_frame_x(right ? MSP_MODAL_X + MSP_MODAL_W - MSP_ARROW_IN - MSP_ARROW_W / 2 : MSP_MODAL_X + MSP_ARROW_IN + MSP_ARROW_W / 2);
    if (y) *y = render_frame_y(ms_modal_y(t) + MSP_ARROW_IN + MSP_ARROW_H / 2);
    return true;
}
void render_milestones_row(int row, int *name_x, int *y) {
    if (name_x) *name_x = render_frame_x(100);
    if (y)      *y      = render_frame_y(MSP_ROW_Y0 + row * MSP_ROW_H + MSP_ROW_MID);
}

/* ---- announcement modal (notice.h, 2026-09-15) ----
 * The milestones page's detail modal, over the live tank: the badge art at
 * 2x (a fish's own sprite for a stage), the name, the caption, and a thin
 * bar along the foot that runs out with the notice's time. */
void render_notice(const tank_t *t, uint16_t *fb, int stride, int kind, int fish, uint32_t bit, float frac_left) {
    ctx_t c = ctx_page(fb, stride);
    const int X = MSP_MODAL_X, W = MSP_MODAL_W, Y = MSP_MODAL_Y, H = MSP_MODAL_H;
    rect_fill(&c, X, Y, W, H, 0x04141a);
    rect_edge(&c, X, Y, W, H, MSP_TEAL); rect_edge(&c, X + 1, Y + 1, W - 2, H - 2, 0x1c2f36);
    char title[FISH_NAME_MAX + 16] = "THE TANK", caption[40] = "";
    const char *caption2 = NULL;                              /* a second line: the title and both move up */
    const icon_t *ic = NULL;
    const fish_t *f = fish >= 0 && fish < t->n_fish ? &t->fish[fish] : NULL;
    if (kind == 5) {                                          /* NOTICE_LIGHTS_OUT (2026-10-03): the first double-tap that turned the light off */
        snprintf(title, sizeof title, "LIGHTS OUT");
        snprintf(caption, sizeof caption, "YOU DOUBLE-TAPPED THE GLASS");
        caption2 = "DOUBLE-TAP TO TURN IT ON";
        /* a crescent moon: a disc with a bite out of it */
        const int R = 20, PX = X + W / 2 - 3, PY = Y + 36;
        for (int dy = -R; dy <= R; dy++) for (int dx = -R; dx <= R; dx++) {
            int bx = dx - 10, by = dy + 7;
            if (dx * dx + dy * dy <= R * R && bx * bx + by * by > 17 * 17) rect_fill(&c, PX + dx, PY + dy, 1, 1, MSP_TEAL);
        }
    } else if (kind == 4) {                                   /* NOTICE_UPDATED (2026-09-30): the first boot of a new release */
        snprintf(title, sizeof title, "UPDATED");
        snprintf(caption, sizeof caption, "YOUR TANK IS NOW V%s", PT_RELEASE);
        /* a tick in a ring, teal: the shape says it, not the hue */
        const int R = 22, PX = X + W / 2, PY = Y + 48;
        ring(&c, PX, PY, R, MSP_TEAL); ring(&c, PX, PY, R - 1, MSP_TEAL);
        for (int i = 0; i < 6; i++)  rect_fill(&c, PX - 11 + i, PY + i - 1, 3, 3, MSP_TEAL);
        for (int i = 0; i < 12; i++) rect_fill(&c, PX - 6 + i, PY + 4 - i, 3, 3, MSP_TEAL);
    } else if (kind == 2) {                                   /* NOTICE_STAGE */
        if (f) { snprintf(title, sizeof title, "%s", f->name);
                 snprintf(caption, sizeof caption, "IS NOW %s %s", f->stage == STAGE_ADULT || f->stage == STAGE_ELDER ? "AN" : "A", STAGE_WORDS[f->stage & 3]);
                 render_fish_portrait(fb, stride, X + W / 2, Y + 48, fminf(f->size * 1.6f, 1.5f), f, t->clock); }
    } else if (kind == 1) {                                   /* NOTICE_TANK_MILESTONE */
        for (int k = 0; k < TANK_BADGE_N; k++) if (TANK_BADGES[k].bit == bit) ic = TANK_BADGES[k].icon;
        int bi = 0; while (bi < 31 && !(bit & (1u << bi))) bi++;
        snprintf(caption, sizeof caption, "%s", bi < TMS_COUNT ? TMS_NAMES[bi] : "");
        if (bit == TMS_FIRST_SALE) caption2 = "FRY NOW IN THE SHOP";   /* what the sale opened (0.3.5) */
        if (!ic && t->n_fish) {                               /* a population milestone: the newest fish */
            const fish_t *n = &t->fish[t->n_fish - 1];
            render_fish_preview(fb, stride, X + W / 2, Y + 48, n->size * 1.6f, n->color, n->fin, n->accent, t->clock);
        }
    } else {                                                  /* NOTICE_MILESTONE */
        for (int k = 0; k < 6; k++) if (FISH_BADGES[k].bit == bit) ic = FISH_BADGES[k].icon;
        int bi = 0; while (bi < 31 && !(bit & (1u << bi))) bi++;
        if (f) snprintf(title, sizeof title, "%s", f->name);
        snprintf(caption, sizeof caption, "%s", bi < MS_FISH_COUNT ? MS_NAMES[bi] : "");
        if (!ic && f) render_fish_portrait(fb, stride, X + W / 2, Y + 48, fminf(f->size * 1.6f, 1.5f), f, t->clock);
    }
    if (ic) blit_icon_scaled(&c, X + (W - ic->w * 2) / 2, Y + 16 + (32 - ic->w), ic, 2, true);
    draw_text(&c, X + (W - text_w(title, 3)) / 2, Y + (caption2 ? 70 : 92), 3, 0xffffff, title);
    draw_text(&c, X + (W - text_w(caption, 2)) / 2, Y + (caption2 ? 104 : 124), 2, MSP_TEAL, caption);
    if (caption2) draw_text(&c, X + (W - text_w(caption2, 2)) / 2, Y + 126, 2, MSP_TEAL, caption2);
    if (frac_left < 0) frac_left = 0;
    if (frac_left > 1) frac_left = 1;
    rect_fill(&c, X + 2, Y + H - 4, (int)((W - 4) * frac_left), 2, 0x1c2f36);
}

/* ---- the shop (2026-09-15): sand dollars, and what they buy ----
 * The milestones page's dress. A 64 px coin and the balance at the top, a
 * row per item (SD_ITEMS), HOW TO EARN bottom left, CLOSE bottom right. A
 * row opens the item's modal (the art at 2x, the words, the price, UNLOCK -
 * dim when the balance is short, IN THE TANK once owned); HOW TO EARN a
 * modal of the sources. The page dims under a modal like the milestones
 * page. Fingers land low here too: the row bands run 8 px above and to the
 * next row, the buttons' bands to the glass edge. */
/* (the page's numbers, SHP_*: render.h) */
static int  g_shp_modal = -1;        /* the item whose modal is up, or -1 */
static bool g_shp_earn;              /* the HOW TO EARN modal is up */
static bool g_shp_sell_armed;        /* SELL tapped once: the next tap on it sells */
static const icon_t *shop_icon(int item) { return item == 0 ? &icon_shop_plant : item == 1 ? &icon_shop_snail : item == 2 ? &icon_shop_castle : item == 3 ? &icon_shop_coral : item == 4 ? &icon_shop_cluster : item == 5 ? &icon_shop_shrimp : item == 6 ? &icon_shop_urchin : item == 7 ? &icon_shop_seahorse : &icon_shop_pumpkin; }
/* pages (2026-09-23, the fourth item): SHP_PER_PAGE rows fit between the
 * coin and the foot buttons once the filler caption went (HOW TO EARN says
 * the same). With more items than a page holds, arrows at the header's
 * right flip through the pages; with one page nothing shows. */
static int g_shp_page;
static void shop_arrow(ctx_t *c, int x, int y, bool right, uint32_t rgb) {   /* a chevron in a button */
    button(c, x, y, SHP_ARROW_W, SHP_ARROW_H, 0x1c2f36, rgb, "", 2);
    int cx = x + SHP_ARROW_W / 2, cy = y + SHP_ARROW_H / 2;
    for (int i = 0; i < 7; i++) {                            /* a chevron: two strokes meeting at the tip */
        int x = right ? cx - 3 + i : cx + 3 - i;
        rect_fill(c, x, cy - 6 + i, 2, 1, rgb);
        rect_fill(c, x, cy + 6 - i, 2, 1, rgb);
    }
}
static void price_tag(ctx_t *c, int x, int y, int price, uint32_t rgb) {   /* the small coin + the number */
    blit_icon(c, x, y - 1, &icon_shop_sand_dollar_16, 255);
    char n[16]; snprintf(n, sizeof n, "%d", price);
    draw_text(c, x + 20, y, 2, rgb, n);
}
/* the FRY row (0.3.5): the first row once a fish has been sold. Rows are
 * numbered across the pages; row 0 is FRY when it shows, the items after */
#define SHP_MODAL_FRY 99             /* g_shp_modal: the fry's modal */
static int g_shp_fry_pick;           /* the fry picked in its modal */
static bool shp_fry(const tank_t *t) { return progression_fry_for_sale(t); }
static int  shp_rows(const tank_t *t) { return progression_shop_count(t) + shp_fry(t); }
static int  shp_pages(const tank_t *t) { return (shp_rows(t) + SHP_PER_PAGE - 1) / SHP_PER_PAGE; }
static int  shp_item(const tank_t *t, int row) {
    return progression_shop_item(t, shp_fry(t) ? row - 1 : row);
}
static void fry_hours(char *out, size_t n) {   /* "NEW FRY IN 14 H" / "IN 40 MIN" */
    float s = progression_fry_new_in_s();
    if (s >= 3600) snprintf(out, n, "NEW FRY IN %d H", (int)((s + 1799) / 3600));
    else snprintf(out, n, "NEW FRY IN %d MIN", (int)(s / 60) + 1);
}
static void fry_draw(uint16_t *fb, int stride, float x, float y, float size, const fry_offer_t *o, float clock) {
    fish_t f; memset(&f, 0, sizeof f);
    f.stage = STAGE_FRY; f.color = o->body; f.fin = tank_fin_for(o->body); f.accent = o->accent;
    f.variety = o->variety; f.pattern = o->pattern;
    render_fish_portrait(fb, stride, x, y, size, &f, clock);
}
static int fry_first_left(void) {
    for (int k = 0; k < FRY_OFFER_N; k++) { fry_offer_t o; progression_fry_offer(k, &o); if (!o.sold) return k; }
    return -1;
}
static void shop_fry_row(ctx_t *c, const tank_t *t, uint16_t *fb, int stride, int top) {
    int k = fry_first_left();
    bool room = progression_fry_room(t), can = t->sd_balance >= SD_PRICE_FRY;
    if (k >= 0) { fry_offer_t o; progression_fry_offer(k, &o); fry_draw(fb, stride, 48, top + 16, 0.9f, &o, t->clock); }
    draw_text(c, 76, top + 2, 2, 0xffffff, "FRY");
    if (k < 0) { char l[24]; fry_hours(l, sizeof l); draw_text(c, 76, top + 20, 2, MSP_DIM, l); }
    else price_tag(c, 76, top + 20, SD_PRICE_FRY, can && room ? MSP_TEAL : MSP_DIM);
    const char *b = k < 0 ? "SOLD OUT" : !room ? "FULL" : "CHOOSE";
    if (k >= 0 && room && can) button(c, SHP_BTN_X, top, SHP_BTN_W, SHP_BTN_H, 0x1c2f36, MSP_TEAL, b, 2);
    else button(c, SHP_BTN_X, top, SHP_BTN_W, SHP_BTN_H, 0x1c2f36, MSP_DIM, b, 2);
}
static void shop_fry_modal(ctx_t *c, const tank_t *t, uint16_t *fb, int stride) {
    const int X = SHP_MODAL_X, Y = SHP_MODAL_Y, W = SHP_MODAL_W, H = SHP_MODAL_H;
    rect_fill(c, X, Y, W, H, 0x04141a);
    rect_edge(c, X, Y, W, H, MSP_TEAL); rect_edge(c, X + 1, Y + 1, W - 2, H - 2, 0x1c2f36);
    draw_text(c, X + (W - text_w("FRY FOR SALE", 3)) / 2, Y + 14, 3, 0xffffff, "FRY FOR SALE");
    for (int k = 0; k < FRY_OFFER_N; k++) {
        fry_offer_t o; progression_fry_offer(k, &o);
        int bx = SHP_FRY_BOX_X(k), by = SHP_FRY_BOX_Y;
        bool lit = !o.sold && k == g_shp_fry_pick;
        rect_fill(c, bx, by, SHP_FRY_BOX_W, SHP_FRY_BOX_H, lit ? 0x14303a : 0x0b1d23);
        uint32_t e = lit ? 0xffffff : o.sold ? 0x1c2f36 : 0x4d6a72;
        rect_edge(c, bx, by, SHP_FRY_BOX_W, SHP_FRY_BOX_H, e);
        if (lit) rect_edge(c, bx + 1, by + 1, SHP_FRY_BOX_W - 2, SHP_FRY_BOX_H - 2, e);
        if (o.sold) continue;                                    /* bought: a blank until the fresh three */
        fry_draw(fb, stride, bx + SHP_FRY_BOX_W / 2, by + 26, 1.0f, &o, t->clock + k * 0.7f);
        /* its colors, both: the body and the markings it grows into */
        draw_text(c, bx + (SHP_FRY_BOX_W - text_w(FISH_VARIETY_NAMES[o.variety], 2)) / 2, by + 52, 2, MSP_TEAL, FISH_VARIETY_NAMES[o.variety]);
        int sx = bx + SHP_FRY_BOX_W / 2 - 19, sy = by + SHP_FRY_BOX_H - 22;
        rect_fill(c, sx, sy, 16, 14, o.body);   rect_edge(c, sx, sy, 16, 14, 0x1c2f36);
        rect_fill(c, sx + 22, sy, 16, 14, o.accent); rect_edge(c, sx + 22, sy, 16, 14, 0x1c2f36);
    }
    bool room = progression_fry_room(t), can = t->sd_balance >= SD_PRICE_FRY;
    fry_offer_t pk; progression_fry_offer(g_shp_fry_pick, &pk);
    bool go = room && can && !pk.sold && progression_fry_left() > 0;
    char line[32]; snprintf(line, sizeof line, "%d", SD_PRICE_FRY);
    price_tag(c, X + (W - 20 - text_w(line, 2)) / 2, Y + 144, SD_PRICE_FRY, go ? 0xffffff : MSP_DIM);
    if (!room) snprintf(line, sizeof line, t->n_fish >= POP_CAP ? "THE TANK IS FULL" : "WELCOME THE NEW FRY");
    else if (!can) snprintf(line, sizeof line, "YOU HAVE %d", (int)t->sd_balance);
    else line[0] = 0;
    if (line[0]) draw_text(c, X + (W - text_w(line, 2)) / 2, Y + 164, 2, MSP_DIM, line);
    fry_hours(line, sizeof line);
    draw_text(c, X + (W - text_w(line, 2)) / 2, Y + 182, 2, 0x3f6a72, line);
    const int bx = X + (W - MSP_HOW_W) / 2, by = Y + H - 12 - MSP_HOW_H;
    if (go) button(c, bx, by, MSP_HOW_W, MSP_HOW_H, 0x1c2f36, MSP_TEAL, "BUY", 2);
    else button(c, bx, by, MSP_HOW_W, MSP_HOW_H, 0x1c2f36, MSP_DIM, "BUY", 2);
}
void render_shop(const tank_t *t, uint16_t *fb, int stride) {
    ctx_t c = ctx_page(fb, stride);
    rect_fill(&c, -PAGE_X, -PAGE_Y, PAGE_FRAME_W, PAGE_FRAME_H, MSP_INK);
    blit_icon(&c, SHP_COIN_X, SHP_COIN_Y, &icon_shop_sand_dollar_64, 255);
    draw_text(&c, SHP_HEAD_X, SHP_COIN_Y + 6, 2, MSP_TEAL, "SAND DOLLARS");
    char bal[16]; snprintf(bal, sizeof bal, "%d", (int)t->sd_balance);
    draw_text(&c, SHP_HEAD_X, SHP_COIN_Y + 28, 4, 0xffffff, bal);
    for (int x = 24; x < PAGE_W - 24; x++) px_blend(&c, x, SHP_ROW_Y0 - 10, MSP_DIM, 200);
    if (shp_pages(t) > 1) {
        shop_arrow(&c, SHP_ARROW_X0, SHP_ARROW_Y, false, g_shp_page > 0 ? MSP_TEAL : MSP_DIM);
        shop_arrow(&c, SHP_ARROW_X1, SHP_ARROW_Y, true, g_shp_page < shp_pages(t) - 1 ? MSP_TEAL : MSP_DIM);
    }
    for (int r = g_shp_page * SHP_PER_PAGE; r < shp_rows(t) && r < (g_shp_page + 1) * SHP_PER_PAGE; r++) {
        int top = SHP_ROW_Y0 + (r - g_shp_page * SHP_PER_PAGE) * SHP_ROW_DY, i = shp_item(t, r);
        if (i < 0) { shop_fry_row(&c, t, fb, stride, top); continue; }
        const sd_item_t *it = &SD_ITEMS[i];
        bool owned = progression_item_owned(t, i), can = t->sd_balance >= it->price;
        if (i >= 9 && i <= 12) draw_ornament(&c, i, 24, top, 1, false, owned, MSP_INK);
        else if (i == 13) draw_buoy_icon(&c, 24, top, 1, owned, MSP_INK);
        else if (i >= 16) draw_ornament_icon(&c, i, 24, top, 36, owned, MSP_INK);
        else if (i == 14 || i == 15) {
            int count = i == 14 ? SAND_STYLE_COUNT : WATER_STYLE_COUNT - 1;
            int width = 36 / count;
            for (int s = 0; s < count; s++) rect_fill(&c, 28 + s * width, top + 4, width, 28, shop_art_color(i == 14 ? SAND_PALETTE[s][0] : WATER_PALETTE[s + 1][0], owned, MSP_INK));
        }
        else if (owned) blit_icon(&c, 32, top, shop_icon(i), 255); else blit_icon_locked(&c, 32, top, shop_icon(i));   /* (the rows are where the bowl is wide: their own column) */
        draw_text(&c, 76, top + 2, 2, 0xffffff, it->name);
        if (owned) draw_text(&c, 76, top + 20, 2, MSP_TEAL, "IN THE TANK");
        else price_tag(&c, 76, top + 20, it->price, can ? MSP_TEAL : MSP_DIM);
        if (owned)     button(&c, SHP_BTN_X, top, SHP_BTN_W, SHP_BTN_H, MSP_INK, MSP_DIM, "IN TANK", 2);
        else if (can) button(&c, SHP_BTN_X, top, SHP_BTN_W, SHP_BTN_H, 0x1c2f36, MSP_TEAL, "UNLOCK", 2);
        else           button(&c, SHP_BTN_X, top, SHP_BTN_W, SHP_BTN_H, 0x1c2f36, MSP_DIM, "UNLOCK", 2);
    }
    button(&c, SHP_EARN_X, MSP_CLOSE_Y, SHP_EARN_W, MSP_CLOSE_H, 0x1c2f36, MSP_TEAL, "HOW TO EARN", 2);
    button(&c, 194, MSP_CLOSE_Y, 116, MSP_CLOSE_H, 0x1c2f36, MSP_TEAL, "TANKS", 2);
    button(&c, SHP_CLOSE_X, MSP_CLOSE_Y, MSP_CLOSE_W, MSP_CLOSE_H, 0x1c2f36, MSP_TEAL, "CLOSE", 2);
    if (g_shp_modal < 0 && !g_shp_earn) return;
    for (int y = 0; y < TANK_H; y++)                          /* the page out of reach under a modal */
        for (int x = 0; x < TANK_W; x++) fb[y * stride + x] = (uint16_t)((fb[y * stride + x] >> 1) & 0x7bef);
    const int X = SHP_MODAL_X, W = SHP_MODAL_W;
    if (g_shp_earn) {
        const int Y = SHP_EARN_MODAL_Y, H = SHP_EARN_MODAL_H;
        rect_fill(&c, X, Y, W, H, 0x04141a);
        rect_edge(&c, X, Y, W, H, MSP_TEAL); rect_edge(&c, X + 1, Y + 1, W - 2, H - 2, 0x1c2f36);
        draw_text(&c, X + (W - text_w("HOW TO EARN", 3)) / 2, Y + 14, 3, 0xffffff, "HOW TO EARN");
        const char *const *lines = progression_sd_earn_lines();
        for (int i = 0; lines[i]; i++) draw_text(&c, X + 14, Y + 50 + i * 24, 2, MSP_TEAL, lines[i]);
        draw_text(&c, X + (W - text_w("TAP TO CLOSE", 2)) / 2, Y + H - 24, 2, 0x3f6a72, "TAP TO CLOSE");
        return;
    }
    if (g_shp_modal == SHP_MODAL_FRY) { shop_fry_modal(&c, t, fb, stride); return; }
    const sd_item_t *it = &SD_ITEMS[g_shp_modal];
    const int Y = SHP_MODAL_Y, H = SHP_MODAL_H;
    bool owned = progression_item_owned(t, g_shp_modal), can = t->sd_balance >= it->price;
    rect_fill(&c, X, Y, W, H, 0x04141a);
    rect_edge(&c, X, Y, W, H, MSP_TEAL); rect_edge(&c, X + 1, Y + 1, W - 2, H - 2, 0x1c2f36);
    if (g_shp_modal >= 9 && g_shp_modal <= 12) draw_ornament(&c, g_shp_modal, X + (W - tank_ornament_w(g_shp_modal) * 2) / 2, Y + 8, 2, false, owned, 0x04141a);
    else if (g_shp_modal == 13) {
        draw_buoy_icon(&c, X + W / 2 - 36, Y + 8, 2, owned, 0x04141a);
    } else if (g_shp_modal >= 16) {
        draw_ornament_icon(&c, g_shp_modal, X + W / 2 - 36, Y + 8, 72, owned, 0x04141a);
    } else if (g_shp_modal == 14 || g_shp_modal == 15) {
        int count = g_shp_modal == 14 ? SAND_STYLE_COUNT : WATER_STYLE_COUNT - 1;
        int width = 128 / count;
        for (int s = 0; s < count; s++) rect_fill(&c, X + W / 2 - 64 + s * width, Y + 18, width - 2, 44, shop_art_color(g_shp_modal == 14 ? SAND_PALETTE[s][0] : WATER_PALETTE[s + 1][0], owned, 0x04141a));
    }
    else blit_icon_scaled(&c, X + (W - 64) / 2, Y + 14, shop_icon(g_shp_modal), 2, owned);
    draw_text(&c, X + (W - text_w(it->name, 3)) / 2, Y + 88, 3, 0xffffff, it->name);
    draw_text(&c, X + (W - text_w(it->words, 2)) / 2, Y + 118, 2, MSP_TEAL, it->words);
    draw_text(&c, X + (W - text_w(it->words2, 2)) / 2, Y + 138, 2, MSP_TEAL, it->words2);
    const int bx = X + (W - MSP_HOW_W) / 2, by = Y + H - 12 - MSP_HOW_H;
    if (owned && tank_decor_placeable(g_shp_modal)) {         /* a placeable piece: MOVE re-opens the placement page, SELL (twice) sells it back */
        char sell[24]; snprintf(sell, sizeof sell, "SELLS BACK FOR %d", progression_sell_value(g_shp_modal));
        draw_text(&c, X + (W - text_w("IN THE TANK", 2)) / 2, Y + 164, 2, MSP_TEAL, "IN THE TANK");
        draw_text(&c, X + (W - text_w(sell, 2)) / 2, Y + 184, 2, MSP_DIM, sell);
        button(&c, SHP_TWO_X0, by, MSP_HOW_W, MSP_HOW_H, 0x1c2f36, MSP_TEAL, "MOVE", 2);
        if (g_shp_sell_armed) { snprintf(sell, sizeof sell, "+%d OK?", progression_sell_value(g_shp_modal));
                                button(&c, SHP_TWO_X1, by, MSP_HOW_W, MSP_HOW_H, 0x1c2f36, 0xffffff, sell, 2); }
        else button(&c, SHP_TWO_X1, by, MSP_HOW_W, MSP_HOW_H, 0x1c2f36, MSP_DIM, "SELL", 2);
    } else if (owned && g_shp_modal >= 22 && g_shp_modal <= 29) {
        char sell[32]; snprintf(sell, sizeof sell, "SELLS BACK FOR %d", progression_sell_value(g_shp_modal));
        draw_text(&c, X + (W - text_w("FIXED SCENERY", 2)) / 2, Y + 164, 2, MSP_TEAL, "FIXED SCENERY");
        draw_text(&c, X + (W - text_w(sell, 2)) / 2, Y + 184, 2, MSP_DIM, sell);
        if (g_shp_sell_armed) snprintf(sell, sizeof sell, "+%d OK?", progression_sell_value(g_shp_modal));
        else snprintf(sell, sizeof sell, "SELL");
        button(&c, bx, by, MSP_HOW_W, MSP_HOW_H, 0x1c2f36, g_shp_sell_armed ? 0xffffff : MSP_TEAL, sell, 2);
    } else if (owned) draw_text(&c, X + (W - text_w("IN THE TANK", 2)) / 2, by + 8, 2, MSP_TEAL, "IN THE TANK");   /* the snail: a permanent resident */
    else {
        char line[32]; snprintf(line, sizeof line, "%d", it->price);
        int pw = 20 + text_w(line, 2);
        price_tag(&c, X + (W - pw) / 2, Y + 164, it->price, can ? 0xffffff : MSP_DIM);
        if (!can) { snprintf(line, sizeof line, "YOU HAVE %d", (int)t->sd_balance);
                    draw_text(&c, X + (W - text_w(line, 2)) / 2, Y + 184, 2, MSP_DIM, line); }
        if (can) button(&c, bx, by, MSP_HOW_W, MSP_HOW_H, 0x1c2f36, MSP_TEAL, "UNLOCK", 2);
        else button(&c, bx, by, MSP_HOW_W, MSP_HOW_H, 0x1c2f36, MSP_DIM, "UNLOCK", 2);
    }
}
int render_shop_tap(const tank_t *t, float x, float y) {
    x = render_page_x(x); y = render_page_y(y);
    if (g_shp_earn) { g_shp_earn = false; return SHOP_TAP_KEPT; }
    if (g_shp_modal == SHP_MODAL_FRY) {                       /* the fry: a box picks, BUY buys the pick, elsewhere closes */
        if (y >= SHP_FRY_BOX_Y - 8 && y < SHP_FRY_BOX_Y + SHP_FRY_BOX_H + 6)
            for (int k = 0; k < FRY_OFFER_N; k++)
                if (x >= SHP_FRY_BOX_X(k) - SHP_FRY_BOX_GAP / 2 && x < SHP_FRY_BOX_X(k) + SHP_FRY_BOX_W + SHP_FRY_BOX_GAP / 2) {
                    fry_offer_t o; progression_fry_offer(k, &o);
                    if (!o.sold) g_shp_fry_pick = k;
                    return SHOP_TAP_KEPT;
                }
        const int bx = SHP_MODAL_X + (SHP_MODAL_W - MSP_HOW_W) / 2, by = SHP_MODAL_Y + SHP_MODAL_H - 12 - MSP_HOW_H;
        bool on_btn = y >= by - MSP_HOW_SLOP_UP && y < by + MSP_HOW_H + MSP_HOW_SLOP_DN && x >= bx - MSP_HOW_SLOP_X && x < bx + MSP_HOW_W + MSP_HOW_SLOP_X;
        fry_offer_t pk; progression_fry_offer(g_shp_fry_pick, &pk);
        g_shp_modal = -1;
        if (on_btn && !pk.sold && progression_fry_room(t) && t->sd_balance >= SD_PRICE_FRY) return SHOP_TAP_FRY + g_shp_fry_pick;
        return SHOP_TAP_KEPT;
    }
    if (g_shp_modal >= 0) {
        int item = g_shp_modal; const sd_item_t *it = &SD_ITEMS[item];
        bool owned = progression_item_owned(t, item), can = t->sd_balance >= it->price;
        const int bx = SHP_MODAL_X + (SHP_MODAL_W - MSP_HOW_W) / 2, by = SHP_MODAL_Y + SHP_MODAL_H - 12 - MSP_HOW_H;
        bool row = y >= by - MSP_HOW_SLOP_UP && y < by + MSP_HOW_H + MSP_HOW_SLOP_DN;
        bool on_btn = row && x >= bx - MSP_HOW_SLOP_X && x < bx + MSP_HOW_W + MSP_HOW_SLOP_X;
        if (owned && item >= 22 && item <= 29) {
            if (on_btn && !g_shp_sell_armed) { g_shp_sell_armed = true; return SHOP_TAP_KEPT; }
            g_shp_modal = -1; g_shp_sell_armed = false;
            return on_btn ? SHOP_TAP_SELL + item : SHOP_TAP_KEPT;
        }
        if (owned && tank_decor_placeable(item)) {                /* two buttons: MOVE, and SELL armed then confirmed */
            bool on_move = row && x >= SHP_TWO_X0 - MSP_HOW_SLOP_X && x < SHP_TWO_X0 + MSP_HOW_W + SHP_TWO_GAP / 2;
            bool on_sell = row && x >= SHP_TWO_X1 - SHP_TWO_GAP / 2 && x < SHP_TWO_X1 + MSP_HOW_W + MSP_HOW_SLOP_X;
            if (on_sell && !g_shp_sell_armed) { g_shp_sell_armed = true; return SHOP_TAP_KEPT; }
            g_shp_sell_armed = false; g_shp_modal = -1;
            if (on_sell) return SHOP_TAP_SELL + item;
            if (on_move) return SHOP_TAP_MOVE + item;
            return SHOP_TAP_KEPT;
        }
        g_shp_modal = -1; g_shp_sell_armed = false;
        if (on_btn && !owned && can) return SHOP_TAP_BUY + item;
        return SHOP_TAP_KEPT;
    }
    if (x >= SHP_CLOSE_X - 6 && y >= MSP_CLOSE_Y - 4) return SHOP_TAP_CLOSE;
    if (x >= 190 && x < 316 && y >= MSP_CLOSE_Y - 4) return SHOP_TAP_TANKS;
    if (x < SHP_EARN_X + SHP_EARN_W + 8 && y >= MSP_CLOSE_Y - 4) { g_shp_earn = true; return SHOP_TAP_KEPT; }
    if (shp_pages(t) > 1 && y < SHP_ROW_Y0 - 10 && x >= SHP_ARROW_X0 - 8) {   /* the page arrows, in the header band */
        if (x < SHP_ARROW_X1 - 4) { if (g_shp_page > 0) g_shp_page--; }
        else if (g_shp_page < shp_pages(t) - 1) g_shp_page++;
        return SHOP_TAP_KEPT;
    }
    for (int r = g_shp_page * SHP_PER_PAGE; r < shp_rows(t) && r < (g_shp_page + 1) * SHP_PER_PAGE; r++) {
        int top = SHP_ROW_Y0 + (r - g_shp_page * SHP_PER_PAGE) * SHP_ROW_DY, i = shp_item(t, r);
        if (x >= 20 && y >= top - 8 && y < top + SHP_ROW_DY - 8) {
            if (i < 0) { int k = fry_first_left(); g_shp_fry_pick = k < 0 ? 0 : k; g_shp_modal = SHP_MODAL_FRY; }   /* the fry's modal, the first one left picked */
            else g_shp_modal = i;
            return SHOP_TAP_KEPT;
        }
    }
    return SHOP_TAP_NONE;
}
void render_shop_leave(void) { g_shp_modal = -1; g_shp_earn = false; g_shp_page = 0; g_shp_sell_armed = false; }

/* the toast: "+N" by a coin, top centre, for TOAST_S on the tank clock */
#define TOAST_S 2.5f
static int   g_toast_n;
static float g_toast_until = -1;
void render_sd_toast(const tank_t *t, uint16_t *fb, int stride) {
    int n = progression_sd_take_award();
    if (n > 0) {
        if (t->clock < g_toast_until) g_toast_n += n; else g_toast_n = n;
        g_toast_until = t->clock + TOAST_S;
    }
    if (t->clock >= g_toast_until || t->clock < g_toast_until - TOAST_S - 1) return;   /* (a reset sends the clock back) */
    ctx_t c = ctx_full(fb, stride, 1.0f);
    char txt[16]; snprintf(txt, sizeof txt, "+%d", g_toast_n);
    const int W = 30 + text_w(txt, 2), H = 22, X = (TANK_W - W) / 2, Y = 8;
    for (int y = Y; y < Y + H; y++)
        for (int x = X; x < X + W; x++) px_blend(&c, x, y, 0x04141a, 215);
    rect_edge(&c, X, Y, W, H, MSP_TEAL);
    blit_icon(&c, X + 5, Y + 3, &icon_shop_sand_dollar_16, 255);
    draw_text(&c, X + 25, Y + 4, 2, 0xffffff, txt);
}

/* ---- settings page ---- */
/* (the page's numbers, SET_*: render.h) */
static const char *const SET_BRIGHT[3] = { "30%", "60%", "100%" };
static const int         SET_BRIGHT_PCT[3] = { 30, 60, 100 };
static char s_backup_message[64] = "JSON BACKUP / RESTORE FROM SD";
static float s_backup_clock, s_restore_until;
void render_backup_status(const char *message, bool arm_restore) {
    snprintf(s_backup_message, sizeof s_backup_message, "%s", message);
    s_restore_until = arm_restore ? s_backup_clock + 10 : 0;
}
static const char *const SET_FEED[2]   = { "ON", "OFF" };          /* the default first */
static const char *const SAND_NAMES[SAND_STYLE_COUNT] = { "ORIGINAL", "GOLD", "PEARL", "VOLCANIC", "RAINBOW", "HOT PINK", "ELECTRIC CYAN", "NEON LIME" };
static const char *const WATER_NAMES[WATER_STYLE_COUNT] = { "ORIGINAL", "BLUE", "TEAL", "VIOLET", "AMBER", "RAINBOW", "HOT PINK", "NEON LIME" };

static int clock_offset_seconds(const tank_t *t) {
    return t->aquariums.clock_utc_quarters * 900 + (t->aquariums.clock_flags & CLOCK_DST ? 3600 : 0);
}
bool render_clock_format(const tank_t *t, int64_t unix_time, char text[9], int *hour, int *minute) {
    if (unix_time < INT64_C(1609459200) || unix_time > INT64_C(4102444800)) {
        snprintf(text, 9, "--:--"); *hour = *minute = 0; return false;
    }
    int64_t local = unix_time + clock_offset_seconds(t);
    int day = (int)(local % 86400);
    *hour = day / 3600; *minute = day / 60 % 60;
    int display = *hour;
    if (t->aquariums.clock_flags & CLOCK_12H) display = display % 12 ? display % 12 : 12;
    snprintf(text, 9, "%02d:%02d", display, *minute);
    return true;
}
static void clock_text_center(ctx_t *c, int x, int y, int w, int scale, uint32_t rgb, const char *text) {
    int width = ((int)strlen(text) * 6 - 1) * scale;
    draw_text(c, x + (w - width) / 2, y, scale, rgb, text);
}
static const int CLOCK_DELAYS[CLOCK_DELAY_COUNT] = {60, 15, 30, 120, 300, 600, -1};
bool render_clock_active(const tank_t *t) {
    if (t->ui_cover || t->aquariums.clock_theme <= CLOCK_OFF || t->aquariums.clock_theme >= CLOCK_THEME_COUNT) return false;
    int setting = t->aquariums.clock_delay & CLOCK_DELAY_MASK;
    int delay = CLOCK_DELAYS[setting < CLOCK_DELAY_COUNT ? setting : 0];
    return t->clock_requested || (delay >= 0 && t->clock_idle_s >= delay);
}
void render_clock_bounds(const tank_t *t, int *x, int *y, int *w, int *h) {
    int size = t->aquariums.clock_layout & 3, position = t->aquariums.clock_layout >> 2;
    if (size >= CLOCK_SIZE_COUNT) size = CLOCK_LARGE;
    static const int widths[] = {220, 172, 116}, heights[] = {112, 90, 68};
    static const int analog_w[] = {154, 126, 100}, analog_h[] = {166, 138, 112};
    bool analog = t->aquariums.clock_theme == CLOCK_ANALOG || t->aquariums.clock_theme == CLOCK_ORBIT;
    *w = analog ? analog_w[size] : widths[size]; *h = analog ? analog_h[size] : heights[size];
    *x = (TANK_W - *w) / 2; *y = (TANK_H - *h) / 2;
    if (position == CLOCK_TOP_LEFT || position == CLOCK_BOTTOM_LEFT) *x = 6;
    if (position == CLOCK_TOP_RIGHT || position == CLOCK_BOTTOM_RIGHT) *x = TANK_W - *w - 6;
    if (position == CLOCK_TOP_LEFT || position == CLOCK_TOP_RIGHT) *y = 6;
    if (position == CLOCK_BOTTOM_LEFT || position == CLOCK_BOTTOM_RIGHT) *y = TANK_H - *h - 6;
}
static void clock_hand(ctx_t *c, int cx, int cy, float angle, int length, int width, uint32_t rgb) {
    for (int r = 0; r <= length; r++)
        rect_fill(c, cx + (int)(cosf(angle) * r) - width / 2, cy + (int)(sinf(angle) * r) - width / 2, width, width, rgb);
}
static void clock_segment(ctx_t *c, int x, int y, int length, int thickness, bool horizontal, uint32_t rgb) {
    for (int row = 0; row < thickness; row++) {
        int inset = abs(2 * row - (thickness - 1)) / 2;
        if (horizontal) rect_fill(c, x + inset, y + row, length - 2 * inset, 1, rgb);
        else rect_fill(c, x + row, y + inset, 1, length - 2 * inset, rgb);
    }
}
static void clock_segments(ctx_t *c, int x, int y, int scale, uint32_t ink, const char *text) {
    static const uint8_t masks[] = {0x3f, 0x06, 0x5b, 0x4f, 0x66, 0x6d, 0x7d, 0x07, 0x7f, 0x6f};
    int s = scale, thick = s / 2 + 1, gap = s / 4;
    if (gap < 1) gap = 1;
    for (; *text; text++) {
        if (*text == ':') {
            for (int row = 0; row < thick; row++) {
                int inset = row == 0 || row == thick - 1 ? 1 : 0;
                rect_fill(c, x + s / 2 + inset, y + 2 * s + row, thick - 2 * inset, 1, ink);
                rect_fill(c, x + s / 2 + inset, y + 5 * s + row, thick - 2 * inset, 1, ink);
            }
            x += 3 * s; continue;
        }
        int mask = *text >= '0' && *text <= '9' ? masks[*text - '0'] : 0x40;
        static const int sx[] = {0, 3, 3, 0, 0, 0, 0}, sy[] = {0, 0, 3, 6, 3, 0, 3};
        for (int k = 0; k < 7; k++) {
            bool horizontal = k == 0 || k == 3 || k == 6;
            int inset = thick / 2 + gap;
            clock_segment(c, x + sx[k] * s + (horizontal ? inset : 0),
                          y + sy[k] * s + (horizontal ? 0 : inset),
                          3 * s - 2 * gap + 1, thick, horizontal,
                          mask & (1 << k) ? ink : 0x173139);
        }
        x += 5 * s;
    }
}
typedef struct { uint32_t ink, edge, hour, minute, second; } clock_colors_t;
static const char *const CLOCK_COLOR_NAMES[] = {"THEME", "WHITE", "AMBER", "MINT", "CYAN", "ROSE", "LAVENDER", "RED"};
static clock_colors_t clock_colors(const tank_t *t) {
    int theme = t->aquariums.clock_theme, color = t->aquariums.clock_flags >> CLOCK_COLOR_SHIFT;
    clock_colors_t p = {theme == CLOCK_NEON ? 0x73f5e5 : theme == CLOCK_FLIP ? 0xffe9b0 : 0xffffff,
                       theme == CLOCK_NEON ? 0xf698eb : 0x9fd8e2, 0xffdc73, 0x73f5e5, 0xff7289};
    static const uint32_t inks[] = {0, 0xffffff, 0xffdc73, 0x89ffb1, 0x73e5ff, 0xffa3cf, 0xc5adff, 0xff8173};
    if (color) p.ink = p.hour = inks[color];
    int secondary = t->aquariums.clock_delay >> CLOCK_SECONDARY_SHIFT;
    if (secondary) p.edge = p.minute = p.second = inks[secondary];
    return p;
}
static void clock_flip(ctx_t *c, int x, int y, int w, int size, uint32_t ink, const char *text) {
    int scale = size == CLOCK_LARGE ? 6 : size == CLOCK_MEDIUM ? 4 : 3;
    int margin = size == CLOCK_SMALL ? 6 : 12, gap = 3, colon = size == CLOCK_SMALL ? 6 : 12;
    int padding = size == CLOCK_SMALL ? 4 : 6;
    int tile_w = (w - 2 * margin - 3 * gap - colon) / 4, tile_h = 7 * scale + 2 * padding;
    int top = y + 8;
    for (int digit = 0; digit < 4; digit++) {
        int left = x + margin + digit * (tile_w + gap) + (digit >= 2 ? colon : 0);
        rect_fill(c, left, top, tile_w, tile_h / 2, 0x304450);
        rect_fill(c, left, top + tile_h / 2, tile_w, tile_h - tile_h / 2, 0x1a2a35);
        rect_edge(c, left, top, tile_w, tile_h, 0x6c8390);
        char label[2] = {text[digit + (digit >= 2 ? 1 : 0)], 0};
        clock_text_center(c, left, top + padding, tile_w, scale, ink, label);
        rect_fill(c, left + 1, top + tile_h / 2, tile_w - 2, 1, 0x061118);
        rect_fill(c, left, top + tile_h / 2 - 1, 2, 3, 0x8f9ca0);
        rect_fill(c, left + tile_w - 2, top + tile_h / 2 - 1, 2, 3, 0x8f9ca0);
    }
    int cx = x + margin + 2 * (tile_w + gap) + colon / 2 - 2;
    rect_fill(c, cx, top + tile_h / 3, 2, 2, ink);
    rect_fill(c, cx, top + tile_h * 2 / 3, 2, 2, ink);
}
static void clock_binary(ctx_t *c, int x, int y, int w, int size, const clock_colors_t *p, int hour, int minute, int second, bool valid, bool seconds) {
    int digits[] = {hour / 10, hour % 10, minute / 10, minute % 10, second / 10, second % 10};
    int cols = seconds ? 6 : 4, pitch = (w - 32) / cols, dot = size == CLOCK_LARGE ? 7 : size == CLOCK_MEDIUM ? 5 : 3;
    int top = y + (size == CLOCK_LARGE ? 24 : size == CLOCK_MEDIUM ? 20 : 15), row_pitch = dot + (size == CLOCK_SMALL ? 3 : 5);
    const uint32_t colors[] = {p->hour, p->minute, p->second};
    for (int col = 0; col < cols; col++) {
        int cx = x + 24 + col * pitch;
        if (!(col % 2)) {
            char label[2] = {"HMS"[col / 2], 0};
            clock_text_center(c, cx - pitch / 2, y + 4, 2 * pitch, 1, colors[col / 2], label);
        }
        for (int row = 0; row < 4; row++) {
            int bit = 1 << (3 - row);
            if (!col) {
                char weight[2] = {(char)('0' + bit), 0};
                draw_text(c, x + 5, top + row * row_pitch, 1, p->edge, weight);
            }
            rect_fill(c, cx - dot / 2, top + row * row_pitch, dot, dot,
                      valid && (digits[col] & bit) ? colors[col / 2] : 0x233a43);
        }
    }
}
static void clock_orbit(ctx_t *c, int x, int y, int w, int size, const clock_colors_t *p, const char *text, int hour, int minute, int second, bool valid, bool seconds) {
    int radius = w / 2 - 14, cx = x + w / 2, cy = y + radius + 3;
    int gap = size == CLOCK_LARGE ? 9 : size == CLOCK_MEDIUM ? 8 : 7;
    int progress[] = {((hour % 12) * 3600 + minute * 60 + second) / 720, minute, second};
    const uint32_t colors[] = {p->hour, p->minute, p->second};
    int tracks = seconds ? 3 : 2;
    for (int track = 0; track < tracks; track++) {
        for (int mark = 0; mark < 60; mark++) {
            float angle = mark * TAU / 60 - TAU / 4;
            int r = radius - track * gap;
            rect_fill(c, cx + (int)(cosf(angle) * r), cy + (int)(sinf(angle) * r), 2, 2,
                      valid && mark < progress[track] ? colors[track] : 0x233a43);
        }
        char label[2] = {"HMS"[track], 0};
        draw_text(c, cx - (tracks * 12 - 7) / 2 + track * 12, cy + 15, 1, colors[track], label);
    }
    int scale = size == CLOCK_LARGE ? 3 : size == CLOCK_MEDIUM ? 2 : 1;
    clock_text_center(c, x, cy - 7 * scale / 2, w, scale, p->ink, text);
}
void render_clock_overlay(const tank_t *t, uint16_t *fb, int stride, int64_t unix_time) {
    int theme = t->aquariums.clock_theme;
    if (theme <= CLOCK_OFF || theme >= CLOCK_THEME_COUNT) return;
    char text[9]; int hour, minute;
    bool valid = render_clock_format(t, unix_time, text, &hour, &minute);
    int x, y, w, h; render_clock_bounds(t, &x, &y, &w, &h);
    int size = t->aquariums.clock_layout & 3;
    if (size >= CLOCK_SIZE_COUNT) size = CLOCK_LARGE;
    ctx_t c = ctx_full(fb, stride, 1);
    clock_colors_t palette = clock_colors(t);
    uint32_t edge = palette.edge, ink = palette.ink;
    bool seconds = !(t->aquariums.clock_flags & CLOCK_HIDE_SECONDS);
    if (!(t->aquariums.clock_flags & CLOCK_HIDE_PANEL)) {
        for (int py = y; py < y + h; py++) for (int px = x; px < x + w; px++)
            px_blend(&c, px, py, 0x04141a, theme == CLOCK_COMPACT ? 155 : 210);
        rect_edge(&c, x, y, w, h, edge);
    }
    if (theme == CLOCK_FLIP) {
        clock_flip(&c, x, y, w, size, ink, text);
    } else if (theme == CLOCK_BINARY) {
        int display_hour = t->aquariums.clock_flags & CLOCK_12H ? (hour % 12 ? hour % 12 : 12) : hour;
        clock_binary(&c, x, y, w, size, &palette, display_hour, minute, valid ? (int)(unix_time % 60) : 0, valid, seconds);
    } else if (theme == CLOCK_ORBIT) {
        clock_orbit(&c, x, y, w, size, &palette, text, hour, minute, valid ? (int)(unix_time % 60) : 0, valid, seconds);
    } else if (theme == CLOCK_ANALOG && valid) {
        int cx = x + w / 2, radius = h / 2 - 22, cy = y + radius + 8;
        for (int mark = 0; mark < 60; mark++) {
            float a = mark * TAU / 60 - TAU / 4;
            int length = mark % 5 == 0 ? 5 : 2;
            for (int r = radius - length; r <= radius; r++)
                px_blend(&c, cx + (int)(cosf(a) * r), cy + (int)(sinf(a) * r), edge, 255);
        }
        for (int n = 1; n <= 12; n++) {
            char number[3]; snprintf(number, sizeof number, "%d", n);
            float a = n * TAU / 12 - TAU / 4;
            int nx = cx + (int)(cosf(a) * (radius - 12)), ny = cy + (int)(sinf(a) * (radius - 12)) - 3;
            clock_text_center(&c, nx - 7, ny, 14, 1, edge, number);
        }
        float ma = minute * TAU / 60 - TAU / 4;
        float ha = (hour % 12 + minute / 60.0f) * TAU / 12 - TAU / 4;
        clock_hand(&c, cx, cy, ma, radius - 8, 2, t->aquariums.clock_delay & CLOCK_SECONDARY_MASK ? palette.minute : ink);
        clock_hand(&c, cx, cy, ha, radius * 3 / 5, 3, palette.hour);
        if (seconds) clock_hand(&c, cx, cy, (unix_time % 60) * TAU / 60 - TAU / 4, radius - 4, 1, palette.second);
        rect_fill(&c, cx - 2, cy - 2, 5, 5, edge);
        char label[9];
        snprintf(label, sizeof label, "%.5s%s", text, t->aquariums.clock_flags & CLOCK_12H ? (hour < 12 ? " AM" : " PM") : "");
        clock_text_center(&c, x, y + h - 24, w, 1, ink, label);
    } else {
        int scale = size == CLOCK_LARGE ? 6 : size == CLOCK_MEDIUM ? 4 : 3;
        if (theme == CLOCK_ANALOG) scale = size == CLOCK_LARGE ? 4 : size == CLOCK_MEDIUM ? 3 : 2;
        if (theme == CLOCK_DIGITAL) {
            int segment_scale = size == CLOCK_LARGE ? 8 : size == CLOCK_MEDIUM ? 6 : 4;
            clock_segments(&c, x + (w - 22 * segment_scale) / 2, y + 10, segment_scale, ink, text);
        } else {
            if (theme == CLOCK_NEON) {
                clock_text_center(&c, x + 1, y + 12, w, scale, t->aquariums.clock_flags & CLOCK_COLOR_MASK ? (ink >> 1) & 0x7f7f7f : 0x854375, text);
                rect_fill(&c, x + 8, y + h - 4, w - 16, 1, edge);
            }
            clock_text_center(&c, x, y + 10, w, scale, ink, text);
        }
    }
    if (!valid) {
        clock_text_center(&c, x, y + h - 16, w, 1, edge, "SYNC NEEDED");
    } else if (!(t->aquariums.clock_flags & CLOCK_HIDE_DATE)) {
        time_t local = (time_t)(unix_time + clock_offset_seconds(t));
        struct tm *date = gmtime(&local);
        char calendar[20];
        if (date) strftime(calendar, sizeof calendar, "%a %d %b", date);
        else { snprintf(calendar, sizeof calendar, "DATE UNAVAILABLE"); fprintf(stderr, "Clock date conversion failed\n"); }
        clock_text_center(&c, x, y + h - 12, w, 1, edge, calendar);
    }
    if (theme != CLOCK_ANALOG && valid) {
        const char *caption = t->aquariums.clock_flags & CLOCK_12H ? (hour < 12 ? "AM" : "PM") : "24H";
        char detail[16];
        if (seconds) snprintf(detail, sizeof detail, "%s  :%02d", caption, (int)(unix_time % 60));
        else snprintf(detail, sizeof detail, "%s", caption);
        clock_text_center(&c, x, y + h - (theme == CLOCK_ORBIT ? 24 : 28), w, 1, edge, detail);
    }
}
static bool g_clock_settings, g_clock_style;
static int g_clock_step;
static const int CLOCK_STEPS[] = {4, 2, 1};
static const char *const CLOCK_STEP_NAMES[] = {"1H", "30M", "15M"};
enum { CLOCK_HIT_PAGE = 100, CLOCK_HIT_THEME, CLOCK_HIT_FORMAT, CLOCK_HIT_POSITION, CLOCK_HIT_PREVIOUS, CLOCK_HIT_NEXT, CLOCK_HIT_SIZE, CLOCK_HIT_DELAY, CLOCK_HIT_DST, CLOCK_HIT_STEP, CLOCK_HIT_STYLE, CLOCK_HIT_COLOR, CLOCK_HIT_SECONDS, CLOCK_HIT_PANEL, CLOCK_HIT_DATE, CLOCK_HIT_SECONDARY };
static const char *const CLOCK_NAMES[CLOCK_THEME_COUNT] = {"OFF", "MINIMAL", "DIGITAL", "ANALOG", "NEON", "FLIP", "BINARY", "ORBIT"};
static const char *const CLOCK_SIZES[] = {"LARGE", "MEDIUM", "SMALL"};
static const char *const CLOCK_POSITIONS[] = {"CENTER", "TOP LEFT", "TOP RIGHT", "BOTTOM LEFT", "BOTTOM RIGHT"};
static const char *const CLOCK_DELAY_NAMES[] = {"1 MIN", "15 SEC", "30 SEC", "2 MIN", "5 MIN", "10 MIN", "MANUAL ONLY"};
static void clock_settings_draw(ctx_t *c, const tank_t *t) {
    if (g_clock_style) {
        draw_text(c, 32, 14, 3, 0xffffff, "CLOCK STYLE");
        button(c, 326, 8, 112, 36, 0x1c2f36, MSP_TEAL, "BACK", 2);
        char label[40];
        snprintf(label, sizeof label, "PRIMARY: %s", CLOCK_COLOR_NAMES[t->aquariums.clock_flags >> CLOCK_COLOR_SHIFT]);
        button(c, 32, 56, 396, 38, 0x1c2f36, MSP_TEAL, label, 2);
        snprintf(label, sizeof label, "SECONDARY: %s", CLOCK_COLOR_NAMES[t->aquariums.clock_delay >> CLOCK_SECONDARY_SHIFT]);
        button(c, 32, 100, 396, 38, 0x1c2f36, MSP_TEAL, label, 2);
        button(c, 32, 144, 396, 38, 0x1c2f36, MSP_TEAL, t->aquariums.clock_flags & CLOCK_HIDE_SECONDS ? "SECONDS: HIDDEN" : "SECONDS: SHOWN", 2);
        button(c, 32, 188, 396, 38, 0x1c2f36, MSP_TEAL, t->aquariums.clock_flags & CLOCK_HIDE_PANEL ? "PANEL + BORDER: HIDDEN" : "PANEL + BORDER: SHOWN", 2);
        button(c, 32, 232, 396, 38, 0x1c2f36, MSP_TEAL, t->aquariums.clock_flags & CLOCK_HIDE_DATE ? "DATE: HIDDEN" : "DATE: SHOWN", 2);
        button(c, 32, 282, 396, 42, 0x1c2f36, MSP_TEAL, "SHOW CLOCK", 2);
        draw_text_wrap(c, 32, 344, 396, MSP_TEAL, s_backup_message);
        return;
    }
    draw_text(c, 32, 14, 3, 0xffffff, "CLOCK");
    button(c, 140, 8, 174, 36, 0x1c2f36, MSP_TEAL, "STYLE >", 2);
    button(c, 326, 8, 112, 36, 0x1c2f36, MSP_TEAL, "BACK", 2);
    char label[40];
    snprintf(label, sizeof label, "THEME: %s", CLOCK_NAMES[t->aquariums.clock_theme < CLOCK_THEME_COUNT ? t->aquariums.clock_theme : 0]);
    button(c, 32, 52, 396, 34, 0x1c2f36, MSP_TEAL, label, 2);
    int size = t->aquariums.clock_layout & 3, position = t->aquariums.clock_layout >> 2;
    snprintf(label, sizeof label, "SIZE: %s", CLOCK_SIZES[size < CLOCK_SIZE_COUNT ? size : 0]);
    button(c, 32, 92, 396, 34, 0x1c2f36, MSP_TEAL, label, 2);
    snprintf(label, sizeof label, "POSITION: %s", CLOCK_POSITIONS[position < CLOCK_POSITION_COUNT ? position : 0]);
    button(c, 32, 132, 396, 34, 0x1c2f36, MSP_TEAL, label, 2);
    button(c, 32, 172, 190, 34, 0x1c2f36, MSP_TEAL, t->aquariums.clock_flags & CLOCK_12H ? "12 HOUR" : "24 HOUR", 2);
    button(c, 238, 172, 190, 34, 0x1c2f36, MSP_TEAL, t->aquariums.clock_flags & CLOCK_DST ? "DST: +1 HOUR" : "DST: OFF", 2);
    int delay = t->aquariums.clock_delay & CLOCK_DELAY_MASK;
    snprintf(label, sizeof label, "ACTIVATE: %s", CLOCK_DELAY_NAMES[delay < CLOCK_DELAY_COUNT ? delay : 0]);
    button(c, 32, 212, 396, 34, 0x1c2f36, MSP_TEAL, label, 2);
    int offset = t->aquariums.clock_utc_quarters, absolute = abs(offset);
    snprintf(label, sizeof label, "UTC %c%02d:%02d", offset < 0 ? '-' : '+', absolute / 4, absolute % 4 * 15);
    draw_text(c, (PAGE_W - text_w(label, 2)) / 2, 252, 2, 0xffffff, label);
    snprintf(label, sizeof label, "STEP: %s", CLOCK_STEP_NAMES[g_clock_step]);
    button(c, 116, 270, 228, 24, 0x1c2f36, MSP_TEAL, label, 2);
    snprintf(label, sizeof label, "-%s", CLOCK_STEP_NAMES[g_clock_step]);
    button(c, 32, 252, 76, 36, 0x1c2f36, MSP_TEAL, label, 2);
    snprintf(label, sizeof label, "+%s", CLOCK_STEP_NAMES[g_clock_step]);
    button(c, 352, 252, 76, 36, 0x1c2f36, MSP_TEAL, label, 2);
    button(c, 32, 300, 190, 36, 0x1c2f36, MSP_TEAL, "SHOW CLOCK", 2);
    button(c, 238, 300, 190, 36, 0x1c2f36, MSP_TEAL, "WI-FI / SYNC", 2);
    draw_text_wrap(c, 32, 344, 396, MSP_TEAL, s_backup_message);
}
static void set_row(ctx_t *c, int row_y, const char *label, const char *const names[], int n, int chosen) {
    draw_text(c, SET_LABEL_X, row_y, 2, MSP_TEAL, label);
    for (int i = 0; i < n; i++) {
        int x = SET_SEG_X + i * SET_SEG_DX, y = SET_SEG_Y(row_y);
        if (i == chosen) {                       /* lit: teal, ink lettering */
            button(c, x, y, SET_SEG_W, SET_SEG_H, MSP_TEAL, MSP_TEAL, names[i], 2);
            draw_text(c, x + (SET_SEG_W - text_w(names[i], 2)) / 2, y + (SET_SEG_H - 14) / 2, 2, MSP_INK, names[i]);
        } else button(c, x, y, SET_SEG_W, SET_SEG_H, 0x1c2f36, MSP_DIM, names[i], 2);
    }
}
void render_settings(const tank_t *t, uint16_t *fb, int stride, int bright_pct) {
    ctx_t c = ctx_page(fb, stride);
    rect_fill(&c, -PAGE_X, -PAGE_Y, PAGE_FRAME_W, PAGE_FRAME_H, MSP_INK);
    if (g_clock_settings) { clock_settings_draw(&c, t); return; }
    button(&c, 144, SET_CLOSE_Y, 156, SET_CLOSE_H, 0x1c2f36, MSP_TEAL, "CLOCK", 2);
    button(&c, SET_CLOSE_X, SET_CLOSE_Y, SET_CLOSE_W, SET_CLOSE_H, 0x1c2f36, MSP_TEAL, "CLOSE", 2);
    button(&c, SET_WIPE_X, SET_WIPE_Y, SET_WIPE_W, SET_WIPE_H, 0x7a2028, 0xf25b65, "WIPE", 2);
    int bi = bright_pct <= 30 ? 0 : bright_pct <= 60 ? 1 : 2;
    set_row(&c, SET_ROW1_Y, "BRIGHTNESS", SET_BRIGHT, 3, bi);
    s_backup_clock = t->clock;
    if (s_restore_until > 0 && t->clock >= s_restore_until) render_backup_status("RESTORE CONFIRMATION EXPIRED", false);
    button(&c, SET_LABEL_X, SET_SEG_Y(SET_ROW2_Y), 170, SET_SEG_H, 0x1c2f36, MSP_TEAL, "BACKUP TO SD", 2);
    button(&c, 218, SET_SEG_Y(SET_ROW2_Y), 198, SET_SEG_H, 0x1c2f36, MSP_TEAL, s_restore_until > 0 ? "CONFIRM RESTORE" : "RESTORE FROM SD", 2);
    draw_text_wrap(&c, SET_LABEL_X, SET_NOTE_Y, SET_COLOR_W, MSP_TEAL, s_backup_message);
    set_row(&c, SET_ROW3_Y, "AUTO FEED", SET_FEED, 2, t->autofeed_off ? 1 : 0);
    char sand[32], water[32];
    snprintf(sand, sizeof sand, "SAND: %s", t->sd_unlocks & SD_ITEM_SAND ? SAND_NAMES[t->sand_style % SAND_STYLE_COUNT] : "SHOP");
    snprintf(water, sizeof water, "WATER: %s", t->sd_unlocks & SD_ITEM_LIGHTS ? WATER_NAMES[t->water_style % WATER_STYLE_COUNT] : "SHOP");
    button(&c, SET_LABEL_X, SET_SEG_Y(SET_ROW4_Y), SET_COLOR_W, SET_SEG_H, 0x1c2f36, MSP_TEAL, sand, 2);
    button(&c, SET_LABEL_X, SET_SEG_Y(SET_ROW5_Y), SET_COLOR_W, SET_SEG_H, 0x1c2f36, MSP_TEAL, water, 2);
    char ver[64]; snprintf(ver, sizeof ver, "V%s %s - BUILD IN UPDATES", PT_RELEASE, PT_RELEASE_STAGE);
    draw_text(&c, SET_LABEL_X, PAGE_H - 18, 2, MSP_TEAL, ver);
    button(&c, SET_UPD_X, SET_FOOT_Y, SET_UPD_W, MSP_CLOSE_H, 0x1c2f36, MSP_TEAL, "UPDATES", 2);
    button(&c, SET_TOUCH_X, SET_FOOT_Y, SET_TOUCH_W, MSP_CLOSE_H, 0x1c2f36, MSP_TEAL, "TOUCH CALIBRATION", 2);
}
static int g_aquarium_page, g_aquarium_pick = -1;
static char g_aquarium_message[80];
static char g_aquarium_names[AQUARIUM_CAP][16], g_aquarium_draft[16];
static bool g_aquarium_naming;
static int g_aquarium_letter;
void render_aquariums_open(const tank_t *t) {
    g_aquarium_page = 0; g_aquarium_pick = -1; g_aquarium_message[0] = 0;
    g_aquarium_naming = false; g_aquarium_letter = 0;
    for (int i = 0; i < t->aquariums.count; i++) {
        if (!progression_aquarium_label(t, i, g_aquarium_names[i])) {
            snprintf(g_aquarium_names[i], sizeof g_aquarium_names[i], "TANK %d ?", i + 1);
            snprintf(g_aquarium_message, sizeof g_aquarium_message, "SD TANK NAMES UNAVAILABLE");
        }
    }
}
void render_aquariums(const tank_t *t, uint16_t *fb, int stride) {
    ctx_t c = ctx_page(fb, stride); char line[64];
    rect_fill(&c, 0, 0, PAGE_W, PAGE_H, MSP_INK);
    draw_text(&c, 24, 12, 2, MSP_TEAL, "AQUARIUMS");
    button(&c, SET_CLOSE_X, SET_CLOSE_Y, SET_CLOSE_W, SET_CLOSE_H, 0x1c2f36, MSP_TEAL,
           g_aquarium_pick < 0 && !g_aquarium_naming ? "CLOSE" : "BACK", 2);
    snprintf(line, sizeof line, "ACTIVE: %s", g_aquarium_names[t->aquariums.active]);
    draw_text(&c, 24, 48, 2, 0xffffff, line);
    if (g_aquarium_naming) {
        draw_text(&c, 24, 82, 2, MSP_TEAL, "SELECT A LETTER - USE < / >");
        for (int i = 0; i < 15; i++) {
            char letter[2] = {g_aquarium_draft[i] == ' ' ? '_' : g_aquarium_draft[i], 0};
            button(&c, 24 + (i % 8) * 50, 116 + (i / 8) * 58, 44, 48,
                   i == g_aquarium_letter ? 0x1c2f36 : 0x10282e,
                   i == g_aquarium_letter ? 0xffffff : MSP_TEAL, letter, 3);
        }
        button(&c, 24, 236, 72, 38, 0x1c2f36, MSP_TEAL, "<", 3);
        button(&c, 112, 236, 72, 38, 0x1c2f36, MSP_TEAL, ">", 3);
        button(&c, 300, 236, 124, 38, 0x1c2f36, MSP_TEAL, "SAVE", 2);
    } else if (g_aquarium_pick >= 0) {
        bool buy = g_aquarium_pick == t->aquariums.count;
        draw_text(&c, 24, 96, 3, 0xffffff, buy ? "BUY NEW TANK?" : "SWITCH TANK?");
        snprintf(line, sizeof line, "%s", buy ? "500 SAND DOLLARS" : g_aquarium_names[g_aquarium_pick]);
        draw_text(&c, 24, 138, 2, MSP_TEAL, line);
        draw_text(&c, 24, 170, 2, MSP_TEAL, buy ? "TWO NEW FRY." : "CURRENT TANK SAVES TO SD.");
        draw_text(&c, 24, 194, 2, MSP_TEAL, buy ? "SAND DOLLARS ARE GLOBAL." : "OTHER TANKS STAY PAUSED.");
        if (buy) draw_text(&c, 24, 218, 2, MSP_TEAL, "OWN ITEMS, JOURNAL AND BANK.");
        button(&c, 40, 236, 168, 38, 0x1c2f36, MSP_TEAL, "CANCEL", 2);
        button(&c, 240, 236, 168, 38, 0x1c2f36, MSP_TEAL, "CONFIRM", 2);
    } else {
        int rows = t->aquariums.count < AQUARIUM_CAP ? t->aquariums.count + 1 : AQUARIUM_CAP;
        int pages = (rows + 3) / 4;
        snprintf(line, sizeof line, "OWNED %u/%d  PAGE %d/%d", t->aquariums.count, AQUARIUM_CAP, g_aquarium_page + 1, pages);
        draw_text(&c, 24, 78, 2, MSP_TEAL, line);
        button(&c, 328, 68, 44, 28, 0x1c2f36, MSP_TEAL, "<", 2);
        button(&c, 380, 68, 44, 28, 0x1c2f36, MSP_TEAL, ">", 2);
        for (int row = 0; row < 4; row++) {
            int slot = g_aquarium_page * 4 + row, y = 108 + row * 43;
            if (slot >= rows) break;
            snprintf(line, sizeof line, "%s", slot == t->aquariums.count ? "NEW TANK" : g_aquarium_names[slot]);
            draw_text(&c, 24, y + 8, 2, 0xffffff, line);
            const char *label = slot == t->aquariums.count ? "BUY" : slot == t->aquariums.active ? "ACTIVE" : "OPEN";
            button(&c, 300, y, 124, 34, 0x1c2f36, MSP_TEAL, label, 2);
        }
    }
    draw_text_wrap(&c, 24, 286, 400, 0xffffff, g_aquarium_message);
    if (g_aquarium_pick < 0 && !g_aquarium_naming) {
        button(&c, 24, 328, 160, 22, 0x1c2f36, MSP_TEAL, "NAME TANK", 2);
        draw_text(&c, 208, 332, 2, MSP_TEAL, "ACTIVE TANK ONLY");
    }
    draw_text(&c, 24, 352, 2, MSP_TEAL, "500 SAND DOLLARS / NEW TANK");
}
int render_aquariums_tap(tank_t *t, float x, float y) {
    x = render_page_x(x); y = render_page_y(y);
    if (x >= SET_CLOSE_X && y < SET_CLOSE_Y + SET_CLOSE_H) {
        if (g_aquarium_naming) { g_aquarium_naming = false; g_aquarium_message[0] = 0; return 0; }
        if (g_aquarium_pick < 0) return 1;
        g_aquarium_pick = -1; g_aquarium_message[0] = 0; return 0;
    }
    if (g_aquarium_naming) {
        if (x >= 24 && x < 424 && y >= 116 && y < 222) {
            int row = y >= 174 ? 1 : 0, column = (int)((x - 24) / 50);
            int letter = row * 8 + column;
            if (letter < 15 && x < 24 + column * 50 + 44 && y < 116 + row * 58 + 48)
                g_aquarium_letter = letter;
        } else if (y >= 236 && y < 274) {
            if (x >= 300 && x < 424) {
                char name[16]; memcpy(name, g_aquarium_draft, sizeof name);
                for (int i = 14; i >= 0 && name[i] == ' '; i--) name[i] = 0;
                const char *error = progression_aquarium_rename(t, name);
                if (!error) { render_aquariums_open(t); snprintf(g_aquarium_message, sizeof g_aquarium_message, "TANK NAME SAVED"); }
                else snprintf(g_aquarium_message, sizeof g_aquarium_message, "%s", error);
            } else if ((x >= 24 && x < 96) || (x >= 112 && x < 184)) {
                static const char letters[] = " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-";
                const char *current = strchr(letters, g_aquarium_draft[g_aquarium_letter]);
                int n = current ? (int)(current - letters) : 0, count = (int)sizeof letters - 1;
                n = (n + (x < 100 ? count - 1 : 1)) % count;
                g_aquarium_draft[g_aquarium_letter] = letters[n];
            }
        }
        return 0;
    }
    if (g_aquarium_pick >= 0) {
        if (y >= 236 && y < 274) {
            if (x >= 40 && x < 208) { g_aquarium_pick = -1; g_aquarium_message[0] = 0; }
            else if (x >= 240 && x < 408) {
                const char *error = progression_aquarium_move(t, g_aquarium_pick);
                if (!error) { render_aquariums_open(t); return 2; }
                snprintf(g_aquarium_message, sizeof g_aquarium_message, "%s", error);
            }
        }
        return 0;
    }
    if (x >= 24 && x < 184 && y >= 328 && y < 350) {
        memset(g_aquarium_draft, ' ', 15); g_aquarium_draft[15] = 0;
        size_t n = strlen(g_aquarium_names[t->aquariums.active]);
        memcpy(g_aquarium_draft, g_aquarium_names[t->aquariums.active], n);
        g_aquarium_naming = true; g_aquarium_letter = 0; g_aquarium_message[0] = 0;
        return 0;
    }
    int rows = t->aquariums.count < AQUARIUM_CAP ? t->aquariums.count + 1 : AQUARIUM_CAP;
    if (y >= 68 && y < 96 && x >= 328 && x < 424) {
        if (x < 376 && g_aquarium_page > 0) g_aquarium_page--;
        else if (x >= 380 && (g_aquarium_page + 1) * 4 < rows) g_aquarium_page++;
    } else if (x >= 24 && x < 424 && y >= 108 && y < 280) {
        int row = (int)((y - 108) / 43), slot = g_aquarium_page * 4 + row;
        if (slot < rows && y < 108 + row * 43 + 34) {
            if (slot == t->aquariums.active) snprintf(g_aquarium_message, sizeof g_aquarium_message, "THIS TANK IS ALREADY ACTIVE");
            else { g_aquarium_pick = slot; g_aquarium_message[0] = 0; }
        }
    }
    return 0;
}
static int g_bank_tab, g_bank_page, g_bank_pick = -1, g_bank_pending = -2;
static char g_bank_message[64];
void render_bank_open(void) {
    g_bank_tab = g_bank_page = 0; g_bank_pick = -1; g_bank_pending = -2; g_bank_message[0] = 0;
}
static void bank_portrait(const bank_fish_t *b, fish_t *f) {
    memset(f, 0, sizeof *f); memcpy(f->name, b->name, sizeof f->name);
    f->stage = (stage_t)b->fish.stage; f->color = b->body; f->fin = tank_fin_for(b->body); f->accent = b->accent;
    f->variety = bank_variety(b); f->pattern = bank_pattern(b);
}
void render_bank(const tank_t *t, uint16_t *fb, int stride) {
    ctx_t c = ctx_page(fb, stride); char line[64];
    rect_fill(&c, 0, 0, PAGE_W, PAGE_H, MSP_INK);
    draw_text(&c, 24, 12, 2, MSP_TEAL, "FISH BANK");
    button(&c, SET_CLOSE_X, SET_CLOSE_Y, SET_CLOSE_W, SET_CLOSE_H, 0x1c2f36, MSP_TEAL,
           g_bank_pick >= 0 || g_bank_pending != -2 ? "BACK" : "CLOSE", 2);
    snprintf(line, sizeof line, "STORED %u/%d   TANK %d/%d", t->bank_count, FISH_BANK_CAP, t->n_fish, POP_CAP);
    draw_text(&c, 24, 44, 2, 0xffffff, line);
    bool detail = g_bank_pick >= 0 && g_bank_pick < t->bank_count && g_bank_tab == 0;
    if (g_bank_pending != -2) {
        int active = g_bank_pending;
        draw_text(&c, 40, 100, 3, 0xffffff, g_bank_pick < 0 ? "STORE FISH?" : active < 0 ? "RETURN FISH?" : "SWAP FISH?");
        if (g_bank_pick >= 0 && g_bank_pick < t->bank_count) {
            snprintf(line, sizeof line, "BANK: %s", t->bank[g_bank_pick].name); draw_text(&c, 40, 144, 2, MSP_TEAL, line);
        }
        if (active >= 0 && active < t->n_fish) {
            snprintf(line, sizeof line, "TANK: %s", t->fish[active].name); draw_text(&c, 40, 168, 2, MSP_TEAL, line);
        }
        button(&c, 64, 220, 144, 40, 0x1c2f36, MSP_TEAL, "CANCEL", 2);
        button(&c, 240, 220, 144, 40, 0x1c2f36, MSP_TEAL, "CONFIRM", 2);
    } else if (detail) {
        const bank_fish_t *b = &t->bank[g_bank_pick]; fish_t f; bank_portrait(b, &f);
        draw_text(&c, 24, 76, 3, 0xffffff, b->name);
        render_fish_portrait(fb, stride, 310, 110, 1.1f, &f, t->clock);
        draw_text(&c, 24, 112, 2, MSP_TEAL, FISH_VARIETY_NAMES[f.variety]);
        snprintf(line, sizeof line, "%s / %s", STAGE_WORDS[f.stage], FISH_PATTERN_NAMES[f.pattern]);
        draw_text(&c, 24, 144, 2, MSP_TEAL, line);
        snprintf(line, sizeof line, "AGE %.1f H  TRUST %.1f", (double)b->fish.age_s / 3600, (double)b->fish.trust);
        draw_text(&c, 24, 172, 2, 0xffffff, line);
        draw_text(&c, 24, 196, 2, MSP_TEAL, "PAUSED - NO CARE NEEDED");
        button(&c, 24, 224, 192, 42, 0x1c2f36, MSP_TEAL, "RETURN", 2);
        button(&c, 232, 224, 192, 42, 0x1c2f36, MSP_TEAL, "SWAP", 2);
    } else {
        int count = g_bank_tab ? t->n_fish : t->bank_count;
        int pages = (count + BANK_PER_PAGE - 1) / BANK_PER_PAGE; if (!pages) pages = 1;
        if (g_bank_page >= pages) g_bank_page = pages - 1;
        snprintf(line, sizeof line, "%s %d/%d", g_bank_pick >= 0 ? "SWAP WITH - PAGE" : "PAGE", g_bank_page + 1, pages);
        draw_text(&c, 24, 76, 2, MSP_TEAL, line);
        button(&c, 328, 65, 44, 26, 0x1c2f36, MSP_TEAL, "<", 2);
        button(&c, 380, 65, 44, 26, 0x1c2f36, MSP_TEAL, ">", 2);
        for (int row = 0; row < BANK_PER_PAGE; row++) {
            int i = g_bank_page * BANK_PER_PAGE + row, y = BANK_ROW_Y + row * BANK_ROW_DY;
            if (i >= count) break;
            fish_t portrait; const fish_t *f;
            if (g_bank_tab) f = &t->fish[i]; else { bank_portrait(&t->bank[i], &portrait); f = &portrait; }
            rect_fill(&c, 24, y, 400, 42, 0x10282e);
            render_fish_portrait(fb, stride, 48, y + 21, 0.55f, f, t->clock);
            snprintf(line, sizeof line, "%s / %s", f->name, STAGE_WORDS[f->stage]);
            draw_text(&c, 80, y + 4, 2, 0xffffff, line);
            draw_text(&c, 80, y + 23, 2, MSP_TEAL, FISH_VARIETY_NAMES[f->variety]);
            button(&c, 314, y + 3, 102, 36, 0x1c2f36, MSP_TEAL,
                   !g_bank_tab ? "CHOOSE" : g_bank_pick >= 0 ? "SWAP" : "STORE", 2);
        }
        if (!count) {
            draw_text(&c, 40, 142, 2, 0xffffff, "YOUR BANK IS EMPTY");
            draw_text(&c, 40, 172, 2, MSP_TEAL, "OPEN IN TANK TO STORE A FISH");
        }
    }
    draw_text_wrap(&c, 24, 280, 400, 0xffffff, g_bank_message);
    if (g_bank_pending == -2 && !detail) {
        button(&c, 24, 314, 192, 32, g_bank_tab == 0 ? 0x1c2f36 : 0x10282e, g_bank_tab == 0 ? 0xffffff : MSP_TEAL, "STORED", 2);
        button(&c, 232, 314, 192, 32, g_bank_tab == 1 ? 0x1c2f36 : 0x10282e, g_bank_tab == 1 ? 0xffffff : MSP_TEAL, "IN TANK", 2);
    }
    draw_text(&c, 24, 352, 2, MSP_TEAL, "FREE STORAGE - KEEP TWO IN TANK");
}
int render_bank_tap(tank_t *t, float x, float y) {
    x = render_page_x(x); y = render_page_y(y);
    if (x >= SET_CLOSE_X && x < SET_CLOSE_X + SET_CLOSE_W && y >= SET_CLOSE_Y && y < SET_CLOSE_Y + SET_CLOSE_H) {
        if (g_bank_pending != -2) g_bank_pending = -2;
        else if (g_bank_pick >= 0) { g_bank_pick = -1; g_bank_tab = 0; }
        else return 1;
        g_bank_message[0] = 0; return 0;
    }
    if (g_bank_pending != -2) {
        if (y >= 220 && y < 260) {
            if (x >= 64 && x < 208) { g_bank_pending = -2; g_bank_message[0] = 0; }
            else if (x >= 240 && x < 384) {
                const char *error = progression_bank_move(t, g_bank_pick, g_bank_pending);
                snprintf(g_bank_message, sizeof g_bank_message, "%s", error ? error : "SAVED - FISH MOVED SAFELY");
                if (!error) { g_bank_pending = -2; g_bank_pick = -1; g_bank_tab = g_bank_page = 0; }
            }
        }
        return 0;
    }
    bool detail = g_bank_pick >= 0 && g_bank_pick < t->bank_count && g_bank_tab == 0;
    if (detail) {
        if (y >= 224 && y < 266) {
            if (x >= 24 && x < 216) {
                if (t->n_fish >= POP_CAP) snprintf(g_bank_message, sizeof g_bank_message, "TANK FULL - USE SWAP");
                else { g_bank_pending = -1; g_bank_message[0] = 0; }
            } else if (x >= 232 && x < 424) { g_bank_tab = 1; g_bank_page = 0; g_bank_message[0] = 0; }
        }
    } else if (y >= 314 && y < 346 && x >= 24 && x < 424) {
        g_bank_tab = x < 224 ? 0 : 1; g_bank_page = 0; g_bank_pick = -1; g_bank_message[0] = 0;
    } else if (y >= 65 && y < 91 && x >= 328 && x < 424) {
        int count = g_bank_tab ? t->n_fish : t->bank_count;
        if (x < 376 && g_bank_page > 0) g_bank_page--;
        else if (x >= 380 && (g_bank_page + 1) * BANK_PER_PAGE < count) g_bank_page++;
    } else if (x >= 24 && x < 424 && y >= BANK_ROW_Y && y < BANK_ROW_Y + BANK_PER_PAGE * BANK_ROW_DY) {
        int i = g_bank_page * BANK_PER_PAGE + (int)((y - BANK_ROW_Y) / BANK_ROW_DY);
        if (!g_bank_tab && i < t->bank_count) { g_bank_pick = i; g_bank_message[0] = 0; }
        else if (g_bank_tab && i < t->n_fish) {
            if (g_bank_pick < 0 && t->n_fish <= FISH_KEEP_MIN)
                snprintf(g_bank_message, sizeof g_bank_message, "KEEP TWO IN TANK - USE SWAP");
            else if (g_bank_pick < 0 && t->bank_count >= FISH_BANK_CAP)
                snprintf(g_bank_message, sizeof g_bank_message, "BANK FULL - USE SWAP");
            else { g_bank_pending = i; g_bank_message[0] = 0; }
        }
    }
    return 0;
}
static int g_journal_tab;
static char g_journal_message[48];
void render_journal_open(void) { g_journal_tab = 0; g_journal_message[0] = 0; }
void render_journal(const tank_t *t, uint16_t *fb, int stride) {
    ctx_t c = ctx_page(fb, stride);
    rect_fill(&c, 0, 0, PAGE_W, PAGE_H, MSP_INK);
    draw_text(&c, 24, 10, 2, MSP_TEAL, "AQUARIUM JOURNAL");
    button(&c, SET_CLOSE_X, SET_CLOSE_Y, SET_CLOSE_W, SET_CLOSE_H, 0x1c2f36, MSP_TEAL, "CLOSE", 2);
    char line[64]; int level = journal_level(t);
    if (level == JOURNAL_LEVEL_MAX)
        snprintf(line, sizeof line, "KEEPER %d/15  MAX - %lu XP", level, (unsigned long)t->journal.xp);
    else snprintf(line, sizeof line, "KEEPER %d/15  %lu/%lu XP", level,
                  (unsigned long)t->journal.xp, (unsigned long)journal_level_xp(level + 1));
    draw_text(&c, 24, 42, 2, 0xffffff, line);
    uint32_t floor = journal_level_xp(level), next = level == 15 ? t->journal.xp : journal_level_xp(level + 1);
    float fraction = level == 15 ? 1 : (float)(t->journal.xp - floor) / (next - floor);
    rect_fill(&c, 24, 64, 400, 6, MSP_DIM);
    rect_fill(&c, 24, 64, (int)(400 * fraction), 6, MSP_TEAL);
    if (g_journal_tab == 0) {
        snprintf(line, sizeof line, "SAND DOLLARS %ld", (long)t->sd_balance);
        draw_text(&c, 24, 76, 2, MSP_TEAL, line);
        for (int slot = 0; slot < 3; slot++) {
            int goal = journal_active(t, slot), y = JOURNAL_GOAL_Y + slot * JOURNAL_GOAL_DY;
            if (goal < 0) continue;
            const journal_goal_t *g = &JOURNAL_GOALS[goal]; unsigned progress = journal_progress(t, goal);
            rect_fill(&c, 24, y, 400, 58, 0x10282e);
            rect_edge(&c, 24, y, 400, 58, progress == g->target ? 0x38dcc7 : MSP_DIM);
            draw_text(&c, 32, y + 3, 2, 0xffffff, g->name);
            draw_text(&c, 32, y + 21, 2, 0xb9d1d5, g->detail);
            snprintf(line, sizeof line, "%u/%u +%uXP +%uC", progress, g->target, g->xp, g->coins);
            draw_text(&c, 32, y + 39, 2, MSP_TEAL, line);
            rect_fill(&c, 32, y + 55, (int)(290u * progress / g->target), 2, MSP_TEAL);
            if (progress == g->target) draw_text(&c, 348, y + 39, 2, 0x38dcc7, "CLAIM");
        }
        if (journal_active(t, 0) < 0) {
            draw_text(&c, 50, 150, 2, MSP_TEAL, "ALL OBJECTIVES COMPLETE!");
            draw_text(&c, 50, 180, 2, 0xffffff, "CARE STILL EARNS XP");
            draw_text(&c, 50, 202, 2, MSP_TEAL, "NO DAILY STREAKS");
        }
    } else if (g_journal_tab == 1) {
        draw_text(&c, 24, 76, 2, MSP_TEAL, "LOOKS - CUSTOMIZATION COUNTS");
        for (int i = 0; i < FISH_VARIETY_COUNT; i++) {
            int x = 24 + i % 2 * 204, y = 96 + i / 2 * 39;
            bool seen = (t->journal.seen & (1u << i)) != 0, grown = (t->journal.grown & (1u << i)) != 0;
            fish_t portrait; memset(&portrait, 0, sizeof portrait);
            portrait.variety = (uint8_t)i; portrait.stage = STAGE_ADULT;
            portrait.color = seen ? 0x38dcc7 : 0x203c42; portrait.fin = seen ? 0x167f85 : 0x203c42;
            portrait.accent = seen ? 0xffbd59 : 0x203c42;
            render_fish_portrait(fb, stride, x + 26, y + 18, 0.6f, &portrait, t->clock);
            draw_text(&c, x + 56, y + 3, 2, seen ? 0xffffff : MSP_DIM, FISH_VARIETY_NAMES[i]);
            draw_text(&c, x + 56, y + 21, 2, MSP_TEAL, grown ? "ADULT" : seen ? "SEEN" : "NOT SEEN");
        }
    } else {
        static const int levels[4] = {1, 3, 6, 10};
        for (int theme = 0; theme < 4; theme++) {
            int y = 92 + theme * 46;
            bool active = t->journal.theme == theme;
            button(&c, 24, y, 400, 38, active ? 0x1c2f36 : 0x10282e,
                   active ? 0xffffff : journal_theme_unlocked(t, theme) ? MSP_TEAL : MSP_DIM, "", 2);
            draw_text(&c, 40, y + 12, 2, 0xffffff, JOURNAL_THEME_NAMES[theme]);
            if (active) draw_text(&c, 340, y + 12, 2, 0xffffff, "ACTIVE");
            if (!journal_theme_unlocked(t, theme)) {
                snprintf(line, sizeof line, "LEVEL %d", levels[theme]);
                draw_text(&c, 320, y + 12, 2, MSP_TEAL, line);
            }
        }
    }
    draw_text_wrap(&c, 24, 280, 400, 0xffffff, g_journal_message);
    static const char *const tabs[3] = {"GOALS", "ALBUM", "THEMES"};
    for (int i = 0; i < 3; i++) button(&c, 24 + i * 136, JOURNAL_TAB_Y, 128, 32,
                                     g_journal_tab == i ? 0x1c2f36 : 0x10282e,
                                     g_journal_tab == i ? 0xffffff : MSP_TEAL, tabs[i], 2);
    draw_text(&c, 24, 352, 2, MSP_TEAL, "CARE EARNS XP - TAP READY GOALS");
}
int render_journal_tap(tank_t *t, float x, float y) {
    x = render_page_x(x); y = render_page_y(y);
    if (x >= SET_CLOSE_X && y >= SET_CLOSE_Y && y < SET_CLOSE_Y + SET_CLOSE_H) return 1;
    if (y >= JOURNAL_TAB_Y && y < JOURNAL_TAB_Y + 32 && x >= 24 && x < 424) {
        g_journal_tab = (int)((x - 24) / 136); g_journal_message[0] = 0; return 0;
    }
    if (g_journal_tab == 0 && x >= 24 && x < 424 && y >= JOURNAL_GOAL_Y && y < JOURNAL_GOAL_Y + 3 * JOURNAL_GOAL_DY) {
        int goal = journal_active(t, (int)((y - JOURNAL_GOAL_Y) / JOURNAL_GOAL_DY));
        if (goal >= 0 && journal_progress(t, goal) == JOURNAL_GOALS[goal].target)
            snprintf(g_journal_message, sizeof g_journal_message, "%s", journal_claim(t, goal) ? "REWARD SAVED!" : "REWARD FAILED - CHECK SAVE STORAGE");
        else snprintf(g_journal_message, sizeof g_journal_message, "KEEP CARING - PROGRESS NEVER EXPIRES");
    } else if (g_journal_tab == 2 && x >= 24 && x < 424 && y >= 92 && y < 276) {
        int theme = (int)((y - 92) / 46);
        snprintf(g_journal_message, sizeof g_journal_message, "%s", !journal_theme_unlocked(t, theme) ? "EARN MORE XP TO UNLOCK THIS THEME" :
                 journal_set_theme(t, theme) ? "THEME SAVED!" : "SAVE FAILED - THEME UNCHANGED");
    }
    return 0;
}
enum { GAME_MENU, GAME_BUBBLES, GAME_MATCH, GAME_RESULT, GAME_SLALOM, GAME_CAST, GAME_FRENZY, GAME_REEF };
static struct {
    int page, kind, score, moves, pairs, first, second, spawned, reward;
    int fish, lane, target, errors, rounds, discard;
    float left, spawn, reveal;
    float travel;
    bool claimed, save_failed;
    uint8_t cards[12], matched[12], pipes[9];
    struct { float x, y; bool live, gold; } bubbles[6];
} g_game;
static uint32_t g_game_rng = 0x7a5b193du;
static uint32_t game_random(void) {
    g_game_rng ^= g_game_rng << 13; g_game_rng ^= g_game_rng >> 17; g_game_rng ^= g_game_rng << 5;
    return g_game_rng;
}
void render_games_open(void) {
    memset(&g_game, 0, sizeof g_game); g_game.first = g_game.second = -1;
}
static void game_start(int kind) {
    int fish = g_game.fish;
    render_games_open(); g_game.fish = fish; g_game.page = g_game.kind = kind; g_game.left = 30;
    g_game.target = (int)(game_random() % 3); g_game.lane = 1;
    for (int i = 0; i < 12; i++) g_game.cards[i] = (uint8_t)(i / 2);
    for (int i = 11; i > 0; i--) {
        int j = (int)(game_random() % (unsigned)(i + 1));
        uint8_t tmp = g_game.cards[i]; g_game.cards[i] = g_game.cards[j]; g_game.cards[j] = tmp;
    }
    static const uint8_t routes[2][9] = {
        {6, 10, 12, 9, 3, 3, 3, 3, 3},
        {3, 3, 3, 12, 3, 6, 3, 10, 9}
    };
    unsigned route = game_random() % 2;
    for (int i = 0; i < 9; i++) {
        unsigned mask = routes[route][i];
        for (unsigned r = game_random() % 4; r; r--) mask = ((mask << 1) | (mask >> 3)) & 15;
        g_game.pipes[i] = (uint8_t)mask;
    }
    g_game.pipes[3] = 3; /* Closed inlet: a generated puzzle cannot start solved. */
}
static void game_finish(void) {
    g_game.page = GAME_RESULT;
    g_game.reward = g_game.kind == GAME_MATCH ? (g_game.moves <= 18 ? 8 : 5) :
                   g_game.kind == GAME_REEF ? 6 : g_game.kind == GAME_CAST ? g_game.score :
                   g_game.score / (g_game.kind == GAME_BUBBLES ? 3 : 2);
    if (g_game.reward > 10) g_game.reward = 10;
}
void render_games_tick(float dt) {
    if (!isfinite(dt) || dt <= 0) return;
    if (dt > 0.25f) dt = 0.25f;
    if (g_game.page == GAME_BUBBLES) {
        g_game.left -= dt;
        if (g_game.left <= 0) { g_game.left = 0; game_finish(); return; }
        g_game.spawn -= dt;
        for (int i = 0; i < 6; i++) {
            if (g_game.bubbles[i].live) {
                g_game.bubbles[i].y -= dt * (g_game.bubbles[i].gold ? 58 : 42);
                if (g_game.bubbles[i].y < 100) g_game.bubbles[i].live = false;
            }
            if (!g_game.bubbles[i].live && g_game.spawn <= 0) {
                g_game.bubbles[i].x = 52 + game_random() % 344;
                g_game.bubbles[i].y = 304; g_game.bubbles[i].live = true;
                g_game.bubbles[i].gold = ++g_game.spawned % 7 == 0;
                g_game.spawn = 0.65f;
            }
        }
    } else if (g_game.page == GAME_SLALOM) {
        g_game.travel += dt * 95;
        if (g_game.travel >= 210) {
            if (g_game.lane == g_game.target) g_game.score += 2; else g_game.errors++;
            g_game.travel = 0; g_game.rounds++; g_game.target = (int)(game_random() % 3);
            if (g_game.rounds == 12) game_finish();
        }
    } else if (g_game.page == GAME_CAST) {
        g_game.travel += dt * 0.9f;
        if (g_game.travel >= 2) g_game.travel -= 2;
    } else if (g_game.page == GAME_FRENZY) {
        g_game.left -= dt;
        if (g_game.reveal > 0) g_game.reveal -= dt;
        if (g_game.left <= 0) { g_game.left = 0; game_finish(); }
    } else if (g_game.page == GAME_MATCH && g_game.reveal > 0) {
        g_game.reveal -= dt;
        if (g_game.reveal <= 0) g_game.first = g_game.second = -1;
    }
}
static bool game_reef_connected(void) {
    bool wet[9] = {false};
    if (!(g_game.pipes[3] & 8)) return false;
    wet[3] = true;
    static const int dx[] = {0, 1, 0, -1}, dy[] = {-1, 0, 1, 0};
    for (int pass = 0; pass < 9; pass++) for (int i = 0; i < 9; i++) if (wet[i]) {
        for (int d = 0; d < 4; d++) {
            int x = i % 3 + dx[d], y = i / 3 + dy[d];
            if (x >= 0 && x < 3 && y >= 0 && y < 3 && (g_game.pipes[i] & (1 << d)) &&
                (g_game.pipes[y * 3 + x] & (1 << ((d + 2) % 4)))) wet[y * 3 + x] = true;
        }
    }
    return wet[5] && (g_game.pipes[5] & 2);
}
bool render_games_tap(tank_t *t, float x, float y) {
    x = render_page_x(x); y = render_page_y(y);
    if (x >= 326 && x < 438 && y >= 8 && y < 44) {
        if (g_game.page == GAME_MENU) return true;
        if (g_game.page == GAME_RESULT && g_game.reward && !g_game.claimed && g_game.discard != 1) {
            g_game.discard = 1; return false;
        }
        render_games_open(); return false;
    }
    if (g_game.page == GAME_MENU) {
        if (x >= 32 && x < 428 && y >= 60 && y < 102 && t->n_fish)
            g_game.fish = (g_game.fish + 1) % t->n_fish;
        if (x >= 24 && x < 424 && y >= 116 && y < 308) {
            int col = x >= 228, row = (int)(y - 116) / 64;
            static const int kinds[] = {GAME_BUBBLES, GAME_MATCH, GAME_SLALOM, GAME_CAST, GAME_FRENZY, GAME_REEF};
            if (x < (col ? 424 : 220) && y < 116 + row * 64 + 56) game_start(kinds[row * 2 + col]);
        }
    } else if (g_game.page == GAME_BUBBLES) {
        for (int i = 5; i >= 0; i--) {
            float dx = x - g_game.bubbles[i].x, dy = y - g_game.bubbles[i].y;
            if (g_game.bubbles[i].live && dx * dx + dy * dy <= 28 * 28) {
                g_game.score += g_game.bubbles[i].gold ? 3 : 1;
                g_game.bubbles[i].live = false; break;
            }
        }
    } else if (g_game.page == GAME_SLALOM) {
        if (y >= 276 && y < 328) {
            if (x >= 32 && x < 214 && g_game.lane > 0) g_game.lane--;
            else if (x >= 234 && x < 416 && g_game.lane < 2) g_game.lane++;
        }
    } else if (g_game.page == GAME_CAST) {
        if (x >= 32 && x < 416 && y >= 276 && y < 328) {
            float pos = g_game.travel <= 1 ? g_game.travel : 2 - g_game.travel;
            float target = (g_game.target + 0.5f) / 3;
            if (fabsf(pos - target) < 0.12f) g_game.score += 2; else g_game.errors++;
            g_game.rounds++; g_game.travel = 0; g_game.target = (int)(game_random() % 3);
            if (g_game.rounds == 5) game_finish();
        }
    } else if (g_game.page == GAME_FRENZY) {
        if (x >= 24 && x < 424 && y >= 268 && y < 326 && g_game.reveal <= 0) {
            int choice = (int)(x - 24) / 136;
            if (choice < 3 && x < 24 + choice * 136 + 128) {
                if (choice == g_game.target) g_game.score += 2; else { g_game.errors++; if (g_game.score) g_game.score--; }
                g_game.target = (int)(game_random() % 3);
                g_game.reveal = 0.2f;
                if (t->n_fish) g_game.fish = (g_game.fish + 1) % t->n_fish;
            }
        }
    } else if (g_game.page == GAME_REEF) {
        if (x >= 112 && x < 328 && y >= 98 && y < 314) {
            int col = (int)(x - 112) / 72, row = (int)(y - 98) / 72, i = row * 3 + col;
            if (x < 112 + col * 72 + 66 && y < 98 + row * 72 + 66) {
                unsigned mask = g_game.pipes[i];
                g_game.pipes[i] = (uint8_t)(((mask << 1) | (mask >> 3)) & 15);
                if (g_game.moves < 9999) g_game.moves++;
                if (game_reef_connected()) game_finish();
            }
        }
    } else if (g_game.page == GAME_MATCH && g_game.reveal <= 0) {
        if (x >= 32 && x < 416 && y >= 94 && y < 310) {
            int col = (int)(x - 32) / 96, row = (int)(y - 94) / 72, i = row * 4 + col;
            if (x >= 32 + col * 96 + 88 || y >= 94 + row * 72 + 64 || g_game.matched[i] || i == g_game.first) return false;
            if (g_game.first < 0) g_game.first = i;
            else {
                g_game.second = i; if (g_game.moves < 9999) g_game.moves++;
                if (g_game.cards[i] == g_game.cards[g_game.first]) {
                    g_game.matched[i] = g_game.matched[g_game.first] = 1; g_game.pairs++;
                    g_game.first = g_game.second = -1;
                    if (g_game.pairs == 6) game_finish();
                } else g_game.reveal = 0.85f;
            }
        }
    } else if (g_game.page == GAME_RESULT && x >= 32 && x < 428) {
        if (y >= 196 && y < 240 && g_game.reward && !g_game.claimed) {
            if (progression_game_reward(t, g_game.reward)) { g_game.claimed = true; g_game.save_failed = false; g_game.discard = 0; }
            else g_game.save_failed = true;
        } else if (y >= 278 && y < 322) {
            if (g_game.reward && !g_game.claimed && g_game.discard != 2) g_game.discard = 2;
            else game_start(g_game.kind);
        }
    }
    return false;
}
void render_games(const tank_t *t, uint16_t *fb, int stride) {
    ctx_t c = ctx_page(fb, stride);
    rect_fill(&c, 0, 0, PAGE_W, PAGE_H, MSP_INK);
    static const char *const names[] = {"MINI GAMES", "BUBBLE POP", "PEARL MATCH", "", "FISH SLALOM", "TREASURE CAST", "FEED FRENZY", "REEF PUZZLE"};
    const char *title = names[g_game.page == GAME_MENU ? GAME_MENU : g_game.kind];
    draw_text(&c, 24, 14, 2, 0xffffff, title);
    button(&c, 326, 8, 112, 36, 0x1c2f36, MSP_TEAL, g_game.page == GAME_MENU ? "CLOSE" : "BACK", 2);
    char text[64];
    if (g_game.page == GAME_MENU) {
        snprintf(text, sizeof text, "PLAY AS: %s >", t->n_fish ? t->fish[g_game.fish % t->n_fish].name : "GUEST");
        button(&c, 32, 60, 396, 42, 0x1c2f36, MSP_TEAL, text, 2);
        static const int kinds[] = {GAME_BUBBLES, GAME_MATCH, GAME_SLALOM, GAME_CAST, GAME_FRENZY, GAME_REEF};
        for (int i = 0; i < 6; i++)
            button(&c, 24 + i % 2 * 204, 116 + i / 2 * 64, 196, 56, 0x1c2f36, MSP_TEAL, names[kinds[i]], 2);
        draw_text(&c, 32, 316, 2, 0xffffff, "WIN SAND DOLLARS + JOURNAL XP");
    } else if (g_game.page == GAME_BUBBLES) {
        snprintf(text, sizeof text, "TIME %02d   SCORE %d", (int)ceilf(g_game.left), g_game.score);
        draw_text(&c, 32, 62, 2, MSP_TEAL, text);
        for (int i = 0; i < 6; i++) if (g_game.bubbles[i].live) {
            int x = (int)g_game.bubbles[i].x, y = (int)g_game.bubbles[i].y;
            uint32_t color = g_game.bubbles[i].gold ? 0xffdc73 : 0x73e5ff;
            ring(&c, x, y, 24, color); ring(&c, x, y, 23, color);
            rect_fill(&c, x - 10, y - 13, 8, 4, 0xffffff);
            if (g_game.bubbles[i].gold) draw_text(&c, x - 5, y - 7, 2, color, "3");
        }
    } else if (g_game.page == GAME_SLALOM || g_game.page == GAME_CAST || g_game.page == GAME_FRENZY) {
        snprintf(text, sizeof text, "SCORE %d   %s %d", g_game.score, g_game.page == GAME_FRENZY ? "TIME" : "ROUND",
                 g_game.page == GAME_FRENZY ? (int)ceilf(g_game.left) : g_game.rounds + 1);
        draw_text(&c, 24, 58, 2, MSP_TEAL, text);
        if (g_game.page == GAME_SLALOM) {
            draw_text(&c, 24, 82, 2, MSP_TEAL, "SWIM THROUGH THE OPEN GATE");
            for (int lane = 0; lane < 3; lane++) {
                int x = 74 + lane * 140;
                rect_fill(&c, x - 46, 112, 92, 148, 0x102831);
                if (lane == g_game.target) {
                    rect_edge(&c, x - 36, 112 + (int)(g_game.travel * 0.55f), 72, 12, 0xffdc73);
                    rect_fill(&c, x - 3, 115 + (int)(g_game.travel * 0.55f), 6, 6, 0xffdc73);
                }
                else rect_fill(&c, x - 44, 112 + (int)(g_game.travel * 0.55f), 88, 12, 0x8b6570);
            }
            if (t->n_fish) render_fish_portrait(fb, stride, 74 + g_game.lane * 140, 235, 1.3f, &t->fish[g_game.fish % t->n_fish], g_game.travel * 0.03f);
            button(&c, 32, 276, 182, 52, 0x1c2f36, MSP_TEAL, "< LEFT", 2);
            button(&c, 234, 276, 182, 52, 0x1c2f36, MSP_TEAL, "RIGHT >", 2);
        } else if (g_game.page == GAME_CAST) {
            draw_text(&c, 24, 86, 2, MSP_TEAL, "STOP THE HOOK OVER THE TREASURE");
            int tx = 32 + (int)((g_game.target + 0.5f) * 384 / 3);
            rect_fill(&c, tx - 42, 218, 84, 38, 0xa97532); rect_edge(&c, tx - 42, 218, 84, 38, 0xffdc73);
            draw_text(&c, tx - 12, 226, 2, 0xffdc73, "$");
            float pos = g_game.travel <= 1 ? g_game.travel : 2 - g_game.travel;
            int hx = 32 + (int)(pos * 384);
            rect_fill(&c, hx, 116, 2, 84, MSP_TEAL); ring(&c, hx - 5, 201, 6, 0xffffff);
            for (int lane = 0; lane < 3; lane++) if (lane != g_game.target) {
                if (t->n_fish && lane == (g_game.target + 1) % 3)
                    render_fish_portrait(fb, stride, 96 + lane * 128, 234, 0.7f,
                                         &t->fish[g_game.fish % t->n_fish], g_game.travel);
                else rect_fill(&c, 64 + lane * 128, 224, 62, 26, 0x785b6a);
            }
            button(&c, 32, 276, 384, 52, 0x1c2f36, MSP_TEAL, "CAST!", 3);
        } else {
            static const char *const snacks[] = {"FLAKES", "WORMS", "GREENS"};
            snprintf(text, sizeof text, "WANTS: %s", snacks[g_game.target]);
            draw_text(&c, 80, 98, 3, 0xffdc73, text);
            rect_fill(&c, 84 + g_game.target * 136, 252, 8, 8, 0xffdc73);
            if (t->n_fish) {
                const fish_t *f = &t->fish[g_game.fish % t->n_fish];
                render_fish_portrait(fb, stride, 224, 182, 2, f, g_game.left);
                draw_text(&c, 32, 232, 2, MSP_TEAL, f->name);
            }
            for (int i = 0; i < 3; i++) button(&c, 24 + i * 136, 268, 128, 58, 0x1c2f36, MSP_TEAL, snacks[i], 2);
        }
    } else if (g_game.page == GAME_REEF) {
        draw_text(&c, 24, 58, 2, MSP_TEAL, "TAP PIPES TO CONNECT IN TO OUT");
        draw_text(&c, 32, 191, 2, 0x73e5ff, "IN >"); draw_text(&c, 344, 191, 2, 0xffdc73, "> OUT");
        for (int i = 0; i < 9; i++) {
            int x = 112 + i % 3 * 72, y = 98 + i / 3 * 72, mask = g_game.pipes[i];
            rect_fill(&c, x, y, 66, 66, 0x1c2f36); rect_edge(&c, x, y, 66, 66, MSP_TEAL);
            if (mask & 1) rect_fill(&c, x + 27, y + 1, 12, 38, 0x73e5ff);
            if (mask & 2) rect_fill(&c, x + 27, y + 27, 38, 12, 0x73e5ff);
            if (mask & 4) rect_fill(&c, x + 27, y + 27, 12, 38, 0x73e5ff);
            if (mask & 8) rect_fill(&c, x + 1, y + 27, 38, 12, 0x73e5ff);
        }
    } else if (g_game.page == GAME_MATCH) {
        snprintf(text, sizeof text, "PAIRS %d/6   MOVES %d", g_game.pairs, g_game.moves);
        draw_text(&c, 32, 62, 2, MSP_TEAL, text);
        static const uint32_t colors[] = {0xffdc73, 0x73e5ff, 0xffa3cf, 0x89ffb1, 0xc5adff, 0xff8173};
        for (int i = 0; i < 12; i++) {
            int x = 32 + i % 4 * 96, y = 94 + i / 4 * 72;
            bool shown = g_game.matched[i] || i == g_game.first || i == g_game.second;
            button(&c, x, y, 88, 64, g_game.matched[i] ? 0x124236 : 0x1c2f36, MSP_TEAL, shown ? "" : "?", 3);
            if (shown) {
                uint32_t color = colors[g_game.cards[i]];
                ring(&c, x + 22, y + 32, 12, color);
                snprintf(text, sizeof text, "%d", g_game.cards[i] + 1);
                draw_text(&c, x + 46, y + 21, 3, color, text);
            }
        }
    } else {
        draw_text(&c, 32, 80, 3, 0xffffff, "ROUND COMPLETE");
        snprintf(text, sizeof text, g_game.kind == GAME_MATCH || g_game.kind == GAME_REEF ? "SOLVED IN %d MOVES" : "SCORE: %d",
                 g_game.kind == GAME_MATCH || g_game.kind == GAME_REEF ? g_game.moves : g_game.score);
        draw_text(&c, 32, 126, 2, MSP_TEAL, text);
        snprintf(text, sizeof text, "WALLET: %ld SAND DOLLARS", (long)t->sd_balance);
        draw_text(&c, 32, 164, 2, MSP_TEAL, text);
        snprintf(text, sizeof text, g_game.claimed ? "SAVED +%d SAND DOLLARS" : g_game.reward ? "CLAIM %d SAND DOLLARS" : "SCORE TOO LOW - TRY AGAIN", g_game.reward);
        button(&c, 32, 196, 396, 44, 0x1c2f36, MSP_TEAL, text, 2);
        button(&c, 32, 278, 396, 44, 0x1c2f36, MSP_TEAL, "PLAY AGAIN", 2);
        snprintf(text, sizeof text, "ALSO %d JOURNAL XP", g_game.reward * 3);
        draw_text(&c, 32, 248, 2, MSP_TEAL, text);
    }
    draw_text(&c, 24, 340, 2, g_game.save_failed ? 0xff8173 : MSP_TEAL,
              g_game.save_failed ? "SAVE FAILED - TAP CLAIM TO RETRY" :
              g_game.discard ? "TAP AGAIN TO DISCARD REWARD" : "AQUARIUM CARE PAUSED");
}
static int set_segment(float x, int n) {
    if (x < SET_SEG_X - 10) return -1;
    int i = (int)((x - SET_SEG_X + 3) / SET_SEG_DX);
    return i < 0 ? 0 : i >= n ? n - 1 : i;
}
/* *value: BRIGHT the percent, FEED 1 = ON. */
int render_settings_tap(float x, float y, int *value) {
    x = render_page_x(x); y = render_page_y(y);
    if (g_clock_settings) {
        if (g_clock_style) {
            if (x >= 326 && x < 438 && y >= 8 && y < 44) return CLOCK_HIT_STYLE;
            if (x >= 32 && x < 428) {
                if (y >= 56 && y < 94) return CLOCK_HIT_COLOR;
                if (y >= 100 && y < 138) return CLOCK_HIT_SECONDARY;
                if (y >= 144 && y < 182) return CLOCK_HIT_SECONDS;
                if (y >= 188 && y < 226) return CLOCK_HIT_PANEL;
                if (y >= 232 && y < 270) return CLOCK_HIT_DATE;
                if (y >= 282 && y < 324) return SET_TAP_CLOCK_SHOW;
            }
            return SET_TAP_NONE;
        }
        if (x >= 140 && x < 314 && y >= 8 && y < 44) return CLOCK_HIT_STYLE;
        if (x >= 32 && x < 222 && y >= 300 && y < 336) return SET_TAP_CLOCK_SHOW;
        if (x >= 238 && x < 428 && y >= 300 && y < 336) return SET_TAP_WIFI;
        if (x >= 326 && x < 438 && y >= 8 && y < 44) return CLOCK_HIT_PAGE;
        if (x >= 32 && x < 428) {
            if (y >= 52 && y < 86) return CLOCK_HIT_THEME;
            if (y >= 92 && y < 126) return CLOCK_HIT_SIZE;
            if (y >= 132 && y < 166) return CLOCK_HIT_POSITION;
            if (y >= 172 && y < 206 && x < 222) return CLOCK_HIT_FORMAT;
            if (y >= 172 && y < 206 && x >= 238) return CLOCK_HIT_DST;
            if (y >= 212 && y < 246) return CLOCK_HIT_DELAY;
            if (y >= 252 && y < 288 && x < 108) return CLOCK_HIT_PREVIOUS;
            if (y >= 252 && y < 288 && x >= 352) return CLOCK_HIT_NEXT;
            if (y >= 270 && y < 294 && x >= 116 && x < 344) return CLOCK_HIT_STEP;
        }
        return SET_TAP_NONE;
    }
    if (x >= 144 && x < 300 && y >= SET_CLOSE_Y - 4 && y < SET_CLOSE_Y + SET_CLOSE_H + 4) return CLOCK_HIT_PAGE;
    if (x >= SET_WIPE_X && x < SET_WIPE_X + SET_WIPE_W + 4 &&
        y >= SET_WIPE_Y - 4 && y < SET_WIPE_Y + SET_WIPE_H + 4) return SET_TAP_WIPE;
    if (x >= SET_CLOSE_X - 4 && x < SET_CLOSE_X + SET_CLOSE_W + 4 &&
        y >= SET_CLOSE_Y - 4 && y < SET_CLOSE_Y + SET_CLOSE_H + 4) return SET_TAP_CLOSE;
    if (x >= SET_TOUCH_X - 8 && x < SET_TOUCH_X + SET_TOUCH_W + 8 &&
        y >= SET_FOOT_Y - 4 && y < SET_FOOT_Y + MSP_CLOSE_H + 4) return SET_TAP_TOUCH;
    if (x >= SET_UPD_X - 8 && x < SET_UPD_X + SET_UPD_W + 8 &&
        y >= SET_FOOT_Y - 4 && y < SET_FOOT_Y + MSP_CLOSE_H + 4) return SET_TAP_UPDATES;
    /* the row bands: from a little above each segment down to the next row
       (fingers report low); the last one stops at the foot's */
    int seg = set_segment(x, 3), two = set_segment(x, 2);
    if (y >= SET_SEG_Y(SET_ROW1_Y) - 12 && y < SET_SEG_Y(SET_ROW2_Y) - 12) { if (seg < 0) return SET_TAP_NONE; *value = SET_BRIGHT_PCT[seg]; return SET_TAP_BRIGHT; }
    if (y >= SET_SEG_Y(SET_ROW2_Y) - 8 && y < SET_SEG_Y(SET_ROW2_Y) + SET_SEG_H + 4) {
        if (x >= SET_LABEL_X && x < 202) return SET_TAP_BACKUP;
        if (x >= 218 && x < 416) return SET_TAP_RESTORE;
        return SET_TAP_NONE;
    }
    if (y >= SET_SEG_Y(SET_ROW3_Y) - 4 && y < SET_SEG_Y(SET_ROW4_Y) - 4) { if (two < 0 || x >= SET_SEG_X + 2 * SET_SEG_DX) return SET_TAP_NONE; *value = two == 0; return SET_TAP_FEED; }
    if (x >= SET_LABEL_X && x < SET_LABEL_X + SET_COLOR_W) {
        if (y >= SET_SEG_Y(SET_ROW4_Y) - 4 && y < SET_SEG_Y(SET_ROW5_Y) - 4) return SET_TAP_SAND;
        if (y >= SET_SEG_Y(SET_ROW5_Y) - 4 && y < SET_SEG_Y(SET_ROW5_Y) + SET_SEG_H + 4) return SET_TAP_WATER;
    }
    return SET_TAP_NONE;
}
int render_settings_touch(tank_t *t, float x, float y, bool down, int *value) {
    static bool s_down; static float s_px, s_py; static int s_hit, s_hv;
    int r = SET_TAP_NONE; *value = 0;
    s_backup_clock = t->clock;
    if (down && !s_down) {                                  /* press */
        s_px = x; s_py = y;
        s_hit = render_settings_tap(x, y, &s_hv);
    } else if (!down && s_down) {                           /* release: a tap, if it stayed on what it pressed */
        int v = 0, h = render_settings_tap(x, y, &v);
        float dx = x - s_px, dy = y - s_py;
        if (h == s_hit && dx * dx + dy * dy < 24 * 24) {
            if (h == CLOCK_HIT_PAGE) {
                g_clock_settings = !g_clock_settings;
                g_clock_style = false;
                render_backup_status(g_clock_settings ? (g_clock_synchronized ? "NTP READY - CHANGES SAVE AUTO" : "SYNC NEEDED - SET UP WI-FI") : "JSON BACKUP / RESTORE FROM SD", false);
            } else if (h == CLOCK_HIT_STEP) {
                g_clock_step = (g_clock_step + 1) % 3;
            } else if (h == CLOCK_HIT_STYLE) {
                g_clock_style = !g_clock_style;
            } else if (h >= CLOCK_HIT_THEME && h <= CLOCK_HIT_SECONDARY) {
                aquarium_state_t before = t->aquariums;
                if (h == CLOCK_HIT_THEME) t->aquariums.clock_theme = (uint8_t)((t->aquariums.clock_theme + 1) % CLOCK_THEME_COUNT);
                if (h == CLOCK_HIT_FORMAT) t->aquariums.clock_flags ^= CLOCK_12H;
                if (h == CLOCK_HIT_DST) t->aquariums.clock_flags ^= CLOCK_DST;
                if (h == CLOCK_HIT_COLOR) t->aquariums.clock_flags = (uint8_t)((t->aquariums.clock_flags & ~CLOCK_COLOR_MASK) | (((t->aquariums.clock_flags >> CLOCK_COLOR_SHIFT) + 1) % 8 << CLOCK_COLOR_SHIFT));
                if (h == CLOCK_HIT_SECONDARY) t->aquariums.clock_delay = (uint8_t)((t->aquariums.clock_delay & CLOCK_DELAY_MASK) | (((t->aquariums.clock_delay >> CLOCK_SECONDARY_SHIFT) + 1) % 8 << CLOCK_SECONDARY_SHIFT));
                if (h == CLOCK_HIT_SECONDS) t->aquariums.clock_flags ^= CLOCK_HIDE_SECONDS;
                if (h == CLOCK_HIT_PANEL) t->aquariums.clock_flags ^= CLOCK_HIDE_PANEL;
                if (h == CLOCK_HIT_DATE) t->aquariums.clock_flags ^= CLOCK_HIDE_DATE;
                if (h == CLOCK_HIT_POSITION) t->aquariums.clock_layout = (uint8_t)((((t->aquariums.clock_layout >> 2) + 1) % CLOCK_POSITION_COUNT) * 4 + (t->aquariums.clock_layout & 3));
                if (h == CLOCK_HIT_SIZE) t->aquariums.clock_layout = (uint8_t)((t->aquariums.clock_layout & ~3) + ((t->aquariums.clock_layout & 3) + 1) % CLOCK_SIZE_COUNT);
                if (h == CLOCK_HIT_DELAY) t->aquariums.clock_delay = (uint8_t)((t->aquariums.clock_delay & CLOCK_SECONDARY_MASK) | (((t->aquariums.clock_delay & CLOCK_DELAY_MASK) + 1) % CLOCK_DELAY_COUNT));
                if (h == CLOCK_HIT_PREVIOUS || h == CLOCK_HIT_NEXT) {
                    int offset = t->aquariums.clock_utc_quarters;
                    offset += (h == CLOCK_HIT_NEXT ? 1 : -1) * CLOCK_STEPS[g_clock_step];
                    if (offset < -48) offset = t->aquariums.clock_utc_quarters == -48 ? 56 : -48;
                    if (offset > 56) offset = t->aquariums.clock_utc_quarters == 56 ? -48 : 56;
                    t->aquariums.clock_utc_quarters = (int8_t)offset;
                }
                if (!progression_save(t)) {
                    t->aquariums = before;
                    fprintf(stderr, "Clock setting save failed; change rolled back\n");
                    render_backup_status("CLOCK SAVE FAILED - RETRY", false);
                } else render_backup_status("CLOCK SAVED FOR THIS TANK", false);
            } else if (h == SET_TAP_CLOCK_SHOW) {
                if (t->aquariums.clock_theme == CLOCK_OFF) render_backup_status("CHOOSE A CLOCK THEME FIRST", false);
                else { g_clock_settings = g_clock_style = false; r = h; }
            } else if (h == SET_TAP_FEED) {                    /* ON (1, the default) / OFF */
                if ((v == 0) != t->autofeed_off) { t->autofeed_off = v == 0; progression_settings_changed(); }
                r = SET_TAP_FEED; *value = v;
            } else if (h == SET_TAP_SAND || h == SET_TAP_WATER) {
                bool sand = h == SET_TAP_SAND;
                if (!(t->sd_unlocks & (sand ? SD_ITEM_SAND : SD_ITEM_LIGHTS))) {
                    render_backup_status(sand ? "UNLOCK SAND PALETTE IN THE SHOP" : "UNLOCK MOOD LIGHTS IN THE SHOP", false);
                } else {
                    uint8_t *style = sand ? &t->sand_style : &t->water_style;
                    *style = (uint8_t)((*style + 1) % (sand ? SAND_STYLE_COUNT : WATER_STYLE_COUNT));
                    progression_settings_changed(); r = h; *value = *style;
                }
            } else if (h == SET_TAP_CLOSE || h == SET_TAP_BRIGHT || h == SET_TAP_TOUCH || h == SET_TAP_UPDATES
                       || h == SET_TAP_BACKUP || h == SET_TAP_RESTORE || h == SET_TAP_WIPE || h == SET_TAP_WIFI) {
                r = h; *value = v;
                if (h == SET_TAP_RESTORE) *value = s_restore_until > t->clock;
                else s_restore_until = 0;
            }
        }
    }
    s_down = down;
    return r;
}
