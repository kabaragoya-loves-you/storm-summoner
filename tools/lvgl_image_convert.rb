#!/usr/bin/env ruby
# LVGL 9.x Image Converter
# Converts PNG/JPG images to LVGL binary format (RGB565 default)
#
# Usage: ruby lvgl_image_convert.rb input.png [output.bin]
#
# Requires: gem install chunky_png (for PNG)
#           gem install mini_magick (for JPG and other formats)

require 'fileutils'

# LVGL 9.x color formats
COLOR_FORMATS = {
  'RGB888' => 0x0F,
  'ARGB8888' => 0x10,
  'RGB565' => 0x07,
}.freeze

LVGL_MAGIC = 0x19

def convert_image(input_path, output_path, color_format: 'RGB888')
  unless File.exist?(input_path)
    puts "Error: Input file not found: #{input_path}"
    exit 1
  end

  ext = File.extname(input_path).downcase

  if ext == '.png'
    convert_png(input_path, output_path, color_format)
  else
    convert_with_mini_magick(input_path, output_path, color_format)
  end
end

def convert_png(input_path, output_path, color_format)
  begin
    require 'chunky_png'
  rescue LoadError
    puts "Error: chunky_png gem not installed. Run: gem install chunky_png"
    exit 1
  end

  puts "Loading PNG: #{input_path}"
  img = ChunkyPNG::Image.from_file(input_path)
  w, h = img.width, img.height
  puts "Dimensions: #{w}x#{h}"

  write_lvgl_bin(output_path, w, h, color_format) do |file|
    h.times do |y|
      w.times do |x|
        pixel = img[x, y]
        r = ChunkyPNG::Color.r(pixel)
        g = ChunkyPNG::Color.g(pixel)
        b = ChunkyPNG::Color.b(pixel)
        a = ChunkyPNG::Color.a(pixel)

        case color_format
        when 'RGB888'
          file.write([b, g, r].pack('CCC'))  # BGR order for LVGL
        when 'ARGB8888'
          file.write([b, g, r, a].pack('CCCC'))  # BGRA order
        when 'RGB565'
          rgb565 = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
          file.write([rgb565].pack('v'))  # Little-endian 16-bit
        end
      end
    end
  end
end

def convert_jpg(input_path, output_path, color_format)
  # Try using ruby-vips first (fast, handles many formats)
  begin
    require 'vips'
    puts "Loading JPG with ruby-vips: #{input_path}"
    img = Vips::Image.new_from_file(input_path)
    w, h = img.width, img.height
    puts "Dimensions: #{w}x#{h}"

    # Get raw RGB data
    rgb_data = img.colourspace(:srgb).cast(:uchar).write_to_memory

    write_lvgl_bin(output_path, w, h, color_format) do |file|
      (0...rgb_data.bytesize).step(3) do |i|
        r, g, b = rgb_data.getbyte(i), rgb_data.getbyte(i + 1), rgb_data.getbyte(i + 2)

        case color_format
        when 'RGB888'
          file.write([b, g, r].pack('CCC'))
        when 'ARGB8888'
          file.write([b, g, r, 255].pack('CCCC'))
        when 'RGB565'
          rgb565 = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
          file.write([rgb565].pack('v'))
        end
      end
    end
    return
  rescue LoadError
    # ruby-vips not available, try mini_magick
  end

  # Try mini_magick (requires ImageMagick)
  begin
    require 'mini_magick'
    puts "Loading image with mini_magick: #{input_path}"
    img = MiniMagick::Image.open(input_path)
    w, h = img.width, img.height
    puts "Dimensions: #{w}x#{h}"

    pixels = img.get_pixels.flatten

    write_lvgl_bin(output_path, w, h, color_format) do |file|
      (0...pixels.length).step(3) do |i|
        r, g, b = pixels[i], pixels[i + 1], pixels[i + 2]

        case color_format
        when 'RGB888'
          file.write([b, g, r].pack('CCC'))
        when 'ARGB8888'
          file.write([b, g, r, 255].pack('CCCC'))
        when 'RGB565'
          rgb565 = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
          file.write([rgb565].pack('v'))
        end
      end
    end
    return
  rescue LoadError
    # mini_magick not available
  rescue MiniMagick::Error => e
    if e.message.include?('executable not found')
      # ImageMagick not installed
    else
      raise
    end
  end

  # No image library available for JPG
  puts ""
  puts "Error: Cannot load JPG file - no suitable library available."
  puts ""
  puts "Options:"
  puts "  1. Convert your JPG to PNG first (use Windows Photos, Paint, or online tool)"
  puts "     Then run: ruby #{$0} your_image.png"
  puts ""
  puts "  2. Install ruby-vips (recommended, fast):"
  puts "     gem install ruby-vips"
  puts "     (Requires libvips: https://github.com/libvips/libvips/releases)"
  puts ""
  puts "  3. Install ImageMagick + mini_magick:"
  puts "     Download ImageMagick from https://imagemagick.org/script/download.php"
  puts "     gem install mini_magick"
  puts ""
  exit 1
