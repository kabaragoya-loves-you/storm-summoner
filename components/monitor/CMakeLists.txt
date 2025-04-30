idf_component_register(
    SRCS "monitor.c"
    INCLUDE_DIRS "include"
    REQUIRES freertos
    PRIV_REQUIRES main
) 