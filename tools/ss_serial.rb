#!/usr/bin/env ruby
# frozen_string_literal: true

# Shared serial communication base class for Storm Summoner Ruby tools
# Provides common connect/disconnect, send/receive, and mode management

require 'serialport'
require_relative 'ss_config'

class SSSerial
  READ_TIMEOUT = 2.0

  attr_reader :port_name, :debug, :current_mode

  def initialize(port_name, debug: false)
    @port_name = port_name
    @port = nil
    @current_mode = nil
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
      leave_mode if @current_mode
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
    loop do
      line = read_line(timeout: timeout)
      break if line.nil?
      break if line == "END"
      lines << line
    end
    lines
  end

  # Enter a USB CDC mode (e.g., SETTINGS, CONFIG, UPDATE)
  # Returns true on success, false on failure
  def enter_mode(mode_name, expected_response: "#{mode_name}_STARTED")
    return true if @current_mode == mode_name

    # Leave current mode first if any
    leave_mode if @current_mode

    flush_input
    send_line("")
    sleep 0.1
    flush_input
    send_line(mode_name)

    line = read_line(timeout: 3.0)
    if line == expected_response
      @current_mode = mode_name
      puts "Entered #{mode_name} mode."
      true
    else
      raise "Failed to enter #{mode_name} mode (got: #{line.inspect})"
    end
  end

  def leave_mode
    return unless @current_mode

    send_line("EXIT")
    read_line(timeout: 1.0)
    puts "Left #{@current_mode} mode." if @debug
    @current_mode = nil
  end

  # Read a potentially large JSON response
  def read_json_response(timeout: 5.0)
    json_str = read_line(timeout: timeout)

    if json_str.nil? || json_str.empty?
      raise "No response from device"
    end

    if json_str.start_with?("ERROR:")
      raise json_str
    end

    JSON.parse(json_str)
  rescue JSON::ParserError => e
    raise "Failed to parse JSON: #{e.message}\nRaw: #{json_str[0..200]}"
  end
end
