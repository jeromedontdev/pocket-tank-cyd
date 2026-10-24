/* render.h — software renderer: draws the tank into a raw RGB565 framebuffer.
 * No LVGL/SDL dependency; the same code runs on the ESP32 (the display port
 * just decides where the buffer goes). */
#ifndef RENDER_H
#define RENDER_H

#include <stddef.h>
#include "tank.h"

/* fb is TANK_W x TANK_H, RGB565, stride in PIXELS (usually TANK_W). */
/* PAGE space (2026-10-01): the full-screen pages (milestones, shop, settings,
 * updates, the setup flow), the prompts and the notices are laid out on the
 * rectangle's 448 x 368. On a bigger frame (the round bowl's 466 x 466) that
 * layout sits centred: page (0,0) is frame (PAGE_X, PAGE_Y). The drawing
 * helpers below (render_text, render_rect, ...) take PAGE coordinates; the
 * tap tests take the frame's, as the touch ports report them. In the
 * rectangle the two are the same thing. The CYD scales the entire page
 * into its smaller framebuffer; render_page_* and render_frame_* convert
 * between these coordinate spaces. Text is rasterized on the framebuffer
 * grid, never reduced below a complete 5x7 glyph. */
#define PAGE_W 448
#define PAGE_H 368
#define PAGE_X 0
#define PAGE_Y 0
#define PAGE_FRAME_W PAGE_W
#define PAGE_FRAME_H PAGE_H
void render_tile(int y, int height);
int render_tile_top(void);
int render_tile_bottom(void);
const char *render_palette_name(bool sand, int index);
void render_settings_open(void);
static inline float render_page_x(float x) {
    return x * PAGE_W / TANK_W;
}
static inline float render_page_y(float y) {
    return y * PAGE_H / TANK_H;
}
static inline float render_frame_x(float x) {
    return x * TANK_W / PAGE_W;
}
static inline float render_frame_y(float y) {
    return y * TANK_H / PAGE_H;
}
void render_tank(const tank_t *t, uint16_t *fb, int stride);
void render_clock_overlay(const tank_t *t, uint16_t *fb, int stride, int64_t unix_time);
bool render_clock_format(const tank_t *t, int64_t unix_time, char text[9], int *hour, int *minute);
void render_clock_synchronized(bool synchronized);
bool render_clock_active(const tank_t *t);
void render_clock_bounds(const tank_t *t, int *x, int *y, int *w, int *h);
/* Whole clock bounds include the separate, non-overlapping caption panel.
 * Circular faces step down if necessary to keep legible captions on screen. */
typedef struct {
    int x, y, w, h, face_x, face_y, face_w, face_h;
    int caption_y, caption_h, caption_scale, caption_pitch, size, rows;
} render_clock_layout_t;
void render_clock_layout(const tank_t *t, render_clock_layout_t *layout);

/* Optional frame profiling: set a microsecond clock and render_tank fills
 * render_prof_us per stage (0 scene-copy, 1 shafts, 2 veg (back), 3 food+
 * bubbles, 4 fish + front veg, 5 vignette sweep, 6 algae film, 7 clock). Accumulates
 * until the caller zeroes it. NULL = off. */
extern int64_t (*render_clock_us)(void);
extern int64_t render_prof_us[8];

/* Optional static-scene cache (TANK_W*TANK_H uint16): the water gradient,
 * pebbles and backdrop decor are rendered once per lighting state and copied each
 * frame instead of recomputed - keeps core 0's frame budget flat as the scene
 * gets lusher. NULL disables. */
void render_set_scene_cache(uint16_t *buf);
/* Lossless row RLE of background and a stable decoration prefix; complex rows draw live. */
void render_set_backdrop_cache(void *buf, size_t bytes);
/* Begin once after simulation; ranges include old bounds and require retained LCD contents. */
void render_damage_begin(const tank_t *t, bool force_full);
bool render_damage_range(int tile_y, int tile_height, int *first, int *height);
/* Consumes merged 32x40 block rectangles, at most 80 rows high. */
bool render_damage_next(int *cursor, int *x, int *y, int *width, int *height);
/* Width zero restores the full horizontal viewport; framebuffer stride stays unchanged. */
void render_region(int x, int width);
int render_region_left(void);
int render_region_right(void);
/* Release before starting network work; caches are optional and allocated lazily. */
void render_work_caches(bool enabled);
size_t render_work_cache_bytes(void);

