#!/usr/bin/env ruby
# SVG to LVGL Vector Art Binary Converter
# Converts simple SVG path data to a compact binary format for lv_vector_art widget
#
# Usage: ruby svg_to_vector.rb <input.svg> <output.bin>
#
# Supported SVG path commands:
#   M/m - moveto (absolute/relative)
#   L/l - lineto (absolute/relative)
#   H/h - horizontal line (absolute/relative)
#   V/v - vertical line (absolute/relative)
#   Z/z - close path
#
# Binary format:
#   Header (12 bytes):
#     uint16_t version (1)
#     uint16_t width, height (viewbox)
#     uint16_t shape_count
#     uint16_t reserved
#     uint32_t shape_table_offset
#
#   Shape Entry (variable):
#     uint8_t  name_len
#     char     name[name_len]
#     uint8_t  r, g, b, a
#     uint16_t point_count
#     int16_t  points[point_count * 2]

require 'rexml/document'

# Parse SVG path 'd' attribute into array of sub-paths (each sub-path is an array of points)
def parse_svg_path(d)
  subpaths = []
  current_subpath = []
  current_x = 0.0
  current_y = 0.0
  start_x = 0.0
  start_y = 0.0
  
  # Tokenize the path data
  # Handle negative numbers and commas properly
  tokens = d.scan(/([MmLlHhVvZzCcSsQqTtAa])|(-?\d+\.?\d*(?:e[+-]?\d+)?)/i).flatten.compact
  
  i = 0
  current_cmd = nil
  
  while i < tokens.length
    token = tokens[i]
    
    # Check if it's a command letter
    if token =~ /^[MmLlHhVvZzCcSsQqTtAa]$/
      current_cmd = token
      i += 1
      next
    end
    
    case current_cmd
    when 'M' # Absolute moveto - starts a new sub-path
      # Save current subpath if it has points
      if current_subpath.length >= 3
        subpaths << current_subpath
      end
      current_subpath = []
      
      x = tokens[i].to_f
      y = tokens[i + 1].to_f
      current_x = x
      current_y = y
      start_x = x
      start_y = y
      current_subpath << [current_x, current_y]
      i += 2
      current_cmd = 'L' # Subsequent coordinates are lineto
      
    when 'm' # Relative moveto - starts a new sub-path
      # Save current subpath if it has points
      if current_subpath.length >= 3
        subpaths << current_subpath
      end
      current_subpath = []
      
      x = tokens[i].to_f
      y = tokens[i + 1].to_f
      current_x += x
      current_y += y
      start_x = current_x
      start_y = current_y
      current_subpath << [current_x, current_y]
      i += 2
      current_cmd = 'l' # Subsequent coordinates are lineto
      
    when 'L' # Absolute lineto
      x = tokens[i].to_f
      y = tokens[i + 1].to_f
      current_x = x
      current_y = y
      current_subpath << [current_x, current_y]
      i += 2
      
    when 'l' # Relative lineto
      x = tokens[i].to_f
      y = tokens[i + 1].to_f
      current_x += x
      current_y += y
      current_subpath << [current_x, current_y]
      i += 2
      
    when 'H' # Absolute horizontal line
      x = tokens[i].to_f
      current_x = x
      current_subpath << [current_x, current_y]
      i += 1
      
    when 'h' # Relative horizontal line
      x = tokens[i].to_f
      current_x += x
      current_subpath << [current_x, current_y]
      i += 1
      
    when 'V' # Absolute vertical line
      y = tokens[i].to_f
      current_y = y
      current_subpath << [current_x, current_y]
      i += 1
      
    when 'v' # Relative vertical line
      y = tokens[i].to_f
      current_y += y
      current_subpath << [current_x, current_y]
      i += 1
      
    when 'Z', 'z' # Close path
      # Close the current subpath
      current_x = start_x
      current_y = start_y
      # Add close point if needed
      if current_subpath.length > 0
        first = current_subpath.first
        last = current_subpath.last
        if (last[0] - first[0]).abs > 0.01 || (last[1] - first[1]).abs > 0.01
          current_subpath << [first[0], first[1]]
        end
      end
      # Don't consume a token for Z - but don't save yet, wait for next M/m
      
    when 'C', 'c', 'S', 's', 'Q', 'q', 'T', 't', 'A', 'a'
      # Skip unsupported curve commands
      puts "  Warning: Unsupported command '#{current_cmd}' - skipping"
      skip = case current_cmd.upcase
             when 'C' then 6
             when 'S' then 4
             when 'Q' then 4
             when 'T' then 2
             when 'A' then 7
             else 0
             end
      i += skip
      
    else
      i += 1
    end
  end
  
  # Don't forget the last subpath
  if current_subpath.length >= 3
    subpaths << current_subpath
  end
  
  subpaths
end

