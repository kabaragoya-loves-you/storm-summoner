idf_component_register(
    SRCS
        "miniz.c"
        "miniz_tdef.c"
        "miniz_tinfl.c"
        "miniz_zip.c"
    INCLUDE_DIRS
        "include"           # External: #include "miniz/miniz.h"
        "include/miniz"     # Internal: #include "miniz.h" within component
)

# Suppress warnings from third-party code
target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wno-sign-compare
    -Wno-unused-parameter
    -Wno-unused-function
    -Wno-type-limits
)

# Disable miniz file I/O features we don't use (silences #pragma message)
# NOTE: Do NOT disable ARCHIVE_APIS or ARCHIVE_WRITING_APIS - we need them for ZIP support
target_compile_definitions(${COMPONENT_LIB} PRIVATE
    MINIZ_NO_STDIO=1
)

