#ifndef STARS_H
#define STARS_H

#include "lvgl.h"

#define DISP_HOR_RES 128
#define DISP_VER_RES 128
#define MAX_STARS 200

typedef struct {
  int x, y;
  int z;
  uint8_t brightness; // 0..255
} Star;

void create_starfield(void);  // Deprecated
void starfield_start(void);
void starfield_stop(void);
void starfield_cleanup(void);  // Full cleanup of resources

#endif // STARS_H