# Parse color from SVG fill attribute
def parse_color(fill)
  return [0, 0, 0, 255] if fill.nil? || fill.empty? || fill == 'none'
  
  # Handle hex colors
  if fill =~ /^#([0-9a-fA-F]{6})$/
    r = $1[0..1].to_i(16)
    g = $1[2..3].to_i(16)
    b = $1[4..5].to_i(16)
    return [r, g, b, 255]
  elsif fill =~ /^#([0-9a-fA-F]{3})$/
    r = ($1[0] * 2).to_i(16)
    g = ($1[1] * 2).to_i(16)
    b = ($1[2] * 2).to_i(16)
    return [r, g, b, 255]
  end
  
  # Handle rgb()
  if fill =~ /rgb\s*\(\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*\)/
    return [$1.to_i, $2.to_i, $3.to_i, 255]
  end
  
  # Handle rgba()
  if fill =~ /rgba\s*\(\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*,\s*([\d.]+)\s*\)/
    return [$1.to_i, $2.to_i, $3.to_i, ($4.to_f * 255).to_i]
  end
  
  # Default to black
  [0, 0, 0, 255]
end

# Parse viewBox attribute
def parse_viewbox(viewbox)
  return [0, 0, 240, 240] if viewbox.nil? || viewbox.empty?
  
  parts = viewbox.split(/[\s,]+/).map(&:to_f)
  if parts.length >= 4
    [parts[0], parts[1], parts[2], parts[3]]
  else
    [0, 0, 240, 240]
  end
end

def convert_svg_to_binary(input_path, output_path)
  puts "Loading SVG: #{input_path}"
  
  doc = REXML::Document.new(File.read(input_path))
  
  # Get viewBox from svg element
  svg = doc.root
  viewbox = parse_viewbox(svg.attributes['viewBox'])
  width = viewbox[2].to_i
  height = viewbox[3].to_i
  
  puts "ViewBox: #{width}x#{height}"
  
  # Extract all path elements
  shapes = []
  path_index = 0
  
  doc.elements.each('//path') do |path|
    d = path.attributes['d']
    next if d.nil? || d.empty?
    
    fill = path.attributes['fill'] || '#000000'
    fill_rule = path.attributes['fill-rule'] || 'nonzero'
    base_id = path.attributes['id'] || "path#{path_index}"
    
    subpaths = parse_svg_path(d)
    next if subpaths.empty?
    
    color = parse_color(fill)
    
    # Each sub-path becomes a separate shape
    # For compound paths: first sub-path is outer boundary, rest are holes
    # (This applies to both evenodd and nonzero - we're decomposing, not ray-casting)
    subpaths.each_with_index do |points, sub_idx|
      shape_id = subpaths.length > 1 ? "#{base_id}_#{sub_idx}" : base_id
      is_hole = sub_idx > 0  # All sub-paths after the first are holes
      
      # For holes, set alpha to 0 to indicate they should be drawn transparent
      shape_color = is_hole ? [color[0], color[1], color[2], 0] : color
      
      shapes << {
        name: shape_id,
        color: shape_color,
        points: points
      }
      
      hole_marker = is_hole ? " [HOLE]" : ""
      puts "  Shape '#{shape_id}': #{points.length} points, color=#{'%02X%02X%02X' % color[0..2]}#{hole_marker}"
    end
    
    path_index += 1
  end
  
  if shapes.empty?
    puts "Error: No valid paths found in SVG"
    exit 1
  end
  
  puts "Found #{shapes.length} shapes"
  
  # Build binary data
  version = 1
  shape_count = shapes.length
  # Header: version(2) + width(2) + height(2) + shape_count(2) + reserved(2) + offset(4) = 14 bytes
  header_size = 14
  
  # Calculate shape table offset (right after header)
  shape_table_offset = header_size
  
  # Build shape data
  shape_data = []
  shapes.each do |shape|
    name_bytes = shape[:name].bytes
    name_len = [name_bytes.length, 255].min
    
    entry = []
    entry << [name_len].pack('C')
    entry << name_bytes[0...name_len].pack('C*')
    entry << shape[:color].pack('CCCC')
    entry << [shape[:points].length].pack('S<')
    
    # Pack points as int16 pairs
    shape[:points].each do |pt|
      x = [[pt[0].round, -32768].max, 32767].min
      y = [[pt[1].round, -32768].max, 32767].min
      entry << [x, y].pack('s<s<')
    end
    
    shape_data << entry.join
  end
  
  # Write binary file
  File.open(output_path, 'wb') do |f|
    # Header
    f.write([version].pack('S<'))                    # version
    f.write([width, height].pack('S<S<'))            # width, height
    f.write([shape_count].pack('S<'))                # shape_count
    f.write([0].pack('S<'))                          # reserved
    f.write([shape_table_offset].pack('L<'))         # shape_table_offset
    
    # Shape data
    shape_data.each do |data|
      f.write(data)
    end
  end
  
  file_size = File.size(output_path)
  total_points = shapes.sum { |s| s[:points].length }
  
  puts "\n=== Conversion Complete ==="
  puts "Output: #{output_path}"
  puts "File size: #{file_size} bytes"
  puts "Shapes: #{shape_count}"
  puts "Total points: #{total_points}"
  puts "Viewbox: #{width}x#{height}"
end

# Main
if ARGV.length != 2
  puts "Usage: ruby svg_to_vector.rb <input.svg> <output.bin>"
  puts ""
  puts "Converts simple SVG paths to LVGL vector art binary format."
  puts ""
  puts "Supported path commands: M, m, L, l, H, h, V, v, Z, z"
  puts "NOT supported: curves (C, S, Q, T, A)"
  exit 1
end

input_path = ARGV[0]
output_path = ARGV[1]

unless File.exist?(input_path)
  puts "Error: Input file not found: #{input_path}"
  exit 1
end

convert_svg_to_binary(input_path, output_path)
