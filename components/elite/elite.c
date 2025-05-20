/*
 * Code based on https://github.com/OkuboHeavyIndustries/SSD1306_3D_ELITE_SHIP_VIEWER/
 */

#include "lvgl.h"  // Updated path to use ESP-IDF's component include path
#include "elite.h"
#include "elite_config.h"
#include "ships.h"
#include "esp_random.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <math.h>
#include "flyer_venice_14.h"

#define TAG "ELITE"

// Global variables
static float rotx, roty, rotz, rotxx, rotyy, rotzz, rotxxx, rotyyy, rotzzz;
static int wireframe[43][3];
static int scale = 1;

static int ship_vertices_cnt, ship_faces_cnt;
static int ship_vertices[43][3];
static int ship_faces[23][9];

// Center coordinates for display
static const int originx = ELITE_DISPLAY_WIDTH / 2;
static const int originy = ELITE_DISPLAY_HEIGHT / 2;

static float vector;
static float scalefactor = 0;

// Ship info string
static char ShipName[32] = "";

// LVGL objects
static lv_obj_t *canvas;
static lv_obj_t *info_label;
static lv_timer_t *rotation_timer;
static lv_timer_t *g_ship_cycling_timer = NULL; // For cycling ships
static lv_style_t style_default;
static bool g_elite_style_initialized = false;

// Drawing buffer for canvas
static lv_color_t canvas_buf[ELITE_DISPLAY_WIDTH * ELITE_DISPLAY_HEIGHT];

// Forward declarations
static void next_random_ship(lv_timer_t *timer);
static void draw_wireframe_ship(void);
static void rotate_ship_cb(lv_timer_t *timer);
static void cleanup_ship_resources(void);

static void draw_wireframe_ship(void) {
    // Clear the buffer with black
    for(int i = 0; i < ELITE_DISPLAY_WIDTH * ELITE_DISPLAY_HEIGHT; i++) {
        canvas_buf[i] = lv_color_make(0, 0, 0);
    }
    
    int face, f_line, wf_f_1, wf_f_2;
    lv_layer_t layer;
    lv_canvas_init_layer(canvas, &layer);

    for (face = 0; face < ship_faces_cnt; face++) {
        vector = 0;
        for (f_line = 1; f_line < ship_faces[face][0]; f_line++) {
            wf_f_1 = ship_faces[face][f_line];
            wf_f_2 = ship_faces[face][f_line + 1];
            vector += wireframe[wf_f_1][0] * wireframe[wf_f_2][1] - 
                 wireframe[wf_f_1][1] * wireframe[wf_f_2][0];
        }
        wf_f_1 = ship_faces[face][f_line];
        wf_f_2 = ship_faces[face][1];
        vector += wireframe[wf_f_1][0] * wireframe[wf_f_2][1] - 
             wireframe[wf_f_1][1] * wireframe[wf_f_2][0];

        if (vector >= 0) {
            for (f_line = 1; f_line < ship_faces[face][0]; f_line++) {
                wf_f_1 = ship_faces[face][f_line];
                wf_f_2 = ship_faces[face][f_line + 1];
                
                // Ensure coordinates are within canvas bounds
                int x1 = wireframe[wf_f_1][0];
                int y1 = wireframe[wf_f_1][1];
                int x2 = wireframe[wf_f_2][0];
                int y2 = wireframe[wf_f_2][1];
                
                // Clip coordinates to canvas boundaries
                x1 = LV_CLAMP(0, x1, ELITE_DISPLAY_WIDTH - 1);
                y1 = LV_CLAMP(0, y1, ELITE_DISPLAY_HEIGHT - 1);
                x2 = LV_CLAMP(0, x2, ELITE_DISPLAY_WIDTH - 1);
                y2 = LV_CLAMP(0, y2, ELITE_DISPLAY_HEIGHT - 1);
                
                lv_draw_line_dsc_t line_dsc;
                lv_draw_line_dsc_init(&line_dsc);
                line_dsc.color = lv_color_white();
                line_dsc.width = 1;
                line_dsc.opa = LV_OPA_COVER;
                
                line_dsc.p1.x = x1;
                line_dsc.p1.y = y1;
                line_dsc.p2.x = x2;
                line_dsc.p2.y = y2;
                
                lv_draw_line(&layer, &line_dsc);
            }
            
            // Draw closing line with boundary checking
            wf_f_1 = ship_faces[face][f_line];
            wf_f_2 = ship_faces[face][1];
            
            int x1 = wireframe[wf_f_1][0];
            int y1 = wireframe[wf_f_1][1];
            int x2 = wireframe[wf_f_2][0];
            int y2 = wireframe[wf_f_2][1];
            
            x1 = LV_CLAMP(0, x1, ELITE_DISPLAY_WIDTH - 1);
            y1 = LV_CLAMP(0, y1, ELITE_DISPLAY_HEIGHT - 1);
            x2 = LV_CLAMP(0, x2, ELITE_DISPLAY_WIDTH - 1);
            y2 = LV_CLAMP(0, y2, ELITE_DISPLAY_HEIGHT - 1);
            
            lv_draw_line_dsc_t line_dsc;
            lv_draw_line_dsc_init(&line_dsc);
            line_dsc.color = lv_color_white();
            line_dsc.width = 1;
            line_dsc.opa = LV_OPA_COVER;
            
            line_dsc.p1.x = x1;
            line_dsc.p1.y = y1;
            line_dsc.p2.x = x2;
            line_dsc.p2.y = y2;
            
            lv_draw_line(&layer, &line_dsc);
        }
    }
    
    lv_canvas_finish_layer(canvas, &layer);
}

