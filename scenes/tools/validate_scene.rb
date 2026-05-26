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
].freeze

# Valid touchwheel modes
VALID_TOUCHWHEEL_MODES = %w[
  buttons program_change continuous set_tempo
  pitch_bend aftertouch nrpn rpn double_cc
].freeze

# Valid touchwheel styles
VALID_TOUCHWHEEL_STYLES = %w[odometer endless bipolar].freeze

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
  when 'pc', 'scene_set'
    unless action['number'].is_a?(Integer) && action['number'].between?(0, 127)
      errors << "#{context}: requires 'number' (0-127)"
    end
  when 'set_tempo'
    if action['bpm'] && !(action['bpm'].is_a?(Integer) && action['bpm'].between?(20, 300))
      errors << "#{context}: 'bpm' must be 20-300"
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
      if action.key?('waveform') && !(action['waveform'].is_a?(Integer) && action['waveform'].between?(0, 5))
        errors << "#{context}: lfo modify 'waveform' must be 0-5"
      end
      if action.key?('rate_mode') && !(action['rate_mode'].is_a?(Integer) && action['rate_mode'].between?(0, 6))
        errors << "#{context}: lfo modify 'rate_mode' must be 0-6"
      end
      if action.key?('rate_hz_x100') && !(action['rate_hz_x100'].is_a?(Integer) && action['rate_hz_x100'].between?(5, 2000))
        errors << "#{context}: lfo modify 'rate_hz_x100' must be 5-2000"
      end
      if action.key?('division') && !(action['division'].is_a?(Integer) && action['division'].between?(0, 10))
        errors << "#{context}: lfo modify 'division' must be 0-10"
      end
      if action.key?('polarity') && !(action['polarity'].is_a?(Integer) && action['polarity'].between?(0, 2))
        errors << "#{context}: lfo modify 'polarity' must be 0-2"
      end
      if action.key?('floor') && !(action['floor'].is_a?(Integer) && action['floor'].between?(0, 127))
        errors << "#{context}: lfo modify 'floor' must be 0-127"
      end
      if action.key?('ceiling') && !(action['ceiling'].is_a?(Integer) && action['ceiling'].between?(0, 127))
        errors << "#{context}: lfo modify 'ceiling' must be 0-127"
      end
      if action.key?('resolution_mode') && !(action['resolution_mode'].is_a?(Integer) && action['resolution_mode'].between?(0, 4))
        errors << "#{context}: lfo modify 'resolution_mode' must be 0-4"
      end
      if action.key?('manual_steps') && !(action['manual_steps'].is_a?(Integer) && action['manual_steps'].between?(1, 256))
        errors << "#{context}: lfo modify 'manual_steps' must be 1-256"
      end
    else
      errors << "#{context}: lfo variant must be 'start', 'stop', 'toggle', or 'modify' (got #{variant.inspect})"
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
    
    active_pads = scene_data['touchpads']&.count { |p| p['enabled'] } || 0
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
