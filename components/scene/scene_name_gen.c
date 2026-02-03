#include "scene_name_gen.h"
#include "compressed_loader.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_heap_caps.h"
#include <string.h>
#include <ctype.h>

#define TAG "scene_name_gen"

// Wordlist path on LittleFS (stored alongside image assets)
#define WORDLIST_PATH "/assets/images/wordlist.txt.z"

// Loaded wordlist data (in PSRAM)
static char* s_word_data = NULL;      // Raw decompressed text
static char** s_words = NULL;         // Array of word pointers
static uint16_t s_word_count = 0;

esp_err_t scene_name_gen_init(void) {
  if (s_word_data != NULL) {
    ESP_LOGD(TAG, "Already initialized with %u words", (unsigned)s_word_count);
    return ESP_OK;
  }

  // Load and decompress wordlist from LittleFS
  size_t data_size;
  s_word_data = compressed_load(WORDLIST_PATH, &data_size);
  if (!s_word_data) {
    ESP_LOGE(TAG, "Failed to load wordlist from %s", WORDLIST_PATH);
    return ESP_FAIL;
  }

  // Count words (newlines + 1)
  s_word_count = 1;
  for (size_t i = 0; i < data_size; i++) {
    if (s_word_data[i] == '\n') s_word_count++;
  }

  // Allocate pointer array in PSRAM
  s_words = heap_caps_calloc(s_word_count, sizeof(char*), MALLOC_CAP_SPIRAM);
  if (!s_words) {
    ESP_LOGE(TAG, "Failed to allocate word pointer array");
    compressed_free(s_word_data);
    s_word_data = NULL;
    s_word_count = 0;
    return ESP_ERR_NO_MEM;
  }

  // Parse words: split on newlines, null-terminate each word
  uint16_t word_idx = 0;
  char* word_start = s_word_data;

  for (size_t i = 0; i < data_size && word_idx < s_word_count; i++) {
    if (s_word_data[i] == '\n' || s_word_data[i] == '\r') {
      s_word_data[i] = '\0';  // Null-terminate the word

      // Only add non-empty words
      if (word_start[0] != '\0') {
        // Trim trailing CR if present (Windows line endings)
        size_t len = strlen(word_start);
        while (len > 0 && (word_start[len - 1] == '\r' || word_start[len - 1] == '\n')) {
          word_start[--len] = '\0';
        }
        if (len > 0) {
          s_words[word_idx++] = word_start;
        }
      }
      word_start = &s_word_data[i + 1];
    }
  }

  // Handle last word (no trailing newline)
  if (word_start[0] != '\0' && word_idx < s_word_count) {
    s_words[word_idx++] = word_start;
  }

  s_word_count = word_idx;  // Actual count of valid words

  ESP_LOGI(TAG, "Loaded %u words from wordlist", (unsigned)s_word_count);
  return ESP_OK;
}

void scene_name_generate(char* out, size_t out_size) {
  if (!out || out_size == 0) return;
  out[0] = '\0';

  if (!s_words || s_word_count < 2) {
    ESP_LOGW(TAG, "Wordlist not loaded or too few words");
    strncpy(out, "New Scene", out_size - 1);
    out[out_size - 1] = '\0';
    return;
  }

  // Pick two different random words
  uint16_t idx1 = esp_random() % s_word_count;
  uint16_t idx2;
  do {
    idx2 = esp_random() % s_word_count;
  } while (idx2 == idx1);

  const char* word1 = s_words[idx1];
  const char* word2 = s_words[idx2];

  // Format as "WORD1 WORD2" (uppercase)
  size_t pos = 0;
  size_t max_len = (out_size - 1 < SCENE_NAME_MAX_LEN) ? out_size - 1 : SCENE_NAME_MAX_LEN;

  // Copy first word (uppercase)
  for (size_t i = 0; word1[i] && pos < max_len; i++) {
    out[pos++] = (char)toupper((unsigned char)word1[i]);
  }

  // Add space if room
  if (pos < max_len) {
    out[pos++] = ' ';
  }

  // Copy second word (uppercase)
  for (size_t i = 0; word2[i] && pos < max_len; i++) {
    out[pos++] = (char)toupper((unsigned char)word2[i]);
  }

  out[pos] = '\0';
}

bool scene_name_gen_ready(void) {
  return s_words != NULL && s_word_count >= 2;
}

uint16_t scene_name_gen_word_count(void) {
  return s_word_count;
}

void scene_name_gen_deinit(void) {
  if (s_words) {
    heap_caps_free(s_words);
    s_words = NULL;
  }
  if (s_word_data) {
    compressed_free(s_word_data);
    s_word_data = NULL;
  }
  s_word_count = 0;
  ESP_LOGI(TAG, "Name generator deinitialized");
}
