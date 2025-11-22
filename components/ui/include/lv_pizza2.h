#ifndef LV_PIZZA2_H
#define LV_PIZZA2_H

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Create a pizza2 widget
 * 
 * Creates a widget that draws filled pizza slices with a "bite" taken out
 * of the center. Each slice can be individually enabled/disabled.
 * 
 * @param parent Parent object
 * @return Pointer to the created pizza2 object
 */
lv_obj_t * lv_pizza2_create(lv_obj_t * parent);

/**
 * @brief Set the number of slices
 * @param obj Pizza2 object
 * @param count Number of slices (default 8)
 */
void lv_pizza2_set_slice_count(lv_obj_t * obj, uint8_t count);

/**
 * @brief Set which slices are active (filled)
 * @param obj Pizza2 object
 * @param active_mask Bitmask of active slices (bit 0 = slice 0, etc)
 */
void lv_pizza2_set_active_slices(lv_obj_t * obj, uint32_t active_mask);

/**
 * @brief Set a specific slice active/inactive
 * @param obj Pizza2 object
 * @param slice_index Slice index (0 to slice_count-1)
 * @param active true to fill slice, false to hide it
 */
void lv_pizza2_set_slice_active(lv_obj_t * obj, uint8_t slice_index, bool active);

/**
 * @brief Set the slice fill color (grayscale tone)
 * @param obj Pizza2 object
 * @param gray_tone Grayscale value (0-15, matching 16-tone display)
 */
void lv_pizza2_set_gray_tone(lv_obj_t * obj, uint8_t gray_tone);

/**
 * @brief Set the outer radius of slices
 * @param obj Pizza2 object
 * @param radius Outer radius in pixels
 */
void lv_pizza2_set_radius(lv_obj_t * obj, int32_t radius);

/**
 * @brief Set the bite size (inner radius)
 * @param obj Pizza2 object
 * @param bite_size Inner radius in pixels (0 = no bite, full slice)
 */
void lv_pizza2_set_bite_size(lv_obj_t * obj, int32_t bite_size);

/**
 * @brief Set the slice state provider callback
 * @param obj Pizza2 object
 * @param provider Function that returns slice state based on index
 * @param user_data User data passed to provider function
 */
typedef bool (*lv_pizza2_slice_state_provider_t)(uint8_t slice_index, void * user_data);
void lv_pizza2_set_state_provider(lv_obj_t * obj, lv_pizza2_slice_state_provider_t provider, void * user_data);

#ifdef __cplusplus
}
#endif

#endif // LV_PIZZA2_H
