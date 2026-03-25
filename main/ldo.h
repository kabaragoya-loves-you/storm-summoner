#pragma once

#include "esp_err.h"

// Initialize LDO VO4 in bypass mode for 3.3V GPIO power rail
// Must be called before any GPIO operations
esp_err_t ldo_init(void);
