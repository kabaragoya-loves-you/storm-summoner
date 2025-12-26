#!/usr/bin/env ruby
# frozen_string_literal: true

# LVGL Display Stream Viewer - Real-time Gosu preview
# Connects to Storm Summoner over USB CDC and displays output live
#
# Requirements:
#   gem install serialport
#   gem install gosu
#
# Usage: ruby display_viewer.rb [COM_PORT] [--stats]

require 'serialport'
require 'gosu'
require 'optparse'
require_relative 'ss_config'

# Protocol constants
MAGIC = 0xAC01
MAGIC_BYTES = [0x01, 0xAC].pack('CC').b
HEADER_SIZE = 16

class DisplayViewer < Gosu::Window
  def initialize(port, show_stats: false, auto_sync_interval: 0)
    @port = port
    @serial = nil
    @width = 198  # Default, will be updated
    @height = 198
    @scale = 3    # Display scaling
    @show_stats = show_stats
    @auto_sync_interval = auto_sync_interval  # Seconds between auto syncs (0 = disabled)
    
    super(@width * @scale, @height * @scale, false)
    self.caption = "LVGL Stream Viewer"
    
    @framebuffer = nil      # Raw RGBA string
    @display_image = nil    # Gosu::Image for drawing
    @read_buffer = "".b
    @frame_count = 0
    @bytes_received = 0
    @start_time = Time.now
    @running = true
    @mutex = Mutex.new
    @write_mutex = Mutex.new
    @image_dirty = false
    @last_image_update = Time.now
    @last_sync_time = Time.now
    @closed = false
  end

  def start
    return false unless connect
    return false unless start_stream
    
    # Resize window to match display
    self.width = @width * @scale
    self.height = @height * @scale
    
    # Initialize framebuffer as RGBA
    @framebuffer = "\x00\x00\x00\xFF".b * (@width * @height)
    
    # Start reader thread
    @reader_thread = Thread.new { reader_loop }
    
    true
  end

  def connect
    puts "Connecting to #{@port}..."
    @serial = SerialPort.new(@port, 115200, 8, 1, SerialPort::NONE)
    @serial.read_timeout = 5000
    puts "Connected!"
    true
  rescue => e
    puts "Connection failed: #{e.message}"
    false
  end

  def start_stream
    2.times do |attempt|
      puts "Sending DISPLAY command..."
      @serial.write("DISPLAY\n")
      @serial.flush

      response = ""
      start = Time.now
      while Time.now - start < 5.0
        ch = @serial.read(1)
        break if ch.nil?
        break if ch == "\n"
        response += ch unless ch == "\r"
      end

      if response.start_with?("DISPLAY_STARTED")
        parts = response.split
        @width = parts[1].to_i
        @height = parts[2].to_i
        puts "Stream started: #{@width}x#{@height}"
        @serial.read_timeout = 50
        return true
      end

      # If we got binary data (nulls or non-printable), device was already streaming
      if response.bytes.any? { |b| b < 32 && b != 10 && b != 13 }
        puts "Device appears to be streaming, resetting..."
        @serial.write("EXIT\n")
        @serial.flush
        sleep 0.5
        # Drain any remaining data
        @serial.read_timeout = 100
        loop do
          chunk = @serial.read(1024)
          break if chunk.nil? || chunk.empty?
        end
        @serial.read_timeout = 5000
        next
      end

      puts "Failed: #{response.inspect}"
      return false
    end
    
    puts "Failed after retries"
    false
  end

  def reader_loop
    while @running
      begin
        chunk = @serial.read(4096)
        if chunk && !chunk.empty?
          @mutex.synchronize { @read_buffer += chunk }
        end
      rescue => e
        break unless @running
      end
      
      # Process frames
      @mutex.synchronize do
        while process_one_frame; end
      end
    end
  end

  def process_one_frame
    return false if @read_buffer.length < HEADER_SIZE

    magic_pos = @read_buffer.index(MAGIC_BYTES)
    if magic_pos.nil?
      @read_buffer = @read_buffer[-1] || "".b
      return false
    end

    @read_buffer = @read_buffer[magic_pos..] if magic_pos > 0
    return false if @read_buffer.length < HEADER_SIZE

    header = parse_header(@read_buffer[0, HEADER_SIZE])
    unless valid_header?(header)
      @read_buffer = @read_buffer[2..] || "".b
      return false
    end

    total_size = HEADER_SIZE + header[:payload_len]
    return false if @read_buffer.length < total_size

    payload = @read_buffer[HEADER_SIZE, header[:payload_len]]
    @read_buffer = @read_buffer[total_size..]

    @frame_count += 1
    @bytes_received += total_size

    update_framebuffer(header, payload)
    @image_dirty = true
    true
  end

  def parse_header(data)
    values = data.unpack('S<CCS<S<S<S<L<')
    { magic: values[0], type: values[1], format: values[2],
      x: values[3], y: values[4], w: values[5], h: values[6],
      payload_len: values[7] }
  end

  def valid_header?(h)
    return false unless h[:magic] == MAGIC
    return false unless h[:x] < @width && h[:y] < @height
    return false unless h[:w] > 0 && h[:h] > 0
    return false unless h[:x] + h[:w] <= @width && h[:y] + h[:h] <= @height
    
    # Validate payload length based on format (RGB565 = 2 bytes, RGB888 = 3 bytes)
    bpp = (h[:format] == 1) ? 2 : 3
    h[:payload_len] == h[:w] * h[:h] * bpp
  end

  def update_framebuffer(header, payload)
    x, y, w, h = header[:x], header[:y], header[:w], header[:h]
    format = header[:format]
    
    src = 0
    h.times do |row|
      dst_row = (y + row) * @width + x
      w.times do |col|
        if format == 1
          # RGB565 (little-endian) -> RGBA
          lo = payload.getbyte(src) || 0
          hi = payload.getbyte(src + 1) || 0
          rgb565 = (hi << 8) | lo
          
          r = ((rgb565 >> 11) & 0x1F) * 255 / 31
          g = ((rgb565 >> 5) & 0x3F) * 255 / 63
          b = (rgb565 & 0x1F) * 255 / 31
          src += 2
        else
          # RGB888 (BGR order) -> RGBA
          b = payload.getbyte(src) || 0
          g = payload.getbyte(src + 1) || 0
          r = payload.getbyte(src + 2) || 0
          src += 3
        end
        
        dst = (dst_row + col) * 4
        @framebuffer[dst] = r.chr
        @framebuffer[dst + 1] = g.chr
        @framebuffer[dst + 2] = b.chr
        @framebuffer[dst + 3] = "\xFF".b
      end
    end
  end

  def update
    now = Time.now
    
    # Auto-sync periodically if enabled
    if @auto_sync_interval > 0 && (now - @last_sync_time) >= @auto_sync_interval
      request_sync
      @last_sync_time = now
    end
    
    # Rebuild image periodically (not every frame)
    if @image_dirty && (now - @last_image_update) >= 0.033  # ~30fps max
      @mutex.synchronize do
        @display_image = Gosu::Image.from_blob(@width, @height, @framebuffer)
        @image_dirty = false
      end
      @last_image_update = now
    end
  end

  def draw
    if @display_image
      @display_image.draw(0, 0, 0, @scale, @scale)
    end
    
    return unless @show_stats
    
    # Stats overlay
    elapsed = Time.now - @start_time
    fps = elapsed > 0 ? @frame_count / elapsed : 0
    kbps = elapsed > 0 ? (@bytes_received / 1024.0) / elapsed : 0
    
    font.draw_text("Frames: #{@frame_count} | %.1f fps | %.1f KB/s" % [fps, kbps],
      10, 10, 1, 1, 1, Gosu::Color::WHITE)
  end

  def font
    @font ||= Gosu::Font.new(16)
  end

  def button_down(id)
    case id
    when Gosu::KB_ESCAPE
      close
    when Gosu::KB_S
      request_sync
    when Gosu::KB_R
      reset_stats
    end
  end

  def request_sync
    @write_mutex.synchronize do
      @serial&.write("SYNC\n")
      @serial&.flush
    end
  rescue
    # Ignore sync errors
  end

  def reset_stats
    @frame_count = 0
    @bytes_received = 0
    @start_time = Time.now
    puts "Stats reset"
  end

  def close
    return if @closed
    @closed = true
    @running = false
    
    # Stop reader thread first
    if @reader_thread
      @reader_thread.join(2) rescue nil
      @reader_thread.kill rescue nil
    end
    
    # Send EXIT and close serial
    if @serial
      begin
        @serial.write("EXIT\n")
        @serial.flush
        sleep 0.1  # Give device time to process
      rescue
        # Ignore write errors
      end
      begin
        @serial.close
      rescue
        # Ignore close errors
      end
      @serial = nil
    end
    
    super
  end
end

# Main
if __FILE__ == $0
  viewer = nil
  
  # Ensure cleanup on any exit
  at_exit do
    if viewer && !viewer.instance_variable_get(:@closed)
      puts "\nCleaning up..."
      viewer.close rescue nil
    end
  end
  
  options = { stats: false, sync_interval: 5.0 }
  
  parser = OptionParser.new do |opts|
    opts.banner = "Usage: #{$0} [options] [COM_PORT]"
    opts.on("--stats", "Show stats overlay") { options[:stats] = true }
    opts.on("--sync-interval=N", Float, "Auto-sync interval in seconds (default 5, 0 = disabled)") do |v|
      options[:sync_interval] = v
    end
    opts.on("-h", "--help", "Show this help") do
      puts opts
      puts "\nKeys: S=sync, R=reset stats, ESC=quit"
      exit
    end
  end
  parser.parse!
  
  port = ARGV[0] || SSConfig.default_port
  puts "Using port: #{port}" if ARGV.empty? && SSConfig.cdc_port
  
  viewer = DisplayViewer.new(port,
    show_stats: options[:stats],
    auto_sync_interval: options[:sync_interval])
  if viewer.start
    viewer.show
  else
    puts "Failed to start viewer"
    exit 1
  end
end
