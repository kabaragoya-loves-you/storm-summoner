#!/usr/bin/env ruby

# Reset Storm Summoner Device via CDC
# Usage: ruby reset_device.rb [serial_port]

require 'serialport'
require_relative 'ss_config'

port_name = ARGV[0] || SSConfig.default_port
puts "Using port: #{port_name}" if ARGV.empty? && SSConfig.cdc_port

begin
  puts "Connecting to #{port_name}..."
  port = SerialPort.new(port_name, 115200, 8, 1, SerialPort::NONE)
  port.dtr = 1
  port.rts = 1
  port.read_timeout = 2000 # 2 seconds timeout

  puts "Sending RESET command..."
  port.write("RESET\n")
  port.flush

  # Read response
  loop do
    begin
      line = port.gets
      if line
        line.chomp!
        puts "Device: #{line}"
        if line.include?("RESETTING")
          puts "\n✓ Reset command accepted. Device is rebooting."
          break
        end
      end
    rescue => e
      # Timeout or error likely means device reset immediately or port closed
      puts "\n✓ Device reset (port disconnected)."
      break
    end
  end

  port.close
rescue => e
  puts "\n✗ Error: #{e.message}"
  exit 1
end

