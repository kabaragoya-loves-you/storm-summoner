#!/usr/bin/env ruby

# MIDI Monitor for Storm Summoner
# Displays incoming MIDI messages with CC names resolved from device JSON definitions
# Supports both USB MIDI output monitoring and CDC relay for incoming messages
#
# Usage:
#   ruby midi_monitor.rb <device_slug> [--port COM3] [--clock]
#
# Examples:
#   ruby midi_monitor.rb meris.ottobit_jr
#   ruby midi_monitor.rb meris.ottobit_jr --port COM3
#   ruby midi_monitor.rb chase_bliss.mood_mkii --port COM3 --clock

require 'ffi'
require 'json'
require 'thread'

# Optional serialport for CDC relay
begin
  require 'serialport'
  SERIALPORT_AVAILABLE = true
rescue LoadError
  SERIALPORT_AVAILABLE = false
end

# Windows Multimedia MIDI API via FFI
module WinMM
  extend FFI::Library
  ffi_lib 'winmm'
  ffi_convention :stdcall

  CALLBACK_FUNCTION = 0x00030000
  MMSYSERR_NOERROR = 0
  MIM_DATA = 0x3C3
  MIM_LONGDATA = 0x3C4

  class MIDIHDR < FFI::Struct
    layout :lpData,          :pointer,
           :dwBufferLength,  :ulong,
           :dwBytesRecorded, :ulong,
           :dwUser,          :ulong,
           :dwFlags,         :ulong,
           :lpNext,          :pointer,
           :reserved,        :ulong,
           :dwOffset,        :ulong,
           :dwReserved,      [:ulong, 4]
  end

  callback :midi_in_proc, [:pointer, :uint, :ulong, :ulong, :ulong], :void

  attach_function :midiInGetNumDevs, [], :uint
  attach_function :midiInGetDevCapsA, [:uint, :pointer, :uint], :uint
  attach_function :midiInOpen, [:pointer, :uint, :midi_in_proc, :ulong, :ulong], :uint
  attach_function :midiInStart, [:pointer], :uint
  attach_function :midiInStop, [:pointer], :uint
  attach_function :midiInClose, [:pointer], :uint
  attach_function :midiInReset, [:pointer], :uint
  attach_function :midiInPrepareHeader, [:pointer, :pointer, :uint], :uint
  attach_function :midiInUnprepareHeader, [:pointer, :pointer, :uint], :uint
  attach_function :midiInAddBuffer, [:pointer, :pointer, :uint], :uint

  def self.get_device_name(device_id)
    caps = FFI::MemoryPointer.new(:char, 64)
    result = midiInGetDevCapsA(device_id, caps, 64)
    return nil unless result == MMSYSERR_NOERROR
    caps.get_bytes(8, 32).strip.gsub("\0", "")
  end
end

# Thread-safe message queue
class MidiMessageQueue
  def initialize
    @mutex = Mutex.new
    @messages = []
  end

  def push(msg)
    @mutex.synchronize { @messages << msg }
  end

  def pop_all
    @mutex.synchronize do
      result = @messages.dup
      @messages.clear
      result
    end
  end
end

$midi_queue = MidiMessageQueue.new
$sysex_queue = MidiMessageQueue.new
$cdc_queue = MidiMessageQueue.new

MidiCallback = Proc.new do |handle, msg, instance, param1, param2|
  case msg
  when WinMM::MIM_DATA
    $midi_queue.push(param1)
  when WinMM::MIM_LONGDATA
    $sysex_queue.push(:received) if param1 != 0
  end
end

