#!/usr/bin/env ruby
# frozen_string_literal: true

require 'json'

# Valid action types (string names from scene.c action_type_json_names)
VALID_ACTION_TYPES = %w[
  none
  program_next program_prev pc
  scene_next scene_prev scene_set
  transport_play transport_stop transport_pause transport_record
  tap_tempo set_tempo tempo_inc tempo_dec
  send_cc send_cc_hold send_cc_cycle send_double_cc
  send_nrpn send_rpn
  send_note_on send_note_off
  send_pitch_bend send_aftertouch
  send_song_select send_song_position send_mmc
  randomize_cc
  send_clock_start send_clock_stop send_clock_continue
  send_reset send_tune_request
  confirm_pending
  all_notes_off all_sound_off
  sustain sostenuto
  tw_mode_hold tw_mode_cycle
  touchwheel touchwheel_hold touchwheel_cycle
  lfo lfo_start lfo_stop lfo_toggle lfo_shape
  clock clock_toggle clock_hold clock_burst
  cut cut_toggle cut_hold
  ui set_ui ui_hold ui_cycle
  param param_hold param_cycle
  note
  rtg rtg_toggle rtg_hold
  sample_hold sample_hold_toggle sample_hold_hold
  step
  inspect_scene
].freeze

# Valid touchwheel modes
VALID_TOUCHWHEEL_MODES = %w[
  buttons program_change continuous set_tempo
  pitch_bend aftertouch nrpn rpn double_cc
].freeze

# Valid touchwheel styles
VALID_TOUCHWHEEL_STYLES = %w[odometer endless bipolar].freeze

def validate_engine_modify_fields(action, context, errors)
  rand_u8 = 254
  rand_u16 = 65534
  if action.key?('rate_mode') &&
     !(action['rate_mode'].is_a?(Integer) &&
       (action['rate_mode'] == rand_u8 || action['rate_mode'].between?(0, 1)))
    errors << "#{context}: modify 'rate_mode' must be 0-1 or #{rand_u8} (random)"
  end
  if action.key?('rate_hz_x100') &&
     !(action['rate_hz_x100'].is_a?(Integer) &&
       (action['rate_hz_x100'] == rand_u16 || action['rate_hz_x100'].between?(50, 2500)))
    errors << "#{context}: modify 'rate_hz_x100' must be 50-2500 or #{rand_u16} (random)"
  end
  if action.key?('sync_mult_x1000') &&
     !(action['sync_mult_x1000'].is_a?(Integer) &&
       (action['sync_mult_x1000'] == rand_u16 || action['sync_mult_x1000'].between?(125, 8000)))
    errors << "#{context}: modify 'sync_mult_x1000' must be 125-8000 or #{rand_u16} (random)"
  end
  if action.key?('glide') &&
     !(action['glide'].is_a?(Integer) &&
       (action['glide'] == rand_u8 || action['glide'].between?(0, 1)))
    errors << "#{context}: modify 'glide' must be 0-1 or #{rand_u8} (random)"
  end
  if action.key?('probability') &&
     !(action['probability'].is_a?(Integer) &&
       (action['probability'] == rand_u8 || action['probability'].between?(10, 100)))
    errors << "#{context}: modify 'probability' must be 10-100 or #{rand_u8} (random)"
  end
end