/* Optional vignette alpha cache (TANK_W*TANK_H bytes): with a scene cache the
 * porthole vignette is baked into the scene and re-applied per frame only on
 * dynamic pixels; this LUT removes the per-pixel float math. NULL = compute. */
void render_set_vignette_cache(uint8_t *buf);

/* Scene prefetch support: render_scene_buf returns the built scene cache (or
 * NULL) and its epoch (bumped on lighting rebuilds). A platform that copies
 * the scene into fb ahead of time (e.g. by DMA) calls render_fb_primed; the
 * next render_tank into that fb at that epoch skips its own scene restore. */
const uint16_t *render_scene_buf(unsigned *epoch);

/* Dirty mask (RENDER_DIRTY_WORDS uint32): required with the scene cache -
 * marks the pixels drawn each frame so the vignette re-apply reads the mask,
 * not the scene. Without it the renderer falls back to full redraws. */
#define RENDER_DIRTY_WORDS (TANK_H * ((TANK_W + 31) / 32))
void render_set_dirty_mask(uint32_t *buf);
/* Word-only masks/palette may live in ESP32 32-bit instruction RAM. Set at startup. */
size_t render_mask_scratch_size(void);
void render_set_mask_scratch(uint32_t *buf);
void render_fb_primed(const uint16_t *fb, unsigned epoch);

/* Stats overlay for one selected fish: selection ring + a small card of
 * visual bars (drives + personality) and stage pips. No text, no digits —
 * the progression design's "simple and visual" stats view. Personality bars
 * are revealed only after the fish has shown that side of itself (milestone
 * bits): you learn your fish by watching. A MORE button sits at the foot
 * of the card (2026-09-16, Strato: "how do I get to the milestones and
 * settings screen? it's not very obvious") - the label is the only text on
 * it; a tap anywhere on the card, button or not, opens the milestones page.
 * fish_idx == RENDER_CARD_SNAIL (2026-09-16): the SNAIL's card instead - a
 * ring on the snail and a small centred card: the upright sprite at 2x and
 * how much algae it has grazed so far (tank_t.snail_grazed). The platforms
 * keep it in the same selection slot as a fish (a tap on the snail opens it,
 * a tap anywhere else dismisses it, no card cache). */
#define RENDER_CARD_SNAIL 99
/* fish_idx == RENDER_CARD_SHRIMP (2026-09-29): the shrimp school's card, the
 * snail's way - a ring round the school and a centred card: a shrimp at 4x,
 * how many, the pellets they have eaten, ten pips toward the next shrimp and
 * where that stands (N more / arrives in N min / full / too much algae). */
#define RENDER_CARD_SHRIMP 98
/* fish_idx == RENDER_CARD_URCHIN (2026-10-02): the urchin's card, the
 * snail's way - a ring on it and a centred card: the urchin at 3x, the grass
 * it has eaten (cm) and what it is up to (chewing / off to the tall grass /
 * resting). */
#define RENDER_CARD_URCHIN 97
/* fish_idx == RENDER_CARD_SEAHORSE (0.3.6): the seahorse's card - a ring on
 * it and a centred card: the seahorse at 2x, its days with the keeper, its
 * hitches, the trust it passed on and what it is doing. */
#define RENDER_CARD_SEAHORSE 96
void render_stats_card(const tank_t *t, int fish_idx, uint16_t *fb, int stride);
int render_creature_colors_hit(const tank_t *t, int selected, float x, float y);
int render_creature_card_hit(const tank_t *t, float x, float y);
void render_life_creature_open(int creature);
#define RENDER_CARD_NEXT_X 154
#define RENDER_CARD_NEXT_Y 8
#define RENDER_CARD_NEXT_W 44
#define RENDER_CARD_NEXT_H 40
bool render_stats_next_hit(const tank_t *t, int fish_idx, float x, float y);
/* Optional card cache (RENDER_CARD_W x RENDER_CARD_H uint16): with the scene
 * cache live, the card is redrawn at most 4x/s and blitted otherwise (~7 ms
 * -> ~1 ms per frame on the device). NULL = draw every frame. */
