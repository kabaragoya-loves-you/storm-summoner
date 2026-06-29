#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "app_settings.h"
#include "event_bus.h"
#include "tilt.h"
#include "lis3dhtr_internal.h"
#include <string.h>
#include <stdlib.h>

#define TAG "TILT"

// NVS keys (u16 raw accel values are signed; we store the int16 bitpattern in u16).
#define NVS_TILT_CAL_CX   "tilt_cal_cx"
#define NVS_TILT_CAL_CY   "tilt_cal_cy"
#define NVS_TILT_CAL_LX   "tilt_cal_lx"
#define NVS_TILT_CAL_RX   "tilt_cal_rx"
#define NVS_TILT_CAL_FY   "tilt_cal_fy"
#define NVS_TILT_CAL_BY   "tilt_cal_by"
#define NVS_TILT_FORGIVE  "tilt_forgive"
#define NVS_TILT_MID_W    "tilt_mid_w"
#define NVS_TILT_DZ       "tilt_dz"
#define NVS_TILT_RATE     "tilt_rate"
#define NVS_TILT_CALIBRATED "tilt_calibrated"
#define NVS_TILT_INV_X    "tilt_inv_x"
#define NVS_TILT_INV_Y    "tilt_inv_y"
#define NVS_TILT_NOTE_OFF "tilt_note_off"

// Default controller numbers used in emitted EVENT_SENSOR_TILT_* events.
// Scenes may override via their own continuous_mapping.cc_number.
#define DEFAULT_CC_TILT_X 20
#define DEFAULT_CC_TILT_Y 21

#define DEFAULT_MIDDLE_WIDTH 8
#define DEFAULT_DEADZONE     1
#define DEFAULT_RATE_HZ      40
#define MIN_RATE_HZ          10
#define MAX_RATE_HZ          100

// Minimum acceptable swing (raw LSB) between center and extent for cal validation.
#define MIN_EXTENT_SWING 80

typedef struct {
  int16_t center;
  int16_t neg_extent;
  int16_t pos_extent;
  int16_t last_raw;
  uint8_t last_midi_sent;
  float   filt;  // IIR state (midi-domain)
  bool    enabled;
  bool    inverted;  // Global polarity: flip direction->value mapping
  uint8_t cc_number;
  // Pending calibration captures
  int16_t cap_center;
  int16_t cap_neg;
  int16_t cap_pos;
} axis_state_t;

static axis_state_t s_ax[2];

static bool    s_calibrated = false;
static bool    s_forgive = true;
static uint8_t s_middle_width = DEFAULT_MIDDLE_WIDTH;
static uint8_t s_deadzone = DEFAULT_DEADZONE;
static uint8_t s_rate_hz = DEFAULT_RATE_HZ;
static tilt_note_off_mode_t s_note_off_mode = TILT_NOTE_OFF_OFF;
static bool    s_cal_in_progress = false;
static bool    s_cal_steps_done[TILT_CAL_NUM_STEPS];

static int16_t s_last_sample_x = 0;
static int16_t s_last_sample_y = 0;

