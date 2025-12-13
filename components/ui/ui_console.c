#include "ui_console.h"
#include "ui.h"
#include "ui_module_settings.h"
#include "esp_log.h"
#include "esp_console.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include <stdlib.h>
#include <string.h>

// buttons API (defined in buttons.c)
void buttons_set_planet(const char *name);
const char *buttons_get_planet(void);
void buttons_set_rotation(float speed);
float buttons_get_rotation(void);

static const char* TAG = "ui_console";

static const char* registered_commands[] = {
  "info", "size", "top", "left", "module", "planet", "rotation", "perf", "cpu", 
  "settings", "set"
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
  &pixels_module,
  &pizza_module,
  &slices_module,
  &sphere_module,
  &splash_module,
  &summoner_module,
  &scene_ui_module,
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
  ui_draw_module_t *current_mod = ui_get_current_module();
  bool is_buttons = (current_mod && strcmp(current_mod->name, "buttons") == 0);
  
  if (!is_buttons) {
    printf("Planet command only works for 'buttons' module.\n");
    return 1;
  }
  
  if (argc < 2) {
    const char* current = buttons_get_planet();
    printf("Available planets:\n");
    for (int i = 0; i < num_planets; i++) {
      const char* marker = (current && strcmp(current, planet_textures[i]) == 0) ? " *" : "";
      printf("  %s%s\n", planet_textures[i], marker);
    }
    return 0;
  }
  
  const char* target = argv[1];
  for (int i = 0; i < num_planets; i++) {
    if (strcmp(planet_textures[i], target) == 0) {
      buttons_set_planet(target);
      printf("Planet set to: %s\n", target);
      return 0;
    }
  }
  
  printf("Unknown planet: %s\n", target);
  printf("Use 'planet' with no args to list available planets.\n");
  return 1;
}

