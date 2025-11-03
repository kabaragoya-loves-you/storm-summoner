idf_component_register(
  SRCS "assets_manager.c" "assets_parser.c" "assets_cache.c" "assets_manager_console.c"
  INCLUDE_DIRS "include"
  REQUIRES "joltwallet__littlefs" "json" "console"
)

