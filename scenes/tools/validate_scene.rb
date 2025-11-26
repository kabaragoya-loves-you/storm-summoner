#!/usr/bin/env ruby
# frozen_string_literal: true

require 'json'

def validate_action(action, context, errors)
  unless action['type'].is_a?(Integer)
    errors << "#{context}: missing or invalid 'type'"
    return
  end
  
  type = action['type']
  
  # Validate type range
  if type < 0 || type > 25
    errors << "#{context}: invalid action type #{type} (must be 0-25)"
  end
  
  # Validate parameters based on type
  case type
  when 4, 5, 6, 10  # CC-related actions
    unless action['cc'].is_a?(Integer) && action['cc'] >= 0 && action['cc'] <= 127
      errors << "#{context}: requires 'cc' (0-127)"
    end
    if action['value'] && !(action['value'] >= 0 && action['value'] <= 127)
      errors << "#{context}: 'value' must be 0-127"
    end
  when 7, 8  # Note actions
    unless action['note'].is_a?(Integer) && action['note'] >= 0 && action['note'] <= 127
      errors << "#{context}: requires 'note' (0-127)"
    end
  when 2, 3, 9  # Target actions (program set, scene set, send PC)
    unless action['number'].is_a?(Integer) && action['number'] >= 0 && action['number'] <= 127
      errors << "#{context}: requires 'number' (0-127)"
    end
  when 11  # Multi-randomize
    unless action['num_ccs'].is_a?(Integer) && action['num_ccs'] >= 1 && action['num_ccs'] <= 8
      errors << "#{context}: requires 'num_ccs' (1-8)"
    end
    unless action['cc_numbers'].is_a?(Array)
      errors << "#{context}: requires 'cc_numbers' array"
    end
  end
end

def validate_scene(scene_file)
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

  # Manual validation (more reliable than json-schema gem)
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
    errors << "program_number must be 0-127" unless pn.is_a?(Integer) && pn >= 0 && pn <= 127
  end
  
  if scene_data['touchwheel_mode']
    unless ['buttons', 'program_change', 'continuous'].include?(scene_data['touchwheel_mode'])
      errors << "touchwheel_mode must be 'buttons', 'program_change', or 'continuous'"
    end
  end
  
  if scene_data['touchwheel_style']
    unless ['odometer', 'endless'].include?(scene_data['touchwheel_style'])
      errors << "touchwheel_style must be 'odometer' or 'endless'"
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
  
  # Validate buttons
  ['button_left', 'button_right', 'button_both'].each do |btn|
    if scene_data[btn]
      if scene_data[btn].length > 4
        errors << "#{btn} has #{scene_data[btn].length} actions (max 4)"
      end
      
      scene_data[btn]&.each_with_index do |action, idx|
        validate_action(action, "#{btn} action #{idx}", errors)
      end
    end
  end

  if errors.empty?
    puts "✅ #{File.basename(scene_file)} is valid"
    
    # Additional semantic checks
    warnings = []
    
    # Check touchpad count
    if scene_data['touchpads']&.length != 12
      warnings << "Expected exactly 12 touchpads, found #{scene_data['touchpads']&.length || 0}"
    end
    
    # Check action chain lengths
    scene_data['touchpads']&.each_with_index do |pad, idx|
      if pad['actions']&.length > 4
        warnings << "Pad #{idx} has #{pad['actions'].length} actions (max 4)"
      end
    end
    
    ['button_left', 'button_right', 'button_both'].each do |btn|
      if scene_data[btn]&.length > 4
        warnings << "#{btn} has #{scene_data[btn].length} actions (max 4)"
      end
    end
    
    # Check for action type validity
    scene_data['touchpads']&.each_with_index do |pad, idx|
      pad['actions']&.each_with_index do |action, aidx|
        type = action['type']
        if type.nil? || type < 0 || type > 25
          warnings << "Pad #{idx} action #{aidx} has invalid type: #{type}"
        end
      end
    end
    
    unless warnings.empty?
      puts "\n⚠️  Warnings:"
      warnings.each { |w| puts "   - #{w}" }
    end
    
    # Print summary
    puts "\nScene Summary:"
    puts "  Name: #{scene_data['name']}"
    puts "  Program: #{scene_data['program_number']}"
    puts "  Send PC: #{scene_data['send_pc_on_change']}"
    puts "  Touchwheel: #{scene_data['touchwheel_mode']}"
    
    active_pads = scene_data['touchpads']&.count { |p| p['enabled'] } || 0
    puts "  Active touchpads: #{active_pads}/12"
    
    return true
  else
    puts "❌ Validation failed for #{File.basename(scene_file)}:"
    errors.each { |error| puts "   - #{error}" }
    return false
  end
end

if ARGV.empty?
  puts "Usage: ruby validate_scene.rb <scene_file.json>"
  puts "       ruby validate_scene.rb scenes/*.json"
  exit 1
end

results = ARGV.map { |file| validate_scene(file) }

if results.all?
  puts "\n✅ All #{results.length} scene(s) valid!"
  exit 0
else
  failed = results.count(false)
  puts "\n❌ #{failed}/#{results.length} scene(s) failed validation"
  exit 1
end

