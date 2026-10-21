#ifndef AQUARIUM_STORE_H
#define AQUARIUM_STORE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
bool aquarium_store_read(const char *directory, const uint32_t id[2], int slot, void *data, size_t size);
bool aquarium_store_write(const char *directory, const uint32_t id[2], int slot, const void *data, size_t size);
#endif