#define RENDER_CARD_X 14
#define RENDER_CARD_Y 8
#define RENDER_CARD_W 124
#define RENDER_CARD_H 258       /* 228 + the MORE button strip (2026-09-16) */
/* the card's tap hit box (touch ports): the card itself plus slop, most of
 * it BELOW the MORE button - fingers aiming at a button by the foot land
 * low and wide (Strato, 2026-09-16: "I'm not tapping it reliably"). Since
 * the toolbox (2026-10-01) the slop below is the gap down to it: 56 px of
 * it was for the panel's stretched report, which the calibration (09-30)
 * took out. RENDER_CARD_HIT(x, y) is the test. */
#define RENDER_TOOLS_GAP 14
#define RENDER_CARD_HIT_BELOW RENDER_TOOLS_GAP
#define RENDER_CARD_HIT_SIDE  12
#define RENDER_CARD_HIT_X(x) render_page_x(x)
#define RENDER_CARD_HIT_Y(y) render_page_y(y)
#define RENDER_CARD_HIT(x, y) (RENDER_CARD_HIT_X(x) >= RENDER_CARD_X - RENDER_CARD_HIT_SIDE && RENDER_CARD_HIT_X(x) < RENDER_CARD_X + RENDER_CARD_W + RENDER_CARD_HIT_SIDE && \
                               RENDER_CARD_HIT_Y(y) >= RENDER_CARD_Y && RENDER_CARD_HIT_Y(y) < RENDER_CARD_Y + RENDER_CARD_H + RENDER_CARD_HIT_BELOW)
void render_set_card_cache(uint16_t *buf);
/* The TOOLBOX (2026-10-01): under a fish's card, a little apart from it so
 * MORE keeps its own ground, a box in the card's dress with two big buttons
 * - the SPONGE and the SCISSORS (tank.h TOOL_*). render_stats_card draws it
 * with a fish's card; the one in hand is lit (a white double edge on a
 * lighter fill - brightness, not hue). render_tools_hit(x, y) is the tap
 * test while a fish's card is up: TOOL_SPONGE / TOOL_SCISSORS, or -1 (the
 * box's half, from its top edge down to the bezel, plus side slop). */
#define RENDER_TOOLS_X RENDER_CARD_X
#define RENDER_TOOLS_Y (RENDER_CARD_Y + RENDER_CARD_H + RENDER_TOOLS_GAP)
#define RENDER_TOOLS_W RENDER_CARD_W
#define RENDER_TOOLS_H 70
int  render_tools_hit(float x, float y);
/* the LOCK (0.3.5): a small box of its own just right of the toolbox, the
 * padlock open (bare fingers wipe and cut, as before) or shut (only the
 * tools do: tank_lock_tools). Shut is lit like a tool in hand - a white
 * double edge on a lighter fill - and the shackle's shape says it too,
 * never the hue alone. render_lock_hit is its tap test while a fish's card
 * is up: its column, from the toolbox's top edge down to the bezel. */
#define RENDER_LOCK_X (RENDER_TOOLS_X + RENDER_TOOLS_W + RENDER_CARD_HIT_SIDE)
#define RENDER_LOCK_W 44
#define RENDER_LOCK_H 44
#define RENDER_LOCK_Y (RENDER_TOOLS_Y + (RENDER_TOOLS_H - RENDER_LOCK_H) / 2)
bool render_lock_hit(float x, float y);
/* With a tool in hand and no fish card up, a chip at the top left says so:
 * the tool and DONE. A tap on it (render_tool_chip_hit) puts the tool back
 * in the box. render_tool_chip draws nothing with TOOL_HAND, when
 * fish_idx is a fish (its card shows the box instead), or while a stroke is
 * on the glass (0.3.5: the film under it shows as it is wiped). */
void render_tool_chip(const tank_t *t, int fish_idx, uint16_t *fb, int stride);
bool render_tool_chip_hit(const tank_t *t, float x, float y);

/* an announcement over the live tank (notice.h: a milestone the moment it
 * is earned, a stage reached, lights out), in the milestones page's modal
 * style; frac_left (1 -> 0) is its remaining time, drawn as a thin bar */
void render_notice(const tank_t *t, uint16_t *fb, int stride, int kind, int fish, uint32_t bit, float frac_left);

