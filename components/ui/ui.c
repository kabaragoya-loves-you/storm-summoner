#include "lvgl.h"
#include "ui.h"
#include <math.h>  // For sin/cos calculations
#include "esp_log.h"

lv_obj_t *canvas = NULL;
static lv_obj_t *circle = NULL;

// LVGL canvas buffer (16-bit color) - ensure proper alignment
static lv_color_t display_buf[128 * 128] __attribute__((aligned(4)));

// Define center point of the display and radius
#define CENTER_X 64
#define CENTER_Y 64
#define RADIUS 60
#define TAG "UI"

// Define the number of slices
#define SLICE_COUNT 8

// Define grayscale tone for filling (0-15 for SSD1327)
#define GRAY_TONE 6
#define DEFAULT_BITE_SIZE 25 // Pixels removed from the center of the slice (0 for full slice)

void boundary_circle(void) {
  // Create circle only if it doesn't exist
  if (circle == NULL) {
    circle = lv_obj_create(lv_scr_act());
    lv_obj_set_size(circle, 128, 128);
    lv_obj_set_style_bg_opa(circle, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(circle, 1, 0);
    lv_obj_set_style_border_color(circle, lv_color_white(), 0);
    lv_obj_set_style_radius(circle, LV_RADIUS_CIRCLE, 0);
    lv_obj_align(circle, LV_ALIGN_CENTER, 0, 0);
  }
}

void pizza(void) {
  // Make sure canvas is initialized
  if (!canvas) return;

  // Initialize drawing layer
  lv_layer_t layer;
  lv_canvas_init_layer(canvas, &layer);
  
  // Clear the canvas with black
  lv_canvas_fill_bg(canvas, lv_color_black(), LV_OPA_COVER);
  
  // Draw the circle boundary
  lv_draw_arc_dsc_t arc_dsc;
  lv_draw_arc_dsc_init(&arc_dsc);
  arc_dsc.color = lv_color_white();
  arc_dsc.width = 1;
  arc_dsc.opa = LV_OPA_COVER;
  arc_dsc.center.x = CENTER_X;
  arc_dsc.center.y = CENTER_Y;
  arc_dsc.radius = RADIUS;
  arc_dsc.start_angle = 0;
  arc_dsc.end_angle = 360;
  lv_draw_arc(&layer, &arc_dsc);
  
  // Setup for drawing lines
  lv_draw_line_dsc_t line_dsc;
  lv_draw_line_dsc_init(&line_dsc);
  line_dsc.color = lv_color_white();
  line_dsc.width = 1;
  line_dsc.opa = LV_OPA_COVER;
  
  // Draw lines along the major axes to create the 8 pizza slices
  // Horizontal axis
  line_dsc.p1.x = CENTER_X - RADIUS;
  line_dsc.p1.y = CENTER_Y;
  line_dsc.p2.x = CENTER_X + RADIUS;
  line_dsc.p2.y = CENTER_Y;
  lv_draw_line(&layer, &line_dsc);
  
  // Vertical axis
  line_dsc.p1.x = CENTER_X;
  line_dsc.p1.y = CENTER_Y - RADIUS;
  line_dsc.p2.x = CENTER_X;
  line_dsc.p2.y = CENTER_Y + RADIUS;
  lv_draw_line(&layer, &line_dsc);
  
  // Diagonal axis (top-left to bottom-right)
  line_dsc.p1.x = CENTER_X - (int)(RADIUS * 0.7071); // cos(45°) = 0.7071
  line_dsc.p1.y = CENTER_Y - (int)(RADIUS * 0.7071); // sin(45°) = 0.7071
  line_dsc.p2.x = CENTER_X + (int)(RADIUS * 0.7071);
  line_dsc.p2.y = CENTER_Y + (int)(RADIUS * 0.7071);
  lv_draw_line(&layer, &line_dsc);
  
  // Diagonal axis (top-right to bottom-left)
  line_dsc.p1.x = CENTER_X + (int)(RADIUS * 0.7071);
  line_dsc.p1.y = CENTER_Y - (int)(RADIUS * 0.7071);
  line_dsc.p2.x = CENTER_X - (int)(RADIUS * 0.7071);
  line_dsc.p2.y = CENTER_Y + (int)(RADIUS * 0.7071);
  lv_draw_line(&layer, &line_dsc);
  
  // Finish drawing
  lv_canvas_finish_layer(canvas, &layer);
}

void redraw_circle(void) {
  if (circle != NULL) {
    // Force the circle to redraw by invalidating it
    lv_obj_invalidate(circle);
  }
}

void lvgl_timer_cb(lv_timer_t *timer) {
  LV_UNUSED(timer);
  
  // Only redraw if canvas exists
  if (canvas != NULL) {
    lv_obj_invalidate(canvas);
  }
}

// Draw a single active (filled) slice with its outline
static void draw_active_filled_slice(lv_layer_t *layer, uint8_t slice_index) {
    ESP_LOGI(TAG, "draw_active_filled_slice: Drawing slice %d with bite size %d", slice_index, DEFAULT_BITE_SIZE);
    float slice_angle_degrees = 360.0f / SLICE_COUNT;
    float start_angle_deg = (float)slice_index * slice_angle_degrees;
    float end_angle_deg = start_angle_deg + slice_angle_degrees;

    float rad_start = start_angle_deg * (M_PI / 180.0f);
    float rad_end = end_angle_deg * (M_PI / 180.0f);

    lv_coord_t center_x_coord = CENTER_X;
    lv_coord_t center_y_coord = CENTER_Y;

    // 1. Draw the filled gray sector using multiple concentric 1px arcs
    if (DEFAULT_BITE_SIZE <= RADIUS) {
        lv_draw_arc_dsc_t fill_arc_dsc;
        lv_draw_arc_dsc_init(&fill_arc_dsc);
        uint8_t actual_gray_value_for_lvgl = (GRAY_TONE * 255) / 15;
        fill_arc_dsc.color = lv_color_make(actual_gray_value_for_lvgl, actual_gray_value_for_lvgl, actual_gray_value_for_lvgl);
        fill_arc_dsc.width = 1; // Each concentric arc is 1px wide
        fill_arc_dsc.opa = LV_OPA_COVER;
        fill_arc_dsc.center.x = center_x_coord;
        fill_arc_dsc.center.y = center_y_coord;
        fill_arc_dsc.start_angle = (int32_t)start_angle_deg;
        fill_arc_dsc.end_angle = (int32_t)end_angle_deg;
        fill_arc_dsc.rounded = 0;

        ESP_LOGD(TAG, "draw_active_filled_slice [%d]: Filling with concentric arcs from r=%d to r=%d", slice_index, DEFAULT_BITE_SIZE, RADIUS);
        for (uint16_t r = DEFAULT_BITE_SIZE; r <= RADIUS; r++) {
            if (r == 0 && DEFAULT_BITE_SIZE == 0) { /*ESP_LOGD(TAG, "Skipping r=0 for full fill");*/ continue; }
            fill_arc_dsc.radius = r;
            lv_draw_arc(layer, &fill_arc_dsc);
        }
        ESP_LOGD(TAG, "draw_active_filled_slice [%d]: Concentric fill done.", slice_index);
    }

    // Calculate points for radial lines
    lv_point_precise_t start_inner_p = {
        .x = center_x_coord + (lv_coord_t)(DEFAULT_BITE_SIZE * cosf(rad_start)),
        .y = center_y_coord + (lv_coord_t)(DEFAULT_BITE_SIZE * sinf(rad_start))
    };
    lv_point_precise_t start_outer_p = {
        .x = center_x_coord + (lv_coord_t)(RADIUS * cosf(rad_start)),
        .y = center_y_coord + (lv_coord_t)(RADIUS * sinf(rad_start))
    };
    lv_point_precise_t end_inner_p = {
        .x = center_x_coord + (lv_coord_t)(DEFAULT_BITE_SIZE * cosf(rad_end)),
        .y = center_y_coord + (lv_coord_t)(DEFAULT_BITE_SIZE * sinf(rad_end))
    };
    lv_point_precise_t end_outer_p = {
        .x = center_x_coord + (lv_coord_t)(RADIUS * cosf(rad_end)),
        .y = center_y_coord + (lv_coord_t)(RADIUS * sinf(rad_end))
    };

    // 2. Draw white radial lines on top of the fill
    lv_draw_line_dsc_t line_dsc;
    lv_draw_line_dsc_init(&line_dsc);
    line_dsc.color = lv_color_white();
    line_dsc.width = 1;
    line_dsc.opa = LV_OPA_COVER;

    line_dsc.p1 = start_inner_p;
    line_dsc.p2 = start_outer_p;
    lv_draw_line(layer, &line_dsc);

    line_dsc.p1 = end_inner_p;
    line_dsc.p2 = end_outer_p;
    lv_draw_line(layer, &line_dsc);
    ESP_LOGD(TAG, "draw_active_filled_slice [%d]: Radial lines drawn.", slice_index);

    // 3. Draw white outer arc segment (outline) on top of the fill
    lv_draw_arc_dsc_t outline_arc_dsc;
    lv_draw_arc_dsc_init(&outline_arc_dsc);
    outline_arc_dsc.color = lv_color_white();
    outline_arc_dsc.width = 1; 
    outline_arc_dsc.opa = LV_OPA_COVER;
    outline_arc_dsc.center.x = center_x_coord;
    outline_arc_dsc.center.y = center_y_coord;
    outline_arc_dsc.radius = RADIUS; 
    outline_arc_dsc.start_angle = (int32_t)start_angle_deg;
    outline_arc_dsc.end_angle = (int32_t)end_angle_deg;
    outline_arc_dsc.rounded = 0; 
    lv_draw_arc(layer, &outline_arc_dsc);
    ESP_LOGD(TAG, "draw_active_filled_slice [%d]: Outer arc drawn.", slice_index);

    // 4. Draw white inner arc segment (bite outline) if BITE_SIZE > 0
    if (DEFAULT_BITE_SIZE > 0) {
        lv_draw_arc_dsc_t bite_outline_arc_dsc;
        lv_draw_arc_dsc_init(&bite_outline_arc_dsc);
        bite_outline_arc_dsc.color = lv_color_white();
        bite_outline_arc_dsc.width = 1;
        bite_outline_arc_dsc.opa = LV_OPA_COVER;
        bite_outline_arc_dsc.center.x = center_x_coord;
        bite_outline_arc_dsc.center.y = center_y_coord;
        bite_outline_arc_dsc.radius = DEFAULT_BITE_SIZE;
        bite_outline_arc_dsc.start_angle = (int32_t)start_angle_deg;
        bite_outline_arc_dsc.end_angle = (int32_t)end_angle_deg;
        bite_outline_arc_dsc.rounded = 0;
        lv_draw_arc(layer, &bite_outline_arc_dsc);
        ESP_LOGD(TAG, "draw_active_filled_slice [%d]: Inner bite arc drawn.", slice_index);
    }
    ESP_LOGI(TAG, "draw_active_filled_slice: Finished drawing slice %d", slice_index);
}

void pizza2(const bool slice_states[SLICE_COUNT]) {
  if (!canvas) {
    ESP_LOGE(TAG, "pizza2: Canvas is NULL!");
    return;
  }
  ESP_LOGI(TAG, "pizza2: Starting (will call diagnostic draw_active_filled_slice).");

  lv_layer_t layer;
  lv_canvas_init_layer(canvas, &layer);
  if (!layer.draw_buf) {
    ESP_LOGE(TAG, "pizza2: Failed to initialize layer, draw_buf is NULL.");
    return;
  }
  ESP_LOGI(TAG, "pizza2: Layer initialized.");

  // Clear the canvas with black first.
  lv_canvas_fill_bg(canvas, lv_color_black(), LV_OPA_COVER);
  ESP_LOGI(TAG, "pizza2: Canvas cleared to black.");
  
  // Draw each active slice (which will only draw for slice_index 0 due to draw_active_filled_slice diagnostic state)
  ESP_LOGI(TAG, "pizza2: Looping to call draw_active_filled_slice...");
  for (uint8_t i = 0; i < SLICE_COUNT; i++) {
    if (slice_states[i]) { // slice_states[0] is true
      ESP_LOGI(TAG, "pizza2: Calling draw_active_filled_slice for slice_index %d", i);
      draw_active_filled_slice(&layer, i);
    }
  }
  ESP_LOGI(TAG, "pizza2: Loop completed.");
  
  lv_canvas_finish_layer(canvas, &layer);
  ESP_LOGI(TAG, "pizza2: Drawing completed (diagnostic single arc).");
}

void ui_init(void) {
  ESP_LOGI(TAG, "ui_init: Starting UI initialization."); // Log at the very start

  // Create canvas with proper buffer
  canvas = lv_canvas_create(lv_scr_act());
  if (canvas == NULL) {
    ESP_LOGE(TAG, "ui_init: Failed to create canvas!");
    return; // Critical failure
  }
  ESP_LOGI(TAG, "ui_init: Canvas object created successfully.");

  // Attempt to remove all styles from the canvas before doing anything else to it
  lv_obj_remove_style_all(canvas);
  ESP_LOGI(TAG, "ui_init: lv_obj_remove_style_all(canvas) executed.");

  lv_canvas_set_buffer(
    canvas,
    display_buf,
    128,
    128,
    LV_COLOR_FORMAT_NATIVE
  );
  ESP_LOGI(TAG, "ui_init: Canvas buffer set.");

  lv_obj_set_size(canvas, 128, 128);
  ESP_LOGI(TAG, "ui_init: lv_obj_set_size(canvas, 128, 128) executed.");

  lv_obj_center(canvas); // Test this call now
  ESP_LOGI(TAG, "ui_init: lv_obj_center(canvas) executed.");

  // ESP_LOGI(TAG, "ui_init: Skipping pizza2 call for diagnostics."); // Old log
  
  bool slices[SLICE_COUNT] = {true, false, true, false, true, false, true, false};
  ESP_LOGI(TAG, "ui_init: Preparing to call pizza2...");
  pizza2(slices);
  ESP_LOGI(TAG, "ui_init: Returned from pizza2.");
  
  // Create timer with error checking
  // Using a longer refresh rate since we don't need 60fps for a static circle
  lv_timer_t *timer = lv_timer_create(lvgl_timer_cb, 33, NULL);  // ~30fps
  if (timer == NULL) {
    ESP_LOGE(TAG, "ui_init: Failed to create lvgl_timer_cb timer!");
    // Handle timer creation failure, e.g., by not invalidating, 
    // but canvas and initial pizza2 draw are already done.
    // Depending on desired behavior, could delete canvas or just log.
    // For now, just log, as initial draw is complete.
  }
  ESP_LOGI(TAG, "ui_init: lvgl_timer_cb timer created.");
}
