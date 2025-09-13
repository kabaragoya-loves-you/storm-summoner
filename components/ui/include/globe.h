#ifndef GLOBE_H
#define GLOBE_H

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

void globe_init(void);
void globe_draw(lv_obj_t *canvas, int center_x, int center_y, float radius, float rotation_x, float rotation_y, float rotation_z, float scale);

#ifdef __cplusplus
}
#endif

#endif // GLOBE_H 