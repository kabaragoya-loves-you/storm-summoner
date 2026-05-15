#!/usr/bin/env ruby

# Assets Manager CLI for Storm Summoner
# Manage files on the LittleFS assets volumes over USB CDC.
#
# Partition layout (post-split):
#   /assets    -- read-only shared content (replaced by Assets OTA only)
#   /userdata  -- read-write user content (scenes, custom pedals, cache)
#
# Mutating commands (mkdir/rm/rmrf/mv/cp/upload/extract) only work under
# /userdata. The device enforces this; this CLI also pre-warns for clarity.
#
# Usage:
#   ruby assets_manager.rb <port> <command> [args...]
#
# Commands:
#   ls [path]                    - List directory contents (default /userdata)
#   stat <path>                  - Get file/directory info
#   df                           - Show filesystem usage (both partitions)
#   cat <path>                   - Display file contents (text files <4KB)
#   mkdir <path>                 - Create directory (RW only)
#   rm <path>                    - Remove file or empty directory (RW only)
#   rmrf <path>                  - Recursively delete directory and contents (RW only)
#   mv <src> <dst>               - Move/rename file or directory (RW only)
#   cp <src> <dst>               - Copy file (RW destination only; src may be RO)
#   upload <local> <remote>      - Upload file to device (RW only)
#   download <remote> <local>    - Download file from device
#   manifest [type]              - Show manifest (scenes|shared_devices|user_devices|images)
#   zip <remote_path> [local.zip] - Download folder as ZIP archive
#   extract <local.zip> <remote_path> - Upload and extract ZIP to folder (RW only)
#   interactive                  - Enter interactive mode

require 'serialport'
require 'json'
require 'fileutils'

