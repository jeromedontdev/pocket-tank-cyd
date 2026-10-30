/* setup.c — first-run setup flow (see setup.h). */
#include "setup.h"
#include "tank_events.h"
#include "render.h"
#include "progression.h"
#include <string.h>
#include <math.h>
#include <stdio.h>

/* the reset prompt's palette: ink panel, teal edge, calm / lit buttons */
#define C_PANEL  0x04141a
#define C_EDGE   0x9fd8e2
#define C_INNER  0x1c2f36
#define C_DIM    0x2a3f45
#define C_KEY    0x0e2229
#define C_TEXT   0xffffff
#define C_CAPT   0x9fd8e2
#define C_GO     C_INNER
#define C_GO_E   C_EDGE
#define C_FILL   0x5f8a92      /* the family page's trait bars: a muted teal the parents' ticks stand out on */

static bool s_active;
static bool s_birth;           /* the birth flow (three pages about s_fish), not the first run */
static bool s_place;           /* the placement page (one page about s_item) */
static bool s_rename;          /* the rename page (one page about s_fish: CANCEL / DONE) */
static char s_old_name[FISH_NAME_MAX + 1];   /* ... the name it came in with (CANCEL puts it back) */
static uint32_t s_old_body, s_old_fin, s_old_accent;
static uint8_t s_old_variety, s_old_pattern;
static fish_t s_custom_draft;
static bool s_save_error;
static const char *s_place_error;
static setup_place_snapshot_t s_place_original;
static int s_extra = -1;
static inventory_extra_t s_place_extras_original[DECOR_EXTRA_CAP];
static float s_place_base_x[SD_ITEM_COUNT];
static uint8_t s_place_base_z[SD_ITEM_COUNT];
static float s_selected_original_x;
const setup_place_snapshot_t *setup_place_original(void) {
    return s_active && s_place && s_extra < 0 ? &s_place_original : NULL;
}
static float *place_x(tank_t *t, int item) {
    if (s_extra >= 0) return &t->inventory.extra[s_extra].x;
    if (SD_IS_NEW_DECOR(item)) return &t->content.x[item - SD_DECOR_FIRST];
    switch (item) {
    case 0: return &t->plant_x; case 2: return &t->castle_x;
    case 3: return &t->coral_x; case 4: return &t->cluster_x;
    case 8: return &t->pumpkin_x; case 9: return &t->chest_x;
    default: return tank_fixed_index(item) >= 0 ? &t->fixed.x[tank_fixed_index(item)] : &t->ornament_x[tank_ornament_index(item)];
    }
}
static int  s_renamed;         /* a rename page just closed: the fish's slot + 1, until the platform takes it */
static int  s_item;            /* placement: the SD item being placed */
static float placed_x(const tank_t *t) {
    return s_extra < 0 ? tank_decor_x(t, s_item) : t->inventory.extra[s_extra].x;
}
static int placed_z(const tank_t *t) {
    return s_extra < 0 ? tank_decor_z(t, s_item) : t->inventory.extra[s_extra].z;
}
static void placed_set(tank_t *t, float x, int z) {
    if (s_extra < 0) { tank_decor_set(t, s_item, x, z); return; }
    float half = tank_decor_half_w(s_item);
    float lo = TANK_FX0 + DECOR_MARGIN + half, hi = TANK_FX1 - DECOR_MARGIN - half;
    t->inventory.extra[s_extra].x = fmaxf(lo, fminf(hi, x));
    t->inventory.extra[s_extra].z = z == DECOR_Z_BACK ? DECOR_Z_BACK : DECOR_Z_FRONT;
}
typedef struct { int item, copy; } place_copy_t;
static bool placed_mirrored(const tank_t *t) {
    return s_extra < 0 ? tank_decor_mirrored(t, s_item) : (t->inventory.extra[s_extra].rank & DECOR_RANK_MIRROR) != 0;
}
static void placed_mirror_toggle(tank_t *t) {
    if (!tank_decor_mirrorable(s_item)) return;
    if (s_extra < 0) tank_decor_set_mirrored(t, s_item, !tank_decor_mirrored(t, s_item));
    else t->inventory.extra[s_extra].rank ^= DECOR_RANK_MIRROR;
}
static void place_restore_preview(tank_t *t) {
    s_extra = -1;
    for (int item = 0; item < SD_ITEM_COUNT; item++) {
        if (!tank_decor_movable(item)) continue;
        tank_decor_set(t, item, s_place_base_x[item], s_place_base_z[item]);
        *place_x(t, item) = s_place_base_x[item];
    }
    memcpy(t->inventory.extra, s_place_extras_original, sizeof s_place_extras_original);
    t->coral_rgb = s_place_original.rgb;
    t->cluster_scheme = s_place_original.scheme;
    memcpy(t->decor_rank, s_place_original.rank, sizeof t->decor_rank);
}
static float place_touch_top(const tank_t *t, int item) {
    float top = TANK_BOT - 16 - 40;
    if (item == 0) tank_veg_bed(t, 3, NULL, NULL, &top, NULL);
    if (item == 2) top = TANK_BOT - 16 - 146;
    if (item == 3) top = TANK_BOT - 14 + DECOR_SINK - 92;
    if (item == 4) top = TANK_BOT - 14 + DECOR_SINK - 120;
    if (item == 8) top = TANK_BOT - 14 + DECOR_SINK - 82;
    if (item >= 9) top = TANK_BOT - 14 + DECOR_SINK - tank_ornament_h(item);
    return top - 8;
}
static int place_candidates_at(const tank_t *t, float x, float y, place_copy_t *copies) {
    int count = 0;
    for (int item = 0; item < SD_ITEM_COUNT; item++) {
        if (!tank_decor_movable(item)) continue;
        float reach = tank_decor_half_w(item) + 10;
        if (tank_decor_surface(item) ? y > 48 : y < place_touch_top(t, item) || y > TANK_BOT - 8) continue;
        if (progression_item_owned(t, item) && fabsf(tank_decor_x(t, item) - x) <= reach)
            copies[count++] = (place_copy_t){ item, -1 };
        for (int copy = 0; copy < DECOR_EXTRA_CAP; copy++)
            if (t->inventory.extra[copy].item == item + 1 &&
                fabsf(t->inventory.extra[copy].x - x) <= reach)
                copies[count++] = (place_copy_t){ item, copy };
    }
    return count;
}
static int place_copy_count(const tank_t *t) {
    int count = progression_item_owned(t, s_item) ? 1 : 0;
    for (int i = 0; i < DECOR_EXTRA_CAP; i++)
        count += t->inventory.extra[i].item == s_item + 1;
    return count;
}
static int place_copy_number(const tank_t *t) {
    if (s_extra < 0) return 1;
    int number = progression_item_owned(t, s_item) ? 1 : 0;
    for (int i = 0; i < s_extra; i++)
        number += t->inventory.extra[i].item == s_item + 1;
    return number + 1;
}
static void place_select_copy(tank_t *t, int item, int copy) {
    s_item = item;
    s_extra = copy;
    s_selected_original_x = placed_x(t);
    if (copy < 0) {
        s_place_original.item = item;
        s_place_original.x = s_place_base_x[item];
        s_place_original.z = s_place_base_z[item];
    }
}
static bool place_cycle_copy_at(tank_t *t, float x, float y) {
    place_copy_t copies[SD_ITEM_COUNT + DECOR_EXTRA_CAP];
    int count = place_candidates_at(t, x, y, copies), current = -1;
    for (int i = 0; i < count; i++)
        if (copies[i].item == s_item && copies[i].copy == s_extra) { current = i; break; }
    if (count < 2) return false;
    place_copy_t next = copies[current < 0 ? 0 : (current + 1) % count];
    place_restore_preview(t);
    place_select_copy(t, next.item, next.copy);
    return true;
}
static void place_cycle_piece(tank_t *t, int direction) {
    place_copy_t copies[SD_ITEM_COUNT + DECOR_EXTRA_CAP];
    int count = 0, current = 0;
    for (int item = 0; item < SD_ITEM_COUNT; item++) {
        if (!tank_decor_movable(item)) continue;
        if (progression_item_owned(t, item))
            copies[count++] = (place_copy_t){item, -1};
        for (int copy = 0; copy < DECOR_EXTRA_CAP; copy++)
            if (t->inventory.extra[copy].item == item + 1)
                copies[count++] = (place_copy_t){item, copy};
    }
    if (!count) return;
    for (int i = 0; i < count; i++)
        if (copies[i].item == s_item && copies[i].copy == s_extra) current = i;
    place_copy_t next = copies[(current + count + direction) % count];
    place_restore_preview(t);
    place_select_copy(t, next.item, next.copy);
}
static int  s_fish;            /* birth flow: the newborn's slot */
static int  s_birth_offered = -1;   /* setup_poll_birth: the arrival already opened for (-1 = none since boot) */
static int  s_page;
static int s_mode_pick;
static char s_mode_error[80];
/* the finger (setup_touch) */
static bool  s_down;
static float s_px, s_py;       /* press point */
static bool  s_spun;           /* this press moved the bubble column or placement preview */
static bool  s_remove_armed;
static bool  s_place_dragging;

static int page_fish(void) { return s_birth || s_rename ? s_fish : s_place ? 0 : (s_page == SETUP_PG_NAME_A || s_page == SETUP_PG_LOOK_A) ? 0 : 1; }
static bool page_is_name(void) { return s_page == SETUP_PG_NAME_A || s_page == SETUP_PG_NAME_B || s_page == SETUP_PG_NAME_NEW || s_page == SETUP_PG_RENAME; }
static bool page_is_look(void) { return s_page == SETUP_PG_LOOK_A || s_page == SETUP_PG_LOOK_B || s_page == SETUP_PG_CUSTOM; }
static bool page_is_last(void) { return s_page == SETUP_PG_CARE || s_page == SETUP_PG_FAMILY; }
static bool page_one_button(void) { return s_page == SETUP_PG_WELCOME || s_page == SETUP_PG_BORN; }   /* a lone NEXT at the foot */
static int  first_page(void) { return s_rename ? SETUP_PG_RENAME : s_place ? SETUP_PG_PLACE : s_birth ? SETUP_PG_BORN : SETUP_PG_WELCOME; }
static bool nav_on_top(void) { return page_is_name() || page_is_look() || s_page == SETUP_PG_BUBBLES || s_page == SETUP_PG_PLACE; }
static int  place_y(void) { return s_item == 3 ? SETUP_PLACE_CORAL_Y : s_item == 4 ? SETUP_PLACE_CLUSTER_Y : SETUP_PLACE_Y; }   /* where the drag zone starts */
static const char *fish_name(const tank_t *t, int i) { return i >= 0 && i < t->n_fish ? t->fish[i].name : "?"; }
static void stage(tank_t *t);
static void finish_name(tank_t *t, int fish);

