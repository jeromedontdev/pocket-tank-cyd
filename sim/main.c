/* Native CYD simulator bridge. Python supplies the window and file dialogs. */
#include "tank.h"
#include "advisor.h"
#include "render.h"
#include "home_navigation.h"
#include "progression.h"
#include "setup.h"
#include "notice.h"
#include "icons.h"
#include "update.h"
#include "companion.h"
#include "weather.h"
#include <math.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#define API __declspec(dllexport)
#else
#define API __attribute__((visibility("default")))
#endif

static tank_t tank;
static uint16_t frame[TANK_MAX_W * TANK_MAX_H];
static unsigned char *bank;
static size_t bank_size;
static int page, selected = -1, bright = 100, bright_saved = 100;
static float px, py, held, last_x, last_y;
static bool pressed;
static bool from_home, home_tanks;
static int child_parent = 1;
static float wipe_left;
static char error[128];
void net_sim_advance(float dt);
#ifdef __EMSCRIPTEN__
static render_device_t browser_device = {0, 1, 1, -1, SCENE_BALANCED};
static bool browser_preferences_load(void) {
    unsigned char values[4];
    FILE *file = fopen("device.bin", "rb");
    if (!file) return errno == ENOENT;
    bool ok = fread(values, 1, sizeof values, file) == sizeof values &&
        fgetc(file) == EOF && !ferror(file) &&
        values[0] < 3 && values[1] < 3 && values[2] < 2 && values[3] < SCENE_DETAIL_COUNT;
    if (fclose(file)) ok = false;
    if (ok) {
        browser_device.room_mode = values[0]; browser_device.led_mode = values[1];
        browser_device.auto_backup = values[2]; browser_device.scene_detail = values[3];
    }
    render_set_device(&browser_device);
    return ok;
}
static bool browser_brightness_valid(void) {
    FILE *file = fopen("tank.sav.brightness", "rb");
    if (!file) return errno == ENOENT;
    int value = fgetc(file);
    bool ok = value >= 10 && value <= 100 && fgetc(file) == EOF && !ferror(file);
    if (fclose(file)) ok = false;
    return ok;
}
static bool browser_preferences_save(void) {
    unsigned char values[] = {browser_device.room_mode, browser_device.led_mode,
                             browser_device.auto_backup, browser_device.scene_detail};
    FILE *file = fopen("device.bin.next", "wb");
    if (!file) return false;
    bool ok = fwrite(values, 1, sizeof values, file) == sizeof values;
    if (fclose(file)) ok = false;
    if (ok) ok = rename("device.bin.next", "device.bin") == 0;
    if (!ok) remove("device.bin.next");
    return ok;
}
#endif