/* Milestones page (separate screen, never on the tank; 2026-09-13 redesign
 * on Strato's pixel-art badges): one row per fish - its sprite at its real
 * size, its name, a growth strip fry -> elder - and six event badges; the
 * tank's row below with the population strip and the tank badges - six a
 * page; the shrimp school's seventh (once there are shrimp) turns the row
 * into pages behind a small arrow at its right end (2026-09-30). A locked
 * badge is the same picture as a grey silhouette; one earned since the
 * keeper last closed the page wears a ring. While the tank can still grow,
 * a NEW FRY row sits under the last fish (2026-09-14): the fry-to-be as a
 * silhouette, a tick per gate, and the next arrival's gates as badges
 * (progression_next_fry) - lit once met, a filling bar under each still
 * owed; a tap on a gate says what to do and where it stands, with a HOW?
 * button that flips to a tip page (progression_fry_tip: how the keeper
 * moves that gate); a tap on the name gives the tally. render_milestones_tap maps a tap: a badge, a name
 * or a strip opens a small detail modal (the art at 2x, a title, the
 * words). Arrow buttons at the modal's top corners (2026-09-16) step to the
 * previous / next of its group without leaving it - a fish's six badges,
 * the tank's (across its pages), the fry checklist's gates, or, from a fish's name, the
 * fish themselves (wrapping; a group of one shows none). Any other tap
 * closes the modal. A CLOSE button at the
 * bottom right leaves the page; a SETTINGS button at the bottom left
 * leaves it for the settings page; both the settings page's and the shop's
 * CLOSE bring the milestones page BACK (the platforms do that). */
void render_milestones(const tank_t *t, uint16_t *fb, int stride);
/* a tap on the page (2026-09-13, Strato: with this much to tap, a stray tap
 * must not drop the whole page): MS_TAP_CLOSE = the CLOSE button, bottom
 * right - the ONLY way out by touch (caller closes the page, then
 * progression_ack_milestones + render_milestones_leave); MS_TAP_KEPT = a
 * badge / name / strip opened the detail modal, or the modal was up and
 * this tap closed it; MS_TAP_NONE = nothing here (the caller may try the
 * brightness row). */
enum { MS_TAP_NONE = 0, MS_TAP_KEPT = 1, MS_TAP_CLOSE = 2, MS_TAP_SETTINGS = 3,   /* SETTINGS: the button bottom left (2026-09-15) opens the settings page */
       MS_TAP_SHOP = 4,                                                            /* the sand dollar left of the TANK row opens the shop */
       MS_TAP_JOURNAL = 5, MS_TAP_BANK = 6, MS_TAP_GAMES = 7, MS_TAP_LIFE = 8, MS_TAP_RENAME = 16, MS_TAP_SELL = 32, MS_TAP_CUSTOM = 64 };
void render_games_open(void);
void render_games_tick(float dt);
bool render_games_tap(tank_t *t, float x, float y);
void render_games(const tank_t *t, uint16_t *fb, int stride);
void render_bank_open(void);
void render_bank(const tank_t *t, uint16_t *fb, int stride);
int render_bank_tap(tank_t *t, float x, float y);
#define BANK_ROW_Y 94
#define BANK_ROW_DY 46
#define BANK_PER_PAGE 4                   /* + the fish's index */
void render_journal_open(void);
void render_journal(const tank_t *t, uint16_t *fb, int stride);
int render_journal_tap(tank_t *t, float x, float y); /* 1 closes; 0 keeps page */
#define JOURNAL_ENTRY_Y 348
void render_life_open(void);
void render_life(const tank_t *t, uint16_t *fb, int stride);
enum { HOME_NONE, HOME_CLOSE, HOME_BANK, HOME_SHOP, HOME_JOURNAL, HOME_GAMES, HOME_SETTINGS, HOME_TANKS, HOME_KEEPER };
int render_life_tap(tank_t *t, float x, float y);
#define JOURNAL_TAB_Y 312
#define JOURNAL_GOAL_Y 94
#define JOURNAL_GOAL_DY 64
/* MS_TAP_RENAME + fish: the card's RENAME - the caller leaves the page (ack +
 * leave, as for CLOSE) and opens setup_begin_rename; when that flow ends
 * (setup_take_renamed) it brings the page back with render_milestones_show_fish.
 * MS_TAP_SELL + fish: the card's SELL, confirmed (its second tap) - the
 * caller calls progression_sell_fish; the page stays up, a row shorter. */
