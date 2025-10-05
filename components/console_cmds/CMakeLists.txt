idf_component_register(
  SRCS "console_cmds.c"
  INCLUDE_DIRS "include"
  REQUIRES console task_monitor log scene midi
)
