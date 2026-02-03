#ifndef SCENE_NAME_GEN_H
#define SCENE_NAME_GEN_H

#include "esp_err.h"
#include <stddef.h>
#include <stdbool.h>

// Maximum scene name length (enforced by text editor and generation)
#define SCENE_NAME_MAX_LEN 16

/**
 * Initialize the name generator by loading wordlist from LittleFS
 * Must be called before scene_name_generate()
 * @return ESP_OK on success
 */
esp_err_t scene_name_gen_init(void);

/**
 * Generate a random scene name from two words
 * Format: "WORD1 WORD2" (uppercase, space in middle)
 * @param out Output buffer
 * @param out_size Size of output buffer (should be >= SCENE_NAME_MAX_LEN + 1)
 */
void scene_name_generate(char* out, size_t out_size);

/**
 * Check if name generator is initialized and ready
 * @return true if wordlist is loaded
 */
bool scene_name_gen_ready(void);

/**
 * Get the number of words in the wordlist
 * @return Word count, or 0 if not initialized
 */
uint16_t scene_name_gen_word_count(void);

/**
 * Free resources used by name generator
 */
void scene_name_gen_deinit(void);

#endif // SCENE_NAME_GEN_H
