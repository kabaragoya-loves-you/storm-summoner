#include "polygon.h"
#include "lvgl.h"
#include <math.h>
#include <string.h>

// Define M_PI if not available
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// 🎯 REUSABLE POLYGON RASTERIZER - 1980s style pixel-perfect control!

// Scanline polygon fill algorithm
void polygon_fill(lv_obj_t *canvas, polygon_point_t *vertices, int vertex_count, lv_color_t color, lv_opa_t opa) {
  if (vertex_count < 3 || !canvas) return;
  
  // Get canvas dimensions
  int32_t canvas_w = lv_obj_get_width(canvas);
  int32_t canvas_h = lv_obj_get_height(canvas);
  
  // For transparent fills (holes), we need direct buffer access
  // because lv_canvas_set_px does alpha blending, not replacement
  uint8_t *buf = NULL;
  uint32_t stride = 0;
  bool is_hole = (opa == LV_OPA_TRANSP);
  
  if (is_hole) {
    lv_draw_buf_t *draw_buf = lv_canvas_get_draw_buf(canvas);
    if (draw_buf) {
      buf = draw_buf->data;
      stride = draw_buf->header.stride;
    }
  }
  
  // Find bounding box
  float min_y = vertices[0].y, max_y = vertices[0].y;
  for (int i = 1; i < vertex_count; i++) {
    if (vertices[i].y < min_y) min_y = vertices[i].y;
    if (vertices[i].y > max_y) max_y = vertices[i].y;
  }
  
  // Clamp to canvas bounds  
  int start_y = (int)fmaxf(0, floorf(min_y));
  int end_y = (int)fminf(canvas_h - 1, ceilf(max_y));
  
  // For each scanline
  for (int y = start_y; y <= end_y; y++) {
    float intersections[POLYGON_MAX_INTERSECTIONS];
    int intersection_count = 0;
    
    // Find intersections with polygon edges
    for (int i = 0; i < vertex_count; i++) {
      int j = (i + 1) % vertex_count;
      polygon_point_t p1 = vertices[i];
      polygon_point_t p2 = vertices[j];
      
      // Check if scanline intersects this edge (avoid double-counting vertices)
      if ((p1.y < y && p2.y >= y) || (p2.y < y && p1.y >= y)) {
        // Skip horizontal edges
        if (fabsf(p2.y - p1.y) > 0.001f) {
          // Calculate intersection x coordinate
          float x_intersect = p1.x + (y - p1.y) * (p2.x - p1.x) / (p2.y - p1.y);
          if (intersection_count < POLYGON_MAX_INTERSECTIONS) {
            intersections[intersection_count++] = x_intersect;
          }
        }
      }
    }
    
    // Sort intersections
    for (int i = 0; i < intersection_count - 1; i++) {
      for (int j = i + 1; j < intersection_count; j++) {
        if (intersections[i] > intersections[j]) {
          float temp = intersections[i];
          intersections[i] = intersections[j];
          intersections[j] = temp;
        }
      }
    }
    
    // Fill between pairs of intersections
    for (int i = 0; i < intersection_count; i += 2) {
      if (i + 1 < intersection_count) {
        int start_x = (int)fmaxf(0, floorf(intersections[i] + 0.5f));
        int end_x = (int)fminf(canvas_w - 1, floorf(intersections[i + 1] + 0.5f));
        
        if (is_hole && buf) {
          // For holes: directly write transparent pixels (ARGB8888: B,G,R,A)
          for (int x = start_x; x <= end_x; x++) {
            uint8_t *px = buf + y * stride + x * 4;
            px[0] = 0;  // B
            px[1] = 0;  // G
            px[2] = 0;  // R
            px[3] = 0;  // A = transparent
          }
        } else {
          // Normal fill
          for (int x = start_x; x <= end_x; x++) {
            lv_canvas_set_px(canvas, x, y, color, opa);
          }
        }
      }
    }
  }
}

// Helper: Create arc vertices  
int polygon_create_arc(polygon_point_t *vertices, float center_x, float center_y, 
                       float radius, float start_angle_deg, float end_angle_deg, 
                       int steps, bool reverse) {
  int count = 0;
  
  if (reverse) {
    for (int i = steps; i >= 0; i--) {
      float angle = start_angle_deg + (end_angle_deg - start_angle_deg) * i / steps;
      float rad = angle * (M_PI / 180.0f);
      vertices[count++] = (polygon_point_t){
        .x = center_x + radius * cosf(rad),
        .y = center_y + radius * sinf(rad)
      };
    }
  } else {
    for (int i = 0; i <= steps; i++) {
      float angle = start_angle_deg + (end_angle_deg - start_angle_deg) * i / steps;
      float rad = angle * (M_PI / 180.0f);
      vertices[count++] = (polygon_point_t){
        .x = center_x + radius * cosf(rad),
        .y = center_y + radius * sinf(rad)
      };
    }
  }
  
  return count;
} 