end

def convert_with_mini_magick(input_path, output_path, color_format)
  convert_jpg(input_path, output_path, color_format)
end

def write_lvgl_bin(output_path, width, height, color_format)
  cf_value = COLOR_FORMATS[color_format]
  unless cf_value
    puts "Error: Unknown color format: #{color_format}"
    puts "Supported: #{COLOR_FORMATS.keys.join(', ')}"
    exit 1
  end

  bytes_per_pixel = case color_format
                    when 'RGB888' then 3
                    when 'ARGB8888' then 4
                    when 'RGB565' then 2
                    end

  stride = width * bytes_per_pixel

  File.open(output_path, 'wb') do |file|
    # LVGL 9.x lv_image_header_t (12 bytes total)
    # Bytes 0-3: magic(8), cf(8), flags(16)
    # Bytes 4-7: w(16), h(16)
    # Bytes 8-11: stride(16), reserved(16)

    header = [
      LVGL_MAGIC,
      cf_value,
      0, 0,  # flags (16-bit as two bytes)
    ].pack('CCCC')

    header += [width, height].pack('vv')  # 16-bit w, 16-bit h
    header += [stride, 0].pack('vv')      # 16-bit stride, 16-bit reserved

    file.write(header)

    yield file
  end

  puts "Output: #{output_path} (#{File.size(output_path)} bytes)"
  puts "Format: #{color_format}, #{width}x#{height}, #{bytes_per_pixel} bytes/pixel"
  puts "Header: 12 bytes, stride: #{stride}"
end

# Main
if ARGV.empty?
  puts "LVGL 9.x Image Converter"
  puts ""
  puts "Usage: ruby #{$0} <input_image> [output.bin] [--format FORMAT]"
  puts ""
  puts "Options:"
  puts "  --format FORMAT   Color format: RGB565 (default), RGB888, ARGB8888"
  puts ""
  puts "Examples:"
  puts "  ruby #{$0} earth.png"
  puts "  ruby #{$0} earth.png earth.bin"
  puts "  ruby #{$0} earth.jpg earth.bin --format RGB565"
  puts ""
  puts "Dependencies:"
  puts "  gem install chunky_png   # For PNG files"
  puts "  gem install mini_magick  # For JPG and other formats (requires ImageMagick)"
  exit 0
end

input_path = ARGV[0]
output_path = ARGV[1]
color_format = 'RGB565'

# Parse --format option
if idx = ARGV.index('--format')
  color_format = ARGV[idx + 1]&.upcase || 'RGB888'
  ARGV.delete_at(idx + 1)
  ARGV.delete_at(idx)
  output_path = ARGV[1]
end

# Default output path
output_path ||= input_path.sub(/\.\w+$/, '.bin')

convert_image(input_path, output_path, color_format: color_format)
puts "Done!"

