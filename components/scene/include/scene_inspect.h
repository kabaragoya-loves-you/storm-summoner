#ifndef SCENE_INSPECT_H
#define SCENE_INSPECT_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "scene.h"

typedef struct {
  char *buf;
  size_t cap;
  size_t len;
  bool truncated;
} scene_inspect_buf_t;

void scene_inspect_buf_init(scene_inspect_buf_t *b, char *buf, size_t cap);
bool scene_inspect_buf_append(scene_inspect_buf_t *b, const char *fmt, ...);

// Build full inspect text for scene_index (uses scene_get_current if scene is NULL).
bool scene_inspect_build(const scene_t *scene, uint8_t scene_index, char *buf, size_t cap);

// Build inspect text for any manifest scene index (loads from cache or flash).
esp_err_t scene_inspect_at_index(uint8_t scene_index, char *buf, size_t cap,
  bool *truncated_out);

#endif // SCENE_INSPECT_H