int  render_milestones_tap(const tank_t *t, float x, float y);
void render_milestones_show_fish(const tank_t *t, int fish);   /* put a fish's card up (the page must be showing) */
/* where things are, in the FRAME's coordinates as a tap arrives - for the
 * tests and the director, so neither copies a board's layout: a fish's card
 * (false = none up) with the centres of RENAME and SELL; a point on a row's name */
bool render_milestones_card(const tank_t *t, int *fish, int *rename_x, int *sell_x, int *btn_y);
bool render_milestones_arrow(const tank_t *t, bool right, int *x, int *y);   /* the modal's left / right arrow (false = no modal up) */
void render_milestones_row(int row, int *name_x, int *y);
/* a sideways swipe on the milestones page (release - press dx): along the
 * TANK row, with more badges than one row holds, it turns the row's page
 * (2026-09-30). True = it did (or was a page swipe at the row's end);
 * false = not a page swipe. */
bool render_milestones_swipe(const tank_t *t, float x, float y, float dx);
void render_milestones_leave(void);

/* The shop (2026-09-15): the sand dollar page. The balance at the top, one
 * row per item (SD_ITEMS: the art, the name, the price, UNLOCK / IN TANK), a
 * HOW TO EARN button bottom left (a modal listing the sources) and CLOSE
 * bottom right. A tap on a row opens the item's modal - the art at 2x, the
 * words, the price, an UNLOCK button; render_shop_tap returns SHOP_TAP_BUY +
 * item when that button is tapped (the caller calls progression_buy; a short
 * balance was already a dim button), SHOP_TAP_CLOSE for the way out (back
 * to the milestones page, 2026-09-16),
 * SHOP_TAP_KEPT when a modal opened or closed. Page state is render-local;
 * render_shop_leave clears it when the page closes. */
enum { SHOP_TAP_NONE = 0, SHOP_TAP_KEPT = 1, SHOP_TAP_CLOSE = 2, SHOP_TAP_TANKS = 3, SHOP_TAP_BUY = 16, SHOP_TAP_MOVE = 64, SHOP_TAP_SELL = 128,
       SHOP_TAP_FRY = 256 };   /* Non-overlapping ranges for all 32 stable item IDs. */
void render_aquariums_open(const tank_t *t);
void render_aquariums(const tank_t *t, uint16_t *fb, int stride);
/* 1 closes to shop, 2 means a different tank was loaded. */
int render_aquariums_tap(tank_t *t, float x, float y);
/* THE FRY (0.3.5, progression_fry_*): once a fish has been sold the shop's
 * first row is FRY - the first fry still for sale at its left, the price,
 * CHOOSE (SOLD OUT with all three bought, FULL with no slot). Its modal
 * shows the three side by side, a bought one a blank; a tap picks one (lit:
 * a white double edge), BUY returns SHOP_TAP_FRY + its place. Under them the
 * price, why BUY is dim (no room, too few dollars) and when a fresh three
 * comes. */
/* SHOP_TAP_SELL (2026-09-24): an owned placeable piece's modal has SELL next
 * to MOVE; the first tap arms it ("+30 OK?"), the second returns SELL + item
 * and the platform calls progression_sell. Test SELL before MOVE before BUY. */
/* SHOP_TAP_MOVE (2026-09-16): an owned, placeable item's modal carries a MOVE
 * button - the platform closes the shop and opens setup.c's placement page
 * (setup_begin_place), the same page a purchase opens. */
void render_shop(const tank_t *t, uint16_t *fb, int stride);
void render_ornament_preview(uint16_t *fb, int stride, int item, int cx, int bottom);
int  render_shop_tap(const tank_t *t, float x, float y);
void render_shop_leave(void);
int  render_coral_cells(float growth);   /* the coral sprite's filled cells at a growth (the sim's selftest) */
int  render_cluster_cells(float growth); /* the reef cluster's, likewise */
/* the decor scratch (2026-09-24): the coral's and the cluster's tables in one
 * block the platform provides - PSRAM on the board (internal RAM is spoken
 * for); the sim leaves it unset and render calloc's it */
size_t render_decor_scratch_size(void);
/* the pumpkin lantern (0.3.6, SD_ITEM_PUMPKIN): its sprite's pixels, and
 * in *glow the ones redrawn every frame (the face + the flame) */
