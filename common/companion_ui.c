#include "companion.h"
#include "render.h"
#include "progression.h"
#include "setup.h"
#include "weather.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static int page = COMPANION_SECTION_CARE, section = COMPANION_SECTION_CARE;
static int selected, leaf, story;
static bool draft_pending;
static bool confirm_departure;
static bool rpg_chapter_finished;
static int wardrobe = WEAR_TOP_HAT;
static int creature;
static uint8_t color_draft;
static char notice[48];
static int market_rank;
static fish_sale_t market_sale;
static char market_sale_name[FISH_NAME_MAX + 1];
static int market_creature;
static creature_sale_t creature_sale;
static int creature_sale_parent = COMPANION_SECTION_CREATURE_MARKET;
static fish_sale_t trade_sale;
static fish_auction_t auction_result;
static char trade_name[FISH_NAME_MAX + 1];
static int trade_parent = COMPANION_SECTION_CONTRACT;
int companion_variety_at(int rank) {
    for (int id = 0; id < FISH_VARIETY_COUNT; id++)
        if (MARKET_VARIETY_TIERS[id] == rank) return id;
    return 0;
}
void companion_select_fish(int fish) { selected = fish >= 0 ? fish : 0; }
int companion_selected_fish(void) { return selected; }
int companion_current_section(void) { return page; }
static const uint32_t frame_colors[] = {0x83b8ac, 0x8ed9a5, 0xf0aeca, 0xc0b1ff};
static void text(uint16_t *fb, int stride, int x, int y, uint32_t rgb, const char *s) {
    render_text(fb, stride, x, y, render_text_w(s, 2) <= PAGE_W - x - 12 ? 2 : 1, rgb, s);
}
static void key_face(uint16_t *fb, int stride, int x, int y, int w) {   /* a bevelled key: lit lip, shadowed foot */
    render_button(fb, stride, x, y, w, 30, 0x183a43, 0x739c99, "", 1);
}
static void button(uint16_t *fb, int stride, int x, int y, int w, const char *s) {
    key_face(fb, stride, x, y, w);
    int scale = render_text_w(s, 2) <= w - 16 ? 2 : 1;
#ifdef RENDER_MENU_TESTING
    extern void menu_test_button(int x, int y, int w, int h, const char *label, int scale);
    menu_test_button(x, y, w, 30, s, scale);
#endif
    render_text(fb, stride, x + 8, y + (scale == 2 ? 7 : 11), scale, 0xe7f4dc, s);
}
static void market_button(uint16_t *fb, int stride, int x, int y, int w, const char *label) {
    render_button(fb, stride, x, y, w, 42, 0x183a43, 0x739c99, "", 1);
    int scale = render_text_w(label, 2) <= w - 16 ? 2 : 1;
#ifdef RENDER_MENU_TESTING
    extern void menu_test_button(int x, int y, int w, int h, const char *label, int scale);
    menu_test_button(x, y, w, 42, label, scale);
#endif
    render_text(fb, stride, x + 8, y + (scale == 2 ? 13 : 17), scale, 0xe7f4dc, label);
}
static void stamp(uint16_t *fb, int stride, int x, int y, int kind, bool found);
static void illustrated_button(uint16_t *fb, int stride, int x, int y, int w,
                               const char *label, int icon, bool found) {
    key_face(fb, stride, x, y, w);
    stamp(fb, stride, x + 8, y + 7, icon, found);
    int scale = render_text_w(label, 2) <= w - 42 ? 2 : 1;
#ifdef RENDER_MENU_TESTING
    extern void menu_test_button(int x, int y, int w, int h, const char *label, int scale);
    menu_test_button(x + 26, y, w - 26, 30, label, scale);
#endif
    render_text(fb, stride, x + 34, y + (scale == 2 ? 7 : 11), scale, 0xe7f4dc, label);
}
static unsigned bits(unsigned value) {
    unsigned n = 0; for (; value; value >>= 1) n += value & 1u; return n;
}
static void bond_progress_text(const tank_t *t, const companion_bond_t *bond,
                               char *line, size_t size) {
    static const char *const tricks[] = {"DART", "BUBBLES", "FOLLOW"};
    static const unsigned need[] = {10, 25, 50};
    if (progression_creative(t)) {
        snprintf(line, size, "CREATIVE: ALL TRICKS OPEN.");
        return;
    }
    for (int i = 0; i < 3; i++) if (bond->bond < need[i]) {
        unsigned hellos = (need[i] - bond->bond + 4) / 5;
        snprintf(line, size, "%s: %u BOND / %u HELLO%s.",
                 tricks[i], need[i], hellos, hellos == 1 ? "" : "S");
        return;
    }
    snprintf(line, size, "ALL TRICKS OPEN.");
}
static void life_progress_text(const tank_t *t, int fish, char *line, size_t size) {
    static const char *const next_stage[] = {"JUVENILE", "ADULT", "ELDER"};
    static const unsigned threshold[] = {STAGE_JUV_AGE, STAGE_ADULT_AGE, STAGE_ELDER_AGE};
    const fish_t *f = &t->fish[fish];
    if (f->stage >= STAGE_ELDER) {
        snprintf(line, size, "ELDER: NO NEXT STAGE. AGE CONTINUES WITH TIME.");
        return;
    }
    unsigned stage = (unsigned)f->stage;
    float age = progression_age_s(t, fish);
    if (!(age >= 0)) age = 0;
    float remaining = age < threshold[stage] ? threshold[stage] - age : 0;
    unsigned hours = (unsigned)ceilf(remaining / 3600);
    snprintf(line, size, "NEXT: %s IN %u GROWTH-HOUR%s; SLEEP IS SLOWER.",
             next_stage[stage], hours, hours == 1 ? "" : "S");
}
/* Shared 16x16, four-color sprites; scenery stays static and needs no cache. */
static const char story_art[8][16][17] = {
    {
        "................", "................", "......2222......", "....22333322....",
        "...2331331332...", "..233133113332..", ".23311331133332.", ".23111331113332.",
        ".23111331113332.", "..231133113332..", "...2313311332...", "....23333332....",
        ".....222222.....", "......2332......", ".....222222.....", "................"
    }, {
        "...22......22...", "..2332....2332..", "..2332....2332..", "...232....232...",
        "....232..232....", "...2332222332...", "..232233332232..", "..232.2332.232..",
        "...22.2332.22...", "......2332......", ".....233332.....", "....23311332....",
        "....232..232....", "...2332..2332...", "..222222222222..", "................"
    }, {
        ".......2........", "......232.......", ".....23332......", "....2331332.....",
        "...233111332....", "..23311111332...", ".2331111111332..", "..23111111332...",
        "...231111332....", "....2311332.....", ".....23332......", "......232.......",
        "......232.......", ".....23332......", "....2222222.....", "................"
    }, {
        ".......22.......", "......2332......", ".......22.......", ".....222222.....",
        "....23333332....", "....23111332....", "....23111332....", "....23111332....",
        "....23111332....", "....23111332....", "....23333332....", ".....222222.....",
        "......2332......", ".....233332.....", "....22222222....", "................"
    }, {
        "....22222222....", "...2333333332...", "..233222222332..", ".2332......2332.",
        ".232........232.", ".232........232.", ".232........232.", ".232........232.",
        ".232........232.", ".232........232.", ".232........232.", ".232........232.",
        ".232........232.", ".2332......2332.", "222222....222222", "................"
    }, {
        "................", "....222..222....", "...2333223332...", "..233333333332..",
        "..233113311332..", "..231111111132..", "..231111111132..", "...2311111132...",
        "...2331111332...", "....23311332....", ".....233332.....", "......2332......",
        ".......22.......", "......2332......", ".....222222.....", "................"
    }, {
        "................", "..22222..22222..", ".23333322333332.", ".23111322311132.",
        ".23111322311132.", ".23333322333332.", ".23111322311132.", ".23111322311132.",
        ".23333322333332.", ".23111322311132.", ".23111322311132.", ".23333322333332.",
        ".22222222222222.", ".......22.......", ".......22.......", "................"
    }, {
        "................", ".......22.......", "......2332......", "......2332......",
        ".....233332.....", ".22222333322222.", "..233333333332..", "...2333113332...",
        "....23111132....", "...2331111332...", "...2333333332...", "..23332..23332..",
        "..2332....2332..", "..222......222..", "................", "................"
    }
};
enum { STAMP_HEART, STAMP_EYE, STAMP_LEAF, STAMP_STAR, STAMP_BOOK, STAMP_FISH,
       STAMP_FAMILY, STAMP_GENES, STAMP_COIN, STAMP_FLAG, STAMP_CLOCK, STAMP_SHELL };
