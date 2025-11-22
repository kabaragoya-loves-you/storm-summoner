#include "lv_starfield.h"
#include <stdlib.h>
#include <string.h>
#include "esp_log.h"

#define TAG "LV_STARFIELD"

/**********************
 *  STATIC PROTOTYPES
 **********************/
static void lv_starfield_draw_event_cb(lv_event_t * e);
static void lv_starfield_destructor_event_cb(lv_event_t * e);
static void animation_timer_cb(lv_timer_t * timer);
static void init_star(lv_star_t * star, lv_area_t * area);
static bool check_sibling_exclusion(lv_obj_t * starfield, int32_t x, int32_t y);

/**********************
 *   FUNCTIONS
 **********************/

lv_obj_t * lv_starfield_create(lv_obj_t * parent) {
    // Create a base object
    lv_obj_t * obj = lv_obj_create(parent);
    if (obj == NULL) return NULL;
    
    // Allocate and initialize starfield data
    lv_starfield_data_t * starfield_data = malloc(sizeof(lv_starfield_data_t));
    if (starfield_data == NULL) {
        lv_obj_delete(obj);
        return NULL;
    }
    
    // Initialize with defaults
    starfield_data->star_count = LV_STARFIELD_DEFAULT_COUNT;
    starfield_data->twinkle_variance = LV_STARFIELD_DEFAULT_TWINKLE_VARIANCE;
    starfield_data->move_chance = LV_STARFIELD_DEFAULT_MOVE_CHANCE;
    starfield_data->move_counter_max = LV_STARFIELD_DEFAULT_MOVE_COUNTER_MAX;
    starfield_data->animation_timer = NULL;
    starfield_data->exclusion_checks = NULL;
    starfield_data->exclusion_count = 0;
    starfield_data->exclusion_user_data = NULL;
    starfield_data->exclude_siblings = true;  // Default to excluding siblings
    
    // Allocate stars
    starfield_data->stars = malloc(starfield_data->star_count * sizeof(lv_star_t));
    if (starfield_data->stars == NULL) {
        free(starfield_data);
        lv_obj_delete(obj);
        return NULL;
    }
    
    // Store data as user data
    lv_obj_set_user_data(obj, starfield_data);
    
    // Set to fill parent by default
    lv_obj_set_size(obj, LV_PCT(100), LV_PCT(100));
    
    // Make it non-clickable and transparent
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    
    // Add event callbacks
    lv_obj_add_event_cb(obj, lv_starfield_draw_event_cb, LV_EVENT_DRAW_MAIN, NULL);
    lv_obj_add_event_cb(obj, lv_starfield_destructor_event_cb, LV_EVENT_DELETE, NULL);
    
    // Initialize stars after we know the object's area
    lv_obj_update_layout(obj);
    lv_area_t area;
    lv_obj_get_coords(obj, &area);
    
    for (uint16_t i = 0; i < starfield_data->star_count; i++) {
        init_star(&starfield_data->stars[i], &area);
    }
    
    // Create animation timer
    starfield_data->animation_timer = lv_timer_create(animation_timer_cb, 50, obj);  // 20 FPS
    if (starfield_data->animation_timer) {
        lv_timer_set_repeat_count(starfield_data->animation_timer, -1);  // Infinite
    }
    
    ESP_LOGI(TAG, "Starfield created with %d stars", starfield_data->star_count);
    
    return obj;
}

static void lv_starfield_destructor_event_cb(lv_event_t * e) {
    lv_obj_t * obj = lv_event_get_target(e);
    lv_starfield_data_t * starfield_data = lv_obj_get_user_data(obj);
    if (starfield_data) {
        if (starfield_data->animation_timer) {
            lv_timer_delete(starfield_data->animation_timer);
        }
        if (starfield_data->stars) {
            free(starfield_data->stars);
        }
        free(starfield_data);
        lv_obj_set_user_data(obj, NULL);
    }
}

