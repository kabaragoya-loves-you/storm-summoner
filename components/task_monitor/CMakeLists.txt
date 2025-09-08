idf_component_register(
  SRCS "task_monitor.c"
  INCLUDE_DIRS "include"
  REQUIRES "log" "freertos" "esp_system" "heap"
)
