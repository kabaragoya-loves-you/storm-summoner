#include "lvgl.h"
#include "ui.h"
#include "shared_canvas_buffer.h"
#include "esp_log.h"

#define TAG "SPLASH"

//=============================================================================
// SPLASH MODULE CONFIGURATION
//=============================================================================

// Sunburst gradient colors (gold center to darker gold edge)
#define SPLASH_CENTER_R  0xF0
#define SPLASH_CENTER_G  0xA6
#define SPLASH_CENTER_B  0x47

#define SPLASH_EDGE_R    0xC9
#define SPLASH_EDGE_G    0x8A
#define SPLASH_EDGE_B    0x40

// SVG overlay
#define SPLASH_IMAGE_PATH     "A:images/kabaragoya.svg"

//=============================================================================

extern lv_obj_t *canvas;

static lv_obj_t *g_screen = NULL;
static lv_obj_t *g_svg_image = NULL;
static lv_style_t style_grad;
static lv_grad_dsc_t grad;

static void splash_draw_deferred_cb(lv_timer_t *timer) {
  if (!canvas) {
    lv_timer_delete(timer);
    return;
  }
  
  if (g_screen == NULL) {
    
    lv_display_t *disp = lv_obj_get_display(canvas);
    if (!disp) {
      ESP_LOGE(TAG, "Failed to get display from canvas");
      lv_timer_delete(timer);
      return;
    }
    
    uint16_t disp_width = shared_canvas_buffer_get_width();
    uint16_t disp_height = shared_canvas_buffer_get_height();
    
    // Create screen
    g_screen = lv_obj_create(NULL);
    lv_obj_set_size(g_screen, disp_width, disp_height);
    lv_obj_remove_flag(g_screen, LV_OBJ_FLAG_SCROLLABLE);
    
    // Configure radial gradient (center to corner)
    // The to_x, to_y define the outer circle edge point
    lv_grad_radial_init(&grad, 
      LV_PCT(50), LV_PCT(50),   // center x, y (50% = center of widget)
      LV_PCT(100), LV_PCT(50),  // to_x, to_y (edge of circle)
      LV_GRAD_EXTEND_PAD);      // pad color at edges
    
    // Set up gradient color stops (center gold to edge darker gold)
    lv_color_t colors[2] = {
      lv_color_make(SPLASH_CENTER_R, SPLASH_CENTER_G, SPLASH_CENTER_B),
      lv_color_make(SPLASH_EDGE_R, SPLASH_EDGE_G, SPLASH_EDGE_B)
    };
    lv_opa_t opas[2] = { LV_OPA_COVER, LV_OPA_COVER };
    uint8_t fracs[2] = { 0, 255 };  // 0% to 100%
    lv_grad_init_stops(&grad, colors, opas, fracs, 2);
    
    // Apply gradient style
    lv_style_init(&style_grad);
    lv_style_set_bg_grad(&style_grad, &grad);
    lv_style_set_bg_opa(&style_grad, LV_OPA_COVER);
    lv_obj_add_style(g_screen, &style_grad, 0);
    
    // Create image widget
    g_svg_image = lv_image_create(g_screen);
    lv_image_set_src(g_svg_image, SPLASH_IMAGE_PATH);
    lv_obj_center(g_svg_image);
    
    ESP_LOGI(TAG, "Splash screen created");
  }
  
  lv_screen_load(g_screen);
  lv_timer_delete(timer);
}

UI_CREATE_DEFERRED_DRAW_FUNC(splash, splash_draw_deferred_cb)

static void splash_teardown(void) {
  if (g_screen) {
    lv_obj_delete(g_screen);
    g_screen = NULL;
    g_svg_image = NULL;
  }
  ESP_LOGD(TAG, "Splash module teardown complete");
}

static void splash_init(void) {
  ESP_LOGI(TAG, "Splash module initialized");
}

ui_draw_module_t splash_module = {
  .draw_func = splash_draw,
  .teardown_func = splash_teardown,
  .init_func = splash_init,
  .name = "splash"
};

