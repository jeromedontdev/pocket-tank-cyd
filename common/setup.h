/* setup.h — first-run setup: the welcome and a name for
 * each of the two fry, then the care tips - for a fresh install and after a
 * reset. Platform-agnostic: the state machine, the gestures, the hit
 * geometry and the drawing all live here (the reset prompt kept its state in
 * each platform's touch code; a five-page flow would have been written
 * twice), built on render.h's UI primitives over the LIVE tank - the fish
 * keep swimming behind the pages. Historical color pages are skipped in all
 * modes; resident looks come from purchased stock and inheritance.
 *
 * The platforms do three things: setup_begin when progression says the flow
 * is pending (boot, a reset YES, the director's cue); setup_touch every
 * frame with the finger's position and state while it is up (taps are
 * classified here); and render_setup after
 * render_tank while setup_active. Every other gesture is swallowed while it
 * is up. No timeout: it waits for the keeper.
 *
 * PLACE THE BUBBLES (2026-09-13, Strato's idea): right after the welcome,
 * over the live tank - a finger anywhere on the water drags the bubble
 * column to that x (tank_set_bubble_x keeps it clear of the grass corner the fish rest by); a translucent stripe marks the column, the airstone at its foot
 * moves with it, the bubbles already rising shift as one.
 *
 * Naming (QWERTY, matching weather and network search): the tank stays in
 * view with the fish ringed behind a compact keyboard. The shared keyboard
 * draws and hit-tests staggered letter rows, with space and delete; aquarium
 * names additionally expose digits and a hyphen. Keys append at the end,
 * delete removes one trailing character, and the length is bounded by the
 * name field. Existing characters are retained unless the keeper edits them.
 *
 * THE BIRTH FLOW (2026-09-14, Strato: an arrival "will be an exciting event
 * for the player"): when progression says a fry is owed its welcome
 * (progression_newborn), setup_poll_birth opens three pages over the live
 * tank - A NEW FRY! (the announcement: the fry ringed wherever it hatched,
 * its parents named), NAME THE NEW FRY (the QWERTY keyboard, as in the first
 * run) and the family page (a panel: the fry's portrait on its own, as it
 * is now; whose body and whose markings it wears; its boldness and
 * sociability on bars between its parents' marks). No colour picker - a
 * fry's look is its family's (tank_add_fish). DONE clears the debt and
 * saves the name; a cancel (director `setup off`, sim S) leaves it owed, so
 * it returns at the next boot. */
/* PLACE A PIECE (2026-09-16, Strato: a bought plant or decoration "should
 * allow the player to place the piece wherever they like ... similar to the
 * bubble column placement experience", plus a z-layer): one page over the
 * live tank, opened by the platform right after a purchase (and by the
 * holding a decoration already in the tank). A finger on the
 * water drags the piece to that x (tank_decor_set clamps it inside the
 * window); a tap on overlapping placed decorations cycles through only
 * owned pieces and copies without moving them. Selecting another piece
 * drops the current preview back to its starting layout. PREV/NEXT also
 * cycle every placed original and extra copy, including fixed scenery,
 * skipping absent pieces. The selected
 * footprint is outlined, and a marker stays at its starting position while
 * it is dragged. The page identifies the placed original or extra copy;
 * REMOVE returns the selected decoration to storage without a refund,
 * DONE saves; there is no on-screen CANCEL (the serial `setup off` still
 * restores the original layout). MIRROR, centred over the depth bar, flips sprite pieces. */
#ifndef POCKET_TANK_SETUP_H
#define POCKET_TANK_SETUP_H
#include "tank.h"
#include "render.h"            /* PAGE_*: the page the layout below is on */
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

void setup_begin(tank_t *t);             /* page 1 (needs 2 fish; fewer = done at once) */
void setup_begin_birth(tank_t *t, int slot);   /* the birth flow for the fry in `slot` */
/* platforms, every frame while no prompt owns the glass: opens the birth
 * flow once per arrival owed (never over a flow already up); returns the
 * slot it opened for, or -1 */
int  setup_poll_birth(tank_t *t);
/* a fry's welcome is up, or owed and about to open (it is born mid-frame,
 * the flow opens after): the notices wait for it (2026-09-29). A welcome the
 * director dropped is not due - it returns at the next boot. */
