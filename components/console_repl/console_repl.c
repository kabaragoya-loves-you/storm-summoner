#include "console_repl.h"
#include "scene_console.h"
#include "console_completion.h"
#include "config_console.h"
#include "device_config_console.h"
#include "app_settings_console.h"
#include "revision_console.h"
#include "i2c_common_console.h"
#include "input_manager_console.h"
#include "buttons_console.h"
#include "touch_console.h"
#include "switch_console.h"
#include "bump_console.h"
#include "sensor_console.h"
#include "expression_console.h"
#include "cv_console.h"
#include "dac_console.h"
#include "adc_manager_console.h"
#include "midi_console.h"
#include "midi_send_console.h"
#include "tempo_console.h"
#include "transport_console.h"
#include "clock_sync_console.h"
#include "haptic_console.h"
#include "display_console.h"
#include "ui_console.h"
#include "version_console.h"
#include "screensaver_console.h"
#include "assets_manager_console.h"
#include "firmware_update_console.h"
#include "event_bus_console.h"
#include "lfo_console.h"
#include "esp_console.h"
#include "esp_vfs_dev.h"
#include "driver/uart.h"
#include "linenoise/linenoise.h"
#include "argtable3/argtable3.h"
#include "esp_log.h"
#include "esp_system.h"
#include <string.h>

static const char* TAG = "console_repl";

#define MAX_CONTEXTS 32

// Context registry
typedef struct {
  char name[32];
  context_init_fn init_fn;
  context_cleanup_fn cleanup_fn;
  bool active;
} context_info_t;

static struct {
  context_info_t contexts[MAX_CONTEXTS];
  int context_count;
  int current_context;  // -1 = root, else index into contexts
  char prompt[64];
} g_console_state = {
  .context_count = 0,
  .current_context = -1,
  .prompt = ">"
};

static esp_console_repl_t *g_repl = NULL;

// Update prompt based on current context
static void update_prompt(void) {
  if (g_console_state.current_context == -1) {
    strncpy(g_console_state.prompt, ">", sizeof(g_console_state.prompt) - 1);
  } else {
    snprintf(g_console_state.prompt, sizeof(g_console_state.prompt), "%s>", 
             g_console_state.contexts[g_console_state.current_context].name);
  }
}

// Command: cd <context> - Enter a context
static struct {
  struct arg_str *context_name;
  struct arg_end *end;
} cd_args;

static int cmd_cd(int argc, char **argv) {
  // cd with no args or cd / = go to root
  if (argc == 1 || (argc == 2 && strcmp(argv[1], "/") == 0)) {
    if (g_console_state.current_context >= 0) {
      // Cleanup current context
      context_info_t* ctx = &g_console_state.contexts[g_console_state.current_context];
      if (ctx->cleanup_fn) ctx->cleanup_fn();
      ctx->active = false;
    }
    g_console_state.current_context = -1;
    update_prompt();
    ESP_LOGI(TAG, "Returned to root");
    return 0;
  }
  
  // cd .. = go up one level (same as root for now)
  if (argc == 2 && strcmp(argv[1], "..") == 0) {
    if (g_console_state.current_context >= 0) {
      context_info_t* ctx = &g_console_state.contexts[g_console_state.current_context];
      if (ctx->cleanup_fn) ctx->cleanup_fn();
      ctx->active = false;
    }
    g_console_state.current_context = -1;
    update_prompt();
    ESP_LOGI(TAG, "Returned to root");
    return 0;
  }
  
  // cd <context> = enter context
  int nerrors = arg_parse(argc, argv, (void **) &cd_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, cd_args.end, argv[0]);
    return 1;
  }
  
  const char* name = cd_args.context_name->sval[0];
  
  // Find and enter context
  for (int i = 0; i < g_console_state.context_count; i++) {
    if (strcmp(g_console_state.contexts[i].name, name) == 0) {
      // Cleanup previous context if any
      if (g_console_state.current_context >= 0) {
        context_info_t* old_ctx = &g_console_state.contexts[g_console_state.current_context];
        if (old_ctx->cleanup_fn) old_ctx->cleanup_fn();
        old_ctx->active = false;
      }
      
      // Enter new context
      g_console_state.current_context = i;
      context_info_t* new_ctx = &g_console_state.contexts[i];
      if (new_ctx->init_fn) {
        esp_err_t ret = new_ctx->init_fn();
        if (ret != ESP_OK) {
          ESP_LOGE(TAG, "Failed to initialize context '%s': %s", name, esp_err_to_name(ret));
          g_console_state.current_context = -1;
          return 1;
        }
      }
      new_ctx->active = true;
      update_prompt();
      ESP_LOGI(TAG, "Entered context: %s (type 'help' for commands, 'cd ..' to exit)", name);
      return 0;
    }
  }
  
  ESP_LOGE(TAG, "Unknown context: %s", name);
  return 1;
}

// Command: contexts - List available contexts
static int cmd_contexts(int argc, char **argv) {
  printf("Available contexts:\r\n");
  for (int i = 0; i < g_console_state.context_count; i++) {
    printf("  %s%s\r\n", g_console_state.contexts[i].name,
           g_console_state.contexts[i].active ? " (active)" : "");
  }
  printf("Use 'cd <context>' to enter, 'cd ..' to exit\r\n");
  return 0;
}

// Command: reset - Restart the device
static int cmd_reset(int argc, char **argv) {
  printf("Restarting...\r\n");
  vTaskDelay(pdMS_TO_TICKS(100)); // Brief delay for output to flush
  esp_restart();
  return 0; // Never reached
}

