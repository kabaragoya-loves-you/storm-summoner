#ifndef SCENE_TEST_H
#define SCENE_TEST_H

#include <stdint.h>

// Test functions for scene management
void scene_test_info(void);
void scene_test_next(void);
void scene_test_previous(void);
void scene_test_set_cc(uint8_t pad, uint8_t cc, uint8_t value);
void scene_test_demo(void);

// Monitor keyboard handler
void scene_test_monitor_handler(char key);

#endif // SCENE_TEST_H