bool setup_birth_due(void);
void setup_begin_place(tank_t *t, int item);   /* the placement page for SD item `item` (placeable ones only) */
typedef struct {
    int item;
    float x;
    uint8_t z, scheme, rank[DECOR_SLOTS];
    uint32_t rgb;
} setup_place_snapshot_t;
const setup_place_snapshot_t *setup_place_original(void);
void setup_begin_extra_place(tank_t *t, int copy);
bool setup_begin_place_at(tank_t *t, float x, float y);
/* RENAME A FISH (2026-10-01, Strato: "when you tap a fish and see its card
 * there should be a change name button"): the shared QWERTY keyboard, one page
 * over the live tank with the fish ringed on its stage - the name page of
 * the first run and the birth flow, CANCEL / DONE on top. DONE keeps the
 * name and saves it (nothing left = the preset's again); CANCEL, or a flow
 * dropped from outside, puts the old one back. The milestones page's fish
 * card opens it (MS_TAP_RENAME); when it closes, setup_take_renamed gives
 * the platform the fish once (else -1) so it can put that card back up. */
void setup_begin_rename(tank_t *t, int fish);
/* Compatibility no-op: resident looks are purchased or inherited in every mode. */
void setup_begin_customize(tank_t *t, int fish);
bool setup_is_rename(void);
int  setup_take_renamed(void);
bool setup_active(void);
bool setup_is_birth(void);               /* the birth flow, not the first run */
bool setup_is_place(void);               /* the placement page */
/* the placement page's look, hashed (0 = no placement page): the tank under it repaints only
   where it changes while this holds; animated_rows: the PAGE rows it repaints every frame */
uint32_t setup_place_signature(const tank_t *t);
bool setup_place_animated_rect(int *x0, int *y0, int *x1, int *y1);
int  setup_item(void);                   /* the placement page's SD item (-1 = none) */
int  setup_fish(void);                   /* the fish the current page is about (-1 = none) */
void setup_cancel(tank_t *t);            /* drop the pages; the save still says pending */
int  setup_page(void);                   /* SETUP_PG_* while active */
/* the finger, every frame (or poll): x,y in tank coordinates, down = touching.
 * A press then release on one element taps it.
 * Placement water taps cycle overlapping owned copies; horizontal drags move
 * only the selected copy. */
void setup_touch(tank_t *t, float x, float y, bool down);
/* the pieces, for tests and the touch log: the element under (x,y) on the
 * current page (0 = none) and a tap on it */
int  setup_hit(float x, float y);
void setup_activate(tank_t *t, int id);
const char *setup_hit_name(int id);      /* "NEXT", a keyboard character, "body 3"... */
int  setup_slot(void);                   /* compatibility: name pages no longer select a slot */
/* Shared QWERTY name input. Coordinates passed to hit are page coordinates;
 * draw uses the same origin and width. Keys return uppercase letters, digits,
 * '-' (extended layout), space, or '\b' for delete; zero means no key. */
void setup_name_keyboard_draw(uint16_t *fb, int stride, int x, int y, int width,
                              const char *name, int max_chars, bool extended,
                              uint32_t fill, uint32_t edge, uint32_t text);
int  setup_name_keyboard_hit(float x, float y, int left, int top, int width, bool extended);
bool setup_name_keyboard_apply(char *name, size_t capacity, int key);
void render_setup(const tank_t *t, uint16_t *fb, int stride, float clock);

