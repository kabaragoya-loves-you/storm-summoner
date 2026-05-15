# promote_release.cmake
# Called at build time to promote firmware and assets to web/binaries
# and regenerate the releases manifest (only if new binaries are added).
#
# Phase 7 (partition split):
#   - Adds partition-table promotion as `partition_table_v2-<hash>.bin`.
#     v(N+2) firmware needs a different on-flash partition table than v(N)
#     and v(N+1); the web app's System Update flow uploads this via the
#     PARTITION_TABLE CDC command (see components/firmware_update).
#   - Promotes the LittleFS image as `shared_assets-<hash>.bin` to make its
#     read-only role explicit. Older `assets-<hash>.bin` files are still
#     listed for backward compat with v(N) units doing a plain ASSETS OTA.
#   - Adds a `system_update` array to releases.json that bundles a specific
#     {firmware, partition_table, shared_assets} triple by hash so the web
#     app can validate compatibility before driving the multi-step push.

# Required variables passed from CMakeLists.txt:
# - FW_VERSION_MAJOR
# - FW_VERSION_MINOR
# - SOURCE_DIR
# - BINARY_DIR

set(WEB_BINARIES_DIR "${SOURCE_DIR}/web/binaries")
set(RELEASES_JSON "${SOURCE_DIR}/web/releases.json")
set(MANIFEST_JSON "${SOURCE_DIR}/midi-devices/manifest.json")
set(PARTITIONS_CSV "${SOURCE_DIR}/partitions.csv")

# Track if anything was promoted
set(PROMOTED_SOMETHING FALSE)

# Ensure binaries directory exists
file(MAKE_DIRECTORY "${WEB_BINARIES_DIR}")

# --- Firmware promotion ---
set(FW_FILENAME "storm-summoner-${FW_VERSION_MAJOR}.${FW_VERSION_MINOR}.bin")
set(FW_SOURCE "${BINARY_DIR}/storm-summoner.bin")
set(FW_DEST "${WEB_BINARIES_DIR}/${FW_FILENAME}")

if(EXISTS "${FW_SOURCE}" AND NOT EXISTS "${FW_DEST}")
  message(STATUS "Promoting firmware: ${FW_FILENAME}")
  file(COPY "${FW_SOURCE}" DESTINATION "${WEB_BINARIES_DIR}")
  file(RENAME "${WEB_BINARIES_DIR}/storm-summoner.bin" "${FW_DEST}")
  set(PROMOTED_SOMETHING TRUE)
else()
  if(NOT EXISTS "${FW_SOURCE}")
    message(STATUS "Firmware source not found: ${FW_SOURCE}")
  else()
    message(STATUS "Firmware already exists: ${FW_FILENAME}")
  endif()
endif()

# --- Shared assets promotion ---
# After the partition split this image holds only shared content (RO):
# midi-devices/ + images/. Scenes and user-created devices are excluded
# (see main/CMakeLists.txt).
set(ASSETS_SOURCE "${BINARY_DIR}/assets.bin")
set(SHARED_ASSETS_FILENAME "")
set(SHARED_ASSETS_HASH "")

if(EXISTS "${MANIFEST_JSON}" AND EXISTS "${ASSETS_SOURCE}")
  file(SHA256 "${MANIFEST_JSON}" MANIFEST_HASH)
  string(SUBSTRING "${MANIFEST_HASH}" 0 8 MANIFEST_HASH_SHORT)

  set(SHARED_ASSETS_FILENAME "shared_assets-${MANIFEST_HASH_SHORT}.bin")
  set(SHARED_ASSETS_HASH "${MANIFEST_HASH_SHORT}")
  set(SHARED_ASSETS_DEST "${WEB_BINARIES_DIR}/${SHARED_ASSETS_FILENAME}")

  if(NOT EXISTS "${SHARED_ASSETS_DEST}")
    message(STATUS "Promoting shared assets: ${SHARED_ASSETS_FILENAME}")
    file(COPY "${ASSETS_SOURCE}" DESTINATION "${WEB_BINARIES_DIR}")
    file(RENAME "${WEB_BINARIES_DIR}/assets.bin" "${SHARED_ASSETS_DEST}")
    set(PROMOTED_SOMETHING TRUE)
  else()
    message(STATUS "Shared assets already exists: ${SHARED_ASSETS_FILENAME}")
  endif()
else()
  if(NOT EXISTS "${MANIFEST_JSON}")
    message(STATUS "Manifest not found: ${MANIFEST_JSON}")
  endif()
  if(NOT EXISTS "${ASSETS_SOURCE}")
    message(STATUS "Assets source not found: ${ASSETS_SOURCE}")
  endif()
endif()

# --- Partition table promotion ---
# Hash the source partitions.csv (small text input -> stable, human-traceable
# identifier; the generated .bin can vary slightly across IDF versions).
set(PT_SOURCE "${BINARY_DIR}/partition_table/partition-table.bin")
set(PT_FILENAME "")
set(PT_HASH "")