static uint32_t sig_mix(uint32_t h, const void *data, size_t size) {
    const unsigned char *p = data;
    while (size--) h = (h ^ *p++) * 16777619u;
    return h;
}
uint32_t setup_place_signature(const tank_t *t) {
    if (!s_active || s_page != SETUP_PG_PLACE) return 0;
    struct {
        int item, extra, z, total, number, owned, coral, cluster; float x, original_x;
        bool remove_armed, save_error, mirrored; const char *error;
        uint32_t fish_color, fish_accent; int fish_variety, fish_pattern, fish_stage, n_fish;
    } s;
    memset(&s, 0, sizeof s);
    s.item = s_item; s.extra = s_extra; s.z = placed_z(t); s.x = placed_x(t); s.original_x = s_selected_original_x;
    s.total = place_copy_count(t); s.number = place_copy_number(t);
    s.owned = s_extra >= 0 ? t->inventory.extra[s_extra].item == s_item + 1 : progression_item_owned(t, s_item);
    s.coral = (int)tank_coral_rgb(t); s.cluster = tank_cluster_scheme(t);
    s.remove_armed = s_remove_armed; s.save_error = s_save_error; s.error = s_place_error; s.mirrored = placed_mirrored(t);
    s.n_fish = t->n_fish;
    if (t->n_fish > 0) {
        const fish_t *f = &t->fish[0];
        s.fish_color = f->color; s.fish_accent = f->accent; s.fish_variety = f->variety; s.fish_pattern = f->pattern; s.fish_stage = f->stage;
    }
    uint32_t h = sig_mix(2166136261u, &s, sizeof s);
    h = sig_mix(h, t->decor_rank, sizeof t->decor_rank);
    h = sig_mix(h, t->inventory.extra, sizeof t->inventory.extra);
    return h ? h : 1;
}
bool setup_place_animated_rect(int *x0, int *y0, int *x1, int *y1) {
    if (!s_active || s_page != SETUP_PG_PLACE) return false;
    int bw = tank_decor_z_count(s_item) * SETUP_DEPTH_SEG_W;   /* the depth tiles' fish swim; their words stay still */
    *x0 = (PAGE_W - bw) / 2; *x1 = *x0 + bw;
    *y0 = SETUP_DEPTH_Y; *y1 = SETUP_DEPTH_Y + 4 + SETUP_DEPTH_TILE_H + 4;
    return true;
}
void setup_begin(tank_t *t) {
    if (t->n_fish < 2) { progression_setup_done(t); s_active = false; return; }   /* nothing to name */
    s_save_error = false;
    s_active = true; s_birth = false; s_place = false; s_rename = false; s_fish = -1; s_page = SETUP_PG_WELCOME; s_down = false;
    if (!progression_mode_chosen(t)) s_page = SETUP_PG_MODE;
    s_mode_pick = GAME_MODE_NORMAL; s_mode_error[0] = 0;
    tank_emit(TEV_WELCOME, -1);
}
void setup_begin_birth(tank_t *t, int slot) {
    if (slot < 0 || slot >= t->n_fish) { s_active = false; return; }
    s_save_error = false;
    s_active = true; s_birth = true; s_place = false; s_rename = false; s_fish = slot; s_page = SETUP_PG_BORN; s_down = false;
}
void setup_begin_rename(tank_t *t, int fish) {
    if (fish < 0 || fish >= t->n_fish || s_active) return;
    s_active = true; s_birth = false; s_place = false; s_rename = true; s_fish = fish; s_page = SETUP_PG_RENAME; s_down = false;
    s_renamed = 0;
    memcpy(s_old_name, t->fish[fish].name, sizeof s_old_name);
    s_old_body = t->fish[fish].color; s_old_fin = t->fish[fish].fin; s_old_accent = t->fish[fish].accent;
    s_old_variety = t->fish[fish].variety; s_old_pattern = t->fish[fish].pattern; s_save_error = false;
    stage(t);
}
void setup_begin_customize(tank_t *t, int fish) {
    (void)t; (void)fish;
}
bool setup_is_rename(void) { return s_active && s_rename; }
int  setup_take_renamed(void) { int f = s_renamed - 1; s_renamed = 0; return f; }
void setup_begin_place(tank_t *t, int item) {
    if (!tank_decor_movable(item) || s_active) return;
    s_extra = -1;
    s_place_original.item = item; s_place_original.x = *place_x(t, item);
    s_place_original.z = (uint8_t)tank_decor_z(t, item);
    if (tank_fixed_index(item) >= 0) s_place_original.z = t->fixed.z[tank_fixed_index(item)];   /* the stored byte (a float reads FRONT, stores BACK) */
    s_place_original.rgb = t->coral_rgb; s_place_original.scheme = t->cluster_scheme;
    memcpy(s_place_original.rank, t->decor_rank, sizeof t->decor_rank);
    memcpy(s_place_extras_original, t->inventory.extra, sizeof s_place_extras_original);
    memset(s_place_base_x, 0, sizeof s_place_base_x);
    memset(s_place_base_z, 0, sizeof s_place_base_z);
    for (int i = 0; i < SD_ITEM_COUNT; i++)
        if (tank_decor_movable(i)) {
            s_place_base_x[i] = *place_x(t, i);
            s_place_base_z[i] = (uint8_t)tank_decor_z(t, i);
        }
    s_save_error = false; s_place_error = NULL;
    s_active = true; s_birth = false; s_place = true; s_rename = false; s_item = item; s_fish = -1; s_page = SETUP_PG_PLACE; s_down = false;
    s_remove_armed = false;
    s_selected_original_x = placed_x(t);
    stage(t);
}
void setup_begin_extra_place(tank_t *t, int copy) {
    if (s_active || copy < 0 || copy >= DECOR_EXTRA_CAP || !t->inventory.extra[copy].item) return;
    setup_begin_place(t, t->inventory.extra[copy].item - 1);
    s_extra = copy;
    s_selected_original_x = placed_x(t);
}
bool setup_begin_place_at(tank_t *t, float x, float y) {
    if (s_active) return false;
    int item = tank_decor_hit(t, x, y);
    if (item >= 0) {
        setup_begin_place(t, item);
        return true;
    }
    place_copy_t copies[SD_ITEM_COUNT + DECOR_EXTRA_CAP];
    int count = place_candidates_at(t, x, y, copies);
    if (!count) return false;
    setup_begin_extra_place(t, copies[count - 1].copy);
    return setup_is_place();
}
int setup_poll_birth(tank_t *t) {
    int nb = progression_newborn();
    if (nb < 0) { s_birth_offered = -1; return -1; }    /* nothing owed: a later arrival in the same slot is new */
    if (s_active || nb == s_birth_offered) return -1;   /* a flow is up, or this one was offered (and dropped) already */
    if (nb >= t->n_fish || t->fish[nb].stage != STAGE_FRY) {
        progression_newborn_done(t);
        return -1;
    }
    s_birth_offered = nb;
    setup_begin_birth(t, nb);
    return nb;
}
bool setup_birth_due(void) {                               /* a welcome up, or owed and not yet offered */
    int nb = progression_newborn();
    return setup_is_birth() || (nb >= 0 && nb != s_birth_offered);
}
bool setup_active(void) { return s_active; }
bool setup_is_birth(void) { return s_active && s_birth; }
bool setup_is_place(void) { return s_active && s_place; }
int  setup_item(void)     { return s_active && s_place ? s_item : -1; }
int  setup_fish(void) { return s_active && (page_is_name() || page_is_look() || s_birth) ? page_fish() : -1; }
/* the rename page closes: DONE saves the current name (empty = the preset),
   anything else puts the old one back. Either
   way the platform is owed the way back to the fish's card (setup_take_renamed). */
static void rename_end(tank_t *t, bool keep) {
    if (s_fish >= 0 && s_fish < t->n_fish) {
        if (keep) {
            fish_t *f = &t->fish[s_fish];
            uint32_t body = f->color, fin = f->fin, accent = f->accent;
            uint32_t seen = t->journal.seen;
            uint8_t variety = f->variety, pattern = f->pattern;
            if (s_page == SETUP_PG_CUSTOM && progression_creative(t)) {
                f->color = s_custom_draft.color; f->fin = s_custom_draft.fin; f->accent = s_custom_draft.accent;
                f->variety = s_custom_draft.variety; f->pattern = s_custom_draft.pattern;
                t->journal.seen |= (1u << variety) | (1u << f->variety);
            } else finish_name(t, s_fish);
            if (!progression_save(t)) {
                f->color = body; f->fin = fin; f->accent = accent; f->variety = variety; f->pattern = pattern;
                t->journal.seen = seen;
                s_save_error = true; return;
            }
        } else {
            tank_set_name(t, s_fish, s_old_name);
            t->fish[s_fish].color = s_old_body; t->fish[s_fish].fin = s_old_fin; t->fish[s_fish].accent = s_old_accent;
            t->fish[s_fish].variety = s_old_variety; t->fish[s_fish].pattern = s_old_pattern;
        }
        s_renamed = s_fish + 1;
    }
    s_active = false; s_rename = false; stage(t);
}
static void stage(tank_t *t) {                          /* who is on stage, and where */
    if (s_active && (page_is_name() || page_is_look())) {
        t->stage_fish = (int8_t)page_fish(); t->stage_x = SETUP_STAGE_X;
        t->stage_y = page_is_name() ? SETUP_STAGE_NAME_Y : SETUP_STAGE_LOOK_Y;
    } else t->stage_fish = -1;
}
void setup_cancel(tank_t *t) {
    if (s_active && s_rename) { rename_end(t, false); s_renamed = 0; return; }   /* the old name back, no card to return to */
    if (s_active && s_place) {
        place_restore_preview(t);
    }
    s_active = false; stage(t);
}
int  setup_page(void)   { return s_page; }
int  setup_slot(void)   { return 0; }

const char *setup_hit_name(int id) {
    static char buf[12];
    if (id == SETUP_HIT_NEXT) return "NEXT";
    if (id == SETUP_HIT_BACK) return "BACK";
    if (id == '\b') return "DELETE";
    if (id == ' ') return "SPACE";
    if ((id >= 'A' && id <= 'Z') || (id >= '0' && id <= '9') || id == '-') {
        snprintf(buf, sizeof buf, "key %c", id); return buf;
    }
    if (id == SETUP_HIT_REMOVE) return "REMOVE";
    if (id == SETUP_HIT_PREV_PIECE) return "PREV PIECE";
    if (id == SETUP_HIT_NEXT_PIECE) return "NEXT PIECE";
    if (id == SETUP_HIT_MIRROR) return "MIRROR";
    if (id == SETUP_HIT_SUGGEST) return "SUGGEST";
    if (id == SETUP_HIT_BCK) return "SEND BACK";
    if (id == SETUP_HIT_FWD) return "BRING FORWARD";
    if (id == SETUP_HIT_UP)   return "up";
    if (id == SETUP_HIT_DOWN) return "down";
    if (id >= SETUP_HIT_SLOT0 && id < SETUP_HIT_SLOT0 + FISH_NAME_MAX) { snprintf(buf, sizeof buf, "slot %d", id - SETUP_HIT_SLOT0); return buf; }
    if (id >= SETUP_HIT_BODY0 && id < SETUP_HIT_BODY0 + LOOK_N) { snprintf(buf, sizeof buf, "body %d", id - SETUP_HIT_BODY0); return buf; }
    if (id >= SETUP_HIT_Z0 && id < SETUP_HIT_Z0 + DECOR_Z_N) return id == SETUP_HIT_Z0 ? "BEHIND" : id == SETUP_HIT_Z0 + 1 ? "AMONG" : "IN FRONT";
    return "nothing";
}