/* pages, in order */
enum { SETUP_PG_WELCOME, SETUP_PG_BUBBLES, SETUP_PG_NAME_A, SETUP_PG_LOOK_A, SETUP_PG_NAME_B, SETUP_PG_LOOK_B, SETUP_PG_CARE, SETUP_PG_N };
/* the birth flow's pages, in order (numbered past the first run's) */
enum { SETUP_PG_BORN = SETUP_PG_N, SETUP_PG_NAME_NEW, SETUP_PG_FAMILY, SETUP_PG_BIRTH_END };
#define SETUP_BIRTH_PAGES (SETUP_PG_BIRTH_END - SETUP_PG_BORN)
/* the placement page (its own one-page flow, numbered past the birth flow's) */
enum { SETUP_PG_PLACE = SETUP_PG_BIRTH_END };
/* the rename page (one page too) */
enum { SETUP_PG_RENAME = SETUP_PG_PLACE + 1 };
enum { SETUP_PG_CUSTOM = SETUP_PG_RENAME + 1 };
enum { SETUP_PG_MODE = SETUP_PG_CUSTOM + 1 };
#define SETUP_CUSTOM_X 32
#define SETUP_CUSTOM_Y 166
#define SETUP_CUSTOM_W 384
#define SETUP_CUSTOM_H 36
#define SETUP_CUSTOM_DY 44
#define SETUP_HIT_CUSTOM0 200
#define SETUP_HIT_MODE0 220
/* element ids (setup_hit / setup_activate) */
#define SETUP_HIT_NEXT  1
#define SETUP_HIT_BACK  2
#define SETUP_HIT_REMOVE 5               /* Confirm removal from the tank into shared storage. */
#define SETUP_HIT_BCK   6                /* the placement page's order arrows (2026-10-17): one step back in the stack ... */
#define SETUP_HIT_FWD   7                /* ... and one step forward (tank_decor_shift) */
#define SETUP_HIT_MIRROR 8               /* the placement page's MIRROR toggle, centred over the depth bar (sprite pieces) */
#define SETUP_HIT_SUGGEST 9              /* the fish keyboard's SUGGEST key: a fresh name from the pool */
#define SETUP_HIT_PREV_PIECE 230
#define SETUP_HIT_NEXT_PIECE 231
#define SETUP_HIT_UP    3                /* retired letter-wheel identifier */
#define SETUP_HIT_DOWN  4                /* retired letter-wheel identifier */
#define SETUP_HIT_SLOT0 10               /* retired letter-wheel identifiers */
#define SETUP_HIT_BODY0 40               /* + swatch 0..LOOK_N-1 */
#define SETUP_HIT_Z0    60               /* + DECOR_Z_BACK..FRONT: the placement page's layer row */
#define SETUP_HIT_COLOR0 80              /* + 0..CORAL_N-1: the coral's colour swatches (its placement page) */

/* geometry (tank coordinates), shared by the drawing, the hit test and the
 * sim's selftest. The panelled pages (welcome, colours, care) sit inside the
 * bezel curve (x 32..416, y 16..342); the name page draws straight on the
 * tank. */
#define SETUP_X 32
#define SETUP_Y 16
#define SETUP_W 384
#define SETUP_H 326
#define SETUP_BTN_W 110
#define SETUP_BTN_H 42
#define SETUP_BTN_Y (SETUP_Y + SETUP_H - 12 - SETUP_BTN_H)   /* welcome / care: the foot */
#define SETUP_BACK_X (SETUP_X + 16)
#define SETUP_NEXT_X (SETUP_X + SETUP_W - 16 - SETUP_BTN_W)
#define SETUP_MID_X  (SETUP_X + (SETUP_W - SETUP_BTN_W) / 2)
#define SETUP_TOP_BTN_W 84                                 /* name / look: the top row */
#define SETUP_TOP_BTN_Y (SETUP_Y + 24)
#define SETUP_TOP_BACK_X (SETUP_X + 12)
#define SETUP_TOP_NEXT_X (SETUP_X + SETUP_W - 12 - SETUP_TOP_BTN_W)
#define SETUP_ORD_W      36                                 /* the order arrows, between REMOVE and DONE (the placement page) */
#define SETUP_ORD_X0     (PAGE_W / 2 - SETUP_ORD_W - 8)
#define SETUP_ORD_X1     (PAGE_W / 2 + 8)
/* shared name keyboard: staggered rows in the middle, large enough to hit
 * at either display scale. Aquarium naming enables its numeric/punctuation
 * row through setup_name_keyboard_draw/hit. */
#define SETUP_NAME_KB_X 24
#define SETUP_NAME_KB_Y 128
#define SETUP_NAME_KB_W 400
/* rename from the Fish menu owns the page: preview card on top, keyboard at the foot */
#define SETUP_RENAME_KB_Y 224
/* the colour page (third design too): no panel, the live FRY wears the pick
 * with a ring round it; one row of 8 body swatches, 42 x 60 px at a 46 px
 * pitch, a tall band around it so a low-landing finger still hits; the
 * accent is a "?" - a fry's markings come in as it grows */
#define SETUP_SW_N  LOOK_N
#define SETUP_SW_PX 46
#define SETUP_SW_W  42
#define SETUP_SW_H  60
#define SETUP_SW_X  ((PAGE_W - (SETUP_SW_N - 1) * SETUP_SW_PX - SETUP_SW_W) / 2)
#define SETUP_SW_Y  176
#define SETUP_ACC_Y 276
/* the family page (birth flow): the portrait ringed under the name, then
 * four rows - BODY / MARKINGS (a swatch, whose it is), BOLD / SOCIAL (a bar
 * of the fry's own, the parents' ticks in their body colours) - and a
 * PARENTS legend naming both in their tick colours; BACK + DONE at the foot */
