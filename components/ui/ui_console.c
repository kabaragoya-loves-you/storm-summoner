#include "ui_console.h"
#include "ui.h"
#include "lv_globe.h"
#include "esp_log.h"
#include "esp_console.h"
#include <stdlib.h>
#include <string.h>

static const char* TAG = "ui_console";

static const char* registered_commands[] = {
  "info", "size", "top", "left", "module", "planet", "ambient", "spin"
};

// Available planet textures
static const char* planet_textures[] = {
  "earth", "moon", "mars", "jupiter"
};
static const int num_planets = sizeof(planet_textures) / sizeof(planet_textures[0]);
static const int num_registered_commands = sizeof(registered_commands) / sizeof(registered_commands[0]);

// Registry of available modules for runtime switching
static ui_draw_module_t* available_modules[] = {
  &boundary_circle_module,
  &buttons_module,
  &draw_lizard_module,
  &kabaragoya_module,
  &pizza_module,
  &pizza2_module,
  &plasma_module,
  &sphere_module,
  &greyscale_test_module,
  &template_module,
};
static const int num_modules = sizeof(available_modules) / sizeof(available_modules[0]);

// Command: info
static int cmd_info(int argc, char **argv) {
  const char* mode_str;
  switch (g_app_mode) {
    case APP_MODE_PERFORMANCE: mode_str = "Performance"; break;
    case APP_MODE_PROGRAMMING: mode_str = "Programming"; break;
    case APP_MODE_SCREENSAVER: mode_str = "Screensaver"; break;
    default: mode_str = "Unknown"; break;
  }
  
  ESP_LOGI(TAG, "====== UI ======");
  ESP_LOGI(TAG, "App mode: %s", mode_str);
  ESP_LOGI(TAG, "Circle: size=%ld, left=%ld, top=%ld",
    boundary_circle_get_size(), boundary_circle_get_left(), boundary_circle_get_top());
  ESP_LOGI(TAG, "================");
  
  return 0;
}

// Command: size <n>
static int cmd_size(int argc, char **argv) {
  if (argc < 2) {
    ESP_LOGI(TAG, "size: %ld", boundary_circle_get_size());
    return 0;
  }
  int32_t val = atoi(argv[1]);
  boundary_circle_set_size(val);
  ESP_LOGI(TAG, "size: %ld", val);
  return 0;
}

// Command: top <n>
static int cmd_top(int argc, char **argv) {
  if (argc < 2) {
    ESP_LOGI(TAG, "top: %ld", boundary_circle_get_top());
    return 0;
  }
  int32_t val = atoi(argv[1]);
  boundary_circle_set_top(val);
  ESP_LOGI(TAG, "top: %ld", val);
  return 0;
}

// Command: left <n>
static int cmd_left(int argc, char **argv) {
  if (argc < 2) {
    ESP_LOGI(TAG, "left: %ld", boundary_circle_get_left());
    return 0;
  }
  int32_t val = atoi(argv[1]);
  boundary_circle_set_left(val);
  ESP_LOGI(TAG, "left: %ld", val);
  return 0;
}

// Command: planet [name]
static int cmd_planet(int argc, char **argv) {
  if (argc < 2) {
    // List planets and show current
    const char* current = lv_globe_get_texture();
    printf("Available planets:\n");
    for (int i = 0; i < num_planets; i++) {
      char path[32];
      snprintf(path, sizeof(path), "A:images/%s.bin", planet_textures[i]);
      const char* marker = (current && strstr(current, planet_textures[i])) ? " *" : "";
      printf("  %s%s\n", planet_textures[i], marker);
    }
    return 0;
  }
  
  // Find and switch to named planet
  const char* target = argv[1];
  for (int i = 0; i < num_planets; i++) {
    if (strcmp(planet_textures[i], target) == 0) {
      char path[32];
      snprintf(path, sizeof(path), "A:images/%s.bin", target);
      lv_globe_set_texture(path);
      printf("Planet set to: %s\n", target);
      return 0;
    }
  }
  
  printf("Unknown planet: %s\n", target);
  printf("Use 'planet' with no args to list available planets.\n");
  return 1;
}

// Command: ambient [0.0-1.0]
static int cmd_ambient(int argc, char **argv) {
  if (argc < 2) {
    printf("Ambient light: %.2f\n", lv_globe_get_ambient_light());
    return 0;
  }
  
  float val = strtof(argv[1], NULL);
  lv_globe_set_ambient_light(val);
  printf("Ambient light set to: %.2f\n", lv_globe_get_ambient_light());
  return 0;
}

