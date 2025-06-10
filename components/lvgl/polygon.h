#ifndef POLYGON_H
#define POLYGON_H

#include "lvgl.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define POLYGON_MAX_INTERSECTIONS 32
#define POLYGON_MAX_VERTICES 64

typedef struct {
  float x, y;
} polygon_point_t;

/**
 * Fill a polygon using scanline rasterization
 * @param canvas Canvas object to draw on
 * @param vertices Array of polygon vertices
 * @param vertex_count Number of vertices
 * @param color Fill color
 */
void polygon_fill(lv_obj_t *canvas, polygon_point_t *vertices, int vertex_count, lv_color_t color);

/**
 * Create arc vertices for use in polygons
 * @param vertices Output vertex array
 * @param center_x Arc center X coordinate
 * @param center_y Arc center Y coordinate  
 * @param radius Arc radius
 * @param start_angle_deg Start angle in degrees
 * @param end_angle_deg End angle in degrees
 * @param steps Number of arc segments
 * @param reverse Whether to create vertices in reverse order
 * @return Number of vertices created
 */
int polygon_create_arc(polygon_point_t *vertices, float center_x, float center_y, 
                       float radius, float start_angle_deg, float end_angle_deg, 
                       int steps, bool reverse);

#ifdef __cplusplus
}
#endif

#endif // POLYGON_H 