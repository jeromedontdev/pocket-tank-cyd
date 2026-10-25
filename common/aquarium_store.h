#ifndef AQUARIUM_STORE_H
#define AQUARIUM_STORE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
/* size is buffer capacity: checksum-verified older shorter saves zero-fill
 * their appended tails. The newest valid generation wins across migrations. */
bool aquarium_store_read(const char *directory, const uint32_t id[2], int slot, void *data, size_t size);
bool aquarium_store_write(const char *directory, const uint32_t id[2], int slot, const void *data, size_t size);
/* -1 I/O error, 0 absent, 1 present (including corrupt records). */
int aquarium_store_present(const char *directory, const uint32_t id[2], int slot);
/* Alternate-write/readback first, then retire the previous archive generation. */
bool aquarium_store_archive(const char *directory, const uint32_t id[2], int slot, const void *data, size_t size);
#endif
