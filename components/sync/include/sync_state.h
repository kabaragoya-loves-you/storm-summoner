#ifndef _SYNC_STATE_H
#define _SYNC_STATE_H

#include <stdint.h>
#include <stdbool.h>
#include "transport.h"
#include "tempo.h"

typedef enum {
  SYNC_CLOCK_QUALITY_LOST = 0,
  SYNC_CLOCK_QUALITY_DEGRADED,
  SYNC_CLOCK_QUALITY_FREEWHEEL,
  SYNC_CLOCK_QUALITY_LOCKED
} sync_clock_quality_t;

typedef enum {
  SYNC_SOURCE_INTERNAL = 0,
  SYNC_SOURCE_MIDI_CLOCK,
  SYNC_SOURCE_ANALOG_SYNC,
  SYNC_SOURCE_COUNT
} sync_clock_source_id_t;

typedef struct {
  transport_state_t state;
  transport_source_t source;
  uint8_t is_resume;
  uint8_t is_fresh_start;
} sync_transport_state_t;

typedef struct {
  uint16_t bpm_x10;
  uint8_t ppq_tick;
  uint8_t beat_in_bar;
  uint32_t bar;
  uint8_t ts_numerator;
  uint8_t ts_denominator;
} sync_musical_clock_t;

typedef struct {
  uint16_t spp_sixteenths;
  uint16_t quarter_notes;
} sync_song_position_t;

typedef struct {
  uint8_t hours;
  uint8_t minutes;
  uint8_t seconds;
  uint8_t frames;
  uint8_t frame_rate;
  bool drop_frame;
  int8_t offset_frames;
  bool valid;
} sync_timecode_t;

typedef struct {
  sync_clock_quality_t internal;
  sync_clock_quality_t midi_clock;
  sync_clock_quality_t analog_sync;
  sync_clock_source_id_t active_musical_source;
} sync_clock_quality_state_t;

typedef struct {
  int16_t output_offset_ms;
  int16_t input_offset_ms;
} sync_latency_t;

typedef struct {
  sync_transport_state_t transport;
  sync_musical_clock_t musical;
  sync_song_position_t song;
  sync_timecode_t timecode;
  sync_clock_quality_state_t quality;
  sync_latency_t latency;
  uint32_t revision;
} sync_state_t;

const char *sync_clock_quality_str(sync_clock_quality_t quality);
const char *sync_clock_source_str(sync_clock_source_id_t source);

#endif /* _SYNC_STATE_H */
