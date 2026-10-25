#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif
#include "aquarium_store.h"
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <io.h>
#define sync_file(file) _commit(_fileno(file))
#else
#include <unistd.h>
#define sync_file(file) fsync(fileno(file))
#endif

typedef struct { uint32_t magic, version, sequence, length, crc, header_crc; } header_t;
#define STORE_MAGIC 0x41515943u
static uint32_t crc32(uint32_t crc, const void *data, size_t size) {
    const unsigned char *p = data;
    crc = ~crc;
    while (size--) {
        crc ^= *p++;
        for (int k = 0; k < 8; k++) crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}
static bool name(char path[192], const char *directory, const uint32_t id[2], int slot, int copy) {
    int n = snprintf(path, 192, "%s/%08lx%08lx-%d.%c", directory,
                     (unsigned long)id[0], (unsigned long)id[1], slot, copy ? 'b' : 'a');
    if (n <= 0 || n >= 192) { fprintf(stderr, "Aquarium snapshot path too long\n"); return false; }
    return true;
}
/* 0 missing, 1 valid, -1 unreadable/corrupt. Verify without a second full-size buffer. */
static int inspect(const char *path, size_t size, uint32_t *sequence) {
    FILE *file = fopen(path, "rb");
    if (!file) {
        if (errno == ENOENT) return 0;
        fprintf(stderr, "Aquarium open failed: %s (%s)\n", path, strerror(errno)); return -1;
    }
    header_t h = {0};
    bool ok = fread(&h, 1, sizeof h, file) == sizeof h && h.magic == STORE_MAGIC && h.version == 1 &&
              h.length && h.length <= size && h.sequence && h.header_crc == crc32(0, &h, offsetof(header_t, header_crc));
    unsigned char buffer[256];
    uint32_t crc = 0;
    size_t left = h.length;
    while (ok && left) {
        size_t n = left < sizeof buffer ? left : sizeof buffer;
        if (fread(buffer, 1, n, file) != n) { ok = false; break; }
        crc = crc32(crc, buffer, n); left -= n;
    }
    ok = ok && crc == h.crc && fgetc(file) == EOF && !ferror(file);
    if (fclose(file) != 0) ok = false;
    if (!ok) { fprintf(stderr, "Aquarium snapshot validation failed: %s\n", path); return -1; }
    *sequence = h.sequence; return 1;
}
static bool copies(const char *directory, const uint32_t id[2], int slot, size_t size,
                   char paths[2][192], uint32_t sequence[2], int status[2]) {
    for (int i = 0; i < 2; i++) {
        if (!name(paths[i], directory, id, slot, i)) return false;
        status[i] = inspect(paths[i], size, &sequence[i]);
    }
    return true;
}
bool aquarium_store_read(const char *directory, const uint32_t id[2], int slot, void *data, size_t size) {
    char paths[2][192]; uint32_t sequence[2] = {0}; int status[2];
    if (!copies(directory, id, slot, size, paths, sequence, status)) return false;
    int use = status[1] == 1 && (status[0] != 1 || sequence[1] > sequence[0]) ? 1 : 0;
    if (status[use] != 1) { fprintf(stderr, "No valid aquarium snapshot for tank %d\n", slot + 1); return false; }
    FILE *file = fopen(paths[use], "rb");
    if (!file) { fprintf(stderr, "Aquarium reread failed: %s\n", strerror(errno)); return false; }
    header_t h = {0};
    memset(data, 0, size); /* Append-only save tails: old SD aquariums migrate as a zero tail. */
    bool ok = fread(&h, 1, sizeof h, file) == sizeof h && h.magic == STORE_MAGIC && h.version == 1 &&
              h.sequence == sequence[use] && h.length && h.length <= size && fread(data, 1, h.length, file) == h.length &&
              h.header_crc == crc32(0, &h, offsetof(header_t, header_crc)) &&
              h.crc == crc32(0, data, h.length) && fgetc(file) == EOF && !ferror(file);
    if (fclose(file) != 0) ok = false;
    if (!ok) fprintf(stderr, "Aquarium reread verification failed\n");
    return ok;
}
bool aquarium_store_write(const char *directory, const uint32_t id[2], int slot, const void *data, size_t size) {
    char paths[2][192]; uint32_t sequence[2] = {0}; int status[2];
    if (size > UINT32_MAX || !copies(directory, id, slot, size, paths, sequence, status)) return false;
    /* Never erase the newest verified copy. An interrupted alternate write is recoverable. */
    int newest = status[1] == 1 && (status[0] != 1 || sequence[1] > sequence[0]) ? 1 : 0;
    uint32_t last = status[newest] == 1 ? sequence[newest] : 0;
    if (last == UINT32_MAX) { fprintf(stderr, "Aquarium generation limit reached\n"); return false; }
    int use = last ? 1 - newest : 0;
    header_t h = {STORE_MAGIC, 1, last + 1, (uint32_t)size, crc32(0, data, size), 0};
    h.header_crc = crc32(0, &h, offsetof(header_t, header_crc));
    FILE *file = fopen(paths[use], "wb");
    if (!file) { fprintf(stderr, "Aquarium write failed: %s\n", strerror(errno)); return false; }
    bool ok = fwrite(&h, 1, sizeof h, file) == sizeof h && fwrite(data, 1, size, file) == size;
    if (fflush(file) != 0 || sync_file(file) != 0) ok = false;
    if (fclose(file) != 0) ok = false;
    uint32_t verified = 0;
    ok = ok && inspect(paths[use], size, &verified) == 1 && verified == h.sequence;
    if (!ok) fprintf(stderr, "Aquarium write/readback failed; previous snapshot retained\n");
    return ok;
}
int aquarium_store_present(const char *directory, const uint32_t id[2], int slot) {
    int found = 0;
    for (int copy = 0; copy < 2; copy++) {
        char path[192]; struct stat info;
        if (!name(path, directory, id, slot, copy)) return -1;
        if (stat(path, &info) == 0) found = 1;
        else if (errno != ENOENT) {
            fprintf(stderr, "Aquarium stat failed: %s (%s)\n", path, strerror(errno));
            return -1;
        }
    }
    return found;
}
bool aquarium_store_archive(const char *directory, const uint32_t id[2], int slot, const void *data, size_t size) {
    if (!aquarium_store_write(directory, id, slot, data, size)) return false;
    char paths[2][192]; uint32_t sequence[2] = {0}; int status[2];
    if (!copies(directory, id, slot, size, paths, sequence, status)) return false;
    int newest = status[1] == 1 && (status[0] != 1 || sequence[1] > sequence[0]) ? 1 : 0;
    if (status[newest] != 1) return false;
    if (remove(paths[1 - newest]) != 0 && errno != ENOENT) {
        fprintf(stderr, "Aquarium archive cleanup failed: %s\n", strerror(errno));
        return false;
    }
    return true;
}
