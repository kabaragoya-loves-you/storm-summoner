#!/usr/bin/env ruby

# CDC Firmware Updater for Storm Summoner
#
# Drives the FIRMWARE and ASSETS over-the-air commands over USB CDC. Note
# that ASSETS only replaces the read-only `/assets` partition (shared MIDI
# device DB + UI images); the writable `/userdata` partition holding scenes,
# user-created/cloned pedals and the device cache is never touched here.
#
# Partition-table updates and the coordinated v(N+1) -> v(N+2) split-deploy
# flow live in the web app's System Update panel — this CLI does not drive
# PARTITION_TABLE / COMMIT_PARTITION_TABLE / RAW_ASSETS_WRITE.
require 'serialport'
require_relative 'ss_config'
begin
  require 'ruby-progressbar'
  HAS_PROGRESSBAR = true
rescue LoadError
  HAS_PROGRESSBAR = false
end

class CDCUpdater
  def initialize(port_name, firmware_path)
    @port_name = port_name
    @firmware_path = firmware_path
    @port = nil
  end

  def connect
    puts "Connecting to #{@port_name}..."
    @port = SerialPort.new(@port_name, 115200, 8, 1, SerialPort::NONE)
    # Set DTR/RTS to ensure device knows we are ready
    @port.dtr = 1
    @port.rts = 1
    @port.read_timeout = 5000  # 5 second timeout
    puts "Connected!"
  end

  def disconnect
    @port.close if @port
    puts "Disconnected."
  end

  def send_command(cmd)
    puts "Sending: #{cmd}"
    @port.write("#{cmd}\n")
    @port.flush
  end

  def read_response
    # Read line by line
    begin
      response = @port.gets
      response&.chomp
    rescue => e
      puts "Read error: #{e}"
      nil
    end
  end

  def wait_for_response(expected = nil, timeout = 5)
    start_time = Time.now
    loop do
      response = read_response
      if response
        puts "Received: #{response}"
        return response if expected.nil? || response.start_with?(expected)
        
        if response.start_with?("ERROR")
          raise "Device error: #{response}"
        end
      end
      
      if Time.now - start_time > timeout
        raise "Timeout waiting for response"
      end
      
      sleep 0.01
    end
  end

  def upload_firmware
    upload_file("FIRMWARE")
  end

  def upload_assets
    upload_file("ASSETS")
  end

  def upload_file(type)
    file_path = (type == "FIRMWARE") ? @firmware_path : @assets_path
    
    unless file_path && File.exist?(file_path)
      raise "#{type} file not found: #{file_path}"
    end

    data = File.binread(file_path)
    size = data.size

    puts "\n#{type} file: #{file_path}"
    puts "Size: #{size} bytes (#{size / 1024} KB)"

    send_command("#{type} #{size}")
    wait_for_response("READY")

    puts "\nUploading #{type.downcase}..."
    
    chunk_size = 4096 # Larger chunk size for speed
    sent_bytes = 0
    start_upload_time = Time.now

    if HAS_PROGRESSBAR
      progressbar = ProgressBar.create(
        :title => "Uploading",
        :total => size,
        :format => '%t: |%B| %p%% %e'
      )
    end

    while sent_bytes < size
      chunk = data[sent_bytes, chunk_size]
      
      begin
        @port.write(chunk)
        @port.flush
      rescue => e
        puts "Write error at offset #{sent_bytes}: #{e}"
        raise e
      end
      
      sent_bytes += chunk.size

      if HAS_PROGRESSBAR
        progressbar.progress = sent_bytes
      else
        # Simple text progress fallback
        percent = (sent_bytes * 100.0 / size).to_i
        print "\rUploading: #{percent}% (#{sent_bytes}/#{size} bytes)"
        $stdout.flush
      end
    end

    if !HAS_PROGRESSBAR
      print "\n"
    end

    puts "Upload complete in #{Time.now - start_upload_time}s"

    puts "Waiting for transfer confirmation..."
    wait_for_response("TRANSFER_COMPLETE", 30) # Increased timeout for large files

    puts "\nCommitting #{type.downcase} update..."
    send_command("COMMIT")
    
    # Flash write might take a while for large asset partitions (10MB+)
    # Upload took ~400s, flash write should be faster but let's be generous
    response = wait_for_response(nil, 600) 
    if response && response.start_with?("SUCCESS")
      puts "\n✓ #{type} update successful!"
      return true
    else
      raise "Commit failed: #{response}"
    end
  end

  def run
    begin
      connect
      if @firmware_path
        upload_firmware
      end
      if @assets_path
        upload_assets
      end
      disconnect
    rescue => e
      puts "\n✗ Error: #{e.message}"
      disconnect
      exit 1
    end
  end
end

# Parse args - port is optional if ss.cdcPort is set
args = ARGV.dup
port_name = nil
firmware_path = nil
assets_path = nil

# Check if first arg looks like a port (COMx, /dev/...)
if args[0] && (args[0] =~ /^COM\d+$/i || args[0].start_with?('/dev/'))
  port_name = args.shift
else
  port_name = SSConfig.default_port
  puts "Using port: #{port_name}" if SSConfig.cdc_port
end

firmware_path = args.shift
firmware_path = nil if firmware_path == "nil"
assets_path = args.shift

if firmware_path.nil? && assets_path.nil?
  puts "Usage: ruby cdc_updater.rb [serial_port] <firmware_file> [assets_file]"
  puts "       Port defaults to ss.cdcPort from .vscode/settings.json"
  puts "Example: ruby cdc_updater.rb build/firmware.bin"
  puts "         ruby cdc_updater.rb COM3 build/firmware.bin"
  exit 1
end

updater = CDCUpdater.new(port_name, firmware_path)
if assets_path
  updater.instance_variable_set(:@assets_path, assets_path)
end
updater.run
