#include "lvgl.h"
#include "ui.h"
#include "lv_vector_art.h"
#include "display_console.h"
#include "version.h"
#include "esp_log.h"

#define TAG "SPLASH"

//=============================================================================
// SPLASH MODULE CONFIGURATION
//=============================================================================

// Sunburst gradient colors (gold center to darker gold edge)
#define SPLASH_CENTER_R  255
#define SPLASH_CENTER_G  200
#define SPLASH_CENTER_B  100

#define SPLASH_EDGE_R    180
#define SPLASH_EDGE_G    100
#define SPLASH_EDGE_B    40

// Vector art overlay
#define SPLASH_VECTOR_PATH "/assets/images/kabaragoya_vec.bin.z"
#define BUBBLE_VECTOR_PATH "/assets/images/bubble.bin.z"

// Bubble dimensions (pre-scaled to target size)
#define BUBBLE_WIDTH   41
#define BUBBLE_HEIGHT  31

// Animation timing (milliseconds)
#define ANIM_FADE_IN_DURATION     1000   // 0-1s: backlight and background fade
#define ANIM_SPIN_START           500    // 0.5s: start spinning text
#define ANIM_SPIN_DURATION        1000   // 1s: spin duration (lands at 1.5s)
#define ANIM_KABARAGOYA_START     1500   // 1.5s: second label appears
#define ANIM_KABARAGOYA_DURATION  300    // 0.3s fade in
#define ANIM_SHADOW_START         1800   // 1.8s: shadows appear
#define ANIM_SHADOW_DURATION      300    // 0.3s fade in
#define ANIM_BUBBLE_START         2500   // 2.5s: speech bubble appears (400ms after text done)
#define ANIM_BUBBLE_DURATION      150    // 0.15s pop in

// Shadow offset
#define SHADOW_OFFSET_X  2
#define SHADOW_OFFSET_Y  3

//=============================================================================
// STATE
//=============================================================================

static lv_obj_t *g_screen = NULL;
static lv_obj_t *g_content_container = NULL;  // Holds gradient + art (for opacity animation)
static lv_obj_t *g_vector_art = NULL;
static lv_grad_dsc_t g_grad;

// Labels
static lv_obj_t *g_storm_shadow = NULL;       // Shadow for "Storm Summoner"
static lv_obj_t *g_storm_label = NULL;        // Main "Storm Summoner"
static lv_obj_t *g_kaba_shadow = NULL;        // Shadow for "Kabaragoya"
static lv_obj_t *g_kaba_label = NULL;         // Main "Kabaragoya"
static lv_obj_t *g_version_label = NULL;      // Version at bottom
static lv_obj_t *g_bubble_container = NULL;   // Container for bubble + text
static lv_obj_t *g_bubble_art = NULL;         // Vector art speech bubble
static lv_obj_t *g_bubble_label = NULL;       // Text inside bubble

// Display dimensions
static uint16_t g_disp_width = 0;
static uint16_t g_disp_height = 0;

// Animation state
static lv_anim_t g_backlight_anim;

// External font declarations
LV_FONT_DECLARE(flyer_venice_20);
LV_FONT_DECLARE(flyer_venice_24);
LV_FONT_DECLARE(chalet_ny_8);
LV_FONT_DECLARE(chalet_ny_14);

//=============================================================================
// ANIMATION CALLBACKS
//=============================================================================

// Backlight animation callback
static void backlight_anim_cb(void *var, int32_t value) {
  (void)var;
  display_set_brightness((uint8_t)value);
}

// Content container opacity animation callback
static void content_opa_anim_cb(void *obj, int32_t value) {
  lv_obj_set_style_opa((lv_obj_t *)obj, (lv_opa_t)value, 0);
}

// Label opacity animation callback
static void label_opa_anim_cb(void *obj, int32_t value) {
  lv_obj_set_style_text_opa((lv_obj_t *)obj, (lv_opa_t)value, 0);
}

// Rotation animation callback (value in 0.1 degree units)
static void rotation_anim_cb(void *obj, int32_t value) {
  lv_obj_set_style_transform_rotation((lv_obj_t *)obj, value, 0);
}

// X position animation callback
static void x_pos_anim_cb(void *obj, int32_t value) {
  lv_obj_set_x((lv_obj_t *)obj, value);
}

// Scale animation callback (value in 256 = 100%)
static void scale_anim_cb(void *obj, int32_t value) {
  lv_obj_set_style_transform_scale(obj, value, 0);
}

