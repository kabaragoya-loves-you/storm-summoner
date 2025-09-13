#include "starfield.h"
#include <stdlib.h>
#include <string.h>
#include "esp_log.h"

#define TAG "STARFIELD"

// Module state
static star_t* g_stars = NULL;
static starfield_config_t g_config = {0};
static bool g_initialized = false;

// Initialize a single star with random position and brightness
static void init_star(star_t* star) {
  star->x = rand() % g_config.field_size;
  star->y = rand() % g_config.field_size;
  star->base_brightness = (rand() % 12) + 1;
  // 25% chance for extra bright star
  if (rand() % 4 == 0) {
    star->base_brightness = (rand() % 15) + 1;
  }
  star->brightness = star->base_brightness;
  star->move_counter = rand() % g_config.move_counter_max;
}

void starfield_init(void) {
  starfield_config_t default_config = {
    .field_size = STARFIELD_DEFAULT_SIZE,
    .star_count = STARFIELD_DEFAULT_COUNT,
    .twinkle_variance = STARFIELD_DEFAULT_TWINKLE_VARIANCE,
    .move_chance = STARFIELD_DEFAULT_MOVE_CHANCE,
    .move_counter_max = STARFIELD_DEFAULT_MOVE_COUNTER_MAX
  };
  starfield_init_with_config(&default_config);
}

void starfield_init_with_config(const starfield_config_t* config) {
  if (!config) {
    ESP_LOGE(TAG, "Invalid configuration");
    return;
  }
  
  // Clean up any existing starfield
  starfield_deinit();
  
  // Copy configuration
  memcpy(&g_config, config, sizeof(starfield_config_t));
  
  // Allocate stars
  g_stars = malloc(g_config.star_count * sizeof(star_t));
  if (!g_stars) {
    ESP_LOGE(TAG, "Failed to allocate memory for stars");
    return;
  }
  
  // Initialize all stars
  for (uint16_t i = 0; i < g_config.star_count; i++) {
    init_star(&g_stars[i]);
  }
  
  g_initialized = true;
  ESP_LOGI(TAG, "Starfield initialized with %d stars in %dx%d field", 
           g_config.star_count, g_config.field_size, g_config.field_size);
}

void starfield_deinit(void) {
  if (g_stars) {
    free(g_stars);
    g_stars = NULL;
  }
  g_initialized = false;
  memset(&g_config, 0, sizeof(starfield_config_t));
}

void starfield_update(void) {
  if (!g_initialized || !g_stars) return;
  
  for (uint16_t i = 0; i < g_config.star_count; i++) {
    star_t* star = &g_stars[i];
    
    // Update twinkling
    int brightness_variance = (rand() % (g_config.twinkle_variance * 2 + 1)) - g_config.twinkle_variance;
    int new_brightness = star->base_brightness + brightness_variance;
    star->brightness = LV_CLAMP(1, new_brightness, 15);
    
    // Update movement counter
    star->move_counter++;
    if (star->move_counter >= g_config.move_counter_max) {
      star->move_counter = 0;
      
      // Check if star should move
      if ((rand() % 100) < g_config.move_chance) {
        star->x = rand() % g_config.field_size;
        star->y = rand() % g_config.field_size;
        
        // 33% chance to change brightness when moving
        if ((rand() % 3) == 0) {
          star->base_brightness = (rand() % 12) + 1;
          if (rand() % 4 == 0) {
            star->base_brightness = (rand() % 15) + 1;
          }
        }
      }
    }
  }
}

void starfield_draw(lv_obj_t* canvas, 
                   starfield_exclusion_check_fn* exclusion_checks,
                   size_t exclusion_count,
                   void* user_data) {
  if (!g_initialized || !g_stars || !canvas) return;
  
  for (uint16_t i = 0; i < g_config.star_count; i++) {
    star_t* star = &g_stars[i];
    float sx = star->x;
    float sy = star->y;
    
    // Check exclusion zones
    bool excluded = false;
    if (exclusion_checks && exclusion_count > 0) {
      for (size_t j = 0; j < exclusion_count; j++) {
        if (exclusion_checks[j] && exclusion_checks[j](sx, sy, user_data)) {
          excluded = true;
          break;
        }
      }
    }
    
    if (!excluded) {
      // Convert 0-15 brightness to 0-255 for LVGL
      uint8_t scaled_gray = star->brightness * 17;
      lv_canvas_set_px(canvas, (int)sx, (int)sy, 
                       lv_color_make(scaled_gray, scaled_gray, scaled_gray), 
                       LV_OPA_COVER);
    }
  }
}

const starfield_config_t* starfield_get_config(void) {
  return g_initialized ? &g_config : NULL;
}

void starfield_reset(void) {
  if (!g_initialized || !g_stars) return;
  
  for (uint16_t i = 0; i < g_config.star_count; i++) {
    init_star(&g_stars[i]);
  }
  
  ESP_LOGI(TAG, "Starfield reset");
}