int    render_pumpkin_px(int *glow);   /* its pixels; *glow = the ones redrawn every frame */
void   render_set_pumpkin_hidden(bool hidden);   /* the frame-rate A/B (director `pumpkin off`): not drawn, no light */
bool   render_pumpkin_hidden(void);
void   render_set_decor_scratch(void *buf);
/* the sand dollar toast: dollars awarded during play (progression_sd_take_award)
 * show as a small pill top centre of the live tank, "+N" beside the coin,
 * for a few seconds; amounts that land while it is up add on. Call every
 * frame the live tank is showing (never over a page). */
void render_sd_toast(const tank_t *t, uint16_t *fb, int stride);

/* Reset confirm (2026-09-11): a modal panel over the live tank - "RESET
 * TANK?", what it costs, a NO and a YES button, and a bar draining toward
 * the timeout (frac 1 -> 0). The first text the renderer draws (a 5x7
 * pixel font, upper case). Drawn last, over the card / milestones page.
 * render_confirm_hit maps a tap in tank coordinates to a button (+1 YES,
 * -1 NO, 0 neither) so the device's touch port and the sim's mouse share
 * the geometry. */
#define RENDER_CONFIRM_X     56
#define RENDER_CONFIRM_Y     76
#define RENDER_CONFIRM_W     336
#define RENDER_CONFIRM_H     216
#define RENDER_CONFIRM_BTN_W 132
#define RENDER_CONFIRM_BTN_H 56
#define RENDER_CONFIRM_BTN_Y (RENDER_CONFIRM_Y + 112)
#define RENDER_CONFIRM_NO_X  (RENDER_CONFIRM_X + 24)
#define RENDER_CONFIRM_YES_X (RENDER_CONFIRM_X + RENDER_CONFIRM_W - 24 - RENDER_CONFIRM_BTN_W)
void render_confirm_reset(uint16_t *fb, int stride, float frac);
int  render_confirm_hit(float x, float y);

/* Feed every frame's touch state to classify taps and apply gameplay settings.
 * Brightness and SD operations are handed back to the platform. */
enum { SET_TAP_NONE = 0, SET_TAP_CLOSE = 1, SET_TAP_BRIGHT = 2, SET_TAP_LIGHT = 4, SET_TAP_IDLE = 5,
       SET_TAP_UPDATES = 6, SET_TAP_FEED = 8,
       SET_TAP_BACKUP = 10, SET_TAP_RESTORE = 11, SET_TAP_SAND = 12, SET_TAP_WATER = 13, SET_TAP_TOUCH = 14, SET_TAP_WIPE = 15, SET_TAP_WIFI = 16, SET_TAP_CLOCK_SHOW = 17, SET_TAP_BRIGHT_PREVIEW = 18 };
void render_backup_status(const char *message, bool arm_restore);
void render_settings(const tank_t *t, uint16_t *fb, int stride, int bright_pct);
int  render_settings_tap(float x, float y, int *value);
int  render_settings_touch(tank_t *t, float x, float y, bool down, int *value);

/* UI primitives (2026-09-13) for panels built outside this file (the first-
 * run setup in common/setup.c): the confirm prompt's pixel font, flat rects
 * and buttons, and a fish drawn on its own for a preview. All ignore the
 * night dim, like the card, and draw AFTER render_tank (nothing re-vignettes
 * them). Text is upper case + digits + a little punctuation; `scale` is the
 * pixel size of one font dot (2 = caption, 3 = button). */
int  render_text_w(const char *s, int scale);
void render_text(uint16_t *fb, int stride, int x, int y, int scale, uint32_t rgb, const char *s);
/* Page text has a minimum 5x7 framebuffer glyph; wrapping uses 16 page units per line. */
int render_text_wrap(uint16_t *fb, int stride, int x, int y, int width, uint32_t rgb, const char *s);
void render_rect(uint16_t *fb, int stride, int x, int y, int w, int h, uint32_t rgb);
void render_rect_blend(uint16_t *fb, int stride, int x, int y, int w, int h, uint32_t rgb, int alpha);   /* alpha 0..255 */
void render_rect_edge(uint16_t *fb, int stride, int x, int y, int w, int h, uint32_t rgb);
void render_ring(uint16_t *fb, int stride, float cx, float cy, float r, uint32_t rgb);   /* the card's selection ring */
void render_button(uint16_t *fb, int stride, int x, int y, int w, int h, uint32_t fill, uint32_t edge, const char *label, int scale);
/* one 5x7 glyph from any table (rows: 5 bits, high bit left) at scale - the case-sensitive font in update.c draws with it */
void render_glyph(uint16_t *fb, int stride, int x, int y, int scale, uint32_t rgb, const uint8_t *rows);
/* an adult fish facing right at (x,y), body length ~42 x size px, tail
 * swimming on `clock` - the setup's live preview of a colour choice */
