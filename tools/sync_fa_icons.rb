#!/usr/bin/env ruby
# frozen_string_literal: true

# Download Font Awesome Free SVGs used by the web app into web/assets/fontawesome/svgs/.
#
# Usage:
#   ruby tools/sync_fa_icons.rb [--force]
#
# Discovers wa-icon names from web/index.html and web/js/**/*.js (excluding
# library="system" icons, which Web Awesome bundles inline). Skips downloads
# when the destination file already exists unless --force is passed.

require 'net/http'
require 'optparse'
require 'pathname'
require 'set'
require 'uri'

FA_TAG = '7.2.0'
FA_RAW = "https://raw.githubusercontent.com/FortAwesome/Font-Awesome/#{FA_TAG}/svgs"

ALIASES = {
  'info-circle' => 'circle-info'
}.freeze

SKIP_PATH_PARTS = %w[webawesome].freeze

# wa-icon names built at runtime (template literals), not visible to static scan.
EXTRA_SOLID = %w[file folder house-chimney-window house-flag].freeze

options = { force: false }
OptionParser.new do |opts|
  opts.on('--force', 'Re-download all icons even if present') { options[:force] = true }
end.parse!

repo_root = Pathname.new(__dir__).join('..').expand_path
web_root = repo_root.join('web')
out_root = web_root.join('assets', 'fontawesome', 'svgs')
version_file = web_root.join('assets', 'fontawesome', 'VERSION')

def scan_web_files(web_root)
  files = [web_root.join('index.html')]
  files.concat(Dir.glob(web_root.join('js', '**', '*.js').to_s))
  files.select do |path|
    path_str = path.to_s
    next false unless File.file?(path_str)
    SKIP_PATH_PARTS.none? { |part| path_str.include?(part) }
  end
end

def discover_icons(web_root)
  solid = Set.new
  regular = Set.new

  wa_tag = /<wa-icon\b[^>]*>/i
  system_lib = /\blibrary\s*=\s*["']system["']/i
  name_attr = /\bname\s*=\s*["']([a-z0-9-]+)["']/i
  variant_regular = /\bvariant\s*=\s*["']regular["']/i

  scan_web_files(web_root).each do |path|
    File.read(path.to_s).scan(wa_tag) do
      tag = Regexp.last_match(0)
      next if tag.match?(system_lib)
      name_match = tag.match(name_attr)
      next unless name_match
      name = name_match[1]
      solid << name
      regular << name if tag.match?(variant_regular)
    end
  end

  solid.merge(EXTRA_SOLID)
  [solid.to_a.sort, regular.to_a.sort]
end

def download_svg(url, dest, force:)
  return [:skipped, nil] if dest.exist? && !force

  uri = URI(url)
  Net::HTTP.start(uri.host, uri.port, use_ssl: uri.scheme == 'https', open_timeout: 30, read_timeout: 30) do |http|
    res = http.get(uri.request_uri)
    return [:http_error, res.code.to_i] unless res.is_a?(Net::HTTPSuccess)

    dest.dirname.mkpath
    dest.binwrite(res.body)
    [:ok, nil]
  end
rescue StandardError => e
  [:error, e.message]
end

solid_icons, regular_icons = discover_icons(web_root)
force = options[:force] || !version_file.exist? || version_file.read.strip != FA_TAG
missing = []

[['solid', solid_icons], ['regular', regular_icons]].each do |folder, names|
  names.each do |name|
    url = "#{FA_RAW}/#{folder}/#{name}.svg"
    dest = out_root.join(folder, "#{name}.svg")
    status, detail = download_svg(url, dest, force: force)

    case status
    when :ok
      puts "OK  #{folder}/#{name}.svg"
    when :skipped
      puts "SKIP #{folder}/#{name}.svg"
    when :http_error
      if folder == 'regular'
        solid_src = out_root.join('solid', "#{name}.svg")
        if solid_src.exist?
          dest.dirname.mkpath
          dest.binwrite(File.binread(solid_src.to_s))
          puts "FALLBACK regular/#{name}.svg <- solid/#{name}.svg"
          next
        end
      end
      missing << "#{folder}/#{name}.svg (HTTP #{detail})"
      warn "MISS #{folder}/#{name}.svg (HTTP #{detail})"
    when :error
      missing << "#{folder}/#{name}.svg (#{detail})"
      warn "MISS #{folder}/#{name}.svg (#{detail})"
    end
  end
end

ALIASES.each do |alias_name, target|
  %w[solid].each do |folder|
    src = out_root.join(folder, "#{target}.svg")
    dest = out_root.join(folder, "#{alias_name}.svg")
    if src.exist?
      dest.dirname.mkpath
      dest.binwrite(File.binread(src.to_s)) if force || !dest.exist? || File.binread(dest.to_s) != File.binread(src.to_s)
      puts "ALIAS #{folder}/#{alias_name}.svg -> #{target}.svg"
    else
      missing << "alias #{folder}/#{alias_name}.svg (source missing)"
      warn "MISS alias #{folder}/#{alias_name}.svg (source missing)"
    end
  end
end

version_file.dirname.mkpath
version_file.write(FA_TAG)

if missing.empty?
  puts "Synced #{solid_icons.length} solid and #{regular_icons.length} regular icon(s)"
  exit 0
end

warn "\nMissing icons:"
missing.each { |item| warn "  - #{item}" }
exit 1
