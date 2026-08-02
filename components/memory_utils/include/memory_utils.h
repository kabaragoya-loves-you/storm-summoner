#ifndef MEMORY_UTILS_H
#define MEMORY_UTILS_H

#include <stdlib.h>
#include <stdbool.h>
#include "esp_heap_caps.h"
#include "esp_memory_utils.h"

/**
 * @brief Allocate memory preferring PSRAM with fallback to internal heap
 * 
 * Attempts to allocate from PSRAM first. If PSRAM allocation fails,
 * falls back to standard malloc (which uses internal heap).
 * 
 * @param size Number of bytes to allocate
 * @return Pointer to allocated memory, or NULL if allocation failed
 */
static inline void *malloc_prefer_psram(size_t size) {
  void *ptr = heap_caps_malloc(size, MALLOC_CAP_SPIRAM);
  if (!ptr) ptr = malloc(size);
  return ptr;
}

/**
 * @brief Allocate zeroed memory preferring PSRAM with fallback to internal heap
 * 
 * Attempts to allocate from PSRAM first. If PSRAM allocation fails,
 * falls back to standard calloc (which uses internal heap).
 * 
 * @param count Number of elements to allocate
 * @param size Size of each element in bytes
 * @return Pointer to allocated zeroed memory, or NULL if allocation failed
 */
static inline void *calloc_prefer_psram(size_t count, size_t size) {
  void *ptr = heap_caps_calloc(count, size, MALLOC_CAP_SPIRAM);
  if (!ptr) ptr = calloc(count, size);
  return ptr;
}

/**
 * @brief Reallocate memory preferring PSRAM with fallback to internal heap
 * 
 * If the original pointer was allocated from PSRAM, attempts to reallocate
 * from PSRAM. Falls back to standard realloc if PSRAM allocation fails.
 * 
 * @param ptr Pointer to previously allocated memory (or NULL)
 * @param size New size in bytes
 * @return Pointer to reallocated memory, or NULL if allocation failed
 */
static inline void *realloc_prefer_psram(void *ptr, size_t size) {
  void *new_ptr = heap_caps_realloc(ptr, size, MALLOC_CAP_SPIRAM);
  if (!new_ptr) new_ptr = realloc(ptr, size);
  return new_ptr;
}

/**
 * @brief Free a pointer that must have been allocated with MALLOC_CAP_SPIRAM
 *
 * Always nulls *pp. If *pp is non-NULL but not in external RAM, the free is
 * skipped (avoids heap_caps_free panic on corrupted static pointer slots) and
 * the function returns true so the caller can log / fail hard.
 *
 * @param pp Address of the pointer to clear
 * @return true if *pp was non-NULL but not SPIRAM (corrupt); false otherwise
 */
static inline bool clear_spiram_ptr(void **pp) {
  if (!pp || !*pp) return false;
  void *p = *pp;
  *pp = NULL;
  if (esp_ptr_external_ram(p)) {
    heap_caps_free(p);
    return false;
  }
  return true;
}

#endif // MEMORY_UTILS_H
