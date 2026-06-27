#ifndef ASSETS_TYPES_H
#define ASSETS_TYPES_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// Control types
#define MIDI_CONTROL_TYPE_CC 0
#define MIDI_CONTROL_TYPE_NRPN 1

// MIDI TRS wiring types (x_midiTrs extension)
typedef enum {
  MIDI_TRS_UNKNOWN = 0,    // Not specified or unknown
  MIDI_TRS_TYPE_A,         // MIDI signal on tip (Empress, 1010music, Red Panda, etc.)
  MIDI_TRS_TYPE_B,         // MIDI signal on ring (Chase Bliss Audio)
  MIDI_TRS_TYPE_TS,        // Tip/Sleeve (Disaster Area, Source Audio, etc.)
  MIDI_TRS_TYPE_BOTH       // Transmit both Type A and Type B simultaneously
} midi_trs_type_t;

// Cache file magic number
#define CACHE_MAGIC 0x4D444343  // 'MDCC'
// Schema 2 adds x_variants (mode-dependent CC overrides).
// Schema 3 adds x_noop (hide a CC in certain gate combinations).
// Schema 4 parses x_mandatory into the control flags byte.
#define CACHE_SCHEMA_VERSION 4

// midi_control_t.flags bits
#define MIDI_CONTROL_FLAG_MANDATORY 0x01  // x_mandatory: required per-scene CC default

// Maximum discrete values per control
#define MAX_DISCRETE_VALUES 16

// Maximum x_variants per control
#define MAX_VARIANTS 8

// Comparison operators for x_variants constraints.
// Evaluated as: (gating CC current value) op value
typedef enum {
  CC_VARIANT_OP_LT = 0,   // <
  CC_VARIANT_OP_LE,       // <=
  CC_VARIANT_OP_GT,       // >
  CC_VARIANT_OP_GE,       // >=
  CC_VARIANT_OP_EQ,       // ==
  CC_VARIANT_OP_NE        // !=
} cc_variant_op_t;

// Discrete value entry (for controls with named preset values)
typedef struct {
  const char *name;       // Value name (points into PSRAM string blob)
  uint16_t value;         // MIDI value (0-127 for CC, 0-16383 for NRPN)
} discrete_value_t;

// A single mode-dependent override of a control (x_variants entry).
// When the gating CC satisfies the constraint, these fields replace the
// control's base name/min/max/discrete set. Inherited fields are
// materialized at parse/load time, so every field is always populated.
typedef struct {
  uint8_t gating_cc;      // CC number whose value is tested
  uint8_t op;             // cc_variant_op_t
  uint16_t value;         // comparison value
  const char *name;       // Effective name (points into PSRAM string blob)
  uint16_t min;           // Effective minimum value
  uint16_t max;           // Effective maximum value
  discrete_value_t *discrete_values;  // Effective discrete values (NULL if continuous)
  uint8_t discrete_count;             // Number of discrete values (0 if continuous)
  uint8_t noop;           // x_noop: when set, hide the CC while this variant matches
} cc_variant_t;

// Individual MIDI control definition
typedef struct {
  uint8_t type;           // 0=CC, 1=NRPN
  uint16_t id;            // CC number or packed NRPN (msb<<7)|lsb
  uint16_t min;           // Minimum value
  uint16_t max;           // Maximum value
  const char *name;       // Control name (points into PSRAM string blob)
  const char *additional_info;  // Optional additional info
  uint8_t flags;          // Reserved for taper, stepped, etc.
  discrete_value_t *discrete_values;  // Array of discrete values (NULL if continuous)
  uint8_t discrete_count;             // Number of discrete values (0 if continuous)
  cc_variant_t *variants;             // Array of mode variants (NULL if none)
  uint8_t variant_count;              // Number of variants (0 if none)
  uint8_t noop;                       // x_noop on base: hide CC when no variant matches
} midi_control_t;

// Bank select mode for program changes (matches device_config.h)
typedef enum {
  PC_BANK_SELECT_NONE = 0,   // PC only (default, 0-127)
  PC_BANK_SELECT_CC0,        // CC0 + PC (128 banks × 128 programs)
  PC_BANK_SELECT_CC0_CC32    // CC0 + CC32 + PC (explicit LSB)
} pc_bank_select_mode_t;

// Program change configuration
typedef struct {
  uint16_t index_base;           // Starting index (0 or 1)
  uint16_t count;                // Number of program changes (default 128)
  pc_bank_select_mode_t bank_mode;  // Bank select protocol
  const char **names;            // Optional array of preset names (NULL if not provided)
} program_change_info_t;

