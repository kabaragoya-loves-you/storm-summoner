#!/usr/bin/env ruby
# Compress binary assets for LittleFS storage
# Usage: ruby compress_assets.rb [input.bin ...] 
#        ruby compress_assets.rb --all   (compress all .bin in images/)
#
# Outputs .bin.z files (zlib compressed, no gzip header for smaller size)

require 'zlib'

def compress_file(input_path)
  output_path = input_path + '.z'
  
  data = File.binread(input_path)
  original_size = data.bytesize
  
  # Use zlib deflate (raw, no gzip header)
  compressed = Zlib::Deflate.deflate(data, Zlib::BEST_COMPRESSION)
  compressed_size = compressed.bytesize
  
  # Prepend original size (4 bytes, little-endian) for decompression
  File.open(output_path, 'wb') do |f|
    f.write([original_size].pack('L<'))
    f.write(compressed)
  end
  
  ratio = (compressed_size.to_f / original_size * 100).round(1)
  savings = original_size - compressed_size - 4  # -4 for size header
  
  puts "#{File.basename(input_path)}: #{original_size} -> #{compressed_size + 4} bytes (#{ratio}%, saved #{savings} bytes)"
  
  { input: input_path, output: output_path, original: original_size, compressed: compressed_size + 4 }
end

#=============================================================================

if ARGV.empty?
  puts "Usage: ruby compress_assets.rb [input.bin ...]"
  puts "       ruby compress_assets.rb --all"
  exit 1
end

files = if ARGV[0] == '--all'
  Dir.glob('images/*.bin').reject { |f| f.end_with?('.bin.z') }
else
  ARGV
end

if files.empty?
  puts "No files to compress"
  exit 0
end

puts "Compressing #{files.length} files..."
puts ""

results = files.map { |f| compress_file(f) }

puts ""
total_original = results.sum { |r| r[:original] }
total_compressed = results.sum { |r| r[:compressed] }
total_savings = total_original - total_compressed

puts "=== Summary ==="
puts "Total original:   #{total_original} bytes (#{(total_original / 1024.0).round(1)} KB)"
puts "Total compressed: #{total_compressed} bytes (#{(total_compressed / 1024.0).round(1)} KB)"
puts "Total savings:    #{total_savings} bytes (#{(total_savings / 1024.0).round(1)} KB)"
puts "Compression ratio: #{(total_compressed.to_f / total_original * 100).round(1)}%"