static void load_nvs(void) {
  uint16_t u;
  uint8_t  b;

  if (app_settings_load_u8(NVS_TILT_CALIBRATED, &b) == ESP_OK) s_calibrated = (b != 0);

  if (app_settings_load_u16(NVS_TILT_CAL_CX, &u) == ESP_OK) s_ax[TILT_AXIS_X].center = (int16_t)u;
  if (app_settings_load_u16(NVS_TILT_CAL_CY, &u) == ESP_OK) s_ax[TILT_AXIS_Y].center = (int16_t)u;
  if (app_settings_load_u16(NVS_TILT_CAL_LX, &u) == ESP_OK) s_ax[TILT_AXIS_X].neg_extent = (int16_t)u;
  if (app_settings_load_u16(NVS_TILT_CAL_RX, &u) == ESP_OK) s_ax[TILT_AXIS_X].pos_extent = (int16_t)u;
  if (app_settings_load_u16(NVS_TILT_CAL_FY, &u) == ESP_OK) s_ax[TILT_AXIS_Y].neg_extent = (int16_t)u;
  if (app_settings_load_u16(NVS_TILT_CAL_BY, &u) == ESP_OK) s_ax[TILT_AXIS_Y].pos_extent = (int16_t)u;

  if (app_settings_load_u8(NVS_TILT_FORGIVE, &b) == ESP_OK) s_forgive = (b != 0);
  if (app_settings_load_u8(NVS_TILT_MID_W, &b) == ESP_OK) s_middle_width = b;
  if (app_settings_load_u8(NVS_TILT_DZ, &b) == ESP_OK) s_deadzone = b;
  if (app_settings_load_u8(NVS_TILT_RATE, &b) == ESP_OK) {
    if (b < MIN_RATE_HZ) b = MIN_RATE_HZ;
    if (b > MAX_RATE_HZ) b = MAX_RATE_HZ;
    s_rate_hz = b;
  }

  if (app_settings_load_u8(NVS_TILT_INV_X, &b) == ESP_OK) s_ax[TILT_AXIS_X].inverted = (b != 0);
  if (app_settings_load_u8(NVS_TILT_INV_Y, &b) == ESP_OK) s_ax[TILT_AXIS_Y].inverted = (b != 0);

  if (app_settings_load_u8(NVS_TILT_NOTE_OFF, &b) == ESP_OK) {
    if (b < TILT_NOTE_OFF_NUM_MODES) s_note_off_mode = (tilt_note_off_mode_t)b;
  }
}

void tilt_init(void) {
  for (int i = 0; i < 2; i++) {
    s_ax[i].center = 0;
    s_ax[i].neg_extent = -512;
    s_ax[i].pos_extent = 512;
    s_ax[i].last_raw = 0;
    s_ax[i].last_midi_sent = 64;
    s_ax[i].filt = 64.0f;
    s_ax[i].enabled = false;
    s_ax[i].inverted = false;
  }
  s_ax[TILT_AXIS_X].cc_number = DEFAULT_CC_TILT_X;
  s_ax[TILT_AXIS_Y].cc_number = DEFAULT_CC_TILT_Y;

  load_nvs();
  ESP_LOGI(TAG, "Tilt init (calibrated=%d forgive=%d mid_w=%u dz=%u rate=%u Hz)",
    (int)s_calibrated, (int)s_forgive, (unsigned)s_middle_width,
    (unsigned)s_deadzone, (unsigned)s_rate_hz);
}

bool tilt_poll_active(void) {
  return s_ax[TILT_AXIS_X].enabled || s_ax[TILT_AXIS_Y].enabled || s_cal_in_progress;
}

void tilt_axis_set_enabled(tilt_axis_t axis, bool enabled) {
  if ((int)axis < 0 || (int)axis > 1) return;
  bool was_active = tilt_poll_active();
  s_ax[axis].enabled = enabled;
  if (enabled) {
    s_ax[axis].filt = 64.0f;
    s_ax[axis].last_midi_sent = 64;
  }
  // Kick the unified sampling task if we just transitioned from "no polling
  // needed" to "polling needed"; otherwise it stays blocked on the click
  // semaphore indefinitely until a physical click ISR fires.
  if (!was_active && tilt_poll_active()) lis3dhtr_wake_sampling_task();
  ESP_LOGI(TAG, "Tilt axis %c enabled=%d", axis == TILT_AXIS_X ? 'X' : 'Y', (int)enabled);
}

bool tilt_axis_get_enabled(tilt_axis_t axis) {
  if ((int)axis < 0 || (int)axis > 1) return false;
  return s_ax[axis].enabled;
}

int16_t tilt_get_raw(tilt_axis_t axis) {
  if ((int)axis < 0 || (int)axis > 1) return 0;
  return s_ax[axis].last_raw;
}

uint8_t tilt_get_midi(tilt_axis_t axis) {
  if ((int)axis < 0 || (int)axis > 1) return 64;
  return s_ax[axis].last_midi_sent;
}

