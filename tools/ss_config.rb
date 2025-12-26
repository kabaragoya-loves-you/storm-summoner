#!/usr/bin/env ruby
# frozen_string_literal: true

# Shared configuration helper for Storm Summoner Ruby tools
# Reads settings from .vscode/settings.json

require 'json'

module SSConfig
  SETTINGS_PATH = File.expand_path('../../.vscode/settings.json', __FILE__)

  def self.load_settings
    return {} unless File.exist?(SETTINGS_PATH)
    JSON.parse(File.read(SETTINGS_PATH))
  rescue JSON::ParserError => e
    warn "Warning: Failed to parse settings.json: #{e.message}"
    {}
  end

  def self.cdc_port
    settings = load_settings
    settings['ss.cdcPort']
  end

  def self.default_port
    cdc_port || (RUBY_PLATFORM =~ /mswin|mingw/ ? 'COM3' : '/dev/ttyACM0')
  end

  # Get port from args or settings, with helpful error message
  def self.get_port(args = ARGV, option_name: nil)
    # If option_name specified, look for --option_name VALUE in args
    if option_name
      idx = args.index("--#{option_name}") || args.index("-p")
      return args[idx + 1] if idx && args[idx + 1]
    end

    # Check positional arg (first non-option argument)
    positional = args.find { |a| !a.start_with?('-') }
    return positional if positional

    # Fall back to settings.json
    port = cdc_port
    if port
      puts "Using port from .vscode/settings.json: #{port}"
      return port
    end

    # Platform default
    default_port
  end
end

