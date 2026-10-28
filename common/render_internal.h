#ifndef RENDER_INTERNAL_H
#define RENDER_INTERNAL_H

#include "render.h"

typedef struct {
    uint16_t *fb;
    int stride;
    float dim;
    int ox, oy, w, h;
    bool scaled;
} render_ctx_t;

#define MSP_INK  0x031015
#define MSP_DIM  0x2a3f45
#define MSP_TEAL 0x9fd8e2

enum {
    STAMP_FISH, STAMP_TANK, STAMP_MAP, STAMP_BOOK, STAMP_GAME, STAMP_GEAR,
    STAMP_HEART, STAMP_BANK, STAMP_SHIRT, STAMP_PEN, STAMP_FAMILY, STAMP_CUP,
    STAMP_FOOD, STAMP_LEAF, STAMP_CLEAN
};

render_ctx_t render_internal_page_ctx(uint16_t *fb, int stride);
void render_internal_rect_fill(render_ctx_t *c, int x, int y, int w, int h, uint32_t rgb);
void render_internal_rect_edge(render_ctx_t *c, int x, int y, int w, int h, uint32_t rgb);
void render_internal_ring(render_ctx_t *c, float cx, float cy, float r, uint32_t rgb);
void render_internal_button(render_ctx_t *c, int x, int y, int w, int h,
                            uint32_t fill, uint32_t edge, const char *label, int scale);
void render_internal_text(render_ctx_t *c, int x, int y, int scale, uint32_t rgb, const char *s);
int render_internal_text_wrap(render_ctx_t *c, int x, int y, int width, uint32_t rgb, const char *s);
void render_internal_page_stamp(render_ctx_t *c, int x, int y, int kind, uint32_t color);
void render_internal_page_medal(render_ctx_t *c, int x, int y, unsigned medal);
/* game art primitives: each frame row is drawn once (alpha 255 = opaque) */
void render_internal_ellipse(render_ctx_t *c, float cx, float cy, float rx, float ry, uint32_t rgb, int alpha);
void render_internal_annulus(render_ctx_t *c, float cx, float cy, float r, float r_in, uint32_t rgb, int alpha);
void render_internal_rect_blend(render_ctx_t *c, int x, int y, int w, int h, uint32_t rgb, int alpha);
void render_internal_poly(render_ctx_t *c, const float *xs, const float *ys, int n, uint32_t rgb, int alpha);

#ifdef RENDER_GAMES_TESTING
#include "games_internal.h"
#endif

#endif
