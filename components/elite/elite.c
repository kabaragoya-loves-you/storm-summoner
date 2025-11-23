/*
 * Code based on https://github.com/OkuboHeavyIndustries/SSD1306_3D_ELITE_SHIP_VIEWER/
 */

#include "lvgl.h"
#include "elite.h"
#include "ships.h"
#include "esp_random.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_heap_caps.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <math.h>
#include "flyer_venice_14.h"

#define TAG "ELITE"

static float rotx, roty, rotz, rotxx, rotyy, rotzz, rotxxx, rotyyy, rotzzz;
static int wireframe[43][3];
static int scale = 1;

static int ship_vertices_cnt, ship_faces_cnt;
static int ship_vertices[43][3];
static int ship_faces[23][9];

static const int originx = ELITE_DISPLAY_WIDTH / 2;
static const int originy = ELITE_DISPLAY_HEIGHT / 2;

static float vector;
static float scalefactor = 0;

static char ShipName[32] = "";

static lv_obj_t *canvas;
static lv_obj_t *info_label;
static lv_timer_t *rotation_timer;
static lv_timer_t *g_ship_cycling_timer = NULL; // For cycling ships
static lv_style_t style_default;
static bool g_elite_style_initialized = false;

static void *canvas_buf = NULL;
static lv_obj_t *g_elite_screen = NULL;
static lv_obj_t *g_previous_screen = NULL;
static volatile bool g_elite_stopping = false;

static void next_random_ship(lv_timer_t *timer);
static void draw_wireframe_ship(void);
static void rotate_ship_cb(lv_timer_t *timer);
static void cleanup_ship_resources(void);
static void draw_line_on_canvas(int x0, int y0, int x1, int y1, lv_color_t color);

static void draw_wireframe_ship(void) {
  if (!canvas_buf || !canvas || g_elite_stopping) return;
  
  // Verify canvas has a valid buffer before proceeding
  if (!lv_canvas_get_buf(canvas)) {
    ESP_LOGW(TAG, "Canvas buffer not set yet, skipping draw");
    return;
  }
  
  // Check if canvas is on the active screen
  lv_obj_t *canvas_screen = lv_obj_get_screen(canvas);
  if (canvas_screen != lv_screen_active()) {
    ESP_LOGW(TAG, "Canvas is not on active screen, skipping draw");
    return;
  }
  
  if (canvas_buf) {
    // Use memset for safety and performance
    size_t buf_size = ELITE_DISPLAY_WIDTH * ELITE_DISPLAY_HEIGHT * lv_color_format_get_size(LV_COLOR_FORMAT_NATIVE);
    memset(canvas_buf, 0, buf_size);
  }
  
  // Direct wireframe drawing without layers
  int face, f_line, wf_f_1, wf_f_2;

  for (face = 0; face < ship_faces_cnt; face++) {
    vector = 0;

    // Calculate face normal for back-face culling
    for (f_line = 1; f_line < ship_faces[face][0]; f_line++) {
      wf_f_1 = ship_faces[face][f_line];
      wf_f_2 = ship_faces[face][f_line + 1];
      vector += wireframe[wf_f_1][0] * wireframe[wf_f_2][1] - wireframe[wf_f_1][1] * wireframe[wf_f_2][0];
    }

    wf_f_1 = ship_faces[face][f_line];
    wf_f_2 = ship_faces[face][1];
    vector += wireframe[wf_f_1][0] * wireframe[wf_f_2][1] - wireframe[wf_f_1][1] * wireframe[wf_f_2][0];

    // Only draw visible faces (back-face culling)
    if (vector >= 0) {
      // Draw each edge of the face
      for (f_line = 1; f_line < ship_faces[face][0]; f_line++) {
        wf_f_1 = ship_faces[face][f_line];
        wf_f_2 = ship_faces[face][f_line + 1];
        
        int x1 = wireframe[wf_f_1][0];
        int y1 = wireframe[wf_f_1][1];
        int x2 = wireframe[wf_f_2][0];
        int y2 = wireframe[wf_f_2][1];
        
        // Draw the line using our direct drawing function
        draw_line_on_canvas(x1, y1, x2, y2, lv_color_white());
      }
      
      // Draw the closing edge
      wf_f_1 = ship_faces[face][f_line];
      wf_f_2 = ship_faces[face][1];
      
      int x1 = wireframe[wf_f_1][0];
      int y1 = wireframe[wf_f_1][1];
      int x2 = wireframe[wf_f_2][0];
      int y2 = wireframe[wf_f_2][1];
      
      draw_line_on_canvas(x1, y1, x2, y2, lv_color_white());
    }
  }
  
  // Invalidate the canvas to trigger a redraw (unless we're stopping)
  if (!g_elite_stopping && canvas && lv_obj_is_valid(canvas)) lv_obj_invalidate(canvas);
}