void render_fish_preview(uint16_t *fb, int stride, float x, float y, float size,
                         uint32_t body, uint32_t fin, uint32_t accent, float clock);
/* the same, but AS THE FISH IS: its own stage (a fry shows no markings yet,
 * an elder its long tail), calm and fed - the birth flow's portrait */
void render_fish_portrait(uint16_t *fb, int stride, float x, float y, float size, const fish_t *who, float clock);
void render_creature_portrait(uint16_t *fb, int stride, float x, float y, int creature, uint8_t colors, float clock);

/* ---- the pages' layouts (2026-10-01). These lived in render.c; they are here
 * so the sim's selftests tap where the layout says on every board instead of
 * carrying the rectangle's pixels. PAGE coordinates unless a line says the
 * frame's; render.c has the words that go with each page. ---- */
/* the snail's and the shrimp school's cards: centered on the glass (the frame's coordinates) */
#define SNAIL_CARD_W 336
#define SNAIL_CARD_H 286
#define SHRIMP_CARD_W 336
#define SHRIMP_CARD_H 286
#define URCHIN_CARD_W 336
#define URCHIN_CARD_H 286
#define SEAHORSE_CARD_W 336
#define SEAHORSE_CARD_H 286
/* the milestones page (the bowl has its own rows and buttons: render.c) */
#define MSP_ROW_Y0    4
#define MSP_ROW_H     40
#define MSP_TANK_Y    254
#define MSP_ROW_MID   20
#define MSP_ROW_STRIP 30
#define MSP_ROW_BADGE 4
#define MSP_ROW_BAR   38
#define MSP_BADGE_X0  176
#define MSP_BADGE_DX  40
#define MSP_ICON      32
#define MSP_FISH_X    52
#define MSP_CLOSE_W   92
#define MSP_CLOSE_H   30
#define MSP_SET_W     116
#define MSP_CLOSE_X   324               /* the CLOSE button, bottom right, inside the bezel curve; clear of the brightness row's number */
#define MSP_CLOSE_Y   312
#define MSP_SET_X     32                /* the SETTINGS button, bottom left, where the brightness row was */
#define MSP_SET_Y     MSP_CLOSE_Y
#define SHP_CLOSE_X   MSP_CLOSE_X
#define SET_CLOSE_X   340
#define SET_CLOSE_Y   4
#define SET_CLOSE_W   104
#define SET_CLOSE_H   30
#define MSP_SD_X      36                /* the sand dollar on the TANK row (the shop), centred like the fish portraits */
#define MSP_UPG_X     178               /* the UPGRADES button, centred between SETTINGS and CLOSE: the shop too (Strato, 2026-09-15) */
#define MSP_UPG_W     116
#define MSP_MODAL_X   56
#define MSP_MODAL_Y   100
#define MSP_MODAL_W   336
#define MSP_MODAL_H   156
#define MSP_PER_ROW   6
/* the arrow's column: right of the sixth badge's slop, out to the glass
                                                                    (the bowl: its "new" ring and the page pips inside the circle) */
#define MSP_TPG_X     412
#define MSP_ARROW_W   40             /* the arrow buttons, inset at the modal's top corners */
#define MSP_ARROW_H   32
#define MSP_ARROW_IN  10
/* the hit box: the corner's whole width in from each side, 12 above, 24 below
                                        (a miss closes the modal, so the box is wide) */
#define MSP_ARROW_HIT 100
/* a gate's modal is taller (two sentence lines, progress, the HOW? button)
   so it sits higher than the badge modal, clear of the CLOSE button */
#define MSP_FRY_MODAL_Y 60
#define MSP_HOW_W 100                 /* CLOSE-sized (Strato hit the 76 x 26 one a third of the time) */
#define MSP_HOW_H 32
/* its hit box: wide and deep. Fingers on this panel land low and wide of
   where they feel, and a miss here costs the modal (any other tap closes
   it), so the box runs 36 px past each side, 12 above and 28 below - the
   whole foot of the panel, down to its edge. */