/* Keep typed spaces and existing printable characters intact. Only an empty
 * name takes a fresh suggestion from the name pool. */
static uint32_t s_suggestions;
static void finish_name(tank_t *t, int fish) {
    if (!t->fish[fish].name[0]) tank_give_name(t, fish, ++s_suggestions);
}

enum { NAME_KEY_H = 30, NAME_KEY_GAP = 4, NAME_KEY_PITCH = NAME_KEY_H + NAME_KEY_GAP };
static void keyboard_key(uint16_t *fb, int stride, int x, int y, int w, const char *label,
                         uint32_t fill, uint32_t edge) {
    render_button(fb, stride, x, y, w, NAME_KEY_H, fill, edge, label, 2);
}
static int keyboard_row_hit(float x, float y, int left, int top, int key_w, int count,
                            const char *keys) {
    if (y < top || y >= top + NAME_KEY_H) return 0;
    for (int i = 0; i < count; i++) {
        int kx = left + i * (key_w + NAME_KEY_GAP);
        if (x >= kx && x < kx + key_w) return (unsigned char)keys[i];
    }
    return 0;
}
void setup_name_keyboard_draw(uint16_t *fb, int stride, int x, int y, int width,
                              const char *name, int max_chars, bool extended,
                              uint32_t fill, uint32_t edge, uint32_t text) {
    static const char *const rows[] = { "QWERTYUIOP", "ASDFGHJKL", "ZXCVBNM" };
    const int key_w = 32, row_counts[] = { 10, 9, 7 };
    int rows_n = extended ? 5 : 4;
    int total_h = rows_n * NAME_KEY_H + (rows_n - 1) * NAME_KEY_GAP;
    render_rect_blend(fb, stride, x - 8, y - 42, width + 16, total_h + 54, C_PANEL, 205);
    render_rect(fb, stride, x, y - 34, width, 28, C_KEY);
    render_rect_edge(fb, stride, x, y - 34, width, 28, edge);
    char shown[64] = {0};
    int n = 0;
    if (name) {
        while (name[n] && n < (int)sizeof shown - 1 && n < max_chars) {
            unsigned char ch = (unsigned char)name[n];
            shown[n] = ch >= 'a' && ch <= 'z' ? (char)(ch - ('a' - 'A')) : (char)ch;
            n++;
        }
    }
    if (n) render_text(fb, stride, x + 7, y - 28, 2, text, shown);
    else render_text(fb, stride, x + 7, y - 28, 2, edge, "TYPE A NAME");
    for (int row = 0; row < 3; row++) {
        int row_w = row_counts[row] * key_w + (row_counts[row] - 1) * NAME_KEY_GAP;
        int left = x + (width - row_w) / 2;
        for (int i = 0; i < row_counts[row]; i++) {
            char label[2] = { rows[row][i], 0 };
            keyboard_key(fb, stride, left + i * (key_w + NAME_KEY_GAP),
                         y + row * NAME_KEY_PITCH, key_w, label, fill, edge);
        }
    }
    int next_row = 3;
    if (extended) {
        static const char digits[] = "0123456789-";
        int digit_w = 28, row_w = (int)sizeof digits - 1;
        row_w = row_w * digit_w + (row_w - 1) * NAME_KEY_GAP;
        int left = x + (width - row_w) / 2;
        for (int i = 0; digits[i]; i++) {
            char label[2] = { digits[i], 0 };
            keyboard_key(fb, stride, left + i * (digit_w + NAME_KEY_GAP),
                         y + next_row * NAME_KEY_PITCH, digit_w, label, fill, edge);
        }
        next_row++;
    }
    int space_w = width * 2 / 3, delete_w = width - space_w - NAME_KEY_GAP;
    if (extended) keyboard_key(fb, stride, x, y + next_row * NAME_KEY_PITCH, space_w, "SPACE", fill, edge);
    else {
        int half = (space_w - NAME_KEY_GAP) / 2;
        keyboard_key(fb, stride, x, y + next_row * NAME_KEY_PITCH, half, "SPACE", fill, edge);
        keyboard_key(fb, stride, x + half + NAME_KEY_GAP, y + next_row * NAME_KEY_PITCH,
                     space_w - half - NAME_KEY_GAP, "SUGGEST", fill, edge);
    }
    keyboard_key(fb, stride, x + space_w + NAME_KEY_GAP, y + next_row * NAME_KEY_PITCH,
                 delete_w, "DELETE", fill, edge);
}
int setup_name_keyboard_hit(float x, float y, int left, int top, int width, bool extended) {
    static const char *const rows[] = { "QWERTYUIOP", "ASDFGHJKL", "ZXCVBNM" };
    const int counts[] = { 10, 9, 7 }, key_w = 32;
    for (int row = 0; row < 3; row++) {
        int row_w = counts[row] * key_w + (counts[row] - 1) * NAME_KEY_GAP;
        int row_left = left + (width - row_w) / 2;
        int key = keyboard_row_hit(x, y, row_left, top + row * NAME_KEY_PITCH, key_w,
                                   counts[row], rows[row]);
        if (key) return key;
    }
    int row = 3;
    if (extended) {
        static const char digits[] = "0123456789-";
        const int key_w_digits = 28, count = (int)sizeof digits - 1;
        int row_w = count * key_w_digits + (count - 1) * NAME_KEY_GAP;
        int row_left = left + (width - row_w) / 2;
        int key = keyboard_row_hit(x, y, row_left, top + row * NAME_KEY_PITCH,
                                   key_w_digits, count, digits);
        if (key) return key;
        row++;
    }
    int space_w = width * 2 / 3;
    int action_y = top + row * NAME_KEY_PITCH;
    if (y >= action_y && y < action_y + NAME_KEY_H) {
        int half = (space_w - NAME_KEY_GAP) / 2;
        if (!extended && x >= left + half + NAME_KEY_GAP && x < left + space_w) return SETUP_HIT_SUGGEST;
        if (x >= left && x < left + (extended ? space_w : half)) return ' ';
        if (x >= left + space_w + NAME_KEY_GAP && x < left + width) return '\b';
    }
    return 0;
}
bool setup_name_keyboard_apply(char *name, size_t capacity, int key) {
    if (!name || !capacity) return false;
    size_t n = 0;
    while (n < capacity && name[n]) n++;
    if (n == capacity) { name[capacity - 1] = 0; n = capacity - 1; }
    if (key == '\b') {
        if (!n) return false;
        name[n - 1] = 0;
        return true;
    }
    if (!((key >= 'A' && key <= 'Z') || (key >= 'a' && key <= 'z') || (key >= '0' && key <= '9') ||
          key == '-' || key == ' '))
        return false;
    if (n + 1 >= capacity) return false;
    name[n++] = (char)key;
    name[n] = 0;
    return true;
}

static bool in_box(float x, float y, int bx, int by, int bw, int bh, int m) {
    return x >= bx - m && x < bx + bw + m && y >= by - m && y < by + bh + m;
}
int setup_hit(float x, float y) {
    if (!s_active) return 0;
    x = render_page_x(x); y = render_page_y(y);
    const int m = 10;                                   /* a fingertip's slop, as on the reset prompt */
    if (s_page == SETUP_PG_MODE) {
        for (int i = 0; i < 3; i++)
            if (in_box(x, y, 24, 76 + i * 56, 400, 44, 0)) return SETUP_HIT_MODE0 + i;
        return in_box(x, y, 24, 282, 400, 38, 0) ? SETUP_HIT_NEXT : 0;
    }
    if (s_page == SETUP_PG_CUSTOM) {
        if (in_box(x, y, SETUP_TOP_NEXT_X, SETUP_TOP_BTN_Y, SETUP_TOP_BTN_W, SETUP_BTN_H, m)) return SETUP_HIT_NEXT;
        if (in_box(x, y, SETUP_TOP_BACK_X, SETUP_TOP_BTN_Y, SETUP_TOP_BTN_W, SETUP_BTN_H, m)) return SETUP_HIT_BACK;
        for (int i = 0; i < 4; i++)
            if (in_box(x, y, SETUP_CUSTOM_X, SETUP_CUSTOM_Y + i * SETUP_CUSTOM_DY, SETUP_CUSTOM_W, SETUP_CUSTOM_H, 2)) return SETUP_HIT_CUSTOM0 + i;
        return 0;
    }
    if (page_one_button())
        return in_box(x, y, SETUP_MID_X, SETUP_BTN_Y, SETUP_BTN_W, SETUP_BTN_H, m) ? SETUP_HIT_NEXT : 0;
    if (nav_on_top()) {
        if (s_place && in_box(x, y, 24, SETUP_MIRROR_Y, 120, 26, 0)) return SETUP_HIT_PREV_PIECE;
        if (s_place && in_box(x, y, 304, SETUP_MIRROR_Y, 120, 26, 0)) return SETUP_HIT_NEXT_PIECE;
        if (s_place && tank_decor_mirrorable(s_item) &&
            in_box(x, y, SETUP_MIRROR_X, SETUP_MIRROR_Y, SETUP_MIRROR_W, SETUP_MIRROR_H, 0)) return SETUP_HIT_MIRROR;
        if (in_box(x, y, SETUP_TOP_NEXT_X, SETUP_TOP_BTN_Y, SETUP_TOP_BTN_W, SETUP_BTN_H, m)) return SETUP_HIT_NEXT;
        if (s_place && tank_decor_movable(s_item) && in_box(x, y, SETUP_TOP_BACK_X, SETUP_TOP_BTN_Y, SETUP_TOP_BTN_W, SETUP_BTN_H, m)) return SETUP_HIT_REMOVE;
        if (s_place && !tank_decor_surface(s_item) && in_box(x, y, SETUP_ORD_X0, SETUP_TOP_BTN_Y, SETUP_ORD_W, SETUP_BTN_H, 6)) return SETUP_HIT_BCK;
        if (s_place && !tank_decor_surface(s_item) && in_box(x, y, SETUP_ORD_X1, SETUP_TOP_BTN_Y, SETUP_ORD_W, SETUP_BTN_H, 6)) return SETUP_HIT_FWD;
        if (!s_place && in_box(x, y, SETUP_TOP_BACK_X, SETUP_TOP_BTN_Y, SETUP_TOP_BTN_W, SETUP_BTN_H, m)) return SETUP_HIT_BACK;
    } else {
        if (in_box(x, y, SETUP_NEXT_X, SETUP_BTN_Y, SETUP_BTN_W, SETUP_BTN_H, m)) return SETUP_HIT_NEXT;
        if (in_box(x, y, SETUP_BACK_X, SETUP_BTN_Y, SETUP_BTN_W, SETUP_BTN_H, m)) return SETUP_HIT_BACK;
    }
    if (page_is_name()) {
        return setup_name_keyboard_hit(x, y, SETUP_NAME_KB_X, s_rename ? SETUP_RENAME_KB_Y : SETUP_NAME_KB_Y,
                                       SETUP_NAME_KB_W, false);
    }
    if (s_page == SETUP_PG_PLACE) {
        if (s_item == 4) {                              /* the cluster's LOOK row: the nearest tile along x */
            int rw = (SETUP_LOOK_N - 1) * SETUP_LOOK_PX + SETUP_LOOK_W;
            if (in_box(x, y, SETUP_LOOK_X, SETUP_LOOK_Y, rw, SETUP_LOOK_H, 12)) {
                int i = (int)((x - SETUP_LOOK_X + (SETUP_LOOK_PX - SETUP_LOOK_W) * 0.5f) / SETUP_LOOK_PX);
                if (i < 0) i = 0;
                if (i >= SETUP_LOOK_N) i = SETUP_LOOK_N - 1;
                return SETUP_HIT_COLOR0 + i;
            }
        }
        if (s_item == 3) {                              /* the coral's COLOR row: the nearest swatch along x, a band 12 px around */
            int rw = (SETUP_COL_N - 1) * SETUP_COL_PX + SETUP_COL_W;
            if (in_box(x, y, SETUP_COL_X, SETUP_COL_Y, rw, SETUP_COL_H, 12)) {
                int i = (int)((x - SETUP_COL_X + (SETUP_COL_PX - SETUP_COL_W) * 0.5f) / SETUP_COL_PX);
                if (i < 0) i = 0;
                if (i >= SETUP_COL_N) i = SETUP_COL_N - 1;
                return SETUP_HIT_COLOR0 + i;
            }
        }
        /* the DEPTH bar: a band 8 px around it, the segment under the finger
           (the item's own depths: the plant's three, the castle's two) */
        int n = tank_decor_z_count(s_item), bw = n * SETUP_DEPTH_SEG_W, bx = (PAGE_W - bw) / 2;
        if (n < 2 || !in_box(x, y, bx, SETUP_DEPTH_Y, bw, SETUP_DEPTH_H, 8)) return 0;
        int i = (int)((x - bx) / SETUP_DEPTH_SEG_W);
        if (i < 0) i = 0;
        if (i >= n) i = n - 1;
        return SETUP_HIT_Z0 + tank_decor_z_at(s_item, i);
    }
    if (page_is_look()) {
        /* the swatch row, a tall band (40 px above, 40 below - fingers land
           low); the nearest swatch along x, no dead space */
        int rw = (SETUP_SW_N - 1) * SETUP_SW_PX + SETUP_SW_W;
        if (!in_box(x, y, SETUP_SW_X, SETUP_SW_Y, rw, SETUP_SW_H, 40) || y >= SETUP_ACC_Y) return 0;
        int i = (int)((x - SETUP_SW_X + (SETUP_SW_PX - SETUP_SW_W) * 0.5f) / SETUP_SW_PX);
        if (i < 0) i = 0;
        if (i >= SETUP_SW_N) i = SETUP_SW_N - 1;
        return SETUP_HIT_BODY0 + i;
    }
    return 0;
}

