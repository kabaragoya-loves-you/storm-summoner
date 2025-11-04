#!/usr/bin/env ruby
# frozen_string_literal: true

require 'json'
require 'fileutils'

def extract_scene_info(scene_file)
  data = JSON.parse(File.read(scene_file))
  
  # Extract index from filename (scene_001.json -> 0)
  filename = File.basename(scene_file)
  if filename =~ /scene_(\d{3})\.json/
    index = $1.to_i - 1
  else
    puts "⚠️  Warning: #{filename} doesn't match scene_XXX.json pattern"
    return nil
  end
  
  {
    'index' => index,
    'name' => data['name'] || "Scene #{index + 1}",
    'filename' => filename
  }
rescue JSON::ParserError => e
  puts "❌ Error parsing #{scene_file}: #{e.message}"
  nil
rescue => e
  puts "❌ Error processing #{scene_file}: #{e.message}"
  nil
end

def build_manifest(scenes_dir, output_file = nil)
  unless Dir.exist?(scenes_dir)
    puts "❌ Error: Directory not found: #{scenes_dir}"
    return false
  end
  
  # Find all scene JSON files
  scene_files = Dir.glob(File.join(scenes_dir, 'scene_*.json')).sort
  
  if scene_files.empty?
    puts "❌ Error: No scene_*.json files found in #{scenes_dir}"
    return false
  end
  
  puts "Found #{scene_files.length} scene file(s)..."
  
  # Extract info from each scene
  scenes = scene_files.map { |file| extract_scene_info(file) }.compact
  
  if scenes.empty?
    puts "❌ Error: No valid scenes found"
    return false
  end
  
  # Sort by index
  scenes.sort_by! { |s| s['index'] }
  
  # Check for duplicate indices
  indices = scenes.map { |s| s['index'] }
  duplicates = indices.select { |i| indices.count(i) > 1 }.uniq
  unless duplicates.empty?
    puts "❌ Error: Duplicate scene indices found: #{duplicates.join(', ')}"
    return false
  end
  
  # Check for index gaps (warning only)
  (0...scenes.length).each do |i|
    expected = i
    actual = scenes[i]['index']
    if actual != expected
      puts "⚠️  Warning: Scene index gap - expected #{expected}, found #{actual}"
    end
  end
  
  # Build manifest
  manifest = {
    'scenes' => scenes
  }
  
  # Determine output path
  output_path = output_file || File.join(scenes_dir, 'manifest.json')
  
  # Write manifest
  File.write(output_path, JSON.pretty_generate(manifest))
  
  puts "✅ Generated manifest with #{scenes.length} scene(s)"
  puts "   Output: #{output_path}"
  puts ""
  puts "Scene list:"
  scenes.each do |scene|
    puts "  #{scene['index'].to_s.rjust(3)}: #{scene['name']} (#{scene['filename']})"
  end
  
  true
rescue => e
  puts "❌ Error: #{e.message}"
  puts e.backtrace.take(5).join("\n")
  false
end

if ARGV.empty?
  puts "Usage: ruby build_scene_manifest.rb <scenes_directory> [output_file]"
  puts ""
  puts "Examples:"
  puts "  ruby build_scene_manifest.rb scenes/"
  puts "  ruby build_scene_manifest.rb scenes/ custom_manifest.json"
  puts ""
  puts "This will scan for scene_*.json files and generate manifest.json"
  exit 1
end

scenes_dir = ARGV[0]
output_file = ARGV[1]

success = build_manifest(scenes_dir, output_file)
exit(success ? 0 : 1)

