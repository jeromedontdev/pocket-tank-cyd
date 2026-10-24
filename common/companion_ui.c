#include "companion.h"
#include "render.h"
#include "progression.h"
#include <stdio.h>
#include <string.h>

static int page, selected, leaf, story, section;
static bool draft_pending;
static bool confirm_departure;
static int wardrobe = WEAR_TOP_HAT;
static int creature;
static uint8_t color_draft;
static char notice[48];
static const uint32_t frame_colors[] = {0x83b8ac, 0x8ed9a5, 0xf0aeca, 0xc0b1ff};
static void text(uint16_t *fb, int stride, int x, int y, uint32_t rgb, const char *s) {
    render_text(fb, stride, x, y, 2, rgb, s);
}
static void button(uint16_t *fb, int stride, int x, int y, int w, const char *s) {
    render_rect(fb, stride, x, y, w, 30, 0x264d55);
    int scale = render_text_w(s, 2) <= w - 16 ? 2 : 1;
    render_text(fb, stride, x + 8, y + (scale == 2 ? 7 : 11), scale, 0xe7f4dc, s);
}
static unsigned bits(unsigned value) {
    unsigned n = 0; for (; value; value >>= 1) n += value & 1u; return n;
}
static void result(bool ok) { snprintf(notice, sizeof notice, "%s", ok ? "Saved. No hurry, ever." : "Not ready or save failed. Retry."); }
void companion_open(void) { page = section = selected = leaf = story = creature = 0; wardrobe = WEAR_TOP_HAT; color_draft = 0; draft_pending = false; confirm_departure = false; notice[0] = 0; }
void companion_open_section(int target) { companion_open(); page = section = target; draft_pending = target == 5; }
static void prepare_draft(const tank_t *t) { if (draft_pending) { color_draft = t->companion.creature_colors[creature]; draft_pending = false; } }
void companion_render(const tank_t *t, uint16_t *fb, int stride) {
    prepare_draft(t);
    const companion_state_t *s = &t->companion;
    unsigned cosmetic = s->cosmetic <= 3 ? s->cosmetic : 0;
    render_rect(fb, stride, 0, 0, PAGE_W, PAGE_H, 0x102b36);
    render_rect_edge(fb, stride, 3, 3, PAGE_W - 6, PAGE_H - 6, frame_colors[cosmetic]);
    const char *title = page == 0 ? "CARE & BONDS" : page == 1 ? "DISCOVERIES" : page == 2 ? "ADVENTURES" :
                        page == 4 ? "WARDROBE" : page == 5 ? "CREATURE COLORS" : page == 6 ? "GEAR COLORS" : page == 7 ? "SAND & WATER" : "BOND HELP";
    text(fb, stride, 14, 15, frame_colors[cosmetic], title);
    button(fb, stride, 346, 8, 90, "BACK");
    render_text(fb, stride, 14, 55, 1, 0xa7c4ba, "Your aquarium, your pace.");
    char line[64];
    if (page == 0) {
        button(fb, stride, 12, 86, 205, s->cozy ? "CARE: COZY" : "CARE: CLASSIC");
        button(fb, stride, 231, 86, 205, s->captions ? "CAPTIONS: ON" : "CAPTIONS: OFF");
        button(fb, stride, 12, 124, 205, s->lively ? "MOOD: LIVELY" : "MOOD: CALM");
        button(fb, stride, 231, 124, 205, "BOND / TRICK HELP");
        text(fb, stride, 14, 165, 0xa7c4ba, s->cozy ? "Cozy: comfortable, no fake XP." : "Classic care. No fish ever dies.");
        if (t->n_fish) {
            int i = selected < t->n_fish ? selected : 0;
            const companion_bond_t *b = &s->active[i];
            snprintf(line, sizeof line, "%s BOND %u TRUST %.1f", t->fish[i].name, b->bond, (double)t->fish[i].trust);
            text(fb, stride, 14, 194, 0xe7f4dc, line);
            button(fb, stride, 12, 220, 126, "NEXT FISH");
            button(fb, stride, 152, 220, 132, "SAY HELLO");
            companion_favorite_label(t, i, line, sizeof line);
            render_text(fb, stride, 14, 250, 1, 0xffd998, line);
            render_text(fb, stride, 14, 268, 1, 0xa7c4ba, "Hello: +5 bond every 30 seconds.");
            static const char *const tricks[] = {"DART 10", "BUBBLE 25", "FOLLOW 50"};
            for (int j = 0; j < 3; j++) button(fb, stride, 12 + 146 * j, 284, 132, tricks[j]);
        }
    } else if (page == 7) {
        snprintf(line, sizeof line, "SAND: %s", render_palette_name(true, t->sand_style));
        button(fb, stride, 12, 100, 424, line);
        snprintf(line, sizeof line, "WATER: %s", render_palette_name(false, t->water_style));
        button(fb, stride, 12, 160, 424, line);
        text(fb, stride, 14, 218, 0xa7c4ba, "Upgrade palettes in Tank > Shop.");
    } else if (page == 1) {
        snprintf(line, sizeof line, "DISCOVERIES %u/12   GAME GOLD %u/6", bits(s->discoveries), bits(s->game_challenges));
        text(fb, stride, 14, 91, 0xe7f4dc, line);
        for (int j = 0; j < 4; j++) {
            int id = leaf * 4 + j;
            snprintf(line, sizeof line, "%s %s", s->discoveries & (1u << id) ? "*" : "-", COMPANION_DISCOVERIES[id]);
            text(fb, stride, 14, 121 + j * 24, s->discoveries & (1u << id) ? 0xe7f4dc : 0x809b9a, line);
        }
        button(fb, stride, 304, 204, 132, "NEXT PAGE");
        for (int j = 0; j < 3; j++) {
            static const char *const names[] = {"NATURE", "FRIEND", "STORY"};
            static const unsigned masks[] = {0x3f, 0xc0, 0x700}, totals[] = {6, 2, 3};
            snprintf(line, sizeof line, "%s %u/%u %s", names[j], bits(s->discoveries & masks[j]), totals[j],
                     s->collections & (1u << j) ? "OWNED" : companion_collection_ready(t, j) ? "CLAIM" : "FIND");
            button(fb, stride, 12, 242 + j * 30, 282, line);
        }
        button(fb, stride, 304, 272, 132, s->collections ? "FRAME >" : "LOCKED");
        button(fb, stride, 304, 242, 132, s->decor ? "SHELF ON" : "SHELF OFF");
        static const char *const frames[] = {"DEFAULT", "NATURE", "FRIEND", "STORY"};
        text(fb, stride, 304, 310, frame_colors[cosmetic], frames[cosmetic]);
        if (!notice[0]) render_text(fb, stride, 14, 344, 1, 0xa7c4ba, s->collections ? "FRAME > cycles owned frames." : "Claim a full set for a frame.");
    } else if (page == 2) {
        int a = s->adventure ? s->adventure - 1 : story;
        text(fb, stride, 14, 92, 0xa7c4ba, "Fish leaves; returns on its own.");
        text(fb, stride, 14, 117, 0xa7c4ba, "Safe away: no hunger or aging.");
        text(fb, stride, 14, 151, 0xe7f4dc, COMPANION_ADVENTURES[a]);
        if (s->adventure) {
            /* Name and timer stay left of the portrait frame (x 330..434, y 136..200). */
            unsigned left = companion_adventure_total(s, a) - s->adventure_s;
            snprintf(line, sizeof line, "%s %uh %02um %s", s->portrait_name,
                     left / 3600, left % 3600 / 60, s->away ? "LEFT" : "HOME");
            text(fb, stride, 14, 181, 0xe7f4dc, line);
            /* A copied portrait of the traveller's look, not the fish itself. */
            fish_t portrait; memset(&portrait, 0, sizeof portrait);
            portrait.stage = STAGE_ADULT; portrait.variety = s->portrait_variety; portrait.pattern = s->portrait_pattern;
            portrait.wearable = s->portrait_wearable;
            portrait.wear_colors = s->portrait_colors;
            portrait.color = s->portrait_body; portrait.fin = tank_fin_for(s->portrait_body);
            portrait.accent = s->portrait_accent; portrait.size = 1;
            render_rect(fb, stride, 331, 137, 102, 62, 0x0b2029);
            render_fish_portrait(fb, stride, 382, 168, 0.85f, &portrait, t->clock);
            render_rect_edge(fb, stride, 330, 136, 104, 64, frame_colors[cosmetic]);
        } else {
            snprintf(line, sizeof line, "%uh away: %d coins + %d XP", COMPANION_ADVENTURE_SECONDS[a] / 3600, 10 * (a + 1), 30 * (a + 1));
            text(fb, stride, 14, 181, 0xe7f4dc, line);
        }
        if (s->adventure_done & (1u << a)) {
            static const char *const finds[] = {"lantern shell", "pearl bookmark", "star stone"};
            snprintf(line, sizeof line, "%s: %s", finds[a], s->adventure_routes & (1u << a) ? "in bubbles!" : "by an arch!");
            text(fb, stride, 14, 212, 0xffd998, line);
        }
        else text(fb, stride, 14, 212, 0xa7c4ba, "Route changes postcard only.");
        button(fb, stride, 12, 244, 132, "NEXT STORY");
        button(fb, stride, 158, 244, 278, s->away ? "FISH IS AWAY" : s->adventure ? "CLAIM POSTCARD" :
               s->adventure_claimed & (1u << story) ? "ALREADY COLLECTED" : confirm_departure ? "CONFIRM SEND" : "SEND FISH");
        if (t->n_fish) {
            int i = selected < t->n_fish ? selected : 0;
            snprintf(line, sizeof line, "%s: %s%s", s->adventure ? (s->away ? "AWAY" : "HOME") : "FISH",
                     s->adventure ? s->portrait_name : t->fish[i].name, s->adventure ? "" : " >");
            button(fb, stride, 12, 285, 278, line);
            button(fb, stride, 304, 285, 132, s->adventure_choice ? "BUBBLES" : "VIA ARCH");
        }
        render_text(fb, stride, 14, 323, 1, 0xa7c4ba, "Synced time, else powered-on.");
    } else if (page == 4 && t->n_fish) {
        int i = selected < t->n_fish ? selected : 0;
        const companion_bond_t *b = &s->active[i];
        fish_t preview = t->fish[i]; preview.wearable = (uint8_t)wardrobe;
        snprintf(line, sizeof line, "DRESS UP: %s", preview.name);
        text(fb, stride, 14, 94, 0xe7f4dc, line);
        button(fb, stride, 304, 86, 132, "NEXT FISH");
        render_fish_portrait(fb, stride, 224, 170, 1.8f, &preview, t->clock);
        button(fb, stride, 12, 166, 114, "COLORS");
        button(fb, stride, 12, 220, 66, "<");
        text(fb, stride, 108, 228, 0xffd998, WEAR_NAMES[wardrobe]);
        button(fb, stride, 370, 220, 66, ">");
        bool owned = (b->wearables & (1u << (wardrobe - 1))) != 0;
        if (owned) snprintf(line, sizeof line, "%s / YOU: %ld SD",
                            b->wearable == wardrobe ? "EQUIPPED" : "OWNED", (long)t->sd_balance);
        else snprintf(line, sizeof line, "LV %u / %u SD / YOU: %ld",
                      WEAR_LEVELS[wardrobe], WEAR_PRICES[wardrobe], (long)t->sd_balance);
        text(fb, stride, 14, 260, 0xa7c4ba, line);
        button(fb, stride, 12, 286, 205, b->wearable == wardrobe ? "EQUIPPED" :
               owned ? "EQUIP FREE" : journal_level(t) < WEAR_LEVELS[wardrobe] ? "LEVEL LOCKED" : "BUY + EQUIP");
        button(fb, stride, 231, 286, 205, "REMOVE");
        text(fb, stride, 14, 322, 0xa7c4ba, "Cosmetic. Owned by this fish.");
    } else if (page == 5 || (page == 6 && t->n_fish)) {
        bool gear = page == 6;
        int i = selected < t->n_fish ? selected : 0;
        snprintf(line, sizeof line, "%s", gear ? "GEAR COLORS" : CREATURE_NAMES[creature]);
        text(fb, stride, 14, 94, 0xe7f4dc, line);
        button(fb, stride, 304, 86, 132, gear ? "BACK" : "NEXT TYPE");
        if (gear) {
            fish_t preview = t->fish[i]; preview.wearable = (uint8_t)wardrobe; preview.wear_colors = color_draft;
            render_fish_portrait(fb, stride, 224, 165, 1.5f, &preview, t->clock);
        } else render_creature_portrait(fb, stride, 224, 165, creature, color_draft, t->clock);
        snprintf(line, sizeof line, "PRIMARY: %s >", COSMETIC_COLOR_NAMES[color_draft & 15]);
        button(fb, stride, 12, 220, 424, line);
        snprintf(line, sizeof line, "DETAIL: %s >", COSMETIC_COLOR_NAMES[color_draft >> 4]);
        button(fb, stride, 12, 254, 424, line);
        button(fb, stride, 12, 294, 205, "SAVE COLORS");
        button(fb, stride, 231, 294, 205, "NATURAL");
        text(fb, stride, 14, 328, 0xa7c4ba, gear ? "Free. Colors follow this fish." :
             creature == CREATURE_SHRIMP ? "Free. All shrimp share colors." : "Free. No changes until SAVE.");
    } else {
        static const char *const help[] = {
            "BOND: friendship, 0 to 100.",
            "Hello adds 5; never fades.",
            "TRUST: care confidence, 0 to 10.",
            "Food and calm care build trust.",
            "Bond does not change trust/XP.",
            "Tricks unlock at bond 10/25/50:",
            "DART: a quick playful swim.",
            "BUBBLE: visit the bubble column.",
            "FOLLOW: swim with a tankmate.",
            "Close CARE to watch a trick."
        };
        for (int i = 0; i < 10; i++) text(fb, stride, 14, 91 + 23 * i, 0xe7f4dc, help[i]);
        button(fb, stride, 304, 324, 132, "BACK");
    }
    if (notice[0]) render_text(fb, stride, 14, 344, 1, 0xffd998, notice);
}
bool companion_tap(tank_t *t, float frame_x, float frame_y) {
    prepare_draft(t);
    const companion_state_t *s = &t->companion;
    float x = render_page_x(frame_x), y = render_page_y(frame_y);
    if (x >= 346 && x < 436 && y >= 8 && y < 38) {
        if (page == 6) { page = 4; return false; }
        if (page != section) { page = section; return false; }
        return true;
    }
    if (page == 0) {
        if (y >= 86 && y < 116 && x >= 12 && x < 217)
            result(companion_set_options(t, !t->companion.cozy, t->companion.captions, t->companion.lively));
        else if (y >= 86 && y < 116 && x >= 231 && x < 436)
            result(companion_set_options(t, t->companion.cozy, !t->companion.captions, t->companion.lively));
        else if (y >= 124 && y < 154 && x >= 12 && x < 217)
            result(companion_set_options(t, t->companion.cozy, t->companion.captions, !t->companion.lively));
        else if (y >= 124 && y < 154 && x >= 231 && x < 436) { page = 3; notice[0] = 0; }
        else if (y >= 220 && y < 250 && x >= 12 && x < 138 && t->n_fish) selected = (selected + 1) % t->n_fish;
        else if (y >= 220 && y < 250 && x >= 152 && x < 284) {
            int i = selected < t->n_fish ? selected : 0;
            if (t->companion.active[i].next_greet_s > t->companion.powered_s)
                snprintf(notice, sizeof notice, "Hello ready in %lus.", (unsigned long)(t->companion.active[i].next_greet_s - t->companion.powered_s));
            else snprintf(notice, sizeof notice, "%s", companion_greet(t, i) ? "Hello! +5 bond. Trust unchanged." : "Save failed. Hello not recorded.");
        }
        else if (y >= 284 && y < 314) for (int j = 0; j < 3; j++)
            if (x >= 12 + 146 * j && x < 144 + 146 * j) {
                static const unsigned need[] = {10, 25, 50};
                static const char *const effects[] = {"Dart! Close CARE to watch.", "Bubbles! Close CARE to watch.", "Follow! Close CARE to watch."};
                int i = selected < t->n_fish ? selected : 0;
                if (t->companion.active[i].bond < need[j]) snprintf(notice, sizeof notice, "Locked: needs %u bond. Say hello.", need[j]);
                else if (j == 2 && t->n_fish < 2) snprintf(notice, sizeof notice, "Follow needs a tankmate at home.");
                else snprintf(notice, sizeof notice, "%s", companion_trick(t, i, j) ? effects[j] : "Save failed. Trick not started.");
            }
    } else if (page == 7) {
        if (x >= 12 && x < 436 && ((y >= 100 && y < 130) || (y >= 160 && y < 190))) {
            bool sand = y < 130;
            if (!(t->sd_unlocks & (sand ? SD_ITEM_SAND : SD_ITEM_LIGHTS))) {
                snprintf(notice, sizeof notice, "Buy the palette upgrade in Shop.");
            } else {
                uint8_t before = sand ? t->sand_style : t->water_style;
                if (sand) t->sand_style = (before + 1) % SAND_STYLE_COUNT;
                else t->water_style = (before + 1) % WATER_STYLE_COUNT;
                bool ok = progression_save(t);
                if (!ok) { if (sand) t->sand_style = before; else t->water_style = before; }
                result(ok);
            }
        }
    } else if (page == 5 || (page == 6 && t->n_fish)) {
        bool gear = page == 6;
        int i = selected < t->n_fish ? selected : 0;
        if (y >= 86 && y < 116 && x >= 304 && x < 436) {
            if (gear) page = 4;
            else { creature = (creature + 1) % CREATURE_COUNT; color_draft = s->creature_colors[creature]; }
            notice[0] = 0;
        } else if (x >= 12 && x < 436 && y >= 220 && y < 250) {
            color_draft = (uint8_t)((color_draft & 240) | ((color_draft + 1) & 15)); notice[0] = 0;
        } else if (x >= 12 && x < 436 && y >= 254 && y < 284) {
            color_draft = (uint8_t)(color_draft + 16); notice[0] = 0;
        } else if (x >= 12 && x < 217 && y >= 294 && y < 324) {
            snprintf(notice, sizeof notice, "%s", companion_colors(t, gear, gear ? i : creature, color_draft) ?
                     "Colors saved. No coins spent." : "Save failed. Nothing changed.");
        } else if (x >= 231 && x < 436 && y >= 294 && y < 324) {
            color_draft = 0; snprintf(notice, sizeof notice, "Natural preview. Tap SAVE.");
        }
    } else if (page == 4 && t->n_fish) {
        int i = selected < t->n_fish ? selected : 0;
        if (y >= 86 && y < 116 && x >= 304 && x < 436) {
            selected = (i + 1) % t->n_fish; notice[0] = 0;
        } else if (y >= 166 && y < 196 && x >= 12 && x < 126) {
            page = 6; color_draft = s->active[i].wear_colors; notice[0] = 0;
        } else if (y >= 220 && y < 250 && ((x >= 12 && x < 78) || (x >= 370 && x < 436))) {
            wardrobe = 1 + (wardrobe - 1 + (x < 78 ? WEAR_COUNT - 2 : 1)) % (WEAR_COUNT - 1);
            notice[0] = 0;
        } else if (y >= 286 && y < 316 && x >= 12 && x < 436) {
            int choice = x < 217 ? wardrobe : x >= 231 ? WEAR_NONE : -1;
            if (choice < 0) return false;
            bool buy = choice && !(t->companion.active[i].wearables & (1u << (choice - 1)));
            if (s->version < 2 && s->adventure) snprintf(notice, sizeof notice, "Claim your old postcard first.");
            else if (buy && journal_level(t) < WEAR_LEVELS[choice])
                snprintf(notice, sizeof notice, "Unlocks at level %u.", WEAR_LEVELS[choice]);
            else if (buy && t->sd_balance < WEAR_PRICES[choice])
                snprintf(notice, sizeof notice, "Needs %u sand dollars.", WEAR_PRICES[choice]);
            else snprintf(notice, sizeof notice, "%s", companion_wearable(t, i, choice) ?
                          (choice ? "Saved. Looking good!" : "Removed. Still owned.") : "Save failed. Nothing changed.");
        }
    } else if (page == 1) {
        if (x >= 304 && x < 436 && y >= 204 && y < 234) leaf = (leaf + 1) % 3;
        else if (x >= 12 && x < 294 && y >= 242 && y < 332) {
            int set = (int)(y - 242) / 30;
            if (t->companion.collections & (1u << set)) snprintf(notice, sizeof notice, "Owned! Use FRAME > to equip.");
            else if (!companion_collection_ready(t, set)) snprintf(notice, sizeof notice, "Find missing discoveries first.");
            else result(companion_collection_claim(t, set));
        }
        else if (x >= 304 && x < 436 && y >= 272 && y < 302) {
            if (!t->companion.collections) {
                snprintf(notice, sizeof notice, "Frames: claim a completed set.");
                return false;
            }
            int next = t->companion.cosmetic;
            do { next = (next + 1) % 4; } while (next && !(t->companion.collections & (1u << (next - 1))));
            result(companion_cosmetic(t, next));
        }
        else if (x >= 304 && x < 436 && y >= 242 && y < 272) result(companion_set_decor(t, !t->companion.decor));
    } else if (page == 2) {
        if (x >= 12 && x < 144 && y >= 244 && y < 274 && !t->companion.adventure) { story = (story + 1) % 3; confirm_departure = false; }
        else if (x >= 158 && x < 436 && y >= 244 && y < 274) {
            if (t->companion.away) snprintf(notice, sizeof notice, "Safe away. Returns on its own.");
            else if (t->companion.adventure) result(companion_adventure_claim(t));
            else if (t->companion.adventure_claimed & (1u << story)) snprintf(notice, sizeof notice, "Postcard already collected.");
            else if (!confirm_departure) { confirm_departure = true; snprintf(notice, sizeof notice, "CONFIRM SEND to leave the tank."); }
            else { result(companion_adventure_start(t, story, selected < t->n_fish ? selected : 0)); confirm_departure = false; }
        }
        else if (x >= 12 && x < 290 && y >= 285 && y < 315 && t->n_fish && !t->companion.adventure) { selected = (selected + 1) % t->n_fish; confirm_departure = false; }
        else if (x >= 304 && x < 436 && y >= 285 && y < 315 && !t->companion.adventure)
            { confirm_departure = false; result(companion_adventure_choice(t, !t->companion.adventure_choice)); }
    } else if (x >= 304 && x < 436 && y >= 324 && y < 354) page = 0;
    return false;
}
void companion_render_decor(const tank_t *t, uint16_t *fb, int stride) {
    const companion_state_t *s = &t->companion;
    if (!s->decor || !(s->collections || s->game_challenges)) return;
    /* PAGE coordinates scale to a tiny sand-corner shelf on every CYD frame.
     * Public rectangles participate in the scene renderer's dirty mask. */
    render_rect(fb, stride, 10, 344, 86, 4, 0x8f7758);
    for (int i = 0; i < 3; i++) if (s->collections & (1u << i)) {
        int x = 16 + 25 * i; uint32_t color = frame_colors[i + 1];
        render_rect(fb, stride, x + 3, 325, 12, 4, color);
        render_rect(fb, stride, x, 329, 18, 8, color);
        render_rect(fb, stride, x + 3, 337, 12, 5, color);
        render_rect(fb, stride, x + 8, 328, 2, 12, 0x4c5963);
    }
    for (int i = 0; i < 6; i++) if (s->game_challenges & (1u << i))
        render_rect(fb, stride, 12 + i * 14, 350, 8, 5, 0xffd998);
}
