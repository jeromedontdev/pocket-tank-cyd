#include "progression.h"
#include "aquarium_store.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#endif

static char tank_directory[1024];
bool aquarium_port_ready(void) {
    const char *save = getenv("POCKET_TANK_SAVE");
    int n = snprintf(tank_directory, sizeof tank_directory, "%s.tanks", save ? save : "tank.sav");
    if (n < 0 || n >= (int)sizeof tank_directory) { fprintf(stderr, "Aquarium directory path too long\n"); return false; }
#ifdef _WIN32
    int result = _mkdir(tank_directory);
#else
    int result = mkdir(tank_directory, 0755);
#endif
    if (result != 0 && errno != EEXIST) { perror("Aquarium directory"); return false; }
    return true;
}
void aquarium_port_id(uint32_t id[2]) {
    id[0] = (uint32_t)time(NULL); id[1] = (uint32_t)clock() ^ 0xc1d7a453u;
}
bool aquarium_port_read(const uint32_t id[2], int slot, void *data, size_t size) {
    return aquarium_port_ready() && aquarium_store_read(tank_directory, id, slot, data, size);
}
bool aquarium_port_read_sized(const uint32_t id[2], int slot, void *data, size_t size, size_t *got) {
    if (got) *got = 0;
    return aquarium_port_ready() && aquarium_store_read_sized(tank_directory, id, slot, data, size, got);
}
bool aquarium_port_write(const uint32_t id[2], int slot, const void *data, size_t size) {
    return aquarium_port_ready() && aquarium_store_write(tank_directory, id, slot, data, size);
}

static const char *path(void) {
    const char *name = getenv("POCKET_TANK_SAVE");
    return name ? name : "tank.sav";
}
static char profile_directory[1024];
static const uint32_t profile_id[2] = {0x50524631u, 0x41435456u};
static bool save_blocked;
static bool profile_ready(void) {
    int n = snprintf(profile_directory, sizeof profile_directory, "%s.profiles", path());
    if (n < 0 || n >= (int)sizeof profile_directory) return false;
#ifdef _WIN32
    int result = _mkdir(profile_directory);
#else
    int result = mkdir(profile_directory, 0755);
#endif
    return result == 0 || errno == EEXIST;
}
static int profile_exists(int slot) {
    int found = 0;
    for (int copy = 0; copy < 2; copy++) {
        char name[1200]; struct stat info;
        snprintf(name, sizeof name, "%s/%08lx%08lx-%d.%c", profile_directory,
            (unsigned long)profile_id[0], (unsigned long)profile_id[1], slot, copy ? 'b' : 'a');
        if (stat(name, &info) == 0) found = 1;
        else if (errno != ENOENT) return -1;
    }
    return found;
}
static bool remove_active(void) {
    bool ok = true;
    for (int copy = 0; copy < 2; copy++) {
        char name[1200];
        snprintf(name, sizeof name, "%s/%08lx%08lx-0.%c", profile_directory,
            (unsigned long)profile_id[0], (unsigned long)profile_id[1], copy ? 'b' : 'a');
        if (remove(name) != 0 && errno != ENOENT) ok = false;
    }
    return ok;
}
bool persist_port_blocked(void) { return save_blocked; }
bool persist_port_load(void *buf, size_t max, size_t *got) {
    *got = 0;
    if (!profile_ready()) { save_blocked = true; return false; }
    int active = profile_exists(0), marker = profile_exists(1);
    if (active < 0 || marker < 0) { save_blocked = true; return false; }
    uint32_t state = 0;
    if (marker) {
        if (!aquarium_store_read(profile_directory, profile_id, 1, &state, sizeof state) || (state != 1 && state != 2)) {
            save_blocked = true; return false;
        }
        if (state == 2) return false;
    }
    if (active) {
        if (aquarium_store_read_sized(profile_directory, profile_id, 0, buf, max, got)) return true;
        save_blocked = true; return false;
    }
    if (marker) {
        save_blocked = true;
        return false;
    }
    FILE *file = fopen(path(), "rb");
    if (!file) {
        if (errno != ENOENT) { perror("Simulator save read"); save_blocked = true; }
        return false;
    }
    *got = fread(buf, 1, max, file);
    bool ok = !ferror(file) && fgetc(file) == EOF;
    if (fclose(file) != 0) ok = false;
    if (!ok || !*got) { perror("Simulator save read"); save_blocked = true; }
    return ok && *got > 0;
}
bool persist_port_save(const void *buf, size_t size) {
    if (save_blocked || !profile_ready()) return false;
    uint32_t initialized = 1, state = 0;
    int marker = profile_exists(1);
    if (marker < 0 || (marker && !aquarium_store_read(profile_directory, profile_id, 1, &state, sizeof state))) return false;
    if (state == 2) {
        if (!remove_active() || !aquarium_store_write(profile_directory, profile_id, 0, buf, size)) return false;
        return aquarium_store_write(profile_directory, profile_id, 1, &initialized, sizeof initialized);
    }
    if (state != 1 && !aquarium_store_write(profile_directory, profile_id, 1, &initialized, sizeof initialized)) return false;
    if (!aquarium_store_write(profile_directory, profile_id, 0, buf, size)) return false;
    return true;
}
bool persist_port_erase(void) {
    if (!profile_ready()) return false;
    uint32_t wiped = 2;
    if (!aquarium_store_write(profile_directory, profile_id, 1, &wiped, sizeof wiped)) return false;
    if (remove(path()) != 0 && errno != ENOENT) fprintf(stderr, "Profile wiped; legacy save cleanup failed\n");
    if (!remove_active()) fprintf(stderr, "Profile wiped; obsolete copies await cleanup\n");
    save_blocked = false;
    return true;
}
const char *version_port_string(void) { return "CYD simulator"; }
int64_t clock_port_now_unix(void) { return (int64_t)time(NULL); }