static void stamp(uint16_t *fb, int stride, int x, int y, int kind, bool found) {
    static const uint8_t pixels[][8] = {
        {0x00,0x66,0xff,0xff,0x7e,0x3c,0x18,0x00},
        {0x00,0x3c,0x66,0xdb,0xdb,0x66,0x3c,0x00},
        {0x03,0x0f,0x1f,0x3e,0x7c,0x78,0x50,0x80},
        {0x18,0x18,0xff,0x7e,0x3c,0x7e,0x66,0x42},
        {0x00,0x7e,0xdb,0xdb,0xdb,0xdb,0x7e,0x18},
        {0x00,0x02,0x6f,0xff,0xfd,0x6f,0x02,0x00},
        {0x66,0x66,0x24,0x3c,0x18,0x18,0x3c,0x3c},
        {0x42,0x66,0x3c,0x18,0x18,0x3c,0x66,0x42},
        {0x3c,0x66,0xdb,0xcb,0xd3,0xdb,0x66,0x3c},
        {0x40,0x7e,0x7c,0x78,0x40,0x40,0x40,0xe0},
        {0x3c,0x66,0xc3,0xcb,0xcf,0xc3,0x66,0x3c},
        {0x3c,0x7e,0xdb,0xdb,0xdb,0x7e,0x3c,0x18}
    };
    uint32_t color = found ? 0xffd998 : 0x607e87;
    for (int row = 0; row < 8; row++)
        for (int col = 0; col < 8; col++)
            if (pixels[kind][row] & (0x80u >> col))
                render_rect(fb, stride, x + col * 2, y + row * 2, 2, 2, color);
}
static void story_sprite(uint16_t *fb, int stride, int x, int y, int theme, int scale, bool found) {
    static const uint32_t colors[8][3] = {
        {0xffedba, 0xa5696d, 0xf4b395}, {0xa6e6b8, 0x326654, 0x68b888},
        {0xe1ffff, 0x487b9c, 0x86cbd5}, {0xfff5c0, 0x977047, 0xffd477},
        {0xd7ead7, 0x556879, 0x9eb4b3}, {0xffe2bc, 0x986a91, 0xe4a0bb},
        {0xffedc0, 0x617b9c, 0x9fcad3}, {0xfff2ba, 0x997c51, 0xe7bd72}
    };
    for (int row = 0; row < 16; row++)
        for (int col = 0; col < 16;) {
            char pixel = story_art[theme][row][col];
            int end = col + 1;
            while (end < 16 && story_art[theme][row][end] == pixel) end++;
            if (pixel != '.')
                render_rect(fb, stride, x + col * scale, y + row * scale,
                            (end - col) * scale, scale, found ? colors[theme][pixel - '1'] : 0x526b75);
            col = end;
        }
}
static void story_scene(uint16_t *fb, int stride, int x, int y,
                        int w, int h, int chapter, const fish_t *fish, bool bubbles) {
    static const uint32_t water[6] = {0x123d50, 0x123e36, 0x253f58, 0x182c4d, 0x29374a, 0x153b4b};
    int theme = chapter < 6 ? chapter : 5;
    render_rect(fb, stride, x, y, w, h, water[theme]);
    for (int j = 0; j < 4; j++) {
        int sx = x + 12 + j * (w - 24) / 4;
        render_rect(fb, stride, sx, y + 5, 2, h - 12, 0x31535d);
        render_rect(fb, stride, sx + 2, y + h - 16, 5, 3, 0x51897d);
        render_rect(fb, stride, sx - 4, y + h - 25, 5, 4, 0x51897d);
        render_rect(fb, stride, sx + 2, y + h - 35, 6, 4, 0x3f736c);
    }
    render_rect(fb, stride, x + 1, y + h - 8, w - 2, 7, 0x8e947a);
    render_rect(fb, stride, x + w / 3, y + h - 6, w / 3, 5, 0xbfc09a);
    int scale = h >= 56 ? 3 : 2;
    int ax = x + w / 2 - 8 * scale, ay = y + h - 8 - 16 * scale;
    story_sprite(fb, stride, ax, ay, theme, scale, true);
    if (bubbles) {
        for (int j = 0; j < 3; j++)
            render_ring(fb, stride, x + w - 20 - 5 * j, y + 12 + 12 * j, 3, 0xa8e1e0);
    } else {
        render_rect(fb, stride, x + w - 36, y + 12, 24, 5, 0x617e85);
        render_rect(fb, stride, x + w - 36, y + 17, 5, h - 24, 0x617e85);
        render_rect(fb, stride, x + w - 17, y + 17, 5, h - 24, 0x617e85);
    }
    if (fish) render_fish_portrait(fb, stride, x + 38, y + h / 2, 0.7f, fish, 0);
    render_rect_edge(fb, stride, x, y, w, h, 0x719ba2);
}
static void story_prompt(uint16_t *fb, int stride, const char *prompt) {
    size_t capacity = (420 - render_text_w("W", 1)) /
        (render_text_w("WW", 1) - render_text_w("W", 1)) + 1;
    for (int y = 184; *prompt && y < 218; y += 16) {
        size_t length = strlen(prompt), count = length < capacity ? length : capacity;
        if (length > count) {
            while (count && prompt[count] != ' ') count--;
            if (!count) count = capacity;
        }
        char line[128];
        memcpy(line, prompt, count); line[count] = 0;
        render_text(fb, stride, 14, y, 1, 0xe7f4dc, line);
        prompt += count; while (*prompt == ' ') prompt++;
    }
}
static void fish_heading(const tank_t *t, int fish, uint16_t *fb, int stride, const char *detail) {
    render_rect(fb, stride, 12, 82, 278, 36, 0x183b47);
    render_fish_portrait(fb, stride, 40, 100, 0.6f, &t->fish[fish], 0);
    render_text(fb, stride, 72, 86, 1, 0xe7f4dc, t->fish[fish].name);
    render_text(fb, stride, 72, 103, 1, 0xa7c4ba, detail);
}
static void rpg_render(const tank_t *t, uint16_t *fb, int stride) {
    const tank_rpg_t *r = &t->rpg;
    char line[128];
    int i = selected < t->n_fish ? selected : 0;
    if (page == COMPANION_SECTION_RPG_EVENT) {
        render_text(fb, stride, 14, 92, 2, 0xffd998, "YOUR STORY IS SAVED");
        render_text_wrap(fb, stride, 14, 132, 420, 0xe7f4dc, companion_rpg_outcome(t));
        story_scene(fb, stride, 14, 170, 420, 52,
                    rpg_chapter_finished && r->chapter ? r->chapter - 1 : r->chapter,
                    t->n_fish ? &t->fish[i] : NULL, r->outcome <= 3);
        snprintf(line, sizeof line, "COURAGE %u / CURIOSITY %u / KIND %u",
                 r->skills[0], r->skills[1], r->skills[2]);
        render_text(fb, stride, 14, 226, 1, 0xffd998, line);
        render_text_wrap(fb, stride, 14, 256, 420, 0xa7c4ba, rpg_chapter_finished ?
                         "Chapter and its guide recorded. Rewards saved once." :
                         "Your fish is safe at home. Continue whenever you like.");
        button(fb, stride, 12, 304, 424, "CONTINUE STORY");
        return;
    }
    if (page == COMPANION_SECTION_RPG_HELP) {
        static const char *const help[] = {
            "A saved story. Entirely optional.",
            "Choose a fish, then an approach.",
            "Courage, curiosity, kindness grow.",
            "Bonds and tricks offer help.",
            "Genes and family add new context.",
            "Decor, visitors and trips matter.",
            "No purchase or trait is required.",
            "Fish never leave or get hurt.",
            "No care stats or settings change.",
            "Each choice saves. BACK is safe.",
            "Chapter prizes are earned once.",
            "The family can continue later."
        };
        for (int j = 0; j < 12; j++)
            render_text(fb, stride, 14, 88 + 21 * j, 1, 0xe7f4dc, help[j]);
        return;
    }
    if (page == COMPANION_SECTION_RPG_JOURNAL) {
        snprintf(line, sizeof line, "COURAGE %u / CURIOSITY %u / KIND %u",
                 r->skills[0], r->skills[1], r->skills[2]);
        render_text(fb, stride, 14, 86, 1, 0xffd998, line);
        for (int chapter = 0; chapter < 6; chapter++) {
            int y = 112 + chapter * 31;
            bool done = chapter < r->chapter;
            snprintf(line, sizeof line, "%d %s", chapter + 1, companion_rpg_chapter_title(chapter));
            story_sprite(fb, stride, 14, y, chapter, 1, done || chapter == r->chapter);
            render_text(fb, stride, 38, y, 1, done ? 0xe7f4dc : 0x809b9a, line);
            if (done) {
                const char *name = companion_story_name(t, r->contributors[chapter]);
                snprintf(line, sizeof line, "%.12s: %s", name && *name ? name : "EARLIER FISH",
                         companion_rpg_route(t, chapter));
            } else snprintf(line, sizeof line, "%s", chapter == r->chapter ? "YOUR NEXT CHAPTER" : "UNEXPLORED");
            render_text(fb, stride, 38, y + 16, 1, done ? 0xffd998 : 0x809b9a, line);
        }
        button(fb, stride, 12, 310, 205, "HOW TO PLAY");
        button(fb, stride, 231, 310, 205, "CONTINUE STORY");
        return;
    }
    if (t->n_fish) {
        snprintf(line, sizeof line, "GUIDE: %.15s", t->fish[i].name);
        render_text(fb, stride, 14, 90, 1, 0xe7f4dc, line);
        button(fb, stride, 304, 82, 132, "NEXT FISH");
    } else render_text(fb, stride, 14, 90, 1, 0xe7f4dc, "A FISH AT HOME CAN GUIDE YOU.");
    button(fb, stride, 12, 118, 132, "LOG / HELP");
    story_scene(fb, stride, 154, 116, 282, 46, r->chapter,
                t->n_fish ? &t->fish[i] : NULL, r->step == 1);
    if (r->chapter < 6) snprintf(line, sizeof line, "CHAPTER %u/6", r->chapter + 1u);
    else snprintf(line, sizeof line, "COMPLETE");
    render_text(fb, stride, 14, 150, 1, 0xa7c4ba, line);
    render_text(fb, stride, 14, 166, 1, 0xffd998, companion_rpg_chapter_name(t));
    story_prompt(fb, stride, companion_rpg_prompt(t));
    if (r->chapter < 6 && t->n_fish) {
        for (int choice = 0; choice < 3; choice++) {
            companion_rpg_choice_label(t, choice, line, sizeof line);
            render_button(fb, stride, 12, 232 + choice * 36, 424, 30,
                          0x183a43, 0x739c99, "", 1);
            stamp(fb, stride, 20, 239 + choice * 36,
                  choice == 0 ? STAMP_FLAG : choice == 1 ? STAMP_EYE : STAMP_HEART, true);
            render_text(fb, stride, 46, 243 + choice * 36, 1, 0xe7f4dc, line);
        }
    } else if (r->chapter >= 6) {
        render_text_wrap(fb, stride, 14, 240, 420, 0xffd998, companion_rpg_outcome(t));
        button(fb, stride, 12, 304, 424, "READ YOUR CAMPAIGN JOURNAL");
    } else render_text_wrap(fb, stride, 14, 240, 420, 0xa7c4ba, "Return a fish from the Bank or wait for your traveler. The story waits without penalty.");
    snprintf(line, sizeof line, "COURAGE %u / CURIOSITY %u / KIND %u", r->skills[0], r->skills[1], r->skills[2]);
    render_text(fb, stride, 14, 346, 1, 0xa7c4ba, line);
}
static void paragraph_at(uint16_t *fb, int stride, int x, int y, int width, uint32_t rgb, const char *s) {
    size_t capacity = (width - render_text_w("W", 1)) /
        (render_text_w("WW", 1) - render_text_w("W", 1)) + 1;
    if (capacity > 70) capacity = 70;
    while (*s && y < 338) {
        size_t n = strlen(s), count = n < capacity ? n : capacity;
        if (n > count) {
            while (count && s[count] != ' ') count--;
            if (!count) count = capacity;
        }
        char line[71]; memcpy(line, s, count); line[count] = 0;
        render_text(fb, stride, x, y, 1, rgb, line);
        s += count; while (*s == ' ') s++;
        y += 16;
    }
}
static void paragraph(uint16_t *fb, int stride, int y, uint32_t rgb, const char *s) {
    paragraph_at(fb, stride, 14, y, 420, rgb, s);
}
static const char *who(const tank_t *t, uint16_t id) {
    const char *name = companion_story_name(t, id);
    return name && *name ? name : id ? "AN EARLIER FISH" : "UNKNOWN";
}
static void family_render(const tank_t *t, int i, uint16_t *fb, int stride) {
    char line[96];
    if (!t->n_fish) { text(fb, stride, 14, 94, 0xe7f4dc, "No fish at home."); return; }
    const fish_lineage_t *l = &t->story.active[i];
    uint16_t id = t->polish.active[i].id <= 0xffff ? (uint16_t)t->polish.active[i].id : 0;
    snprintf(line, sizeof line, "GENERATION %u", l->generation + 1u);
    fish_heading(t, i, fb, stride, line);
    button(fb, stride, 304, 84, 132, "NEXT FISH");
    if (!l->parent[0] && !l->parent[1])
        snprintf(line, sizeof line, "PARENTS: %s", l->generation ? "NOT RECORDED" : "A FOUNDER OF THIS TANK");
    else if (!l->parent[1] || !l->parent[0]) snprintf(line, sizeof line, "PARENT: %s", who(t, l->parent[0] ? l->parent[0] : l->parent[1]));
    else snprintf(line, sizeof line, "PARENTS: %s + %s", who(t, l->parent[0]), who(t, l->parent[1]));
    stamp(fb, stride, 14, 124, STAMP_FAMILY, !!(l->parent[0] || l->parent[1]));
    paragraph_at(fb, stride, 40, 124, 396, 0xffd998, line);
    int n = snprintf(line, sizeof line, "GRANDPARENTS:");
    int grand = 0;
    for (int k = 0; k < 2; k++) {
        const fish_lineage_t *pl = companion_story_line(t, l->parent[k]);
        if (pl) for (int g = 0; g < 2; g++) if (pl->parent[g] && n < (int)sizeof line - 12) {
            n += snprintf(line + n, sizeof line - n, "%s%s", grand ? ", " : " ", who(t, pl->parent[g])); grand++;
        }
    }
    if (!grand) snprintf(line + n, sizeof line - n, " NOT RECORDED");
    stamp(fb, stride, 14, 172, STAMP_BOOK, grand > 0);
    paragraph_at(fb, stride, 40, 172, 396, 0xa7c4ba, line);
    int children = 0, siblings = 0;
    n = snprintf(line, sizeof line, "CHILDREN:");
    for (int k = 0; k < N_FISH_MAX + FISH_BANK_CAP + 1; k++) {
        const fish_lineage_t *c = k < N_FISH_MAX ? (k < t->n_fish ? &t->story.active[k] : NULL) :
            k < N_FISH_MAX + FISH_BANK_CAP ? (k - N_FISH_MAX < t->bank_count ? &t->story.bank[k - N_FISH_MAX] : NULL) :
            t->companion.away ? &t->story.traveler : NULL;
        if (!c || c == l) continue;
        const char *name = k < N_FISH_MAX ? t->fish[k].name : k < N_FISH_MAX + FISH_BANK_CAP ? t->bank[k - N_FISH_MAX].name : t->companion.traveler.name;
        if (id && (c->parent[0] == id || c->parent[1] == id)) {
            if (children < 3) n += snprintf(line + n, sizeof line - n, "%s%s", children ? ", " : " ", name);
            children++;
        }
        if ((l->parent[0] || l->parent[1]) &&
            ((l->parent[0] && (c->parent[0] == l->parent[0] || c->parent[1] == l->parent[0])) ||
             (l->parent[1] && (c->parent[0] == l->parent[1] || c->parent[1] == l->parent[1])))) siblings++;
    }
    if (!children) snprintf(line + n, sizeof line - n, " NONE YET");
    else if (children > 3) snprintf(line + n, sizeof line - n, " +%d", children - 3);
    stamp(fb, stride, 14, 220, STAMP_FISH, children > 0);
    paragraph_at(fb, stride, 40, 220, 396, 0xe7f4dc, line);
    snprintf(line, sizeof line, "SIBLINGS: %d / BORN HERE: %u", siblings, t->story.births);
    render_text(fb, stride, 14, 272, 1, 0xa7c4ba, line);
    if (companion_rpg_legacy(t, i))
        render_text(fb, stride, 14, 288, 1, 0xffd998, "THIS FAMILY KNOWS THE LOST CURRENT.");
}
static void genes_render(const tank_t *t, int i, uint16_t *fb, int stride) {
    if (!t->n_fish) { text(fb, stride, 14, 94, 0xe7f4dc, "No fish at home."); return; }
    const fish_lineage_t *l = &t->story.active[i];
    char line[160];
    fish_heading(t, i, fb, stride, "INHERITED LOOKS");
    button(fb, stride, 304, 84, 132, "NEXT FISH");
    snprintf(line, sizeof line, "MARKING: %s", FISH_PATTERN_NAMES[t->fish[i].pattern]);
    render_text(fb, stride, 14, 124, 1, 0xe7f4dc, line);
    int n;
    n = snprintf(line, sizeof line, "GENES:");
    int shown = 0;
    for (int k = 0; k < TRAIT_COUNT; k++) if (l->traits & (1u << k))
        n += snprintf(line + n, sizeof line - n, "%s%s", shown++ ? ", " : " ", FISH_TRAIT_NAMES[k]);
    if (!shown) n += snprintf(line + n, sizeof line - n, " CLASSIC");
    paragraph(fb, stride, 154, l->mutation ? 0xffd998 : 0xe7f4dc, line);
    n = snprintf(line, sizeof line, "CARRIES:");
    int carried = 0;
    if (l->pattern2 && l->pattern2 - 1 != t->fish[i].pattern && l->pattern2 <= FISH_PATTERN_COUNT)
        n += snprintf(line + n, sizeof line - n, " %s", FISH_PATTERN_NAMES[l->pattern2 - 1]), carried++;
    if (l->color2) n += snprintf(line + n, sizeof line - n, "%sA HIDDEN COLOUR", carried++ ? ", " : " ");
    for (int k = 0; k < TRAIT_COUNT; k++) if (l->traits & (16u << k))
        n += snprintf(line + n, sizeof line - n, "%s%s", carried++ ? ", " : " ", FISH_TRAIT_NAMES[k]);
    if (!carried) snprintf(line + n, sizeof line - n, " NOTHING HIDDEN");
    paragraph(fb, stride, 202, 0xa7c4ba, line);
    if (l->mutation) {
        snprintf(line, sizeof line, "BIRTH MUTATION: %s%s%s",
                 l->mutation & GENE_MUTANT_PATTERN ? "PATTERN " : "",
                 l->mutation & GENE_MUTANT_TRAIT ? "TRAIT " : "",
                 l->mutation & GENE_MUTANT_COLOR ? "BODY COLOR" : "");
        paragraph(fb, stride, 264, 0xffd998, line);
    }
    paragraph(fb, stride, l->mutation ? 308 : 264, 0xa7c4ba,
              "Hidden genes pass to fry. Recessive traits need two copies.");
}
static void polish_render(const tank_t *t, uint16_t *fb, int stride) {
    const tank_polish_t *p = &t->polish;
    char line[80], date[40];
    int i = selected < t->n_fish ? selected : 0;
    if (page == COMPANION_SECTION_REEF) {
        text(fb, stride, 14, 88, 0xffd998, t->reef_flow_s > 0 ? "CURRENT FLOWING" :
             t->reef_flow_cooldown > 0 ? "CURRENT RESTING" : t->night ? "READY IN DAYLIGHT" : "CURRENT READY");
        paragraph(fb, stride, 126, 0xe7f4dc,
                  "Swipe sideways across a quarter of the tank in daylight. Current: 12 seconds. Cooldown: 30 seconds.");
        paragraph(fb, stride, 200, 0xa7c4ba,
                  "Optional play: nearby fish may gain calm and curiosity. No chores, penalties or daily duties.");
        paragraph(fb, stride, 274, 0xa7c4ba,
                  "The bubbler is an enrichment toy for curious fish, not oxygen or water quality. Fish do not need it to survive.");
    } else if (page == COMPANION_SECTION_SYMBOLS) {
        static const char *const headings[] = {
            "READING YOUR FISH", "PERSONALITY, NOT A SCORE", "FOLIAGE & CARE"
        };
        static const char *const lines[3][6] = {
            {"FOOD / FULLNESS: MORE IS BETTER.",
             "BOLT / ENERGY: MORE IS BETTER.",
             "STRESS: LESS IS BETTER.",
             "HEART / TRUST: MORE IS BETTER.",
             "BARS SHOW CURRENT NEEDS.",
             "FEEDING AND CALM CARE BUILD TRUST."},
            {"BOLDNESS: SHY <-> BOLD.",
             "SOCIAL: SOLO <-> SOCIAL.",
             "CURIOSITY: CAUTIOUS <-> CURIOUS.",
             "NEITHER END IS GOOD OR BAD.",
             "? = NOT YET OBSERVED. WATCH THEM.",
             "BORDER COLOR IDENTIFIES YOUR FISH."},
            {"FOLIAGE: PERMANENT, NO TRIMMING.",
             "SWORD PLANT: A CALM SHELTER.",
             "SNAIL: CLEANS GLASS, CALM +10%.",
             "SHRIMP FORAGE WITHOUT GLASS CHORES.",
             "ARROW: NEXT. BACK: FISH MENU.",
             "MAIN MENU: TANK, GAMES, SETTINGS."}
        };
        text(fb, stride, 14, 92, 0xffd998, headings[leaf]);
        for (int j = 0; j < 6; j++)
            render_text(fb, stride, 14, 128 + j * 24, 1, 0xe7f4dc, lines[leaf][j]);
        snprintf(line, sizeof line, "SYMBOLS %d/3", leaf + 1);
        render_text(fb, stride, 14, 290, 1, 0xa7c4ba, line);
        button(fb, stride, 12, 312, 205, "< PREVIOUS");
        button(fb, stride, 231, 312, 205, "NEXT >");
    } else if (page == COMPANION_SECTION_LIFE) {
        if (!t->n_fish) text(fb, stride, 14, 94, 0xe7f4dc, "No fish at home. Check Bank / away.");
        else {
            const fish_t *f = &t->fish[i]; const fish_life_t *life = &p->active[i];
            static const char *const stages[] = {"FRY", "JUVENILE", "ADULT", "ELDER"};
            fish_heading(t, i, fb, stride, stages[f->stage]);
            button(fb, stride, 304, 86, 132, "NEXT FISH");
            if (life->arrived_s == UINT32_MAX) snprintf(line, sizeof line, "Arrival predates this journal.");
            else snprintf(line, sizeof line, "Joined: powered tank hour %lu.", (unsigned long)(life->arrived_s / 3600));
            render_text(fb, stride, 14, 125, 1, 0xa7c4ba, line);
            const char *buddy = life->buddy ? "A FORMER TANKMATE" : "GETTING ACQUAINTED";
            for (int j = 0; j < t->n_fish; j++) if (p->active[j].id == life->buddy) buddy = t->fish[j].name;
            for (int j = 0; j < t->bank_count; j++) if (p->bank[j].id == life->buddy) buddy = t->bank[j].name;
            if (t->companion.away && p->traveler.id == life->buddy) buddy = t->companion.traveler.name;
            snprintf(line, sizeof line, "BUDDY: %s", buddy);
            render_text(fb, stride, 14, 153, 1, 0xffd998, line);
            render_text(fb, stride, 14, 173, 1, 0xa7c4ba,
                        life->friend_s >= 600 ? "Close companions" : life->friend_s >= 30 ? "Familiar companions" : "A friendship takes quiet time.");
            life_progress_text(t, i, line, sizeof line);
            paragraph(fb, stride, 198, 0xe7f4dc, line);
            int spot = tank_favorite_decor(t, i), love = tank_fish_favorite_item(t, i);
            bool present = love >= 0 && tank_item_in_tank(t, love);
            snprintf(line, sizeof line, "LOVES: %s", love < 0 ? "OPEN WATER" : SD_ITEMS[love].name);
            stamp(fb, stride, 14, 226, STAMP_LEAF, present);
            render_text(fb, stride, 40, 230, 1, present ? 0x8ed9a5 : 0xffb48a, line);
            render_text(fb, stride, 14, 248, 1, 0xa7c4ba, present ?
                        "IN TANK: CALMER, GROWS 10% FASTER" : "MISSING: LESS CALM, 10% SLOWER");
            snprintf(line, sizeof line, "REST: %.14s", spot < 0 ? "OPEN WATER" : SD_ITEMS[spot].name);
            render_text(fb, stride, 14, 266, 1, 0xa7c4ba, line);
            snprintf(line, sizeof line, "GROWTH x%.2f",
                     (double)(progression_fish_growth_factor(t, i) * progression_food_growth_multiplier(t)));
            render_text(fb, stride, 432 - render_text_w(line, 1), 266, 1, 0x8ed9a5, line);
        }
    } else if (page == COMPANION_SECTION_RECAP) {
        int memory_pages = (p->recap_count + 2) / 3;
        if (!memory_pages) memory_pages = 1;
        snprintf(line, sizeof line, "Welcome back to %.15s.", t->aquariums.name);
        render_text(fb, stride, 14, 88, 1, 0xe7f4dc, line);
        if (leaf < memory_pages) {
            if (!p->recap_count) render_text_wrap(fb, stride, 14, 121, 420, 0xa7c4ba, "No observations yet. Quiet viewing is enough.");
            for (int j = 0; j < 3; j++) {
                int k = leaf * 3 + j;
                if (k >= p->recap_count) break;
                companion_memory_text(&p->recap[k], line, sizeof line);
                paragraph(fb, stride, 119 + j * 48, 0xe7f4dc, line);
            }
        } else {
            render_text_wrap(fb, stride, 14, 120, 420, 0xa7c4ba, "Snail: keeps the glass clear; calm recovers 10% faster.");
            snprintf(line, sizeof line, "Shrimp: %ld meals foraged.", (long)t->shrimp_eaten);
            render_text_wrap(fb, stride, 14, 160, 420, 0xa7c4ba, line);
            render_text_wrap(fb, stride, 14, 200, 420, 0xa7c4ba, "Urchin: night grazer; resting fish settle 20% faster.");
            snprintf(line, sizeof line, "Seahorse: +%.1f shared trust.", (double)t->sh_trust_passed);
            render_text_wrap(fb, stride, 14, 234, 420, 0xa7c4ba, line);
        }
        snprintf(line, sizeof line, "NEXT / HELPERS (%d/%d)", leaf + 1, memory_pages + 1);
        button(fb, stride, 12, 270, 424, line);
    } else if (page == COMPANION_SECTION_IDENTITY) {
        render_tank_emblem(fb, stride, 14, 86, p->emblem);
        render_text(fb, stride, 46, 90, 1, 0xe7f4dc, t->aquariums.name);
        button(fb, stride, 12, 116, 424, "RENAME TANK");
        snprintf(line, sizeof line, "SUBTITLE: %s", p->subtitle ? companion_subtitle(p->subtitle) : "NONE");
        button(fb, stride, 12, 154, 424, line);
        snprintf(line, sizeof line, "EMBLEM: %s", companion_emblem(p->emblem));
        button(fb, stride, 12, 192, 424, line);
        companion_date_text(p->founded_unix, date, sizeof date);
        snprintf(line, sizeof line, "Founded: %s", date);
        render_text(fb, stride, 14, 234, 1, 0xa7c4ba, line);
        companion_date_text(p->backup_unix, date, sizeof date);
        snprintf(line, sizeof line, "Last SD backup: %s", p->backups ? date : "NOT RECORDED");
        render_text(fb, stride, 14, 251, 1, 0xa7c4ba, line);
        uint8_t flags = t->story.flags;
        button(fb, stride, 12, 270, 424, flags & STORY_SEASONS_OFF ? "SEASON ACCENTS: OFF" : "SEASON ACCENTS: AUTO");
        bool south = false;
        bool located = weather_location_south(&south);
        render_text(fb, stride, 14, 312, 1, 0xa7c4ba, located ?
                    "SEASONS USE WEATHER CITY." : "SET WEATHER CITY FOR SEASONS.");
        render_text(fb, stride, 14, 335, 1, 0xa7c4ba, "NAME, BADGE & SUBTITLE.");
    } else if (page == COMPANION_SECTION_OBJECTS) {
        int item = leaf % SD_ITEM_COUNT;
        bool owned = progression_item_owned(t, item);
        render_decor_thumbnail(fb, stride, 14, 82, 32, item, owned);
        text(fb, stride, 58, 90, 0xe7f4dc, SD_ITEMS[item].name);
        render_text(fb, stride, 14, 124, 1, 0xa7c4ba, SD_ITEMS[item].words);
        render_text(fb, stride, 14, 144, 1, 0xa7c4ba, SD_ITEMS[item].words2);
        text(fb, stride, 14, 166, 0xffd998, owned ? "OWNED / SHOP PURCHASE" : "AVAILABLE IN TANK > SHOP");
        int date_item = item < 32 ? item : SD_IS_SCENERY(item) ? 32 + item - SD_SCENERY_FIRST : -1;
        if (date_item >= 0 && owned && p->decor_since[date_item])
            snprintf(line, sizeof line, "Bought at tank hour %lu.", (unsigned long)((p->decor_since[date_item] - 1) / 3600));
        else snprintf(line, sizeof line, "Purchase date not recorded.");
        render_text(fb, stride, 14, 202, 1, 0xa7c4ba, line);
        int fans = 0;
        for (int j = 0; j < t->n_fish; j++) if (tank_favorite_decor(t, j) == item) {
            snprintf(line, sizeof line, "Favorite of %s.", t->fish[j].name);
            render_text(fb, stride, 14, 226, 1, 0xe7f4dc, line); fans++; break;
        }
        if (!fans) render_text(fb, stride, 14, 226, 1, 0xa7c4ba, "A little piece of your tank's story.");
        button(fb, stride, 12, 258, 205, "< OBJECT");
        button(fb, stride, 231, 258, 205, "OBJECT >");
    } else if (page == COMPANION_SECTION_KEEPSAKES) {
        static const char *const keepsakes[] = {"Lantern shell", "Pearl bookmark", "Star stone"};
        static const char *const tales[] = {
            "A shell that recalls the lagoon's gentle lights.",
            "A tiny marker from a library beneath the tide.",
            "A smooth stone found under a sky of stars."
        };
        for (int j = 0; j < 3; j++) {
            bool have = t->n_fish && (p->active[i].adventures & (1u << j));
            story_sprite(fb, stride, 14, 92 + j * 62, j == 0 ? 0 : j == 1 ? 6 : 7, 2, have);
            snprintf(line, sizeof line, "%s / %s", keepsakes[j], have ? "BROUGHT HOME" : "NOT FOUND");
            render_text(fb, stride, 58, 96 + j * 62, 1, have ? 0xffd998 : 0xa7c4ba, line);
            render_text_wrap(fb, stride, 58, 116 + j * 62, 378, 0xa7c4ba, tales[j]);
        }
        render_text(fb, stride, 14, 278, 1, 0xa7c4ba, "Find more in Adventures.");
    } else if (page == COMPANION_SECTION_HINTS) {
        static const char *const hints[] = {
            "Watch a calm fish explore the bubble column.",
            "Leave room for a little unhurried exploration.",
            "Leave a reef, sword plant or decor as a calm resting place.",
            "Calm nearby fish get to know one another.",
            "Fed, rested fish sometimes make a playful dart.",
            "Reef decorations give curious fish a destination.",
            "Use My Fish > Say Hello to build a bond.",
            "Say hello, then try a bond-unlocked trick.",
            "Send a fish to Lantern lagoon; wait for its return.",
            "Send a fish to Pearl library; wait for its return.",
            "Send a fish to Starlight cove; wait for its return.",
            "Patient care gives a young fish time to grow."
        };
        int d = leaf % COMPANION_DISCOVERY_COUNT;
        text(fb, stride, 14, 94, 0xe7f4dc, COMPANION_DISCOVERIES[d]);
        story_scene(fb, stride, 14, 122, 420, 62, d < 6 ? 1 : d < 8 ? 5 : d < 11 ? (d - 8) * 2 : 0,
                    t->n_fish ? &t->fish[i] : NULL, d == 0 || d == 8);
        render_text_wrap(fb, stride, 14, 190, 420, 0xa7c4ba, hints[d]);
        render_text(fb, stride, 14, 282, 1, 0xa7c4ba, "No streaks, deadlines, or penalties.");
        button(fb, stride, 12, 240, 205, "< HINT");
        button(fb, stride, 231, 240, 205, "HINT >");
    } else if (page == COMPANION_SECTION_FAMILY) {
        family_render(t, i, fb, stride);
    } else if (page == COMPANION_SECTION_GENES) {
        genes_render(t, i, fb, stride);
    } else if (page == COMPANION_SECTION_VISITORS) {
        const tank_story_t *s = &t->story;
        for (int j = 0; j < VISITOR_SPECIES; j++) {
            bool seen = s->seen[j] > 0;
            snprintf(line, sizeof line, "%s", seen ? COMPANION_VISITORS[j].name : "???");
            render_visitor_portrait(fb, stride, 14, 82 + j * 34, j, seen);
            render_text(fb, stride, 64, 86 + j * 34, 1, seen ? 0xe7f4dc : 0x809b9a, line);
            if (seen) {
                if (s->first_seen_s[j]) snprintf(line, sizeof line, "x%u, hour %lu", s->seen[j], (unsigned long)((s->first_seen_s[j] - 1) / 3600));
                else snprintf(line, sizeof line, "x%u, pre-log", s->seen[j]);
            } else snprintf(line, sizeof line, "%s", j == VISITOR_SQUID ? "Very rare." : "Not seen yet.");
            render_text(fb, stride, 240, 92 + j * 34, 1, seen ? 0xa7c4ba : 0x809b9a, line);
        }
        if (p->visits && s->seen[s->last_species]) render_text_wrap(fb, stride, 14, 260, 420, 0xffd998, COMPANION_VISITORS[s->last_species].words);
        else render_text_wrap(fb, stride, 14, 260, 420, 0xa7c4ba, "Visitors come only while you watch.");
    } else if (page == COMPANION_SECTION_FIRSTS && t->n_fish) {
        uint32_t milestones = p->active[i].milestones;
        fish_heading(t, i, fb, stride, "LIFE FIRSTS");
        int n = 0;
        for (int j = 0; j < MS_FISH_COUNT; j++) if (milestones & (1u << j)) {
            if (n / 6 == leaf) {
                stamp(fb, stride, 14, 121 + (n % 6) * 24, STAMP_STAR, true);
                render_text(fb, stride, 40, 124 + (n % 6) * 24, 1, 0xa7c4ba, MS_NAMES[j]);
            }
            n++;
        }
        if (!n) render_text_wrap(fb, stride, 14, 124, 420, 0xa7c4ba, "Life firsts appear through patient care.");
        button(fb, stride, 304, 264, 132, "MORE");
    }
    if (page == COMPANION_SECTION_RECAP) render_text(fb, stride, 14, 344, 1, 0xa7c4ba, "Observed here. No offline stories.");
}
static void result(bool ok) {
    snprintf(notice, sizeof notice, "%s", ok ? "Saved. No hurry, ever." : "Not ready or save failed. Retry.");
}
void companion_open(void) {
    page = section = COMPANION_SECTION_SUMMARY;
    selected = leaf = story = creature = 0;
    wardrobe = WEAR_TOP_HAT;
    color_draft = 0;
    draft_pending = false;
    confirm_departure = false;
    notice[0] = 0;
    market_rank = 0;
    market_creature = 0;
    memset(&market_sale, 0, sizeof market_sale);
}
void companion_open_section(int target) {
    companion_open();
    switch (target) {
    case COMPANION_SECTION_COLLECTIONS: case COMPANION_SECTION_OBJECTS:
    case COMPANION_SECTION_KEEPSAKES: case COMPANION_SECTION_HINTS:
    case COMPANION_SECTION_FIRSTS: case COMPANION_SECTION_REWARDS:
    case COMPANION_SECTION_CREATURE_COLORS:
    case COMPANION_SECTION_CARE: case COMPANION_SECTION_TRICKS: case COMPANION_SECTION_REEF:
        target = COMPANION_SECTION_SUMMARY;
        break;
    default: break;
    }
    page = section = target;
    draft_pending = target == COMPANION_SECTION_CREATURE_COLORS;
}
void companion_open_creature(int kind) {
    companion_open_section(COMPANION_SECTION_CREATURE_MARKET);
    if (kind >= CREATURE_SHRIMP && kind <= CREATURE_MUSSEL) market_creature = kind;
}
static void prepare_draft(const tank_t *t) {
    if (draft_pending) {
        color_draft = companion_creature_colors(&t->companion, creature);
        draft_pending = false;
    }
}
static bool rpg_page(void) {
    return page == COMPANION_SECTION_RPG || page == COMPANION_SECTION_RPG_JOURNAL ||
        page == COMPANION_SECTION_RPG_HELP || page == COMPANION_SECTION_RPG_EVENT;
}
static bool information_page(void) {
    switch (page) {
    case COMPANION_SECTION_LIFE:
    case COMPANION_SECTION_RECAP:
    case COMPANION_SECTION_IDENTITY:
    case COMPANION_SECTION_OBJECTS:
    case COMPANION_SECTION_KEEPSAKES:
    case COMPANION_SECTION_HINTS:
    case COMPANION_SECTION_FIRSTS:
    case COMPANION_SECTION_FAMILY:
    case COMPANION_SECTION_VISITORS:
    case COMPANION_SECTION_REWARDS:
    case COMPANION_SECTION_GENES:
    case COMPANION_SECTION_SYMBOLS:
    case COMPANION_SECTION_REEF:
        return true;
    default:
        return false;
    }
}
static void pedigree_certificate(const tank_t *t, int fish, uint16_t *fb, int stride, int y) {
    char line[96];
    const fish_t *f = &t->fish[fish];
    const fish_lineage_t *l = t->story.version ? &t->story.active[fish] : NULL;
    int pct = progression_fish_gene_pct(t, fish), gen = l ? l->generation : 0;
    render_button(fb, stride, 12, y, 424, 148, 0x1d1a10, 0xe0b04a, "", 1);
    render_doubloon(fb, stride, 22, y + 4);
    render_text(fb, stride, 46, y + 9, 1, 0xffe29a, "PEDIGREE CERTIFICATE");
    snprintf(line, sizeof line, "%s / %s / GEN %d", FISH_VARIETY_NAMES[f->variety],
             progression_fish_grade(pct), gen);
    render_text(fb, stride, 24, y + 30, 1, 0xe7f4dc, line);
    const char *pa = l && l->parent[0] ? companion_story_name(t, l->parent[0]) : NULL;
    const char *pb = l && l->parent[1] ? companion_story_name(t, l->parent[1]) : NULL;
    if (pa || pb) snprintf(line, sizeof line, "PARENTS: %s + %s", pa ? pa : "UNKNOWN", pb ? pb : "UNKNOWN");
    else snprintf(line, sizeof line, "PARENTS: FOUNDER STOCK");
    render_text(fb, stride, 24, y + 50, 1, 0xe7f4dc, line);
    int grand = 0;
    for (int k = 0; l && k < 2; k++) {
        const fish_lineage_t *p = l->parent[k] ? companion_story_line(t, l->parent[k]) : NULL;
        for (int j = 0; p && j < 2; j++) grand += p->parent[j] && companion_story_name(t, p->parent[j]);
    }
    snprintf(line, sizeof line, "GRANDPARENTS: %d OF 4", grand);
    render_text(fb, stride, 24, y + 70, 1, 0xa7c4ba, line);
    char muts[32] = "";
    static const char *const names[3] = {"MARK", "TRAIT", "COLOR"};
    for (int k = 0; l && k < 3; k++) if (l->mutation & (1u << k))
        snprintf(muts + strlen(muts), sizeof muts - strlen(muts), "%s%s", muts[0] ? "+" : "", names[k]);
    snprintf(line, sizeof line, "GENES x%d.%02d / VIGOR %d", pct / 100, pct % 100, progression_fish_vigor(t, fish));
    render_text(fb, stride, 24, y + 90, 1, 0xd8c3f2, line);
    snprintf(line, sizeof line, "MUTATIONS: %s", muts[0] ? muts : "NONE");
    render_text(fb, stride, 24, y + 110, 1, 0xd8c3f2, line);
    bool wanted = gen > 0 && progression_demand_variety(t) == f->variety;
    snprintf(line, sizeof line, "BLOODLINE +%d%% / %s", progression_fish_pedigree_pct(t, fish),
             wanted ? "DEMAND +25%" : "NO DEMAND");
    render_text(fb, stride, 24, y + 130, 1, 0xffd998, line);
}
/* ---- breeding contracts and auction day ---- */
static const char *const CONTRACT_NEEDS[CONTRACT_NEED_COUNT] = {"FINE+ GRADE", "RARE+ GRADE", "ANY MUTATION"};
static const char *contract_reason(const tank_t *t, int fish, const fish_contract_t *c) {
    const fish_t *f = &t->fish[fish];
    if (c->filled) return "ORDER FILLED - WAIT FOR THE NEXT";
    if (f->variety != c->variety) return "WRONG VARIETY";
    if (f->stage < STAGE_ADULT) return "NOT AN ADULT YET";
    if (!t->story.version || t->story.active[fish].generation < 1) return "MUST BE BRED BY YOU";
    if (!progression_fish_sellable(t, fish)) return "KEEP YOUR LAST PAIR";
    return c->need == CONTRACT_NEED_MUTANT ? "NEEDS A MUTATION" : "GRADE TOO LOW";
}
static void trade_fish_row(const tank_t *t, uint16_t *fb, int stride, int fish) {
    static const char *const stages[] = {"FRY", "JUVENILE", "ADULT", "ELDER"};
    const fish_t *f = &t->fish[fish];
    char line[64];
    render_fish_portrait(fb, stride, 224, 196, 1.0f, f, 0);
    market_button(fb, stride, 12, 176, 70, "<");
    market_button(fb, stride, 366, 176, 70, ">");
    text(fb, stride, 14, 226, 0xe7f4dc, f->name);
    int pct = progression_fish_gene_pct(t, fish);
    snprintf(line, sizeof line, "%s / %s / %s", FISH_VARIETY_NAMES[f->variety], stages[f->stage & 3],
             progression_fish_grade(pct));
    render_text(fb, stride, 14, 252, 1, 0xa7c4ba, line);
}
static void trade_render(const tank_t *t, uint16_t *fb, int stride) {
    char line[96];
    int fish = selected < t->n_fish ? selected : 0;
    if (page == COMPANION_SECTION_TRADE_CONFIRM) {
        bool auction = trade_parent == COMPANION_SECTION_AUCTION;
        if (auction) snprintf(line, sizeof line, "ENTER %s IN THE AUCTION? QUOTE %d.", trade_name, trade_sale.value);
        else snprintf(line, sizeof line, "DELIVER %s FOR %d DOUBLOONS?", trade_name, trade_sale.value);
        render_text_wrap(fb, stride, 14, 92, 420, 0xffd998, line);
        int sf = trade_sale.fish >= 0 && trade_sale.fish < t->n_fish ? trade_sale.fish : 0;
        if (t->n_fish) pedigree_certificate(t, sf, fb, stride, 120);
        button(fb, stride, 12, 274, 205, "CANCEL");
        button(fb, stride, 231, 274, 205, auction ? "SELL AT AUCTION" : "DELIVER");
        render_text(fb, stride, 14, 322, 1, 0xa7c4ba, auction ? "BIDS ARE FINAL ONCE YOU COMMIT." :
                                                       "THE FISH LEAVES WITH THE CLIENT.");
        render_text(fb, stride, 14, 342, 1, 0xa7c4ba, "THE QUOTE IS RECHECKED ON SAVE.");
        return;
    }
    if (page == COMPANION_SECTION_AUCTION_RESULT) {
        snprintf(line, sizeof line, "SOLD: %s", trade_name);
        text(fb, stride, 14, 88, 0xffd998, line);
        for (int i = 0; i < PROGRESSION_AUCTION_BIDS; i++) {
            bool last = i == PROGRESSION_AUCTION_BIDS - 1;
            int y = 120 + i * 30;
            render_button(fb, stride, 12, y, 424, 24, last ? 0x2a2a12 : 0x10282e, last ? 0xe0b04a : 0x435f61, "", 1);
            render_text(fb, stride, 22, y + 8, 1, last ? 0xffe29a : 0xe7f4dc,
                        PROGRESSION_TRADE_CLIENTS[auction_result.bidder[i] % PROGRESSION_TRADE_CLIENT_COUNT]);
            snprintf(line, sizeof line, "%s%d", last ? "SOLD " : "", auction_result.bids[i]);
            render_text(fb, stride, 426 - render_text_w(line, 1), y + 8, 1, last ? 0xffe29a : 0xa7c4ba, line);
        }
        int pct = trade_sale.value > 0 ? (int)((long)auction_result.hammer * 100 / trade_sale.value) : 100;
        snprintf(line, sizeof line, "HAMMER %d = %d%% OF QUOTE", auction_result.hammer, pct);
        render_text(fb, stride, 14, 280, 1, pct >= 100 ? 0x8ed9a5 : 0xffd998, line);
        market_button(fb, stride, 12, 306, 424, "DONE");
        return;
    }
    bool contract = page == COMPANION_SECTION_CONTRACT;
    fish_contract_t c = {0};
    if (contract) {
        if (!progression_contract_today(t, &c)) {
            paragraph(fb, stride, 100, 0xe7f4dc, progression_creative(t) ?
                      "Creative mode has no prices, so visitors place no orders." : "No visitors today.");
            return;
        }
        snprintf(line, sizeof line, "ORDER: %s", FISH_VARIETY_NAMES[c.variety]);
        text(fb, stride, 14, 86, 0xffd998, line);
        snprintf(line, sizeof line, "BRED ADULT, %s", CONTRACT_NEEDS[c.need]);
        render_text(fb, stride, 14, 114, 1, 0xe7f4dc, line);
        snprintf(line, sizeof line, "%s / %d DAY%s LEFT", PROGRESSION_TRADE_CLIENTS[c.client], c.days_left,
                 c.days_left == 1 ? "" : "S");
        render_text(fb, stride, 14, 134, 1, 0xa7c4ba, c.filled ? "FILLED - NEW ORDER SOON" : line);
        snprintf(line, sizeof line, "PAYS QUOTE x1.6 + %d", PROGRESSION_CONTRACT_BONUS);
        render_text(fb, stride, 14, 154, 1, 0xd8c3f2, line);
    } else {
        int days = progression_auction_days(t);
        if (progression_creative(t)) snprintf(line, sizeof line, "NO AUCTIONS IN CREATIVE");
        else if (progression_auction_sold(t)) snprintf(line, sizeof line, "THIS WEEK'S LOT IS SOLD");
        else if (days) snprintf(line, sizeof line, "NEXT AUCTION: %d DAY%s", days, days == 1 ? "" : "S");
        else snprintf(line, sizeof line, progression_auction_open(t) ? "AUCTION DAY: OPEN" : "AUCTION CLOSED");
        text(fb, stride, 14, 86, 0xffd998, line);
        render_text(fb, stride, 14, 114, 1, 0xe7f4dc, "SATURDAYS: RARE+ OR 2ND-GEN ADULT");
        render_text(fb, stride, 14, 134, 1, 0xa7c4ba, "RIVALS BID 95-165% OF THE QUOTE");
        render_text(fb, stride, 14, 154, 1, 0xd8c3f2, "COMMIT FIRST - BIDS ARE FINAL");
    }
    if (!t->n_fish) {
        paragraph(fb, stride, 190, 0xa7c4ba, "No fish at home to offer.");
        return;
    }
    trade_fish_row(t, fb, stride, fish);
    bool ok;
    if (contract) {
        ok = progression_contract_fits(t, fish);
        if (ok) snprintf(line, sizeof line, "FITS THE ORDER: PAYS %d", progression_contract_pay(t, fish));
        else snprintf(line, sizeof line, "%s", contract_reason(t, fish, &c));
    } else {
        ok = progression_auction_eligible(t, fish);
        if (ok) snprintf(line, sizeof line, "QUOTE %d / BIDS FROM %d", progression_fish_value(t, fish),
                         progression_fish_value(t, fish) * 80 / 100);
        else if (progression_auction_sold(t)) snprintf(line, sizeof line, "BACK NEXT SATURDAY");
        else if (!progression_auction_open(t)) snprintf(line, sizeof line, "OPENS ON AUCTION DAY");
        else if (!progression_fish_sellable(t, fish)) snprintf(line, sizeof line, "KEEP YOUR LAST PAIR");
        else snprintf(line, sizeof line, "NEEDS RARE+ OR 2ND GENERATION");
    }
    render_text(fb, stride, 14, 270, 1, ok ? 0x8ed9a5 : 0xffd998, line);
    market_button(fb, stride, 12, 290, 424, contract ? (ok ? "FILL THIS ORDER" : "CHOOSE A FITTING FISH") :
                                            (ok ? "ENTER THIS LOT" : "NOT ELIGIBLE"));
    render_text(fb, stride, 14, 344, 1, 0xa7c4ba, contract ? "NEW ORDERS EVERY 3 DAYS." : "ONE LOT EACH WEEK.");
}
static bool trade_tap(tank_t *t, float x, float y) {
    if (page == COMPANION_SECTION_TRADE_CONFIRM) {
        if (y < 274 || y >= 304) return true;
        if (x >= 12 && x < 217) { page = trade_parent; notice[0] = 0; }
        else if (x >= 231 && x < 436) {
            if (trade_parent == COMPANION_SECTION_AUCTION) {
                if (progression_auction_sell(t, &trade_sale, &auction_result)) {
                    page = COMPANION_SECTION_AUCTION_RESULT; selected = 0; notice[0] = 0;
                } else { page = trade_parent; snprintf(notice, sizeof notice, "%s", progression_shop_notice()); }
            } else {
                bool done = progression_contract_fill(t, &trade_sale);
                page = trade_parent; selected = 0;
                snprintf(notice, sizeof notice, "%s", done ? "Order delivered and saved." : progression_shop_notice());
            }
        }
        return true;
    }
    if (page == COMPANION_SECTION_AUCTION_RESULT) {
        if (x >= 12 && x < 436 && y >= 306 && y < 348) { page = COMPANION_SECTION_AUCTION; notice[0] = 0; }
        return true;
    }
    if (!t->n_fish) return true;
    int fish = selected < t->n_fish ? selected : 0;
    if (y >= 176 && y < 218 && ((x >= 12 && x < 82) || (x >= 366 && x < 436))) {
        selected = (fish + t->n_fish + (x < 82 ? -1 : 1)) % t->n_fish; notice[0] = 0;
    } else if (x >= 12 && x < 436 && y >= 290 && y < 332) {
        bool contract = page == COMPANION_SECTION_CONTRACT;
        bool ok = contract ? progression_contract_quote(t, fish, &trade_sale) : progression_auction_quote(t, fish, &trade_sale);
        if (ok) {
            snprintf(trade_name, sizeof trade_name, "%s", t->fish[fish].name);
            trade_parent = page; page = COMPANION_SECTION_TRADE_CONFIRM; notice[0] = 0;
        } else snprintf(notice, sizeof notice, "%s", contract ? "This fish does not fit the order." :
                                                         "This fish cannot enter the auction.");
    }
    return true;
}
static void market_render(const tank_t *t, uint16_t *fb, int stride) {
    char line[96];
    int fish = selected < t->n_fish ? selected : 0;
    if (page == COMPANION_SECTION_MARKET || page == COMPANION_SECTION_RESIDENTS ||
        page == COMPANION_SECTION_NURSERY) {
        market_button(fb, stride, 12, 80, 132, "BUY STOCK");
        market_button(fb, stride, 158, 80, 132, "RESIDENTS");
        market_button(fb, stride, 304, 80, 132, "NURSERY");
    }
    if (page == COMPANION_SECTION_CREATURE_SALE) {
        snprintf(line, sizeof line, "SELL %s FOR %d DOUBLOONS?",
                 creature_sale.species == CREATURE_SHRIMP ? "SHRIMP SCHOOL" : CREATURE_NAMES[creature_sale.species],
                 creature_sale.value);
        render_text_wrap(fb, stride, 14, 96, 420, 0xffd998, line);
        snprintf(line, sizeof line, "COLORS: %s",
                 CREATURE_RARITY_NAMES[progression_creature_rarity_of(t, creature_sale.species)]);
        text(fb, stride, 14, 150, 0xe7f4dc, line);
        paragraph(fb, stride, 180, 0xa7c4ba,
                  "It leaves this aquarium. A new one rolls fresh colors at purchase. The quote is rechecked before saving.");
        button(fb, stride, 12, 274, 205, "CANCEL");
        button(fb, stride, 231, 274, 205, "CONFIRM SALE");
        return;
    }
    if (page == COMPANION_SECTION_CREATURE_MARKET) {
        int kind = market_creature;
        bool owned = progression_creature_adults(t, kind) > 0;
        render_creature_portrait(fb, stride, 224, 118, kind,
                                 owned ? companion_creature_colors(&t->companion, kind) : 0, 0);
        market_button(fb, stride, 12, 96, 70, "<");
        market_button(fb, stride, 366, 96, 70, ">");
        snprintf(line, sizeof line, "%s / %s", kind == CREATURE_SHRIMP ? "SHRIMP SCHOOL" : CREATURE_NAMES[kind],
                 owned ? "OWNED" : "NOT OWNED");
        text(fb, stride, 14, 152, 0xffd998, line);
        if (owned) {
            int r = progression_creature_rarity_of(t, kind);
            static const char *const mult[CREATURE_RARITY_COUNT] = {"x1.0", "x1.4", "x2.0", "x3.5"};
            snprintf(line, sizeof line, "COLORS: %s %s", CREATURE_RARITY_NAMES[r], mult[r]);
        } else snprintf(line, sizeof line, "COLORS ROLLED AT PURCHASE");
        render_text(fb, stride, 14, 186, 1, owned ? 0xd8c3f2 : 0xa7c4ba, line);
        snprintf(line, sizeof line, "%s", progression_creature_status(t, kind));
        render_text(fb, stride, 14, 210, 1, 0xa7c4ba, line);
        snprintf(line, sizeof line, owned ? "OWNED - SELL TO REROLL" : "BUY FOR %d DOUBLOONS",
                 progression_creature_price(t, kind));
        market_button(fb, stride, 12, 242, 424, line);
        if (owned) snprintf(line, sizeof line, "SELL FOR +%d DOUBLOONS", progression_creature_value(t, kind, false));
        else snprintf(line, sizeof line, "NOTHING TO SELL");
        market_button(fb, stride, 12, 290, 424, line);
        render_text(fb, stride, 14, 344, 1, 0xa7c4ba, "RARER COLORS RESELL FOR MORE.");
        return;
    }    if (page == COMPANION_SECTION_SALE) {
        snprintf(line, sizeof line, "SELL %s FOR %d DOUBLOONS?",
                 market_sale_name, market_sale.value);
        render_text_wrap(fb, stride, 14, 92, 420, 0xffd998, line);
        int sf = market_sale.fish >= 0 && market_sale.fish < t->n_fish ? market_sale.fish : 0;
        if (t->n_fish) pedigree_certificate(t, sf, fb, stride, 120);
        button(fb, stride, 12, 274, 205, "CANCEL");
        button(fb, stride, 231, 274, 205, "CONFIRM SALE");
        render_text(fb, stride, 14, 322, 1, 0xa7c4ba, "PER GEN: +2%, RARE+ LINES +6%.");
        render_text(fb, stride, 14, 342, 1, 0xa7c4ba, "THE QUOTE IS RECHECKED ON SAVE.");
        return;
    }
    if (page == COMPANION_SECTION_NURSERY) {
        int a, b;
        render_text_wrap(fb, stride, 14, 138, 420, 0xffd998, progression_breeding_status(t));
        if (progression_breeding_pair(t, &a, &b)) {
            snprintf(line, sizeof line, "%s + %s / %s", t->fish[a].name, t->fish[b].name,
                     FISH_VARIETY_NAMES[t->fish[a].variety]);
            render_text(fb, stride, 14, 182, 1, 0xe7f4dc, line);
            render_fish_portrait(fb, stride, 116, 220, 1, &t->fish[a], 0);
            render_fish_portrait(fb, stride, 320, 220, 1, &t->fish[b], 0);
        }
        paragraph(fb, stride, 258, 0xa7c4ba,
                  "Two healthy adults of the SAME VARIETY, room and player-fed meals. Fry inherit family looks; rare mutations can occur.");
        market_button(fb, stride, 12, 320, 424, "FAMILY & INHERITANCE");
        return;
    }
    if (page == COMPANION_SECTION_RESIDENTS) {
        if (!t->n_fish) {
            paragraph(fb, stride, 150, 0xe7f4dc, "No fish at home. Buy stock or return a fish from the Bank.");
        } else {
            const fish_t *f = &t->fish[fish];
            static const char *const stages[] = {"FRY", "JUVENILE", "ADULT", "ELDER"};
            render_fish_portrait(fb, stride, 224, 163, 1.1f, f, 0);
            market_button(fb, stride, 12, 140, 70, "<");
            market_button(fb, stride, 366, 140, 70, ">");
            text(fb, stride, 14, 202, 0xffd998, f->name);
            snprintf(line, sizeof line, "%s / %s", FISH_VARIETY_NAMES[f->variety], stages[f->stage & 3]);
            render_text(fb, stride, 14, 230, 1, 0xe7f4dc, line);
            snprintf(line, sizeof line, "SELL FOR %d DOUBLOONS", progression_fish_value(t, fish));
            market_button(fb, stride, 12, 268, 424, line);
        }
        market_button(fb, stride, 12, 320, 424, "CREATURE MARKET");
        return;
    }
    int variety = companion_variety_at(market_rank);
    fish_market_offer_t offer;
    progression_market_offer(t, variety, &offer);
    fish_t preview = {0};
    progression_market_preview(t, variety, &preview);
    render_fish_portrait(fb, stride, 224, 163, 1.1f, &preview, 0);
    market_button(fb, stride, 12, 140, 70, "<");
    market_button(fb, stride, 366, 140, 70, ">");
    snprintf(line, sizeof line, "%s / ADULT / %d DOUBLOONS", FISH_VARIETY_NAMES[variety], offer.price);
    text(fb, stride, 14, 202, 0xffd998, line);
    const char *blocked = progression_market_status(t);
    snprintf(line, sizeof line, "%ld DOUBLOONS IN YOUR WALLET", (long)t->sd_balance);
    render_text(fb, stride, 14, 232, 1, 0xe7f4dc, line);
    snprintf(line, sizeof line, "DEMAND: BRED %s +%d%%", FISH_VARIETY_NAMES[progression_demand_variety(t)], PROGRESSION_DEMAND_PCT);
    render_text(fb, stride, 14, 250, 1, blocked || progression_demand_variety(t) == variety ? 0xffd998 : 0xa7c4ba,
                blocked ? blocked : line);
    market_button(fb, stride, 12, 268, 424, "BUY THIS VARIETY");
    market_button(fb, stride, 12, 320, 424, "CREATURE MARKET");
}
/* ---- Help: Main Menu > Help, a selectable guide to every part of the game ---- */
typedef struct { const char *title; int icon; const char *body[2]; } help_topic_t;
static const help_topic_t HELP_TOPICS[] = {
    {"GETTING STARTED", STAMP_STAR, {
        "Your aquarium begins with two Tetras. Swipe down from the top of the glass to drop food; tap a fish for its summary. MENU opens Fish, Tank, Market, Games, Casino, Help and the Settings cog.",
        "Doubloons are the only currency. Earn them from games, fish and creature sales, and a pearl mussel; spend them on stock, decor, foliage, food and themes."}},
    {"FISH NEEDS", STAMP_HEART, {
        "TRUST grows with meals and gentle holds. ENERGY drains while swimming and refills with rest. FULLNESS falls over time; feed when it runs low. CALM drops when startled.",
        "Calm is cultivated, not given: a newcomer settles only part way, and each point of trust raises how calm it can become. Cover, Cozy Shelter and its favorite piece help it settle."}},
    {"COMFORT & FAVORITES", STAMP_LEAF, {
        "Every fish loves one decoration or foliage set, shown as LOVES on its summary. With that piece placed, calm recovers 30% faster, settles a quarter higher and the fish grows 10% faster.",
        "Without it the fish stays a little restless and grows 10% slower. Store pieces instead of selling them to switch favorites quickly."}},
    {"GROWTH & FOOD", STAMP_FISH, {
        "Fry become juveniles after 2 growth hours, adults after 12 and elders after 96. Asleep or away, a tank grows at a quarter of awake speed.",
        "Permanent diets multiply growth: Pellets 1x, Flakes 1.25x (250), Bloodworms 1.5x (750). Favorite decor adds or removes 10%; hybrid vigor adds up to 10%."}},
    {"BREEDING", STAMP_FAMILY, {
        "Two healthy adults or elders of the SAME variety can breed when there is room and you have fed them. Varieties never cross. Name each new fry with the keyboard; SUGGEST rolls a fresh name.",
        "Fry inherit one allele of each gene from each parent. Pair carriers of hidden traits to reveal them. Check Fish > Family to follow a line."}},
    {"GENETICS & VIGOR", STAMP_GENES, {
        "Each fish has two marking alleles, two body-color alleles and four recessive traits. A trait shows only with two copies; one copy is carried silently.",
        "VIGOR (0-10) counts mixed alleles and carried traits. Diverse lines grow faster and sell higher. Rare birth mutations change a marking, trait or color."}},
    {"MARKET PRICES", STAMP_COIN, {
        "Value = life stage x variety tier x care x genes. Care uses trust, energy, fullness and calm (60-100%). Genes add 4% per vigor, 15% per shown trait, 25% per mutation and 10% for a fancy marking.",
        "Grades: COMMON, FINE (125%), RARE (150%), PRIZED (190%). Market stock is plain and costs more than its resale, so profit comes from breeding and raising."}},
    {"CONTRACTS & AUCTIONS", STAMP_COIN, {
        "Market > Contracts: every 3 days a visitor orders one variety you have unlocked, bred by you, at FINE+ or RARE+ grade or with a mutation. Filling it pays the quote x1.6 + 50, once per order.",
        "Market > Auction: on Saturdays one RARE+ or 2nd-generation adult can be sold to rival bidders. Commit first; the hammer lands at 95-165% of the quote. One lot a week."}},
    {"CREATURES", STAMP_SHELL, {
        "Market > Creature Market trades every creature. They do not breed: one snail, urchin, seahorse, pearl mussel and shrimp school per tank. Colors roll at purchase: NATURAL, TINTED (x1.4), TWO-TONE (x2) or PRIZED (x3.5) resale.",
        "Shrimp eat fallen food. The snail clears the glass (calm recovers 10% faster); the urchin grazes at night (resting fish settle 20% faster). Seahorses perch on kelp, grass and decor and double trust from holds. The mussel pays doubloons."}},
    {"TANKS & IDENTITY", STAMP_FLAG, {
        "Keep up to eight aquariums, each with its own residents, decor and theme. Tank > Tank Summary switches, adds and reviews tanks.",
        "Tank Summary > Name & Identity renames a tank and picks one of many subtitles and illustrated emblems, enough for every tank to have its own."}},
    {"DECOR & FOLIAGE", STAMP_LEAF, {
        "Tank > Decor & Storage buys, places, stores and sells pieces. Hold a piece in the tank to move it; REMOVE sends any piece, even fixed scenery, to storage and PLACE brings it back.",
        "The lighthouse, stone bridge, pagoda and torii gate are large pieces as big as the castle. Ground cover and kelp never need trimming. Tall pieces give fish shelter and seahorses perches."}},
    {"THEMES", STAMP_BOOK, {
        "Tank > Themes changes the backdrop of the current tank. Original is free; the others are bought once with doubloons and stay unlocked for every tank.",
        "A theme only changes scenery and light. Residents, decor and foliage stay where you placed them."}},
    {"GAMES", STAMP_STAR, {
        "Main Menu > Games lists every arcade game, the Stories trips and The Lost Current RPG together. Page through with the arrow.",
        "Each game pays its score reward plus 2 doubloons for up to five rounds a day. Current Run is a tap-to-rise dive through rock gaps; Reef Architect is a coral logic grid."}},
    {"CASINO", STAMP_COIN, {
        "Main Menu > Casino holds Fish Races, Reef Reels, Pearl Hi-Lo, a Shell Game, Treasure Dive and the Tide Wheel. Choose a bet of 5, 10, 25 or 50 doubloons before each round.",
        "Results are random, and every win or loss is saved at once. Bet only spare doubloons; your fish, decor and creatures are never at stake."}},
    {"CLOCK & WEATHER", STAMP_CLOCK, {
        "Settings > Clock chooses a face, style, text size and time, and whether the clock and weather box show over the tank. Analog faces keep a brass dial; the others sit on rounded sea glass.",
        "Set a weather city to show live conditions. The same city decides which hemisphere's seasonal accents appear."}},
    {"SETTINGS & BACKUP", STAMP_EYE, {
        "Settings holds brightness, auto feed, clock, weather, updates and play modes. Creative mode removes prices; Hard mode adds real starvation risk.",
        "With an SD card, Settings can back up and restore the whole save. Saves are checked before any purchase or sale is paid."}},
};
#define HELP_TOPIC_COUNT ((int)(sizeof HELP_TOPICS / sizeof HELP_TOPICS[0]))
#define HELP_PER_PAGE 7
static int help_topic;
static void help_render(uint16_t *fb, int stride) {
    char line[48];
    int pages = (HELP_TOPIC_COUNT + HELP_PER_PAGE - 1) / HELP_PER_PAGE;
    if (page == COMPANION_SECTION_HELP) {
        int first = (leaf % pages) * HELP_PER_PAGE;
        for (int j = 0; j < HELP_PER_PAGE && first + j < HELP_TOPIC_COUNT; j++)
            illustrated_button(fb, stride, 12, 84 + j * 34, 424, HELP_TOPICS[first + j].title,
                               HELP_TOPICS[first + j].icon, true);
        snprintf(line, sizeof line, "PAGE %d/%d", leaf % pages + 1, pages);
        button(fb, stride, 12, 324, 132, "< PAGE");
        render_text(fb, stride, 190, 334, 1, 0xa7c4ba, line);
        button(fb, stride, 304, 324, 132, "PAGE >");
        return;
    }
    const help_topic_t *h = &HELP_TOPICS[help_topic % HELP_TOPIC_COUNT];
    stamp(fb, stride, 14, 88, h->icon, true);
    text(fb, stride, 40, 86, 0xffd998, h->title);
    paragraph(fb, stride, 120, 0xe7f4dc, h->body[0]);
    paragraph(fb, stride, 222, 0xa7c4ba, h->body[1]);
    snprintf(line, sizeof line, "%d/%d", help_topic % HELP_TOPIC_COUNT + 1, HELP_TOPIC_COUNT);
    button(fb, stride, 12, 324, 150, "< TOPIC");
    render_text(fb, stride, 206, 334, 1, 0xa7c4ba, line);
    button(fb, stride, 286, 324, 150, "TOPIC >");
}
static void help_tap(float x, float y) {
    int pages = (HELP_TOPIC_COUNT + HELP_PER_PAGE - 1) / HELP_PER_PAGE;
    if (y >= 324 && y < 354) {
        bool back = x >= 12 && x < (page == COMPANION_SECTION_HELP ? 144 : 162);
        bool next = x >= (page == COMPANION_SECTION_HELP ? 304 : 286) && x < 436;
        if (page == COMPANION_SECTION_HELP) {
            if (back) leaf = (leaf + pages - 1) % pages;
            if (next) leaf = (leaf + 1) % pages;
        } else {
            if (back) help_topic = (help_topic + HELP_TOPIC_COUNT - 1) % HELP_TOPIC_COUNT;
            if (next) help_topic = (help_topic + 1) % HELP_TOPIC_COUNT;
        }
        return;
    }
    if (page != COMPANION_SECTION_HELP || x < 12 || x >= 436 || y < 84 || (int)(y - 84) % 34 >= 30) return;
    int k = (leaf % pages) * HELP_PER_PAGE + (int)(y - 84) / 34;
    if ((int)(y - 84) / 34 < HELP_PER_PAGE && k < HELP_TOPIC_COUNT) {
        help_topic = k; page = COMPANION_SECTION_HELP_TOPIC;
    }
}
static bool bank_request;
bool companion_take_bank_request(void) { bool r = bank_request; bank_request = false; return r; }
void companion_render(const tank_t *t, uint16_t *fb, int stride) {
    prepare_draft(t);
    const companion_state_t *s = &t->companion;
    unsigned cosmetic = s->cosmetic <= 3 ? s->cosmetic : 0;
    render_menu_backdrop(fb, stride, 0, 0, PAGE_W, PAGE_H);
    render_button(fb, stride, 10, 76, 428, 286, 0x102a32, 0x435f61, "", 1);
    const char *title = page == COMPANION_SECTION_SUMMARY ? "FISH SUMMARY" :
                        page == COMPANION_SECTION_HELP || page == COMPANION_SECTION_HELP_TOPIC ? "HELP" :
                        page == COMPANION_SECTION_FOOD ? "FISH FOOD" :
                        page == COMPANION_SECTION_RESIDENTS ? "YOUR RESIDENTS" :
                        page == COMPANION_SECTION_CREATURE_MARKET ? "CREATURE MARKET" :
                        page == COMPANION_SECTION_CREATURE_SALE ? "SELL CREATURE?" :
                        page == COMPANION_SECTION_MARKET ? "FISH MARKET" :
                        page == COMPANION_SECTION_NURSERY ? "PAIR & NURSERY" :
                        page == COMPANION_SECTION_SALE ? "CONFIRM FISH SALE" :
                        page == COMPANION_SECTION_CONTRACT ? "BREEDING CONTRACT" :
                        page == COMPANION_SECTION_AUCTION ? "AUCTION DAY" :
                        page == COMPANION_SECTION_TRADE_CONFIRM ? (trade_parent == COMPANION_SECTION_AUCTION ? "CONFIRM LOT" : "CONFIRM ORDER") :
                        page == COMPANION_SECTION_AUCTION_RESULT ? "AUCTION RESULT" :
                        page == COMPANION_SECTION_CARE ? "FISH INTERACTION" : page == COMPANION_SECTION_COLLECTIONS ? "DISCOVERIES" : page == COMPANION_SECTION_ADVENTURES ? "ADVENTURES" :
                        page == COMPANION_SECTION_WARDROBE ? "WARDROBE" : page == COMPANION_SECTION_CREATURE_COLORS ? "CREATURE COLORS" : page == COMPANION_SECTION_GEAR_COLORS ? "GEAR COLORS" : page == COMPANION_SECTION_APPEARANCE ? "SAND & WATER" :
                        page == COMPANION_SECTION_REEF ? "CURRENTS" : page == COMPANION_SECTION_SYMBOLS ? "SYMBOL GUIDE" : page == COMPANION_SECTION_LIFE ? "A FISH'S LIFE" : page == COMPANION_SECTION_RECAP ? "WHILE YOU WATCHED" : page == COMPANION_SECTION_IDENTITY ? "NAME & IDENTITY" :
                        page == COMPANION_SECTION_OBJECTS ? "OBJECT STORIES" : page == COMPANION_SECTION_KEEPSAKES ? "ADVENTURE KEEPSAKES" : page == COMPANION_SECTION_HINTS ? "DISCOVERY HINTS" :
                        page == COMPANION_SECTION_FIRSTS ? "LIFE FIRSTS" : page == COMPANION_SECTION_FAMILY ? "FAMILY TREE" : page == COMPANION_SECTION_VISITORS ? "VISITOR LOG" :
                        page == COMPANION_SECTION_REWARDS ? "DISPLAY REWARDS" : page == COMPANION_SECTION_GENES ? "FISH GENES" :
                        page == COMPANION_SECTION_RPG ? "THE LOST CURRENT" : page == COMPANION_SECTION_RPG_JOURNAL ? "CAMPAIGN JOURNAL" :
                        page == COMPANION_SECTION_RPG_HELP ? "STORY HELP" : page == COMPANION_SECTION_RPG_EVENT ? "STORY EVENT" : "BOND & TRICKS";
    int emblem = page >= COMPANION_SECTION_CONTRACT && page <= COMPANION_SECTION_AUCTION_RESULT ? STAMP_COIN :
                 page == COMPANION_SECTION_ADVENTURES || rpg_page() ? STAMP_SHELL :
                 page == COMPANION_SECTION_FAMILY ? STAMP_FAMILY : page == COMPANION_SECTION_GENES ? STAMP_GENES :
                 page == COMPANION_SECTION_COLLECTIONS || page == COMPANION_SECTION_HINTS || page == COMPANION_SECTION_VISITORS ? STAMP_EYE :
                 page == COMPANION_SECTION_LIFE || page == COMPANION_SECTION_FIRSTS ? STAMP_FISH :
                 page == COMPANION_SECTION_RECAP || page == COMPANION_SECTION_OBJECTS || page == COMPANION_SECTION_KEEPSAKES ||
                 page == COMPANION_SECTION_HELP || page == COMPANION_SECTION_HELP_TOPIC ? STAMP_BOOK : STAMP_HEART;
    stamp(fb, stride, 14, 16, emblem, true);
    render_text(fb, stride, 40, 15, render_text_w(title, 2) <= 202 ? 2 : 1,
                frame_colors[cosmetic], title);
    button(fb, stride, 252, 8, 84, "MENU");
    button(fb, stride, 346, 8, 90, "BACK");
    char context[48];
    if (page == COMPANION_SECTION_RPG) companion_rpg_context(t, selected < t->n_fish ? selected : 0, context, sizeof context);
    else snprintf(context, sizeof context, "%ld DOUBLOONS", (long)t->sd_balance);
    if (page == COMPANION_SECTION_RPG)
        render_text_wrap(fb, stride, 14, 49, 420, notice[0] ? 0xffd998 : 0xa7c4ba, notice[0] ? notice : context);
    else {
        if (!notice[0]) render_doubloon(fb, stride, 14, 49);
        render_text(fb, stride, notice[0] ? 14 : 38, 55, 1, notice[0] ? 0xffd998 : 0xe9ce95,
                     notice[0] ? notice : context);
    }
    char line[64];
    if (page == COMPANION_SECTION_MARKET || page == COMPANION_SECTION_RESIDENTS ||
        page == COMPANION_SECTION_NURSERY || page == COMPANION_SECTION_SALE ||
        page == COMPANION_SECTION_CREATURE_MARKET || page == COMPANION_SECTION_CREATURE_SALE) {
        market_render(t, fb, stride);
        return;
    }
    if (page >= COMPANION_SECTION_CONTRACT && page <= COMPANION_SECTION_AUCTION_RESULT) {
        trade_render(t, fb, stride);
        return;
    }
    if (page == COMPANION_SECTION_SUMMARY) {
        if (!t->n_fish) {
            paragraph(fb, stride, 100, 0xe7f4dc, "No fish at home. Check Market or Fish Bank.");
            market_button(fb, stride, 304, 320, 132, "FISH BANK");
            return;
        }
        int i = selected < t->n_fish ? selected : 0;
        const fish_t *f = &t->fish[i];
        static const char *const stages[] = {"FRY", "JUVENILE", "ADULT", "ELDER"};
        snprintf(line, sizeof line, "%s / %s", FISH_VARIETY_NAMES[f->variety], stages[f->stage & 3]);
        fish_heading(t, i, fb, stride, line);
        market_button(fb, stride, 304, 82, 132, "NEXT FISH");
        float values[] = {f->trust, f->energy, 10 - f->hunger, 10 - f->stress, (float)progression_fish_vigor(t, i)};
        for (unsigned j = 0; j < 5; j++)
            render_fish_meter(fb, stride, 127 + (int)j * 29, j, values[j]);
        int pct = progression_fish_gene_pct(t, i), love = tank_fish_favorite_item(t, i);
        snprintf(line, sizeof line, "%s GENES x%d.%02d / WORTH %d", progression_fish_grade(pct), pct / 100, pct % 100,
                 progression_creative(t) ? 0 : progression_fish_value(t, i));
        render_doubloon(fb, stride, 14, 272);
        render_text(fb, stride, 38, 276, 1, 0xe9ce95, line);
        bool present = love >= 0 && tank_item_in_tank(t, love);
        snprintf(line, sizeof line, "LOVES %s: %s", love >= 0 ? SD_ITEMS[love].name : "OPEN WATER",
                 present ? "IN TANK" : "MISSING");
        stamp(fb, stride, 14, 295, STAMP_LEAF, present);
        render_text(fb, stride, 38, 298, 1, present ? 0x8ed9a5 : 0xffb48a, line);
        int gen = t->story.version ? t->story.active[i].generation : 0, bonus = progression_fish_pedigree_pct(t, i);
        if (gen) snprintf(line, sizeof line, "PEDIGREE: GEN %d, +%d%% BLOODLINE", gen, bonus);
        else snprintf(line, sizeof line, "PEDIGREE: FOUNDER STOCK");
        stamp(fb, stride, 14, 317, STAMP_FAMILY, gen > 0);
        render_text(fb, stride, 38, 320, 1, gen ? 0xd8c3f2 : 0xa7c4ba, line);
        int demand = progression_demand_variety(t);
        bool wanted = demand == f->variety;
        snprintf(line, sizeof line, "DEMAND: %s +%d%%%s", FISH_VARIETY_NAMES[demand], PROGRESSION_DEMAND_PCT,
                 wanted && gen ? " - YOURS!" : " IF BRED");
        stamp(fb, stride, 14, 339, STAMP_STAR, wanted);
        render_text(fb, stride, 38, 342, 1, wanted ? 0xffd998 : 0xa7c4ba, line);
        return;
    }
    if (page == COMPANION_SECTION_HELP || page == COMPANION_SECTION_HELP_TOPIC) {
        help_render(fb, stride);
        return;
    }
    if (page == COMPANION_SECTION_FOOD) {
        snprintf(line, sizeof line, "%ld DOUBLOONS", (long)t->sd_balance);
        render_doubloon(fb, stride, 24, 96);
        text(fb, stride, 40, 88, 0xffd998, line);
        paragraph(fb, stride, 126, 0xa7c4ba,
                  "Buy once for all tanks. Feed your best recipe forever. Faster growth, with no refill purchases.");
        static const char *const names[] = {"PELLETS / 1.00X", "FLAKES / 1.25X", "BLOODWORMS / 1.50X"};
        unsigned tier = progression_food_tier(t);
        for (unsigned j = 0; j < 3; j++) {
            if (j <= tier) snprintf(line, sizeof line, "%s / %s", names[j], j == tier ? "ACTIVE" : "OWNED");
            else snprintf(line, sizeof line, "%s / %d", names[j], progression_food_price(j));
            int y = 204 + (int)j * 52;
            market_button(fb, stride, 12, y, 424, "");
            render_food_sample(fb, stride, 40, y + 19, j);
            render_text(fb, stride, 64, y + 16, 1, 0xe7f4dc, line);
#ifdef RENDER_MENU_TESTING
            extern void menu_test_button(int x, int y, int w, int h, const char *label, int scale);
            menu_test_button(12, y, 424, 42, line, 1);
#endif
        }
        return;
    }
    if (rpg_page()) {
        rpg_render(t, fb, stride);
        return;
    }
    if (information_page()) polish_render(t, fb, stride);
    if (information_page() && page != COMPANION_SECTION_REWARDS) {
        return;
    }
    if (page == COMPANION_SECTION_REWARDS) {
        paragraph(fb, stride, 84, 0xa7c4ba, "Complete discovery sets, then claim their display rewards. No care bonuses.");
        for (int j = 0; j < 3; j++) {
            static const char *const names[] = {"NATURE", "FRIENDSHIP", "ADVENTURES"};
            static const unsigned masks[] = {0x3f, 0xc0, 0x700}, totals[] = {6, 2, 3};
            snprintf(line, sizeof line, "%s %u/%u: %s", names[j], bits(s->discoveries & masks[j]), totals[j],
                     progression_creative(t) ? "AVAILABLE" : s->collections & (1u << j) ? "CLAIMED" :
                     companion_collection_ready(t, j) ? "TAP TO CLAIM" : "FIND MORE");
            illustrated_button(fb, stride, 12, 130 + j * 34, 424, line,
                               j == 0 ? STAMP_LEAF : j == 1 ? STAMP_HEART : STAMP_SHELL,
                               !!(s->collections & (1u << j)));
        }
        button(fb, stride, 12, 242, 205, s->decor ? "TROPHIES: SHOW" : "TROPHIES: HIDE");
        static const char *const frames[] = {"BASE", "NATURE", "FRIEND", "STORY"};
        snprintf(line, sizeof line, "FRAME: %s", frames[cosmetic]);
        render_rect(fb, stride, 231, 242, 205, 30, 0x264d55);
        render_text(fb, stride, 239, 249, 2, 0xe7f4dc, line);
        paragraph(fb, stride, 282, 0xa7c4ba, progression_creative(t) ?
                  "Creative: all frames are available. Trophies sit on the sand. Frames change border colors, not the font." :
                  "Trophies sit on the sand. Frames change border colors, not the font. Claim a set to unlock its frame.");
    } else if (page == COMPANION_SECTION_CARE) {
        if (t->n_fish) {
            int i = selected < t->n_fish ? selected : 0;
            const companion_bond_t *b = &s->active[i];
            snprintf(line, sizeof line, "%s BOND %u TRUST %.1f", t->fish[i].name, b->bond, (double)t->fish[i].trust);
            render_fish_portrait(fb, stride, 34, 89, 0.55f, &t->fish[i], 0);
            render_text(fb, stride, 64, 88, 1, 0xe7f4dc, line);
            button(fb, stride, 304, 110, 132, "NEXT FISH");
            illustrated_button(fb, stride, 12, 110, 278, "SAY HELLO (+5 BOND)", STAMP_HEART, true);
            bond_progress_text(t, b, line, sizeof line);
            render_text(fb, stride, 14, 148, 1, 0xa7c4ba, line);
            companion_favorite_label(t, i, line, sizeof line);
            render_text(fb, stride, 14, 170, 1, 0xffd998, line);
            companion_care_text(t, i, line, sizeof line);
            paragraph(fb, stride, 194, 0xa7c4ba, line);
            static const char *const tricks[] = {"DART", "BUBBLES", "FOLLOW"};
            static const unsigned need[] = {10, 25, 50};
            for (int j = 0; j < 3; j++) {
                bool unlocked = progression_creative(t) || b->bond >= need[j];
                snprintf(line, sizeof line, "%s%s", tricks[j], unlocked ? "" : " -");
                button(fb, stride, 12 + 146 * j, 244, 132, line);
            }
            render_text(fb, stride, 14, 282, 1, 0xa7c4ba,
                        progression_creative(t) ? "CREATIVE: ALL TRICKS AVAILABLE." : "- = LOCKED: 10 / 25 / 50 BOND.");
            button(fb, stride, 12, 306, 424, "HOW BONDS WORK");
        } else paragraph(fb, stride, 94, 0xa7c4ba, "No fish at home. Check the Fish Bank or Adventures.");
    } else if (page == COMPANION_SECTION_APPEARANCE) {
        snprintf(line, sizeof line, "SAND: %s", render_palette_name(true, t->sand_style));
        button(fb, stride, 12, 100, 424, line);
        snprintf(line, sizeof line, "WATER: %s", render_palette_name(false, t->water_style));
        button(fb, stride, 12, 160, 424, line);
        text(fb, stride, 14, 218, 0xa7c4ba, "Upgrade palettes in Tank > Shop.");
    } else if (page == COMPANION_SECTION_COLLECTIONS) {
        snprintf(line, sizeof line, "DISCOVERIES %u/12 / PAGE %d/3", bits(s->discoveries), leaf + 1);
        render_text(fb, stride, 14, 91, 1, 0xe7f4dc, line);
        for (int j = 0; j < 4; j++) {
            int id = leaf * 4 + j;
            snprintf(line, sizeof line, "%s %s", s->discoveries & (1u << id) ? "+" : "-", COMPANION_DISCOVERIES[id]);
            illustrated_button(fb, stride, 12, 121 + j * 38, 424, line,
                               id < 6 ? STAMP_LEAF : id < 8 ? STAMP_HEART : STAMP_SHELL,
                               !!(s->discoveries & (1u << id)));
        }
        button(fb, stride, 12, 286, 424, ">");
        render_text(fb, stride, 14, 326, 1, 0xa7c4ba, "+ = FOUND. TAP A ROW FOR A HINT.");
    } else if (page == COMPANION_SECTION_ADVENTURES) {
        int a = s->adventure ? s->adventure - 1 : story;
        fish_t portrait;
        const fish_t *guide = t->n_fish ? &t->fish[selected < t->n_fish ? selected : 0] : NULL;
        if (s->adventure) {
            unsigned left = companion_adventure_total(s, a) - s->adventure_s;
            snprintf(line, sizeof line, "%s %uh %02um %s", s->portrait_name,
                     left / 3600, left % 3600 / 60, s->away ? "LEFT" : "HOME");
            memset(&portrait, 0, sizeof portrait);
            portrait.stage = STAGE_ADULT; portrait.variety = s->portrait_variety; portrait.pattern = s->portrait_pattern;
            portrait.wearable = s->portrait_wearable;
            portrait.wear_colors = s->portrait_colors;
            portrait.color = s->portrait_body; portrait.fin = tank_fin_for(s->portrait_body);
            portrait.accent = s->portrait_accent; portrait.size = 1;
            guide = &portrait;
        } else {
            snprintf(line, sizeof line, "%uh away: %d DOUBLOONS", COMPANION_ADVENTURE_SECONDS[a] / 3600, 10 * (a + 1));
        }
        static const int destinations[] = {3, 4, 2};
        bool route = s->adventure_done & (1u << a) ? !!(s->adventure_routes & (1u << a)) : !!s->adventure_choice;
        story_scene(fb, stride, 14, 82, 420, 78, destinations[a], guide, route);
        text(fb, stride, 14, 170, 0xe7f4dc, COMPANION_ADVENTURES[a]);
        render_text(fb, stride, 14, 194, 1, 0xe7f4dc, line);
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
    } else if (page == COMPANION_SECTION_WARDROBE && t->n_fish) {
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
        bool owned = progression_creative(t) || (companion_wearables(b) & (1u << (wardrobe - 1))) != 0;
        bool fits = companion_wearable_fits(preview.variety, wardrobe);
        if (!fits) snprintf(line, sizeof line, "NO FIT FOR THIS TYPE%s", owned ? " / OWNED" : "");
        else if (owned) snprintf(line, sizeof line, "%s / %ld DOUBLOONS",
                            b->wearable == wardrobe ? "EQUIPPED" : "OWNED", (long)t->sd_balance);
        else snprintf(line, sizeof line, "%u DOUBLOONS / BALANCE %ld",
                      WEAR_PRICES[wardrobe], (long)t->sd_balance);
        text(fb, stride, 14, 260, 0xa7c4ba, line);
        button(fb, stride, 12, 286, 205, !fits ? "UNAVAILABLE" : b->wearable == wardrobe ? "EQUIPPED" :
               owned ? "EQUIP" : "BUY + EQUIP");
        button(fb, stride, 231, 286, 205, "REMOVE");
        text(fb, stride, 14, 322, 0xa7c4ba, "Cosmetic. Owned by this fish.");
    } else if (page == COMPANION_SECTION_CREATURE_COLORS || (page == COMPANION_SECTION_GEAR_COLORS && t->n_fish)) {
        bool gear = page == COMPANION_SECTION_GEAR_COLORS;
        int i = selected < t->n_fish ? selected : 0;
        snprintf(line, sizeof line, "%s", gear ? t->fish[i].name : CREATURE_NAMES[creature]);
        text(fb, stride, 14, 94, 0xe7f4dc, line);
        if (!gear) button(fb, stride, 304, 86, 132, "NEXT TYPE");
        if (gear) {
            fish_t preview = t->fish[i]; preview.wearable = (uint8_t)wardrobe; preview.wear_colors = color_draft;
            render_fish_portrait(fb, stride, 224, 165, 1.5f, &preview, t->clock);
            if (!companion_wearable_fits(preview.variety, wardrobe))
                text(fb, stride, 14, 202, 0xffd998, "Accessory does not fit this type.");
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
            "Bond and trust are separate.",
            "Tricks unlock at bond 10/25/50:",
            "DART: a quick playful swim.",
            "BUBBLE: visit the bubble column.",
            "FOLLOW: swim with a tankmate.",
            "Use BACK to watch a trick."
        };
        for (int i = 0; i < 10; i++) text(fb, stride, 14, 91 + 23 * i, 0xe7f4dc, help[i]);
    }
}
bool companion_tap(tank_t *t, float frame_x, float frame_y) {
    prepare_draft(t);
    const companion_state_t *s = &t->companion;
    float x = render_page_x(frame_x), y = render_page_y(frame_y);
    if (x >= 346 && x < 436 && y >= 8 && y < 38) {
        if (page == COMPANION_SECTION_CREATURE_SALE) {
            page = creature_sale_parent; notice[0] = 0; return false;
        }
        if (page == COMPANION_SECTION_CREATURE_MARKET && section != COMPANION_SECTION_CREATURE_MARKET) {
            page = COMPANION_SECTION_MARKET; notice[0] = 0; return false;
        }
        if (page == COMPANION_SECTION_FAMILY && section == COMPANION_SECTION_MARKET) {
            page = COMPANION_SECTION_NURSERY; notice[0] = 0; return false;
        }
        if (page == COMPANION_SECTION_SALE) {
            page = COMPANION_SECTION_RESIDENTS; notice[0] = 0; return false;
        }
        if (page == COMPANION_SECTION_TRADE_CONFIRM) { page = trade_parent; notice[0] = 0; return false; }
        if (page == COMPANION_SECTION_AUCTION_RESULT) { page = COMPANION_SECTION_AUCTION; notice[0] = 0; return false; }
        if (page == COMPANION_SECTION_NURSERY || page == COMPANION_SECTION_RESIDENTS) {
            page = COMPANION_SECTION_MARKET; notice[0] = 0; return false;
        }
        if (page == COMPANION_SECTION_RPG_EVENT) { page = COMPANION_SECTION_RPG; notice[0] = 0; return false; }
        if (page == COMPANION_SECTION_RPG_HELP) { page = COMPANION_SECTION_RPG_JOURNAL; notice[0] = 0; return false; }
        if (page == COMPANION_SECTION_RPG_JOURNAL) { page = COMPANION_SECTION_RPG; notice[0] = 0; return false; }
        if (page == COMPANION_SECTION_GEAR_COLORS) { page = COMPANION_SECTION_WARDROBE; notice[0] = 0; return false; }
        if (page == COMPANION_SECTION_TRICKS) { page = COMPANION_SECTION_CARE; notice[0] = 0; return false; }
        if (page == COMPANION_SECTION_HINTS) { page = COMPANION_SECTION_COLLECTIONS; leaf /= 4; notice[0] = 0; return false; }
        if (page == COMPANION_SECTION_HELP_TOPIC) { page = COMPANION_SECTION_HELP; leaf = help_topic / HELP_PER_PAGE; return false; }
        if (page != section) { page = section; leaf = 0; notice[0] = 0; return false; }
        return true;
    }
    if (page == COMPANION_SECTION_SUMMARY) {
        if (t->n_fish && y >= 82 && y < 124 && x >= 304 && x < 436)
            selected = (selected + 1) % t->n_fish;
        else if (!t->n_fish && x >= 304 && x < 436 && y >= 320 && y < 362) { bank_request = true; return true; }
    } else if (page == COMPANION_SECTION_HELP || page == COMPANION_SECTION_HELP_TOPIC) {
        help_tap(x, y);
    } else if (page >= COMPANION_SECTION_CONTRACT && page <= COMPANION_SECTION_AUCTION_RESULT) {
        trade_tap(t, x, y);
    } else if (page == COMPANION_SECTION_FOOD) {
        if (x >= 12 && x < 436 && y >= 204 && y < 350 && (int)(y - 204) % 52 < 42) {
            unsigned tier = (unsigned)(y - 204) / 52;
            if (tier <= progression_food_tier(t)) snprintf(notice, sizeof notice, "Already owned; your best tier is active.");
            else snprintf(notice, sizeof notice, "%s", progression_food_buy(t, tier) ?
                          "Permanent food upgrade saved." : progression_shop_notice());
        }
    } else if ((page == COMPANION_SECTION_MARKET || page == COMPANION_SECTION_RESIDENTS ||
                page == COMPANION_SECTION_NURSERY) && y >= 80 && y < 122) {
        if (x >= 12 && x < 144) page = COMPANION_SECTION_MARKET;
        else if (x >= 158 && x < 290) page = COMPANION_SECTION_RESIDENTS;
        else if (x >= 304 && x < 436) page = COMPANION_SECTION_NURSERY;
        notice[0] = 0;
    } else if (page == COMPANION_SECTION_CREATURE_MARKET) {
        int kind = market_creature;
        if (y >= 96 && y < 138) {
            if (x >= 12 && x < 82) { market_creature = (market_creature + CREATURE_COUNT - 1) % CREATURE_COUNT; notice[0] = 0; }
            else if (x >= 366 && x < 436) { market_creature = (market_creature + 1) % CREATURE_COUNT; notice[0] = 0; }
        } else if (x >= 12 && x < 436 && y >= 242 && y < 284) {
            bool bought = progression_creature_buy(t, kind);
            snprintf(notice, sizeof notice, "%s", bought ? "Bought and saved. Colors rolled!" : progression_shop_notice());
        } else if (y >= 290 && y < 332 && x >= 12 && x < 436) {
            if (progression_creature_sale_quote(t, kind, false, &creature_sale)) {
                creature_sale_parent = page;
                page = COMPANION_SECTION_CREATURE_SALE; notice[0] = 0;
            } else snprintf(notice, sizeof notice, "Nothing to sell here, or unavailable.");
        }
    } else if (page == COMPANION_SECTION_CREATURE_SALE) {
        if (y >= 274 && y < 304) {
            if (x >= 12 && x < 217) { page = creature_sale_parent; notice[0] = 0; }
            else if (x >= 231 && x < 436) {
                bool sold = progression_creature_sale_confirm(t, &creature_sale);
                page = creature_sale_parent;
                snprintf(notice, sizeof notice, "%s", sold ? "Creature sold and saved." : "Sale not saved. Review stock and retry.");
            }
        }
    } else if (page == COMPANION_SECTION_MARKET || page == COMPANION_SECTION_RESIDENTS) {
        bool residents = page == COMPANION_SECTION_RESIDENTS;
        if (x >= 12 && x < 436 && y >= 320 && y < 362) {
            page = COMPANION_SECTION_CREATURE_MARKET; market_creature = 0; notice[0] = 0;
        } else if (y >= 140 && y < 182 && ((x >= 12 && x < 82) || (x >= 366 && x < 436))) {
            if (residents && t->n_fish) selected = (selected + t->n_fish + (x < 82 ? -1 : 1)) % t->n_fish;
            else market_rank = (market_rank + FISH_VARIETY_COUNT + (x < 82 ? -1 : 1)) % FISH_VARIETY_COUNT;
            notice[0] = 0;
        } else if (!residents && y >= 268 && y < 310) {
            if (x >= 12 && x < 436) {
                bool bought = progression_market_buy(t, companion_variety_at(market_rank));
                snprintf(notice, sizeof notice, "%s", bought ? "Fish bought and saved. Welcome!" : progression_shop_notice());
            }
        } else if (residents && y >= 268 && y < 310 && t->n_fish) {
            if (x >= 12 && x < 436) {
                if (progression_fish_sale_quote(t, selected < t->n_fish ? selected : 0, &market_sale)) {
                    snprintf(market_sale_name, sizeof market_sale_name, "%s", t->fish[market_sale.fish].name);
                    page = COMPANION_SECTION_SALE; notice[0] = 0;
                } else snprintf(notice, sizeof notice, "Keep your pair; finish naming any fry.");
            }
        }
    } else if (page == COMPANION_SECTION_SALE) {
        if (y >= 274 && y < 304) {
            if (x >= 12 && x < 217) { page = COMPANION_SECTION_RESIDENTS; notice[0] = 0; }
            else if (x >= 231 && x < 436) {
                bool sold = progression_fish_sale_confirm(t, &market_sale);
                page = COMPANION_SECTION_RESIDENTS;
                selected = 0;
                snprintf(notice, sizeof notice, "%s", sold ? "Fish sold and saved." : "Sale not saved. Check fish and retry.");
            }
        }
    } else if (page == COMPANION_SECTION_NURSERY) {
        if (x >= 12 && x < 436 && y >= 320 && y < 362) { page = COMPANION_SECTION_FAMILY; notice[0] = 0; }
    } else if (page == COMPANION_SECTION_RPG) {
        if (x >= 304 && x < 436 && y >= 82 && y < 112 && t->n_fish) {
            selected = (selected + 1) % t->n_fish; notice[0] = 0;
        } else if (x >= 12 && x < 144 && y >= 118 && y < 148) {
            page = COMPANION_SECTION_RPG_JOURNAL; notice[0] = 0;
        } else if (t->rpg.chapter >= 6 && x >= 12 && x < 436 && y >= 304 && y < 334) {
            page = COMPANION_SECTION_RPG_JOURNAL; notice[0] = 0;
        } else if (t->rpg.chapter < 6 && t->n_fish && x >= 12 && x < 436 && y >= 232 && y < 334) {
            int choice = (int)(y - 232) / 36;
            if ((int)(y - 232) % 36 < 30) {
                int before = t->rpg.chapter;
                bool ok = companion_rpg_choose(t, selected < t->n_fish ? selected : 0, choice);
                if (ok) {
                    rpg_chapter_finished = t->rpg.chapter != before;
                    page = COMPANION_SECTION_RPG_EVENT; notice[0] = 0;
                } else snprintf(notice, sizeof notice, "Save failed. Story unchanged; retry.");
            }
        }
    } else if (page == COMPANION_SECTION_RPG_JOURNAL) {
        if (y >= 310 && y < 340) {
            if (x >= 12 && x < 217) { page = COMPANION_SECTION_RPG_HELP; notice[0] = 0; }
            else if (x >= 231 && x < 436) { page = COMPANION_SECTION_RPG; notice[0] = 0; }
        }
    } else if (page == COMPANION_SECTION_RPG_EVENT) {
        if (x >= 12 && x < 436 && y >= 304 && y < 334) { page = COMPANION_SECTION_RPG; notice[0] = 0; }
    } else if (page == COMPANION_SECTION_RPG_HELP) {
        return false;
    } else if (page == COMPANION_SECTION_REWARDS) {
        if (x >= 12 && x < 436 && y >= 130 && y < 228) {
            int set = (int)(y - 130) / 34;
            if ((int)(y - 130) % 34 >= 30) return false;
            if (progression_creative(t)) snprintf(notice, sizeof notice, "All frames available. Choose one below.");
            else if (s->collections & (1u << set)) snprintf(notice, sizeof notice, "Claimed. Choose its frame below.");
            else if (!companion_collection_ready(t, set)) snprintf(notice, sizeof notice, "Complete this discovery set first.");
            else result(companion_collection_claim(t, set));
        } else if (y >= 242 && y < 272 && x >= 12 && x < 217) result(companion_set_decor(t, !s->decor));
        else if (y >= 242 && y < 272 && x >= 231 && x < 436) {
            if (!progression_creative(t) && !s->collections) snprintf(notice, sizeof notice, "Claim a set to unlock menu frames.");
            else {
                int next = s->cosmetic;
                do { next = (next + 1) % 4; } while (!progression_creative(t) && next && !(s->collections & (1u << (next - 1))));
                result(companion_cosmetic(t, next));
            }
        }
    } else if (information_page()) {
        if (page == COMPANION_SECTION_SYMBOLS && y >= 312 && y < 342 &&
            ((x >= 12 && x < 217) || (x >= 231 && x < 436)))
            leaf = (leaf + (x < 217 ? 2 : 1)) % 3;
        else if ((page == COMPANION_SECTION_LIFE || page == COMPANION_SECTION_FAMILY || page == COMPANION_SECTION_GENES) && y >= 84 && y < 116 && x >= 304 && x < 436 && t->n_fish)
            selected = (selected + 1) % t->n_fish;
        else if (page == COMPANION_SECTION_IDENTITY && y >= 270 && y < 300 && x >= 12 && x < 436) {
            uint8_t next = (t->story.flags & STORY_SEASONS_OFF) ? 0 : STORY_SEASONS_OFF;
            snprintf(notice, sizeof notice, "%s", companion_set_seasons(t, next) ?
                     (next == STORY_SEASONS_OFF ? "Season accents off." : "Seasons follow your weather city.") :
                     "Save failed; seasons unchanged.");
        }
        else if (page == COMPANION_SECTION_RECAP && y >= 270 && y < 300 && x >= 12 && x < 436) {
            int memory_pages = (t->polish.recap_count + 2) / 3;
            leaf = (leaf + 1) % ((memory_pages ? memory_pages : 1) + 1);
        }
        else if (page == COMPANION_SECTION_IDENTITY && x >= 12 && x < 436 && ((y >= 154 && y < 184) || (y >= 192 && y < 222)))
            snprintf(notice, sizeof notice, "%s", companion_identity(t, y >= 192) ?
                     "Identity saved." : "Save failed; identity unchanged.");
        else if ((page == COMPANION_SECTION_OBJECTS || page == COMPANION_SECTION_HINTS) && y >= (page == COMPANION_SECTION_OBJECTS ? 258 : 240) &&
                 y < (page == COMPANION_SECTION_OBJECTS ? 288 : 270) && ((x >= 12 && x < 217) || (x >= 231 && x < 436))) {
            int count = page == COMPANION_SECTION_OBJECTS ? SD_ITEM_COUNT : COMPANION_DISCOVERY_COUNT;
            leaf = (leaf + count + (x < 217 ? -1 : 1)) % count;
        } else if (page == COMPANION_SECTION_FIRSTS && y >= 264 && y < 294 && x >= 304 && x < 436) {
            unsigned pages = (bits(t->polish.active[selected < t->n_fish ? selected : 0].milestones) + 5) / 6;
            leaf = (leaf + 1) % (pages ? pages : 1);
        }
    } else if (page == COMPANION_SECTION_CARE) {
        if (y >= 306 && y < 336 && x >= 12 && x < 436) { page = COMPANION_SECTION_TRICKS; notice[0] = 0; }
        else if (y >= 110 && y < 140 && x >= 304 && x < 436 && t->n_fish) selected = (selected + 1) % t->n_fish;
        else if (y >= 110 && y < 140 && x >= 12 && x < 290 && t->n_fish) {
            int i = selected < t->n_fish ? selected : 0;
            if (t->companion.active[i].next_greet_s > t->companion.powered_s)
                snprintf(notice, sizeof notice, "Hello ready in %lus.", (unsigned long)(t->companion.active[i].next_greet_s - t->companion.powered_s));
            else snprintf(notice, sizeof notice, "%s", companion_greet(t, i) ? "Hello! +5 bond. Trust unchanged." : "Save failed. Hello not recorded.");
        }
        else if (y >= 244 && y < 274 && t->n_fish) for (int j = 0; j < 3; j++)
            if (x >= 12 + 146 * j && x < 144 + 146 * j) {
                static const unsigned need[] = {10, 25, 50};
                static const char *const effects[] = {"Dart! BACK to watch.", "Bubbles! BACK to watch.", "Follow! BACK to watch."};
                int i = selected < t->n_fish ? selected : 0;
                if (!progression_creative(t) && t->companion.active[i].bond < need[j]) snprintf(notice, sizeof notice, "Locked: needs %u bond. Say hello.", need[j]);
                else if (j == 2 && t->n_fish < 2) snprintf(notice, sizeof notice, "Follow needs a tankmate at home.");
                else snprintf(notice, sizeof notice, "%s", companion_trick(t, i, j) ? effects[j] : "Save failed. Trick not started.");
            }
    } else if (page == COMPANION_SECTION_APPEARANCE) {
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
    } else if (page == COMPANION_SECTION_CREATURE_COLORS || (page == COMPANION_SECTION_GEAR_COLORS && t->n_fish)) {
        bool gear = page == COMPANION_SECTION_GEAR_COLORS;
        int i = selected < t->n_fish ? selected : 0;
        if (!gear && y >= 86 && y < 116 && x >= 304 && x < 436) {
            creature = (creature + 1) % CREATURE_COUNT; color_draft = companion_creature_colors(s, creature);
            notice[0] = 0;
        } else if (x >= 12 && x < 436 && y >= 220 && y < 250) {
            color_draft = (uint8_t)((color_draft & 240) | ((color_draft + 1) & 15)); notice[0] = 0;
        } else if (x >= 12 && x < 436 && y >= 254 && y < 284) {
            color_draft = (uint8_t)(color_draft + 16); notice[0] = 0;
        } else if (x >= 12 && x < 217 && y >= 294 && y < 324) {
            snprintf(notice, sizeof notice, "%s", companion_colors(t, gear, gear ? i : creature, color_draft) ?
                     "Accessory colors saved." : "Not owned or save failed.");
        } else if (x >= 231 && x < 436 && y >= 294 && y < 324) {
            color_draft = 0; snprintf(notice, sizeof notice, "Natural preview. Tap SAVE.");
        }
    } else if (page == COMPANION_SECTION_WARDROBE && t->n_fish) {
        int i = selected < t->n_fish ? selected : 0;
        if (y >= 86 && y < 116 && x >= 304 && x < 436) {
            selected = (i + 1) % t->n_fish; notice[0] = 0;
        } else if (y >= 166 && y < 196 && x >= 12 && x < 126) {
            page = COMPANION_SECTION_GEAR_COLORS; color_draft = s->active[i].wear_colors; notice[0] = 0;
        } else if (y >= 220 && y < 250 && ((x >= 12 && x < 78) || (x >= 370 && x < 436))) {
            wardrobe = 1 + (wardrobe - 1 + (x < 78 ? WEAR_COUNT - 2 : 1)) % (WEAR_COUNT - 1);
            notice[0] = 0;
        } else if (y >= 286 && y < 316 && x >= 12 && x < 436) {
            int choice = x < 217 ? wardrobe : x >= 231 ? WEAR_NONE : -1;
            if (choice < 0) return false;
            bool buy = !progression_creative(t) && choice && !(companion_wearables(&t->companion.active[i]) & (1u << (choice - 1)));
            if (!companion_wearable_fits(t->fish[i].variety, choice))
                snprintf(notice, sizeof notice, "Accessory does not fit this type.");
            else if (s->version < 2 && s->adventure) snprintf(notice, sizeof notice, "Claim your old postcard first.");
            else if (buy && t->sd_balance < WEAR_PRICES[choice])
                snprintf(notice, sizeof notice, "Needs %u doubloons.", WEAR_PRICES[choice]);
            else snprintf(notice, sizeof notice, "%s", companion_wearable(t, i, choice) ?
                          (choice ? "Saved. Looking good!" : "Removed. Still owned.") : "Save failed. Nothing changed.");
        }
    } else if (page == COMPANION_SECTION_COLLECTIONS) {
        if (x >= 12 && x < 436 && y >= 121 && y < 265 && (int)(y - 121) % 38 < 30) {
            leaf = leaf * 4 + (int)(y - 121) / 38; page = COMPANION_SECTION_HINTS; notice[0] = 0;
        } else if (x >= 12 && x < 436 && y >= 286 && y < 316) leaf = (leaf + 1) % 3;
    } else if (page == COMPANION_SECTION_ADVENTURES) {
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
    } else if (x >= 304 && x < 436 && y >= 324 && y < 354) page = COMPANION_SECTION_CARE;
    return false;
}
void companion_render_decor(const tank_t *t, uint16_t *fb, int stride) {
    const companion_state_t *s = &t->companion;
    unsigned collections = progression_creative(t) ? 7 : s->collections;
    if (!s->decor || !(collections || s->game_challenges || t->rpg.chapter)) return;
    /* PAGE coordinates scale to a tiny sand-corner shelf on every CYD frame.
     * Public rectangles participate in the scene renderer's dirty mask. */
    render_rect(fb, stride, 10, 344, 86, 4, 0x8f7758);
    for (int i = 0; i < 3; i++) if (collections & (1u << i)) {
        int x = 16 + 25 * i; uint32_t color = frame_colors[i + 1];
        render_rect(fb, stride, x + 3, 325, 12, 4, color);
        render_rect(fb, stride, x, 329, 18, 8, color);
        render_rect(fb, stride, x + 3, 337, 12, 5, color);
        render_rect(fb, stride, x + 8, 328, 2, 12, 0x4c5963);
    }
    for (int i = 0; i < 6; i++) if (s->game_challenges & (1u << i))
        render_rect(fb, stride, 12 + i * 14, 350, 8, 5, 0xffd998);
    if (t->rpg.chapter) {
        render_rect(fb, stride, 108, 329, 42, 19, 0xd6bc80);
        render_rect(fb, stride, 112, 326, 34, 25, 0xe8d9a9);
        for (int chapter = 0; chapter < t->rpg.chapter; chapter++)
            render_rect(fb, stride, 115 + chapter * 5, 335 + chapter % 2 * 6, 3, 3, 0x3b6d6b);
    }
}