class AssetsManager
  TIMEOUT_SHORT = 2
  TIMEOUT_LONG = 30
  USERDATA_ROOT = '/userdata'.freeze
  ASSETS_ROOT = '/assets'.freeze

  def initialize(port_name)
    @port_name = port_name
    @port = nil
    @in_assets_mode = false
  end

  def connect
    puts "Connecting to #{@port_name}..."
    @port = SerialPort.new(@port_name, 115200, 8, 1, SerialPort::NONE)
    @port.dtr = 1
    @port.rts = 1
    @port.read_timeout = 100
    
    sleep 0.3
    flush_input
    puts "Connected."
  end

  def disconnect
    exit_assets_mode if @in_assets_mode
    @port.close if @port
    @port = nil
  end

  def flush_input
    begin
      while @port.ready?
        @port.getc
      end
    rescue
    end
  end

  def send_line(data)
    @port.write("#{data}\n")
    @port.flush
  end

  def read_line(timeout = TIMEOUT_SHORT)
    start_time = Time.now
    line = ""
    
    while Time.now - start_time < timeout
      begin
        byte = @port.getbyte
        if byte
          char = byte.chr
          if char == "\n"
            return line.gsub("\r", "").strip
          else
            line += char
          end
        end
      rescue Errno::EAGAIN
        sleep 0.01
      end
    end
    
    line.empty? ? nil : line.gsub("\r", "").strip
  end

  def read_binary(size, timeout = TIMEOUT_LONG)
    data = ""
    start_time = Time.now
    
    while data.length < size && Time.now - start_time < timeout
      begin
        remaining = size - data.length
        chunk = @port.read([remaining, 1024].min)
        data += chunk if chunk
      rescue Errno::EAGAIN
        sleep 0.01
      end
    end
    
    data
  end

  def enter_assets_mode
    return if @in_assets_mode
    
    flush_input
    send_line("ASSETS")
    
    response = read_line
    if response == "ASSETS_STARTED"
      @in_assets_mode = true
      puts "Entered assets mode."
    else
      raise "Failed to enter assets mode: #{response}"
    end
  end

  def exit_assets_mode
    return unless @in_assets_mode
    
    send_line("EXIT")
    response = read_line
    @in_assets_mode = false
    puts "Exited assets mode." if response == "ASSETS_STOPPED"
  end

  def assets_command(cmd)
    enter_assets_mode unless @in_assets_mode
    send_line(cmd)
    read_line(TIMEOUT_SHORT)
  end

  # Returns true iff `path` lies on the writable /userdata partition. The
  # device rejects RO mutations on its own; we pre-warn here so users get a
  # friendlier message and don't waste a round-trip.
  def writable_path?(path)
    return false if path.nil? || path.empty?
    path == USERDATA_ROOT || path.start_with?("#{USERDATA_ROOT}/")
  end

  def warn_readonly(path)
    puts "WARNING: #{path} is on the read-only /assets partition."
    puts "         Shared content is replaced wholesale by the Assets OTA flow."
    puts "         Writes go to /userdata/... — see the System Update tab in the web app."
  end

  # ============================================================================
  # Commands
  # ============================================================================

  def cmd_ls(path = "/")
    response = assets_command("LS #{path}")
    
    if response&.start_with?("ERROR")
      puts response
      return
    end
    
    begin
      entries = JSON.parse(response)
      
      if entries.empty?
        puts "(empty directory)"
        return
      end
      
      # Sort: directories first, then files, alphabetically
      entries.sort_by! { |e| [e["type"] == "dir" ? 0 : 1, e["name"].downcase] }
      
      puts ""
      entries.each do |entry|
        if entry["type"] == "dir"
          puts "  #{entry['name']}/"
        else
          size_str = format_size(entry["size"])
          puts "  #{entry['name'].ljust(40)} #{size_str.rjust(10)}"
        end
      end
      puts ""
      puts "#{entries.count { |e| e['type'] == 'dir' }} directories, #{entries.count { |e| e['type'] == 'file' }} files"
      
    rescue JSON::ParserError => e
      puts "Parse error: #{e.message}"
      puts "Raw response: #{response}"
    end
  end

  def cmd_stat(path)
    response = assets_command("STAT #{path}")
    
    if response&.start_with?("ERROR")
      puts response
      return
    end
    
    begin
      info = JSON.parse(response)
      puts "Path: #{path}"
      puts "Type: #{info['type']}"
      puts "Size: #{format_size(info['size'])} (#{info['size']} bytes)" if info['type'] == 'file'
    rescue JSON::ParserError => e
      puts "Parse error: #{e.message}"
    end
  end

  def cmd_df
    response = assets_command("DF")

    if response&.start_with?("ERROR")
      puts response
      return
    end

    begin
      info = JSON.parse(response)
      print_df_partition("/assets (RO)", info["assets"]) if info["assets"]
      if info["userdata"] && info["userdata"]["available"]
        print_df_partition("/userdata (RW)", info["userdata"])
      elsif info["userdata"]
        puts ""
        puts "/userdata: NOT AVAILABLE (degraded boot — re-run System Update from the web app)"
        puts ""
      end
    rescue JSON::ParserError => e
      puts "Parse error: #{e.message}"
      puts "Raw response: #{response}"
    end
  end

  def print_df_partition(label, info)
    total = info["total"].to_i
    used = info["used"].to_i
    free = info["free"].to_i
    percent = total > 0 ? (used.to_f / total * 100).round(1) : 0.0

    puts ""
    puts "Filesystem: #{label}"
    puts "  Total:  #{format_size(total)}"
    puts "  Used:   #{format_size(used)} (#{percent}%)"
    puts "  Free:   #{format_size(free)}"

    bar_width = 40
    filled = total > 0 ? (bar_width * used.to_f / total).round : 0
    bar = "[" + "#" * filled + "-" * (bar_width - filled) + "]"
    puts "  #{bar}"
    puts ""
  end

  def cmd_cat(path)
    response = assets_command("CAT #{path}")
    puts response
  end

  def cmd_mkdir(path)
    warn_readonly(path) unless writable_path?(path)
    response = assets_command("MKDIR #{path}")
    puts response
  end

  def cmd_rm(path)
    warn_readonly(path) unless writable_path?(path)
    response = assets_command("RM #{path}")
    puts response
  end

  def cmd_rmrf(path)
    warn_readonly(path) unless writable_path?(path)
    response = assets_command("RMRF #{path}")
    puts response
  end

  def cmd_mv(src, dst)
    warn_readonly(src) unless writable_path?(src)
    warn_readonly(dst) unless writable_path?(dst)
    response = assets_command("MV #{src} #{dst}")
    puts response
  end

  def cmd_cp(src, dst)
    # Source may be RO (legitimate "seed from shared" use case); only the
    # destination has to be writable, matching the device-side gate.
    warn_readonly(dst) unless writable_path?(dst)
    response = assets_command("CP #{src} #{dst}")
    puts response
  end

  def cmd_manifest(type = "scenes")
    response = assets_command("MANIFEST #{type}")
    
    if response&.start_with?("ERROR")
      puts response
      return
    end
    
    begin
      manifest = JSON.parse(response)
      puts JSON.pretty_generate(manifest)
    rescue JSON::ParserError
      puts response
    end
  end

  def cmd_upload(local_path, remote_path)
    unless File.exist?(local_path)
      puts "ERROR: Local file not found: #{local_path}"
      return
    end

    warn_readonly(remote_path) unless writable_path?(remote_path)

    file_size = File.size(local_path)
    puts "Uploading #{local_path} (#{format_size(file_size)}) to #{remote_path}..."

    enter_assets_mode unless @in_assets_mode
    
    send_line("PUT #{remote_path} #{file_size}")
    response = read_line
    
    unless response == "READY"
      puts "ERROR: Device not ready: #{response}"
      return
    end
    
    # Send file data
    File.open(local_path, "rb") do |f|
      sent = 0
      while (chunk = f.read(1024))
        @port.write(chunk)
        @port.flush
        sent += chunk.length
        
        progress = (sent.to_f / file_size * 100).round
        print "\rProgress: #{progress}% (#{format_size(sent)})   "
      end
    end
    puts ""
    
    response = read_line(TIMEOUT_LONG)
    puts response == "OK" ? "Upload complete." : "Upload failed: #{response}"
  end

  def cmd_download(remote_path, local_path)
    enter_assets_mode unless @in_assets_mode
    
    send_line("GET #{remote_path}")
    response = read_line
    
    if response&.start_with?("ERROR")
      puts response
      return
    end
    
    unless response&.start_with?("SIZE ")
      puts "ERROR: Unexpected response: #{response}"
      return
    end
    
    file_size = response.split(" ")[1].to_i
    puts "Downloading #{remote_path} (#{format_size(file_size)}) to #{local_path}..."
    
    # Receive binary data
    data = ""
    received = 0
    start_time = Time.now
    
    while received < file_size && Time.now - start_time < TIMEOUT_LONG
      begin
        chunk = @port.read([file_size - received, 1024].min)
        if chunk && !chunk.empty?
          data += chunk
          received += chunk.length
          progress = (received.to_f / file_size * 100).round
          print "\rProgress: #{progress}% (#{format_size(received)})   "
        end
      rescue Errno::EAGAIN
        sleep 0.01
      end
    end
    puts ""
    
    if received == file_size
      File.open(local_path, "wb") { |f| f.write(data) }
      puts "Download complete."
    else
      puts "ERROR: Incomplete download (#{received}/#{file_size} bytes)"
    end
  end

  def cmd_zip(remote_path, local_path = nil)
    # Generate default filename from folder name
    if local_path.nil?
      folder_name = File.basename(remote_path)
      folder_name = "assets" if folder_name.empty? || folder_name == "/"
      local_path = "#{folder_name}.zip"
    end
    
    enter_assets_mode unless @in_assets_mode
    
    puts "Creating ZIP archive of #{remote_path}..."
    
    send_line("ZIP #{remote_path}")
    response = read_line(TIMEOUT_LONG)  # Archive creation can take time
    
    if response&.start_with?("ERROR")
      puts response
      return
    end
    
    unless response&.start_with?("SIZE ")
      puts "ERROR: Unexpected response: #{response}"
      return
    end
    
    archive_size = response.split(" ")[1].to_i
    puts "Archive size: #{format_size(archive_size)}"
    puts "Downloading to #{local_path}..."
    
    # Receive binary data
    data = ""
    received = 0
    start_time = Time.now
    
    while received < archive_size && Time.now - start_time < TIMEOUT_LONG
      begin
        chunk = @port.read([archive_size - received, 4096].min)
        if chunk && !chunk.empty?
          data += chunk
          received += chunk.length
          progress = (received.to_f / archive_size * 100).round
          print "\rProgress: #{progress}% (#{format_size(received)})   "
        end
      rescue Errno::EAGAIN
        sleep 0.01
      end
    end
    puts ""
    
    if received == archive_size
      File.open(local_path, "wb") { |f| f.write(data) }
      puts "Archive saved to #{local_path}"
    else
      puts "ERROR: Incomplete download (#{received}/#{archive_size} bytes)"
    end
  end

  def cmd_extract(local_path, remote_path)
    unless File.exist?(local_path)
      puts "ERROR: Local file not found: #{local_path}"
      return
    end

    warn_readonly(remote_path) unless writable_path?(remote_path)

    file_size = File.size(local_path)
    puts "Extracting #{local_path} (#{format_size(file_size)}) to #{remote_path}..."

    enter_assets_mode unless @in_assets_mode
    
    send_line("EXTRACT #{remote_path} #{file_size}")
    response = read_line
    
    unless response == "READY"
      puts "ERROR: Device not ready: #{response}"
      return
    end
    
    # Send ZIP data
    File.open(local_path, "rb") do |f|
      sent = 0
      while (chunk = f.read(4096))
        @port.write(chunk)
        @port.flush
        sent += chunk.length
        
        progress = (sent.to_f / file_size * 100).round
        print "\rUploading: #{progress}% (#{format_size(sent)})   "
      end
    end
    puts ""
    
    puts "Extracting..."
    response = read_line(TIMEOUT_LONG)  # Extraction can take time
    puts response == "OK" ? "Extraction complete." : "Extraction failed: #{response}"
  end

  def cmd_interactive
    enter_assets_mode
    
    puts ""
    puts "=" * 50
    puts "Interactive Assets Mode"
    puts "=" * 50
    puts "Commands: ls, stat, df, cat, mkdir, rm, rmrf, mv, cp, manifest"
    puts "          upload <local> <remote>, download <remote> <local>"
    puts "          zip <path> [output.zip], extract <local.zip> <path>"
    puts "Type 'exit' or 'quit' to leave"
    puts "=" * 50
    puts ""
    
    loop do
      print "assets> "
      input = STDIN.gets&.chomp
      break unless input
      
      parts = input.split(/\s+/)
      cmd = parts[0]&.downcase
      args = parts[1..]
      
      case cmd
      when "exit", "quit", nil
        break
      when "ls"
        cmd_ls(args[0] || USERDATA_ROOT)
      when "stat"
        cmd_stat(args[0]) if args[0]
      when "df"
        cmd_df
      when "cat"
        cmd_cat(args[0]) if args[0]
      when "mkdir"
        cmd_mkdir(args[0]) if args[0]
      when "rm"
        cmd_rm(args[0]) if args[0]
      when "rmrf"
        cmd_rmrf(args[0]) if args[0]
      when "mv"
        cmd_mv(args[0], args[1]) if args[0] && args[1]
      when "cp"
        cmd_cp(args[0], args[1]) if args[0] && args[1]
      when "manifest"
        cmd_manifest(args[0] || "scenes")
      when "upload"
        cmd_upload(args[0], args[1]) if args[0] && args[1]
      when "download"
        cmd_download(args[0], args[1]) if args[0] && args[1]
      when "zip"
        cmd_zip(args[0], args[1]) if args[0]
      when "extract"
        cmd_extract(args[0], args[1]) if args[0] && args[1]
      when "help"
        puts "Commands: ls, stat, df, cat, mkdir, rm, rmrf, mv, cp, manifest, upload, download, zip, extract, exit"
      else
        puts "Unknown command: #{cmd}" unless cmd.to_s.empty?
      end
    end
  end

  private

  def format_size(bytes)
    return "0 B" if bytes == 0
    
    units = ["B", "KB", "MB", "GB"]
    exp = (Math.log(bytes) / Math.log(1024)).to_i
    exp = units.length - 1 if exp >= units.length
    
    if exp == 0
      "#{bytes} B"
    else
      "%.1f %s" % [bytes.to_f / (1024 ** exp), units[exp]]
    end
  end
