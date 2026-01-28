#!/usr/bin/env ruby
# frozen_string_literal: true

# App Settings CLI Tool for Storm Summoner
# Manage NVS key-value pairs via USB CDC SETTINGS mode

require 'serialport'
require 'json'
require 'optparse'
require_relative 'ss_config'

class AppSettingsCLI
  SETTINGS_TIMEOUT = 3.0
  READ_TIMEOUT = 2.0

  def initialize(port_name, debug: false)
    @port_name = port_name
    @port = nil
    @in_settings = false
    @debug = debug
  end

  def connect
    puts "Connecting to #{@port_name}..."
    @port = SerialPort.new(@port_name, 115200, 8, 1, SerialPort::NONE)
    @port.dtr = 1
    @port.rts = 1
    @port.read_timeout = 100
    sleep 0.5
    flush_input
    puts "Connected."
  end

  def disconnect
    begin
      leave_settings if @in_settings
    rescue Errno::EIO, IOError, Errno::EBADF, Interrupt
      # Port already in bad state or interrupted - ignore
    end
    begin
      @port&.close
    rescue Errno::EIO, IOError, Errno::EBADF, Interrupt
      # Already closed or interrupted - ignore
    end
    puts "Disconnected."
  end

  def flush_input
    @port.getc while @port.ready?
  rescue StandardError
    nil
  end

  def send_line(data)
    puts "TX: #{data}" if @debug
    @port.write("#{data}\n")
    @port.flush
  rescue Errno::EIO, IOError, Errno::EBADF, Interrupt
    # Port error or interrupt - will be handled by caller
  end

  def read_line(timeout: READ_TIMEOUT)
    buffer = ""
    start = Time.now
    while Time.now - start < timeout
      begin
        byte = @port.getbyte
        if byte
          if byte == 10 # newline
            line = buffer.gsub("\r", "").strip
            puts "RX: #{line}" if @debug
            return line
          else
            buffer += byte.chr
          end
        else
          sleep 0.01
        end
      rescue Errno::EIO, IOError, Errno::EBADF, Interrupt
        break
      end
    end
    nil
  end

  def read_until_end(timeout: READ_TIMEOUT)
    lines = []
    while true
      line = read_line(timeout: timeout)
      break if line.nil?
      break if line == "END"
      lines << line
    end
    lines
  end

  def enter_settings
    return if @in_settings

    flush_input
    send_line("")
    sleep 0.1
    flush_input
    send_line("SETTINGS")

    line = read_line(timeout: SETTINGS_TIMEOUT)
    if line == "SETTINGS_STARTED"
      @in_settings = true
      puts "Entered settings mode."
    else
      raise "Failed to enter settings mode (got: #{line.inspect})"
    end
  end

  def leave_settings
    return unless @in_settings

    send_line("EXIT")
    read_line(timeout: 1.0)
    @in_settings = false
  end

  # ============================================================================
  # Commands
  # ============================================================================

  def cmd_list
    enter_settings unless @in_settings
    send_line("LIST")

    lines = read_until_end
    if lines.empty?
      puts "(No settings stored)"
    else
      lines.each do |line|
        key, type = line.split(":")
        puts "#{key} (#{type})"
      end
    end
  end

  def cmd_get(key)
    enter_settings unless @in_settings
    send_line("GET #{key}")

    line = read_line
    if line&.start_with?("ERROR:")
      warn line
      exit 1
    elsif line
      puts line
    else
      warn "No response"
      exit 1
    end
  end

  def cmd_set(key, value, type: nil)
    enter_settings unless @in_settings

    # Auto-detect type if not specified
    if type.nil?
      if value =~ /^(true|false)$/i
        type = "bool"
        value = value.downcase
      elsif value =~ /^\d+$/ && value.to_i <= 255
        type = "u8"
      elsif value =~ /^\d+$/ && value.to_i <= 65535
        type = "u16"
      elsif value =~ /^-?\d+$/
        type = "u32"
      else
        type = "str"
      end
    end

    send_line("SET #{type} #{key} #{value}")
    line = read_line

    if line == "OK"
      puts "Set #{key} = #{value}"
    else
      warn "Failed: #{line}"
      exit 1
    end
  end

  def cmd_erase(key)
    enter_settings unless @in_settings
    send_line("ERASE #{key}")

    line = read_line
    if line == "OK"
      puts "Erased #{key}"
    else
      warn "Failed: #{line}"
      exit 1
    end
  end

  def cmd_erase_all(confirm: false)
    unless confirm
      warn "Use --confirm to erase all settings"
      exit 1
    end

    enter_settings unless @in_settings
    send_line("ERASE_ALL")

    line = read_line
    if line == "OK"
      puts "All settings erased"
    else
      warn "Failed: #{line}"
      exit 1
    end
  end

  def cmd_dump(output_file: nil)
    enter_settings unless @in_settings
    send_line("DUMP")

    # Read JSON response (single line)
    json_str = read_line(timeout: 5.0)

    if json_str.nil? || json_str.empty?
      warn "No response from device"
      exit 1
    end

    if json_str.start_with?("ERROR:")
      warn json_str
      exit 1
    end

    begin
      parsed = JSON.parse(json_str)
      json_output = JSON.pretty_generate(parsed)

      if output_file
        File.write(output_file, json_output)
        puts "Settings exported to #{output_file}"
      else
        puts json_output
      end
    rescue JSON::ParserError => e
      warn "Failed to parse JSON: #{e.message}"
      warn "Raw output: #{json_str[0..200]}"
      exit 1
    end
  end

  def cmd_load(input_file)
    unless File.exist?(input_file)
      warn "File not found: #{input_file}"
      exit 1
    end

    begin
      json_data = JSON.parse(File.read(input_file))
    rescue JSON::ParserError => e
      warn "Invalid JSON file: #{e.message}"
      exit 1
    end

    enter_settings unless @in_settings

    # Send individual SET commands for each key
    count = 0
    errors = 0

    json_data.each do |key, value|
      # Determine type and format command
      type, val_str = case value
        when TrueClass, FalseClass
          ["bool", value.to_s]
        when Integer
          if value >= 0 && value <= 255
            ["u8", value.to_s]
          elsif value >= 0 && value <= 65535
            ["u16", value.to_s]
          else
            ["u32", value.to_s]
          end
        when String
          ["str", value]
        when Hash
          if value["_blob"]
            ["blob", value["_blob"]]
          else
            puts "Skipping unsupported hash for #{key}"
            next
          end
        else
          puts "Skipping unsupported type for #{key}: #{value.class}"
          next
        end

      send_line("SET #{type} #{key} #{val_str}")
      line = read_line(timeout: 1.0)

      if line == "OK"
        count += 1
        display_val = type == "blob" ? "(#{val_str.length} bytes base64)" : val_str
        puts "  #{key} = #{display_val}"
      else
        errors += 1
        warn "  Failed to set #{key}: #{line}"
      end
    end

    puts "Imported #{count} settings" + (errors > 0 ? " (#{errors} errors)" : "")
  end
