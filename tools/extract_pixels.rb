#!/usr/bin/env ruby
# Extract white pixel coordinates from a black & white image
# Usage: ruby extract_pixels.rb input.png output.bin [threshold]
#
# Binary format:
#   Header (8 bytes):
#     uint16_t width
#     uint16_t height  
#     uint32_t count
#   Data (count * 4 bytes):
#     uint16_t x, uint16_t y for each pixel

require 'chunky_png'

if ARGV.length < 2
  puts "Usage: ruby extract_pixels.rb input.png output.bin [threshold]"
  puts "  threshold: 0-255, pixels brighter than this are considered white (default: 128)"
  exit 1
end

input_file = ARGV[0]
output_file = ARGV[1]
threshold = (ARGV[2] || 128).to_i

puts "Loading #{input_file}..."
img = ChunkyPNG::Image.from_file(input_file)

puts "Image size: #{img.width}x#{img.height}"
puts "Threshold: #{threshold}"

# Extract white pixel coordinates
coords = []
for y in 0...img.height
  for x in 0...img.width
    pixel = img[x, y]
    r = ChunkyPNG::Color.r(pixel)
    g = ChunkyPNG::Color.g(pixel)
    b = ChunkyPNG::Color.b(pixel)
    
    # Check if pixel is "white" (above threshold)
    brightness = (r + g + b) / 3
    if brightness > threshold
      coords << [x, y]
    end
  end
end

puts "Found #{coords.length} white pixels"

# Write binary file
File.open(output_file, 'wb') do |f|
  # Header: width (u16), height (u16), count (u32)
  f.write([img.width, img.height, coords.length].pack('S<S<L<'))
  
  # Data: x (u16), y (u16) for each coordinate
  coords.each do |x, y|
    f.write([x, y].pack('S<S<'))
  end
end

file_size = File.size(output_file)
puts "Generated #{output_file}"
puts "File size: #{file_size} bytes (8 byte header + #{coords.length * 4} bytes data)"
