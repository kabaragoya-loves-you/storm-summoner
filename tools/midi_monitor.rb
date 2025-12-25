#!/usr/bin/env ruby

# MIDI Monitor for Storm Summoner
# Displays incoming MIDI messages with CC names resolved from device JSON definitions
#
# Usage:
#   ruby midi_monitor.rb <device_slug> [--clock]
#
# Examples:
#   ruby midi_monitor.rb meris.ottobit_jr
#   ruby midi_monitor.rb chase_bliss.mood_mkii --clock

require 'ffi'
require 'json'
require 'thread'

# Windows Multimedia MIDI API via FFI
module WinMM
  extend FFI::Library
  ffi_lib 'winmm'
  ffi_convention :stdcall

  # Constants
  CALLBACK_FUNCTION = 0x00030000
  CALLBACK_NULL = 0x00000000
  MMSYSERR_NOERROR = 0
  MIM_DATA = 0x3C3
  MIM_LONGDATA = 0x3C4
  MIM_OPEN = 0x3C1
  MIM_CLOSE = 0x3C2
  MHDR_DONE = 0x00000001

  # MIDIHDR structure for SysEx
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

  # MIDI Input Callback
  callback :midi_in_proc, [:pointer, :uint, :ulong, :ulong, :ulong], :void

  # Functions
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
    # Name is at offset 8, up to 32 chars
    caps.get_bytes(8, 32).strip.gsub("\0", "")
  end
end

# Thread-safe message queue that's safe for FFI callbacks
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