#define MSP_HOW_SLOP_X 36
#define MSP_HOW_SLOP_UP 12
#define MSP_HOW_SLOP_DN 28
/* the shop */
#define SHP_COIN_X    32
#define SHP_HEAD_X    112
#define SHP_EARN_X    32
#define SHP_COIN_Y    10
#define SHP_ROW_Y0    98
#define SHP_ROW_DY    56
#define SHP_ROW_ICON  32
#define SHP_BTN_X     300
#define SHP_BTN_W     116
#define SHP_BTN_H     32
#define SHP_EARN_W    150
#define SHP_MODAL_X   48             /* wider than the milestones modal (56 / 336): an item's second line runs to 28 chars = 334 px */
#define SHP_MODAL_W   352
#define SHP_MODAL_Y   48
#define SHP_MODAL_H   244
#define SHP_EARN_MODAL_Y 40
/* the caption under the rows sits SHP_BTN_H + 12 under the last row (the third row, the castle, 2026-09-16: it used to be fixed at 224 / 244 and the castle's row ran into it) */
#define SHP_EARN_MODAL_H 248        /* seven lines since the seahorse's snack (0.3.6) */
#define SHP_TWO_GAP 16               /* MOVE and SELL side by side in the modal */
#define SHP_TWO_X0  (SHP_MODAL_X + (SHP_MODAL_W - 2 * MSP_HOW_W - SHP_TWO_GAP) / 2)
#define SHP_TWO_X1  (SHP_TWO_X0 + MSP_HOW_W + SHP_TWO_GAP)
#define SHP_PER_PAGE 4
#define SHP_PAGES    ((SD_ITEM_COUNT + SHP_PER_PAGE - 1) / SHP_PER_PAGE)   /* without the FRY row (render.c: shp_pages) */
#define SHP_FRY_BOX_W  114            /* full-size variety captions in all three boxes */
#define SHP_FRY_BOX_H  94
#define SHP_FRY_BOX_GAP 2
#define SHP_FRY_BOX_X(k) (SHP_MODAL_X + (SHP_MODAL_W - 3 * SHP_FRY_BOX_W - 2 * SHP_FRY_BOX_GAP) / 2 + (k) * (SHP_FRY_BOX_W + SHP_FRY_BOX_GAP))
#define SHP_FRY_BOX_Y  (SHP_MODAL_Y + 44)
#define SHP_ARROW_W  36
#define SHP_ARROW_H  32
#define SHP_ARROW_Y  (SHP_COIN_Y + 16)
#define SHP_ARROW_X1 (PAGE_W - 28 - SHP_ARROW_W)
#define SHP_ARROW_X0 (SHP_ARROW_X1 - SHP_ARROW_W - 8)     /* previous */
/* the settings page */
#define SET_TITLE_Y   14
#define SET_ROW1_Y    58             /* BRIGHTNESS */
#define SET_SLIDER_X  48
#define SET_SLIDER_W  352
#define SET_SLIDER_Y  84
#define SET_ROW2_Y    108            /* SD backup / restore */
#define SET_NOTE_Y    140
#define SET_ROW3_Y    176            /* AUTO FEED */
#define SET_ROW4_Y    222            /* SAND */
#define SET_ROW5_Y    268            /* WATER */
#define SET_FOOT_Y    MSP_CLOSE_Y    /* Updates and touch calibration */
#define SET_LABEL_X   32
#define SET_WIPE_X    4
#define SET_WIPE_Y    SET_CLOSE_Y
#define SET_WIPE_W    104
#define SET_WIPE_H    SET_CLOSE_H
/* first segment (the watch and the bowl: the third one inside the glass -
                                                                    the bowl's circle is narrowest at the BRIGHTNESS row) */
#define SET_SEG_X     190
#define SET_SEG_W     76
#define SET_SEG_DX    82
#define SET_SEG_H     40
#define SET_SEG_Y(row) ((row) - 10)  /* the segment sits on the label's line */
#define SET_COLOR_W   396
#define SET_UPD_X     32
#define SET_UPD_W     112
#define SET_TOUCH_W   240
#define SET_TOUCH_X   (SET_LABEL_X + SET_COLOR_W - SET_TOUCH_W)

#endif