void setup_activate(tank_t *t, int id) {
    if (s_active && s_page == SETUP_PG_MODE) {
        if (id >= SETUP_HIT_MODE0 && id < SETUP_HIT_MODE0 + 3) { s_mode_pick = id - SETUP_HIT_MODE0; s_mode_error[0] = 0; }
        else if (id == SETUP_HIT_NEXT) {
            const char *error = progression_mode_select(t, s_mode_pick);
            if (error) snprintf(s_mode_error, sizeof s_mode_error, "%s", error);
            else { s_page = SETUP_PG_WELCOME; s_mode_error[0] = 0; }
        }
        return;
    }
    if (!s_active || !id) return;
    fish_t *f = &t->fish[page_fish()];
    if (s_page == SETUP_PG_CUSTOM && id >= SETUP_HIT_CUSTOM0 && id < SETUP_HIT_CUSTOM0 + 4) {
        if (!progression_creative(t)) return;
        f = &s_custom_draft;
        int which = id - SETUP_HIT_CUSTOM0;
        if (which == 0) {
            for (int step = 1; step <= FISH_VARIETY_COUNT; step++) {
                int variety = (f->variety + step) % FISH_VARIETY_COUNT;
                if (journal_variety_unlocked(t, variety)) { f->variety = (uint8_t)variety; break; }
            }
        }
        else if (which == 1) f->pattern = (uint8_t)((f->pattern + 1) % FISH_PATTERN_COUNT);
        else {
            const uint32_t *colors = which == 2 ? FISH_BODY_COLORS : FISH_MARKING_COLORS;
            uint32_t old = which == 2 ? f->color : f->accent;
            int next = 0;
            for (int i = 0; i < FISH_COLOR_N; i++) if (colors[i] == old) { next = (i + 1) % FISH_COLOR_N; break; }
            tank_fish_set_look(f, which == 2 ? colors[next] : 0, which == 3 ? colors[next] : 0);
        }
        return;
    }
    if (s_place) {
        if (id == SETUP_HIT_PREV_PIECE || id == SETUP_HIT_NEXT_PIECE) {
            place_cycle_piece(t, id == SETUP_HIT_PREV_PIECE ? -1 : 1);
            s_remove_armed = false; s_save_error = false;
            return;
        }
        if (id == SETUP_HIT_BACK) { setup_cancel(t); return; }
        if (id == SETUP_HIT_REMOVE) {
            if (!s_remove_armed) { s_remove_armed = true; return; }
            s_remove_armed = false;
            s_place = false;
            bool ok = progression_inventory_store(t, s_item, s_extra);
            if (ok) { s_active = false; tank_emit(TEV_CONFIRM, -1); }
            else { s_place = true; s_save_error = true; s_place_error = progression_shop_notice(); }
            return;
        }
        s_remove_armed = false;
        if (id == SETUP_HIT_NEXT) {
            s_place_error = NULL;
            s_place = false;
            if (progression_save(t)) { s_active = false; tank_emit(TEV_CONFIRM, -1); }
            else { s_place = true; s_save_error = true; }
        }
        else if ((id == SETUP_HIT_BCK || id == SETUP_HIT_FWD) && s_extra < 0) tank_decor_shift(t, s_item, id == SETUP_HIT_FWD ? 1 : -1);
        else if (id == SETUP_HIT_MIRROR) placed_mirror_toggle(t);
        else if (id >= SETUP_HIT_Z0 && id < SETUP_HIT_Z0 + DECOR_Z_N)
            placed_set(t, placed_x(t), id - SETUP_HIT_Z0);
        else if (s_item == 3 && id >= SETUP_HIT_COLOR0 && id < SETUP_HIT_COLOR0 + CORAL_N)
            tank_coral_set_rgb(t, CORAL_PAL[id - SETUP_HIT_COLOR0]);   /* the coral wears it at once; DONE saves it */
        else if (s_item == 4 && id >= SETUP_HIT_COLOR0 && id < SETUP_HIT_COLOR0 + CLUSTER_SCHEME_N)
            tank_cluster_set_scheme(t, id - SETUP_HIT_COLOR0);
        return;
    }
    if (s_rename) {                                     /* one page: DONE keeps the name, CANCEL restores the original */
        if (id == SETUP_HIT_NEXT) { rename_end(t, true); tank_emit(TEV_CONFIRM, -1); return; }
        if (id == SETUP_HIT_BACK) { rename_end(t, false); return; }
    }
    int name_key = id >= 'A' && id <= 'Z' ? id + ('a' - 'A') :
                   (id >= '0' && id <= '9') || id == '-' || id == ' ' || id == '\b' ? id : 0;
    if (page_is_name() && id == SETUP_HIT_SUGGEST) {
        tank_give_name(t, page_fish(), ++s_suggestions);
        tank_emit(TEV_WHEEL_TICK, -1);
        return;
    }
    if (page_is_name() && name_key && setup_name_keyboard_apply(f->name, sizeof f->name, name_key)) {
        tank_emit(TEV_WHEEL_TICK, -1);
        return;
    }
    if (id == SETUP_HIT_NEXT) {
        if (page_is_name()) finish_name(t, page_fish());
        if (page_is_last()) {                           /* BEGIN / DONE: the debt is paid, the names saved */
            bool saved = s_birth ? progression_newborn_done(t) : progression_setup_done(t);
            if (!saved) { s_save_error = true; return; }
            s_save_error = false; s_active = false; stage(t);
            tank_emit(TEV_CONFIRM, -1);
            return;
        }
        s_page++;
        if (page_is_look()) s_page++;
        if (s_page == SETUP_PG_BUBBLES) s_page++;
        stage(t);
        return;
    }
    if (id == SETUP_HIT_BACK) {
        if (s_page > first_page()) s_page--;
        if (s_page == SETUP_PG_BUBBLES) s_page--;
        if (page_is_look()) s_page--;
        stage(t);
        return;
    }
    if (page_is_name()) return;
    if (progression_creative(t) && page_is_look() && id >= SETUP_HIT_BODY0 && id < SETUP_HIT_BODY0 + LOOK_N)
        tank_set_look(t, page_fish(), LOOK_BODY[id - SETUP_HIT_BODY0], 0);   /* the accent stays its secret */
}

void setup_touch(tank_t *t, float x, float y, bool down) {
    stage(t);                                           /* every frame: the tank keeps the fish on stage */
    if (!s_active) { s_down = down; return; }
    if (down && !s_down) {                              /* press */
        s_px = x; s_py = y; s_spun = false;
        s_place_dragging = false;
        int h = setup_hit(x, y);
        if (s_page == SETUP_PG_BUBBLES && !h && render_page_y(y) > SETUP_TOP_BTN_Y + SETUP_BTN_H + 8) {
            tank_set_bubble_x(t, x); s_spun = true;         /* the column comes to the finger */
        }
    } else if (down && s_page == SETUP_PG_PLACE && !setup_hit(s_px, s_py) &&
               render_page_y(s_py) > place_y()) {
        if (!s_place_dragging && fabsf(x - s_px) >= 5.0f) {
            s_place_dragging = true;
            s_spun = true;
        }
        if (s_place_dragging) placed_set(t, x, placed_z(t));
    } else if (down && s_page == SETUP_PG_BUBBLES && s_spun) {
        tank_set_bubble_x(t, x);                            /* ... and follows it */
    } else if (!down && s_down) {                       /* release: a tap, unless dragging */
        if (!s_spun) {
            int h = setup_hit(s_px, s_py);
            if (s_page == SETUP_PG_PLACE && !h && !setup_hit(x, y) &&
                render_page_y(s_py) > place_y())
                place_cycle_copy_at(t, s_px, s_py);
            else if (h && h == setup_hit(x, y)) setup_activate(t, h);
        }
    }
    s_down = down;
}