# MIDI callback - must be defined at top level for FFI
# Keep it minimal - just push signals, don't do FFI operations here
MidiCallback = Proc.new do |handle, msg, instance, param1, param2|
  case msg
  when WinMM::MIM_DATA
    $midi_queue.push(param1)
  when WinMM::MIM_LONGDATA
    # Just signal that sysex arrived - we'll read from our buffer in main loop
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

  SYSEX_BUFFER_SIZE = 256

  def initialize(device_slug, show_clock: false)
    @device_slug = device_slug
    @show_clock = show_clock
    @cc_map = {}
    @running = false
    @handle = nil
    @sysex_buffer = nil
    @sysex_header = nil
    @sysex_header_ptr = nil
  end

  def run
    $stdout.sync = true  # Ensure output is flushed immediately

    load_device_definition
    device_id = select_midi_device
    return unless device_id

    device_name = WinMM.get_device_name(device_id)
    puts ""
    puts "Monitoring MIDI input: #{device_name}"
    puts "Device: #{@device_title}" if @device_title
    puts "Press Ctrl+C to exit"
    puts ""
    print_header

    @running = true
    trap("INT") { @running = false }

    # Open MIDI input
    handle_ptr = FFI::MemoryPointer.new(:pointer)
    result = WinMM.midiInOpen(handle_ptr, device_id, MidiCallback, 0, WinMM::CALLBACK_FUNCTION)
    unless result == WinMM::MMSYSERR_NOERROR
      puts "Failed to open MIDI device (error #{result})"
      return
    end
    @handle = handle_ptr.read_pointer

    # Set up SysEx buffer for MMC support
    setup_sysex_buffer

    # Start receiving
    result = WinMM.midiInStart(@handle)
    unless result == WinMM::MMSYSERR_NOERROR
      puts "Failed to start MIDI input (error #{result})"
      cleanup_sysex_buffer
      WinMM.midiInClose(@handle)
      return
    end

    # Process messages by polling the queue
    begin
      while @running
        messages = $midi_queue.pop_all
        messages.each { |data| process_short_message(data) }

        sysex_signals = $sysex_queue.pop_all
        sysex_signals.each { read_and_process_sysex }

        sleep 0.005 if messages.empty? && sysex_signals.empty?
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

    puts "\nExiting."
  end

  def setup_sysex_buffer
    @sysex_buffer = FFI::MemoryPointer.new(:char, SYSEX_BUFFER_SIZE)
    # Allocate header as raw memory and zero it
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

  def read_and_process_sysex
    return unless @sysex_header
    bytes_recorded = @sysex_header[:dwBytesRecorded]
    if bytes_recorded > 0
      data = @sysex_buffer.read_bytes(bytes_recorded).bytes
      process_sysex(data)
    end
    requeue_sysex_buffer
  end

  def requeue_sysex_buffer
    return unless @sysex_header_ptr
    # Reset and re-add the buffer
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
    vendor, pedal = @device_slug.split(".", 2)
    unless vendor && pedal
      puts "Invalid device slug format. Expected: vendor.pedal"
      exit 1
    end

    script_dir = File.dirname(File.expand_path(__FILE__))
    json_path = File.join(script_dir, "..", "midi-devices", "devices", vendor, "#{pedal}.json")

    unless File.exist?(json_path)
      puts "Device definition not found: #{json_path}"
      exit 1
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
      exit 1
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

    # Try to find Storm Summoner automatically
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
    puts format_line("TIME", "CH", "MESSAGE", "VALUE")
    puts "-" * 80
  end

  def format_line(time, channel, message, value)
    "%-12s %-3s %-40s %s" % [time, channel, message, value]
  end

  def timestamp
    Time.now.strftime("%H:%M:%S.%L")
  end

  def process_short_message(data)
    status = data & 0xFF
    byte1 = (data >> 8) & 0xFF
    byte2 = (data >> 16) & 0xFF

    # Handle system messages
    case status
    when 0xF8 # Clock
      return unless @show_clock
      puts format_line(timestamp, "-", "Clock", "")
      return
    when 0xFA, 0xFB, 0xFC
      # Start/Continue/Stop - skip these since MMC is more informative
      return
    when 0xFE # Active Sensing
      return
    when 0xFF # System Reset
      puts format_line(timestamp, "-", "System Reset", "")
      return
    end

    # Channel messages
    msg_type = status & 0xF0
    channel = (status & 0x0F) + 1

    case msg_type
    when 0x80 # Note Off
      puts format_line(timestamp, channel, "Note Off: #{note_name(byte1)}", "")
    when 0x90 # Note On
      if byte2 == 0
        puts format_line(timestamp, channel, "Note Off: #{note_name(byte1)}", "")
      else
        puts format_line(timestamp, channel, "Note On: #{note_name(byte1)}", "vel #{byte2}")
      end
    when 0xA0 # Polyphonic Aftertouch
      puts format_line(timestamp, channel, "Poly Aftertouch: #{note_name(byte1)}", byte2)
    when 0xB0 # Control Change
      handle_cc(channel, byte1, byte2)
    when 0xC0 # Program Change
      puts format_line(timestamp, channel, "Program Change", byte1)
    when 0xD0 # Channel Aftertouch
      puts format_line(timestamp, channel, "Aftertouch", byte1)
    when 0xE0 # Pitch Bend
      bend = (byte2 << 7) | byte1
      puts format_line(timestamp, channel, "Pitch Bend", bend)
    end
  end

  def handle_cc(channel, cc_num, value)
    cc_info = @cc_map[cc_num]

    if cc_info
      message = "#{cc_info[:name]} (CC #{cc_num})"
      value_str = format_cc_value(value, cc_info[:discrete_values])
    else
      message = "CC #{cc_num}"
      value_str = value.to_s
    end

    puts format_line(timestamp, channel, message, value_str)
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

  def process_sysex(data)
    return if data.nil? || data.length < 2

    # Check for MMC: F0 7F <device_id> 06 <command> F7
    if data.length >= 6 && data[0] == 0xF0 && data[1] == 0x7F && data[3] == 0x06
      command = data[4]
      device_id = data[2]
      command_name = MMC_COMMANDS[command] || "Unknown (0x#{command.to_s(16).upcase})"
      id_str = device_id == 0x7F ? "all" : device_id.to_s
      puts format_line(timestamp, "-", "MMC: #{command_name}", "dev #{id_str}")
    else
      # Generic SysEx display
      hex = data.map { |b| "%02X" % b }.join(" ")
      hex = hex[0..50] + "..." if hex.length > 54
      puts format_line(timestamp, "-", "SysEx", hex)
    end
  end
end

# Main
def print_usage
  puts <<~USAGE
    MIDI Monitor for Storm Summoner

    Usage: ruby midi_monitor.rb <device_slug> [--clock]

    Arguments:
      device_slug   Device identifier (e.g., meris.ottobit_jr)
                    Resolves to midi-devices/devices/<vendor>/<pedal>.json

    Options:
      --clock       Show MIDI clock messages (hidden by default)

    Examples:
      ruby midi_monitor.rb meris.ottobit_jr
      ruby midi_monitor.rb chase_bliss.mood_mkii --clock
  USAGE
end

if ARGV.empty? || ARGV.include?("-h") || ARGV.include?("--help")
  print_usage
  exit 0
end

device_slug = ARGV.find { |arg| !arg.start_with?("-") }
show_clock = ARGV.include?("--clock")

unless device_slug
  puts "Error: device_slug is required"
  print_usage
  exit 1
end

monitor = MidiMonitor.new(device_slug, show_clock: show_clock)
monitor.run
