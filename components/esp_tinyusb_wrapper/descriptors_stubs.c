/*
 * Stub implementations for descriptor functions when using custom descriptor callbacks.
 * These functions are called by tinyusb_task.c but we provide descriptors via callbacks instead.
 */

#include "esp_err.h"
#include "esp_log.h"
#include "tinyusb.h"

static const char *TAG = "tusb_desc_stub";

/**
 * @brief Check descriptors configuration (stub - always succeeds when using callbacks)
 */
esp_err_t tinyusb_descriptors_check(tinyusb_port_t port, const tinyusb_desc_config_t *config) {
    // When using custom callbacks, descriptor config can be NULL
    ESP_LOGD(TAG, "Using custom descriptor callbacks");
    return ESP_OK;
}

/**
 * @brief Set descriptors (stub - no-op when using callbacks)
 */
esp_err_t tinyusb_descriptors_set(tinyusb_port_t port, const tinyusb_desc_config_t *config) {
    // Descriptors are provided via callbacks (tud_descriptor_*_cb functions)
    ESP_LOGD(TAG, "Descriptors provided via callbacks");
    return ESP_OK;
}

/**
 * @brief Free descriptors (stub - no-op when using callbacks)
 */
void tinyusb_descriptors_free(void) {
    // Nothing to free when using static descriptors via callbacks
    ESP_LOGD(TAG, "No descriptors to free (using callbacks)");
}