// Full device definition
typedef struct {
  char slug[64];          // Device slug identifier
  char name[64];          // Display name
  char vendor[64];        // Manufacturer
  char model[64];         // Model name
  char version[32];       // Version string
  
  midi_control_t *controls;   // Array of controls
  uint16_t control_count;     // Number of controls
  
  int16_t *cc_lookup;         // 128-entry lookup table: cc→control_idx (-1 if unused)
  
  program_change_info_t *pc_info;  // Program change info (NULL if not supported)
  
  bool receives_pc;       // Receives program change
  bool transmits_pc;      // Transmits program change
  bool receives_clock;    // Receives MIDI clock
  bool receives_notes;    // Receives NOTE_ON/NOTE_OFF
  
  midi_trs_type_t trs_type;  // MIDI TRS wiring type (x_midiTrs extension)
  uint8_t midi_channel;      // Preferred MIDI channel (1-16, 0 = not specified)
  
  void *string_blob;      // PSRAM blob containing all strings
  size_t string_blob_size;

  // Owning base pointers for the bulk pools the controls/variants point into.
  // controls[].discrete_values, variants[], and variants[].discrete_values are
  // sub-ranges of these single allocations, so they must be freed here (not per
  // control) by assets_free_device.
  void *discrete_pool;          // Base of all controls' discrete value entries
  void *variant_pool;           // Base of all cc_variant_t entries
  void *variant_discrete_pool;  // Base of all variants' discrete value entries
  void *pc_names_blob;          // String storage backing pc_info->names
} device_def_t;

// Binary cache file header
typedef struct {
  uint32_t magic;         // 'MDCC'
  uint16_t schema;        // Cache schema version
  uint16_t reserved;
  uint8_t json_sha256[32];  // SHA256 of source JSON
  uint32_t control_count;   // Number of controls
  uint32_t string_blob_size;  // Size of string blob
  uint32_t pc_name_count;   // Number of PC names (0 if none)
  uint32_t crc32;          // CRC32 of data after this header
} __attribute__((packed)) cache_header_t;

// Packed binary control record for cache files
typedef struct {
  uint8_t type;          // 0=CC, 1=NRPN
  uint16_t id;           // CC or NRPN packed
  uint16_t min;
  uint16_t max;
  uint32_t name_offset;  // Offset into string blob
  uint32_t info_offset;  // Offset into string blob (0 if none)
  uint8_t flags;
  uint8_t discrete_count;  // Number of discrete values (0 if continuous)
  uint8_t variant_count;   // Number of x_variants (0 if none)
  uint8_t noop;            // x_noop on base control (0/1)
} __attribute__((packed)) control_record_t;

// Packed binary discrete value record for cache files
typedef struct {
  uint32_t name_offset;  // Offset into string blob
  uint16_t value;        // MIDI value
  uint16_t padding;      // Align to 8 bytes
} __attribute__((packed)) discrete_value_record_t;

// Packed binary variant record for cache files.
// Variant records follow all base discrete records; variant discrete
// records (discrete_value_record_t) follow all variant records in the
// same control/variant order.
typedef struct {
  uint8_t gating_cc;     // CC number whose value is tested
  uint8_t op;            // cc_variant_op_t
  uint16_t value;        // comparison value
  uint32_t name_offset;  // Offset into string blob
  uint16_t min;
  uint16_t max;
  uint8_t discrete_count;  // Number of discrete values for this variant
  uint8_t noop;            // x_noop on this variant (0/1)
  uint8_t padding[2];      // Align to 16 bytes
} __attribute__((packed)) cc_variant_record_t;

// Manifest device entry
typedef struct {
  char slug[64];
  char name[64];
  char vendor[64];
  char version[32];
  char file[128];
  uint8_t sha256[32];
  uint32_t size;
  // Device metadata (for display without loading full device)
  uint8_t trs_type;       // midi_trs_type_t
  uint8_t midi_channel;   // 1-16, 0 = not specified
  bool receives_pc;
  bool receives_clock;
  bool receives_notes;
  bool transmits_pc;
  uint16_t cc_count;
  // PC info from x_pc extension (needed for cache loading)
  uint16_t pc_index_base;  // 0 or 1
  uint16_t pc_count;       // Number of presets (default 128)
  uint8_t pc_bank_mode;    // pc_bank_select_mode_t
  // Origin partition: false = shared/RO (/assets/devices/...),
  //                   true  = user/RW (/userdata/devices/...)
  // Set during manifest parsing; consumed by assets_load_device() when
  // composing the on-disk JSON path.
  bool from_userdata;
} manifest_device_t;

// Manifest structure
typedef struct {
  uint32_t schema;
  uint32_t device_count;
  manifest_device_t *devices;  // Array allocated in PSRAM
} manifest_t;

#endif // ASSETS_TYPES_H

