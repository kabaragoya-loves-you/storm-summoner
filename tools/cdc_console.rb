#!/usr/bin/env ruby

# CDC Console Client for Storm Summoner
# Tests the console mode functionality over USB CDC

require 'serialport'
require 'io/console'

class CDCConsole
  def initialize(port_name)
    @port_name = port_name
    @port = nil
    @running = false
    @interrupted = false
    @last_command = nil  # Track last command to filter device echo
  end

  def connect
    puts "Connecting to #{@port_name}..."
    @port = SerialPort.new(@port_name, 115200, 8, 1, SerialPort::NONE)
    @port.dtr = 1
    @port.rts = 1
    @port.read_timeout = 100  # Short timeout for responsive reading
    
    # Wait for connection to stabilize and flush any garbage
    sleep 0.5
    flush_input
    
    puts "Connected!"
  end

  def flush_input
    # Clear any pending input
    begin
      while @port.ready?
        @port.getc
      end
    rescue
      # Ignore errors
    end
  end

  def disconnect
    @port.close if @port
    puts "\nDisconnected."
  end

  def send(data)
    @port.write(data)
    @port.flush
  end

  def send_line(data)
    send("#{data}\n")
  end

  def start_console_mode
    puts "Starting console mode..."
    
    # Clear any pending data and send newline to reset state
    flush_input
    send("\n")
    sleep 0.1
    flush_input
    
    send_line("CONSOLE")
    
    # Wait for CONSOLE_STARTED response
    start_time = Time.now
    loop do
      line = read_line
      if line
        puts "< #{line}"
        break if line.include?("CONSOLE_STARTED") || line.include?("===")
      end
      break if Time.now - start_time > 3
    end
  end

  def stop_console_mode
    puts "\nStopping console mode..."
    begin
      send_line("exit")
      
      # Wait for CONSOLE_STOPPED response with short timeout
      start_time = Time.now
      loop do
        line = read_line
        if line
          puts "< #{line}"
          break if line.include?("CONSOLE_STOPPED")
        end
        break if Time.now - start_time > 1
      end
    rescue Errno::EIO, IOError, Errno::EBADF => e
      # Port may already be closed or in bad state
      puts "(port unavailable: #{e.message})"
    end
  end

  def read_line
    begin
      line = @port.gets
      line&.chomp
    rescue => e
      nil
    end
  end

  def read_available
    result = ""
    begin
      # Read with short timeout - serialport returns what's available
      while true
        byte = @port.getbyte
        break if byte.nil?
        result += byte.chr
      end
    rescue Errno::EAGAIN, IOError
      # No more data
    rescue => e
      # Ignore other errors  
    end
    result
  end

  def interactive_session
    puts "\n" + "="*50
    puts "Interactive Console Session"
    puts "="*50
    puts "Type commands and press Enter."
    puts "Press Ctrl+C to exit."
    puts "="*50 + "\n"

    @running = true
    
    # Start a reader thread
    reader_thread = Thread.new do
      line_buffer = ""
      while @running
        begin
          data = read_available
          unless data.empty?
            # Process line by line to filter echoes
            line_buffer += data
            while line_buffer.include?("\n")
              idx = line_buffer.index("\n")
              line = line_buffer[0...idx].gsub("\r", "")
              line_buffer = line_buffer[(idx + 1)..]
              
              # Skip if this line is just an echo of our last command
              if @last_command && line == @last_command
                @last_command = nil  # Only filter once
                next
              end
              
              puts line
            end
            # Print any partial line (prompt, etc)
            unless line_buffer.empty?
              print line_buffer
              line_buffer = ""
            end
          end
        rescue => e
          # Ignore
        end
        sleep 0.01
      end
    end

    # Main input loop
    begin
      while @running
        # Use IO.select to make stdin interruptible
        ready = IO.select([STDIN], nil, nil, 0.1)
        next unless ready
        
        line = STDIN.gets
        break unless line
        
        line = line.chomp
        
        # Track command to filter echo, then send
        @last_command = line
        send_line(line)
        
        # If user types 'exit', we should stop
        if line.downcase == 'exit'
          sleep 0.5  # Give time to receive response
          break
        end
      end
    rescue Interrupt
      puts "\n\nInterrupted by user"
      @running = false
      @interrupted = true
    ensure
      @running = false
      reader_thread.join(0.5) rescue nil
    end
  end

  def run
    begin
      connect
      start_console_mode
      interactive_session
      # Only try graceful exit if not interrupted
      stop_console_mode unless @interrupted
    rescue Interrupt
      # Second interrupt during cleanup - just bail
    rescue => e
      puts "\nError: #{e.message}"
      puts e.backtrace.first(5).join("\n")
    ensure
      disconnect rescue nil
    end
  end

  # Simple test mode - just start/stop console without interaction
  def test
    begin
      connect
      
      # Send a bare newline first to clear any partial buffer
      send("\n")
      sleep 0.2
      flush_input
      
      puts "\n--- Testing CONSOLE command ---"
      send_line("CONSOLE")
      
      # Wait for and display response
      wait_and_print(2.0)
      
      puts "\n--- Sending 'help' command ---"
      send_line("help")
      wait_and_print(2.0)
      
      puts "\n--- Sending 'contexts' command ---"
      send_line("contexts")
      wait_and_print(2.0)
      
      puts "\n--- Exiting console mode ---"
      send_line("exit")
      wait_and_print(1.0)
      
      puts "\n--- Test complete ---"
      disconnect
      
    rescue => e
      puts "\nError: #{e.message}"
      puts e.backtrace.first(3).join("\n")
      disconnect
      exit 1
    end
  end

  def wait_and_print(timeout)
    start = Time.now
    got_data = false
    while Time.now - start < timeout
      data = read_available
      unless data.empty?
        print data
        $stdout.flush
        got_data = true
      end
      sleep 0.02
    end
    puts "[no data received]" unless got_data
  end
end

if ARGV.length < 1
  puts "Usage: ruby cdc_console.rb <serial_port> [--test]"
  puts ""
  puts "Examples:"
  puts "  ruby cdc_console.rb COM3           # Interactive console session"
  puts "  ruby cdc_console.rb COM3 --test    # Quick test of console mode"
  puts "  ruby cdc_console.rb /dev/ttyACM0   # Linux example"
  exit 1
end

port_name = ARGV[0]
test_mode = ARGV[1] == "--test"

console = CDCConsole.new(port_name)

if test_mode
  console.test
else
  console.run
end

