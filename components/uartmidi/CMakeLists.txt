idf_component_register(
  SRCS "uartmidi.c" "midi.c"
  INCLUDE_DIRS "include"
  PRIV_REQUIRES driver log
)