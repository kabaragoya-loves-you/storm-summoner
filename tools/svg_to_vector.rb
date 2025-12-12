#!/usr/bin/env ruby
# SVG to LVGL Vector Art Binary Converter
# Converts simple SVG path data to a compact binary format for lv_vector_art widget
#
# Usage:
#   Static:   ruby svg_to_vector.rb <input.svg> <output.bin>
#   Animated: ruby svg_to_vector.rb <input.zip> <output.bin> [fps]
#
# Supported SVG path commands:
#   M/m - moveto (absolute/relative)
#   L/l - lineto (absolute/relative)
#   H/h - horizontal line (absolute/relative)
#   V/v - vertical line (absolute/relative)
#   C/c - cubic Bezier curve (flattened to line segments)
#   Q/q - quadratic Bezier curve (flattened to line segments)
#   Z/z - close path
#
# Binary format (static, version 1):
#   Header (14 bytes):
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
#
# Binary format (animated, version 2):
#   Header (18 bytes):
#     uint16_t version (2)
#     uint16_t width, height (viewbox)
#     uint16_t frame_count
#     uint16_t fps
#     uint32_t reserved
#     uint32_t frame_table_offset
#
#   Frame Table:
#     uint32_t offsets[frame_count]  // byte offset to each frame's data
#
#   Frame Data (per frame):
#     uint16_t shape_count
#     Shape entries (same as static format)

require 'rexml/document'
require 'zip' if defined?(Zip) || begin
  require 'zip'
  true
rescue LoadError
  false
end

# Parse transform attribute - supports translate(x, y) and scale(sx [, sy])
def parse_transform(transform_str)
  return { translate_x: 0, translate_y: 0, scale_x: 1, scale_y: 1 } if transform_str.nil? || transform_str.empty?
  
  result = { translate_x: 0, translate_y: 0, scale_x: 1, scale_y: 1 }
  
  # Parse translate(x, y) or translate(x y)
  if transform_str =~ /translate\s*\(\s*(-?[\d.]+)[\s,]+(-?[\d.]+)\s*\)/
    result[:translate_x] = $1.to_f
    result[:translate_y] = $2.to_f
  elsif transform_str =~ /translate\s*\(\s*(-?[\d.]+)\s*\)/
    result[:translate_x] = $1.to_f
  end
  
  # Parse scale(sx, sy) or scale(s)
  if transform_str =~ /scale\s*\(\s*(-?[\d.]+)[\s,]+(-?[\d.]+)\s*\)/
    result[:scale_x] = $1.to_f
    result[:scale_y] = $2.to_f
  elsif transform_str =~ /scale\s*\(\s*(-?[\d.]+)\s*\)/
    result[:scale_x] = $1.to_f
    result[:scale_y] = $1.to_f
  end
  
  result
end

# Apply transform to a point
def apply_transform(x, y, transform)
  new_x = x * transform[:scale_x] + transform[:translate_x]
  new_y = y * transform[:scale_y] + transform[:translate_y]
  [new_x, new_y]
end

# Flatten a cubic Bezier curve to line segments
# p0, p1, p2, p3 are [x, y] arrays
# Returns array of points (not including p0, ending with p3)
def flatten_cubic_bezier(p0, p1, p2, p3, tolerance = 1.0)
  # Check if curve is flat enough using simple distance heuristic
  # Distance from control points to line p0->p3
  dx = p3[0] - p0[0]
  dy = p3[1] - p0[1]
  len_sq = dx * dx + dy * dy
  
  if len_sq < 0.0001
    # Degenerate case - start and end are same point
    return [p3]
  end
  
  len = Math.sqrt(len_sq)
  
  # Distance from p1 to line p0-p3
  d1 = ((p1[0] - p0[0]) * dy - (p1[1] - p0[1]) * dx).abs / len
  # Distance from p2 to line p0-p3
  d2 = ((p2[0] - p0[0]) * dy - (p2[1] - p0[1]) * dx).abs / len
  
  if d1 < tolerance && d2 < tolerance
    # Flat enough - just return endpoint
    return [p3]
  end
  
  # Subdivide using de Casteljau algorithm
  p01 = [(p0[0] + p1[0]) / 2.0, (p0[1] + p1[1]) / 2.0]
  p12 = [(p1[0] + p2[0]) / 2.0, (p1[1] + p2[1]) / 2.0]
  p23 = [(p2[0] + p3[0]) / 2.0, (p2[1] + p3[1]) / 2.0]
  p012 = [(p01[0] + p12[0]) / 2.0, (p01[1] + p12[1]) / 2.0]
  p123 = [(p12[0] + p23[0]) / 2.0, (p12[1] + p23[1]) / 2.0]
  p0123 = [(p012[0] + p123[0]) / 2.0, (p012[1] + p123[1]) / 2.0]
  
  # Recursively flatten both halves
  left = flatten_cubic_bezier(p0, p01, p012, p0123, tolerance)
  right = flatten_cubic_bezier(p0123, p123, p23, p3, tolerance)
  
  left + right
