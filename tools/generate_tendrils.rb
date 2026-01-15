#!/usr/bin/env ruby
# Generate pre-computed plasma tendril animations with branching sub-tendrils
# Usage: ruby generate_tendrils.rb output.bin
#
# Binary format:
#   Header (8 bytes):
#     uint8_t  num_pads
#     uint8_t  num_frames
#     uint8_t  max_segments
#     uint8_t  reserved (padding)
#     uint32_t total_size (for validation)
#   Data (num_pads * num_frames * max_segments * 9 bytes):
#     int16_t x1, y1, x2, y2 (8 bytes)
#     uint8_t is_branch (1 byte)

#=============================================================================
# Configuration
#=============================================================================

DISPLAY_WIDTH = 240
DISPLAY_HEIGHT = 240
# Tendril convergence point - set to lizard's hand position on 240x240 display
# After updating, regenerate tendrils.bin and run compress_assets.rb
CENTER_X = 120  # TODO: Update to actual hand X position
CENTER_Y = 120  # TODO: Update to actual hand Y position

# Tendril parameters
NUM_PADS = 8
FRAMES_PER_TENDRIL = 24
SEGMENTS_PER_TENDRIL = 12
PAD_RADIUS_FACTOR = 0.49

# Branching parameters
BRANCH_POINTS = 3
SEGMENTS_PER_BRANCH = 4
BRANCH_ANGLE_SPREAD = 45.0
BRANCH_LENGTH_FACTOR = 0.3

# Noise settings
BASE_DISPLACEMENT = 25.0
BRANCH_DISPLACEMENT = 15.0
NOISE_SCALE = 0.3

#=============================================================================
# Simple Perlin-like noise
#=============================================================================

class SimplexNoise
  def initialize(seed = 42)
    @perm = (0..255).to_a.shuffle(random: Random.new(seed))
    @perm += @perm
  end

  def noise2d(x, y)
    xi = x.floor & 255
    yi = y.floor & 255
    xf = x - x.floor
    yf = y - y.floor

    u = fade(xf)
    v = fade(yf)

    aa = @perm[@perm[xi] + yi]
    ab = @perm[@perm[xi] + yi + 1]
    ba = @perm[@perm[xi + 1] + yi]
    bb = @perm[@perm[xi + 1] + yi + 1]

    x1 = lerp(grad(aa, xf, yf), grad(ba, xf - 1, yf), u)
    x2 = lerp(grad(ab, xf, yf - 1), grad(bb, xf - 1, yf - 1), u)

    lerp(x1, x2, v)
  end

  private

  def fade(t)
    t * t * t * (t * (t * 6 - 15) + 10)
  end

  def lerp(a, b, t)
    a + t * (b - a)
  end

  def grad(hash, x, y)
    case hash & 3
    when 0 then  x + y
    when 1 then -x + y
    when 2 then  x - y
    when 3 then -x - y
    end
  end
end

#=============================================================================
# Tendril Generation
#=============================================================================

def calculate_pad_position(pad_index, width, height)
  radius = [width, height].min * PAD_RADIUS_FACTOR
  angle = (-67.5 + pad_index * 45.0) * Math::PI / 180.0
  
  x = (width / 2.0) + Math.cos(angle) * radius
  y = (height / 2.0) + Math.sin(angle) * radius
  
  [x.round, y.round]
end