def validate_action(action, context, errors)
  type = action['type']
  
  unless type.is_a?(String)
    errors << "#{context}: 'type' must be a string"
    return
  end
  
  unless VALID_ACTION_TYPES.include?(type)
    errors << "#{context}: unknown action type '#{type}'"
    return
  end
  
  # Validate parameters based on type
  case type
  when 'send_cc', 'send_cc_hold', 'send_cc_cycle', 'randomize_cc'
    unless action['cc'].is_a?(Integer) && action['cc'].between?(0, 127)
      errors << "#{context}: requires 'cc' (0-127)"
    end
    if action['value'] && !(action['value'].is_a?(Integer) && action['value'].between?(0, 127))
      errors << "#{context}: 'value' must be 0-127"
    end
  when 'send_note_on', 'send_note_off'
    unless action['note'].is_a?(Integer) && action['note'].between?(0, 127)
      errors << "#{context}: requires 'note' (0-127)"
    end
    if action['velocity'] && !(action['velocity'].is_a?(Integer) && action['velocity'].between?(0, 127))
      errors << "#{context}: 'velocity' must be 0-127"
    end
  when 'note'
    note_rand = 254
    unless action['note'].is_a?(Integer) &&
           (action['note'] == note_rand || action['note'].between?(0, 127))
      errors << "#{context}: requires 'note' (0-127 or #{note_rand} for random)"
    end
    if action['velocity'] &&
       !(action['velocity'].is_a?(Integer) &&
         (action['velocity'] == note_rand || action['velocity'].between?(0, 127)))
      errors << "#{context}: 'velocity' must be 0-127 or #{note_rand} (random)"
    end
    if action.key?('random_floor') &&
       !(action['random_floor'].is_a?(Integer) && action['random_floor'].between?(36, 96))
      errors << "#{context}: 'random_floor' must be 36-96"
    end
    if action.key?('random_ceiling') &&
       !(action['random_ceiling'].is_a?(Integer) && action['random_ceiling'].between?(36, 96))
      errors << "#{context}: 'random_ceiling' must be 36-96"
    end
    if action.key?('voices') &&
       !(action['voices'].is_a?(Integer) && action['voices'].between?(1, 4))
      errors << "#{context}: 'voices' must be 1-4"
    end
    if action.key?('bass') && ![true, false].include?(action['bass'])
      errors << "#{context}: 'bass' must be a boolean"
    end
    if action.key?('aftertouch') && ![true, false].include?(action['aftertouch'])
      errors << "#{context}: 'aftertouch' must be a boolean"
    end
  when 'pc', 'scene_set'
    unless action['number'].is_a?(Integer) && action['number'].between?(0, 127)
      errors << "#{context}: requires 'number' (0-127)"
    end
  when 'set_tempo'
    if action['bpm'] && !(action['bpm'].is_a?(Integer) &&
        (action['bpm'] == 0 || action['bpm'] == 65535 || action['bpm'].between?(20, 300)))
      errors << "#{context}: 'bpm' must be 0 (random), 65535 (original), or 20-300"
    if action['bpm'] == 0
      if action.key?('random_floor') &&
         !(action['random_floor'].is_a?(Integer) && action['random_floor'].between?(20, 300))
        errors << "#{context}: 'random_floor' must be 20-300"
      end
      if action.key?('random_ceiling') &&
         !(action['random_ceiling'].is_a?(Integer) && action['random_ceiling'].between?(20, 300))
        errors << "#{context}: 'random_ceiling' must be 20-300"
      end
    end
    end
  when 'send_nrpn', 'send_rpn'
    unless action['parameter'].is_a?(Integer) && action['parameter'].between?(0, 16383)
      errors << "#{context}: requires 'parameter' (0-16383)"
    end
    if action['value'] && !(action['value'].is_a?(Integer) && action['value'].between?(0, 16383))
      errors << "#{context}: 'value' must be 0-16383"
    end
  when 'send_double_cc'
    unless action['cc'].is_a?(Integer) && action['cc'].between?(0, 31)
      errors << "#{context}: requires 'cc' (0-31, MSB of 14-bit pair)"
    end
    if action['value'] && !(action['value'].is_a?(Integer) && action['value'].between?(0, 16383))
      errors << "#{context}: 'value' must be 0-16383"
    end
  when 'tw_mode_hold', 'touchwheel_hold'
    # Legacy single-type entries. New 'touchwheel' type below dispatches on
    # variant. Mode index cap matches firmware NUM_TOUCHWHEEL_USER_MODES = 13.
    unless action['mode'].is_a?(Integer) && action['mode'].between?(0, 12)
      errors << "#{context}: requires 'mode' (0-12)"
    end
    unless action['mode2'].is_a?(Integer) && action['mode2'].between?(0, 12)
      errors << "#{context}: requires 'mode2' (0-12)"
    end
  when 'tw_mode_cycle', 'touchwheel_cycle'
    unless action['num_modes'].is_a?(Integer) && action['num_modes'].between?(2, 8)
      errors << "#{context}: requires 'num_modes' (2-8)"
    end
    unless action['modes'].is_a?(Array) && action['modes'].length >= 2
      errors << "#{context}: requires 'modes' array with 2+ values"
    end
    if action['modes'].is_a?(Array)
      action['modes'].each_with_index do |m, i|
        unless m.is_a?(Integer) && m.between?(0, 12)
          errors << "#{context}: modes[#{i}] must be 0-12 (got #{m.inspect})"
        end
      end
    end
  when 'touchwheel'
    # Consolidated type. Variant decides which fields are required.
    variant = action['variant']
    case variant
    when 'hold', nil
      # nil variant defaults to hold per firmware fallback
      unless action['mode'].is_a?(Integer) && action['mode'].between?(0, 12)
        errors << "#{context}: touchwheel hold requires 'mode' (0-12)"
      end
      if action['release_to_original']
        unless [true, false].include?(action['release_to_original'])
          errors << "#{context}: 'release_to_original' must be boolean"
        end
      else
        unless action['mode2'].is_a?(Integer) && action['mode2'].between?(0, 12)
          errors << "#{context}: touchwheel hold requires 'mode2' (0-12) unless release_to_original is true"
        end
      end
    when 'cycle'
      unless action['num_modes'].is_a?(Integer) && action['num_modes'].between?(2, 8)
        errors << "#{context}: touchwheel cycle requires 'num_modes' (2-8)"
      end
      unless action['modes'].is_a?(Array) && action['modes'].length >= 2
        errors << "#{context}: touchwheel cycle requires 'modes' array with 2+ values"
      end
      if action['modes'].is_a?(Array)
        action['modes'].each_with_index do |m, i|
          unless m.is_a?(Integer) && m.between?(0, 12)
            errors << "#{context}: modes[#{i}] must be 0-12 (got #{m.inspect})"
          end
        end
      end
    else
      errors << "#{context}: touchwheel variant must be 'hold' or 'cycle' (got #{variant.inspect})"
    end
  when 'lfo'
    # Consolidated LFO family. Variant decides which fields apply.
    #   start/stop/toggle: slot only.
    #   modify: slot plus any of 8 optional override fields, each with the
    #     range matched in the JSON schema (sentinels live in firmware as
    #     "field absent from JSON = Original").
    variant = action['variant']
    unless action['slot'].is_a?(Integer) && [1, 2, 3].include?(action['slot'])
      errors << "#{context}: lfo requires 'slot' (1=LFO1, 2=LFO2, 3=both)"
    end
    case variant
    when 'start', 'stop', 'toggle', nil
      # nil variant defaults to start per firmware fallback. No further fields.
    when 'modify'
      lfo_rand_u8 = 254
      lfo_rand_u16 = 65534
      lfo_rand_steps = 254
      if action.key?('waveform') && !(action['waveform'].is_a?(Integer) && (action['waveform'] == lfo_rand_u8 || action['waveform'].between?(0, 5)))
        errors << "#{context}: lfo modify 'waveform' must be 0-5 or #{lfo_rand_u8} (random)"
      end
      if action.key?('rate_mode') && !(action['rate_mode'].is_a?(Integer) && (action['rate_mode'] == lfo_rand_u8 || action['rate_mode'].between?(0, 6)))
        errors << "#{context}: lfo modify 'rate_mode' must be 0-6 or #{lfo_rand_u8} (random)"
      end
      if action.key?('rate_hz_x100') && !(action['rate_hz_x100'].is_a?(Integer) && (action['rate_hz_x100'] == lfo_rand_u16 || action['rate_hz_x100'].between?(5, 2000)))
        errors << "#{context}: lfo modify 'rate_hz_x100' must be 5-2000 or #{lfo_rand_u16} (random)"
      end
      if action.key?('division') && !(action['division'].is_a?(Integer) && (action['division'] == lfo_rand_u8 || action['division'].between?(0, 10)))
        errors << "#{context}: lfo modify 'division' must be 0-10 or #{lfo_rand_u8} (random)"
      end
      if action.key?('polarity') && !(action['polarity'].is_a?(Integer) && (action['polarity'] == lfo_rand_u8 || action['polarity'].between?(0, 2)))
        errors << "#{context}: lfo modify 'polarity' must be 0-2 or #{lfo_rand_u8} (random)"
      end
      if action.key?('floor') && !(action['floor'].is_a?(Integer) && (action['floor'] == lfo_rand_u8 || action['floor'].between?(0, 127)))
        errors << "#{context}: lfo modify 'floor' must be 0-127 or #{lfo_rand_u8} (random)"
      end
      if action.key?('ceiling') && !(action['ceiling'].is_a?(Integer) && (action['ceiling'] == lfo_rand_u8 || action['ceiling'].between?(0, 127)))
        errors << "#{context}: lfo modify 'ceiling' must be 0-127 or #{lfo_rand_u8} (random)"
      end
      if action.key?('resolution_mode') && !(action['resolution_mode'].is_a?(Integer) && (action['resolution_mode'] == lfo_rand_u8 || action['resolution_mode'].between?(0, 4)))
        errors << "#{context}: lfo modify 'resolution_mode' must be 0-4 or #{lfo_rand_u8} (random)"
      end
      if action.key?('manual_steps') && !(action['manual_steps'].is_a?(Integer) && (action['manual_steps'] == lfo_rand_steps || action['manual_steps'].between?(1, 256)))
        errors << "#{context}: lfo modify 'manual_steps' must be 1-256 or #{lfo_rand_steps} (random)"
      end
    else
      errors << "#{context}: lfo variant must be 'start', 'stop', 'toggle', or 'modify' (got #{variant.inspect})"
    end
  when 'clock'
    variant = action['variant']
    clock_speeds = [25, 50, 75, 100, 125, 150, 175, 200, 225, 250, 275, 300]
    case variant
    when 'toggle', 'hold', nil
      if action.key?('start_enabled') && ![true, false].include?(action['start_enabled'])
        errors << "#{context}: clock 'start_enabled' must be a boolean"
      end
    when 'burst'
      if action.key?('speed_percent') &&
         !(action['speed_percent'].is_a?(Integer) && clock_speeds.include?(action['speed_percent']))
        errors << "#{context}: clock burst 'speed_percent' must be one of #{clock_speeds.join(', ')}"
      end
    else
      errors << "#{context}: clock variant must be 'toggle', 'hold', or 'burst' (got #{variant.inspect})"
    end
  when 'clock_toggle', 'clock_hold'
    if action.key?('start_enabled') && ![true, false].include?(action['start_enabled'])
      errors << "#{context}: #{type} 'start_enabled' must be a boolean"
    end
  when 'clock_burst'
    clock_speeds = [25, 50, 75, 100, 125, 150, 175, 200, 225, 250, 275, 300]
    if action.key?('speed_percent') &&
       !(action['speed_percent'].is_a?(Integer) && clock_speeds.include?(action['speed_percent']))
      errors << "#{context}: clock_burst 'speed_percent' must be one of #{clock_speeds.join(', ')}"
    end
  when 'cut'
    variant = action['variant']
    cut_modes = %w[local passthrough both]
    case variant
    when 'toggle', 'hold', nil
      if action.key?('cut_mode') &&
         !(action['cut_mode'].is_a?(String) && cut_modes.include?(action['cut_mode']))
        errors << "#{context}: cut 'cut_mode' must be one of #{cut_modes.join(', ')}"
      end
    else
      errors << "#{context}: cut variant must be 'toggle' or 'hold' (got #{variant.inspect})"
    end
  when 'cut_toggle', 'cut_hold'
    cut_modes = %w[local passthrough both]
    if action.key?('cut_mode') &&
       !(action['cut_mode'].is_a?(String) && cut_modes.include?(action['cut_mode']))
      errors << "#{context}: #{type} 'cut_mode' must be one of #{cut_modes.join(', ')}"
    end
  when 'ui'
    variant = action['variant']
    case variant
    when 'set', nil
      if action.key?('module') &&
         !(action['module'].is_a?(Integer) && action['module'] >= 0)
        errors << "#{context}: ui set 'module' must be a non-negative integer"
      end
    when 'hold'
      unless action['module'].is_a?(Integer) && action['module'] >= 0
        errors << "#{context}: ui hold requires 'module' (non-negative integer)"
      end
      unless action['module2'].is_a?(Integer) && action['module2'] >= 0
        errors << "#{context}: ui hold requires 'module2' (non-negative integer)"
      end
    when 'cycle'
      unless action['num_modules'].is_a?(Integer) && action['num_modules'].between?(2, 8)
        errors << "#{context}: ui cycle requires 'num_modules' (2-8)"
      end
      unless action['modules'].is_a?(Array) && action['modules'].length.between?(2, 8)
        errors << "#{context}: ui cycle requires 'modules' array (2-8 items)"
      end
      if action['modules'].is_a?(Array)
        action['modules'].each_with_index do |m, i|
          unless m.is_a?(Integer) && m >= 0
            errors << "#{context}: ui cycle modules[#{i}] must be a non-negative integer"
          end
        end
      end
    else
      errors << "#{context}: ui variant must be 'set', 'hold', or 'cycle' (got #{variant.inspect})"
    end
  when 'set_ui'
    unless action['module'].is_a?(Integer) && action['module'] >= 0
      errors << "#{context}: set_ui requires 'module' (non-negative integer)"
    end
  when 'ui_hold'
    unless action['module'].is_a?(Integer) && action['module'] >= 0
      errors << "#{context}: ui_hold requires 'module' (non-negative integer)"
    end
    unless action['module2'].is_a?(Integer) && action['module2'] >= 0
      errors << "#{context}: ui_hold requires 'module2' (non-negative integer)"
    end
  when 'ui_cycle'
    unless action['num_modules'].is_a?(Integer) && action['num_modules'].between?(2, 8)
      errors << "#{context}: ui_cycle requires 'num_modules' (2-8)"
    end
    unless action['modules'].is_a?(Array) && action['modules'].length.between?(2, 8)
      errors << "#{context}: ui_cycle requires 'modules' array (2-8 items)"
    end
    if action['modules'].is_a?(Array)
      action['modules'].each_with_index do |m, i|
        unless m.is_a?(Integer) && m >= 0
          errors << "#{context}: ui_cycle modules[#{i}] must be a non-negative integer"
        end
      end
    end
  when 'rtg'
    variant = action['variant']
    case variant
    when 'toggle', 'hold', 'step', nil
      # Parameterless action; no extra fields.
    when 'modify'
      validate_engine_modify_fields(action, context, errors)
    else
      errors << "#{context}: rtg variant must be 'toggle', 'hold', 'step', or 'modify' (got #{variant.inspect})"
    end
  when 'rtg_toggle', 'rtg_hold'
    # Legacy single-type entries; no extra fields.
  when 'sample_hold'
    variant = action['variant']
    case variant
    when 'toggle', 'hold', 'step', nil
      # Parameterless action; no extra fields.
    when 'modify'
      validate_engine_modify_fields(action, context, errors)
    else
      errors << "#{context}: sample_hold variant must be 'toggle', 'hold', 'step', or 'modify' (got #{variant.inspect})"
    end
  when 'sample_hold_toggle', 'sample_hold_hold'
    # Legacy single-type entries; no extra fields.
  when 'step'
    target = action['step_target']
    if target && !%w[rtg sh].include?(target)
      errors << "#{context}: step step_target must be 'rtg' or 'sh' (got #{target.inspect})"
    end
  when 'param'
    variant = action['variant']
    case variant
    when 'hold', nil
      unless action['param'].is_a?(Integer) && action['param'].between?(0, 127)
        errors << "#{context}: param hold requires 'param' (0-127)"
      end
      unless action['param2'].is_a?(Integer) && action['param2'].between?(0, 127)
        errors << "#{context}: param hold requires 'param2' (0-127)"
      end
    when 'cycle'
      unless action['num_params'].is_a?(Integer) && action['num_params'].between?(2, 8)
        errors << "#{context}: param cycle requires 'num_params' (2-8)"
      end
      unless action['params'].is_a?(Array) && action['params'].length >= 2
        errors << "#{context}: param cycle requires 'params' array with 2+ values"
      end
      if action['params'].is_a?(Array)
        action['params'].each_with_index do |p, i|
          unless p.is_a?(Integer) && p.between?(0, 127)
            errors << "#{context}: params[#{i}] must be 0-127 (got #{p.inspect})"
          end
        end
      end
    else
      errors << "#{context}: param variant must be 'hold' or 'cycle' (got #{variant.inspect})"
    end
  when 'param_hold'
    unless action['param'].is_a?(Integer) && action['param'].between?(0, 127)
      errors << "#{context}: param_hold requires 'param' (0-127)"
    end
    unless action['param2'].is_a?(Integer) && action['param2'].between?(0, 127)
      errors << "#{context}: param_hold requires 'param2' (0-127)"
    end
  when 'param_cycle'
    unless action['num_params'].is_a?(Integer) && action['num_params'].between?(2, 8)
      errors << "#{context}: param_cycle requires 'num_params' (2-8)"
    end
    unless action['params'].is_a?(Array) && action['params'].length >= 2
      errors << "#{context}: param_cycle requires 'params' array with 2+ values"
    end
    if action['params'].is_a?(Array)
      action['params'].each_with_index do |p, i|
        unless p.is_a?(Integer) && p.between?(0, 127)
          errors << "#{context}: params[#{i}] must be 0-127 (got #{p.inspect})"
        end
      end
    end
  when 'lfo_start', 'lfo_stop', 'lfo_toggle', 'lfo_shape'
    # Legacy single-type entries (pre-consolidation). Minimal validation --
    # firmware's migration table rewrites them to 'lfo' + the matching
    # variant on load; 'lfo_shape' additionally collapses its old shapes[]
    # cycle to a single waveform override (the first entry).
    unless action['slot'].is_a?(Integer) && [1, 2, 3].include?(action['slot'])
      errors << "#{context}: #{type} requires 'slot' (1=LFO1, 2=LFO2, 3=both)"
    end
    if type == 'lfo_shape'
      # shapes[] is optional in legacy files; if present, sanity-check it.
      if action['shapes'].is_a?(Array)
        action['shapes'].each_with_index do |s, i|
          unless s.is_a?(Integer) && s.between?(0, 5)
            errors << "#{context}: legacy lfo_shape shapes[#{i}] must be 0-5"
          end
        end
      end
    end
  end
