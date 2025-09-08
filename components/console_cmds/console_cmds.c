#include "console_cmds.h"
#include "task_monitor.h"
#include "esp_console.h"
#include "esp_log.h"
#include "argtable3/argtable3.h"

#define TAG "CONSOLE"

static int cmd_heap(int argc, char **argv) {
  task_monitor_print_heap_info();
  return 0;
}

static int cmd_tasks(int argc, char **argv) {
  task_monitor_print_report();
  return 0;
}

static int cmd_free(int argc, char **argv) {
  printf("Free heap: %u bytes\n", (unsigned)esp_get_free_heap_size());
  printf("Min free heap: %u bytes\n", (unsigned)esp_get_minimum_free_heap_size());
  return 0;
}

void console_cmds_init(void) {
  esp_console_cmd_t heap_cmd = {
    .command = "heap",
    .help = "Show heap memory usage",
    .hint = NULL,
    .func = &cmd_heap,
  };
  ESP_ERROR_CHECK(esp_console_cmd_register(&heap_cmd));
  
  esp_console_cmd_t tasks_cmd = {
    .command = "tasks",
    .help = "Show task stack usage",
    .hint = NULL,
    .func = &cmd_tasks,
  };
  ESP_ERROR_CHECK(esp_console_cmd_register(&tasks_cmd));
  
  esp_console_cmd_t free_cmd = {
    .command = "free",
    .help = "Show free memory",
    .hint = NULL,
    .func = &cmd_free,
  };
  ESP_ERROR_CHECK(esp_console_cmd_register(&free_cmd));
  
  ESP_LOGI(TAG, "Console commands registered (type 'help' for list)");
}
