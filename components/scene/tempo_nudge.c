#include "tempo_nudge.h"

uint16_t tempo_nudge_compute_bpm(int32_t scene_bpm, uint8_t pct, float scale) {
  if (pct > 100) pct = 100;
  if (scale > 1.0f) scale = 1.0f;
  if (scale < -1.0f) scale = -1.0f;

  float factor = 1.0f + scale * ((float)pct / 100.0f);
  int32_t new_bpm = (int32_t)((float)scene_bpm * factor + 0.5f);
  if (new_bpm < 20) new_bpm = 20;
  if (new_bpm > 300) new_bpm = 300;
  return (uint16_t)new_bpm;
}

float tempo_nudge_scale_bipolar(uint8_t midi) {
  return ((float)midi - 64.0f) / 63.0f;
}

float tempo_nudge_scale_abs_bipolar(uint8_t midi) {
  float scale = tempo_nudge_scale_bipolar(midi);
  return (scale < 0.0f) ? -scale : scale;
}

float tempo_nudge_scale_unipolar_low_anchor(uint8_t midi) {
  return (float)midi / 127.0f;
}

float tempo_nudge_scale_unipolar_high_anchor(uint8_t midi) {
  return -((127.0f - (float)midi) / 127.0f);
}

float tempo_nudge_scale_unipolar_span(uint8_t midi) {
  return ((float)midi / 127.0f) * 2.0f - 1.0f;
}

float tempo_nudge_scale_abs_from_mid(uint8_t midi, uint8_t mid) {
  int16_t span = (mid >= 64) ? (int16_t)mid : (int16_t)(127 - mid);
  if (span < 1) span = 1;
  int16_t delta = (int16_t)midi - (int16_t)mid;
  if (delta < 0) delta = -delta;
  return (float)delta / (float)span;
}