end

# ============================================================================
# CLI Entry Point
# ============================================================================

options = {}
parser = OptionParser.new do |opts|
  opts.banner = "Usage: #{$0} [options] <command> [args]"
  opts.separator ""
  opts.separator "Commands:"
  opts.separator "  list                    List all NVS keys"
  opts.separator "  get <key>               Get value for key"
  opts.separator "  set <key> <value>       Set key value (auto-detect type)"
  opts.separator "  dump [file]             Export settings as JSON"
  opts.separator "  load <file>             Import settings from JSON file"
  opts.separator "  erase <key>             Erase a specific key"
  opts.separator "  erase_all --confirm     Erase all settings"
  opts.separator ""
  opts.separator "Options:"

  opts.on("-p", "--port PORT", "Serial port") do |p|
    options[:port] = p
  end

  opts.on("-t", "--type TYPE", "Value type for set (u8/u16/u32/bool/str)") do |t|
    options[:type] = t
  end

  opts.on("--confirm", "Confirm destructive operations") do
    options[:confirm] = true
  end

  opts.on("-d", "--debug", "Show debug output") do
    options[:debug] = true
  end

  opts.on("-h", "--help", "Show this help") do
    puts opts
    exit
  end
end

begin
  parser.parse!
rescue OptionParser::InvalidOption => e
  warn e.message
  puts parser
  exit 1
end

if ARGV.empty?
  puts parser
  exit 1
end

command = ARGV.shift
port = options[:port]
unless port
  port = SSConfig.cdc_port
  puts "Using port from .vscode/settings.json: #{port}" if port
end
port ||= SSConfig.default_port

cli = AppSettingsCLI.new(port, debug: options[:debug])

begin
  cli.connect

  case command
  when "list"
    cli.cmd_list
  when "get"
    key = ARGV.shift or raise "Missing key argument"
    cli.cmd_get(key)
  when "set"
    key = ARGV.shift or raise "Missing key argument"
    value = ARGV.shift or raise "Missing value argument"
    cli.cmd_set(key, value, type: options[:type])
  when "dump"
    output_file = ARGV.shift
    cli.cmd_dump(output_file: output_file)
  when "load"
    input_file = ARGV.shift or raise "Missing file argument"
    cli.cmd_load(input_file)
  when "erase"
    key = ARGV.shift or raise "Missing key argument"
    cli.cmd_erase(key)
  when "erase_all"
    cli.cmd_erase_all(confirm: options[:confirm])
  else
    warn "Unknown command: #{command}"
    puts parser
    exit 1
  end
rescue Interrupt
  # Clean exit on Ctrl+C
  puts "\nInterrupted."
rescue => e
  warn "Error: #{e.message}"
  exit 1
ensure
  begin
    cli.disconnect
  rescue Errno::EIO, IOError, Errno::EBADF, Interrupt
    # Port already closed, in bad state, or interrupted - ignore
  end
end