void tilt_get_last_xy(int16_t* x, int16_t* y) {
  if (x) *x = s_last_sample_x;
  if (y) *y = s_last_sample_y;
}

// Map a raw centered value to a signed scale in [-1, +1] using calibration
// extents.
static float to_scale(int32_t centered, int32_t neg_span, int32_t pos_span) {
  float scale;
  if (centered == 0) return 0.0f;
  if (centered < 0) {
    // centered negative; neg_span is a positive magnitude
    scale = neg_span > 0 ? ((float)centered / (float)neg_span) : 0.0f;
  } else {
    scale = pos_span > 0 ? ((float)centered / (float)pos_span) : 0.0f;
  }
  if (scale > 1.0f) scale = 1.0f;
  if (scale < -1.0f) scale = -1.0f;
  return scale;
}

static uint8_t process_axis(tilt_axis_t axis, int16_t raw) {
  axis_state_t* a = &s_ax[axis];
  a->last_raw = raw;

  int32_t centered = (int32_t)raw - (int32_t)a->center;
  int32_t neg_span = (int32_t)a->center - (int32_t)a->neg_extent;
  int32_t pos_span = (int32_t)a->pos_extent - (int32_t)a->center;
  if (neg_span < 1) neg_span = 1;
  if (pos_span < 1) pos_span = 1;

  float scale = to_scale(centered, neg_span, pos_span);
  if (a->inverted) scale = -scale;
  // bipolar MIDI: 64 center
  float midi_f = 64.0f + scale * 63.0f;

  // IIR smoothing (alpha 0.3)
  a->filt = a->filt + 0.3f * (midi_f - a->filt);
  int32_t midi_i = (int32_t)(a->filt + (a->filt >= 0.0f ? 0.5f : -0.5f));
  if (midi_i < 0) midi_i = 0;
  if (midi_i > 127) midi_i = 127;

  // Forgiveness middle: inside the [64-w, 64+w] zone, snap to 64. Outside the
  // zone, rescale so the zone edge maps to 64 and the full extent still maps
  // to 0/127. This hides the zone from the output curve: there's no jump from
  // 64 to (64+w+1) when exiting the zone, and "full tilt" still reaches 127.
  if (s_forgive) {
    int32_t w = (int32_t)s_middle_width;
    int32_t delta = midi_i - 64;
    int32_t span = 63 - w;
    if (span < 1) span = 1;
    if (delta > w) {
      midi_i = 64 + ((delta - w) * 63) / span;
    } else if (delta < -w) {
      midi_i = 64 + ((delta + w) * 63) / span;
    } else {
      midi_i = 64;
    }
    if (midi_i < 0) midi_i = 0;
    if (midi_i > 127) midi_i = 127;
  }

  return (uint8_t)midi_i;
}

uint8_t tilt_get_processed_midi(tilt_axis_t axis) {
  if ((int)axis < 0 || (int)axis > 1) return 64;
  int16_t raw = (axis == TILT_AXIS_X) ? s_last_sample_x : s_last_sample_y;
  return process_axis(axis, raw);
}

static void post_axis_event(tilt_axis_t axis, uint8_t midi) {
  event_t ev = {
    .type = (axis == TILT_AXIS_X) ? EVENT_SENSOR_TILT_X : EVENT_SENSOR_TILT_Y,
    .priority = EVENT_PRIORITY_NORMAL,
    .timestamp = event_bus_get_current_timestamp(),
    .data.sensor = {
      .channel = 0,
      .controller = s_ax[axis].cc_number,
      .value = midi,
    },
  };
  event_bus_post(&ev);
}