// Command: rotation [speed] - animation speed (positive=right, negative=left)
static int cmd_rotation(int argc, char **argv) {
  ui_draw_module_t *current_mod = ui_get_current_module();
  bool is_buttons = (current_mod && strcmp(current_mod->name, "buttons") == 0);
  
  if (!is_buttons) {
    printf("Rotation command only works for 'buttons' module.\n");
    return 1;
  }
  
  if (argc < 2) {
    float current = buttons_get_rotation();
    printf("Rotation speed: %.2f (positive=right, negative=left, try 0.25-1.0)\n", current);
    return 0;
  }
  
  float speed = strtof(argv[1], NULL);
  buttons_set_rotation(speed);
  printf("Rotation speed set to: %.2f\n", speed);
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

// Command: cpu - show per-task CPU usage
static int cmd_cpu(int argc, char **argv) {
  (void)argc;
  (void)argv;
  
#ifdef CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS
  // Get number of tasks
  UBaseType_t num_tasks = uxTaskGetNumberOfTasks();
  
  // Allocate buffer for stats (about 40 chars per task)
  size_t buf_size = num_tasks * 50;
  char *buf = malloc(buf_size);
  if (!buf) {
    printf("Failed to allocate stats buffer\n");
    return 1;
  }
  
  // Get runtime stats
  vTaskGetRunTimeStats(buf);
  printf("Task            Abs Time      %% Time\n");
  printf("-------------------------------------\n");
  printf("%s", buf);
  
  free(buf);
#else
  printf("Enable CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS in menuconfig\n");
#endif
  
  return 0;
}

// Command: perf [show|hide|dump]
static int cmd_perf(int argc, char **argv) {
  // Show heap stats
  size_t free_heap = heap_caps_get_free_size(MALLOC_CAP_DEFAULT);
  size_t min_heap = heap_caps_get_minimum_free_size(MALLOC_CAP_DEFAULT);
  printf("Heap: free=%u min_ever=%u\n", (unsigned)free_heap, (unsigned)min_heap);
  
  // Show LVGL memory stats
  lv_mem_monitor_t mon;
  lv_mem_monitor(&mon);
  printf("LVGL mem: total=%lu used=%lu free=%lu frag=%u%%\n",
         (unsigned long)mon.total_size, (unsigned long)mon.used_cnt,
         (unsigned long)mon.free_cnt, (unsigned)mon.frag_pct);

#if LV_USE_SYSMON && LV_USE_PERF_MONITOR
  if (argc < 2) {
    lv_sysmon_performance_dump(NULL);
    return 0;
  }
  
  const char* action = argv[1];
  if (strcmp(action, "show") == 0) {
    lv_sysmon_show_performance(NULL);
    printf("Performance monitor shown on display\n");
  } else if (strcmp(action, "hide") == 0) {
    lv_sysmon_hide_performance(NULL);
    printf("Performance monitor hidden\n");
  } else if (strcmp(action, "dump") == 0) {
    lv_sysmon_performance_dump(NULL);
  } else {
    printf("Usage: perf [show|hide|dump]\n");
    return 1;
  }
#else
  if (argc >= 2) {
    printf("LVGL sysmon not enabled (LV_USE_SYSMON/LV_USE_PERF_MONITOR)\n");
  }
#endif
  return 0;
}

// Command: settings - List module settings
static int cmd_settings(int argc, char **argv) {
  const char* module_name = NULL;
  
  if (argc >= 2) {
    module_name = argv[1];
  } else {
    // Use current module
    ui_draw_module_t* current = ui_get_current_module();
    if (current) {
      module_name = current->name;
    }
  }
  
  if (!module_name) {
    // List all registered modules with settings
    const char* names[UI_MAX_MODULES_WITH_SETTINGS];
    size_t count = ui_module_list_registered(names, UI_MAX_MODULES_WITH_SETTINGS);
    
    if (count == 0) {
      printf("No modules have registered settings\n");
      return 0;
    }
    
    printf("Modules with settings:\n");
    for (size_t i = 0; i < count; i++) {
      printf("  %s\n", names[i]);
    }
    return 0;
  }
  
  size_t count = 0;
  ui_module_setting_t* settings = ui_module_get_settings(module_name, &count);
  
  if (!settings || count == 0) {
    printf("Module '%s' has no registered settings\n", module_name);
    return 0;
  }
  
  printf("Settings for '%s':\n", module_name);
  for (size_t i = 0; i < count; i++) {
    char value[32];
    ui_module_get_setting_str(module_name, settings[i].name, value, sizeof(value));
    
    const char* type_str;
    switch (settings[i].type) {
      case UI_SETTING_BOOL: type_str = "bool"; break;
      case UI_SETTING_U8: type_str = "u8"; break;
      case UI_SETTING_U16: type_str = "u16"; break;
      case UI_SETTING_FLOAT: type_str = "float"; break;
      default: type_str = "?"; break;
    }
    
    printf("  %-15s = %-10s [%s] %s\n", 
           settings[i].name, value, type_str, 
           settings[i].description ? settings[i].description : "");
  }
  
  return 0;
}

// Command: set - Set a module setting
static int cmd_set(int argc, char **argv) {
  if (argc < 3) {
    printf("Usage: set <module.setting> <value>\n");
    printf("       set <setting> <value>  (uses current module)\n");
    return 1;
  }
  
  const char* setting_spec = argv[1];
  const char* value = argv[2];
  
  // Parse module.setting format
  char module_name[32] = {0};
  char setting_name[32] = {0};
  
  char* dot = strchr(setting_spec, '.');
  if (dot) {
    size_t module_len = dot - setting_spec;
    if (module_len >= sizeof(module_name)) module_len = sizeof(module_name) - 1;
    strncpy(module_name, setting_spec, module_len);
    strncpy(setting_name, dot + 1, sizeof(setting_name) - 1);
  } else {
    // Use current module
    ui_draw_module_t* current = ui_get_current_module();
    if (!current) {
      printf("No current module - specify module.setting\n");
      return 1;
    }
    strncpy(module_name, current->name, sizeof(module_name) - 1);
    strncpy(setting_name, setting_spec, sizeof(setting_name) - 1);
  }
  
  if (!ui_module_set_setting(module_name, setting_name, value)) {
    printf("Failed to set %s.%s = %s\n", module_name, setting_name, value);
    return 1;
  }
  
  printf("Set %s.%s = %s\n", module_name, setting_name, value);
  return 0;
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
  
  // rotation command (for splash5)
  const esp_console_cmd_t rotation_cmd = {
    .command = "rotation",
    .help = "Get/set planet rotation speed (positive=right, negative=left)",
    .hint = "[speed]",
    .func = &cmd_rotation,
  };
  esp_console_cmd_register(&rotation_cmd);
  
  // perf command
  const esp_console_cmd_t perf_cmd = {
    .command = "perf",
    .help = "Show performance stats (perf show/hide for on-screen display)",
    .hint = "[show|hide|dump]",
    .func = &cmd_perf,
  };
  esp_console_cmd_register(&perf_cmd);
  
  // cpu command
  const esp_console_cmd_t cpu_cmd = {
    .command = "cpu",
    .help = "Show per-task CPU usage",
    .hint = NULL,
    .func = &cmd_cpu,
  };
  esp_console_cmd_register(&cpu_cmd);
  
  // settings command
  const esp_console_cmd_t settings_cmd = {
    .command = "settings",
    .help = "List module settings",
    .hint = "[module]",
    .func = &cmd_settings,
  };
  esp_console_cmd_register(&settings_cmd);
  
  // set command
  const esp_console_cmd_t set_cmd = {
    .command = "set",
    .help = "Set a module setting",
    .hint = "<module.setting|setting> <value>",
    .func = &cmd_set,
  };
  esp_console_cmd_register(&set_cmd);
  
  return ESP_OK;
}

void ui_console_cleanup(void) {
  ESP_LOGI(TAG, "Unregistering ui commands");
  
  for (int i = 0; i < num_registered_commands; i++) {
    esp_console_cmd_deregister(registered_commands[i]);
  }
}

