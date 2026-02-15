#!/usr/bin/env ruby
# frozen_string_literal: true

# Device Settings CLI Tool for Storm Summoner
# Manage user-facing device settings via USB CDC CONFIG mode
# Uses the settings.schema.json schema for structured access

require 'json'
require 'optparse'
require_relative 'ss_serial'

class DeviceSettingsCLI < SSSerial
  SCHEMA_PATH = File.expand_path('../../schemas/settings.schema.json', __FILE__)
  CONFIG_TIMEOUT = 3.0

  def initialize(port_name, debug: false)
    super(port_name, debug: debug)
    @schema = nil
  end

  def load_schema
    unless File.exist?(SCHEMA_PATH)
      raise "Schema not found: #{SCHEMA_PATH}"
    end
    @schema = JSON.parse(File.read(SCHEMA_PATH))
  rescue JSON::ParserError => e
    raise "Invalid schema JSON: #{e.message}"
  end

  def enter_config
    enter_mode("CONFIG", expected_response: "CONFIG_STARTED")
  end

  # ============================================================================
  # Commands
  # ============================================================================

  # List all available settings from schema
  def cmd_list
    load_schema unless @schema

    puts "\nAvailable Settings:\n\n"

    @schema["categories"].each do |category|
      puts "#{category['label']} (#{category['id']})"
      puts "  #{category['description']}"
      puts

      category["settings"].each do |setting|
        type_info = case setting["type"]
        when "select"
          options = setting["options"].map { |o| o["label"] }.join(", ")
          "select [#{options}]"
        when "toggle"
          "toggle (on/off)"
        when "number"
          range = "#{setting['min']}-#{setting['max']}"
          range += " step #{setting['step']}" if setting['step'] && setting['step'] != 1
          range += " #{setting['unit']}" if setting['unit']
          "number (#{range})"
        when "calibration"
          "calibration (device only)"
        else
          setting["type"]
        end

        puts "    #{setting['id']}"
        puts "      #{setting['description']}"
        puts "      Type: #{type_info}"
        puts "      Default: #{setting['default']}" if setting.key?('default')
        puts
      end
    end
  end

  # Get current value of a setting
  def cmd_get(setting_id)
    enter_config

    send_line("GET #{setting_id}")
    line = read_line(timeout: CONFIG_TIMEOUT)

    if line.nil?
      warn "No response from device"
      exit 1
    elsif line.start_with?("ERROR:")
      warn line
      exit 1
    else
      # Look up setting in schema to format output
      setting = find_setting(setting_id)
      if setting && setting["type"] == "select"
        value = line.to_i
        option = setting["options"].find { |o| o["value"] == value }
        label = option ? option["label"] : "Unknown"
        puts "#{setting_id} = #{value} (#{label})"
      elsif setting && setting["type"] == "toggle"
        puts "#{setting_id} = #{line == '1' ? 'on' : 'off'}"
      else
        puts "#{setting_id} = #{line}"
      end
    end
  end

  # Set value of a setting
  def cmd_set(setting_id, value)
    load_schema unless @schema
    setting = find_setting(setting_id)

    unless setting
      warn "Unknown setting: #{setting_id}"
      warn "Use 'list' to see available settings"
      exit 1
    end

    if setting["type"] == "calibration"
      warn "Calibration settings must be performed on the device"
      exit 1
    end

    # Validate and normalize value
    normalized_value = normalize_value(setting, value)

    enter_config

    send_line("SET #{setting_id} #{normalized_value}")
    line = read_line(timeout: CONFIG_TIMEOUT)

    if line == "OK"
      puts "Set #{setting_id} = #{value}"
    else
      warn "Failed: #{line}"
      exit 1
    end
  end

  # Get all current values
  def cmd_values(output_file: nil)
    enter_config

    send_line("VALUES")
    values = read_json_response(timeout: 5.0)

    if output_file
      File.write(output_file, JSON.pretty_generate(values))
      puts "Settings exported to #{output_file}"
    else
      puts JSON.pretty_generate(values)
    end
  end

  # Load values from JSON file
  def cmd_load(input_file)
    unless File.exist?(input_file)
      warn "File not found: #{input_file}"
      exit 1
    end

    begin
      values = JSON.parse(File.read(input_file))
    rescue JSON::ParserError => e
      warn "Invalid JSON file: #{e.message}"
      exit 1
    end

    load_schema unless @schema
    enter_config

    count = 0
    errors = 0

    values.each do |setting_id, value|
      setting = find_setting(setting_id)

      unless setting
        puts "  Skipping unknown setting: #{setting_id}"
        next
      end

      if setting["type"] == "calibration"
        puts "  Skipping calibration: #{setting_id}"
        next
      end

      send_line("SET #{setting_id} #{value}")
      line = read_line(timeout: 1.0)

      if line == "OK"
        count += 1
        puts "  #{setting_id} = #{value}"
      else
        errors += 1
        warn "  Failed to set #{setting_id}: #{line}"
      end
    end

    puts "Imported #{count} settings" + (errors > 0 ? " (#{errors} errors)" : "")
  end

  # Factory reset
  def cmd_factory_reset(confirm: false)
    unless confirm
      warn "This will erase ALL settings and restart the device."
      warn "Use --confirm to proceed."
      exit 1
    end

    enter_config

    puts "Erasing all settings..."
    send_line("FACTORY_RESET")
    line = read_line(timeout: 5.0)

    if line == "OK"
      puts "Factory reset complete. Device is restarting..."
      @current_mode = nil # Device will disconnect
    else
      warn "Factory reset failed: #{line}"
      exit 1
    end
  end

  # Show count of registered settings
  def cmd_count
    enter_config

    send_line("COUNT")
    line = read_line(timeout: CONFIG_TIMEOUT)

    if line
      puts "Device has #{line} registered settings"
    else
      warn "No response"
      exit 1
    end
  end

  # Interactive REPL mode
  def cmd_interactive
    load_schema unless @schema
    enter_config

    puts "\nInteractive CONFIG mode. Type 'help' for commands, Ctrl-C to exit.\n\n"

    loop do
      print "config> "
      begin
        input = $stdin.gets
      rescue Interrupt
        puts
        break
      end

      break if input.nil? # EOF

      line = input.strip
      next if line.empty?

      args = line.split(/\s+/)
      cmd = args.shift.downcase

      case cmd
      when "help", "?"
        puts <<~HELP

          Commands:
            get <id>            Get current value of a setting
            set <id> <value>    Set a setting value
            values              Show all current values
            count               Show number of registered settings
            list                List available settings (from schema)
            exit, quit, q       Exit interactive mode

        HELP

      when "get"
        setting_id = args.shift
        unless setting_id
          puts "Usage: get <setting_id>"
          next
        end
        interactive_get(setting_id)

      when "set"
        setting_id = args.shift
        value = args.join(" ")
        if setting_id.nil? || value.empty?
          puts "Usage: set <setting_id> <value>"
          next
        end
        interactive_set(setting_id, value)

      when "values"
        interactive_values

      when "count"
        send_line("COUNT")
        response = read_line(timeout: CONFIG_TIMEOUT)
        puts response ? "#{response} registered settings" : "No response"

      when "list"
        cmd_list

      when "exit", "quit", "q"
        break

      else
        puts "Unknown command: #{cmd}. Type 'help' for available commands."
      end
    end

    puts "Exiting interactive mode."
  end

  private

  def interactive_get(setting_id)
    send_line("GET #{setting_id}")
    line = read_line(timeout: CONFIG_TIMEOUT)

    if line.nil?
      puts "No response"
    elsif line.start_with?("ERROR:")
      puts line
    else
      setting = find_setting(setting_id)
      if setting && setting["type"] == "select"
        value = line.to_i
        option = setting["options"].find { |o| o["value"] == value }
        label = option ? option["label"] : "Unknown"
        puts "#{setting_id} = #{value} (#{label})"
      elsif setting && setting["type"] == "toggle"
        puts "#{setting_id} = #{line == '1' ? 'on' : 'off'}"
      else
        puts "#{setting_id} = #{line}"
      end
    end
  end

  def interactive_set(setting_id, value)
    setting = find_setting(setting_id)

    unless setting
      puts "Unknown setting: #{setting_id}"
      return
    end

    if setting["type"] == "calibration"
      puts "Calibration must be performed on device"
      return
    end

    begin
      normalized_value = normalize_value(setting, value)
    rescue => e
      puts e.message
      return
    end

    send_line("SET #{setting_id} #{normalized_value}")
    line = read_line(timeout: CONFIG_TIMEOUT)

    if line == "OK"
      puts "#{setting_id} = #{value}"
    else
      puts "Failed: #{line}"
    end
  end

  def interactive_values
    send_line("VALUES")
    begin
      values = read_json_response(timeout: 5.0)
      puts JSON.pretty_generate(values)
    rescue => e
      puts "Error: #{e.message}"
    end
  end

  def find_setting(setting_id)
    load_schema unless @schema

    @schema["categories"].each do |category|
      category["settings"].each do |setting|
        return setting if setting["id"] == setting_id
      end
    end
    nil
  end

  def normalize_value(setting, value)
    case setting["type"]
    when "toggle"
      # Accept: true/false, 1/0, on/off, yes/no
      case value.to_s.downcase
      when "true", "1", "on", "yes"
        1
      when "false", "0", "off", "no"
        0
      else
        raise "Invalid toggle value: #{value} (use on/off, true/false, 1/0)"
      end
    when "select"
      # Accept numeric value or label text
      if value =~ /^\d+$/
        int_val = value.to_i
        unless setting["options"].any? { |o| o["value"] == int_val }
          valid = setting["options"].map { |o| "#{o['value']} (#{o['label']})" }.join(", ")
          raise "Invalid option: #{value}. Valid: #{valid}"
        end
        int_val
      else
        # Try to match by label
        option = setting["options"].find { |o|
          o["label"].downcase == value.to_s.downcase
        }
        unless option
          valid = setting["options"].map { |o| o["label"] }.join(", ")
          raise "Unknown option: #{value}. Valid: #{valid}"
        end
        option["value"]
      end
    when "number"
      int_val = value.to_i
      if setting["min"] && int_val < setting["min"]
        raise "Value #{int_val} is below minimum #{setting['min']}"
      end
      if setting["max"] && int_val > setting["max"]
        raise "Value #{int_val} is above maximum #{setting['max']}"
      end
      int_val
    else
      value
    end
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
  opts.separator "  interactive, i          Interactive REPL mode (Ctrl-C to exit)"
  opts.separator "  list                    List all available settings from schema"
  opts.separator "  get <id>                Get current value of a setting"
  opts.separator "  set <id> <value>        Set a setting value"
  opts.separator "  values [file]           Get all current values as JSON"
  opts.separator "  load <file>             Load settings from JSON file"
  opts.separator "  count                   Show number of registered settings"
  opts.separator "  factory_reset --confirm Erase all settings and restart"
  opts.separator ""
  opts.separator "Options:"

  opts.on("-p", "--port PORT", "Serial port") do |p|
    options[:port] = p
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