// Simple Bresenham line drawing algorithm for canvas
static void draw_line_on_canvas(int x0, int y0, int x1, int y1, lv_color_t color) {
  if (!canvas || !canvas_buf || g_elite_stopping) return;
  
  int dx = abs(x1 - x0);
  int dy = abs(y1 - y0);
  int sx = x0 < x1 ? 1 : -1;
  int sy = y0 < y1 ? 1 : -1;
  int err = dx - dy;
  
  while (1) {
    // Extra safety check in case canvas is cleared while drawing
    if (!canvas || !canvas_buf || g_elite_stopping) return;
    
    // Draw pixel if within bounds
    if (x0 >= 0 && x0 < ELITE_DISPLAY_WIDTH && y0 >= 0 && y0 < ELITE_DISPLAY_HEIGHT) {
      // Extra check: ensure canvas is valid and not being rendered
      if (canvas && lv_obj_is_valid(canvas) && !g_elite_stopping) {
        lv_canvas_set_px(canvas, x0, y0, color, LV_OPA_COVER);
      }
    }
    
    if (x0 == x1 && y0 == y1) break;
    
    int e2 = 2 * err;
    if (e2 > -dy) {
      err -= dy;
      x0 += sx;
    }
    if (e2 < dx) {
      err += dx;
      y0 += sy;
    }
  }
}

static void rotate_ship_cb(lv_timer_t *timer) {
  if (!canvas || !canvas_buf || g_elite_stopping) return;
  
  static int angle = 0;
  static int rotation_pattern = -1;  // -1 means we need to pick a new pattern
  
  if (rotation_pattern == -1) rotation_pattern = (esp_random() % 2);
  
  if (canvas_buf) {
    // Use memset for safety and performance
    size_t buf_size = ELITE_DISPLAY_WIDTH * ELITE_DISPLAY_HEIGHT * lv_color_format_get_size(LV_COLOR_FORMAT_NATIVE);
    memset(canvas_buf, 0, buf_size);
  }

  for (int i = 0; i < ship_vertices_cnt; i++) {
    float rot = angle * 0.0174532; // 0.0174532 = one degree
    
    if (rotation_pattern == 0) {
      // Standard rotation
      rotz = ship_vertices[i][2] / scale * cos(rot) - ship_vertices[i][0] / scale * sin(rot);
      rotx = ship_vertices[i][2] / scale * sin(rot) + ship_vertices[i][0] / scale * cos(rot);
      roty = ship_vertices[i][1] / scale;
    } else {
      // Alternative rotation
      rotx = ship_vertices[i][2] / scale * cos(rot) + ship_vertices[i][0] / scale * sin(rot);
      rotz = ship_vertices[i][2] / scale * sin(rot) - ship_vertices[i][0] / scale * cos(rot);
      roty = ship_vertices[i][1] / scale;
    }
    
    // Common rotation steps for both patterns
    rotyy = roty * cos(rot) - rotz * sin(rot);
    rotzz = roty * sin(rot) + rotz * cos(rot);
    rotxx = rotx;
    
    rotxxx = rotxx * cos(rot) - rotyy * sin(rot);
    rotyyy = rotxx * sin(rot) + rotyy * cos(rot);
    rotzzz = rotzz;

    // orthographic projection
    rotxxx = rotxxx * scalefactor + originx;
    rotyyy = rotyyy * scalefactor + originy;

    // store new vertices values for wireframe drawing
    wireframe[i][0] = rotxxx;
    wireframe[i][1] = rotyyy;
    wireframe[i][2] = rotzzz;
  }
  
  draw_wireframe_ship();
  
  if (scalefactor < 1) scalefactor += 0.01;
  
  angle += 3;
  if (angle >= 360) angle = 0;

  lv_label_set_text(info_label, ShipName);
}

