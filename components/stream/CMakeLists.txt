idf_component_register(
  SRCS "stream.c"
  INCLUDE_DIRS "include"
  REQUIRES event_bus transport lfo rtg sample_hold lis3dhtr scene midi expression cv
)