end

# ============================================================================
# Main
# ============================================================================

def print_usage
  puts <<~USAGE
    Storm Summoner Assets Manager
    
    Usage: ruby assets_manager.rb <port> <command> [args...]
    
    Commands:
      ls [path]                    - List directory contents
      stat <path>                  - Get file/directory info
      df                           - Show filesystem usage
      cat <path>                   - Display file contents (text files <4KB)
      mkdir <path>                 - Create directory
      rm <path>                    - Remove file or empty directory
      rmrf <path>                  - Recursively delete directory and contents
      mv <src> <dst>               - Move/rename file or directory
      cp <src> <dst>               - Copy file
      upload <local> <remote>      - Upload file to device
      download <remote> <local>    - Download file from device
      manifest [type]              - Show manifest (scenes|devices|images)
      zip <path> [output.zip]      - Download folder as ZIP archive
      extract <local.zip> <path>   - Upload and extract ZIP to folder
      interactive                  - Enter interactive mode
    
    Examples:
      ruby assets_manager.rb COM3 ls /userdata/scenes
      ruby assets_manager.rb COM3 upload scene_003.json /userdata/scenes/scene_003.json
      ruby assets_manager.rb COM3 download /userdata/scenes/scene_001.json scene_001.json
      ruby assets_manager.rb COM3 zip /userdata/scenes scenes_backup.zip
      ruby assets_manager.rb COM3 df
      ruby assets_manager.rb COM3 interactive

      ruby assets_manager.rb /dev/ttyACM0 ls /userdata  # Linux example

    Notes:
      `ls /` returns the two roots (assets, userdata). `ls` with no path
      defaults to /userdata. The /assets partition is read-only; mutating
      commands targeting /assets are rejected by the device.
  USAGE
