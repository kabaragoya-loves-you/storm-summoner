# promote_release.cmake
# Called at build time to promote firmware and assets to web/binaries
# and regenerate the releases manifest (only if new binaries are added).

# Required variables passed from CMakeLists.txt:
# - FW_VERSION_MAJOR
# - FW_VERSION_MINOR
# - SOURCE_DIR
# - BINARY_DIR

set(WEB_BINARIES_DIR "${SOURCE_DIR}/web/binaries")
set(RELEASES_JSON "${SOURCE_DIR}/web/releases.json")
set(MANIFEST_JSON "${SOURCE_DIR}/midi-devices/manifest.json")

set(PROMOTED_SOMETHING FALSE)

file(MAKE_DIRECTORY "${WEB_BINARIES_DIR}")

# --- Firmware promotion ---
# Re-promote whenever the freshly-built bin is newer than what's already in
# web/binaries/ under the same versioned name. Without the IS_NEWER_THAN
# check, bumping `FW_VERSION_MINOR` back to a value that was already promoted
# (or rebuilding after fixes without bumping the version) silently keeps the
# stale binary on the website, which is how the wrong firmware ends up
# served to testers.
set(FW_FILENAME "storm-summoner-${FW_VERSION_MAJOR}.${FW_VERSION_MINOR}.bin")
set(FW_SOURCE "${BINARY_DIR}/storm-summoner.bin")
set(FW_DEST "${WEB_BINARIES_DIR}/${FW_FILENAME}")

if(EXISTS "${FW_SOURCE}"
    AND (NOT EXISTS "${FW_DEST}" OR "${FW_SOURCE}" IS_NEWER_THAN "${FW_DEST}"))
  if(EXISTS "${FW_DEST}")
    message(STATUS "Re-promoting firmware (source is newer): ${FW_FILENAME}")
  else()
    message(STATUS "Promoting firmware: ${FW_FILENAME}")
  endif()
  file(COPY "${FW_SOURCE}" DESTINATION "${WEB_BINARIES_DIR}")
  file(RENAME "${WEB_BINARIES_DIR}/storm-summoner.bin" "${FW_DEST}")
  # Stamp the destination's mtime to *now*. file(COPY) preserves the source
  # mtime, so without this the date in releases.json would reflect when the
  # linker last wrote build/storm-summoner.bin rather than when we released it.
  file(TOUCH "${FW_DEST}")
  set(PROMOTED_SOMETHING TRUE)
else()
  if(NOT EXISTS "${FW_SOURCE}")
    message(STATUS "Firmware source not found: ${FW_SOURCE}")
  else()
    message(STATUS "Firmware already up to date: ${FW_FILENAME}")
  endif()
endif()

# --- Assets promotion ---
# The LittleFS image baked into the read-only `assets` partition. Hash is over
# midi-devices/manifest.json (the manifest of canonical content); the same
# filename can correspond to different on-disk bytes if only the images/
# subtree changed, so IS_NEWER_THAN forces a re-promote in that case too.
set(ASSETS_SOURCE "${BINARY_DIR}/assets.bin")
set(ASSETS_FILENAME "")

if(EXISTS "${MANIFEST_JSON}" AND EXISTS "${ASSETS_SOURCE}")
  file(SHA256 "${MANIFEST_JSON}" MANIFEST_HASH)
  string(SUBSTRING "${MANIFEST_HASH}" 0 8 MANIFEST_HASH_SHORT)

  set(ASSETS_FILENAME "assets-${MANIFEST_HASH_SHORT}.bin")
  set(ASSETS_DEST "${WEB_BINARIES_DIR}/${ASSETS_FILENAME}")

  if(NOT EXISTS "${ASSETS_DEST}"
      OR "${ASSETS_SOURCE}" IS_NEWER_THAN "${ASSETS_DEST}")
    if(EXISTS "${ASSETS_DEST}")
      message(STATUS "Re-promoting assets (source is newer): ${ASSETS_FILENAME}")
    else()
      message(STATUS "Promoting assets: ${ASSETS_FILENAME}")
    endif()
    file(COPY "${ASSETS_SOURCE}" DESTINATION "${WEB_BINARIES_DIR}")
    file(RENAME "${WEB_BINARIES_DIR}/assets.bin" "${ASSETS_DEST}")
    file(TOUCH "${ASSETS_DEST}")
    set(PROMOTED_SOMETHING TRUE)
  else()
    message(STATUS "Assets already up to date: ${ASSETS_FILENAME}")
  endif()
else()
  if(NOT EXISTS "${MANIFEST_JSON}")
    message(STATUS "Manifest not found: ${MANIFEST_JSON}")
  endif()
  if(NOT EXISTS "${ASSETS_SOURCE}")
    message(STATUS "Assets source not found: ${ASSETS_SOURCE}")
  endif()
endif()

# --- Generate releases.json (only if something was promoted or manifest doesn't exist) ---
if(NOT PROMOTED_SOMETHING AND EXISTS "${RELEASES_JSON}")
  message(STATUS "No new releases, skipping manifest update")
  return()
endif()

if(NOT EXISTS "${RELEASES_JSON}")
  message(STATUS "Creating releases.json (first time)")
endif()

# --- Generate releases.json ---
# Scan existing binaries and build manifest

string(TIMESTAMP CURRENT_TIMESTAMP "%Y-%m-%dT%H:%M:%S")
string(TIMESTAMP CURRENT_DATE "%Y-%m-%d")