end

def validate_continuous_mapping(mapping, name, errors)
  return unless mapping.is_a?(Hash)
  
  if mapping.key?('output_type')
    unless %w[cc note pitch_bend aftertouch].include?(mapping['output_type'])
      errors << "#{name}: invalid output_type '#{mapping['output_type']}'"
    end
  end
  
  if mapping['cc_number'] && !(mapping['cc_number'].is_a?(Integer) && mapping['cc_number'].between?(0, 127))
    errors << "#{name}: cc_number must be 0-127"
  end
  
  %w[min_value max_value velocity].each do |field|
    if mapping[field] && !(mapping[field].is_a?(Integer) && mapping[field].between?(0, 127))
      errors << "#{name}: #{field} must be 0-127"
    end
  end
end

def validate_scene(scene_file)
  basename = File.basename(scene_file)
  
  # Skip non-scene files
  if basename == 'manifest.json'
    puts "⏭️  Skipping #{basename} (not a scene file)"
    return nil  # nil = skipped, not failed
  end
  
  unless File.exist?(scene_file)
    puts "❌ Error: File not found: #{scene_file}"
    return false
  end

  begin
    scene_data = JSON.parse(File.read(scene_file))
  rescue JSON::ParserError => e
    puts "❌ Error: Invalid JSON in #{scene_file}"
    puts "   #{e.message}"
    return false
  end

  errors = []
  
  # Required fields
  errors << "Missing 'name' field" unless scene_data['name'].is_a?(String)
  errors << "Missing 'touchpads' array" unless scene_data['touchpads'].is_a?(Array)
  errors << "Missing 'button_left' array" unless scene_data['button_left'].is_a?(Array)
  errors << "Missing 'button_right' array" unless scene_data['button_right'].is_a?(Array)
  errors << "Missing 'button_both' array" unless scene_data['button_both'].is_a?(Array)
  
  # Validate field values
  if scene_data['name'] && scene_data['name'].length > 32
    errors << "Scene name too long (max 32 chars)"
  end
  
  if scene_data['program_number']
    pn = scene_data['program_number']
    errors << "program_number must be 0-127" unless pn.is_a?(Integer) && pn.between?(0, 127)
  end
  
  if scene_data['touchwheel_mode']
    unless VALID_TOUCHWHEEL_MODES.include?(scene_data['touchwheel_mode'])
      errors << "touchwheel_mode must be one of: #{VALID_TOUCHWHEEL_MODES.join(', ')}"
    end
  end
  
  if scene_data['touchwheel_style']
    unless VALID_TOUCHWHEEL_STYLES.include?(scene_data['touchwheel_style'])
      errors << "touchwheel_style must be one of: #{VALID_TOUCHWHEEL_STYLES.join(', ')}"
    end
  end
  
  # Validate BPM
  if scene_data['bpm']
    bpm = scene_data['bpm']
    errors << "bpm must be 20-300" unless bpm.is_a?(Integer) && bpm.between?(20, 300)
  end
  
  # Validate clock_source
  if scene_data['clock_source']
    unless %w[internal external].include?(scene_data['clock_source'])
      errors << "clock_source must be 'internal' or 'external'"
    end
  end
  
  # Validate beat_divider
  valid_dividers = %w[whole half quarter eighth sixteenth thirtysecond]
  if scene_data['beat_divider'] && !valid_dividers.include?(scene_data['beat_divider'])
    errors << "beat_divider must be one of: #{valid_dividers.join(', ')}"
  end
  
  # Validate time_signature
  if (ts = scene_data['time_signature'])
    if ts.is_a?(Hash)
      unless ts['numerator'].is_a?(Integer) && ts['numerator'].between?(1, 16)
        errors << "time_signature.numerator must be 1-16"
      end
      unless [2, 4, 8, 16].include?(ts['denominator'])
        errors << "time_signature.denominator must be 2, 4, 8, or 16"
      end
    else
      errors << "time_signature must be an object with numerator and denominator"
    end
  end
  
  # Validate touchpads
  if scene_data['touchpads']
    if scene_data['touchpads'].length != 12
      errors << "Must have exactly 12 touchpads, found #{scene_data['touchpads'].length}"
    end
    
    scene_data['touchpads'].each_with_index do |pad, idx|
      errors << "Touchpad #{idx} missing 'enabled' field" unless pad.key?('enabled')
      errors << "Touchpad #{idx} missing 'actions' array" unless pad['actions'].is_a?(Array)
      
      if pad['actions'] && pad['actions'].length > 4
        errors << "Touchpad #{idx} has #{pad['actions'].length} actions (max 4)"
      end
      
      pad['actions']&.each_with_index do |action, aidx|
        validate_action(action, "Touchpad #{idx} action #{aidx}", errors)
      end
    end
  end
  
  # Validate button actions
  %w[button_left button_right button_both bump on_load sustain sostenuto].each do |btn|
    if scene_data[btn].is_a?(Array)
      if scene_data[btn].length > 4
        errors << "#{btn} has #{scene_data[btn].length} actions (max 4)"
      end
      
      scene_data[btn].each_with_index do |action, idx|
        validate_action(action, "#{btn} action #{idx}", errors)
      end
    end
  end
  
  # Validate continuous mappings
  %w[touchwheel expression cv proximity als].each do |mapping_name|
    if scene_data[mapping_name].is_a?(Hash)
      validate_continuous_mapping(scene_data[mapping_name], mapping_name, errors)
    end
  end

  if errors.empty?
    puts "✅ #{basename} is valid"
    
    # Print summary
    puts "\n   Scene Summary:"
    puts "     Name: #{scene_data['name']}"
    puts "     Program: #{scene_data['program_number'] || 'not set'}"
    puts "     PC on load: #{scene_data['send_pc_on_load'] || false}"
    puts "     Touchwheel: #{scene_data['touchwheel_mode']} (#{scene_data['touchwheel_style']})"
    puts "     BPM: #{scene_data['bpm'] || 120}"
    
    if (ts = scene_data['time_signature'])
      puts "     Time sig: #{ts['numerator']}/#{ts['denominator']}"
    end
    
    active_pads = scene_data['touchpads']&.count { |p|
      act = p['action'] || p['actions']&.first
      act && act['type'] && act['type'] != 'none'
    } || 0
    puts "     Active pads: #{active_pads}/12"
    
    return true
  else
    puts "❌ Validation failed for #{basename}:"
    errors.each { |error| puts "   - #{error}" }
    return false
  end
end

if ARGV.empty?
  puts "Usage: ruby validate_scene.rb <scene_file.json>"
  puts "       ruby validate_scene.rb *.json"
  exit 1
end

results = ARGV.map { |file| validate_scene(file) }

# Filter out nil (skipped files)
actual_results = results.compact
skipped = results.count(&:nil?)

if actual_results.empty?
  puts "\n⏭️  No scene files to validate"
  exit 0
elsif actual_results.all?
  puts "\n✅ All #{actual_results.length} scene(s) valid!"
  puts "   (#{skipped} non-scene file(s) skipped)" if skipped > 0
  exit 0
else
  failed = actual_results.count(false)
  puts "\n❌ #{failed}/#{actual_results.length} scene(s) failed validation"
  exit 1
end
