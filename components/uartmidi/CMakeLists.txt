idf_component_register(
  SRCS "midi_callbacks.c" "uartmidi_out.c" "midi_messages.c" "uartmidi_in.c" "midi_tempo.c"
  INCLUDE_DIRS "include"
  PRIV_REQUIRES driver log cv analog_input app_settings
)