uint32_t tilt_poll_once(void) {
  int16_t x = 0, y = 0, z = 0;
  if (lis3dhtr_read_xyz(&x, &y, &z) != ESP_OK) {
    uint32_t rate = s_rate_hz ? s_rate_hz : DEFAULT_RATE_HZ;
    return 1000u / rate;
  }
  s_last_sample_x = x;
  s_last_sample_y = y;

  if (s_ax[TILT_AXIS_X].enabled) {
    uint8_t m = process_axis(TILT_AXIS_X, x);
    uint8_t last = s_ax[TILT_AXIS_X].last_midi_sent;
    uint8_t diff = (m > last) ? (m - last) : (last - m);
    if (diff >= s_deadzone || m == 64) {
      s_ax[TILT_AXIS_X].last_midi_sent = m;
      post_axis_event(TILT_AXIS_X, m);
    }
  }
  if (s_ax[TILT_AXIS_Y].enabled) {
    uint8_t m = process_axis(TILT_AXIS_Y, y);
    uint8_t last = s_ax[TILT_AXIS_Y].last_midi_sent;
    uint8_t diff = (m > last) ? (m - last) : (last - m);
    if (diff >= s_deadzone || m == 64) {
      s_ax[TILT_AXIS_Y].last_midi_sent = m;
      post_axis_event(TILT_AXIS_Y, m);
    }
  }

  uint32_t rate = s_rate_hz ? s_rate_hz : DEFAULT_RATE_HZ;
  return 1000u / rate;
}

// ---------------- Calibration ----------------

void tilt_cal_begin(void) {
  s_cal_in_progress = true;
  memset(s_cal_steps_done, 0, sizeof(s_cal_steps_done));
  for (int i = 0; i < 2; i++) {
    s_ax[i].cap_center = 0;
    s_ax[i].cap_neg = 0;
    s_ax[i].cap_pos = 0;
  }
  ESP_LOGI(TAG, "Calibration started");
}

esp_err_t tilt_cal_capture(tilt_cal_step_t step) {
  if (!s_cal_in_progress) return ESP_ERR_INVALID_STATE;
  if ((int)step < 0 || (int)step >= TILT_CAL_NUM_STEPS) return ESP_ERR_INVALID_ARG;

  // Average 20 raw samples with ~5ms gaps. Blocking, but called from UI task.
  int32_t sum_x = 0;
  int32_t sum_y = 0;
  const int N = 20;
  int got = 0;
  for (int i = 0; i < N; i++) {
    int16_t x = 0, y = 0, z = 0;
    if (lis3dhtr_read_xyz(&x, &y, &z) == ESP_OK) {
      sum_x += x;
      sum_y += y;
      got++;
    }
    vTaskDelay(pdMS_TO_TICKS(5));
  }
  if (got == 0) return ESP_FAIL;

  int16_t avg_x = (int16_t)(sum_x / got);
  int16_t avg_y = (int16_t)(sum_y / got);
  s_last_sample_x = avg_x;
  s_last_sample_y = avg_y;

  switch (step) {
    case TILT_CAL_CENTER:
      s_ax[TILT_AXIS_X].cap_center = avg_x;
      s_ax[TILT_AXIS_Y].cap_center = avg_y;
      break;
    case TILT_CAL_LEFT:
      s_ax[TILT_AXIS_X].cap_neg = avg_x;
      break;
    case TILT_CAL_RIGHT:
      s_ax[TILT_AXIS_X].cap_pos = avg_x;
      break;
    case TILT_CAL_FORWARD:
      s_ax[TILT_AXIS_Y].cap_neg = avg_y;
      break;
    case TILT_CAL_BACK:
      s_ax[TILT_AXIS_Y].cap_pos = avg_y;
      break;
    default:
      break;
  }
  s_cal_steps_done[step] = true;
  ESP_LOGI(TAG, "Calibration step %d captured (avg x=%d y=%d)", (int)step, avg_x, avg_y);
  return ESP_OK;
}