static void rotate_ship_cb(lv_timer_t *timer) {
    static int angle = 0;
    static int rotation_pattern = -1;  // -1 means we need to pick a new pattern
    
    // Pick a new rotation pattern when we start with a new ship
    if (rotation_pattern == -1) {
        rotation_pattern = (esp_random() % 2);
    }
    
    // Clear the buffer with black
    for(int i = 0; i < ELITE_DISPLAY_WIDTH * ELITE_DISPLAY_HEIGHT; i++) {
        canvas_buf[i] = lv_color_make(0, 0, 0);
    }

    for (int i = 0; i < ship_vertices_cnt; i++) {
        float rot = angle * 0.0174532; // 0.0174532 = one degree
        
        if (rotation_pattern == 0) {
            // Standard rotation
            rotz = ship_vertices[i][2] / scale * cos(rot) - 
                 ship_vertices[i][0] / scale * sin(rot);
            rotx = ship_vertices[i][2] / scale * sin(rot) + 
                 ship_vertices[i][0] / scale * cos(rot);
            roty = ship_vertices[i][1] / scale;
        } else {
            // Alternative rotation
            rotx = ship_vertices[i][2] / scale * cos(rot) + 
                 ship_vertices[i][0] / scale * sin(rot);
            rotz = ship_vertices[i][2] / scale * sin(rot) - 
                 ship_vertices[i][0] / scale * cos(rot);
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
    
    if (scalefactor < 1) {
        scalefactor += 0.01;
    }
    
    angle += 3;
    if (angle >= 360) {
        angle = 0;
    }

    lv_label_set_text(info_label, ShipName);
}

static void cleanup_ship_resources(void) {
    // Delete rotation timer if it exists
    if (rotation_timer) {
        lv_timer_del(rotation_timer);
        rotation_timer = NULL;
    }
    // Delete ship cycling timer
    if (g_ship_cycling_timer) {
        lv_timer_del(g_ship_cycling_timer);
        g_ship_cycling_timer = NULL;
    }
    
    // Delete canvas if it exists
    if (canvas) {
        lv_obj_del(canvas);
        canvas = NULL;
    }
    
    // Delete info label if it exists
    if (info_label) {
        lv_obj_del(info_label);
        info_label = NULL;
    }
}

void display_ship(const char* name, int* vertices, int vert_cnt, int vert_scale, int* faces, int face_cnt) {
    // Clean up previous ship's resources (primarily rotation_timer, canvas, info_label)
    // Note: cleanup_ship_resources() is now more comprehensive.
    // If display_ship is called multiple times by next_random_ship, we need to be careful.
    // The current cleanup_ship_resources call here will delete the ship cycling timer if it's called internally.
    // This needs to be handled: display_ship should only clean up *its* specific resources (canvas, label, rotation_timer).
    
    // Simplified cleanup for display_ship (specific to a single ship's display elements)
    if (rotation_timer) {
        lv_timer_del(rotation_timer);
        rotation_timer = NULL;
    }
    if (canvas) {
        lv_obj_del(canvas);
        canvas = NULL;
    }
    if (info_label) {
        lv_obj_del(info_label);
        info_label = NULL;
    }

    // Update ship name
    strcpy(ShipName, name);
    
    // Copy ship data
    memcpy(ship_vertices, vertices, sizeof(int) * vert_cnt * 3);
    ship_vertices_cnt = vert_cnt;
    scale = vert_scale;
    memcpy(ship_faces, faces, sizeof(int) * face_cnt * 9);
    ship_faces_cnt = face_cnt;
    
    // Reset animation variables
    scalefactor = 0;
    
    // Create canvas for ship drawing
    canvas = lv_canvas_create(lv_scr_act());
    
    // Set canvas size and position - ensure it's exactly screen size
    lv_obj_remove_style_all(canvas);  // Remove any default padding/margins
    lv_obj_set_size(canvas, ELITE_DISPLAY_WIDTH, ELITE_DISPLAY_HEIGHT);
    lv_obj_align(canvas, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_pad_all(canvas, 0, 0);  // No padding
    
    // Initialize the canvas buffer with black
    for(int i = 0; i < ELITE_DISPLAY_WIDTH * ELITE_DISPLAY_HEIGHT; i++) {
        canvas_buf[i] = lv_color_make(0, 0, 0);
    }
    
    // Set up the canvas buffer
    lv_canvas_set_buffer(canvas, canvas_buf, ELITE_DISPLAY_WIDTH, ELITE_DISPLAY_HEIGHT, LV_COLOR_FORMAT_RGB565);
    
    // Create info label and set text immediately - centered and shifted down
    info_label = lv_label_create(lv_scr_act());
    lv_obj_align(info_label, LV_ALIGN_TOP_MID, 0, 20);
    lv_obj_add_style(info_label, &style_default, 0);
    lv_label_set_text(info_label, ShipName);
    
    // Start rotation animation with pattern selection reset
    rotation_timer = lv_timer_create(rotate_ship_cb, ELITE_ROTATION_INTERVAL_MS, NULL);
}

void elite_init(void) {  
    // Initialize random number generator - this is a one-time setup
    esp_random();
    ESP_LOGI(TAG, "Elite component initialized (random seeded).");
    // Style initialization moved to elite_start to ensure it happens before object creation if elite is stopped/started multiple times.
    // Initial ship display and timers moved to elite_start.
}

void elite_start(void) {
    ESP_LOGI(TAG, "Starting Elite screensaver...");

    if (!g_elite_style_initialized) {
        lv_style_init(&style_default);
        lv_style_set_text_font(&style_default, &flyer_venice_14);  // Use the new font
        lv_style_set_text_color(&style_default, lv_color_white());
        g_elite_style_initialized = true;
        ESP_LOGI(TAG, "Elite style initialized.");
    }

    // Clear screen to black first - ensure this is effective.
    // The screen is lv_scr_act(), and canvas is created on top.
    // This might be better done by ensuring the canvas itself covers the screen and is black.
    lv_obj_t *screen = lv_scr_act();
    lv_obj_set_style_bg_color(screen, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    
    // Initialize canvas buffer to black - good practice.
    for(int i = 0; i < ELITE_DISPLAY_WIDTH * ELITE_DISPLAY_HEIGHT; i++) {
        canvas_buf[i] = lv_color_make(0, 0, 0);
    }

    // Start with a random ship
    // next_random_ship will call display_ship, which creates canvas, label, and rotation_timer.
    next_random_ship(NULL); 
    
    // Start ship cycling timer if not already running
    if (g_ship_cycling_timer == NULL) {
        g_ship_cycling_timer = lv_timer_create(next_random_ship, ELITE_SHIP_CHANGE_INTERVAL_MS, NULL);
        if (g_ship_cycling_timer == NULL) {
            ESP_LOGE(TAG, "Failed to create ship cycling timer!");
            // Cleanup what was created by next_random_ship if timer fails
            cleanup_ship_resources(); // This will clear canvas, label, rotation_timer created by next_random_ship
            return;
        }
        ESP_LOGI(TAG, "Elite ship cycling timer started.");
    } else {
        lv_timer_resume(g_ship_cycling_timer); // Or lv_timer_reset if preferred
    }
    ESP_LOGI(TAG, "Elite screensaver started.");
}

void elite_stop(void) {
    ESP_LOGI(TAG, "Stopping Elite screensaver...");
    cleanup_ship_resources(); // This now stops rotation_timer, g_ship_cycling_timer, and deletes canvas, info_label.
    ESP_LOGI(TAG, "Elite screensaver stopped and resources cleaned up.");
}

static void next_random_ship(lv_timer_t *timer) {
  // (void)timer; // Mark parameter as intentionally unused - but it is used by lv_timer_create
  LV_UNUSED(timer); // More standard LVGL way
  
  ESP_LOGD(TAG, "Selecting next random ship.");
  // display_ship itself handles cleaning previous ship's canvas/label/timer if called repeatedly.
  // No, display_ship was modified to only clean its own resources.
  // So, if next_random_ship is called, it implicitly means we are switching.
  // The old canvas/label/rotation_timer from the *previous* ship should be gone.
  // This is handled by display_ship's internal cleanup at its start.

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