end

# Flatten a quadratic Bezier curve to line segments
def flatten_quadratic_bezier(p0, p1, p2, tolerance = 1.0)
  # Convert quadratic to cubic: cubic control points are:
  # cp1 = p0 + 2/3 * (p1 - p0)
  # cp2 = p2 + 2/3 * (p1 - p2)
  cp1 = [p0[0] + (2.0/3.0) * (p1[0] - p0[0]), p0[1] + (2.0/3.0) * (p1[1] - p0[1])]
  cp2 = [p2[0] + (2.0/3.0) * (p1[0] - p2[0]), p2[1] + (2.0/3.0) * (p1[1] - p2[1])]
  flatten_cubic_bezier(p0, cp1, cp2, p2, tolerance)
end

# Check if a path is a background rectangle (heuristic: short d attribute)
def is_background_path?(d)
  return false if d.nil?
  # Background paths from Rive are typically simple rectangles like "M0 0L198 0L198 198L0 198L0 0Z"
  d.length < 100
end

# Parse SVG path 'd' attribute into array of sub-paths (each sub-path is an array of points)
def parse_svg_path(d, transform = nil)
  transform ||= { translate_x: 0, translate_y: 0, scale_x: 1, scale_y: 1 }
  
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
      tx, ty = apply_transform(current_x, current_y, transform)
      current_subpath << [tx, ty]
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
      tx, ty = apply_transform(current_x, current_y, transform)
      current_subpath << [tx, ty]
      i += 2
      current_cmd = 'l' # Subsequent coordinates are lineto
      
    when 'L' # Absolute lineto
      x = tokens[i].to_f
      y = tokens[i + 1].to_f
      current_x = x
      current_y = y
      tx, ty = apply_transform(current_x, current_y, transform)
      current_subpath << [tx, ty]
      i += 2
      
    when 'l' # Relative lineto
      x = tokens[i].to_f
      y = tokens[i + 1].to_f
      current_x += x
      current_y += y
      tx, ty = apply_transform(current_x, current_y, transform)
      current_subpath << [tx, ty]
      i += 2
      
    when 'H' # Absolute horizontal line
      x = tokens[i].to_f
      current_x = x
      tx, ty = apply_transform(current_x, current_y, transform)
      current_subpath << [tx, ty]
      i += 1
      
    when 'h' # Relative horizontal line
      x = tokens[i].to_f
      current_x += x
      tx, ty = apply_transform(current_x, current_y, transform)
      current_subpath << [tx, ty]
      i += 1
      
    when 'V' # Absolute vertical line
      y = tokens[i].to_f
      current_y = y
      tx, ty = apply_transform(current_x, current_y, transform)
      current_subpath << [tx, ty]
      i += 1
      
    when 'v' # Relative vertical line
      y = tokens[i].to_f
      current_y += y
      tx, ty = apply_transform(current_x, current_y, transform)
      current_subpath << [tx, ty]
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
      
    when 'C' # Absolute cubic Bezier
      x1 = tokens[i].to_f
      y1 = tokens[i + 1].to_f
      x2 = tokens[i + 2].to_f
      y2 = tokens[i + 3].to_f
      x = tokens[i + 4].to_f
      y = tokens[i + 5].to_f
      
      p0 = [current_x, current_y]
      p1 = [x1, y1]
      p2 = [x2, y2]
      p3 = [x, y]
      
      points = flatten_cubic_bezier(p0, p1, p2, p3)
      points.each do |pt|
        tx, ty = apply_transform(pt[0], pt[1], transform)
        current_subpath << [tx, ty]
      end
      
      current_x = x
      current_y = y
      i += 6
      
    when 'c' # Relative cubic Bezier
      x1 = current_x + tokens[i].to_f
      y1 = current_y + tokens[i + 1].to_f
      x2 = current_x + tokens[i + 2].to_f
      y2 = current_y + tokens[i + 3].to_f
      x = current_x + tokens[i + 4].to_f
      y = current_y + tokens[i + 5].to_f
      
      p0 = [current_x, current_y]
      p1 = [x1, y1]
      p2 = [x2, y2]
      p3 = [x, y]
      
      points = flatten_cubic_bezier(p0, p1, p2, p3)
      points.each do |pt|
        tx, ty = apply_transform(pt[0], pt[1], transform)
        current_subpath << [tx, ty]
      end
      
      current_x = x
      current_y = y
      i += 6
      
    when 'Q' # Absolute quadratic Bezier
      x1 = tokens[i].to_f
      y1 = tokens[i + 1].to_f
      x = tokens[i + 2].to_f
      y = tokens[i + 3].to_f
      
      p0 = [current_x, current_y]
      p1 = [x1, y1]
      p2 = [x, y]
      
      points = flatten_quadratic_bezier(p0, p1, p2)
      points.each do |pt|
        tx, ty = apply_transform(pt[0], pt[1], transform)
        current_subpath << [tx, ty]
      end
      
      current_x = x
      current_y = y
      i += 4
      
    when 'q' # Relative quadratic Bezier
      x1 = current_x + tokens[i].to_f
      y1 = current_y + tokens[i + 1].to_f
      x = current_x + tokens[i + 2].to_f
      y = current_y + tokens[i + 3].to_f
      
      p0 = [current_x, current_y]
      p1 = [x1, y1]
      p2 = [x, y]
      
      points = flatten_quadratic_bezier(p0, p1, p2)
      points.each do |pt|
        tx, ty = apply_transform(pt[0], pt[1], transform)
        current_subpath << [tx, ty]
      end
      
      current_x = x
      current_y = y
      i += 4
      
    when 'S', 's', 'T', 't', 'A', 'a'
      # Skip remaining unsupported curve commands
      puts "  Warning: Unsupported command '#{current_cmd}' - skipping"
      skip = case current_cmd.upcase
             when 'S' then 4
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

