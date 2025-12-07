#!/usr/bin/env ruby
# frozen_string_literal: true

# LVGL Display Stream Viewer - saves frames to PNG
# Connects to Storm Summoner over USB CDC and captures display output
#
# Requirements:
#   gem install serialport
#   gem install chunky_png
#
# Usage: ruby display_viewer.rb [COM_PORT]
#   e.g., ruby display_viewer.rb COM24

require 'serialport'
require 'chunky_png'
require 'fileutils'

# Protocol constants
MAGIC = 0xAC01
MAGIC_BYTES = [0x01, 0xAC].pack('CC').b
HEADER_SIZE = 16

class DisplayCapture
  def initialize(port)
    @port = port
    @serial = nil
    @width = 0
    @height = 0
    @framebuffer = nil
    @read_buffer = "".b
    @frame_count = 0
    @bytes_received = 0
    @start_time = nil
    @running = false
    @output_dir = "captures"
  end

  def connect
    puts "Connecting to #{@port}..."
    @serial = SerialPort.new(@port, 115200, 8, 1, SerialPort::NONE)
    @serial.read_timeout = 5000
    puts "Connected!"
    true
  rescue => e
    puts "Failed: #{e.message}"
    false
  end

  def start_stream
    puts "Sending DISPLAY command..."
    @serial.write("DISPLAY\n")
    @serial.flush

    # Read response with timeout
    response = ""
    start = Time.now
    while Time.now - start < 5.0
      ch = @serial.read(1)
      if ch.nil?
        sleep(0.01)
        next
      end
      break if ch == "\n"
      response += ch unless ch == "\r"
    end

    puts "Response: #{response.inspect}"

    if response.start_with?("DISPLAY_STARTED")
      parts = response.split
      @width = parts[1].to_i
      @height = parts[2].to_i
      puts "Stream started: #{@width}x#{@height}"
      
      # Initialize framebuffer
      @framebuffer = ChunkyPNG::Image.new(@width, @height, ChunkyPNG::Color::BLACK)
      @read_buffer = "".b
      @serial.read_timeout = 100
      @save_count = 0
      
      FileUtils.mkdir_p(@output_dir)
      return true
    end

    puts "Failed to start stream"
    false
  end

  def stop_stream
    @serial.write("EXIT\n") rescue nil
    @serial.flush rescue nil
  end

  def run
    @running = true
    @start_time = Time.now
    @frame_count = 0
    @bytes_received = 0
    last_save = Time.now
    last_update = Time.now
    last_stats = Time.now
    save_interval = 1.0 / 15   # 15 fps max capture rate
    settle_time = 0.003        # Wait 3ms after last update before saving

    puts "\nCapturing frames to #{@output_dir}/ at up to 20fps... (Ctrl+C to stop)\n"
    puts "=" * 60

    @serial.read_timeout = 100
    
    # Reader thread to avoid blocking main loop
    reader = Thread.new do
      while @running
        begin
          chunk = @serial.read(4096)
          Thread.current[:data] ||= "".b
          Thread.current[:data] += chunk if chunk && !chunk.empty?
        rescue => e
          break unless @running
        end
      end
    end
    
    prev_frame_count = 0
    
    while @running
      # Grab data from reader thread
      if reader[:data] && !reader[:data].empty?
        @read_buffer += reader[:data]
        reader[:data] = "".b
      end

      # Process all available frames
      while process_one_frame; end
      
      now = Time.now
      
      # Track when we last received an update
      if @frame_count > prev_frame_count
        last_update = now
        prev_frame_count = @frame_count
      end

      # Save only when settled (no updates for settle_time) and interval met
      if now - last_save >= save_interval && 
         now - last_update >= settle_time && 
         @frame_count > 0
        save_snapshot
        last_save = now
      end

      # Print stats
      if now - last_stats >= 1.0
        elapsed = now - @start_time
        fps = @frame_count / elapsed
        kbps = (@bytes_received / 1024.0) / elapsed
        print "\rFrames: #{@frame_count} | Saved: #{@save_count} | %.1f recv fps | %.1f KB/s" % [fps, kbps]
        $stdout.flush
        last_stats = now
      end

      sleep(0.01)
    end
    
    reader.kill rescue nil

    puts "\n" + "=" * 60
    puts "Captured #{@frame_count} frames, saved #{@save_count} images"
  end

  def process_one_frame
    return false if @read_buffer.length < HEADER_SIZE

    magic_pos = @read_buffer.index(MAGIC_BYTES)
    return clear_buffer if magic_pos.nil?

    @read_buffer = @read_buffer[magic_pos..] if magic_pos > 0
    return false if @read_buffer.length < HEADER_SIZE

    header = parse_header(@read_buffer[0, HEADER_SIZE])
    return skip_bytes(2) unless valid_header?(header)

    total_size = HEADER_SIZE + header[:payload_len]
    return false if @read_buffer.length < total_size

    payload = @read_buffer[HEADER_SIZE, header[:payload_len]]
    @read_buffer = @read_buffer[total_size..]

    @frame_count += 1
    @bytes_received += total_size

    update_framebuffer(header, payload)
    true
  end

  def clear_buffer
    @read_buffer = @read_buffer[-1] || "".b
    false
  end

  def skip_bytes(n)
    @read_buffer = @read_buffer[n..] || "".b
    false
  end

  def parse_header(data)
    values = data.unpack('S<CCS<S<S<S<L<')
    { magic: values[0], type: values[1], format: values[2],
      x: values[3], y: values[4], w: values[5], h: values[6],
      payload_len: values[7] }
  end

  def valid_header?(h)
    h[:magic] == MAGIC &&
    h[:x] < @width && h[:y] < @height &&
    h[:w] > 0 && h[:h] > 0 &&
    h[:x] + h[:w] <= @width && h[:y] + h[:h] <= @height &&
    h[:payload_len] == h[:w] * h[:h] * 3
  end

  def update_framebuffer(header, payload)
    x, y, w, h = header[:x], header[:y], header[:w], header[:h]
    
    src = 0
    h.times do |row|
      w.times do |col|
        # Data comes as BGR, swap to RGB for PNG
        b = payload.getbyte(src) || 0
        g = payload.getbyte(src + 1) || 0
        r = payload.getbyte(src + 2) || 0
        @framebuffer[x + col, y + row] = ChunkyPNG::Color.rgb(r, g, b)
        src += 3
      end
    end
  end

  def save_snapshot(suffix = nil)
    @save_count += 1 unless suffix
    filename = suffix ? "frame_#{suffix}.png" : "frame_%06d.png" % @save_count
    path = File.join(@output_dir, filename)
    @framebuffer.save(path)
  end

  def close
    @running = false
    stop_stream
    @serial&.close
  end
end

# Main
if __FILE__ == $0
  port = ARGV[0] || (RUBY_PLATFORM =~ /mswin|mingw/ ? 'COM3' : '/dev/ttyACM0')
  
  capture = DisplayCapture.new(port)
  
  trap('INT') do
    capture.close
    exit
  end

  exit(1) unless capture.connect
  exit(1) unless capture.start_stream

  begin
    capture.run
  ensure
    capture.close
  end
end