esp_err_t tilt_cal_commit(void) {
  if (!s_cal_in_progress) return ESP_ERR_INVALID_STATE;
  for (int i = 0; i < TILT_CAL_NUM_STEPS; i++) {
    if (!s_cal_steps_done[i]) return ESP_ERR_INVALID_STATE;
  }

  int16_t cx = s_ax[TILT_AXIS_X].cap_center;
  int16_t cy = s_ax[TILT_AXIS_Y].cap_center;
  int16_t lx = s_ax[TILT_AXIS_X].cap_neg;
  int16_t rx = s_ax[TILT_AXIS_X].cap_pos;
  int16_t fy = s_ax[TILT_AXIS_Y].cap_neg;
  int16_t by = s_ax[TILT_AXIS_Y].cap_pos;

  if (abs(cx - lx) < MIN_EXTENT_SWING || abs(rx - cx) < MIN_EXTENT_SWING ||
      abs(cy - fy) < MIN_EXTENT_SWING || abs(by - cy) < MIN_EXTENT_SWING) {
    ESP_LOGW(TAG, "Calibration rejected: insufficient swing");
    return ESP_ERR_INVALID_STATE;
  }

  s_ax[TILT_AXIS_X].center = cx;
  s_ax[TILT_AXIS_X].neg_extent = lx;
  s_ax[TILT_AXIS_X].pos_extent = rx;
  s_ax[TILT_AXIS_Y].center = cy;
  s_ax[TILT_AXIS_Y].neg_extent = fy;
  s_ax[TILT_AXIS_Y].pos_extent = by;
  s_calibrated = true;
  s_cal_in_progress = false;

  app_settings_save_u16(NVS_TILT_CAL_CX, (uint16_t)cx);
  app_settings_save_u16(NVS_TILT_CAL_CY, (uint16_t)cy);
  app_settings_save_u16(NVS_TILT_CAL_LX, (uint16_t)lx);
  app_settings_save_u16(NVS_TILT_CAL_RX, (uint16_t)rx);
  app_settings_save_u16(NVS_TILT_CAL_FY, (uint16_t)fy);
  app_settings_save_u16(NVS_TILT_CAL_BY, (uint16_t)by);
  app_settings_save_u8(NVS_TILT_CALIBRATED, 1);

  ESP_LOGI(TAG, "Calibration committed (cx=%d cy=%d lx=%d rx=%d fy=%d by=%d)",
    cx, cy, lx, rx, fy, by);
  return ESP_OK;
}

void tilt_cal_abort(void) {
  s_cal_in_progress = false;
  ESP_LOGI(TAG, "Calibration aborted");
}

bool tilt_is_calibrated(void) {
  return s_calibrated;
}

// ---------------- Settings ----------------

void tilt_set_forgive_middle(bool enabled) {
  s_forgive = enabled;
  app_settings_save_u8(NVS_TILT_FORGIVE, enabled ? 1 : 0);
}

bool tilt_get_forgive_middle(void) {
  return s_forgive;
}

void tilt_set_middle_width(uint8_t width_midi) {
  if (width_midi > 30) width_midi = 30;
  s_middle_width = width_midi;
  app_settings_save_u8(NVS_TILT_MID_W, width_midi);
}

uint8_t tilt_get_middle_width(void) {
  return s_middle_width;
}

void tilt_set_deadzone(uint8_t dz) {
  if (dz > 10) dz = 10;
  s_deadzone = dz;
  app_settings_save_u8(NVS_TILT_DZ, dz);
}

uint8_t tilt_get_deadzone(void) {
  return s_deadzone;
}

void tilt_set_rate_hz(uint8_t hz) {
  if (hz < MIN_RATE_HZ) hz = MIN_RATE_HZ;
  if (hz > MAX_RATE_HZ) hz = MAX_RATE_HZ;
  s_rate_hz = hz;
  app_settings_save_u8(NVS_TILT_RATE, hz);
}

uint8_t tilt_get_rate_hz(void) {
  return s_rate_hz;
}

void tilt_set_axis_inverted(tilt_axis_t axis, bool inverted) {
  if ((int)axis < 0 || (int)axis > 1) return;
  s_ax[axis].inverted = inverted;
  app_settings_save_u8(axis == TILT_AXIS_X ? NVS_TILT_INV_X : NVS_TILT_INV_Y,
    inverted ? 1 : 0);
  // Reset the IIR state so a direction flip doesn't cause a long crossfade
  // through the filter history.
  s_ax[axis].filt = 64.0f;
  ESP_LOGI(TAG, "Tilt axis %c inverted=%d", axis == TILT_AXIS_X ? 'X' : 'Y',
    (int)inverted);
}