# Parse viewBox attribute - handles both viewBox and width/height attributes
def parse_viewbox(svg)
  viewbox = svg.attributes['viewBox']
  
  if viewbox && !viewbox.empty?
    parts = viewbox.split(/[\s,]+/).map(&:to_f)
    if parts.length >= 4
      return [parts[0], parts[1], parts[2], parts[3]]
    end
  end
  
  # Fall back to width/height attributes
  width = svg.attributes['width']&.to_f || 240
  height = svg.attributes['height']&.to_f || 240
  [0, 0, width, height]
end

# Parse a single SVG and return shapes array and viewbox
def parse_svg(svg_content, skip_backgrounds: false)
  doc = REXML::Document.new(svg_content)
  
  # Get viewBox from svg element
  svg = doc.root
  viewbox = parse_viewbox(svg)
  width = viewbox[2].to_i
  height = viewbox[3].to_i
  
  # Extract all path elements
  shapes = []
  path_index = 0
  
  doc.elements.each('//path') do |path|
    d = path.attributes['d']
    next if d.nil? || d.empty?
    
    # Skip background paths if requested
    if skip_backgrounds && is_background_path?(d)
      puts "  Skipping background path (#{d.length} chars)"
      next
    end
    
    fill = path.attributes['fill'] || '#000000'
    fill_rule = path.attributes['fill-rule'] || 'nonzero'
    base_id = path.attributes['id'] || "path#{path_index}"
    transform = parse_transform(path.attributes['transform'])
    
    subpaths = parse_svg_path(d, transform)
    next if subpaths.empty?
    
    color = parse_color(fill)
    
    # Each sub-path becomes a separate shape
    # For compound paths: first sub-path is outer boundary, rest are holes
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
    end
    
    path_index += 1
  end
  
  { shapes: shapes, width: width, height: height }
end

# Build binary data for shapes (used by both static and animated formats)
def build_shape_data(shapes)
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
  shape_data
end

# Convert static SVG to binary
def convert_svg_to_binary(input_path, output_path)
  puts "Loading SVG: #{input_path}"
  
  result = parse_svg(File.read(input_path), skip_backgrounds: false)
  shapes = result[:shapes]
  width = result[:width]
  height = result[:height]
  
  puts "ViewBox: #{width}x#{height}"
  
  if shapes.empty?
    puts "Error: No valid paths found in SVG"
    exit 1
  end
  
  shapes.each do |shape|
    hole_marker = shape[:color][3] == 0 ? " [HOLE]" : ""
    puts "  Shape '#{shape[:name]}': #{shape[:points].length} points, color=#{'%02X%02X%02X' % shape[:color][0..2]}#{hole_marker}"
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
  shape_data = build_shape_data(shapes)
  
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