if(EXISTS "${PARTITIONS_CSV}" AND EXISTS "${PT_SOURCE}")
  file(SHA256 "${PARTITIONS_CSV}" PT_FULL_HASH)
  string(SUBSTRING "${PT_FULL_HASH}" 0 8 PT_HASH_SHORT)

  set(PT_FILENAME "partition_table_v2-${PT_HASH_SHORT}.bin")
  set(PT_HASH "${PT_HASH_SHORT}")
  set(PT_DEST "${WEB_BINARIES_DIR}/${PT_FILENAME}")

  if(NOT EXISTS "${PT_DEST}")
    message(STATUS "Promoting partition table: ${PT_FILENAME}")
    file(COPY "${PT_SOURCE}" DESTINATION "${WEB_BINARIES_DIR}")
    file(RENAME "${WEB_BINARIES_DIR}/partition-table.bin" "${PT_DEST}")
    set(PROMOTED_SOMETHING TRUE)
  else()
    message(STATUS "Partition table already exists: ${PT_FILENAME}")
  endif()
else()
  if(NOT EXISTS "${PARTITIONS_CSV}")
    message(STATUS "partitions.csv not found: ${PARTITIONS_CSV}")
  endif()
  if(NOT EXISTS "${PT_SOURCE}")
    message(STATUS "Partition-table binary not found: ${PT_SOURCE}")
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

# Get current timestamp
string(TIMESTAMP CURRENT_TIMESTAMP "%Y-%m-%dT%H:%M:%SZ" UTC)
string(TIMESTAMP CURRENT_DATE "%Y-%m-%d" UTC)

# Collect firmware binaries
file(GLOB FW_BINARIES "${WEB_BINARIES_DIR}/storm-summoner-*.bin")
set(FW_ENTRIES "")

foreach(FW_BIN ${FW_BINARIES})
  get_filename_component(FW_NAME "${FW_BIN}" NAME)
  # Extract version from filename: storm-summoner-X.Y.bin -> X.Y
  string(REGEX MATCH "storm-summoner-([0-9]+\\.[0-9]+)\\.bin" _ "${FW_NAME}")
  set(FW_VER "${CMAKE_MATCH_1}")
  
  if(NOT "${FW_VER}" STREQUAL "")
    # Get file modification time for date
    file(TIMESTAMP "${FW_BIN}" FW_DATE "%Y-%m-%d" UTC)
    
    # Build JSON entry
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
    # Create sortable key: pad with zeros for proper sorting
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
  file(TIMESTAMP "${FW_PATH}" FW_DATE "%Y-%m-%d" UTC)
  
  if(FW_JSON_ARRAY)
    set(FW_JSON_ARRAY "${FW_JSON_ARRAY},\n    { \"version\": \"${FW_VER}\", \"filename\": \"${FW_NAME}\", \"date\": \"${FW_DATE}\" }")
  else()
    set(FW_JSON_ARRAY "    { \"version\": \"${FW_VER}\", \"filename\": \"${FW_NAME}\", \"date\": \"${FW_DATE}\" }")
  endif()
endforeach()

# Collect assets binaries (both legacy `assets-*.bin` and new
# `shared_assets-*.bin`; old units doing a plain ASSETS OTA can still pull
# from `assets`, while v(N+2) System Update only consumes `shared_assets`).
file(GLOB ASSETS_BINARIES "${WEB_BINARIES_DIR}/assets-*.bin")
set(ASSETS_ITEMS "")

foreach(ASSET_BIN ${ASSETS_BINARIES})
  get_filename_component(ASSET_NAME "${ASSET_BIN}" NAME)
  string(REGEX MATCH "assets-([a-f0-9]+)\\.bin" _ "${ASSET_NAME}")
  set(ASSET_CHECKSUM "${CMAKE_MATCH_1}")

  if(ASSET_CHECKSUM)
    file(TIMESTAMP "${ASSET_BIN}" ASSET_DATE "%Y-%m-%d" UTC)
    file(TIMESTAMP "${ASSET_BIN}" ASSET_SORT "%Y%m%d%H%M%S" UTC)
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

# Collect shared_assets binaries (post-split RO partition images).
file(GLOB SHARED_ASSETS_BINARIES "${WEB_BINARIES_DIR}/shared_assets-*.bin")
set(SHARED_ASSETS_ITEMS "")

foreach(SA_BIN ${SHARED_ASSETS_BINARIES})
  get_filename_component(SA_NAME "${SA_BIN}" NAME)
  string(REGEX MATCH "shared_assets-([a-f0-9]+)\\.bin" _ "${SA_NAME}")
  set(SA_CHECKSUM "${CMAKE_MATCH_1}")

  if(SA_CHECKSUM)
    file(TIMESTAMP "${SA_BIN}" SA_DATE "%Y-%m-%d" UTC)
    file(TIMESTAMP "${SA_BIN}" SA_SORT "%Y%m%d%H%M%S" UTC)
    list(APPEND SHARED_ASSETS_ITEMS "${SA_SORT}|${SA_CHECKSUM}|${SA_NAME}|${SA_DATE}")
  endif()
