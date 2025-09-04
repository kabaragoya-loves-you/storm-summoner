idf_component_register(
  SRCS "midi_callbacks.c" "midi_out.c" "midi_messages.c" "midi_in.c" "midi_tempo.c"
  INCLUDE_DIRS "include"
  REQUIRES driver log cv analog_input app_settings event_bus
  PRIV_REQUIRES main
)