# Convert animated ZIP to binary
def convert_zip_to_binary(input_path, output_path, fps = 24)
  puts "Loading ZIP: #{input_path}"
  puts "Target FPS: #{fps}"
  
  unless defined?(Zip)
    puts "Error: rubyzip gem is required for ZIP support"
    puts "Install with: gem install rubyzip"
    exit 1
  end
  
  frames = []
  width = 0
  height = 0
  
  Zip::File.open(input_path) do |zip_file|
    # Get all SVG files sorted by name
    svg_entries = zip_file.entries
      .select { |e| e.name =~ /\.svg$/i && !e.name.include?('__MACOSX') }
      .sort_by { |e| e.name }
    
    if svg_entries.empty?
      puts "Error: No SVG files found in ZIP"
      exit 1
    end
    
    puts "Found #{svg_entries.length} SVG frames"
    
    svg_entries.each_with_index do |entry, idx|
      puts "  Processing frame #{idx}: #{entry.name}"
      
      svg_content = entry.get_input_stream.read
      result = parse_svg(svg_content, skip_backgrounds: true)
      
      if idx == 0
        width = result[:width]
        height = result[:height]
        puts "  ViewBox: #{width}x#{height}"
      end
      
      frames << result[:shapes]
      puts "    #{result[:shapes].length} shapes"
    end
  end
  
  if frames.empty?
    puts "Error: No valid frames found"
    exit 1
  end
  
  puts "\nBuilding animated binary..."
  
  # Build binary data
  version = 2
  frame_count = frames.length
  
  # Header: version(2) + width(2) + height(2) + frame_count(2) + fps(2) + reserved(4) + frame_table_offset(4) = 18 bytes
  header_size = 18
  
  # Frame table: frame_count * 4 bytes (uint32 offsets)
  frame_table_size = frame_count * 4
  
  # Frame table offset (right after header)
  frame_table_offset = header_size
  
  # Build frame data and calculate offsets
  frame_data_list = []
  frame_offsets = []
  current_offset = header_size + frame_table_size
  
  frames.each_with_index do |shapes, idx|
    frame_offsets << current_offset
    
    # Build frame data: shape_count(2) + shape_data
    shape_data = build_shape_data(shapes)
    frame_binary = [shapes.length].pack('S<') + shape_data.join
    frame_data_list << frame_binary
    
    current_offset += frame_binary.length
  end
  
  # Write binary file
  File.open(output_path, 'wb') do |f|
    # Header
    f.write([version].pack('S<'))                    # version
    f.write([width, height].pack('S<S<'))            # width, height
    f.write([frame_count].pack('S<'))                # frame_count
    f.write([fps].pack('S<'))                        # fps
    f.write([0].pack('L<'))                          # reserved
    f.write([frame_table_offset].pack('L<'))         # frame_table_offset
    
    # Frame table
    frame_offsets.each do |offset|
      f.write([offset].pack('L<'))
    end
    
    # Frame data
    frame_data_list.each do |data|
      f.write(data)
    end
  end
  
  file_size = File.size(output_path)
  total_shapes = frames.sum(&:length)
  total_points = frames.sum { |f| f.sum { |s| s[:points].length } }
  
  puts "\n=== Conversion Complete ==="
  puts "Output: #{output_path}"
  puts "File size: #{file_size} bytes"
  puts "Frames: #{frame_count}"
  puts "Total shapes: #{total_shapes}"
  puts "Total points: #{total_points}"
  puts "FPS: #{fps}"
  puts "Viewbox: #{width}x#{height}"
end

# Main
if ARGV.length < 2
  puts "Usage:"
  puts "  Static:   ruby svg_to_vector.rb <input.svg> <output.bin>"
  puts "  Animated: ruby svg_to_vector.rb <input.zip> <output.bin> [fps]"
  puts ""
  puts "Converts SVG paths to LVGL vector art binary format."
  puts "ZIP files containing numbered SVG frames (00.svg, 01.svg, etc.) are"
  puts "converted to an animated binary format."
  puts ""
  puts "Supported path commands: M, m, L, l, H, h, V, v, C, c, Q, q, Z, z"
  puts "Supported transforms: translate(x, y), scale(sx, sy)"
  puts "Curves (C, Q) are flattened to line segments automatically"
  puts "NOT supported: S, s, T, t, A, a (smooth curves and arcs)"
  exit 1
end

input_path = ARGV[0]
output_path = ARGV[1]
fps = (ARGV[2] || 24).to_i

unless File.exist?(input_path)
  puts "Error: Input file not found: #{input_path}"
  exit 1
end

# Detect input type by extension
if input_path =~ /\.zip$/i
  convert_zip_to_binary(input_path, output_path, fps)
else
  convert_svg_to_binary(input_path, output_path)
end