static void init_star(lv_star_t * star, lv_area_t * area) {
    int32_t width = lv_area_get_width(area);
    int32_t height = lv_area_get_height(area);
    
    star->x = (float)(rand() % width);
    star->y = (float)(rand() % height);
    star->base_brightness = (rand() % 12) + 1;
    
    // 25% chance for extra bright star
    if (rand() % 4 == 0) {
        star->base_brightness = (rand() % 15) + 1;
    }
    
    star->brightness = star->base_brightness;
    star->move_counter = rand() % LV_STARFIELD_DEFAULT_MOVE_COUNTER_MAX;
}

static bool check_sibling_exclusion(lv_obj_t * starfield, int32_t x, int32_t y) {
    lv_obj_t * parent = lv_obj_get_parent(starfield);
    if (!parent) return false;
    
    // Convert to parent coordinates
    lv_area_t starfield_coords;
    lv_obj_get_coords(starfield, &starfield_coords);
    int32_t abs_x = starfield_coords.x1 + x;
    int32_t abs_y = starfield_coords.y1 + y;
    
    // Check all siblings
    uint32_t child_cnt = lv_obj_get_child_count(parent);
    for (uint32_t i = 0; i < child_cnt; i++) {
        lv_obj_t * child = lv_obj_get_child(parent, i);
        if (child == starfield) continue;  // Skip self
        
        // Check if point is within this sibling's area
        lv_area_t child_coords;
        lv_obj_get_coords(child, &child_coords);
        
        if (abs_x >= child_coords.x1 && abs_x <= child_coords.x2 &&
            abs_y >= child_coords.y1 && abs_y <= child_coords.y2) {
            // Special handling for transparent/non-visible objects
            if (lv_obj_get_style_bg_opa(child, 0) == LV_OPA_TRANSP) {
                continue;  // Don't exclude for transparent objects
            }
            return true;  // Exclude this point
        }
    }
    
    return false;
}

static void lv_starfield_draw_event_cb(lv_event_t * e) {
    lv_obj_t * obj = lv_event_get_target(e);
    lv_starfield_data_t * starfield_data = lv_obj_get_user_data(obj);
    if (!starfield_data || !starfield_data->stars) return;
    
    lv_layer_t * layer = lv_event_get_layer(e);
    
    // Get object coordinates
    lv_area_t obj_coords;
    lv_obj_get_coords(obj, &obj_coords);
    
    // Draw each star
    for (uint16_t i = 0; i < starfield_data->star_count; i++) {
        lv_star_t * star = &starfield_data->stars[i];
        
        // Calculate absolute position
        int32_t x = obj_coords.x1 + (int32_t)star->x;
        int32_t y = obj_coords.y1 + (int32_t)star->y;
        
        // Check custom exclusion zones
        bool excluded = false;
        if (starfield_data->exclusion_checks && starfield_data->exclusion_count > 0) {
            for (size_t j = 0; j < starfield_data->exclusion_count; j++) {
                if (starfield_data->exclusion_checks[j] && 
                    starfield_data->exclusion_checks[j](x, y, starfield_data->exclusion_user_data)) {
                    excluded = true;
                    break;
                }
            }
        }
        
        // Check sibling exclusion
        if (!excluded && starfield_data->exclude_siblings) {
            excluded = check_sibling_exclusion(obj, (int32_t)star->x, (int32_t)star->y);
        }
        
        if (!excluded) {
            // Convert 0-15 brightness to 0-255
            uint8_t scaled_gray = star->brightness * 17;
            
            // Draw the star as a single pixel
            lv_draw_rect_dsc_t rect_dsc;
            lv_draw_rect_dsc_init(&rect_dsc);
            rect_dsc.bg_color = lv_color_make(scaled_gray, scaled_gray, scaled_gray);
            rect_dsc.bg_opa = LV_OPA_COVER;
            rect_dsc.border_width = 0;
            
            lv_area_t pixel_area = {x, y, x, y};
            lv_draw_rect(layer, &rect_dsc, &pixel_area);
        }
    }
}