static void cleanup_ship_resources(void) {
  // This is only called on error during elite_start
  // Stop any active timers
  if (rotation_timer) {
    lv_timer_delete(rotation_timer);
    rotation_timer = NULL;
  }
  if (g_ship_cycling_timer) {
    lv_timer_delete(g_ship_cycling_timer);
    g_ship_cycling_timer = NULL;
  }
  
  // Note: canvas and info_label are deleted with the screen
  canvas = NULL;
  info_label = NULL;
}

void display_ship(const char* name, int* vertices, int vert_cnt, int vert_scale, int* faces, int face_cnt) {
  // Stop rotation timer for previous ship
  if (rotation_timer) {
    lv_timer_delete(rotation_timer);
    rotation_timer = NULL;
  }

  // Copy ship data
  strcpy(ShipName, name);
  memcpy(ship_vertices, vertices, sizeof(int) * vert_cnt * 3);
  ship_vertices_cnt = vert_cnt;
  scale = vert_scale;
  memcpy(ship_faces, faces, sizeof(int) * face_cnt * 9);
  ship_faces_cnt = face_cnt;
  
  scalefactor = 0;
  
  // Update label with new ship name
  if (info_label) lv_label_set_text(info_label, ShipName);
  
  // Clear canvas
  if (canvas_buf) {
    // Use memset for safety and performance
    size_t buf_size = ELITE_DISPLAY_WIDTH * ELITE_DISPLAY_HEIGHT * lv_color_format_get_size(LV_COLOR_FORMAT_NATIVE);
    memset(canvas_buf, 0, buf_size);
  }
  
  // Validate canvas is ready
  if (!canvas || !canvas_buf) {
    ESP_LOGE(TAG, "Canvas or buffer not ready in display_ship!");
    return;
  }
  
  // Create timer and it will start automatically
  rotation_timer = lv_timer_create(rotate_ship_cb, ELITE_ROTATION_INTERVAL_MS, NULL);
}