/* ---- drawing ---- */
static void text_c(uint16_t *fb, int stride, int cx, int y, int scale, uint32_t rgb, const char *s) {
    render_text(fb, stride, cx - render_text_w(s, scale) / 2, y, scale, rgb, s);
}
/* white text on a dark plate: readable over bright water and sand */
static void text_plate(uint16_t *fb, int stride, int cx, int y, int scale, const char *s) {
    int w = render_text_w(s, scale);
    render_rect_blend(fb, stride, cx - w / 2 - 6, y - 4, w + 12, 7 * scale + 8, C_PANEL, 210);
    text_c(fb, stride, cx, y, scale, C_TEXT, s);
}
static void lines_c(uint16_t *fb, int stride, int y, int pitch, uint32_t rgb, const char *const *ls, int n) {
    for (int i = 0; i < n; i++) text_c(fb, stride, SETUP_X + SETUP_W / 2, y + i * pitch, 2, rgb, ls[i]);
}
static void nav(uint16_t *fb, int stride, bool top, const char *next_label, bool go) {
    int y = top ? SETUP_TOP_BTN_Y : SETUP_BTN_Y, w = top ? SETUP_TOP_BTN_W : SETUP_BTN_W;
    render_button(fb, stride, top ? SETUP_TOP_BACK_X : SETUP_BACK_X, y, w, SETUP_BTN_H, C_KEY, C_DIM, s_rename ? "CANCEL" : "BACK", 2);
    render_button(fb, stride, top ? SETUP_TOP_NEXT_X : SETUP_NEXT_X, y, w, SETUP_BTN_H,
                  go ? C_GO : C_INNER, go ? C_GO_E : C_EDGE, next_label, 2);
}
/* page dots along the foot: where the keeper is in the flow */
static void dots(uint16_t *fb, int stride, int y) {
    if (s_place || s_rename) return;                    /* one page: nothing to count */
    int n = s_birth ? SETUP_BIRTH_PAGES : SETUP_PG_N, cur = s_page - first_page();
    int w = n * 10 - 4, x0 = (PAGE_W - w) / 2;
    for (int i = 0; i < n; i++) render_rect(fb, stride, x0 + i * 10, y, 6, 3, i == cur ? C_EDGE : C_DIM);
}
/* a ring round a fish where it swims: the fish is in the TANK, the drawing on
   the page - on a glass the page does not start at (the bowl, the watch) the
   ring sat PAGE_X / PAGE_Y off its fish until 2026-10-01 */
static void fish_ring(uint16_t *fb, int stride, const fish_t *f, float r, uint32_t rgb) {
    render_ring(fb, stride, render_page_x(f->x), render_page_y(f->y), r, rgb);
}
/* a caption with a name in it, the name in its own colour: `pre` NAME `post` */
static void text_named(uint16_t *fb, int stride, int x, int y, const char *pre, const char *name, uint32_t name_rgb, const char *post) {
    render_text(fb, stride, x, y, 2, C_CAPT, pre); x += render_text_w(pre, 2);
    render_text(fb, stride, x, y, 2, name_rgb, name); x += render_text_w(name, 2);
    if (post) render_text(fb, stride, x, y, 2, C_CAPT, post);
}
static uint32_t dim(uint32_t c, int pct) {              /* c toward the ink, pct% of it left */
    return ((c >> 16 & 255) * pct / 100) << 16 | ((c >> 8 & 255) * pct / 100) << 8 | (c & 255) * pct / 100;
}
/* a chevron of 4x4 blocks, `up` pointing up, apex at (cx, y) */
static void chevron(uint16_t *fb, int stride, int cx, int y, bool up, uint32_t rgb) {
    for (int i = 0; i < 5; i++) {
        int yy = up ? y + i * 4 : y - i * 4;
        render_rect(fb, stride, cx - 4 - i * 4, yy, 4, 4, rgb);
        render_rect(fb, stride, cx + i * 4, yy, 4, 4, rgb);
    }
}
/* a small sword leaf for the depth tiles: `h` px tall on the spine x, the
 * plant's own lanceolate taper (widest at 40% up) in its yellow-green */
static void leaf_glyph(uint16_t *fb, int stride, int cx, int y_bot, int h, uint32_t rgb) {
    for (int i = 0; i < h; i++) {
        float u = i / (float)(h - 1);
        float half = 0.6f + 4.4f * (u < 0.4f ? u / 0.4f : (1 - u) / 0.6f);
        int w = (int)(half * 2 + 0.5f); if (w < 1) w = 1;
        render_rect(fb, stride, cx - w / 2, y_bot - i, w, 1, rgb);
    }
}
/* a little castle for the castle's depth tiles: two towers, a wall with
 * merlons, the arch - the stone tones of the real one */
/* a little coral for its depth tiles: a trunk and four branches, rounded
 * with 2 px blocks, in the keeper's colour with a lit tip */
static void coral_glyph(uint16_t *fb, int stride, int cx, int y_bot, uint32_t rgb) {
    uint32_t lit = ((rgb >> 16 & 255) * 3 / 4 + 64) << 16 | ((rgb >> 8 & 255) * 3 / 4 + 64) << 8 | ((rgb & 255) * 3 / 4 + 40);
    render_rect(fb, stride, cx - 2, y_bot - 26, 4, 27, rgb);                       /* the trunk */
    render_rect(fb, stride, cx - 2, y_bot - 28, 4, 2, lit);
    static const int8_t br[4][3] = { { -1, 8, 7 }, { 1, 13, 6 }, { -1, 17, 5 }, { 1, 21, 4 } };   /* side, height, length */
    for (int k = 0; k < 4; k++) {
        int s = br[k][0], y = y_bot - br[k][1], n = br[k][2];
        for (int i = 0; i < n; i++) render_rect(fb, stride, cx + (s > 0 ? 2 + i * 2 : -4 - i * 2), y - i, 2, 3, i == n - 1 ? lit : rgb);
    }
}
/* a little reef cluster for its depth tiles: stones, a tube, a coral sprig,
 * a brain lump, in the look's colours */
static void cluster_glyph(uint16_t *fb, int stride, int cx, int y_bot, const cluster_scheme_t *sc) {
    render_rect(fb, stride, cx - 20, y_bot - 5, 40, 6, 0x7c756a);
    render_rect(fb, stride, cx - 16, y_bot - 8, 10, 4, 0x9a9284); render_rect(fb, stride, cx + 4, y_bot - 8, 12, 4, 0x9a9284);
    render_rect(fb, stride, cx + 8, y_bot - 24, 6, 20, sc->tube); render_rect(fb, stride, cx + 9, y_bot - 25, 4, 2, 0x100418);
    render_rect(fb, stride, cx + 14, y_bot - 17, 5, 13, sc->tube); render_rect(fb, stride, cx + 15, y_bot - 18, 3, 2, 0x100418);
    coral_glyph(fb, stride, cx - 9, y_bot - 4, sc->coral);
    render_rect(fb, stride, cx - 4, y_bot - 12, 12, 8, sc->brain); render_rect(fb, stride, cx - 2, y_bot - 14, 8, 2, sc->brain);
}
static void castle_glyph(uint16_t *fb, int stride, int cx, int y_bot, uint32_t wall, uint32_t roof) {
    const uint32_t tower = 0x87795f, dark = 0x0b1a22, trim = 0xc25f38;
    render_rect(fb, stride, cx - 14, y_bot - 14, 29, 15, wall);
    for (int x = cx - 14; x + 2 <= cx + 14; x += 6) render_rect(fb, stride, x, y_bot - 17, 3, 3, wall);
    render_rect(fb, stride, cx - 21, y_bot - 24, 8, 25, tower);
    for (int i = 0; i < 7; i++) render_rect(fb, stride, cx - 17 - i / 2, y_bot - 31 + i, 1 + i, 1, roof);
    render_rect(fb, stride, cx + 13, y_bot - 20, 8, 21, tower);
    for (int i = 0; i < 6; i++) render_rect(fb, stride, cx + 17 - i / 2, y_bot - 26 + i, 1 + i, 1, roof);
    render_rect(fb, stride, cx - 5, y_bot - 9, 11, 1, trim);
    render_rect(fb, stride, cx - 3, y_bot - 8, 7, 9, dark);
    render_rect(fb, stride, cx - 4, y_bot - 6, 9, 7, dark);
}
/* a little pumpkin lantern for its depth tiles (0.3.6): a squat orange
 * body in three ribs, a stem, a lit face */
static void pumpkin_glyph(uint16_t *fb, int stride, int cx, int y_bot) {
    render_rect(fb, stride, cx - 11, y_bot - 13, 22, 11, 0xe8641a);
    render_rect(fb, stride, cx - 9, y_bot - 15, 18, 15, 0xe8641a);
    render_rect(fb, stride, cx - 13, y_bot - 11, 26, 7, 0xc2420e);
    render_rect(fb, stride, cx - 12, y_bot - 12, 24, 9, 0xe8641a);
    render_rect(fb, stride, cx - 4, y_bot - 14, 1, 13, 0xa83410);    /* the ribs */
    render_rect(fb, stride, cx + 4, y_bot - 14, 1, 13, 0xa83410);
    render_rect(fb, stride, cx - 8, y_bot - 12, 3, 3, 0xf88a2c);     /* the light, upper left */
    render_rect(fb, stride, cx - 1, y_bot - 19, 3, 5, 0x66702a);     /* the stem */
    render_rect(fb, stride, cx - 7, y_bot - 10, 3, 2, 0xffd470);     /* the face, lit */
    render_rect(fb, stride, cx + 4, y_bot - 10, 3, 2, 0xffd470);
    render_rect(fb, stride, cx - 5, y_bot - 6, 10, 2, 0xffa83a);
}
/* a depth tile: the plant's - two leaves and the keeper's first fish, in
 * the order the choice means - BEHIND (the plant behind the fish): leaves,
 * then the fish over them; AMONG: one leaf, the fish, the other leaf; IN
 * FRONT: the fish, then both leaves over it. The castle's (2026-09-16):
 * the castle and two leaves - BEHIND: the castle, the leaves over it; IN
 * FRONT: the leaves, the castle over them (its depths mean the plant layer) */