cli = DeviceSettingsCLI.new(port, debug: options[:debug])

begin
  case command
  when "list"
    # List doesn't require connection, just schema
    cli.cmd_list
  else
    cli.connect

    case command
    when "interactive", "i"
      cli.cmd_interactive
    when "get"
      setting_id = ARGV.shift or raise "Missing setting ID argument"
      cli.cmd_get(setting_id)
    when "set"
      setting_id = ARGV.shift or raise "Missing setting ID argument"
      value = ARGV.shift or raise "Missing value argument"
      cli.cmd_set(setting_id, value)
    when "values"
      output_file = ARGV.shift
      cli.cmd_values(output_file: output_file)
    when "load"
      input_file = ARGV.shift or raise "Missing file argument"
      cli.cmd_load(input_file)
    when "count"
      cli.cmd_count
    when "factory_reset"
      cli.cmd_factory_reset(confirm: options[:confirm])
    else
      warn "Unknown command: #{command}"
      puts parser
      exit 1
    end

    cli.disconnect
  end
rescue Interrupt
  puts "\nInterrupted."
rescue => e
  warn "Error: #{e.message}"
  exit 1
ensure
  begin
    cli.disconnect if cli.instance_variable_get(:@port)
  rescue Errno::EIO, IOError, Errno::EBADF, Interrupt
    # Port already closed, in bad state, or interrupted - ignore
  end
end