esp_err_t console_register_context(const char* name, context_init_fn init_fn, context_cleanup_fn cleanup_fn) {
  if (g_console_state.context_count >= MAX_CONTEXTS) {
    ESP_LOGE(TAG, "Maximum contexts reached");
    return ESP_ERR_NO_MEM;
  }
  
  if (!name || !init_fn) {
    return ESP_ERR_INVALID_ARG;
  }
  
  context_info_t* ctx = &g_console_state.contexts[g_console_state.context_count];
  strncpy(ctx->name, name, sizeof(ctx->name) - 1);
  ctx->name[sizeof(ctx->name) - 1] = '\0';
  ctx->init_fn = init_fn;
  ctx->cleanup_fn = cleanup_fn;
  ctx->active = false;
  
  g_console_state.context_count++;
  
  ESP_LOGD(TAG, "Registered context: %s", name);
  return ESP_OK;
}

esp_err_t console_repl_init(void) {
  ESP_LOGI(TAG, "Initializing console REPL");
  
  // Initialize console REPL for interactive commands
  esp_console_repl_config_t repl_config = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
  repl_config.prompt = g_console_state.prompt;
  repl_config.max_cmdline_length = 256;
  
  esp_console_dev_usb_serial_jtag_config_t usb_config = ESP_CONSOLE_DEV_USB_SERIAL_JTAG_CONFIG_DEFAULT();
  esp_err_t ret = esp_console_new_repl_usb_serial_jtag(&usb_config, &repl_config, &g_repl);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to create REPL: %s", esp_err_to_name(ret));
    return ret;
  }
  
  // Register help command
  ESP_ERROR_CHECK(esp_console_register_help_command());
  
  // Register cd command
  cd_args.context_name = arg_str0(NULL, NULL, "<context>", "Context to enter (or '..' for parent, '/' for root)");
  cd_args.end = arg_end(2);
  
  const esp_console_cmd_t cd_cmd = {
    .command = "cd",
    .help = "Change context (like cd in shell)",
    .hint = NULL,
    .func = &cmd_cd,
    .argtable = &cd_args
  };
  ESP_ERROR_CHECK(esp_console_cmd_register(&cd_cmd));
  
  // Register contexts command
  const esp_console_cmd_t contexts_cmd = {
    .command = "contexts",
    .help = "List available contexts",
    .hint = NULL,
    .func = &cmd_contexts,
  };
  ESP_ERROR_CHECK(esp_console_cmd_register(&contexts_cmd));
  
  // Register reset command
  const esp_console_cmd_t reset_cmd = {
    .command = "reset",
    .help = "Restart the device",
    .hint = NULL,
    .func = &cmd_reset,
  };
  ESP_ERROR_CHECK(esp_console_cmd_register(&reset_cmd));
  
  // Initialize completion system
  console_completion_init();
  
  // Register global MIDI send command
  midi_send_console_register();
  
  // Register all contexts
  console_register_context("config", config_console_init, config_console_cleanup);
  console_register_context("scene", scene_console_init, scene_console_cleanup);
  console_register_context("device_config", device_config_console_init, device_config_console_cleanup);
  console_register_context("app_settings", app_settings_console_init, app_settings_console_cleanup);
  console_register_context("revision", revision_console_init, revision_console_cleanup);
  console_register_context("i2c", i2c_common_console_init, i2c_common_console_cleanup);
  console_register_context("input_manager", input_manager_console_init, input_manager_console_cleanup);
  console_register_context("buttons", buttons_console_init, buttons_console_cleanup);
  console_register_context("touch", touch_console_init, touch_console_cleanup);
  console_register_context("switch", switch_console_init, switch_console_cleanup);
  console_register_context("bump", bump_console_init, bump_console_cleanup);
  console_register_context("sensor", sensor_console_init, sensor_console_cleanup);
  console_register_context("expression", expression_console_init, expression_console_cleanup);
  console_register_context("cv", cv_console_init, cv_console_cleanup);
  console_register_context("dac", dac_console_init, dac_console_cleanup);
  console_register_context("adc_manager", adc_manager_console_init, adc_manager_console_cleanup);
  console_register_context("midi", midi_console_init, midi_console_cleanup);
  console_register_context("tempo", tempo_console_init, tempo_console_cleanup);
  console_register_context("transport", transport_console_init, transport_console_cleanup);
  console_register_context("clock_sync", clock_sync_console_init, clock_sync_console_cleanup);
  // Note: LED commands are now part of the tempo context
  console_register_context("haptic", haptic_console_init, haptic_console_cleanup);
  console_register_context("display", display_console_init, display_console_cleanup);
  console_register_context("ui", ui_console_init, ui_console_cleanup);
  console_register_context("screensaver", screensaver_console_init, screensaver_console_cleanup);
  console_register_context("assets_manager", assets_manager_console_init, assets_manager_console_cleanup);
  console_register_context("firmware_update", firmware_update_console_init, firmware_update_console_cleanup);
  console_register_context("event_bus", event_bus_console_init, event_bus_console_cleanup);
  console_register_context("version", version_console_init, version_console_cleanup);
  console_register_context("lfo", lfo_console_init, lfo_console_cleanup);
  
  // Start the REPL
  ret = esp_console_start_repl(g_repl);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to start REPL: %s", esp_err_to_name(ret));
    return ret;
  }
  
  ESP_LOGI(TAG, "Console REPL started. Type 'contexts' to see available contexts, 'help' for commands.");
  
  return ESP_OK;
}