#define SETUP_FAM_PORTRAIT_Y (SETUP_Y + 78)
#define SETUP_FAM_ROW_Y      (SETUP_Y + 128)
#define SETUP_FAM_ROW_DY     26
#define SETUP_FAM_LABEL_X    (SETUP_X + 28)
#define SETUP_FAM_VALUE_X    (SETUP_X + 150)
#define SETUP_FAM_BAR_W      200
#define SETUP_FAM_BAR_H      8
/* the placement page's DEPTH control (second design, 2026-09-16 - Strato:
 * "initially i thought 'back' button referred to menu navigation .. the
 * z-layer targeting and buttons are a bit ambiguous"): ONE outlined bar of
 * three joined segments under a DEPTH caption - the settings page's idiom,
 * a setting with three values, not three buttons - each segment a picture
 * tile (two small leaves and the keeper's first fish, drawn in that order:
 * the fish over the leaves, woven between them, behind them) with BEHIND /
 * AMONG / IN FRONT under it; a hint line under the bar says what to watch
 * for ("THE FISH SWIM THROUGH IT"). DONE stays alone top right in the go
 * teal, the bar in the calm ink, so "choose" and "finish" read apart. The
 * water below SETUP_PLACE_Y is the drag zone. */
#define SETUP_DEPTH_SEG_W  110
/* the plant's three segments; the castle's two (BEHIND / IN
                                                             * FRONT, Strato: no AMONG) are centred the same way - setup.c
                                                             * sizes the bar from tank_decor_z_count */
#define SETUP_DEPTH_W      (DECOR_Z_N * SETUP_DEPTH_SEG_W)
#define SETUP_DEPTH_X      ((PAGE_W - SETUP_DEPTH_W) / 2)
#define SETUP_MIRROR_Y     (SETUP_TOP_BTN_Y + SETUP_BTN_H + 2)   /* MIRROR, centred over the depth bar (no DEPTH caption) */
#define SETUP_MIRROR_H     24
#define SETUP_MIRROR_W     96
#define SETUP_MIRROR_X     ((PAGE_W - SETUP_MIRROR_W) / 2)
#define SETUP_DEPTH_Y      (SETUP_MIRROR_Y + SETUP_MIRROR_H + 6)
#define SETUP_DEPTH_H      70
#define SETUP_DEPTH_TILE_H 40
#define SETUP_DEPTH_HINT_Y (SETUP_DEPTH_Y + SETUP_DEPTH_H + 8)
#define SETUP_PLACE_Y      (SETUP_DEPTH_HINT_Y + 20)
/* the coral's placement page (2026-09-23) trades the hint line for a COLOR
 * row: CORAL_N swatches at the colour page's pitch, shorter (42 x 26), the
 * chosen one ringed; the coral on the floor below wears the pick at once.
 * The drag zone starts under the row. */
#define SETUP_COL_N   CORAL_N
#define SETUP_COL_PX  SETUP_SW_PX
#define SETUP_COL_W   SETUP_SW_W
#define SETUP_COL_H   26
#define SETUP_COL_X   ((PAGE_W - (SETUP_COL_N - 1) * SETUP_COL_PX - SETUP_COL_W) / 2)
#define SETUP_COL_Y   (SETUP_DEPTH_Y + SETUP_DEPTH_H + 22)
#define SETUP_PLACE_CORAL_Y (SETUP_COL_Y + SETUP_COL_H + 8)
/* the reef cluster's page (2026-09-24): a LOOK row of CLUSTER_SCHEME_N tiles
 * in the COLOR row's place, each three bands (the coral, the tubes, the
 * brain) over the look's name; SETUP_HIT_COLOR0 + i picks it */
#define SETUP_LOOK_N   CLUSTER_SCHEME_N
#define SETUP_LOOK_W   110
#define SETUP_LOOK_PX  118
#define SETUP_LOOK_H   36
#define SETUP_LOOK_X   ((PAGE_W - (SETUP_LOOK_N - 1) * SETUP_LOOK_PX - SETUP_LOOK_W) / 2)
#define SETUP_LOOK_Y   SETUP_COL_Y
#define SETUP_PLACE_CLUSTER_Y (SETUP_LOOK_Y + SETUP_LOOK_H + 8)
/* the stage: the clear spot each page leaves for the fish being edited
 * (tank_t.stage_*), top centre between the buttons */
#define SETUP_STAGE_X   (TANK_W / 2)                    /* the stage is in the TANK: the frame's coordinates, under the page's title */
#define SETUP_STAGE_NAME_Y render_frame_y(94)
#define SETUP_STAGE_LOOK_Y render_frame_y(112)
#endif