def generate_tendril_frame(pad_index, frame_index, noise)
  pad_x, pad_y = calculate_pad_position(pad_index, DISPLAY_WIDTH, DISPLAY_HEIGHT)
  
  dx = CENTER_X - pad_x
  dy = CENTER_Y - pad_y
  length = Math.sqrt(dx * dx + dy * dy)
  
  dx /= length
  dy /= length
  
  perp_x = -dy
  perp_y = dx
  
  time_offset = frame_index * 0.3
  
  main_points = [[pad_x, pad_y]]
  segments = []
  
  prev_x = pad_x
  prev_y = pad_y
  
  # Generate main tendril
  SEGMENTS_PER_TENDRIL.times do |seg|
    t = (seg + 1).to_f / SEGMENTS_PER_TENDRIL
    
    base_x = pad_x + dx * length * t
    base_y = pad_y + dy * length * t
    
    taper = Math.sin(t * Math::PI)
    
    noise_val = noise.noise2d(seg * NOISE_SCALE + pad_index * 10, time_offset)
    displacement = noise_val * BASE_DISPLACEMENT * taper
    
    final_x = (base_x + perp_x * displacement).round.clamp(0, DISPLAY_WIDTH - 1)
    final_y = (base_y + perp_y * displacement).round.clamp(0, DISPLAY_HEIGHT - 1)
    
    segments << { x1: prev_x, y1: prev_y, x2: final_x, y2: final_y, is_branch: 0 }
    main_points << [final_x, final_y]
    
    prev_x = final_x
    prev_y = final_y
  end
  
  # Generate branches
  branch_indices = []
  BRANCH_POINTS.times do |b|
    t = 0.2 + (b.to_f / BRANCH_POINTS) * 0.5
    idx = (t * SEGMENTS_PER_TENDRIL).round.clamp(1, SEGMENTS_PER_TENDRIL - 1)
    branch_indices << idx
  end
  
  branch_indices.each_with_index do |branch_start_idx, branch_num|
    branch_x, branch_y = main_points[branch_start_idx]
    
    next_idx = [branch_start_idx + 1, main_points.length - 1].min
    next_x, next_y = main_points[next_idx]
    tangent_x = next_x - branch_x
    tangent_y = next_y - branch_y
    tangent_len = Math.sqrt(tangent_x * tangent_x + tangent_y * tangent_y)
    tangent_len = 1.0 if tangent_len < 0.001
    tangent_x /= tangent_len
    tangent_y /= tangent_len
    
    side = (branch_num % 2 == 0) ? 1 : -1
    branch_angle = (BRANCH_ANGLE_SPREAD + noise.noise2d(branch_num * 5, time_offset) * 20) * side
    angle_rad = branch_angle * Math::PI / 180.0
    
    branch_dx = tangent_x * Math.cos(angle_rad) - tangent_y * Math.sin(angle_rad)
    branch_dy = tangent_x * Math.sin(angle_rad) + tangent_y * Math.cos(angle_rad)
    
    remaining_length = length * (1.0 - branch_start_idx.to_f / SEGMENTS_PER_TENDRIL)
    branch_length = remaining_length * BRANCH_LENGTH_FACTOR
    
    branch_perp_x = -branch_dy
    branch_perp_y = branch_dx
    
    prev_bx = branch_x
    prev_by = branch_y
    
    SEGMENTS_PER_BRANCH.times do |seg|
      t = (seg + 1).to_f / SEGMENTS_PER_BRANCH
      
      base_bx = branch_x + branch_dx * branch_length * t
      base_by = branch_y + branch_dy * branch_length * t
      
      taper = 1.0 - t
      noise_val = noise.noise2d(seg * NOISE_SCALE + pad_index * 10 + branch_num * 3, time_offset + 100)
      displacement = noise_val * BRANCH_DISPLACEMENT * taper
      
      final_bx = (base_bx + branch_perp_x * displacement).round.clamp(0, DISPLAY_WIDTH - 1)
      final_by = (base_by + branch_perp_y * displacement).round.clamp(0, DISPLAY_HEIGHT - 1)
      
      segments << { x1: prev_bx, y1: prev_by, x2: final_bx, y2: final_by, is_branch: 1 }
      
      prev_bx = final_bx
      prev_by = final_by
    end
  end
  
  segments
end

#=============================================================================
# Main
#=============================================================================

if ARGV.length < 1
  puts "Usage: ruby generate_tendrils.rb output.bin"
  exit 1
end

output_file = ARGV[0]
noise = SimplexNoise.new(12345)

max_segments = SEGMENTS_PER_TENDRIL + (BRANCH_POINTS * SEGMENTS_PER_BRANCH)
segment_size = 9  # 4 * int16 + 1 * uint8
data_size = NUM_PADS * FRAMES_PER_TENDRIL * max_segments * segment_size
total_size = 8 + data_size  # header + data

puts "Generating tendril data..."
puts "  Pads: #{NUM_PADS}"
puts "  Frames per tendril: #{FRAMES_PER_TENDRIL}"
puts "  Max segments per frame: #{max_segments}"
puts "  Segment size: #{segment_size} bytes"

File.open(output_file, 'wb') do |f|
  # Header: num_pads (u8), num_frames (u8), max_segments (u8), reserved (u8), total_size (u32)
  f.write([NUM_PADS, FRAMES_PER_TENDRIL, max_segments, 0, total_size].pack('CCCCL<'))
  
  # Data
  NUM_PADS.times do |pad|
    FRAMES_PER_TENDRIL.times do |frame|
      segments = generate_tendril_frame(pad, frame, noise)
      
      # Pad with empty segments
      while segments.length < max_segments
        segments << { x1: 0, y1: 0, x2: 0, y2: 0, is_branch: 1 }
      end
      
      segments.each do |seg|
        f.write([seg[:x1], seg[:y1], seg[:x2], seg[:y2], seg[:is_branch]].pack('s<s<s<s<C'))
      end
    end
  end
end

file_size = File.size(output_file)
puts "Generated #{output_file}"
puts "File size: #{file_size} bytes"
