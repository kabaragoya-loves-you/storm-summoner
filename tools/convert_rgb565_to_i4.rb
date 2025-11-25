#!/usr/bin/env ruby

# Convert RGB565 image arrays to I4 (4-bit grayscale) format
# Usage: ruby convert_rgb565_to_i4.rb <input_file> [output_file]

require 'optparse'

class RGB565ToI4Converter
  def initialize
    @verbose = false
    @preview = false
  end

  # Convert RGB565 color to 4-bit grayscale
  def rgb565_to_gray4(rgb565_value)
    # Extract RGB components from RGB565
    r = ((rgb565_value >> 11) & 0x1F) << 3  # 5 bits -> 8 bits
    g = ((rgb565_value >> 5) & 0x3F) << 2   # 6 bits -> 8 bits  
    b = (rgb565_value & 0x1F) << 3          # 5 bits -> 8 bits
    
    # Standard grayscale conversion (weighted average)
    # Using NTSC formula: 0.299*R + 0.587*G + 0.114*B
    gray = (0.299 * r + 0.587 * g + 0.114 * b).round
    
    # Convert to 4-bit (0-15 range)
    gray4 = (gray >> 4).clamp(0, 15)
    
    if @verbose
      puts "RGB565: 0x#{rgb565_value.to_s(16).upcase.rjust(4, '0')} -> " +
           "RGB(#{r}, #{g}, #{b}) -> Gray: #{gray} -> Gray4: #{gray4}"
    end
    
    gray4
  end

  # Parse C array from file
  def parse_c_array(filename)
    content = File.read(filename)
    
    # Find the image descriptor structure
    if content =~ /const\s+lv_image_dsc_t\s+(\w+)\s*=\s*{/m
      image_name = $1
      puts "Found image: #{image_name}" if @verbose
    else
      raise "Could not find lv_image_dsc_t structure in file"
    end
    
    # Extract width and height
    width = content[/\.w\s*=\s*(\d+)/, 1]&.to_i
    height = content[/\.h\s*=\s*(\d+)/, 1]&.to_i
    
    if width.nil? || height.nil?
      raise "Could not find width/height in image descriptor"
    end
    
    puts "Image dimensions: #{width}x#{height}" if @verbose
    
    # Check if the image format is RGB565
    format_match = content[/\.cf\s*=\s*(\w+)/, 1] || content[/\.header\.cf\s*=\s*(\w+)/, 1]
    if format_match && !format_match.include?("RGB565")
      puts "Warning: Image format appears to be #{format_match}, not RGB565"
      puts "Proceeding anyway, but results may be incorrect"
    end
    
    # Find the data array - could be uint16_t or uint8_t
    data_match = content.match(/static\s+const\s+uint16_t\s+\w+\[\]\s*=\s*{([^}]+)}/m)
    is_uint16 = true
    
    unless data_match
      # Try alternative uint16_t format
      data_match = content.match(/const\s+uint16_t\s+\w+\[\]\s*=\s*{([^}]+)}/m)
    end
    
    unless data_match
      # Try uint8_t format
      data_match = content.match(/static\s+const\s+uint8_t\s+\w+\[\]\s*=\s*{([^}]+)}/m)
      is_uint16 = false
    end
    
    unless data_match
      # Try alternative uint8_t format
      data_match = content.match(/const\s+.*uint8_t\s+\w+\[\]\s*=\s*{([^}]+)}/m)
      is_uint16 = false
    end
    
    unless data_match
      raise "Could not find RGB565 data array in file"
    end
    
    # Parse the hex values
    data_str = data_match[1]
    values = data_str.scan(/0x([0-9A-Fa-f]+)/).map { |m| m[0].to_i(16) }
    
    puts "Found #{values.length} values (#{is_uint16 ? 'uint16_t' : 'uint8_t'} format)" if @verbose
    
    # Convert uint8_t array to RGB565 values if needed
    if !is_uint16
      # Combine pairs of bytes into RGB565 values
      # Assuming little-endian format (low byte first)
      rgb565_values = []
      values.each_slice(2) do |low, high|
        if high.nil?
          puts "Warning: Odd number of bytes in uint8_t array"
          break
        end
        # Combine bytes: RGB565 = (high << 8) | low
        rgb565_values << ((high << 8) | low)
      end
      values = rgb565_values
      puts "Converted to #{values.length} RGB565 values" if @verbose
    end
    
    expected_size = width * height
    if values.length != expected_size
      puts "Warning: Expected #{expected_size} values but found #{values.length}"
    end
    
    {
      name: image_name,
      width: width,
      height: height,
      data: values
    }
  end

  # Convert RGB565 array to I4 array
  def convert_to_i4(rgb565_data)
    i4_data = []
    
    # Process pairs of pixels (2 pixels = 1 byte in I4 format)
    rgb565_data.each_slice(2) do |pixel1, pixel2|
      gray1 = rgb565_to_gray4(pixel1 || 0)
      gray2 = rgb565_to_gray4(pixel2 || 0)
      
      # Pack two 4-bit values into one byte (first pixel in high nibble)
      byte = (gray1 << 4) | gray2
      i4_data << byte
    end
    
    i4_data
  end

  # Generate C code for I4 image
  def generate_c_code(image_info, i4_data)
    name = image_info[:name]
    width = image_info[:width]
    height = image_info[:height]
    
    # Calculate data size for I4 (4 bits per pixel)
    data_size = (width * height + 1) / 2  # Round up for odd number of pixels
    
    code = <<~EOF
    #include "lvgl.h"
    
    // Converted from RGB565 to I4 (4-bit grayscale)
    // Original image: #{name} (#{width}x#{height})
    
    static const uint8_t #{name}_i4_data[] = {
    EOF
    
    # Write data in rows of 16 bytes
    i4_data.each_slice(16).with_index do |row, i|
      hex_values = row.map { |b| "0x#{b.to_s(16).upcase.rjust(2, '0')}" }.join(", ")
      code += "  #{hex_values},"
      code += " // #{i * 16}-#{[i * 16 + 15, i4_data.length - 1].min}\n"
    end
    
    code += <<~EOF
    };
    
    const lv_image_dsc_t #{name}_i4 = {
      .header = {
        .magic = LV_IMAGE_HEADER_MAGIC,
        .cf = LV_COLOR_FORMAT_I4,
        .flags = 0,
        .w = #{width},
        .h = #{height},
        .stride = (#{width} + 1) / 2,  // 4 bits per pixel
        .reserved_2 = 0
      },
      .data_size = #{data_size},
      .data = #{name}_i4_data,
      .reserved = NULL
    };
    EOF
    
    code
  end

  # Show preview of conversion
  def show_preview(rgb565_data, i4_data, width, height, count = 10)
    puts "\nPreview of first #{count} pixels:"
    puts "Index | RGB565  | R   G   B | Gray | I4"
    puts "------|---------|-----------|------|----"
    
    rgb565_data.first(count).each_with_index do |rgb565, i|
      r = ((rgb565 >> 11) & 0x1F) << 3
      g = ((rgb565 >> 5) & 0x3F) << 2
      b = (rgb565 & 0x1F) << 3
      gray = (0.299 * r + 0.587 * g + 0.114 * b).round
      gray4 = (gray >> 4).clamp(0, 15)
      
      puts "#{i.to_s.rjust(5)} | 0x#{rgb565.to_s(16).upcase.rjust(4, '0')} | " +
           "#{r.to_s.rjust(3)} #{g.to_s.rjust(3)} #{b.to_s.rjust(3)} | " +
           "#{gray.to_s.rjust(4)} | #{gray4.to_s.rjust(2)}"
    end
    
    puts "\nImage statistics:"
    puts "- Total pixels: #{width * height}"
    puts "- RGB565 data size: #{rgb565_data.length * 2} bytes"
    puts "- I4 data size: #{i4_data.length} bytes"
    puts "- Compression ratio: #{(100.0 * i4_data.length / (rgb565_data.length * 2)).round(1)}%"
  end

  def run(argv)
    options = {}
    
    parser = OptionParser.new do |opts|
      opts.banner = "Usage: #{$0} [options] <input_file> [output_file]"
      opts.separator ""
      opts.separator "Options:"
      
      opts.on("-v", "--verbose", "Enable verbose output") do
        @verbose = true
      end
      
      opts.on("-p", "--preview", "Show preview of conversion") do
        @preview = true
      end
      
      opts.on("-h", "--help", "Show this help message") do
        puts opts
        exit
      end
    end
    
    parser.parse!(argv)
    
    if argv.empty?
      puts parser
      exit 1
    end
    
    input_file = argv[0]
    output_file = argv[1] || input_file.sub(/\.(c|h)$/, '_i4.c')
    
    unless File.exist?(input_file)
      puts "Error: Input file '#{input_file}' not found"
      exit 1
    end
    
    begin
      # Parse the input file
      image_info = parse_c_array(input_file)
      
      # Convert to I4
      i4_data = convert_to_i4(image_info[:data])
      
      # Show preview if requested
      if @preview
        show_preview(image_info[:data], i4_data, image_info[:width], image_info[:height])
      end
      
      # Generate C code
      c_code = generate_c_code(image_info, i4_data)
      
      # Write output file
      File.write(output_file, c_code)
      
      puts "\nSuccessfully converted '#{input_file}' to '#{output_file}'"
      puts "Image: #{image_info[:name]} (#{image_info[:width]}x#{image_info[:height]})"
      puts "Format: RGB565 -> I4 (4-bit grayscale)"
      
    rescue => e
      puts "Error: #{e.message}"
      exit 1
    end
  end
end

# Run the converter
if __FILE__ == $0
  converter = RGB565ToI4Converter.new
  converter.run(ARGV)
end 