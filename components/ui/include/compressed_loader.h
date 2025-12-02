#ifndef COMPRESSED_LOADER_H
#define COMPRESSED_LOADER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/**
 * Load a compressed .bin.z file from LittleFS and decompress to PSRAM
 * 
 * File format:
 *   uint32_t original_size (little-endian)
 *   uint8_t[] zlib_compressed_data
 *
 * @param path Path to .bin.z file (e.g., "/assets/images/data.bin.z")
 * @param out_size Pointer to receive decompressed size
 * @return Pointer to decompressed data in PSRAM (caller must free), or NULL on error
 */
void *compressed_load(const char *path, size_t *out_size);

/**
 * Free data loaded by compressed_load()
 */
void compressed_free(void *data);

#endif // COMPRESSED_LOADER_H

