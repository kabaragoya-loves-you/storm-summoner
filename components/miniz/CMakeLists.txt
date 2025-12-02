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

