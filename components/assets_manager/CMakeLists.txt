idf_component_register(
  SRCS "assets_manager.c" "assets_parser.c" "assets_cache.c" "assets_manager_console.c" "assets_file_ops.c"
  INCLUDE_DIRS "include"
  REQUIRES "joltwallet__littlefs" "json" "console" "mbedtls" "miniz" "ui" "memory_utils"
)