class MidiMonitor
  NOTE_NAMES = %w[C C# D D# E F F# G G# A A# B].freeze

  MMC_COMMANDS = {
    0x01 => "Stop",
    0x02 => "Play",
    0x03 => "Deferred Play",
    0x04 => "Fast Forward",
    0x05 => "Rewind",
    0x06 => "Record Strobe",
    0x07 => "Record Exit",
    0x08 => "Record Pause",
    0x09 => "Pause",
    0x0A => "Eject"
  }.freeze

  # MIDI event types matching firmware midi_in.h
  EVENT_TYPES = {
    0 => :note_off,
    1 => :note_on,
    2 => :poly_aftertouch,
    3 => :control_change,
    4 => :program_change,
    5 => :channel_aftertouch,
    6 => :pitch_bend,
    7 => :time_code,
    8 => :song_position,
    9 => :song_select,
    10 => :tune_request,
    11 => :sysex,
    12 => :clock,
    13 => :tick,
    14 => :start,
    15 => :continue,
    16 => :stop,
    17 => :reset,
    18 => :active_sensing
  }.freeze

  SYSEX_BUFFER_SIZE = 256

  def initialize(device_slug: nil, show_clock: false, cdc_port: nil)
    @device_slug = device_slug
    @show_clock = show_clock
    @cdc_port = cdc_port
    @cc_map = {}
    @running = false
    @handle = nil
    @sysex_buffer = nil
    @sysex_header = nil
    @sysex_header_ptr = nil
    @cdc_thread = nil
    @cdc_serial = nil
    @device_detected = false
  end

  def run
    $stdout.sync = true

    # Try to detect device slug via CDC if not provided
    if @device_slug.nil? && @cdc_port && SERIALPORT_AVAILABLE
      detect_device_via_cdc
    end

    # Load device definition if we have a slug
    if @device_slug
      load_device_definition
    else
      puts "No device profile specified. CC names will not be resolved."
      puts "(Connect via CDC or provide device_slug to enable CC name resolution)"
    end

    device_id = select_midi_device
    return unless device_id

    device_name = WinMM.get_device_name(device_id)
    puts ""
    puts "Monitoring MIDI:"
    puts "  USB MIDI output: #{device_name}"
    puts "  CDC relay input: #{@cdc_port || '(not connected)'}"
    puts "  Device profile: #{@device_title}" if @device_title
    puts "Press Ctrl+C to exit"
    puts ""
    print_header

    @running = true
    trap("INT") { @running = false }

    # Open MIDI input first (before CDC, so we fail early if device is busy)
    handle_ptr = FFI::MemoryPointer.new(:pointer)
    result = WinMM.midiInOpen(handle_ptr, device_id, MidiCallback, 0, WinMM::CALLBACK_FUNCTION)
    unless result == WinMM::MMSYSERR_NOERROR
      error_msg = case result
        when 7 then "device already in use by another application"
        when 2 then "device ID out of range"
        when 4 then "insufficient memory"
        else "error code #{result}"
      end
      puts "Failed to open MIDI device: #{error_msg}"
      return
    end
    @handle = handle_ptr.read_pointer

    # Start CDC relay thread if port specified
    start_cdc_relay if @cdc_port

    setup_sysex_buffer

    result = WinMM.midiInStart(@handle)
    unless result == WinMM::MMSYSERR_NOERROR
      puts "Failed to start MIDI input (error #{result})"
      cleanup_sysex_buffer
      WinMM.midiInClose(@handle)
      stop_cdc_relay
      return
    end

    # Main processing loop
    begin
      while @running
        # Process USB MIDI output messages (OUT direction)
        messages = $midi_queue.pop_all
        messages.each { |data| process_short_message(data, :out) }

        sysex_signals = $sysex_queue.pop_all
        sysex_signals.each { read_and_process_sysex(:out) }

        # Process CDC relay messages (IN direction)
        cdc_messages = $cdc_queue.pop_all
        cdc_messages.each { |msg| process_cdc_message(msg) }

        sleep 0.005 if messages.empty? && sysex_signals.empty? && cdc_messages.empty?
      end
    rescue => e
      puts "\nError: #{e.message}"
      puts e.backtrace.first(5).join("\n")
    end

    # Cleanup
    WinMM.midiInStop(@handle)
    WinMM.midiInReset(@handle)
    cleanup_sysex_buffer
    WinMM.midiInClose(@handle)
    stop_cdc_relay

    puts "\nExiting."
  end

  def detect_device_via_cdc
    return unless @cdc_port && SERIALPORT_AVAILABLE

    begin
      serial = SerialPort.new(@cdc_port, 115200, 8, 1, SerialPort::NONE)
      serial.read_timeout = 500

      sleep 0.2
      serial.write("DEVICE\n")
      serial.flush

      # Read response
      start_time = Time.now
      loop do
        line = serial.gets&.chomp
        if line&.start_with?("DEVICE ")
          slug = line[7..].strip
          # Strip @N instance suffix
          @device_slug = slug.sub(/@\d+$/, '')
          @device_detected = true
          puts "Detected device: #{@device_slug}"
          break
        end
        break if Time.now - start_time > 1
      end

      serial.close
    rescue Errno::EACCES, Errno::EBUSY
      puts "CDC port #{@cdc_port} in use - cannot auto-detect device"
    rescue => e
      puts "CDC error during device detection: #{e.message}"
    end
  end

  def start_cdc_relay
    return unless @cdc_port && SERIALPORT_AVAILABLE

    @cdc_thread = Thread.new do
      begin
        @cdc_serial = SerialPort.new(@cdc_port, 115200, 8, 1, SerialPort::NONE)
        @cdc_serial.read_timeout = 100

        # Enter MIDI relay mode
        sleep 0.3
        @cdc_serial.write("MIDI#{@show_clock ? ' CLOCK' : ''}\n")
        @cdc_serial.flush

        # Wait for MIDI_STARTED response
        start_time = Time.now
        loop do
          line = @cdc_serial.gets&.chomp
          break if line&.include?("MIDI_STARTED")
          break if Time.now - start_time > 2
        end

        # Read relay messages
        while @running
          begin
            line = @cdc_serial.gets
            if line
              line = line.chomp.strip
              $cdc_queue.push(line) if line.start_with?("M:")
            end
          rescue Errno::EAGAIN, IOError
            break unless @running
            sleep 0.01
          end
        end

        # Exit relay mode gracefully
        begin
          @cdc_serial.write("EXIT\n")
          @cdc_serial.flush
          sleep 0.1
        rescue
          # Ignore errors during shutdown
        end
      rescue Errno::EACCES, Errno::EBUSY => e
        # Port is in use by another application (e.g., IDF Monitor)
        puts "CDC relay unavailable: port #{@cdc_port} in use (close IDF Monitor?)"
        @cdc_port = nil  # Disable CDC relay
      rescue => e
        # Only report errors if we're still supposed to be running
        puts "CDC error: #{e.message}" if @running && !e.message.include?("closed")
      ensure
        @cdc_serial&.close rescue nil
      end
    end
  end

  def stop_cdc_relay
    return unless @cdc_thread
    @running = false  # Signal thread to exit
    @cdc_thread.join(2) rescue nil
  end

  def process_cdc_message(msg)
    # Format: M:<type>,<channel>,<data1>,<data2>,<length>[,<hex_sysex>]
    return unless msg.start_with?("M:")
    parts = msg[2..].split(",")
    return if parts.length < 5

    type = parts[0].to_i
    channel = parts[1].to_i
    data1 = parts[2].to_i
    data2 = parts[3].to_i
    length = parts[4].to_i
    hex_sysex = parts[5] if parts.length > 5

    event_type = EVENT_TYPES[type] || :unknown

    case event_type
    when :note_off
      output_line(:in, channel + 1, "Note Off: #{note_name(data1)}", "")
    when :note_on
      if data2 == 0
        output_line(:in, channel + 1, "Note Off: #{note_name(data1)}", "")
      else
        output_line(:in, channel + 1, "Note On: #{note_name(data1)}", "vel #{data2}")
      end
    when :poly_aftertouch
      output_line(:in, channel + 1, "Poly Aftertouch: #{note_name(data1)}", data2)
    when :control_change
      handle_cc_with_dir(:in, channel + 1, data1, data2)
    when :program_change
      output_line(:in, channel + 1, "Program Change", data1)
    when :channel_aftertouch
      output_line(:in, channel + 1, "Aftertouch", data1)
    when :pitch_bend
      bend = (data2 << 7) | data1
      output_line(:in, channel + 1, "Pitch Bend", bend)
    when :clock
      output_line(:in, "-", "Clock", "") if @show_clock
    when :start
      output_line(:in, "-", "Start", "")
    when :continue
      output_line(:in, "-", "Continue", "")
    when :stop
      output_line(:in, "-", "Stop", "")
    when :sysex
      if hex_sysex
        sysex_bytes = [hex_sysex].pack("H*").bytes
        process_sysex_data(sysex_bytes, :in)
      else
        output_line(:in, "-", "SysEx", "#{length} bytes")
      end
    end
  end

  def setup_sysex_buffer
    @sysex_buffer = FFI::MemoryPointer.new(:char, SYSEX_BUFFER_SIZE)
    @sysex_header_ptr = FFI::MemoryPointer.new(WinMM::MIDIHDR.size)
    @sysex_header = WinMM::MIDIHDR.new(@sysex_header_ptr)
    @sysex_header[:lpData] = @sysex_buffer
    @sysex_header[:dwBufferLength] = SYSEX_BUFFER_SIZE
    @sysex_header[:dwBytesRecorded] = 0
    @sysex_header[:dwUser] = 0
    @sysex_header[:dwFlags] = 0
    @sysex_header[:lpNext] = nil
    @sysex_header[:reserved] = 0
    @sysex_header[:dwOffset] = 0

    result = WinMM.midiInPrepareHeader(@handle, @sysex_header_ptr, WinMM::MIDIHDR.size)
    if result == WinMM::MMSYSERR_NOERROR
      WinMM.midiInAddBuffer(@handle, @sysex_header_ptr, WinMM::MIDIHDR.size)
    else
      puts "Warning: Failed to prepare SysEx buffer (error #{result})"
    end
  end

  def read_and_process_sysex(direction)
    return unless @sysex_header
    bytes_recorded = @sysex_header[:dwBytesRecorded]
    if bytes_recorded > 0
      data = @sysex_buffer.read_bytes(bytes_recorded).bytes
      process_sysex_data(data, direction)
    end
    requeue_sysex_buffer
  end

  def requeue_sysex_buffer
    return unless @sysex_header_ptr
    @sysex_header[:dwBytesRecorded] = 0
    WinMM.midiInAddBuffer(@handle, @sysex_header_ptr, WinMM::MIDIHDR.size)
  end

  def cleanup_sysex_buffer
    return unless @sysex_header_ptr
    WinMM.midiInUnprepareHeader(@handle, @sysex_header_ptr, WinMM::MIDIHDR.size)
    @sysex_header = nil
    @sysex_header_ptr = nil
    @sysex_buffer = nil
  end

  private

  def load_device_definition
    return unless @device_slug

    vendor, pedal = @device_slug.split(".", 2)
    unless vendor && pedal
      puts "Invalid device slug format. Expected: vendor.pedal"
      return
    end

    script_dir = File.dirname(File.expand_path(__FILE__))
    json_path = File.join(script_dir, "..", "midi-devices", "devices", vendor, "#{pedal}.json")

    unless File.exist?(json_path)
      puts "Device definition not found: #{json_path}"
      return
    end

    begin
      data = JSON.parse(File.read(json_path))
      @device_title = data["title"] || data["displayName"]

      if data["controlChangeCommands"]
        data["controlChangeCommands"].each do |cc|
          cc_num = cc["controlChangeNumber"]
          @cc_map[cc_num] = {
            name: cc["name"],
            discrete_values: parse_discrete_values(cc["valueRange"])
          }
        end
      end
    rescue JSON::ParserError => e
      puts "Failed to parse device JSON: #{e.message}"
    end
  end

  def parse_discrete_values(value_range)
    return nil unless value_range && value_range["discreteValues"]
    value_range["discreteValues"]
      .map { |dv| { value: dv["value"], name: dv["name"] } }
      .sort_by { |dv| -dv[:value] }
  end

  def select_midi_device
    num_devices = WinMM.midiInGetNumDevs
    if num_devices == 0
      puts "No MIDI input devices found."
      return nil
    end

    devices = []
    num_devices.times do |i|
      name = WinMM.get_device_name(i)
      devices << { id: i, name: name } if name
    end

    if devices.empty?
      puts "No accessible MIDI input devices."
      return nil
    end

    storm = devices.find { |d| d[:name] =~ /storm.?summoner/i }
    return storm[:id] if storm
    return devices.first[:id] if devices.length == 1

    puts "Available MIDI inputs:"
    devices.each_with_index do |device, idx|
      puts "  #{idx + 1}. #{device[:name]}"
    end
    print "Select input (1-#{devices.length}): "

    choice = STDIN.gets&.chomp.to_i
    if choice < 1 || choice > devices.length
      puts "Invalid selection."
      return nil
    end

    devices[choice - 1][:id]
  end

  def print_header
    puts format_line("TIME", "DIR", "CH", "MESSAGE", "VALUE")
    puts "-" * 85
  end

  def format_line(time, dir, channel, message, value)
    "%-12s %-3s %-3s %-40s %s" % [time, dir, channel, message, value]
  end

  def output_line(direction, channel, message, value)
    dir_str = direction == :in ? "IN" : "OUT"
    puts format_line(timestamp, dir_str, channel, message, value)
  end

  def timestamp
    Time.now.strftime("%H:%M:%S.%L")
  end

  def process_short_message(data, direction)
    status = data & 0xFF
    byte1 = (data >> 8) & 0xFF
    byte2 = (data >> 16) & 0xFF

    case status
    when 0xF8 # Clock
      return unless @show_clock
      output_line(direction, "-", "Clock", "")
      return
    when 0xFA, 0xFB, 0xFC
      return # Skip, MMC is more informative
    when 0xFE
      return
    when 0xFF
      output_line(direction, "-", "System Reset", "")
      return
    end

    msg_type = status & 0xF0
    channel = (status & 0x0F) + 1

    case msg_type
    when 0x80
      output_line(direction, channel, "Note Off: #{note_name(byte1)}", "")
    when 0x90
      if byte2 == 0
        output_line(direction, channel, "Note Off: #{note_name(byte1)}", "")
      else
        output_line(direction, channel, "Note On: #{note_name(byte1)}", "vel #{byte2}")
      end
    when 0xA0
      output_line(direction, channel, "Poly Aftertouch: #{note_name(byte1)}", byte2)
    when 0xB0
      handle_cc_with_dir(direction, channel, byte1, byte2)
    when 0xC0
      output_line(direction, channel, "Program Change", byte1)
    when 0xD0
      output_line(direction, channel, "Aftertouch", byte1)
    when 0xE0
      bend = (byte2 << 7) | byte1
      output_line(direction, channel, "Pitch Bend", bend)
    end
  end

  def handle_cc_with_dir(direction, channel, cc_num, value)
    cc_info = @cc_map[cc_num]

    if cc_info
      message = "#{cc_info[:name]} (CC #{cc_num})"
      value_str = format_cc_value(value, cc_info[:discrete_values])
    else
      message = "CC #{cc_num}"
      value_str = value.to_s
    end

    output_line(direction, channel, message, value_str)
  end

  def format_cc_value(value, discrete_values)
    return value.to_s unless discrete_values
    match = discrete_values.find { |dv| value >= dv[:value] }
    match ? "#{match[:name]} (#{value})" : value.to_s
  end

  def note_name(note_num)
    octave = (note_num / 12) - 1
    note = NOTE_NAMES[note_num % 12]
    "#{note}#{octave}"
  end

  def process_sysex_data(data, direction)
    return if data.nil? || data.length < 2

    if data.length >= 6 && data[0] == 0xF0 && data[1] == 0x7F && data[3] == 0x06
      command = data[4]
      device_id = data[2]
      command_name = MMC_COMMANDS[command] || "Unknown (0x#{command.to_s(16).upcase})"
      id_str = device_id == 0x7F ? "all" : device_id.to_s
      output_line(direction, "-", "MMC: #{command_name}", "dev #{id_str}")
    else
      hex = data.map { |b| "%02X" % b }.join(" ")
      hex = hex[0..50] + "..." if hex.length > 54
      output_line(direction, "-", "SysEx", hex)
    end
  end
end

# Main
def print_usage
  puts <<~USAGE
    MIDI Monitor for Storm Summoner

    Usage: ruby midi_monitor.rb [device_slug] [--port PORT] [--clock]

    Arguments:
      device_slug   Device identifier (e.g., meris.ottobit_jr)
                    Resolves to midi-devices/devices/<vendor>/<pedal>.json
                    If omitted, auto-detected via CDC DEVICE command

    Options:
      --port PORT   CDC serial port for MIDI IN relay (e.g., COM3)
                    Auto-detected from .vscode/settings.json if not specified
      --clock       Show MIDI clock messages (hidden by default)

    Output:
      DIR column shows message direction:
        OUT = Messages sent from Storm Summoner (via USB MIDI)
        IN  = Messages received by Storm Summoner (via CDC relay)

    Examples:
      ruby midi_monitor.rb                          # Auto-detect device via CDC
      ruby midi_monitor.rb meris.ottobit_jr         # Specify device explicitly
      ruby midi_monitor.rb --port COM3 --clock      # Auto-detect with options
  USAGE
end

if ARGV.include?("-h") || ARGV.include?("--help")
  print_usage
  exit 0
end

device_slug = ARGV.find { |arg| !arg.start_with?("-") && !ARGV[ARGV.index(arg) - 1]&.start_with?("--port") rescue true }
show_clock = ARGV.include?("--clock")

# Try to read CDC port from .vscode/settings.json
def detect_cdc_port
  script_dir = File.dirname(File.expand_path(__FILE__))
  settings_path = File.join(script_dir, "..", ".vscode", "settings.json")
  return nil unless File.exist?(settings_path)

  begin
    settings = JSON.parse(File.read(settings_path))
    settings["ss.cdcPort"]
  rescue
    nil
  end
end

# Parse --port option or auto-detect
cdc_port = nil
port_idx = ARGV.index("--port")
if port_idx && ARGV[port_idx + 1]
  cdc_port = ARGV[port_idx + 1]
else
  cdc_port = detect_cdc_port
end

if cdc_port && !SERIALPORT_AVAILABLE
  puts "Warning: 'serialport' gem not installed. CDC relay disabled."
  puts "Install with: gem install serialport"
  cdc_port = nil
end

monitor = MidiMonitor.new(device_slug: device_slug, show_clock: show_clock, cdc_port: cdc_port)
monitor.run