// Called when bubble animation is about to start - set up transform properties
static void bubble_anim_start_cb(lv_anim_t *anim) {
  lv_obj_t *container = (lv_obj_t *)lv_anim_get_user_data(anim);
  // Now safe to set transform properties and show the bubble
  lv_obj_remove_flag(container, LV_OBJ_FLAG_HIDDEN);
  lv_obj_set_style_transform_pivot_x(container, lv_obj_get_width(container) / 2, 0);
  lv_obj_set_style_transform_pivot_y(container, lv_obj_get_height(container) / 2, 0);
  lv_obj_set_style_transform_scale(container, 0, 0);  // Start at scale 0
}

//=============================================================================
// ANIMATION SETUP
//=============================================================================

static void start_animations(void) {
  // === 1. Backlight fade: 0 -> 100% over 1s ===
  lv_anim_init(&g_backlight_anim);
  lv_anim_set_var(&g_backlight_anim, NULL);
  lv_anim_set_values(&g_backlight_anim, 0, 100);
  lv_anim_set_duration(&g_backlight_anim, ANIM_FADE_IN_DURATION);
  lv_anim_set_exec_cb(&g_backlight_anim, backlight_anim_cb);
  lv_anim_set_path_cb(&g_backlight_anim, lv_anim_path_ease_out);
  lv_anim_start(&g_backlight_anim);
  
  // === 2. Content container opacity: 0 -> 255 over 1s ===
  lv_anim_t content_anim;
  lv_anim_init(&content_anim);
  lv_anim_set_var(&content_anim, g_content_container);
  lv_anim_set_values(&content_anim, LV_OPA_TRANSP, LV_OPA_COVER);
  lv_anim_set_duration(&content_anim, ANIM_FADE_IN_DURATION);
  lv_anim_set_exec_cb(&content_anim, content_opa_anim_cb);
  lv_anim_set_path_cb(&content_anim, lv_anim_path_ease_out);
  lv_anim_start(&content_anim);
  
  // === 3. "Storm Summoner" spin-in: starts at 0.5s, duration 1s ===
  // Position animation: off-screen right -> center
  int32_t start_x = g_disp_width + 50;
  int32_t end_x = (g_disp_width - lv_obj_get_width(g_storm_label)) / 2;
  
  lv_anim_t storm_x_anim;
  lv_anim_init(&storm_x_anim);
  lv_anim_set_var(&storm_x_anim, g_storm_label);
  lv_anim_set_values(&storm_x_anim, start_x, end_x);
  lv_anim_set_duration(&storm_x_anim, ANIM_SPIN_DURATION);
  lv_anim_set_delay(&storm_x_anim, ANIM_SPIN_START);
  lv_anim_set_exec_cb(&storm_x_anim, x_pos_anim_cb);
  lv_anim_set_path_cb(&storm_x_anim, lv_anim_path_ease_out);
  lv_anim_start(&storm_x_anim);
  
  // Rotation animation: 7200 -> 0 (720° CCW in 0.1° units)
  lv_anim_t storm_rot_anim;
  lv_anim_init(&storm_rot_anim);
  lv_anim_set_var(&storm_rot_anim, g_storm_label);
  lv_anim_set_values(&storm_rot_anim, -7200, 0);  // Negative for CCW
  lv_anim_set_duration(&storm_rot_anim, ANIM_SPIN_DURATION);
  lv_anim_set_delay(&storm_rot_anim, ANIM_SPIN_START);
  lv_anim_set_exec_cb(&storm_rot_anim, rotation_anim_cb);
  lv_anim_set_path_cb(&storm_rot_anim, lv_anim_path_ease_out);
  lv_anim_start(&storm_rot_anim);
  
  // Opacity for storm label (fade in during spin)
  lv_anim_t storm_opa_anim;
  lv_anim_init(&storm_opa_anim);
  lv_anim_set_var(&storm_opa_anim, g_storm_label);
  lv_anim_set_values(&storm_opa_anim, LV_OPA_TRANSP, LV_OPA_COVER);
  lv_anim_set_duration(&storm_opa_anim, ANIM_SPIN_DURATION / 2);
  lv_anim_set_delay(&storm_opa_anim, ANIM_SPIN_START);
  lv_anim_set_exec_cb(&storm_opa_anim, label_opa_anim_cb);
  lv_anim_start(&storm_opa_anim);
  
  // === 4. "Kabaragoya" fade-in: starts at 1.5s ===
  lv_anim_t kaba_anim;
  lv_anim_init(&kaba_anim);
  lv_anim_set_var(&kaba_anim, g_kaba_label);
  lv_anim_set_values(&kaba_anim, LV_OPA_TRANSP, LV_OPA_COVER);
  lv_anim_set_duration(&kaba_anim, ANIM_KABARAGOYA_DURATION);
  lv_anim_set_delay(&kaba_anim, ANIM_KABARAGOYA_START);
  lv_anim_set_exec_cb(&kaba_anim, label_opa_anim_cb);
  lv_anim_set_path_cb(&kaba_anim, lv_anim_path_ease_in);
  lv_anim_start(&kaba_anim);
  
  // === 5. Shadow labels fade-in: starts at 1.8s, 50% opacity ===
  // Storm Summoner shadow
  lv_anim_t storm_shadow_anim;
  lv_anim_init(&storm_shadow_anim);
  lv_anim_set_var(&storm_shadow_anim, g_storm_shadow);
  lv_anim_set_values(&storm_shadow_anim, LV_OPA_TRANSP, LV_OPA_50);
  lv_anim_set_duration(&storm_shadow_anim, ANIM_SHADOW_DURATION);
  lv_anim_set_delay(&storm_shadow_anim, ANIM_SHADOW_START);
  lv_anim_set_exec_cb(&storm_shadow_anim, label_opa_anim_cb);
  lv_anim_start(&storm_shadow_anim);
  
  // Kabaragoya shadow
  lv_anim_t kaba_shadow_anim;
  lv_anim_init(&kaba_shadow_anim);
  lv_anim_set_var(&kaba_shadow_anim, g_kaba_shadow);
  lv_anim_set_values(&kaba_shadow_anim, LV_OPA_TRANSP, LV_OPA_50);
  lv_anim_set_duration(&kaba_shadow_anim, ANIM_SHADOW_DURATION);
  lv_anim_set_delay(&kaba_shadow_anim, ANIM_SHADOW_START);
  lv_anim_set_exec_cb(&kaba_shadow_anim, label_opa_anim_cb);
  lv_anim_start(&kaba_shadow_anim);
  
  // === 6. Version label fade-in: starts at 1.8s ===
  lv_anim_t version_anim;
  lv_anim_init(&version_anim);
  lv_anim_set_var(&version_anim, g_version_label);
  lv_anim_set_values(&version_anim, LV_OPA_TRANSP, LV_OPA_COVER);
  lv_anim_set_duration(&version_anim, ANIM_SHADOW_DURATION);
  lv_anim_set_delay(&version_anim, ANIM_SHADOW_START);
  lv_anim_set_exec_cb(&version_anim, label_opa_anim_cb);
  lv_anim_start(&version_anim);
  
  // === 7. Speech bubble pop-in: starts at 2.5s ===
  // Scale from 0 to 256 (100%) - uses start_cb to defer transform setup
  lv_anim_t bubble_scale_anim;
  lv_anim_init(&bubble_scale_anim);
  lv_anim_set_var(&bubble_scale_anim, g_bubble_container);
  lv_anim_set_user_data(&bubble_scale_anim, g_bubble_container);
  lv_anim_set_values(&bubble_scale_anim, 0, 256);
  lv_anim_set_duration(&bubble_scale_anim, ANIM_BUBBLE_DURATION);
  lv_anim_set_delay(&bubble_scale_anim, ANIM_BUBBLE_START);
  lv_anim_set_exec_cb(&bubble_scale_anim, scale_anim_cb);
  lv_anim_set_start_cb(&bubble_scale_anim, bubble_anim_start_cb);
  lv_anim_set_path_cb(&bubble_scale_anim, lv_anim_path_overshoot);
  lv_anim_start(&bubble_scale_anim);
  
  ESP_LOGD(TAG, "Splash animations started");
}

