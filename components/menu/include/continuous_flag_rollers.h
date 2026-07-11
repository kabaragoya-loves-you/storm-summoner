#ifndef CONTINUOUS_FLAG_ROLLERS_H
#define CONTINUOUS_FLAG_ROLLERS_H

#include "menu.h"
#include "continuous_mapping.h"
#include "scene.h"

typedef struct {
  continuous_mapping_t* (*get_mapping)(scene_t* scene);
  const char* parent_title;
  lv_obj_t* (*parent_create)(void);
  void (*persist)(void);
} continuous_flag_ctx_t;

void continuous_flag_ctx_set(const continuous_flag_ctx_t* ctx);

int continuous_flag_append_cc_items(menu_item_t* items, char labels[4][48], int idx);

#endif // CONTINUOUS_FLAG_ROLLERS_H