end

if ARGV.length < 2
  print_usage
  exit 1
end

port = ARGV[0]
command = ARGV[1].downcase
args = ARGV[2..]

manager = AssetsManager.new(port)

begin
  manager.connect
  
  case command
  when "ls"
    manager.cmd_ls(args[0] || AssetsManager::USERDATA_ROOT)
  when "stat"
    if args[0]
      manager.cmd_stat(args[0])
    else
      puts "ERROR: stat requires a path argument"
    end
  when "df"
    manager.cmd_df
  when "cat"
    if args[0]
      manager.cmd_cat(args[0])
    else
      puts "ERROR: cat requires a path argument"
    end
  when "mkdir"
    if args[0]
      manager.cmd_mkdir(args[0])
    else
      puts "ERROR: mkdir requires a path argument"
    end
  when "rm"
    if args[0]
      manager.cmd_rm(args[0])
    else
      puts "ERROR: rm requires a path argument"
    end
  when "rmrf"
    if args[0]
      manager.cmd_rmrf(args[0])
    else
      puts "ERROR: rmrf requires a path argument"
    end
  when "mv"
    if args[0] && args[1]
      manager.cmd_mv(args[0], args[1])
    else
      puts "ERROR: mv requires source and destination arguments"
    end
  when "cp"
    if args[0] && args[1]
      manager.cmd_cp(args[0], args[1])
    else
      puts "ERROR: cp requires source and destination arguments"
    end
  when "upload"
    if args[0] && args[1]
      manager.cmd_upload(args[0], args[1])
    else
      puts "ERROR: upload requires local and remote path arguments"
    end
  when "download"
    if args[0] && args[1]
      manager.cmd_download(args[0], args[1])
    else
      puts "ERROR: download requires remote and local path arguments"
    end
  when "manifest"
    manager.cmd_manifest(args[0] || "scenes")
  when "zip"
    if args[0]
      manager.cmd_zip(args[0], args[1])
    else
      puts "ERROR: zip requires a path argument"
    end
  when "extract"
    if args[0] && args[1]
      manager.cmd_extract(args[0], args[1])
    else
      puts "ERROR: extract requires local ZIP and remote path arguments"
    end
  when "interactive", "i"
    manager.cmd_interactive
  else
    puts "Unknown command: #{command}"
    print_usage
    exit 1
  end

rescue Interrupt
  puts "\nInterrupted."
rescue => e
  puts "Error: #{e.message}"
  puts e.backtrace.first(3).join("\n") if $DEBUG
  exit 1
ensure
  manager.disconnect
end

