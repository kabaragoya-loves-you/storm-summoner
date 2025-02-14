#include "display.h"
#include "stars.h"
#include "touch.h"
#include "drv2605_manager.h"
#include "flicker.h"
#include "cv.h"
#include "expression.h"
#include "vcnl4040.h"

#define TAG "main"

void app_main(void) {
  display_init();
  create_starfield();
  touch_init();
  drv2605_init();
  flicker_init();
  cv_init();
  expression_init();
  vcnl4040_init();
}
