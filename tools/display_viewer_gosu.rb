#!/usr/bin/env ruby
# frozen_string_literal: true

# LVGL Display Stream Viewer - Real-time Gosu preview
# Connects to Storm Summoner over USB CDC and displays output live
#
# Requirements:
#   gem install serialport
#   gem install gosu
#
# Usage: ruby display_viewer_gosu.rb [COM_PORT] [--stats]

require 'serialport'
require 'gosu'
require 'optparse'

# Protocol constants
MAGIC = 0xAC01
MAGIC_BYTES = [0x01, 0xAC].pack('CC').b
HEADER_SIZE = 16

class DisplayViewer < Gosu::Window
  def initialize(port, show_stats: false)
    @port = port
    @serial = nil
    @width = 198  # Default, will be updated
    @height = 198
    @scale = 3    # Display scaling
    @show_stats = show_stats
    
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
    @image_dirty = false
    @last_image_update = Time.now
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

    puts "Failed: #{response.inspect}"
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
    
    # Validate payload length based on format
    bpp = (h[:format] == 1) ? 2 : 3  # RGB565 = 2, RGB888 = 3
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
    # Rebuild image periodically (not every frame)
    now = Time.now
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
    close if id == Gosu::KB_ESCAPE
  end

  def close
    @running = false
    @reader_thread&.join(1)
    @serial&.write("EXIT\n") rescue nil
    @serial&.close rescue nil
    super
  end
end

# Main
if __FILE__ == $0
  options = { stats: false }
  
  parser = OptionParser.new do |opts|
    opts.banner = "Usage: #{$0} [options] [COM_PORT]"
    opts.on("--stats", "Show stats overlay") { options[:stats] = true }
    opts.on("-h", "--help", "Show this help") { puts opts; exit }
  end
  parser.parse!
  
  port = ARGV[0] || (RUBY_PLATFORM =~ /mswin|mingw/ ? 'COM3' : '/dev/ttyACM0')
  
  viewer = DisplayViewer.new(port, show_stats: options[:stats])
  if viewer.start
    viewer.show
  else
    puts "Failed to start viewer"
    exit 1
  end
end