bool tilt_get_axis_inverted(tilt_axis_t axis) {
  if ((int)axis < 0 || (int)axis > 1) return false;
  return s_ax[axis].inverted;
}

void tilt_set_note_off_mode(tilt_note_off_mode_t mode) {
  if ((int)mode < 0 || (int)mode >= TILT_NOTE_OFF_NUM_MODES) return;
  s_note_off_mode = mode;
  app_settings_save_u8(NVS_TILT_NOTE_OFF, (uint8_t)mode);
  ESP_LOGI(TAG, "Tilt note-off mode=%d", (int)mode);
}

tilt_note_off_mode_t tilt_get_note_off_mode(void) {
  return s_note_off_mode;
}

uint32_t tilt_note_off_duration_ms(uint16_t bpm, uint8_t ts_num, uint8_t ts_den) {
  if (s_note_off_mode == TILT_NOTE_OFF_OFF) return 0;

  // Fixed-time modes don't need BPM.
  switch (s_note_off_mode) {
    case TILT_NOTE_OFF_TIME_100MS: return 100;
    case TILT_NOTE_OFF_TIME_250MS: return 250;
    case TILT_NOTE_OFF_TIME_500MS: return 500;
    case TILT_NOTE_OFF_TIME_1S:    return 1000;
    case TILT_NOTE_OFF_TIME_2S:    return 2000;
    case TILT_NOTE_OFF_TIME_5S:    return 5000;
    default: break;
  }

  if (bpm == 0) bpm = 120;
  if (ts_num == 0) ts_num = 4;
  if (ts_den == 0) ts_den = 4;

  // quarter-note duration in ms
  uint32_t quarter_ms = 60000u / (uint32_t)bpm;
  // Beat duration (the denominator note). At 4/4, beat == quarter; at 6/8,
  // beat == eighth.
  uint32_t beat_ms = (quarter_ms * 4u) / (uint32_t)ts_den;
  uint32_t bar_ms = beat_ms * (uint32_t)ts_num;

  switch (s_note_off_mode) {
    case TILT_NOTE_OFF_SUBDIV_16TH:    return quarter_ms / 4u;
    case TILT_NOTE_OFF_SUBDIV_8TH:     return quarter_ms / 2u;
    case TILT_NOTE_OFF_SUBDIV_QUARTER: return quarter_ms;
    case TILT_NOTE_OFF_SUBDIV_HALF:    return quarter_ms * 2u;
    case TILT_NOTE_OFF_SUBDIV_BAR:     return bar_ms;
    case TILT_NOTE_OFF_SUBDIV_2BARS:   return bar_ms * 2u;
    default: return 0;
  }
}

const char* tilt_note_off_mode_label(tilt_note_off_mode_t mode) {
  switch (mode) {
    case TILT_NOTE_OFF_OFF:            return "Off";
    case TILT_NOTE_OFF_TIME_100MS:     return "100 ms";
    case TILT_NOTE_OFF_TIME_250MS:     return "250 ms";
    case TILT_NOTE_OFF_TIME_500MS:     return "500 ms";
    case TILT_NOTE_OFF_TIME_1S:        return "1 s";
    case TILT_NOTE_OFF_TIME_2S:        return "2 s";
    case TILT_NOTE_OFF_TIME_5S:        return "5 s";
    case TILT_NOTE_OFF_SUBDIV_16TH:    return "1/16";
    case TILT_NOTE_OFF_SUBDIV_8TH:     return "1/8";
    case TILT_NOTE_OFF_SUBDIV_QUARTER: return "1/4";
    case TILT_NOTE_OFF_SUBDIV_HALF:    return "1/2";
    case TILT_NOTE_OFF_SUBDIV_BAR:     return "1 bar";
    case TILT_NOTE_OFF_SUBDIV_2BARS:   return "2 bars";
    default: return "?";
  }
}