static void chest_glyph(uint16_t *fb, int stride, int cx, int bottom) {
    render_rect(fb, stride, cx - 16, bottom - 22, 32, 22, 0x976039);
    render_rect_edge(fb, stride, cx - 16, bottom - 22, 32, 22, 0xd9aa40);
    render_rect(fb, stride, cx - 15, bottom - 14, 30, 4, 0xffd55a);
    render_rect(fb, stride, cx - 3, bottom - 10, 6, 6, 0xd9aa40);
}
static void depth_tile(const tank_t *t, uint16_t *fb, int stride, int x, int y, int w, int h, int z, int item, float clock) {
    const int cx = x + w / 2, cy = y + h / 2, lh = h - 6, yb = y + h - 3;
    const uint32_t la = 0x8dbb48, lb = 0x6c9d38;
    const fish_t *who = t->n_fish > 0 ? &t->fish[0] : NULL;
    if (item == 9) {
        if (z == DECOR_Z_BACK) chest_glyph(fb, stride, cx, yb);
        leaf_glyph(fb, stride, cx - 14, yb, lh, la); leaf_glyph(fb, stride, cx + 15, yb, lh, lb);
        if (z != DECOR_Z_BACK) chest_glyph(fb, stride, cx, yb);
        return;
    }
    if (item >= 10 && item <= 13) {
        if (z == DECOR_Z_BACK) render_ornament_preview(fb, stride, item, cx, yb);
        leaf_glyph(fb, stride, cx - 14, yb, lh, la); leaf_glyph(fb, stride, cx + 15, yb, lh, lb);
        if (z != DECOR_Z_BACK) render_ornament_preview(fb, stride, item, cx, yb);
        return;
    }
    if (item == 8) {                                    /* the pumpkin: the castle's reading, the lantern between two leaves */
        if (z == DECOR_Z_BACK) pumpkin_glyph(fb, stride, cx, yb);
        leaf_glyph(fb, stride, cx - 14, yb, lh, la); leaf_glyph(fb, stride, cx + 15, yb, lh, lb);
        if (z != DECOR_Z_BACK) pumpkin_glyph(fb, stride, cx, yb);
        return;
    }
    if (item == 2) {
        if (z == DECOR_Z_BACK) castle_glyph(fb, stride, cx, yb, 0xa99b7b, 0xc4a95e);
        leaf_glyph(fb, stride, cx - 14, yb, lh, la); leaf_glyph(fb, stride, cx + 15, yb, lh, lb);
        if (z != DECOR_Z_BACK) castle_glyph(fb, stride, cx, yb, 0xa99b7b, 0xc4a95e);
        return;
    }
    if (item == 4) {                                    /* the cluster: the same three readings as the coral's */
        const cluster_scheme_t *sc = &CLUSTER_SCHEMES[tank_cluster_scheme(t)];
        const int kx = cx - 18;
        if (z == DECOR_Z_BACK) cluster_glyph(fb, stride, kx, yb, sc);
        leaf_glyph(fb, stride, cx - 44, yb, lh, la);
        if (z == DECOR_Z_MIDDLE) cluster_glyph(fb, stride, kx, yb, sc);
        leaf_glyph(fb, stride, cx + 6, yb, lh, lb);
        if (who) render_fish_portrait(fb, stride, (float)(cx + 26), (float)cy, 0.62f, who, clock);
        if (z == DECOR_Z_FRONT) cluster_glyph(fb, stride, kx, yb, sc);
        return;
    }
    if (item == 3) {                                    /* the coral, left of centre, the fish passing to its right: behind the
                                                           grass and the fish / among the grass, the fish in front / over all */
        uint32_t rgb = tank_coral_rgb(t);
        const int kx = cx - 20;
        if (z == DECOR_Z_BACK) coral_glyph(fb, stride, kx, yb, rgb);
        leaf_glyph(fb, stride, cx - 36, yb, lh, la);
        if (z == DECOR_Z_MIDDLE) coral_glyph(fb, stride, kx, yb, rgb);
        leaf_glyph(fb, stride, cx - 6, yb, lh, lb);
        if (who) render_fish_portrait(fb, stride, (float)(cx + 14), (float)cy, 0.62f, who, clock);
        if (z == DECOR_Z_FRONT) coral_glyph(fb, stride, kx, yb, rgb);
        return;
    }
    if (z == DECOR_Z_BACK)  { leaf_glyph(fb, stride, cx - 8, yb, lh, la); leaf_glyph(fb, stride, cx + 8, yb, lh, lb); }
    if (z == DECOR_Z_MIDDLE)  leaf_glyph(fb, stride, cx - 8, yb, lh, la);
    if (who) render_fish_portrait(fb, stride, (float)cx, (float)cy, 0.62f, who, clock);
    if (z == DECOR_Z_MIDDLE)  leaf_glyph(fb, stride, cx + 8, yb, lh, lb);
    if (z == DECOR_Z_FRONT) { leaf_glyph(fb, stride, cx - 8, yb, lh, la); leaf_glyph(fb, stride, cx + 8, yb, lh, lb); }
}
/* an order arrow (the placement page): a chevron in a small button, lit or dim */
static void ord_arrow(uint16_t *fb, int stride, int x, int y, bool right, bool on) {
    uint32_t e = on ? C_EDGE : dim(C_CAPT, 45);
    render_button(fb, stride, x, y, SETUP_ORD_W, SETUP_BTN_H, C_INNER, e, "", 2);
    int cx = x + SETUP_ORD_W / 2, cy = y + SETUP_BTN_H / 2;
    for (int i = 0; i < 7; i++) {
        int xx = right ? cx - 3 + i : cx + 3 - i;
        render_rect(fb, stride, xx, cy - 6 + i, 2, 1, e);
        render_rect(fb, stride, xx, cy + 6 - i, 2, 1, e);
    }
}
static void panel(uint16_t *fb, int stride) {
    render_menu_backdrop(fb, stride, SETUP_X, SETUP_Y, SETUP_W, SETUP_H);
}