void elite_start(void) {
  ESP_LOGI(TAG, "Starting Elite screensaver...");
  
  // Ensure stopping flag is cleared
  g_elite_stopping = false;
  
  // Save current screen
  g_previous_screen = lv_screen_active();
  
  // Create screen if needed
  if (!g_elite_screen) {
    g_elite_screen = lv_obj_create(NULL);
    lv_obj_set_size(g_elite_screen, ELITE_DISPLAY_WIDTH, ELITE_DISPLAY_HEIGHT);
    lv_obj_set_style_bg_color(g_elite_screen, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(g_elite_screen, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(g_elite_screen, 0, 0);
  }
  
  // Check available memory before allocating canvas buffer
  if (!canvas_buf) {
    size_t free_heap = esp_get_free_heap_size();
    // Calculate size based on the actual color format
    size_t bytes_per_pixel = lv_color_format_get_size(LV_COLOR_FORMAT_NATIVE);
    size_t required_size = ELITE_DISPLAY_WIDTH * ELITE_DISPLAY_HEIGHT * bytes_per_pixel;
    
    ESP_LOGI(TAG, "Canvas dimensions: %dx%d, color format: %d, bytes per pixel: %d", 
      ELITE_DISPLAY_WIDTH, ELITE_DISPLAY_HEIGHT, LV_COLOR_FORMAT_NATIVE, bytes_per_pixel);
    ESP_LOGI(TAG, "Free heap: %d bytes, need %d bytes for canvas", free_heap, required_size);
    
    // Check LVGL memory status
    lv_mem_monitor_t mon;
    lv_mem_monitor(&mon);
    ESP_LOGI(TAG, "LVGL memory - total: %d, used: %d, free: %d, frag: %d%%", 
      mon.total_size, mon.total_size - mon.free_size, mon.free_size, mon.frag_pct);
    
    if (free_heap < required_size + 16384) { // Keep 16KB safety margin
      ESP_LOGE(TAG, "Not enough memory for Elite canvas");
      return;
    }
    
    canvas_buf = heap_caps_aligned_alloc(64, required_size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!canvas_buf) {
      ESP_LOGE(TAG, "Failed to allocate aligned canvas buffer from internal RAM");
      return;
    }
    ESP_LOGI(TAG, "Allocated canvas buffer (32KB) with 64-byte alignment");
  }
  
  // Initialize style if needed
  if (!g_elite_style_initialized) {
    lv_style_init(&style_default);
    lv_style_set_text_font(&style_default, &flyer_venice_14);
    lv_style_set_text_color(&style_default, lv_color_white());
    g_elite_style_initialized = true;
  }
  
  // Create canvas on the screen if not already created
  if (!canvas) {
    canvas = lv_canvas_create(g_elite_screen);
    lv_obj_set_size(canvas, ELITE_DISPLAY_WIDTH, ELITE_DISPLAY_HEIGHT);
    lv_obj_align(canvas, LV_ALIGN_CENTER, 0, 0);
    lv_obj_remove_style_all(canvas);
    lv_obj_set_style_pad_all(canvas, 0, 0);
    
  }
  
  // Create info label if not already created
  if (!info_label) {
    info_label = lv_label_create(g_elite_screen);
    lv_obj_align(info_label, LV_ALIGN_TOP_MID, 0, 20);
    lv_obj_add_style(info_label, &style_default, 0);
  }
  
  // Always set buffer (needed on subsequent runs after buffer was freed)
  if (canvas && canvas_buf) {
    ESP_LOGI(TAG, "Setting canvas buffer");
    lv_canvas_set_buffer(canvas, canvas_buf, ELITE_DISPLAY_WIDTH, ELITE_DISPLAY_HEIGHT, LV_COLOR_FORMAT_NATIVE);
  }
  
  // Clear canvas
  if (canvas_buf) {
    // Use memset for safety and performance
    size_t buf_size = ELITE_DISPLAY_WIDTH * ELITE_DISPLAY_HEIGHT * lv_color_format_get_size(LV_COLOR_FORMAT_NATIVE);
    memset(canvas_buf, 0, buf_size);
  }
  
  // Load the screen
  lv_screen_load(g_elite_screen);

  // Display the first ship immediately
  next_random_ship(NULL); 
  
  if (g_ship_cycling_timer == NULL) {
    g_ship_cycling_timer = lv_timer_create(next_random_ship, ELITE_SHIP_CHANGE_INTERVAL_MS, NULL);
    if (g_ship_cycling_timer == NULL) {
      cleanup_ship_resources();
      return;
    }
  } else {
    lv_timer_resume(g_ship_cycling_timer);
  }

  ESP_LOGI(TAG, "Elite screensaver started");
}

void elite_stop(void) {
  ESP_LOGI(TAG, "Stopping Elite screensaver...");
  
  // Set flag to prevent any drawing
  g_elite_stopping = true;
  
  // Pause timers IMMEDIATELY to prevent any callbacks from firing
  if (rotation_timer) lv_timer_pause(rotation_timer); // Note: We'll delete this in cleanup or when displaying a new ship

  if (g_ship_cycling_timer) lv_timer_pause(g_ship_cycling_timer);  // Just pause, don't delete - we might restart
  
  // Give a moment for any in-flight timer callbacks to complete
  vTaskDelay(pdMS_TO_TICKS(10));
  
  // Clear the canvas pointer first to ensure draw functions bail out early
  lv_obj_t *old_canvas = canvas;
  canvas = NULL;
  info_label = NULL;
  
  // Hide the canvas to prevent any rendering
  if (old_canvas && lv_obj_is_valid(old_canvas)) lv_obj_add_flag(old_canvas, LV_OBJ_FLAG_HIDDEN);
  
  // Restore previous screen BEFORE freeing buffer
  if (g_previous_screen && lv_obj_is_valid(g_previous_screen)) {
    lv_screen_load(g_previous_screen);
    g_previous_screen = NULL;
  } else {
    ESP_LOGW(TAG, "Previous screen invalid or null, cannot restore");
  }
  
  // Now free canvas buffer after screen switch is complete
  if (canvas_buf) {
    void *tmp = canvas_buf;
    canvas_buf = NULL;  // Clear pointer first to prevent race conditions
    lv_free(tmp);
    ESP_LOGI(TAG, "Freed canvas buffer (32KB)");
  }
  
  // DO NOT reset the stopping flag here - it will be reset in elite_start()
  // This prevents any lingering timer callbacks from trying to draw
  
  ESP_LOGI(TAG, "Elite screensaver stopped");
}

void elite_cleanup(void) {
  ESP_LOGI(TAG, "Cleaning up Elite resources...");
  
  // Stop any active timers
  if (rotation_timer) {
    lv_timer_delete(rotation_timer);
    rotation_timer = NULL;
  }
  if (g_ship_cycling_timer) {
    lv_timer_delete(g_ship_cycling_timer);
    g_ship_cycling_timer = NULL;
  }
  
  // Delete screen and all children
  if (g_elite_screen) {
    lv_obj_delete(g_elite_screen);
    g_elite_screen = NULL;
    canvas = NULL;
    info_label = NULL;
  }
  
  // Free canvas buffer
  if (canvas_buf) {
    heap_caps_free(canvas_buf);
    canvas_buf = NULL;
  }
  
  g_previous_screen = NULL;
  
  ESP_LOGI(TAG, "Elite cleanup complete");
}

static void next_random_ship(lv_timer_t *timer) {
  LV_UNUSED(timer);
  
  int randompick = (esp_random() % 28) + 1;
  
  switch(randompick) {
  case 1:
    display_ship("ADDER", 
      (int*)adder_vertices, adder_vertices_cnt, adder_scale,
      (int*)adder_faces, adder_faces_cnt);
    break;

  case 2:
    display_ship("ANACONDA", 
      (int*)anaconda_vertices, anaconda_vertices_cnt, anaconda_scale,
      (int*)anaconda_faces, anaconda_faces_cnt);
    break;

  case 3:
    display_ship("ASP MK II", 
      (int*)asp_vertices, asp_vertices_cnt, asp_scale,
      (int*)asp_faces, asp_faces_cnt);
    break;

  case 4:
    display_ship("ASTEROID", 
      (int*)asteroid_vertices, asteroid_vertices_cnt, asteroid_scale,
      (int*)asteroid_faces, asteroid_faces_cnt);
    break;

  case 5:
    display_ship("BOA", 
      (int*)boa_vertices, boa_vertices_cnt, boa_scale,
      (int*)boa_faces, boa_faces_cnt);
    break;

  case 6:
    display_ship("BOULDER", 
      (int*)boulder_vertices, boulder_vertices_cnt, boulder_scale,
      (int*)boulder_faces, boulder_faces_cnt);
    break;

  case 7:
    display_ship("ESCAPE CAPSULE", 
      (int*)escape_vertices, escape_vertices_cnt, escape_scale,
      (int*)escape_faces, escape_faces_cnt);
    break;

  case 8:
    display_ship("COBRA MK III", 
      (int*)cobraIII_vertices, cobraIII_vertices_cnt, cobraIII_scale,
      (int*)cobraIII_faces, cobraIII_faces_cnt);
    break;

  case 9:
    display_ship("COBRA MK I", 
      (int*)cobraI_vertices, cobraI_vertices_cnt, cobraI_scale,
      (int*)cobraI_faces, cobraI_faces_cnt);
    break;

  case 10:
    display_ship("CONSTRICTOR", 
      (int*)constrictor_vertices, constrictor_vertices_cnt, constrictor_scale,
      (int*)constrictor_faces, constrictor_faces_cnt);
    break;

  case 11:
    display_ship("CORIOLIS STATION", 
      (int*)coriolis_vertices, coriolis_vertices_cnt, coriolis_scale,
      (int*)coriolis_faces, coriolis_faces_cnt);
    break;

  case 12:
    display_ship("COUGAR", 
      (int*)cougar_vertices, cougar_vertices_cnt, cougar_scale,
      (int*)cougar_faces, cougar_faces_cnt);
    break;

  case 13:
    display_ship("DODO STATION", 
      (int*)dodo_vertices, dodo_vertices_cnt, dodo_scale,
      (int*)dodo_faces, dodo_faces_cnt);
    break;

  case 14:
    display_ship("FER DE LANCE", 
      (int*)ferdelance_vertices, ferdelance_vertices_cnt, ferdelance_scale,
      (int*)ferdelance_faces, ferdelance_faces_cnt);
    break;

  case 15:
    display_ship("GECKO", 
      (int*)gecko_vertices, gecko_vertices_cnt, gecko_scale,
      (int*)gecko_faces, gecko_faces_cnt);
    break;

  case 16:
    display_ship("KRAIT", 
      (int*)krait_vertices, krait_vertices_cnt, krait_scale,
      (int*)krait_faces, krait_faces_cnt);
    break;

  case 17:
    display_ship("MAMBA", 
      (int*)mamba_vertices, mamba_vertices_cnt, mamba_scale,
      (int*)mamba_faces, mamba_faces_cnt);
    break;

  case 18:
    display_ship("MISSILE", 
      (int*)missile_vertices, missile_vertices_cnt, missile_scale,
      (int*)missile_faces, missile_faces_cnt);
    break;

  case 19:
    display_ship("MORAY", 
      (int*)moray_vertices, moray_vertices_cnt, moray_scale,
      (int*)moray_faces, moray_faces_cnt);
    break;

  case 20:
    display_ship("HULL PLATELET", 
      (int*)platelet_vertices, platelet_vertices_cnt, platelet_scale,
      (int*)platelet_faces, platelet_faces_cnt);
    break;

  case 21:
    display_ship("PYTHON", 
      (int*)python_vertices, python_vertices_cnt, python_scale,
      (int*)python_faces, python_faces_cnt);
    break;

  case 22:
    display_ship("SIDEWINDER", 
      (int*)sidewinder_vertices, sidewinder_vertices_cnt, sidewinder_scale,
      (int*)sidewinder_faces, sidewinder_faces_cnt);
    break;

  case 23:
    display_ship("SHUTTLE", 
      (int*)shuttle_vertices, shuttle_vertices_cnt, shuttle_scale,
      (int*)shuttle_faces, shuttle_faces_cnt);
    break;

  case 24:
    display_ship("THARGON", 
      (int*)thargon_vertices, thargon_vertices_cnt, thargon_scale,
      (int*)thargon_faces, thargon_faces_cnt);
    break;

  case 25:
    display_ship("THARGOID", 
      (int*)thargoid_vertices, thargoid_vertices_cnt, thargoid_scale,
      (int*)thargoid_faces, thargoid_faces_cnt);
    break;

  case 26:
    display_ship("TRANSPORTER", 
      (int*)transport_vertices, transport_vertices_cnt, transport_scale,
      (int*)transport_faces, transport_faces_cnt);
    break;

  case 27:
    display_ship("VIPER", 
      (int*)viper_vertices, viper_vertices_cnt, viper_scale,
      (int*)viper_faces, viper_faces_cnt);
    break;

  case 28:
    display_ship("WORM", 
      (int*)worm_vertices, worm_vertices_cnt, worm_scale,
      (int*)worm_faces, worm_faces_cnt);
    break;
  }
}
