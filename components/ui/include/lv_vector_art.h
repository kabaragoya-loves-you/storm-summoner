#ifndef LV_VECTOR_ART_H
#define LV_VECTOR_ART_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"
#include <stdbool.h>
#include <stdint.h>

/*********************
 *      DEFINES
 *********************/

#define LV_VECTOR_ART_VERSION_STATIC   1
#define LV_VECTOR_ART_VERSION_ANIMATED 2
#define LV_VECTOR_ART_MAX_SHAPES 32
#define LV_VECTOR_ART_MAX_NAME_LEN 32
#define LV_VECTOR_ART_MAX_FRAMES 64

/**********************
 *      TYPEDEFS
 **********************/

/**
 * Binary file header for static format (14 bytes, version 1)
 */
typedef struct {
  uint16_t version;
  uint16_t width;
  uint16_t height;
  uint16_t shape_count;
  uint16_t reserved;
  uint32_t shape_table_offset;
} __attribute__((packed)) lv_vector_art_header_t;

/**
 * Binary file header for animated format (20 bytes, version 2)
 */
typedef struct {
  uint16_t version;
  uint16_t width;
  uint16_t height;
  uint16_t frame_count;
  uint16_t fps;
  uint32_t reserved;
  uint32_t frame_table_offset;
} __attribute__((packed)) lv_vector_art_anim_header_t;

/**
 * Shape info parsed from binary
 */
typedef struct {
  char name[LV_VECTOR_ART_MAX_NAME_LEN];
  uint8_t r, g, b, a;
  uint16_t point_count;
  lv_point_t *points;  // Pointer into loaded data
  bool visible;
} lv_vector_art_shape_t;

/**
 * Frame data for animation
 */
typedef struct {
  lv_vector_art_shape_t shapes[LV_VECTOR_ART_MAX_SHAPES];
  uint16_t shape_count;
} lv_vector_art_frame_t;

/**
 * Widget data
 */
typedef struct {
  // Static header (also used for version check)
  lv_vector_art_header_t header;
  
  // For static files (version 1)
  lv_vector_art_shape_t shapes[LV_VECTOR_ART_MAX_SHAPES];
  uint16_t shape_count;
  
  // For animated files (version 2)
  lv_vector_art_anim_header_t anim_header;
  lv_vector_art_frame_t *frames;     // Array of frames
  uint16_t frame_count;
  uint16_t current_frame;
  uint16_t fps;
  bool is_animated;
  bool is_playing;
  lv_timer_t *anim_timer;
  
  // Shared data
  uint8_t *raw_data;      // Raw loaded binary data
  size_t raw_data_size;
  void *canvas_buffer;    // Canvas pixel buffer
  size_t canvas_buffer_size;
  float scale;
  int16_t offset_x;
  int16_t offset_y;
  bool loaded;
} lv_vector_art_data_t;

/**********************
 * GLOBAL PROTOTYPES
 **********************/

/**
 * Create a vector art widget
 * @param parent pointer to parent object
 * @return pointer to the created vector art widget
 */
lv_obj_t *lv_vector_art_create(lv_obj_t *parent);

/**
 * Load vector art data from a file path
 * Supports both static (.bin) and animated (.bin) files
 * Also supports compressed .bin.z files
 * @param obj pointer to vector art widget
 * @param path file path (e.g., "/assets/images/logo.bin.z")
 * @return true on success, false on failure
 */
bool lv_vector_art_set_src(lv_obj_t *obj, const char *path);

/**
 * Set visibility of a specific shape by name
 * Useful for animation (e.g., hiding/showing parts)
 * @param obj pointer to vector art widget
 * @param name shape name (from SVG id attribute)
 * @param visible true to show, false to hide
 */
void lv_vector_art_set_shape_visible(lv_obj_t *obj, const char *name, bool visible);

/**
 * Set visibility of a specific shape by index
 * @param obj pointer to vector art widget
 * @param index shape index (0-based)
 * @param visible true to show, false to hide
 */
void lv_vector_art_set_shape_visible_idx(lv_obj_t *obj, uint8_t index, bool visible);

/**
 * Set scale factor for rendering
 * @param obj pointer to vector art widget
 * @param scale scale factor (1.0 = original size)
 */
void lv_vector_art_set_scale(lv_obj_t *obj, float scale);

/**
 * Set offset for rendering (useful for centering)
 * @param obj pointer to vector art widget
 * @param x horizontal offset
 * @param y vertical offset
 */
void lv_vector_art_set_offset(lv_obj_t *obj, int16_t x, int16_t y);

/**
 * Get the original width from the loaded file
 * @param obj pointer to vector art widget
 * @return original width, or 0 if not loaded
 */
uint16_t lv_vector_art_get_width(lv_obj_t *obj);

/**
 * Get the original height from the loaded file
 * @param obj pointer to vector art widget
 * @return original height, or 0 if not loaded
 */
uint16_t lv_vector_art_get_height(lv_obj_t *obj);

/**
 * Get number of shapes in loaded file
 * For animated files, returns shapes in current frame
 * @param obj pointer to vector art widget
 * @return shape count, or 0 if not loaded
 */
uint16_t lv_vector_art_get_shape_count(lv_obj_t *obj);

/**
 * Get shape name by index
 * @param obj pointer to vector art widget
 * @param index shape index
 * @return shape name, or NULL if invalid
 */
const char *lv_vector_art_get_shape_name(lv_obj_t *obj, uint8_t index);

/**
 * Force re-render of the vector art
 * Call this after changing widget size
 * @param obj pointer to vector art widget
 */
void lv_vector_art_invalidate(lv_obj_t *obj);

/**********************
 * ANIMATION FUNCTIONS
 **********************/

/**
 * Check if the loaded file is animated
 * @param obj pointer to vector art widget
 * @return true if animated, false if static
 */
bool lv_vector_art_is_animated(lv_obj_t *obj);

/**
 * Get the number of frames in an animated file
 * @param obj pointer to vector art widget
 * @return frame count, or 0 if static/not loaded
 */
uint16_t lv_vector_art_get_frame_count(lv_obj_t *obj);

/**
 * Get the current frame index
 * @param obj pointer to vector art widget
 * @return current frame index (0-based)
 */
uint16_t lv_vector_art_get_current_frame(lv_obj_t *obj);

/**
 * Set the current frame index (does not affect playback)
 * @param obj pointer to vector art widget
 * @param frame frame index (0-based)
 */
void lv_vector_art_set_frame(lv_obj_t *obj, uint16_t frame);

/**
 * Set the playback FPS (frames per second)
 * @param obj pointer to vector art widget
 * @param fps frames per second (1-60)
 */
void lv_vector_art_set_fps(lv_obj_t *obj, uint16_t fps);

/**
 * Get the current FPS setting
 * @param obj pointer to vector art widget
 * @return current FPS
 */
uint16_t lv_vector_art_get_fps(lv_obj_t *obj);

/**
 * Start playing the animation
 * @param obj pointer to vector art widget
 */
void lv_vector_art_play(lv_obj_t *obj);

/**
 * Pause the animation
 * @param obj pointer to vector art widget
 */
void lv_vector_art_pause(lv_obj_t *obj);

/**
 * Check if animation is currently playing
 * @param obj pointer to vector art widget
 * @return true if playing, false if paused
 */
bool lv_vector_art_is_playing(lv_obj_t *obj);

#ifdef __cplusplus
}
#endif

#endif /* LV_VECTOR_ART_H */