// Command: spin [x y z] - rotation speeds in radians/frame
static int cmd_spin(int argc, char **argv) {
  float rx, ry, rz;
  lv_globe_get_global_rotation_speed(&rx, &ry, &rz);
  
  if (argc < 2) {
    printf("Rotation speed: X=%.4f (tilt) Y=%.4f (spin) Z=%.4f (roll)\n", rx, ry, rz);
    return 0;
  }
  
  if (argc == 2) {
    // Single value sets Y (main horizontal spin)
    ry = strtof(argv[1], NULL);
  } else if (argc == 3) {
    // Two values set X and Y
    rx = strtof(argv[1], NULL);
    ry = strtof(argv[2], NULL);
  } else {
    // Three values set all
    rx = strtof(argv[1], NULL);
    ry = strtof(argv[2], NULL);
    rz = strtof(argv[3], NULL);
  }
  
  lv_globe_set_global_rotation_speed(rx, ry, rz);
  printf("Rotation speed set: X=%.4f Y=%.4f Z=%.4f\n", rx, ry, rz);
  return 0;
}

// Command: module [name]
static int cmd_module(int argc, char **argv) {
  ui_draw_module_t* current = ui_get_current_module();
  
  if (argc < 2) {
    // List all modules, highlight current
    printf("Available modules:\n");
    for (int i = 0; i < num_modules; i++) {
      const char* marker = (available_modules[i] == current) ? " *" : "";
      printf("  %s%s\n", available_modules[i]->name, marker);
    }
    return 0;
  }
  
  // Find and switch to named module
  const char* target = argv[1];
  for (int i = 0; i < num_modules; i++) {
    if (strcmp(available_modules[i]->name, target) == 0) {
      ui_set_draw_module(available_modules[i]);
      printf("Switched to module: %s\n", target);
      return 0;
    }
  }
  
  printf("Unknown module: %s\n", target);
  printf("Use 'module' with no args to list available modules.\n");
  return 1;
}

esp_err_t ui_console_init(void) {
  ESP_LOGI(TAG, "Registering ui commands");
  
  // info command
  const esp_console_cmd_t info_cmd = {
    .command = "info",
    .help = "Show UI state",
    .hint = NULL,
    .func = &cmd_info,
  };
  esp_console_cmd_register(&info_cmd);
  
  // size command
  const esp_console_cmd_t size_cmd = {
    .command = "size",
    .help = "Get/set boundary circle diameter",
    .hint = "[n]",
    .func = &cmd_size,
  };
  esp_console_cmd_register(&size_cmd);
  
  // top command
  const esp_console_cmd_t top_cmd = {
    .command = "top",
    .help = "Get/set boundary circle center Y",
    .hint = "[n]",
    .func = &cmd_top,
  };
  esp_console_cmd_register(&top_cmd);
  
  // left command
  const esp_console_cmd_t left_cmd = {
    .command = "left",
    .help = "Get/set boundary circle center X",
    .hint = "[n]",
    .func = &cmd_left,
  };
  esp_console_cmd_register(&left_cmd);
  
  // module command
  const esp_console_cmd_t module_cmd = {
    .command = "module",
    .help = "List or switch UI modules",
    .hint = "[name]",
    .func = &cmd_module,
  };
  esp_console_cmd_register(&module_cmd);
  
  // planet command
  const esp_console_cmd_t planet_cmd = {
    .command = "planet",
    .help = "List or switch planet textures",
    .hint = "[earth|moon|mars|jupiter]",
    .func = &cmd_planet,
  };
  esp_console_cmd_register(&planet_cmd);
  
  // ambient command
  const esp_console_cmd_t ambient_cmd = {
    .command = "ambient",
    .help = "Get/set globe ambient light (0.0-1.0)",
    .hint = "[value]",
    .func = &cmd_ambient,
  };
  esp_console_cmd_register(&ambient_cmd);
  
  // spin command
  const esp_console_cmd_t spin_cmd = {
    .command = "spin",
    .help = "Get/set globe rotation (rad/frame): spin [y] or spin [x y] or spin [x y z]",
    .hint = "[x] [y] [z]",
    .func = &cmd_spin,
  };
  esp_console_cmd_register(&spin_cmd);
  
  return ESP_OK;
}

void ui_console_cleanup(void) {
  ESP_LOGI(TAG, "Unregistering ui commands");
  
  for (int i = 0; i < num_registered_commands; i++) {
    esp_console_cmd_deregister(registered_commands[i]);
  }
}

