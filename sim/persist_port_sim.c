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
bool aquarium_port_write(const uint32_t id[2], int slot, const void *data, size_t size) {
    return aquarium_port_ready() && aquarium_store_write(tank_directory, id, slot, data, size);
}

static const char *path(void) {
    const char *name = getenv("POCKET_TANK_SAVE");
    return name ? name : "tank.sav";
}
bool persist_port_load(void *buf, size_t max, size_t *got) {
    FILE *file = fopen(path(), "rb");
    if (!file) {
        if (errno != ENOENT) perror("Simulator save read");
        return false;
    }
    *got = fread(buf, 1, max, file);
    bool ok = !ferror(file);
    if (fclose(file) != 0) ok = false;
    if (!ok) perror("Simulator save read");
    return ok && *got > 0;
}
bool persist_port_save(const void *buf, size_t size) {
    FILE *file = fopen(path(), "wb");
    if (!file) { perror("Simulator save write"); return false; }
    bool ok = fwrite(buf, 1, size, file) == size;
    if (fclose(file) != 0) ok = false;
    if (!ok) perror("Simulator save write");
    return ok;
}
bool persist_port_erase(void) {
    if (remove(path()) == 0 || errno == ENOENT) return true;
    perror("Simulator save erase"); return false;
}
const char *version_port_string(void) { return "CYD simulator"; }
int64_t clock_port_now_unix(void) { return (int64_t)time(NULL); }