endforeach()

list(SORT SHARED_ASSETS_ITEMS)
list(REVERSE SHARED_ASSETS_ITEMS)

set(SHARED_ASSETS_JSON_ARRAY "")
foreach(SA_ITEM ${SHARED_ASSETS_ITEMS})
  string(REPLACE "|" ";" SA_PARTS "${SA_ITEM}")
  list(GET SA_PARTS 1 SA_CHECKSUM)
  list(GET SA_PARTS 2 SA_NAME)
  list(GET SA_PARTS 3 SA_DATE)

  if(SHARED_ASSETS_JSON_ARRAY)
    set(SHARED_ASSETS_JSON_ARRAY "${SHARED_ASSETS_JSON_ARRAY},\n    { \"checksum\": \"${SA_CHECKSUM}\", \"filename\": \"${SA_NAME}\", \"date\": \"${SA_DATE}\" }")
  else()
    set(SHARED_ASSETS_JSON_ARRAY "    { \"checksum\": \"${SA_CHECKSUM}\", \"filename\": \"${SA_NAME}\", \"date\": \"${SA_DATE}\" }")
  endif()
endforeach()

# Collect partition table binaries (System Update bundles consume these).
file(GLOB PT_BINARIES "${WEB_BINARIES_DIR}/partition_table_v2-*.bin")
set(PT_ITEMS "")

foreach(PT_BIN ${PT_BINARIES})
  get_filename_component(PT_NAME "${PT_BIN}" NAME)
  string(REGEX MATCH "partition_table_v2-([a-f0-9]+)\\.bin" _ "${PT_NAME}")
  set(PT_CHECKSUM "${CMAKE_MATCH_1}")

  if(PT_CHECKSUM)
    file(TIMESTAMP "${PT_BIN}" PT_DATE "%Y-%m-%d" UTC)
    file(TIMESTAMP "${PT_BIN}" PT_SORT "%Y%m%d%H%M%S" UTC)
    list(APPEND PT_ITEMS "${PT_SORT}|${PT_CHECKSUM}|${PT_NAME}|${PT_DATE}")
  endif()
endforeach()

list(SORT PT_ITEMS)
list(REVERSE PT_ITEMS)

set(PT_JSON_ARRAY "")
foreach(PT_ITEM ${PT_ITEMS})
  string(REPLACE "|" ";" PT_PARTS "${PT_ITEM}")
  list(GET PT_PARTS 1 PT_CHECKSUM)
  list(GET PT_PARTS 2 PT_NAME)
  list(GET PT_PARTS 3 PT_DATE)

  if(PT_JSON_ARRAY)
    set(PT_JSON_ARRAY "${PT_JSON_ARRAY},\n    { \"checksum\": \"${PT_CHECKSUM}\", \"filename\": \"${PT_NAME}\", \"date\": \"${PT_DATE}\" }")
  else()
    set(PT_JSON_ARRAY "    { \"checksum\": \"${PT_CHECKSUM}\", \"filename\": \"${PT_NAME}\", \"date\": \"${PT_DATE}\" }")
  endif()
endforeach()

# Synthesize the System Update bundle entry for the *current* build.
# A `system_update` entry pins one specific {firmware, partition_table,
# shared_assets} triple by hash so the web app's System Update orchestrator
# can refuse a mismatched combination before driving the multi-step push.
# Only emitted when all three pieces are present from this build.
set(SYSTEM_UPDATE_JSON_ARRAY "")
if(NOT "${PT_HASH}" STREQUAL ""
   AND NOT "${SHARED_ASSETS_HASH}" STREQUAL ""
   AND EXISTS "${FW_DEST}")
  set(SYSTEM_UPDATE_JSON_ARRAY "    {\n      \"firmware_version\": \"${FW_VERSION_MAJOR}.${FW_VERSION_MINOR}\",\n      \"firmware\": \"${FW_FILENAME}\",\n      \"partition_table\": \"partition_table_v2-${PT_HASH}.bin\",\n      \"shared_assets\": \"shared_assets-${SHARED_ASSETS_HASH}.bin\",\n      \"date\": \"${CURRENT_DATE}\"\n    }")
endif()

# Write releases.json
set(RELEASES_CONTENT "{
  \"generated\": \"${CURRENT_TIMESTAMP}\",
  \"firmware\": [
${FW_JSON_ARRAY}
  ],
  \"assets\": [
${ASSETS_JSON_ARRAY}
  ],
  \"shared_assets\": [
${SHARED_ASSETS_JSON_ARRAY}
  ],
  \"partition_tables\": [
${PT_JSON_ARRAY}
  ],
  \"system_update\": [
${SYSTEM_UPDATE_JSON_ARRAY}
  ]
}
")

file(WRITE "${RELEASES_JSON}" "${RELEASES_CONTENT}")
message(STATUS "Updated releases.json")