void render_setup(const tank_t *t, uint16_t *fb, int stride, float clock) {
    if (!s_active) return;
    const int CX = PAGE_W / 2;
    const int FLOOR = render_page_y(TANK_BOT - 16);
    if (s_page == SETUP_PG_MODE) {
        render_menu_backdrop(fb, stride, -PAGE_X, -PAGE_Y, PAGE_FRAME_W, PAGE_FRAME_H);
        text_c(fb, stride, CX, 18, 3, C_TEXT, "CHOOSE YOUR MODE");
        text_c(fb, stride, CX, 52, 1, C_CAPT, "WIPE ALL PROGRESS TO CHANGE MODE");
        static const char *const labels[] = {
            "NORMAL - EARN / NO DEATHS",
            "CREATIVE - EVERYTHING FREE",
            "HARD - NEGLECT CAN BE FATAL"
        };
        for (int i = 0; i < 3; i++)
            render_button(fb, stride, 24, 76 + i * 56, 400, 44,
                          s_mode_pick == i ? C_FILL : C_INNER, s_mode_pick == i ? C_TEXT : C_EDGE, labels[i], 2);
        text_c(fb, stride, CX, 250, 1, C_CAPT, s_mode_pick == GAME_MODE_HARD ?
               "HARD: DEATHS ONLY WHILE RUNNING" : "TANKS SHARE MONEY AND PROGRESS");
        render_button(fb, stride, 24, 282, 400, 38, C_INNER, C_EDGE, "START THIS MODE", 2);
        text_c(fb, stride, CX, 336, 1, C_CAPT, s_mode_error);
    } else if (s_page == SETUP_PG_CUSTOM) {
        render_menu_backdrop(fb, stride, -PAGE_X, -PAGE_Y, PAGE_FRAME_W, PAGE_FRAME_H);
        text_c(fb, stride, CX, 10, 2, C_TEXT, s_save_error ? "SAVE FAILED - RETRY DONE" : "CUSTOMIZE YOUR FISH");
        nav(fb, stride, true, "DONE", true);
        fish_t preview = s_custom_draft; preview.stage = STAGE_ADULT;
        render_fish_portrait(fb, stride, CX, 126, 1.6f, &preview, clock);
        char line[40];
        for (int i = 0; i < 4; i++) {
            const fish_t *f = &s_custom_draft;
            if (i == 0) snprintf(line, sizeof line, "VARIETY: %s", FISH_VARIETY_NAMES[f->variety % FISH_VARIETY_COUNT]);
            else if (i == 1) snprintf(line, sizeof line, "PATTERN: %s", FISH_PATTERN_NAMES[f->pattern % FISH_PATTERN_COUNT]);
            else snprintf(line, sizeof line, "%s COLOR: %06lX", i == 2 ? "BODY" : "MARKING", (unsigned long)(i == 2 ? f->color : f->accent));
            render_button(fb, stride, SETUP_CUSTOM_X, SETUP_CUSTOM_Y + i * SETUP_CUSTOM_DY, SETUP_CUSTOM_W, SETUP_CUSTOM_H, C_INNER, C_EDGE, line, 2);
        }
        text_c(fb, stride, CX, 350, 2, C_CAPT, "VARIETIES UNLOCK WITH KEEPER XP");
    } else if (s_page == SETUP_PG_WELCOME) {
        panel(fb, stride);
        text_c(fb, stride, CX, SETUP_Y + 36, 3, C_TEXT, "WELCOME");
        static const char *const ls[] = {
            "TWO FRY HAVE MOVED IN.",
            "THEY EAT, PLAY, REST AND",
            "GROW UP WHILE YOU WATCH.",
            "EACH ONE THINKS FOR ITSELF.",
            "FIRST, LET'S MEET THEM.",
        };
        lines_c(fb, stride, SETUP_Y + 96, 24, C_CAPT, ls, 5);
        render_button(fb, stride, SETUP_MID_X, SETUP_BTN_Y, SETUP_BTN_W, SETUP_BTN_H, C_INNER, C_EDGE, "NEXT", 2);
        dots(fb, stride, SETUP_Y + SETUP_H - 8);
    } else if (s_page == SETUP_PG_BUBBLES) {
        /* the live tank with a stripe down the column: drag it anywhere */
        text_c(fb, stride, CX, SETUP_Y + 7, 2, C_CAPT, "PLACE THE BUBBLES");
        nav(fb, stride, true, "NEXT", false);
        int bx = render_page_x(t->bubble_x);
        render_rect_blend(fb, stride, bx - 22, SETUP_TOP_BTN_Y + SETUP_BTN_H + 10, 44, FLOOR - (SETUP_TOP_BTN_Y + SETUP_BTN_H + 10), C_EDGE, 46);
        chevron(fb, stride, bx, FLOOR - 34, true, C_EDGE);
        text_c(fb, stride, CX, 296, 2, C_CAPT, "DRAG THEM LEFT OR RIGHT");
        dots(fb, stride, PAGE_H - 14);
    } else if (s_page == SETUP_PG_PLACE) {
        /* the live tank with a stripe over the piece's footprint: drag it
           anywhere on the water; the layer row picks its depth (the tank
           under the page redraws with it, so the fish and grass show the choice) */
        char title[40];
        if (s_save_error) snprintf(title, sizeof title, "%s", s_place_error ? s_place_error : "SAVE FAILED - RETRY");
        else snprintf(title, sizeof title, "PREVIEW: %s", SD_ITEMS[s_item].name);
        text_plate(fb, stride, CX, SETUP_Y - 3, render_text_w(title, 2) <= SETUP_W ? 2 : 1, title);
        render_button(fb, stride, SETUP_TOP_NEXT_X, SETUP_TOP_BTN_Y, SETUP_TOP_BTN_W, SETUP_BTN_H, C_GO, C_GO_E, "DONE", 2);
        if (tank_decor_movable(s_item))             /* every piece, fixed scenery too, can be put away */
            render_button(fb, stride, SETUP_TOP_BACK_X, SETUP_TOP_BTN_Y, SETUP_TOP_BTN_W, SETUP_BTN_H,
                          C_INNER, s_remove_armed ? C_GO_E : C_DIM, s_remove_armed ? "REMOVE?" : "REMOVE", 2);
        bool surface = tank_decor_surface(s_item);
        render_button(fb, stride, 24, SETUP_MIRROR_Y, 120, 26, C_INNER, C_EDGE, "< PREV", 2);
        render_button(fb, stride, 304, SETUP_MIRROR_Y, 120, 26, C_INNER, C_EDGE, "NEXT >", 2);
        if (tank_decor_mirrorable(s_item))
            render_button(fb, stride, SETUP_MIRROR_X, SETUP_MIRROR_Y, SETUP_MIRROR_W, SETUP_MIRROR_H,
                          C_INNER, placed_mirrored(t) ? C_EDGE : C_DIM, "MIRROR", 2);
        /* the ORDER arrows (2026-10-17): a step back / forward in the stack of
           pieces at this depth, lit when there is a piece to pass; the relation
           is named on the hint line ("UNDER THE CORAL"). The page draws once per
           tile, so its lookups run once a frame (keyed by the clock and the page's look). */
        static struct { const tank_t *t; float clock; uint32_t sig; int below, above; char rel[40]; } memo;
        uint32_t sig = setup_place_signature(t);
        if (memo.t != t || memo.clock != clock || memo.sig != sig) {
            memo.t = t; memo.clock = clock; memo.sig = sig;
            memo.below = s_extra < 0 ? tank_decor_neighbor(t, s_item, -1) : -1;
            memo.above = s_extra < 0 ? tank_decor_neighbor(t, s_item, 1) : -1;
            /* the relation, in the depth bar's own words (Strato 2026-10-17: "behind
               the coral" / "in front of the coral"); "THE" goes when the line is too
               long for where it sits (the hint line, or beside COLOR / LOOK) */
            int rel_avail = PAGE_W - 2 * 56 - 8;                               /* the hint line: the box every glass shows whole */
            if (s_item == 3) rel_avail = (SETUP_COL_N - 1) * SETUP_COL_PX + SETUP_COL_W - render_text_w("COLOR", 2) - 14;
            if (s_item == 4) rel_avail = (SETUP_LOOK_N - 1) * SETUP_LOOK_PX + SETUP_LOOK_W - render_text_w("LOOK", 2) - 14;
            memo.rel[0] = 0;
            for (int pass = 0; pass < 2; pass++) {
                const char *the = pass ? "" : "THE ";
                if (memo.above >= 0) snprintf(memo.rel, sizeof memo.rel, "BEHIND %s%s", the, SD_ITEMS[memo.above].name);
                else if (memo.below >= 0) snprintf(memo.rel, sizeof memo.rel, "IN FRONT OF %s%s", the, SD_ITEMS[memo.below].name);
                if (render_text_w(memo.rel, 2) <= rel_avail) break;
            }
        }
        int below = memo.below, above = memo.above;
        const char *rel = memo.rel;
        if (!surface) {
            ord_arrow(fb, stride, SETUP_ORD_X0, SETUP_TOP_BTN_Y, false, below >= 0);
            ord_arrow(fb, stride, SETUP_ORD_X1, SETUP_TOP_BTN_Y, true, above >= 0);
        }
        /* the DEPTH bar: one outlined box, three joined segments, the chosen
           one lit; a picture tile on each and the word under it */
        static const char *const zl[DECOR_Z_N] = { "BEHIND", "AMONG", "IN FRONT" };
        static const char *const hint[DECOR_Z_N] = { "THE FISH SWIM IN FRONT OF IT", "THE FISH SWIM THROUGH IT", "THE FISH SWIM BEHIND IT" };
        static const char *const castle_hint[DECOR_Z_N] = { "THE PLANTS GROW IN FRONT OF IT", "", "IT STANDS IN FRONT OF THE PLANTS" };
        static const char *const coral_hint[DECOR_Z_N] = { "BEHIND THE GRASS AND THE FISH", "", "IN FRONT OF EVERYTHING" };
        int z = placed_z(t), zi = tank_decor_z_index(s_item, z);
        int n = tank_decor_z_count(s_item), bw = n * SETUP_DEPTH_SEG_W, bx = (PAGE_W - bw) / 2;   /* the item's own depths */
        if (n) render_rect(fb, stride, bx, SETUP_DEPTH_Y, bw, SETUP_DEPTH_H, C_KEY);
        for (int i = 0; i < n; i++) {
            int sx = bx + i * SETUP_DEPTH_SEG_W, zi_ = tank_decor_z_at(s_item, i);
            if (i == zi) { render_rect(fb, stride, sx, SETUP_DEPTH_Y, SETUP_DEPTH_SEG_W, SETUP_DEPTH_H, C_INNER);
                           render_rect_edge(fb, stride, sx + 1, SETUP_DEPTH_Y + 1, SETUP_DEPTH_SEG_W - 2, SETUP_DEPTH_H - 2, C_EDGE); }
            else if (i > 0) render_rect(fb, stride, sx, SETUP_DEPTH_Y + 6, 1, SETUP_DEPTH_H - 12, C_DIM);   /* a divider */
            depth_tile(t, fb, stride, sx, SETUP_DEPTH_Y + 4, SETUP_DEPTH_SEG_W, SETUP_DEPTH_TILE_H, zi_, s_item, clock);
            text_c(fb, stride, sx + SETUP_DEPTH_SEG_W / 2, SETUP_DEPTH_Y + SETUP_DEPTH_TILE_H + 10, 2, i == zi ? C_TEXT : dim(C_CAPT, 75), zl[zi_]);
        }
        if (n) render_rect_edge(fb, stride, bx, SETUP_DEPTH_Y, bw, SETUP_DEPTH_H, C_EDGE);
        if (s_item == 4) {
            /* the cluster: a LOOK row of three tiles - the coral, the tubes and
               the brain as three bands over the look's name - the pick ringed */
            int look = tank_cluster_scheme(t);
            render_rect_blend(fb, stride, -PAGE_X, SETUP_LOOK_Y - 22, PAGE_FRAME_W, SETUP_LOOK_H + 30, C_PANEL, 190);
            render_text(fb, stride, SETUP_LOOK_X + 2, SETUP_LOOK_Y - 18, 2, C_CAPT, "LOOK");
            if (rel[0]) render_text(fb, stride, SETUP_LOOK_X + SETUP_LOOK_N * SETUP_LOOK_PX - SETUP_LOOK_PX + SETUP_LOOK_W - 2 - render_text_w(rel, 2), SETUP_LOOK_Y - 18, 2, C_TEXT, rel);
            for (int i = 0; i < SETUP_LOOK_N; i++) {
                int x = SETUP_LOOK_X + i * SETUP_LOOK_PX; bool on = i == look;
                const cluster_scheme_t *sc = &CLUSTER_SCHEMES[i];
                render_rect(fb, stride, x, SETUP_LOOK_Y, SETUP_LOOK_W, SETUP_LOOK_H, C_KEY);
                int bw = (SETUP_LOOK_W - 6) / 3;
                render_rect(fb, stride, x + 3, SETUP_LOOK_Y + 3, bw, 14, sc->coral);
                render_rect(fb, stride, x + 3 + bw, SETUP_LOOK_Y + 3, bw, 14, sc->tube);
                render_rect(fb, stride, x + 3 + 2 * bw, SETUP_LOOK_Y + 3, SETUP_LOOK_W - 6 - 2 * bw, 14, sc->brain);
                text_c(fb, stride, x + SETUP_LOOK_W / 2, SETUP_LOOK_Y + 20, 2, on ? C_TEXT : dim(C_CAPT, 45), sc->name);
                render_rect_edge(fb, stride, x, SETUP_LOOK_Y, SETUP_LOOK_W, SETUP_LOOK_H, on ? C_TEXT : C_DIM);
                if (on) render_rect_edge(fb, stride, x + 1, SETUP_LOOK_Y + 1, SETUP_LOOK_W - 2, SETUP_LOOK_H - 2, C_TEXT);
            }
        } else if (s_item == 3) {
            /* the coral: no hint line - a COLOR row instead (the fish colour
               page's swatches, shorter), the pick ringed, the hint under the
               row's caption */
            uint32_t rgb = tank_coral_rgb(t);
            render_rect_blend(fb, stride, -PAGE_X, SETUP_COL_Y - 22, PAGE_FRAME_W, SETUP_COL_H + 30, C_PANEL, 190);
            render_text(fb, stride, SETUP_COL_X + 2, SETUP_COL_Y - 18, 2, C_CAPT, "COLOR");
            if (rel[0]) render_text(fb, stride, SETUP_COL_X + (SETUP_COL_N - 1) * SETUP_COL_PX + SETUP_COL_W - 2 - render_text_w(rel, 2), SETUP_COL_Y - 18, 2, C_TEXT, rel);
            (void)coral_hint;
            for (int i = 0; i < SETUP_COL_N; i++) {
                int x = SETUP_COL_X + i * SETUP_COL_PX;
                bool on = CORAL_PAL[i] == rgb;
                render_rect(fb, stride, x, SETUP_COL_Y, SETUP_COL_W, SETUP_COL_H, CORAL_PAL[i]);
                render_rect_edge(fb, stride, x, SETUP_COL_Y, SETUP_COL_W, SETUP_COL_H, on ? C_TEXT : C_DIM);
                if (on) render_rect_edge(fb, stride, x + 1, SETUP_COL_Y + 1, SETUP_COL_W - 2, SETUP_COL_H - 2, C_TEXT);
            }
        } else text_plate(fb, stride, CX, SETUP_DEPTH_HINT_Y, 2, surface ? hint[z] : rel[0] ? rel : s_item == 2 || s_item >= 8 ? castle_hint[z] : hint[z]);
        float x0 = placed_x(t) - tank_decor_half_w(s_item) - 10, x1 = placed_x(t) + tank_decor_half_w(s_item) + 10, top = TANK_BOT - 16 - 40;
        if (s_item == 0) tank_veg_bed(t, 3, NULL, NULL, &top, NULL);   /* the leaves' reach */
        if (s_item == 2) top = TANK_BOT - 16 - 146;                    /* the tallest spire */
        if (s_item == 3) top = TANK_BOT - 14 + DECOR_SINK - 92;        /* the coral's top tip */
        if (s_item == 4) top = TANK_BOT - 14 + DECOR_SINK - 120;       /* the cluster's tallest tube */
        if (s_item == 8) top = TANK_BOT - 14 + DECOR_SINK - 82;        /* the pumpkin's stem */
        if (s_item >= 9) top = TANK_BOT - 14 + DECOR_SINK - tank_ornament_h(s_item);
        x0 = render_page_x(x0); x1 = render_page_x(x1); top = render_page_y(top);
        int sy = (int)top - 8; if (sy < place_y() || surface) sy = place_y();   /* a float: the whole column under it */
        render_rect_blend(fb, stride, (int)x0, sy, (int)(x1 - x0), FLOOR - sy + 4, C_EDGE, 64);
        render_rect(fb, stride, (int)x0, sy, 2, FLOOR - sy + 4, C_EDGE);
        render_rect(fb, stride, (int)x1 - 2, sy, 2, FLOOR - sy + 4, C_EDGE);
        int original_x = render_page_x(s_selected_original_x);
        render_rect_blend(fb, stride, original_x - 1, sy, 3, FLOOR - sy + 4, C_TEXT, 180);
        render_rect(fb, stride, original_x - 6, FLOOR - 17, 12, 2, C_TEXT);
        if (!surface) chevron(fb, stride, (int)((x0 + x1) * 0.5f), FLOOR - 34, true, C_EDGE);
    } else if (page_is_name()) {
        /* The fish remains staged behind the same shared QWERTY keyboard
           used for aquarium naming. */
        const fish_t *f = &t->fish[page_fish()];
        if (s_rename) {
            /* Renaming from Fish Summary owns the whole page so the menu
               underneath never competes with the keyboard. */
            render_menu_backdrop(fb, stride, -PAGE_X, -PAGE_Y, PAGE_FRAME_W, PAGE_FRAME_H);
            render_button(fb, stride, 24, 94, 400, 82, C_INNER, C_EDGE, "", 1);
            fish_t preview = *f;
            render_fish_portrait(fb, stride, 96, 135, 0.8f, &preview, clock);
            render_text(fb, stride, 176, 108, 2, C_CAPT, FISH_VARIETY_NAMES[f->variety % FISH_VARIETY_COUNT]);
            render_text(fb, stride, 176, 134, 1, C_TEXT, "TYPE A NEW NAME.");
            render_text(fb, stride, 176, 150, 1, C_DIM, "DONE SAVES IT.");
        } else fish_ring(fb, stride, f, 17 * f->size + 6, f->color);
        text_c(fb, stride, CX, SETUP_Y + 7, 2, C_CAPT, s_rename ? "RENAME YOUR FISH" : s_birth ? "NAME THE NEW FRY" : page_fish() == 0 ? "NAME THE FIRST FISH" : "NAME THE SECOND FISH");
        nav(fb, stride, true, s_rename ? "DONE" : "NEXT", s_rename);
        setup_name_keyboard_draw(fb, stride, SETUP_NAME_KB_X, s_rename ? SETUP_RENAME_KB_Y : SETUP_NAME_KB_Y,
                                 SETUP_NAME_KB_W, f->name, FISH_NAME_MAX, false,
                                 C_KEY, C_EDGE, C_TEXT);
        dots(fb, stride, PAGE_H - 14);
    } else if (page_is_look()) {
        /* straight on the tank again: the ringed FRY is the preview (no
           grown-up look - that is the surprise); a row of body swatches; the
           accent a "?" that its first growth spurt answers */
        const fish_t *f = &t->fish[page_fish()];
        fish_ring(fb, stride, f, 17 * f->size + 6, f->color);
        render_rect_blend(fb, stride, -PAGE_X, SETUP_SW_Y - 30, PAGE_FRAME_W, SETUP_ACC_Y + SETUP_SW_H + 16 - (SETUP_SW_Y - 30), C_PANEL, 110);
        char cap[FISH_NAME_MAX + 16]; snprintf(cap, sizeof cap, "A COLOR FOR %s", f->name);
        text_c(fb, stride, CX, SETUP_Y + 7, 2, C_CAPT, cap);
        nav(fb, stride, true, "NEXT", false);
        render_text(fb, stride, SETUP_SW_X + 2, SETUP_SW_Y - 20, 2, C_CAPT, "BODY");
        for (int i = 0; i < SETUP_SW_N; i++) {
            int x = SETUP_SW_X + i * SETUP_SW_PX;
            bool on = LOOK_BODY[i] == f->color;
            render_rect(fb, stride, x, SETUP_SW_Y, SETUP_SW_W, SETUP_SW_H, LOOK_BODY[i]);
            render_rect_edge(fb, stride, x, SETUP_SW_Y, SETUP_SW_W, SETUP_SW_H, on ? C_TEXT : C_DIM);
            if (on) render_rect_edge(fb, stride, x + 1, SETUP_SW_Y + 1, SETUP_SW_W - 2, SETUP_SW_H - 2, C_TEXT);
        }
        render_text(fb, stride, SETUP_SW_X + 2, SETUP_ACC_Y - 20, 2, C_CAPT, "ACCENT");
        render_rect(fb, stride, SETUP_SW_X, SETUP_ACC_Y, SETUP_SW_W, SETUP_SW_H, C_INNER);
        render_rect_edge(fb, stride, SETUP_SW_X, SETUP_ACC_Y, SETUP_SW_W, SETUP_SW_H, C_DIM);
        render_text(fb, stride, SETUP_SW_X + (SETUP_SW_W - 20) / 2, SETUP_ACC_Y + (SETUP_SW_H - 28) / 2, 4, C_CAPT, "?");
        render_text(fb, stride, SETUP_SW_X + SETUP_SW_W + 16, SETUP_ACC_Y + 12, 2, C_CAPT, "ITS MARKINGS COME IN");
        render_text(fb, stride, SETUP_SW_X + SETUP_SW_W + 16, SETUP_ACC_Y + 32, 2, C_CAPT, "AS IT GROWS UP");
        dots(fb, stride, PAGE_H - 14);
    } else if (s_page == SETUP_PG_BORN) {
        /* the announcement, over the live tank: the fry wears a double ring
           wherever it hatched (no stage - it stays in the grass it was born
           in), its parents are named, one button leads on */
        const fish_t *f = &t->fish[page_fish()];
        float r = 17 * f->size + 6 + 3 * sinf(clock * 3);          /* a ring that breathes */
        fish_ring(fb, stride, f, r, f->color);
        fish_ring(fb, stride, f, r + 8, C_EDGE);
        render_rect_blend(fb, stride, -PAGE_X, 92, PAGE_FRAME_W, 116, C_PANEL, 150);
        text_c(fb, stride, CX, 104, 3, C_TEXT, "A NEW FRY!");
        char l1[FISH_NAME_MAX * 2 + 24];
        bool shop = f->parent_a < 0 && f->parent_b < 0;              /* bought (0.3.5): no parents, in at the surface */
        if (shop) snprintf(l1, sizeof l1, "FROM THE SHOP,");
        else snprintf(l1, sizeof l1, "BORN TO %s AND %s", fish_name(t, f->parent_a), fish_name(t, f->parent_b));
        text_c(fb, stride, CX, 140, 2, C_CAPT, l1);
        text_c(fb, stride, CX, 162, 2, C_CAPT, shop ? "IN AT THE SURFACE." : "DOWN IN THE GRASS.");
        text_c(fb, stride, CX, 184, 2, C_CAPT, "GO AND SAY HELLO.");
        render_button(fb, stride, SETUP_MID_X, SETUP_BTN_Y, SETUP_BTN_W, SETUP_BTN_H, C_GO, C_GO_E, "MEET IT", 2);
        dots(fb, stride, PAGE_H - 14);
    } else if (s_page == SETUP_PG_FAMILY) {
        /* the family page: a panel (the fry on its own), its name, its
           portrait as it is now - a fry, no markings yet - and what it
           inherited: whose body, whose markings, and how bold and sociable
           it is next to its parents' marks */
        const fish_t *f = &t->fish[page_fish()];
        const fish_t *pa = f->parent_a >= 0 && f->parent_a < t->n_fish ? &t->fish[f->parent_a] : NULL;
        const fish_t *pb = f->parent_b >= 0 && f->parent_b < t->n_fish ? &t->fish[f->parent_b] : NULL;
        bool shop = f->parent_a < 0 && f->parent_b < 0;              /* bought (0.3.5): its look is the shop's */
        const char *na = shop ? "THE SHOP" : fish_name(t, f->parent_a), *nb = shop ? "THE SHOP" : fish_name(t, f->parent_b);
        uint32_t ca = pa ? pa->color : shop ? C_CAPT : C_DIM, cb = pb ? pb->color : shop ? C_CAPT : C_DIM;
        panel(fb, stride);
        text_c(fb, stride, CX, SETUP_Y + 20, 3, C_TEXT, f->name);
        float ps = f->size * 2.0f;                                   /* a portrait, twice life size (a fry) ... */
        if (ps > 1.2f) ps = 1.2f;                                    /* ... but a juvenile's ring must clear the name */
        render_ring(fb, stride, CX, SETUP_FAM_PORTRAIT_Y, 17 * ps + 6, f->color);
        render_fish_portrait(fb, stride, CX, SETUP_FAM_PORTRAIT_Y, ps, f, clock);
        int y = SETUP_FAM_ROW_Y, vx = SETUP_FAM_VALUE_X;
        render_text(fb, stride, SETUP_FAM_LABEL_X, y, 2, C_CAPT, "BODY");
        render_rect(fb, stride, vx, y - 1, 16, 16, f->color); render_rect_edge(fb, stride, vx, y - 1, 16, 16, C_INNER);
        text_named(fb, stride, vx + 24, y, "FROM ", na, ca, NULL);
        y += SETUP_FAM_ROW_DY;
        render_text(fb, stride, SETUP_FAM_LABEL_X, y, 2, C_CAPT, "MARKINGS");
        render_rect(fb, stride, vx, y - 1, 16, 16, f->accent); render_rect_edge(fb, stride, vx, y - 1, 16, 16, C_INNER);
        text_named(fb, stride, vx + 24, y, "FROM ", nb, cb, NULL);
        static const char *const TRAIT[2] = { "BOLD", "SOCIAL" };
        for (int k = 0; k < 2; k++) {                                /* the fry's own bar, the parents' ticks */
            y += SETUP_FAM_ROW_DY;
            float v = k ? f->sociable : f->bold;
            float va = pa ? (k ? pa->sociable : pa->bold) : -1, vb = pb ? (k ? pb->sociable : pb->bold) : -1;
            int by = y + 3;
            render_text(fb, stride, SETUP_FAM_LABEL_X, y, 2, C_CAPT, TRAIT[k]);
            render_rect(fb, stride, vx, by, SETUP_FAM_BAR_W, SETUP_FAM_BAR_H, C_DIM);
            render_rect(fb, stride, vx, by, (int)(SETUP_FAM_BAR_W * v + 0.5f), SETUP_FAM_BAR_H, C_FILL);
            for (int p = 0; p < 2; p++) {                            /* a parent's tick: its body colour on an ink outline, so it reads on the fill too */
                float vp = p ? vb : va; if (vp < 0) continue;
                int tx = vx + (int)(SETUP_FAM_BAR_W * vp);
                render_rect(fb, stride, tx - 2, by - 5, 5, SETUP_FAM_BAR_H + 10, C_PANEL);
                render_rect(fb, stride, tx - 1, by - 4, 3, SETUP_FAM_BAR_H + 8, p ? cb : ca);
            }
        }
        y += SETUP_FAM_ROW_DY;
        render_text(fb, stride, SETUP_FAM_LABEL_X, y, 2, C_CAPT, "PARENTS");
        if (shop) render_text(fb, stride, vx, y, 2, C_CAPT, "NONE HERE");
        else {
            render_text(fb, stride, vx, y, 2, ca, na);
            render_text(fb, stride, vx + render_text_w(na, 2) + 16, y, 2, cb, nb);
        }
        nav(fb, stride, false, "DONE", true);
        dots(fb, stride, SETUP_Y + SETUP_H - 8);
    } else {                                                        /* SETUP_PG_CARE */
        panel(fb, stride);
        text_c(fb, stride, CX, SETUP_Y + 22, 3, C_TEXT, "CARING FOR THEM");
        static const char *const ls[] = {
            "TAP THE SURFACE TO FEED",
            "HOLD A FINGER AND THEY VISIT",
            "TAP A FISH TO CHECK ON IT",
            "DRAG THE GLASS TO WIPE ALGAE",
            "SWIPE ACROSS GRASS TO TRIM",
            "TWO TAPS FLIP THE LIGHT",
            "THEY CHOOSE. YOU CARE.",
        };
        lines_c(fb, stride, SETUP_Y + 66, 24, C_CAPT, ls, 7);
        nav(fb, stride, false, "BEGIN", true);
        dots(fb, stride, SETUP_Y + SETUP_H - 8);
    }
    if (s_save_error && !s_place && !s_rename) {
        render_rect(fb, stride, 0, 0, PAGE_W, 28, C_PANEL);
        text_c(fb, stride, CX, 10, 2, C_TEXT, "SAVE FAILED - RETRY");
    }
}
