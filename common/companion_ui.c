#include "companion.h"
#include "render.h"
#include "progression.h"
#include <stdio.h>
#include <string.h>

static int page, selected, leaf, story, section;
static bool draft_pending;
static bool confirm_departure;
static bool rpg_chapter_finished;
static int wardrobe = WEAR_TOP_HAT;
static int creature;
static uint8_t color_draft;
static char notice[48];
static const uint32_t frame_colors[] = {0x83b8ac, 0x8ed9a5, 0xf0aeca, 0xc0b1ff};
static void text(uint16_t *fb, int stride, int x, int y, uint32_t rgb, const char *s) {
    render_text(fb, stride, x, y, render_text_w(s, 2) <= PAGE_W - x - 12 ? 2 : 1, rgb, s);
}
static void button(uint16_t *fb, int stride, int x, int y, int w, const char *s) {
    render_rect(fb, stride, x, y, w, 30, 0x264d55);
    int scale = render_text_w(s, 2) <= w - 16 ? 2 : 1;
    render_text(fb, stride, x + 8, y + (scale == 2 ? 7 : 11), scale, 0xe7f4dc, s);
}
static unsigned bits(unsigned value) {
    unsigned n = 0; for (; value; value >>= 1) n += value & 1u; return n;
}
static void polish_nav(uint16_t *fb, int stride) {
    bool fish = page == 8 || page == 12 || page == 14 || page == 15 || page == 19;
    static const char *const fish_names[] = {"LIFE", "FAMILY", "GENES"};
    if (fish)
        for (int i = 0; i < 3; i++) button(fb, stride, 12 + i * 146, 306, 132, fish_names[i]);
    else if (page == 9 || page == 11) {
        button(fb, stride, 12, 306, 205, "RECAP");
        button(fb, stride, 231, 306, 205, "OBJECTS");
    }
}
static void rpg_render(const tank_t *t, uint16_t *fb, int stride) {
    const tank_rpg_t *r = &t->rpg;
    char line[128];
    int i = selected < t->n_fish ? selected : 0;
    if (page == 23) {
        render_text(fb, stride, 14, 92, 2, 0xffd998, "YOUR STORY IS SAVED");
        render_text_wrap(fb, stride, 14, 132, 420, 0xe7f4dc, companion_rpg_outcome(t));
        snprintf(line, sizeof line, "COURAGE %u / CURIOSITY %u / KIND %u",
                 r->skills[0], r->skills[1], r->skills[2]);
        render_text(fb, stride, 14, 226, 1, 0xffd998, line);
        render_text_wrap(fb, stride, 14, 256, 420, 0xa7c4ba, rpg_chapter_finished ?
                         "Chapter and its guide recorded. Rewards saved once." :
                         "Your fish is safe at home. Continue whenever you like.");
        button(fb, stride, 12, 304, 424, "CONTINUE STORY");
        return;
    }
    if (page == 22) {
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
    if (page == 21) {
        snprintf(line, sizeof line, "COURAGE %u / CURIOSITY %u / KIND %u",
                 r->skills[0], r->skills[1], r->skills[2]);
        render_text(fb, stride, 14, 86, 1, 0xffd998, line);
        for (int chapter = 0; chapter < 6; chapter++) {
            int y = 112 + chapter * 31;
            bool done = chapter < r->chapter;
            snprintf(line, sizeof line, "%d %s", chapter + 1, companion_rpg_chapter_title(chapter));
            render_text(fb, stride, 14, y, 1, done ? 0xe7f4dc : 0x809b9a, line);
            if (done) {
                const char *name = companion_story_name(t, r->contributors[chapter]);
                snprintf(line, sizeof line, "%.12s: %s", name && *name ? name : "EARLIER FISH",
                         companion_rpg_route(t, chapter));
            } else snprintf(line, sizeof line, "%s", chapter == r->chapter ? "YOUR NEXT CHAPTER" : "UNEXPLORED");
            render_text(fb, stride, 24, y + 14, 1, done ? 0xffd998 : 0x809b9a, line);
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
    for (int chapter = 0; chapter < 6; chapter++) {
        int x = 166 + chapter * 47;
        uint32_t color = chapter < r->chapter ? 0xffd998 : chapter == r->chapter ? 0xb7f7ff : 0x4c6570;
        if (chapter < 5) render_rect(fb, stride, x + 10, 131, 28, 2, 0x4c6570);
        render_ring(fb, stride, (float)x, 132, 10, color);
        snprintf(line, sizeof line, "%d", chapter + 1);
        render_text(fb, stride, x - 4, 127, 1, color, line);
    }
    snprintf(line, sizeof line, "%s%s", r->chapter < 6 ? "" : "FINALE: ", companion_rpg_chapter_name(t));
    render_text(fb, stride, 14, 152, 1, 0xffd998, line);
    render_text_wrap(fb, stride, 14, 168, 420, 0xe7f4dc, companion_rpg_prompt(t));
    if (r->chapter < 6 && t->n_fish) {
        for (int choice = 0; choice < 3; choice++) {
            companion_rpg_choice_label(t, choice, line, sizeof line);
            render_rect(fb, stride, 12, 232 + choice * 36, 424, 30, 0x264d55);
            render_text(fb, stride, 20, 243 + choice * 36, 1, 0xe7f4dc, line);
        }
    } else if (r->chapter >= 6) {
        render_text_wrap(fb, stride, 14, 240, 420, 0xffd998, companion_rpg_outcome(t));
        button(fb, stride, 12, 304, 424, "READ YOUR CAMPAIGN JOURNAL");
    } else render_text_wrap(fb, stride, 14, 240, 420, 0xa7c4ba, "Return a fish from the Bank or wait for your traveler. The story waits without penalty.");
    snprintf(line, sizeof line, "COURAGE %u / CURIOSITY %u / KIND %u", r->skills[0], r->skills[1], r->skills[2]);
    render_text(fb, stride, 14, 346, 1, 0xa7c4ba, line);
}
static void paragraph(uint16_t *fb, int stride, int y, uint32_t rgb, const char *s) {
    size_t capacity = (420 - render_text_w("W", 1)) /
        (render_text_w("WW", 1) - render_text_w("W", 1)) + 1;
    if (capacity > 70) capacity = 70;
    while (*s && y < 338) {
        size_t n = strlen(s), count = n < capacity ? n : capacity;
        if (n > count) {
            while (count && s[count] != ' ') count--;
            if (!count) count = capacity;
        }
        char line[71]; memcpy(line, s, count); line[count] = 0;
        render_text(fb, stride, 14, y, 1, rgb, line);
        s += count; while (*s == ' ') s++;
        y += 16;
    }
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
    snprintf(line, sizeof line, "%s / GEN %u", t->fish[i].name, l->generation + 1u);
    text(fb, stride, 14, 88, 0xe7f4dc, line);
    button(fb, stride, 304, 84, 132, "NEXT FISH");
    if (!l->parent[0] && !l->parent[1])
        snprintf(line, sizeof line, "PARENTS: %s", l->generation ? "NOT RECORDED" : "A FOUNDER OF THIS TANK");
    else if (!l->parent[1] || !l->parent[0]) snprintf(line, sizeof line, "PARENT: %s", who(t, l->parent[0] ? l->parent[0] : l->parent[1]));
    else snprintf(line, sizeof line, "PARENTS: %s + %s", who(t, l->parent[0]), who(t, l->parent[1]));
    paragraph(fb, stride, 124, 0xffd998, line);
    int n = snprintf(line, sizeof line, "GRANDPARENTS:");
    int grand = 0;
    for (int k = 0; k < 2; k++) {
        const fish_lineage_t *pl = companion_story_line(t, l->parent[k]);
        if (pl) for (int g = 0; g < 2; g++) if (pl->parent[g] && n < (int)sizeof line - 12) {
            n += snprintf(line + n, sizeof line - n, "%s%s", grand ? ", " : " ", who(t, pl->parent[g])); grand++;
        }
    }
    if (!grand) snprintf(line + n, sizeof line - n, " NOT RECORDED");
    paragraph(fb, stride, 172, 0xa7c4ba, line);
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
    paragraph(fb, stride, 220, 0xe7f4dc, line);
    snprintf(line, sizeof line, "SIBLINGS: %d / BORN HERE: %u", siblings, t->story.births);
    render_text(fb, stride, 14, 272, 1, 0xa7c4ba, line);
    if (companion_rpg_legacy(t, i))
        render_text(fb, stride, 14, 288, 1, 0xffd998, "THIS FAMILY KNOWS THE LOST CURRENT.");
}
static void genes_render(const tank_t *t, int i, uint16_t *fb, int stride) {
    if (!t->n_fish) { text(fb, stride, 14, 94, 0xe7f4dc, "No fish at home."); return; }
    const fish_lineage_t *l = &t->story.active[i];
    char line[160];
    text(fb, stride, 14, 88, 0xe7f4dc, t->fish[i].name);
    button(fb, stride, 304, 84, 132, "NEXT FISH");
    snprintf(line, sizeof line, "MARKING: %s", FISH_PATTERN_NAMES[t->fish[i].pattern]);
    render_text(fb, stride, 14, 124, 1, 0xe7f4dc, line);
    int n;
    n = snprintf(line, sizeof line, "GENES:");
    int shown = 0;
    for (int k = 0; k < TRAIT_COUNT; k++) if (l->traits & (1u << k))
        n += snprintf(line + n, sizeof line - n, "%s%s", shown++ ? ", " : " ", FISH_TRAIT_NAMES[k]);
    if (!shown) n += snprintf(line + n, sizeof line - n, " CLASSIC");
    if (l->mutation) snprintf(line + n, sizeof line - n, " - MUTANT!");
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
    paragraph(fb, stride, 264, 0xa7c4ba, "Hidden genes pass to fry. Recessive traits need two copies.");
}
static void polish_render(const tank_t *t, uint16_t *fb, int stride) {
    const tank_polish_t *p = &t->polish;
    char line[80], date[40];
    int i = selected < t->n_fish ? selected : 0;
    if (page == 8) {
        if (!t->n_fish) text(fb, stride, 14, 94, 0xe7f4dc, "No fish at home. Check Bank / away.");
        else {
            const fish_t *f = &t->fish[i]; const fish_life_t *life = &p->active[i];
            static const char *const stages[] = {"FRY", "JUVENILE", "ADULT", "ELDER"};
            snprintf(line, sizeof line, "%s / %s", f->name, stages[f->stage]);
            text(fb, stride, 14, 90, 0xe7f4dc, line);
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
            companion_care_text(t, i, line, sizeof line);
            paragraph(fb, stride, 198, 0xe7f4dc, line);
            int spot = tank_favorite_decor(t, i);
            snprintf(line, sizeof line, "FAVORITE: %s", spot < 0 ? "THE REEF" : SD_ITEMS[spot].name);
            render_text(fb, stride, 14, 236, 1, 0xa7c4ba, line);
            if (companion_rpg_contributor(t, life->id))
                render_text(fb, stride, 14, 250, 1, 0xffd998, "LOST CURRENT: CHAPTER GUIDE");
            snprintf(line, sizeof line, "FIRSTS: %u", bits(life->milestones));
            button(fb, stride, 12, 264, 205, line);
            snprintf(line, sizeof line, "KEEPSAKES: %u", bits(life->adventures));
            button(fb, stride, 231, 264, 205, line);
        }
    } else if (page == 9) {
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
            snprintf(line, sizeof line, "Snail: %ld algae cells cleaned.", (long)t->snail_grazed);
            render_text_wrap(fb, stride, 14, 120, 420, 0xa7c4ba, line);
            snprintf(line, sizeof line, "Shrimp: %ld meals foraged.", (long)t->shrimp_eaten);
            render_text_wrap(fb, stride, 14, 160, 420, 0xa7c4ba, line);
            snprintf(line, sizeof line, "Urchin: %.0f grass pixels trimmed.", (double)t->urchin_grazed_px);
            render_text_wrap(fb, stride, 14, 200, 420, 0xa7c4ba, line);
            snprintf(line, sizeof line, "Seahorse: +%.1f shared trust.", (double)t->sh_trust_passed);
            render_text_wrap(fb, stride, 14, 234, 420, 0xa7c4ba, line);
        }
        snprintf(line, sizeof line, "NEXT / HELPERS (%d/%d)", leaf + 1, memory_pages + 1);
        button(fb, stride, 12, 270, 424, line);
    } else if (page == 10) {
        render_tank_emblem(fb, stride, 14, 86, p->emblem);
        render_text(fb, stride, 46, 90, 1, 0xe7f4dc, t->aquariums.name);
        snprintf(line, sizeof line, "SUBTITLE: %s", p->subtitle ? companion_subtitle(p->subtitle) : "NONE");
        button(fb, stride, 12, 124, 424, line);
        snprintf(line, sizeof line, "EMBLEM: %s", companion_emblem(p->emblem));
        button(fb, stride, 12, 165, 424, line);
        companion_date_text(p->founded_unix, date, sizeof date);
        snprintf(line, sizeof line, "Founded: %s", date);
        render_text(fb, stride, 14, 212, 1, 0xa7c4ba, line);
        companion_date_text(p->backup_unix, date, sizeof date);
        snprintf(line, sizeof line, "Last SD backup: %s", p->backups ? date : "NOT RECORDED");
        render_text(fb, stride, 14, 236, 1, 0xa7c4ba, line);
        snprintf(line, sizeof line, "SIGHTINGS %lu", (unsigned long)p->visits);
        render_text(fb, stride, 14, 266, 1, 0xa7c4ba, line);
        uint8_t flags = t->story.flags;
        button(fb, stride, 231, 256, 205, flags & STORY_SEASONS_OFF ? "SEASONS: OFF" :
                                          flags & STORY_SEASONS_SOUTH ? "SEASONS: SOUTH" : "SEASONS: NORTH");
        paragraph(fb, stride, 306, 0xa7c4ba, "Badge + subtitle: aquarium list and summaries.");
    } else if (page == 11) {
        int item = leaf % SD_ITEM_COUNT;
        text(fb, stride, 14, 90, 0xe7f4dc, SD_ITEMS[item].name);
        render_text(fb, stride, 14, 124, 1, 0xa7c4ba, SD_ITEMS[item].words);
        render_text(fb, stride, 14, 144, 1, 0xa7c4ba, SD_ITEMS[item].words2);
        bool owned = item < 32 ? !!(t->sd_unlocks & (1u << item)) :
            item < 36 ? !!(t->aquariums.stat_upgrades & (1u << (item - 32))) :
            !!(p->scenery_unlocks & (1u << (item - 36)));
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
    } else if (page == 12) {
        static const char *const keepsakes[] = {"Lantern shell", "Pearl bookmark", "Star stone"};
        static const char *const tales[] = {
            "A shell that recalls the lagoon's gentle lights.",
            "A tiny marker from a library beneath the tide.",
            "A smooth stone found under a sky of stars."
        };
        for (int j = 0; j < 3; j++) {
            bool have = t->n_fish && (p->active[i].adventures & (1u << j));
            snprintf(line, sizeof line, "%s / %s", keepsakes[j], have ? "BROUGHT HOME" : "NOT FOUND");
            render_text(fb, stride, 14, 96 + j * 62, 1, have ? 0xffd998 : 0xa7c4ba, line);
            render_text_wrap(fb, stride, 14, 116 + j * 62, 420, 0xa7c4ba, tales[j]);
        }
        render_text(fb, stride, 14, 278, 1, 0xa7c4ba, "Find more in Adventures.");
    } else if (page == 13) {
        static const char *const hints[] = {
            "Watch a calm fish explore the bubble column.",
            "Leave room for a little unhurried exploration.",
            "Let seagrass grow into a quiet resting place.",
            "Calm nearby fish get to know one another.",
            "Fed, rested fish sometimes make a playful dart.",
            "Reef decorations give curious fish a destination.",
            "Use SAY HELLO in Care to build a bond.",
            "Say hello, then try a bond-unlocked trick.",
            "Send a fish to Lantern lagoon; wait for its return.",
            "Send a fish to Pearl library; wait for its return.",
            "Send a fish to Starlight cove; wait for its return.",
            "Patient care gives a young fish time to grow."
        };
        int d = leaf % COMPANION_DISCOVERY_COUNT;
        text(fb, stride, 14, 94, 0xe7f4dc, COMPANION_DISCOVERIES[d]);
        render_text_wrap(fb, stride, 14, 140, 420, 0xa7c4ba, hints[d]);
        render_text(fb, stride, 14, 180, 1, 0xa7c4ba, "No streaks, deadlines, or penalties.");
        button(fb, stride, 12, 240, 205, "< HINT");
        button(fb, stride, 231, 240, 205, "HINT >");
    } else if (page == 15) {
        family_render(t, i, fb, stride);
    } else if (page == 19) {
        genes_render(t, i, fb, stride);
    } else if (page == 16) {
        const tank_story_t *s = &t->story;
        for (int j = 0; j < VISITOR_SPECIES; j++) {
            bool seen = s->seen[j] > 0;
            snprintf(line, sizeof line, "%s", seen ? COMPANION_VISITORS[j].name : "???");
            render_text(fb, stride, 14, 86 + j * 34, 2, seen ? 0xe7f4dc : 0x809b9a, line);
            if (seen) {
                if (s->first_seen_s[j]) snprintf(line, sizeof line, "x%u, hour %lu", s->seen[j], (unsigned long)((s->first_seen_s[j] - 1) / 3600));
                else snprintf(line, sizeof line, "x%u, pre-log", s->seen[j]);
            } else snprintf(line, sizeof line, "%s", j == VISITOR_SQUID ? "Very rare." : "Not seen yet.");
            render_text(fb, stride, 214, 92 + j * 34, 1, seen ? 0xa7c4ba : 0x809b9a, line);
        }
        if (p->visits && s->seen[s->last_species]) render_text_wrap(fb, stride, 14, 260, 420, 0xffd998, COMPANION_VISITORS[s->last_species].words);
        else render_text_wrap(fb, stride, 14, 260, 420, 0xa7c4ba, "Visitors come only while you watch.");
    } else if (page == 14 && t->n_fish) {
        uint32_t milestones = p->active[i].milestones;
        text(fb, stride, 14, 90, 0xe7f4dc, t->fish[i].name);
        int n = 0;
        for (int j = 0; j < MS_FISH_COUNT; j++) if (milestones & (1u << j)) {
            if (n / 6 == leaf) render_text(fb, stride, 14, 124 + (n % 6) * 24, 1, 0xa7c4ba, MS_NAMES[j]);
            n++;
        }
        if (!n) render_text_wrap(fb, stride, 14, 124, 420, 0xa7c4ba, "Life firsts appear through patient care.");
        button(fb, stride, 304, 264, 132, "MORE");
    }
    polish_nav(fb, stride);
    if (page == 9) render_text(fb, stride, 14, 344, 1, 0xa7c4ba, "Observed here. No offline stories.");
}
static void result(bool ok) { snprintf(notice, sizeof notice, "%s", ok ? "Saved. No hurry, ever." : "Not ready or save failed. Retry."); }
void companion_open(void) { page = section = selected = leaf = story = creature = 0; wardrobe = WEAR_TOP_HAT; color_draft = 0; draft_pending = false; confirm_departure = false; notice[0] = 0; }
void companion_open_section(int target) { companion_open(); page = section = target; draft_pending = target == 5; }
void companion_open_creature(int kind) {
    companion_open_section(5);
    if (kind >= 0 && kind < 4) creature = kind;
}
static void prepare_draft(const tank_t *t) { if (draft_pending) { color_draft = t->companion.creature_colors[creature]; draft_pending = false; } }
void companion_render(const tank_t *t, uint16_t *fb, int stride) {
    prepare_draft(t);
    const companion_state_t *s = &t->companion;
    unsigned cosmetic = s->cosmetic <= 3 ? s->cosmetic : 0;
    render_rect(fb, stride, 0, 0, PAGE_W, PAGE_H, 0x102b36);
    render_rect_edge(fb, stride, 3, 3, PAGE_W - 6, PAGE_H - 6, frame_colors[cosmetic]);
    const char *title = page == 0 ? "FISH INTERACTION" : page == 1 ? "DISCOVERIES" : page == 2 ? "ADVENTURES" :
                        page == 4 ? "WARDROBE" : page == 5 ? "CREATURE COLORS" : page == 6 ? "GEAR COLORS" : page == 7 ? "SAND & WATER" :
                        page == 8 ? "A FISH'S LIFE" : page == 9 ? "WHILE YOU WATCHED" : page == 10 ? "TANK IDENTITY" :
                        page == 11 ? "OBJECT STORIES" : page == 12 ? "ADVENTURE KEEPSAKES" : page == 13 ? "DISCOVERY HINTS" :
                        page == 14 ? "LIFE FIRSTS" : page == 15 ? "FAMILY TREE" : page == 16 ? "VISITOR LOG" :
                        page == 18 ? "DISPLAY REWARDS" : page == 19 ? "FISH GENES" :
                        page == 20 ? "THE LOST CURRENT" : page == 21 ? "CAMPAIGN JOURNAL" :
                        page == 22 ? "STORY HELP" : page == 23 ? "STORY EVENT" : "BOND & TRICKS";
    text(fb, stride, 14, 15, frame_colors[cosmetic], title);
    button(fb, stride, 346, 8, 90, "BACK");
    char context[48];
    if (page == 20) companion_rpg_context(t, selected < t->n_fish ? selected : 0, context, sizeof context);
    else snprintf(context, sizeof context, "Your aquarium, your pace.");
    if (page == 20)
        render_text_wrap(fb, stride, 14, 49, 420, notice[0] ? 0xffd998 : 0xa7c4ba, notice[0] ? notice : context);
    else render_text(fb, stride, 14, 55, 1, notice[0] ? 0xffd998 : 0xa7c4ba,
                     notice[0] ? notice : context);
    char line[64];
    if (page >= 20 && page <= 23) { rpg_render(t, fb, stride); return; }
    if (page >= 8) polish_render(t, fb, stride);
    if (page >= 8 && page != 18) {
        return;
    }
    if (page == 18) {
        paragraph(fb, stride, 84, 0xa7c4ba, "Complete discovery sets, then claim their display rewards. No care bonuses.");
        for (int j = 0; j < 3; j++) {
            static const char *const names[] = {"NATURE", "FRIENDSHIP", "ADVENTURES"};
            static const unsigned masks[] = {0x3f, 0xc0, 0x700}, totals[] = {6, 2, 3};
            snprintf(line, sizeof line, "%s %u/%u: %s", names[j], bits(s->discoveries & masks[j]), totals[j],
                     s->collections & (1u << j) ? "CLAIMED" : companion_collection_ready(t, j) ? "TAP TO CLAIM" : "KEEP DISCOVERING");
            button(fb, stride, 12, 130 + j * 34, 424, line);
        }
        button(fb, stride, 12, 242, 205, s->decor ? "TROPHIES: SHOW" : "TROPHIES: HIDE");
        static const char *const frames[] = {"ORIGINAL", "NATURE", "FRIEND", "STORY"};
        snprintf(line, sizeof line, "FRAME: %s >", frames[cosmetic]);
        render_rect(fb, stride, 231, 242, 205, 30, 0x264d55);
        render_text(fb, stride, 239, 253, 1, 0xe7f4dc, line);
        paragraph(fb, stride, 282, 0xa7c4ba, "Trophies sit on the sand. Frames change border colors, not the font. Claim a set to unlock its frame.");
    } else if (page == 0) {
        if (t->n_fish) {
            int i = selected < t->n_fish ? selected : 0;
            const companion_bond_t *b = &s->active[i];
            snprintf(line, sizeof line, "%s BOND %u TRUST %.1f", t->fish[i].name, b->bond, (double)t->fish[i].trust);
            render_text(fb, stride, 14, 88, 1, 0xe7f4dc, line);
            button(fb, stride, 304, 110, 132, "NEXT FISH");
            button(fb, stride, 12, 110, 278, "SAY HELLO (+5 BOND)");
            render_text(fb, stride, 14, 148, 1, 0xa7c4ba, "Hello every 30s. Bond never fades.");
            companion_favorite_label(t, i, line, sizeof line);
            render_text(fb, stride, 14, 170, 1, 0xffd998, line);
            companion_care_text(t, i, line, sizeof line);
            paragraph(fb, stride, 194, 0xa7c4ba, line);
            static const char *const tricks[] = {"DART", "BUBBLES", "FOLLOW"};
            static const unsigned need[] = {10, 25, 50};
            for (int j = 0; j < 3; j++) {
                snprintf(line, sizeof line, "%s%s", tricks[j], b->bond >= need[j] ? "" : " (LOCKED)");
                button(fb, stride, 12 + 146 * j, 244, 132, line);
            }
            render_text(fb, stride, 14, 282, 1, 0xa7c4ba, "TRICKS: 10 / 25 / 50 BOND.");
            button(fb, stride, 12, 306, 424, "HOW BONDS WORK");
        } else paragraph(fb, stride, 94, 0xa7c4ba, "No fish at home. Check the Fish Bank or Adventures.");
    } else if (page == 7) {
        snprintf(line, sizeof line, "SAND: %s", render_palette_name(true, t->sand_style));
        button(fb, stride, 12, 100, 424, line);
        snprintf(line, sizeof line, "WATER: %s", render_palette_name(false, t->water_style));
        button(fb, stride, 12, 160, 424, line);
        text(fb, stride, 14, 218, 0xa7c4ba, "Upgrade palettes in Tank > Shop.");
    } else if (page == 1) {
        snprintf(line, sizeof line, "DISCOVERIES %u/12 / PAGE %d/3", bits(s->discoveries), leaf + 1);
        render_text(fb, stride, 14, 91, 1, 0xe7f4dc, line);
        for (int j = 0; j < 4; j++) {
            int id = leaf * 4 + j;
            snprintf(line, sizeof line, "%s %s", s->discoveries & (1u << id) ? "+" : "-", COMPANION_DISCOVERIES[id]);
            button(fb, stride, 12, 121 + j * 38, 424, line);
        }
        button(fb, stride, 12, 286, 205, "DISPLAY REWARDS");
        button(fb, stride, 231, 286, 205, "NEXT PAGE");
        render_text(fb, stride, 14, 326, 1, 0xa7c4ba, "+ = FOUND. TAP A ROW FOR A HINT.");
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
        button(fb, stride, 12, 326, 424, "THE LOST CURRENT: STORY RPG");
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
    }
}
bool companion_tap(tank_t *t, float frame_x, float frame_y) {
    prepare_draft(t);
    const companion_state_t *s = &t->companion;
    float x = render_page_x(frame_x), y = render_page_y(frame_y);
    if (x >= 346 && x < 436 && y >= 8 && y < 38) {
        if (page == 23) { page = 20; notice[0] = 0; return false; }
        if (page == 22) { page = 21; notice[0] = 0; return false; }
        if (page == 21) { page = 20; notice[0] = 0; return false; }
        if (page == 6) { page = 4; return false; }
        if (page == 3) { page = 0; notice[0] = 0; return false; }
        if (page == 12 || page == 14) { page = 8; leaf = 0; notice[0] = 0; return false; }
        if (page == 13) { page = 1; leaf /= 4; notice[0] = 0; return false; }
        if (page != section) { page = section; leaf = 0; notice[0] = 0; return false; }
        return true;
    }
    if (page == 20) {
        if (x >= 304 && x < 436 && y >= 82 && y < 112 && t->n_fish) {
            selected = (selected + 1) % t->n_fish; notice[0] = 0;
        } else if (x >= 12 && x < 144 && y >= 118 && y < 148) {
            page = 21; notice[0] = 0;
        } else if (t->rpg.chapter >= 6 && x >= 12 && x < 436 && y >= 304 && y < 334) {
            page = 21; notice[0] = 0;
        } else if (t->rpg.chapter < 6 && t->n_fish && x >= 12 && x < 436 && y >= 232 && y < 334) {
            int choice = (int)(y - 232) / 36;
            if ((int)(y - 232) % 36 < 30) {
                int before = t->rpg.chapter;
                bool ok = companion_rpg_choose(t, selected < t->n_fish ? selected : 0, choice);
                if (ok) {
                    rpg_chapter_finished = t->rpg.chapter != before;
                    page = 23; notice[0] = 0;
                } else snprintf(notice, sizeof notice, "Save failed. Story unchanged; retry.");
            }
        }
    } else if (page == 21) {
        if (y >= 310 && y < 340) {
            if (x >= 12 && x < 217) { page = 22; notice[0] = 0; }
            else if (x >= 231 && x < 436) { page = 20; notice[0] = 0; }
        }
    } else if (page == 23) {
        if (x >= 12 && x < 436 && y >= 304 && y < 334) { page = 20; notice[0] = 0; }
    } else if (page == 22) {
        return false;
    } else if (page == 18) {
        if (x >= 12 && x < 436 && y >= 130 && y < 228) {
            int set = (int)(y - 130) / 34;
            if ((int)(y - 130) % 34 >= 30) return false;
            if (s->collections & (1u << set)) snprintf(notice, sizeof notice, "Claimed. Choose its frame below.");
            else if (!companion_collection_ready(t, set)) snprintf(notice, sizeof notice, "Complete this discovery set first.");
            else result(companion_collection_claim(t, set));
        } else if (y >= 242 && y < 272 && x >= 12 && x < 217) result(companion_set_decor(t, !s->decor));
        else if (y >= 242 && y < 272 && x >= 231 && x < 436) {
            if (!s->collections) snprintf(notice, sizeof notice, "Claim a set to unlock menu frames.");
            else {
                int next = s->cosmetic;
                do { next = (next + 1) % 4; } while (next && !(s->collections & (1u << (next - 1))));
                result(companion_cosmetic(t, next));
            }
        }
    } else if (page >= 8) {
        bool fish = page == 8 || page == 12 || page == 14 || page == 15 || page == 19;
        if (fish && y >= 306 && y < 336 && x >= 12 && x < 436) {
            int tab = (int)(x - 12) / 146;
            if (tab < 3 && x < 144 + tab * 146) {
                static const int fish_pages[] = {8, 15, 19};
                page = fish_pages[tab]; leaf = 0; notice[0] = 0;
            }
        } else if ((page == 9 || page == 11) && y >= 306 && y < 336 &&
                   ((x >= 12 && x < 217) || (x >= 231 && x < 436))) {
            page = x < 217 ? 9 : 11; leaf = 0; notice[0] = 0;
        } else if ((page == 8 || page == 15 || page == 19) && y >= 84 && y < 116 && x >= 304 && x < 436 && t->n_fish)
            selected = (selected + 1) % t->n_fish;
        else if (page == 10 && y >= 256 && y < 286 && x >= 231 && x < 436) {
            uint8_t f = t->story.flags, next = f & STORY_SEASONS_OFF ? 0 : f & STORY_SEASONS_SOUTH ? STORY_SEASONS_OFF : STORY_SEASONS_SOUTH;
            snprintf(notice, sizeof notice, "%s", companion_set_seasons(t, next) ?
                     (next == STORY_SEASONS_OFF ? "Seasons off. The floor stays plain." : "Seasons follow the clock.") :
                     "Save failed; seasons unchanged.");
        }
        else if (page == 8 && y >= 264 && y < 294) {
            if (x >= 12 && x < 217) { page = 14; leaf = 0; }
            else if (x >= 231 && x < 436) page = 12;
        } else if (page == 9 && y >= 270 && y < 300 && x >= 12 && x < 436) {
            int memory_pages = (t->polish.recap_count + 2) / 3;
            leaf = (leaf + 1) % ((memory_pages ? memory_pages : 1) + 1);
        }
        else if (page == 10 && x >= 12 && x < 436 && ((y >= 124 && y < 154) || (y >= 165 && y < 195)))
            snprintf(notice, sizeof notice, "%s", companion_identity(t, y >= 165) ?
                     "Identity saved." : "Save failed; identity unchanged.");
        else if ((page == 11 || page == 13) && y >= (page == 11 ? 258 : 240) &&
                 y < (page == 11 ? 288 : 270) && ((x >= 12 && x < 217) || (x >= 231 && x < 436))) {
            int count = page == 11 ? SD_ITEM_COUNT : COMPANION_DISCOVERY_COUNT;
            leaf = (leaf + count + (x < 217 ? -1 : 1)) % count;
        } else if (page == 14 && y >= 264 && y < 294 && x >= 304 && x < 436) {
            unsigned pages = (bits(t->polish.active[selected < t->n_fish ? selected : 0].milestones) + 5) / 6;
            leaf = (leaf + 1) % (pages ? pages : 1);
        }
    } else if (page == 0) {
        if (y >= 306 && y < 336 && x >= 12 && x < 436) { page = 3; notice[0] = 0; }
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
        if (x >= 12 && x < 436 && y >= 121 && y < 265 && (int)(y - 121) % 38 < 30) {
            leaf = leaf * 4 + (int)(y - 121) / 38; page = 13; notice[0] = 0;
        } else if (x >= 231 && x < 436 && y >= 286 && y < 316) leaf = (leaf + 1) % 3;
        else if (x >= 12 && x < 217 && y >= 286 && y < 316) { page = 18; notice[0] = 0; }
    } else if (page == 2) {
        if (x >= 12 && x < 436 && y >= 326 && y < 356) {
            page = 20; notice[0] = 0; confirm_departure = false;
        } else if (x >= 12 && x < 144 && y >= 244 && y < 274 && !t->companion.adventure) { story = (story + 1) % 3; confirm_departure = false; }
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
    if (!s->decor || !(s->collections || s->game_challenges || t->rpg.chapter)) return;
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
    if (t->rpg.chapter) {
        render_rect(fb, stride, 108, 329, 42, 19, 0xd6bc80);
        render_rect(fb, stride, 112, 326, 34, 25, 0xe8d9a9);
        for (int chapter = 0; chapter < t->rpg.chapter; chapter++)
            render_rect(fb, stride, 115 + chapter * 5, 335 + chapter % 2 * 6, 3, 3, 0x3b6d6b);
    }
}