# Collect firmware binaries
file(GLOB FW_BINARIES "${WEB_BINARIES_DIR}/storm-summoner-*.bin")
set(FW_ENTRIES "")

foreach(FW_BIN ${FW_BINARIES})
  get_filename_component(FW_NAME "${FW_BIN}" NAME)
  string(REGEX MATCH "storm-summoner-([0-9]+\\.[0-9]+)\\.bin" _ "${FW_NAME}")
  set(FW_VER "${CMAKE_MATCH_1}")

  if(NOT "${FW_VER}" STREQUAL "")
    file(TIMESTAMP "${FW_BIN}" FW_DATE "%Y-%m-%d")

    set(FW_ENTRY "    { \"version\": \"${FW_VER}\", \"filename\": \"${FW_NAME}\", \"date\": \"${FW_DATE}\" }")
    list(APPEND FW_ENTRIES "${FW_ENTRY}")
  endif()
endforeach()

# Sort firmware entries by version (descending) - newest first
# We'll use a simple approach: collect version numbers, sort, then rebuild
set(FW_VERSIONS "")
foreach(FW_BIN ${FW_BINARIES})
  get_filename_component(FW_NAME "${FW_BIN}" NAME)
  string(REGEX MATCH "storm-summoner-([0-9]+)\\.([0-9]+)\\.bin" _ "${FW_NAME}")
  set(FW_MAJOR "${CMAKE_MATCH_1}")
  set(FW_MINOR "${CMAKE_MATCH_2}")
  if(DEFINED FW_MAJOR AND NOT "${FW_MAJOR}" STREQUAL "" AND NOT "${FW_MINOR}" STREQUAL "")
    math(EXPR SORT_KEY "${FW_MAJOR} * 1000 + ${FW_MINOR}")
    list(APPEND FW_VERSIONS "${SORT_KEY}|${FW_MAJOR}.${FW_MINOR}|${FW_NAME}|${FW_BIN}")
  endif()
endforeach()

list(SORT FW_VERSIONS)
list(REVERSE FW_VERSIONS)

set(FW_JSON_ARRAY "")
foreach(FW_ITEM ${FW_VERSIONS})
  string(REPLACE "|" ";" FW_PARTS "${FW_ITEM}")
  list(GET FW_PARTS 1 FW_VER)
  list(GET FW_PARTS 2 FW_NAME)
  list(GET FW_PARTS 3 FW_PATH)
  file(TIMESTAMP "${FW_PATH}" FW_DATE "%Y-%m-%d")

  if(FW_JSON_ARRAY)
    set(FW_JSON_ARRAY "${FW_JSON_ARRAY},\n    { \"version\": \"${FW_VER}\", \"filename\": \"${FW_NAME}\", \"date\": \"${FW_DATE}\" }")
  else()
    set(FW_JSON_ARRAY "    { \"version\": \"${FW_VER}\", \"filename\": \"${FW_NAME}\", \"date\": \"${FW_DATE}\" }")
  endif()
endforeach()

# Collect assets binaries. These are the LittleFS images consumed by the
# Updater tab's ASSETS-mode OTA path.
file(GLOB ASSETS_BINARIES "${WEB_BINARIES_DIR}/assets-*.bin")
set(ASSETS_ITEMS "")

foreach(ASSET_BIN ${ASSETS_BINARIES})
  get_filename_component(ASSET_NAME "${ASSET_BIN}" NAME)
  string(REGEX MATCH "assets-([a-f0-9]+)\\.bin" _ "${ASSET_NAME}")
  set(ASSET_CHECKSUM "${CMAKE_MATCH_1}")

  if(ASSET_CHECKSUM)
    file(TIMESTAMP "${ASSET_BIN}" ASSET_DATE "%Y-%m-%d")
    file(TIMESTAMP "${ASSET_BIN}" ASSET_SORT "%Y%m%d%H%M%S")
    list(APPEND ASSETS_ITEMS "${ASSET_SORT}|${ASSET_CHECKSUM}|${ASSET_NAME}|${ASSET_DATE}")
  endif()
endforeach()

list(SORT ASSETS_ITEMS)
list(REVERSE ASSETS_ITEMS)

set(ASSETS_JSON_ARRAY "")
foreach(ASSET_ITEM ${ASSETS_ITEMS})
  string(REPLACE "|" ";" ASSET_PARTS "${ASSET_ITEM}")
  list(GET ASSET_PARTS 1 ASSET_CHECKSUM)
  list(GET ASSET_PARTS 2 ASSET_NAME)
  list(GET ASSET_PARTS 3 ASSET_DATE)

  if(ASSETS_JSON_ARRAY)
    set(ASSETS_JSON_ARRAY "${ASSETS_JSON_ARRAY},\n    { \"checksum\": \"${ASSET_CHECKSUM}\", \"filename\": \"${ASSET_NAME}\", \"date\": \"${ASSET_DATE}\" }")
  else()
    set(ASSETS_JSON_ARRAY "    { \"checksum\": \"${ASSET_CHECKSUM}\", \"filename\": \"${ASSET_NAME}\", \"date\": \"${ASSET_DATE}\" }")
  endif()
endforeach()

set(RELEASES_CONTENT "{
  \"generated\": \"${CURRENT_TIMESTAMP}\",
  \"firmware\": [
${FW_JSON_ARRAY}
  ],
  \"assets\": [
${ASSETS_JSON_ARRAY}
  ]
}
")

file(WRITE "${RELEASES_JSON}" "${RELEASES_CONTENT}")
message(STATUS "Updated releases.json")
