#ifndef TEMPO_NUDGE_H
#define TEMPO_NUDGE_H

#include <stdint.h>

// Shared tempo nudge scale/BPM helpers used by continuous input handlers.

uint16_t tempo_nudge_compute_bpm(int32_t scene_bpm, uint8_t pct, float scale);

float tempo_nudge_scale_bipolar(uint8_t midi);
float tempo_nudge_scale_abs_bipolar(uint8_t midi);
float tempo_nudge_scale_unipolar_low_anchor(uint8_t midi);
float tempo_nudge_scale_unipolar_high_anchor(uint8_t midi);
float tempo_nudge_scale_unipolar_span(uint8_t midi);
float tempo_nudge_scale_abs_from_mid(uint8_t midi, uint8_t mid);

#endif // TEMPO_NUDGE_H