//=============================================================================
// MODULE CALLBACKS
//=============================================================================

static void splash_draw_deferred_cb(lv_timer_t *timer) {
  if (g_screen != NULL) {
    lv_screen_load(g_screen);
    lv_timer_delete(timer);
    return;
  }
  
  // Set backlight to 0 before creating content
  display_set_brightness(0);
  
  lv_display_t *disp = lv_display_get_default();
  g_disp_width = lv_display_get_horizontal_resolution(disp);
  g_disp_height = lv_display_get_vertical_resolution(disp);
  
  // Create main screen with BLACK background
  g_screen = lv_obj_create(NULL);
  lv_obj_set_size(g_screen, g_disp_width, g_disp_height);
  lv_obj_remove_flag(g_screen, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(g_screen, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(g_screen, LV_OPA_COVER, 0);
  
  // Create content container (gradient + vector art) - starts transparent
  g_content_container = lv_obj_create(g_screen);
  lv_obj_set_size(g_content_container, g_disp_width, g_disp_height);
  lv_obj_set_pos(g_content_container, 0, 0);
  lv_obj_remove_flag(g_content_container, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_border_width(g_content_container, 0, 0);
  lv_obj_set_style_pad_all(g_content_container, 0, 0);
  lv_obj_set_style_opa(g_content_container, LV_OPA_TRANSP, 0);  // Start invisible
  
  // Set up radial gradient on container
  lv_grad_radial_init(&g_grad, 
    LV_PCT(50), LV_PCT(50),
    LV_PCT(100), LV_PCT(50),
    LV_GRAD_EXTEND_PAD);
  
  lv_color_t colors[2] = {
    lv_color_make(SPLASH_CENTER_R, SPLASH_CENTER_G, SPLASH_CENTER_B),
    lv_color_make(SPLASH_EDGE_R, SPLASH_EDGE_G, SPLASH_EDGE_B)
  };
  lv_opa_t opas[2] = { LV_OPA_COVER, LV_OPA_COVER };
  uint8_t fracs[2] = { 0, 255 };
  lv_grad_init_stops(&g_grad, colors, opas, fracs, 2);
  
  lv_obj_set_style_bg_grad(g_content_container, &g_grad, 0);
  lv_obj_set_style_bg_opa(g_content_container, LV_OPA_COVER, 0);
  
  // Create vector art widget
  g_vector_art = lv_vector_art_create(g_content_container);
  lv_obj_set_size(g_vector_art, g_disp_width, g_disp_height);
  lv_obj_center(g_vector_art);
  
  float scale = (float)g_disp_width / 240.0f;
  lv_vector_art_set_scale(g_vector_art, scale);
  
  if (lv_vector_art_set_src(g_vector_art, SPLASH_VECTOR_PATH)) {
    ESP_LOGD(TAG, "Vector art loaded");
  } else {
    ESP_LOGE(TAG, "Failed to load vector art");
  }
  
  // Calculate label positions
  int32_t center_y = g_disp_height / 2;
  int32_t storm_y = center_y - 20;  // Above center
  int32_t kaba_y = center_y + 10;   // Below Storm Summoner
  
  // === Create shadow labels first (behind main labels) ===
  
  // Storm Summoner shadow (larger font, offset)
  g_storm_shadow = lv_label_create(g_screen);
  lv_label_set_text(g_storm_shadow, "Storm Summoner");
  lv_obj_set_style_text_font(g_storm_shadow, &flyer_venice_24, 0);
  lv_obj_set_style_text_color(g_storm_shadow, lv_color_black(), 0);
  lv_obj_set_style_text_opa(g_storm_shadow, LV_OPA_TRANSP, 0);  // Start invisible
  lv_obj_update_layout(g_storm_shadow);
  lv_obj_set_pos(g_storm_shadow, 
    (g_disp_width - lv_obj_get_width(g_storm_shadow)) / 2 + SHADOW_OFFSET_X,
    storm_y + SHADOW_OFFSET_Y);
  
  // Kabaragoya shadow (same font, offset)
  g_kaba_shadow = lv_label_create(g_screen);
  lv_label_set_text(g_kaba_shadow, "Kabaragoya");
  lv_obj_set_style_text_font(g_kaba_shadow, &flyer_venice_20, 0);  // Same as main
  lv_obj_set_style_text_color(g_kaba_shadow, lv_color_black(), 0);
  lv_obj_set_style_text_opa(g_kaba_shadow, LV_OPA_TRANSP, 0);  // Start invisible
  lv_obj_update_layout(g_kaba_shadow);
  lv_obj_set_pos(g_kaba_shadow,
    (g_disp_width - lv_obj_get_width(g_kaba_shadow)) / 2 + SHADOW_OFFSET_X,
    kaba_y + SHADOW_OFFSET_Y);
  
  // === Create main labels ===
  
  // "Storm Summoner" - starts off-screen right, invisible
  g_storm_label = lv_label_create(g_screen);
  lv_label_set_text(g_storm_label, "Storm Summoner");
  lv_obj_set_style_text_font(g_storm_label, &flyer_venice_24, 0);
  lv_obj_set_style_text_color(g_storm_label, lv_color_white(), 0);
  lv_obj_set_style_text_opa(g_storm_label, LV_OPA_TRANSP, 0);  // Start invisible
  lv_obj_update_layout(g_storm_label);
  
  // Set transform pivot to center of label for rotation
  lv_obj_set_style_transform_pivot_x(g_storm_label, lv_obj_get_width(g_storm_label) / 2, 0);
  lv_obj_set_style_transform_pivot_y(g_storm_label, lv_obj_get_height(g_storm_label) / 2, 0);
  
  // Start off-screen right
  lv_obj_set_pos(g_storm_label, g_disp_width + 50, storm_y);
  
  // "Kabaragoya" - centered below, starts invisible
  g_kaba_label = lv_label_create(g_screen);
  lv_label_set_text(g_kaba_label, "Kabaragoya");
  lv_obj_set_style_text_font(g_kaba_label, &flyer_venice_20, 0);
  lv_obj_set_style_text_color(g_kaba_label, lv_color_white(), 0);
  lv_obj_set_style_text_opa(g_kaba_label, LV_OPA_TRANSP, 0);  // Start invisible
  lv_obj_update_layout(g_kaba_label);
  lv_obj_set_pos(g_kaba_label,
    (g_disp_width - lv_obj_get_width(g_kaba_label)) / 2,
    kaba_y);
  
  // === Version label at bottom ===
  g_version_label = lv_label_create(g_screen);
  // Just version number, no git hash
  lv_label_set_text_fmt(g_version_label, "%d.%d.%lu",
    version_get_major(), version_get_minor(), (unsigned long)version_get_build());
  lv_obj_set_style_text_font(g_version_label, &chalet_ny_14, 0);
  lv_obj_set_style_text_color(g_version_label, lv_color_white(), 0);
  lv_obj_set_style_text_opa(g_version_label, LV_OPA_TRANSP, 0);  // Start invisible
  lv_obj_update_layout(g_version_label);
  lv_obj_set_pos(g_version_label,
    (g_disp_width - lv_obj_get_width(g_version_label)) / 2,
    g_disp_height - lv_obj_get_height(g_version_label) - 10);
  
  // === Speech bubble - comic book style using pre-scaled vector ===
  // Create transparent container to hold bubble art + text
  g_bubble_container = lv_obj_create(g_screen);
  lv_obj_remove_flag(g_bubble_container, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_opa(g_bubble_container, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(g_bubble_container, 0, 0);
  lv_obj_set_style_pad_all(g_bubble_container, 0, 0);
  lv_obj_set_size(g_bubble_container, BUBBLE_WIDTH, BUBBLE_HEIGHT);
  
  // Position: upper right, with tail pointing toward lizard
  lv_obj_set_pos(g_bubble_container, g_disp_width - BUBBLE_WIDTH - 58, 21);
  
  // Create vector art widget for bubble shape (1:1 scale, pre-sized source)
  g_bubble_art = lv_vector_art_create(g_bubble_container);
  lv_obj_set_size(g_bubble_art, BUBBLE_WIDTH, BUBBLE_HEIGHT);
  lv_obj_set_pos(g_bubble_art, 0, 0);
  lv_vector_art_set_scale(g_bubble_art, 1.0f);
  
  if (!lv_vector_art_set_src(g_bubble_art, BUBBLE_VECTOR_PATH)) {
    ESP_LOGE(TAG, "Failed to load bubble vector art");
  }
  
  // Bubble label - positioned in upper portion of bubble (above the tail)
  g_bubble_label = lv_label_create(g_bubble_container);
  lv_label_set_text(g_bubble_label, "LET'S\nGO!");
  lv_obj_set_style_text_font(g_bubble_label, &chalet_ny_8, 0);
  lv_obj_set_style_text_color(g_bubble_label, lv_color_make(0, 180, 220), 0);
  lv_obj_set_style_text_align(g_bubble_label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_update_layout(g_bubble_label);
  // Center horizontally, position in upper half of bubble
  lv_obj_set_pos(g_bubble_label,
    (BUBBLE_WIDTH - lv_obj_get_width(g_bubble_label)) / 2 + 2,
    (BUBBLE_HEIGHT - lv_obj_get_height(g_bubble_label)) / 2 - 1);
  
  // Hide completely until animation starts
  lv_obj_add_flag(g_bubble_container, LV_OBJ_FLAG_HIDDEN);
  
  // Load the screen
  lv_screen_load(g_screen);
  
  // Start all animations
  start_animations();
  
  ESP_LOGI(TAG, "Splash screen created with animations");
  lv_timer_delete(timer);
}

UI_CREATE_DEFERRED_DRAW_FUNC(splash, splash_draw_deferred_cb)

static void splash_teardown(void) {
  // Restore full brightness when leaving splash
  display_set_brightness(100);
  
  if (g_screen) {
    lv_obj_delete(g_screen);
    g_screen = NULL;
    g_content_container = NULL;
    g_vector_art = NULL;
    g_storm_shadow = NULL;
    g_storm_label = NULL;
    g_kaba_shadow = NULL;
    g_kaba_label = NULL;
    g_version_label = NULL;
    g_bubble_container = NULL;
    g_bubble_art = NULL;
    g_bubble_label = NULL;
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