static void animation_timer_cb(lv_timer_t * timer) {
    lv_obj_t * obj = (lv_obj_t *)lv_timer_get_user_data(timer);
    if (!obj) return;
    
    lv_starfield_data_t * starfield_data = lv_obj_get_user_data(obj);
    if (!starfield_data || !starfield_data->stars) return;
    
    // Get object area for bounds
    lv_area_t area;
    lv_obj_get_coords(obj, &area);
    int32_t width = lv_area_get_width(&area);
    int32_t height = lv_area_get_height(&area);
    
    bool needs_redraw = false;
    
    for (uint16_t i = 0; i < starfield_data->star_count; i++) {
        lv_star_t * star = &starfield_data->stars[i];
        
        // Update twinkling
        int brightness_variance = (rand() % (starfield_data->twinkle_variance * 2 + 1)) - 
                                 starfield_data->twinkle_variance;
        int new_brightness = star->base_brightness + brightness_variance;
        uint8_t old_brightness = star->brightness;
        star->brightness = LV_CLAMP(1, new_brightness, 15);
        
        if (star->brightness != old_brightness) {
            needs_redraw = true;
        }
        
        // Update movement counter
        star->move_counter++;
        if (star->move_counter >= starfield_data->move_counter_max) {
            star->move_counter = 0;
            
            // Check if star should move
            if ((rand() % 100) < starfield_data->move_chance) {
                star->x = (float)(rand() % width);
                star->y = (float)(rand() % height);
                needs_redraw = true;
                
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
    
    if (needs_redraw) {
        lv_obj_invalidate(obj);
    }
}

// Setter functions
void lv_starfield_set_count(lv_obj_t * obj, uint16_t count) {
    lv_starfield_data_t * starfield_data = lv_obj_get_user_data(obj);
    if (!starfield_data || starfield_data->star_count == count) return;
    
    // Reallocate stars array
    lv_star_t * new_stars = realloc(starfield_data->stars, count * sizeof(lv_star_t));
    if (!new_stars) {
        ESP_LOGE(TAG, "Failed to reallocate stars");
        return;
    }
    
    starfield_data->stars = new_stars;
    
    // Initialize new stars if count increased
    if (count > starfield_data->star_count) {
        lv_area_t area;
        lv_obj_get_coords(obj, &area);
        
        for (uint16_t i = starfield_data->star_count; i < count; i++) {
            init_star(&starfield_data->stars[i], &area);
        }
    }
    
    starfield_data->star_count = count;
    lv_obj_invalidate(obj);
}

void lv_starfield_set_twinkle_variance(lv_obj_t * obj, uint8_t variance) {
    lv_starfield_data_t * starfield_data = lv_obj_get_user_data(obj);
    if (starfield_data) {
        starfield_data->twinkle_variance = variance;
    }
}

void lv_starfield_set_movement(lv_obj_t * obj, uint8_t move_chance, uint16_t counter_max) {
    lv_starfield_data_t * starfield_data = lv_obj_get_user_data(obj);
    if (starfield_data) {
        starfield_data->move_chance = move_chance;
        starfield_data->move_counter_max = counter_max;
    }
}

void lv_starfield_set_exclusion_checks(lv_obj_t * obj, 
                                      lv_starfield_exclusion_check_fn * checks,
                                      size_t count,
                                      void * user_data) {
    lv_starfield_data_t * starfield_data = lv_obj_get_user_data(obj);
    if (starfield_data) {
        starfield_data->exclusion_checks = checks;
        starfield_data->exclusion_count = count;
        starfield_data->exclusion_user_data = user_data;
        lv_obj_invalidate(obj);
    }
}

void lv_starfield_set_exclude_siblings(lv_obj_t * obj, bool enable) {
    lv_starfield_data_t * starfield_data = lv_obj_get_user_data(obj);
    if (starfield_data) {
        starfield_data->exclude_siblings = enable;
        lv_obj_invalidate(obj);
    }
}

void lv_starfield_reset(lv_obj_t * obj) {
    lv_starfield_data_t * starfield_data = lv_obj_get_user_data(obj);
    if (!starfield_data || !starfield_data->stars) return;
    
    lv_area_t area;
    lv_obj_get_coords(obj, &area);
    
    for (uint16_t i = 0; i < starfield_data->star_count; i++) {
        init_star(&starfield_data->stars[i], &area);
    }
    
    lv_obj_invalidate(obj);
    ESP_LOGI(TAG, "Starfield reset");
}