static bool brightness_path(char *out, size_t cap) {
    const char *explicit_path = getenv("POCKET_TANK_BRIGHTNESS");
    const char *save = getenv("POCKET_TANK_SAVE");
    int n = explicit_path ? snprintf(out, cap, "%s", explicit_path) :
        snprintf(out, cap, "%s.brightness", save ? save : "tank.sav");
    return n > 0 && (size_t)n < cap;
}
static void brightness_load(void) {
    char path[1024]; bright = bright_saved = 100;
    if (!brightness_path(path, sizeof path)) return;
    FILE *file = fopen(path, "rb");
    if (!file) return;
    int v = fgetc(file), end = fgetc(file);
    bool ok = !ferror(file);
    if (fclose(file)) ok = false;
    if (ok && end == EOF && v >= 10 && v <= 100) bright = bright_saved = v;
}
static bool brightness_save(void) {
    if (bright == bright_saved) return true;
    char path[1024], next[1030];
    if (!brightness_path(path, sizeof path)) return false;
    snprintf(next, sizeof next, "%s.next", path);
    FILE *file = fopen(next, "wb");
    if (!file) return false;
    bool ok = fputc(bright, file) != EOF;
    if (fclose(file)) ok = false;
#ifdef _WIN32
    if (ok) ok = MoveFileExA(next, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
    if (ok) ok = rename(next, path) == 0;
#endif
    if (!ok) { remove(next); return false; }
    bright_saved = bright;
    return true;
}

const icon_t *icons_resolve(const icon_t *icon) {
    static icon_t view;
    size_t pixels = (size_t)icon->w * icon->h;
    if (icon->offset > bank_size || pixels * 3 > bank_size - icon->offset) {
        fprintf(stderr, "Invalid icon bank offset %u\n", (unsigned)icon->offset);
        abort();
    }
    view = *icon;
    view.rgb = (const uint16_t *)(bank + icon->offset);
    view.a = bank + icon->offset + pixels * 2;
    return &view;
}

API int sim_init(const void *icons, size_t size) {
    error[0] = 0;
#ifdef __EMSCRIPTEN__
    if (!browser_preferences_load() || !browser_brightness_valid()) {
        snprintf(error, sizeof error, "Browser device preferences are corrupt or unreadable");
        return 0;
    }
#endif
    brightness_load();
    weather_init();
    render_clock_synchronized(true);
    bank = malloc(size);
    void *scratch = calloc(1, render_decor_scratch_size());
    if (!bank || !scratch) {
        fprintf(stderr, "Simulator allocation failed\n");
        snprintf(error, sizeof error, "Simulator allocation failed");
        free(bank); free(scratch); bank = NULL; return 0;
    }
    memcpy(bank, icons, size); bank_size = size;
    render_set_decor_scratch(scratch);
    tank_init(&tank, 12345);
    progression_boot(&tank);
    if (!progression_storage_ready()) {
        snprintf(error, sizeof error, "Profile storage is corrupt or unreadable");
        render_set_decor_scratch(NULL);
        free(bank); free(scratch); bank = NULL; bank_size = 0;
        return 0;
    }
    notice_sync(&tank);
    if (progression_setup_pending()) setup_begin(&tank);
    return 1;
}

API int sim_save(void) {
#ifdef __EMSCRIPTEN__
    if (!browser_preferences_save() || !brightness_save()) return 0;
#endif
    return progression_save(&tank);
}
API int sim_feed_at(float x) {
    if (page || setup_active() || update_active() || wipe_left > 0 ||
        tank.tool != TOOL_HAND || !progression_mode_chosen(&tank) || !isfinite(x) || x < 0 || x >= TANK_W) return 0;
    int before = 0, after = 0;
    for (int i = 0; i < MAX_FOOD; i++) before += tank.food[i].alive;
    tank_feed(&tank, x, 3);
    for (int i = 0; i < MAX_FOOD; i++) after += tank.food[i].alive;
    return after > before;
}
API float sim_food_x(int index) {
    return index >= 0 && index < MAX_FOOD && tank.food[index].alive ? tank.food[index].x : -1;
}
API char *sim_backup(void) { return progression_backup_json(&tank); }
API void sim_free(void *memory) { free(memory); }
API const char *sim_error(void) { return error; }
API int sim_network_page(void) { return update_active() ? update_page() : 0; }
API const char *sim_network_message(void) { return update_message_title(); }
API int sim_clock_active(void) { return render_clock_active(&tank); }
API int sim_brightness(void) { return bright; }
API int sim_brightness_reload(void) { brightness_load(); return bright; }
API int sim_simple_scene(void) { return render_simple_scene(); }
API int sim_scene_detail(void) { return render_scene_detail(); }
API int sim_display_brightness(void) {
    int target = bright * 255 / 100;
    return tank.night && !render_clock_active(&tank) && page != 8 ? target * 6 / 10 : target;
}
API int sim_restore(const char *json, size_t size) {
    if (!progression_restore_json(json, size, true, error, sizeof error)) return 0;
    setup_cancel(&tank);
    tank_init(&tank, 12345); progression_boot(&tank); notice_sync(&tank);
    page = 0; selected = -1;
    return 1;
}

API void sim_action(int action) {
    if (wipe_left > 0) return;
    if (action >= 0 && action <= 3) { from_home = home_tanks = false; child_parent = 1; tank_handled(&tank); tank.ui_cover = action != 0; }
    if (action >= 0 && action <= 3) setup_cancel(&tank);
    if (action == 0) { page = 0; render_shop_leave(); render_milestones_leave(); }
    else if (action == 1) page = 1;
    else if (action == 2) { render_settings_open(); page = 2; }
    else if (action == 3) page = 3;
    else if (action == 4) tank_feed(&tank, TANK_W / 2.0f, 3);
    else if (action == 5) progression_sd_grant(&tank, 25);
    else if (action == 6) tank_toggle_light(&tank);
    else if (action == 7) {
        setup_cancel(&tank);
        from_home = home_tanks = false;
        child_parent = 1;
        tank_handled(&tank); tank.ui_cover = true;
        render_life_open(); page = 9;
    }
}
API int sim_page(void) { return page; }
API int sim_setup_page(void) { return setup_active() ? setup_page() : -1; }
API int sim_mode(void) { return progression_mode(&tank); }
API int sim_mode_chosen(void) { return progression_mode_chosen(&tank); }
API int sim_width(void) { return TANK_W; }
API int sim_height(void) { return TANK_H; }
API void sim_visit(int species) { companion_debug_visit(&tank, species); }
API int sim_selected(void) { return selected; }
API int sim_tool(void) { return tank.tool; }
/* Read-only touch target for automation: do not feed/startle the tank by probing empty glass. */
API int sim_creature_target(int card) {
    for (int y = 0; y < TANK_H; y += 2) for (int x = 0; x < TANK_W; x += 2) {
        if (render_creature_card_hit(&tank, x, y) != card) continue;
        bool fish = false;
        for (int i = 0; i < tank.n_fish; i++)
            if (tank_dist(x, y, tank.fish[i].x, tank.fish[i].y) < 38) { fish = true; break; }
        if (!fish) return y * TANK_W + x;
    }
    return -1;
}

static void shop_action(int result) {
    if (result >= SHOP_TAP_INVENTORY) {
        render_shop_inventory_action(&tank, result);
        if (setup_is_place()) { render_shop_leave(); page = 0; }
    } else if (result == SHOP_TAP_TANKS) { render_aquariums_open(&tank); page = 7; }
    else if (result == SHOP_TAP_CLOSE) { render_shop_leave(); page = child_parent; }
    else if (result >= SHOP_TAP_FRY) progression_buy_fry(&tank, result - SHOP_TAP_FRY);
    else if (result >= SHOP_TAP_SELL) progression_sell(&tank, result - SHOP_TAP_SELL);
    else if (result >= SHOP_TAP_MOVE) {
        setup_begin_place(&tank, result - SHOP_TAP_MOVE); page = 0;
    } else if (result >= SHOP_TAP_BUY) {
        int item = result - SHOP_TAP_BUY;
        if (progression_buy(&tank, item) && tank_decor_placeable(item)) {
            setup_begin_place(&tank, item); page = 0;
        }
    }
}

API void sim_touch(float x, float y, int down) {
    static bool clock_wake;
    last_x = x; last_y = y;
    if (down && !pressed) { px = x; py = y; held = 0; }
    if (update_active()) {
        update_touch(x, y, down != 0);
        pressed = down != 0; return;
    }
    if (clock_wake || (down && !pressed && render_clock_active(&tank))) {
        clock_wake = down != 0; tank_handled(&tank); pressed = false; return;
    }
    if (wipe_left > 0) {
        if (!down && pressed) {
            int hit = render_confirm_hit(px, py);
            if (hit && hit == render_confirm_hit(x, y) && hypotf(x - px, y - py) < 24) {
                wipe_left = 0;
                if (hit > 0) {
                    if (!progression_reset(&tank, 12345)) {
                        fprintf(stderr, "Simulator wipe failed: erase or fresh save failed\n");
                        render_backup_status("WIPE FAILED - CHECK STORAGE", false);
                    }
                    if (progression_setup_pending()) {
                        notice_sync(&tank); selected = -1; page = 0;
                        render_shop_leave(); render_milestones_leave(); setup_begin(&tank);
                    }
                }
            }
        }
        pressed = down != 0; return;
    }
    if (!down && pressed && !setup_active() && !update_active()) {
        if (page == 7) {
            if (held < 0.35f && hypotf(x - px, y - py) < 24) {
                int result = render_aquariums_tap(&tank, px, py);
                if (result == 1) { page = home_tanks ? 9 : 3; home_tanks = false; }
                else if (result == 2) {
                    page = 0; selected = -1; setup_cancel(&tank); notice_sync(&tank);
                    render_fb_primed(NULL, 0);
                    if (progression_setup_pending()) setup_begin(&tank);
                }
            }
        } else if (page == 9) {
            if (held < 0.35f && hypotf(x - px, y - py) < 24) {
                int action = render_life_tap(&tank, px, py);
                if (action == HOME_CLOSE) { page = 0; from_home = false; }
                else if (action) {
                    from_home = true; child_parent = 9;
                    switch (home_open(&tank, action)) {
                    case HOME_DEST_BANK: page = 6; break;
                    case HOME_DEST_JOURNAL: page = 5; break;
                    case HOME_DEST_GAMES: page = 8; break;
                    case HOME_DEST_SETTINGS: page = 2; break;
                    case HOME_DEST_KEEPER: page = 1; break;
                    case HOME_DEST_SHOP: page = 3; break;
                    case HOME_DEST_TANKS: home_tanks = true; page = 7; break;
                    case HOME_DEST_NONE: break;
                    }
                }
            }
        } else if (page == 8) {
            if (held < 0.35f && hypotf(x - px, y - py) < 24 && render_games_tap(&tank, px, py)) page = child_parent;
        } else if (page == 6) {
            if (held < 0.35f && hypotf(x - px, y - py) < 24 && render_bank_tap(&tank, px, py)) page = child_parent;
        } else if (page == 5) {
            if (held < 0.35f && hypotf(x - px, y - py) < 24 && render_journal_tap(&tank, px, py)) page = child_parent;
        } else
        if (page == 1) {
            int result = render_milestones_tap(&tank, px, py);
            if (result != MS_TAP_CLOSE) child_parent = 1;
            if (result == MS_TAP_CLOSE) { progression_ack_milestones(&tank); render_milestones_leave(); page = from_home ? 9 : 0; }
            else if (result == MS_TAP_SETTINGS) { render_settings_open(); page = 2; }
            else if (result == MS_TAP_SHOP) page = 3;
            else if (result == MS_TAP_JOURNAL) { render_journal_open(); page = 5; }
            else if (result == MS_TAP_BANK) { render_bank_open(); page = 6; }
            else if (result == MS_TAP_GAMES) { render_games_open(); page = 8; tank_handled(&tank); }
            else if (result == MS_TAP_LIFE) { render_life_open(); page = 9; tank_handled(&tank); }
            else if (result >= MS_TAP_CUSTOM) { setup_begin_customize(&tank, result - MS_TAP_CUSTOM); page = 0; }
            else if (result >= MS_TAP_SELL) progression_sell_fish(&tank, result - MS_TAP_SELL);
            else if (result >= MS_TAP_RENAME) { setup_begin_rename(&tank, result - MS_TAP_RENAME); page = 0; }
        } else if (page == 3) shop_action(render_shop_tap(&tank, px, py));
        else if (page == 4) {
            int result = updates_page_touch(x, y, false);
            if (result == UPD_TAP_CLOSE) page = 2;
            else if (result == UPD_TAP_CHECK) updates_sd_status("SD INSTALLATION IS ON THE CYD", false);
            else if (result == UPD_TAP_WIFI) updates_sd_status("WI-FI UPDATES ARE ON THE CYD", false);
        } else if (page == 0 && held < 0.35f && hypotf(x - px, y - py) < 12) {
            if (!notice_dismiss()) {
                if (tank.tool != TOOL_HAND) {
                    selected = -1;
                    if (render_tool_chip_hit(&tank, px, py)) tank_set_tool(&tank, TOOL_HAND);
                } else if (render_stats_next_hit(&tank, selected, px, py)) {
                    if (render_stats_next_hit(&tank, selected, x, y)) selected = render_stats_next(&tank, selected);
                } else if (render_creature_colors_hit(&tank, selected, px, py) >= 0) {
                    int kind = render_creature_colors_hit(&tank, selected, px, py);
                    if (render_creature_colors_hit(&tank, selected, x, y) == kind) {
                        render_life_creature_open(kind);
                        from_home = false; page = 9; selected = -1;
                    }
                } else if (selected >= 0 && selected < tank.n_fish && render_tools_hit(px, py) >= 0) {
                    tank_set_tool(&tank, render_tools_hit(px, py)); selected = -1;
                } else if (selected >= 0 && selected < tank.n_fish && render_lock_hit(px, py)) {
                    tank_lock_tools(&tank, !tank.tools_locked); progression_settings_changed();
                } else if (selected >= 0 && selected < tank.n_fish && RENDER_CARD_HIT(px, py)) { render_life_open(); from_home = false; page = 9; }
                else {
                    int best = -1; float distance = 38;
                    for (int i = 0; i < tank.n_fish; i++) {
                        float d = tank_dist(px, py, tank.fish[i].x, tank.fish[i].y);
                        if (d < distance) { best = i; distance = d; }
                    }
                    if (best >= 0) selected = selected == best ? -1 : best;
                    else if (render_creature_card_hit(&tank, px, py) >= 0) {
                        int card = render_creature_card_hit(&tank, px, py);
                        if (card == RENDER_CARD_SHRIMP) {
                            int taps = tank_shrimp_tap(&tank, px, py);
                            if (taps == 1) selected = selected == card ? -1 : card;
                            else if (taps >= 3) selected = -1;
                        } else selected = selected == card ? -1 : card;
                    }
                    else if (selected >= 0) selected = -1;
                    else tank_touch_tap(&tank, px, py);
                }
            }
        }
    }
    if (page == 4 && down) updates_page_touch(x, y, true);
    if (setup_active()) setup_touch(&tank, x, y, down != 0);
    if (update_active()) update_touch(x, y, down != 0);
    if (page == 2) {
        int value, result = render_settings_touch(&tank, x, y, down != 0, &value);
        if (result == SET_TAP_CLOSE) page = child_parent;
        else if (result == SET_TAP_RECOVERED) {
            page = 0; selected = -1; setup_cancel(&tank); notice_sync(&tank); render_fb_primed(NULL, 0);
            if (progression_setup_pending()) setup_begin(&tank);
        }
        else if (result == SET_TAP_CLOCK_SHOW) { page = 0; selected = -1; tank.ui_cover = false; tank.clock_requested = true; }
        else if (result == SET_TAP_WIPE) wipe_left = 20;
        else if (result == SET_TAP_BRIGHT_PREVIEW) bright = value;
        else if (result == SET_TAP_BRIGHT) {
            bright = value;
            if (!brightness_save()) {
                bright = bright_saved;
                render_backup_status("BRIGHTNESS SAVE FAILED - RETRY", false);
            }
        }
        else if (result == SET_TAP_TOUCH) render_backup_status("TOUCH CALIBRATION IS ON THE CYD", false);
        else if (result == SET_TAP_UPDATES) {
#ifdef __EMSCRIPTEN__
            snprintf(error, sizeof error, "FIRMWARE UPDATES ARE ON THE CYD");
            render_backup_status("FIRMWARE UPDATES ARE ON THE CYD", false);
#else
            page = 4;
#endif
        }
        else if (result == SET_TAP_WIFI) {
#ifdef __EMSCRIPTEN__
            render_backup_status("WIFI IS NOT AVAILABLE IN THE BROWSER", false);
#else
            if (progression_save(&tank)) update_wifi_begin();
            else render_backup_status("SAVE FAILED - WIFI NOT OPENED", false);
#endif
        }
        else if (result == SET_TAP_SAND || result == SET_TAP_WATER) progression_save(&tank);
        else if (result == SET_TAP_ROOM || result == SET_TAP_LED || result == SET_TAP_AUTO_BACKUP || result == SET_TAP_DETAIL) {
#ifdef __EMSCRIPTEN__
            render_device_t device = browser_device;
#else
            static render_device_t device = {0, 1, 1, -1, SCENE_BALANCED};
#endif
            if (result == SET_TAP_ROOM) device.room_mode = (uint8_t)((device.room_mode + 1) % 3);
            else if (result == SET_TAP_LED) device.led_mode = (uint8_t)((device.led_mode + 1) % 3);
            else if (result == SET_TAP_DETAIL) device.scene_detail = (uint8_t)((device.scene_detail + 1) % SCENE_DETAIL_COUNT);
            else device.auto_backup = !device.auto_backup;
            render_set_device(&device);
            render_backup_status(result == SET_TAP_DETAIL ? "SCENE CHANGED - NOTHING REMOVED" :
                                 "THE SENSOR AND LED ARE ON THE CYD", false);
#ifdef __EMSCRIPTEN__
            browser_device = device;
#endif
        }
        else if (result == SET_TAP_PHOTO) render_backup_status("PHOTOS SAVE TO THE CYD'S SD CARD", false);
        else if (result == SET_TAP_BACKUP || result == SET_TAP_RESTORE)
#ifdef __EMSCRIPTEN__
            render_backup_status("USE BROWSER SAVE EXPORT / IMPORT BELOW", false);
#else
            render_backup_status("USE THE WINDOW EXPORT / RESTORE BUTTONS", false);
#endif
    }
    pressed = down != 0;
}

API const uint16_t *sim_frame(float dt) {
    if (dt < 0 || dt > 0.25f) dt = 0.04f;
    weather_tick(dt);
    if (wipe_left > 0) { wipe_left -= dt; if (wipe_left < 0) wipe_left = 0; }
    if (update_active()) {
        net_sim_advance(dt); update_tick(dt);
        render_update(frame, TANK_W, tank.clock);
        if (!update_active()) net_port_off();
        return frame;
    }
    if (pressed) {
        held += dt;
        bool card_control = render_stats_next_hit(&tank, selected, px, py) ||
            render_creature_colors_hit(&tank, selected, px, py) >= 0 ||
            (selected >= 0 && selected < tank.n_fish &&
             (render_tools_hit(px, py) >= 0 || render_lock_hit(px, py)));
        if (!page && !setup_active() && !card_control && !render_tool_chip_hit(&tank, px, py)) {
            tank_touch_drag(&tank, last_x, last_y);
            if (tank.tool == TOOL_HAND && held > .7f && fabsf(last_x - px) < 24 &&
                fabsf(last_y - py) < 24 && setup_begin_place_at(&tank, px, py))
                selected = -1;
            else if (held > 0.3f) tank_touch_hold(&tank, last_x, last_y);
        }
    }
    tank.hold_light = setup_active() || page == 8;
    tank.ui_cover = page != 0 || setup_active() || wipe_left > 0;
    if (page == 8) render_games_tick(dt);
    else { tank_tick(&tank, dt, advisor_rules); progression_tick(&tank, dt); }
    companion_tick(&tank, dt);
    if (page != 8) notice_tick(&tank, dt, tank.ui_cover || setup_birth_due());
    if (!setup_active() && page != 8) setup_poll_birth(&tank);
    if (setup_take_renamed() >= 0) page = 1;
    if (page == 1) render_milestones(&tank, frame, TANK_W);
    else if (page == 2) render_settings(&tank, frame, TANK_W, bright);
    else if (page == 3) render_shop(&tank, frame, TANK_W);
    else if (page == 4) render_updates_page(frame, TANK_W);
    else if (page == 5) render_journal(&tank, frame, TANK_W);
    else if (page == 6) render_bank(&tank, frame, TANK_W);
    else if (page == 7) render_aquariums(&tank, frame, TANK_W);
    else if (page == 8) render_games(&tank, frame, TANK_W);
    else if (page == 9) render_life(&tank, frame, TANK_W);
    else {
        render_tank(&tank, frame, TANK_W);
        if (!render_clock_active(&tank)) {
            if (selected >= 0) render_stats_card(&tank, selected, frame, TANK_W);
            render_tool_chip(&tank, selected, frame, TANK_W);
            render_sd_toast(&tank, frame, TANK_W);
            const notice_t *notice = notice_current();
            if (notice) render_notice(&tank, frame, TANK_W, notice->kind, notice->fish, notice->bit, 1 - notice->age / NOTICE_UP_S);
        }
    }
    if (setup_active()) render_setup(&tank, frame, TANK_W, tank.clock);
    if (wipe_left > 0) render_confirm_reset(frame, TANK_W, wipe_left / 20);
    return frame;
}
