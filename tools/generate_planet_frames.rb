#!/usr/bin/env ruby
# Generate pre-rendered planet rotation frames as sparse pixel data
# Usage: ruby generate_planet_frames.rb texture.png output.bin [options]
#
# Options:
#   --diameter N     Planet diameter in pixels (default: 80)
#   --frames N       Number of rotation frames (default: 36)
#   --ambient F      Ambient light 0.0-1.0 (default: 0.3)
#
# Binary format:
#   Header (16 bytes):
#     uint16_t diameter
#     uint16_t num_frames
#     uint16_t reserved1
#     uint16_t reserved2
#     uint32_t frame_table_offset (always 16)
#     uint32_t pixel_data_offset
#
#   Frame table (num_frames * 8 bytes each):
#     uint32_t pixel_data_offset (from start of pixel data section)
#     uint32_t pixel_count
#
#   Pixel data (variable):
#     For each pixel: uint8_t x, uint8_t y, uint8_t r, uint8_t g, uint8_t b

require 'chunky_png'

#=============================================================================
# Configuration (can be overridden by command line)
#=============================================================================

DEFAULT_DIAMETER = 80
DEFAULT_FRAMES = 36
DEFAULT_AMBIENT = 0.3

# Light direction (normalized)
LIGHT_X = 0.5
LIGHT_Y = -0.3
LIGHT_Z = 0.8

#=============================================================================
# Parse arguments
#=============================================================================

if ARGV.length < 2
  puts "Usage: ruby generate_planet_frames.rb texture.png output.bin [options]"
  puts ""
  puts "Options:"
  puts "  --diameter N     Planet diameter in pixels (default: #{DEFAULT_DIAMETER})"
  puts "  --frames N       Number of rotation frames (default: #{DEFAULT_FRAMES})"
  puts "  --ambient F      Ambient light 0.0-1.0 (default: #{DEFAULT_AMBIENT})"
  exit 1
end

texture_file = ARGV[0]
output_file = ARGV[1]

diameter = DEFAULT_DIAMETER
num_frames = DEFAULT_FRAMES
ambient = DEFAULT_AMBIENT

i = 2
while i < ARGV.length
  case ARGV[i]
  when '--diameter'
    diameter = ARGV[i + 1].to_i
    i += 2
  when '--frames'
    num_frames = ARGV[i + 1].to_i
    i += 2
  when '--ambient'
    ambient = ARGV[i + 1].to_f
    i += 2
  else
    i += 1
  end
end

radius = diameter / 2.0

# Normalize light direction
light_len = Math.sqrt(LIGHT_X * LIGHT_X + LIGHT_Y * LIGHT_Y + LIGHT_Z * LIGHT_Z)
light = [LIGHT_X / light_len, LIGHT_Y / light_len, LIGHT_Z / light_len]

#=============================================================================
# Load texture
#=============================================================================

puts "Loading texture: #{texture_file}"
texture = ChunkyPNG::Image.from_file(texture_file)
tex_width = texture.width
tex_height = texture.height
puts "Texture size: #{tex_width}x#{tex_height}"

#=============================================================================
# Render frames
#=============================================================================

puts "Rendering #{num_frames} frames at #{diameter}px diameter..."
puts "Ambient light: #{ambient}"

frames = []

num_frames.times do |frame_idx|
  rotation = (frame_idx.to_f / num_frames) * 2 * Math::PI
  pixels = []
  
  # Render sphere
  diameter.times do |py|
    diameter.times do |px|
      # Map pixel to sphere coordinates (-1 to 1)
      sx = (px - radius + 0.5) / radius
      sy = (py - radius + 0.5) / radius
      
      # Check if inside sphere
      dist_sq = sx * sx + sy * sy
      next if dist_sq > 1.0
      
      # Calculate z coordinate on sphere surface
      sz = Math.sqrt(1.0 - dist_sq)
      
      # Calculate normal (same as position for unit sphere)
      nx, ny, nz = sx, sy, sz
      
      # Calculate diffuse lighting
      diffuse = nx * light[0] + ny * light[1] + nz * light[2]
      diffuse = [diffuse, 0.0].max
      
      # Total lighting
      lighting = ambient + (1.0 - ambient) * diffuse
      lighting = [lighting, 1.0].min
      
      # Calculate texture coordinates (equirectangular projection)
      # Apply rotation around Y axis
      rotated_x = sx * Math.cos(rotation) + sz * Math.sin(rotation)
      rotated_z = -sx * Math.sin(rotation) + sz * Math.cos(rotation)
      
      # Convert to spherical coordinates
      theta = Math.atan2(rotated_x, rotated_z)  # longitude
      phi = Math.asin(sy)                        # latitude
      
      # Map to texture coordinates
      u = (theta / Math::PI + 1.0) / 2.0  # 0 to 1
      v = (phi / (Math::PI / 2) + 1.0) / 2.0  # 0 to 1
      
      # Sample texture
      tex_x = (u * tex_width).to_i % tex_width
      tex_y = ((1.0 - v) * tex_height).to_i % tex_height
      
      pixel = texture[tex_x, tex_y]
      r = ChunkyPNG::Color.r(pixel)
      g = ChunkyPNG::Color.g(pixel)
      b = ChunkyPNG::Color.b(pixel)
      
      # Apply lighting
      r = (r * lighting).round.clamp(0, 255)
      g = (g * lighting).round.clamp(0, 255)
      b = (b * lighting).round.clamp(0, 255)
      
      # Skip very dark pixels (optional, saves space)
      next if r < 8 && g < 8 && b < 8
      
      pixels << { x: px, y: py, r: r, g: g, b: b }
    end
  end
  
  frames << pixels
  print "." if (frame_idx + 1) % 10 == 0
end
puts ""

#=============================================================================
# Write binary file
#=============================================================================

puts "Writing #{output_file}..."

# Calculate sizes
header_size = 16
frame_table_size = num_frames * 8
pixel_data_offset = header_size + frame_table_size

# Build frame table and pixel data
frame_table = []
pixel_data = []
current_offset = 0

frames.each do |pixels|
  frame_table << { offset: current_offset, count: pixels.length }
  
  pixels.each do |p|
    pixel_data << [p[:x], p[:y], p[:r], p[:g], p[:b]].pack('CCCCC')
  end
  
  current_offset += pixels.length * 5
end

# Write file
File.open(output_file, 'wb') do |f|
  # Header
  f.write([
    diameter,
    num_frames,
    0,  # reserved
    0,  # reserved
    header_size,  # frame table offset
    pixel_data_offset  # pixel data offset
  ].pack('S<S<S<S<L<L<'))
  
  # Frame table
  frame_table.each do |entry|
    f.write([entry[:offset], entry[:count]].pack('L<L<'))
  end
  
  # Pixel data
  pixel_data.each { |data| f.write(data) }
end

# Statistics
total_pixels = frames.map(&:length).sum
avg_pixels = total_pixels / num_frames
file_size = File.size(output_file)

puts ""
puts "=== Generation Complete ==="
puts "Output: #{output_file}"
puts "File size: #{file_size} bytes (#{(file_size / 1024.0).round(1)} KB)"
puts "Frames: #{num_frames}"
puts "Diameter: #{diameter}px"
puts "Avg pixels/frame: #{avg_pixels}"
puts "Total pixels: #{total_pixels}"

