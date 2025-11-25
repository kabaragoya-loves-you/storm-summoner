#!/bin/bash
# Reset build number for Storm Summoner firmware
# NOTE: Build number auto-increments on every build now.
#       Use this script only to reset to a specific value.

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_FILE="$SCRIPT_DIR/../build_number.txt"

if [ -z "$1" ]; then
  echo "Usage: reset_build.sh [number]"
  echo "Example: reset_build.sh 100"
  echo ""
  echo "Current build number:"
  if [ -f "$BUILD_FILE" ]; then
    cat "$BUILD_FILE"
  else
    echo "0"
  fi
  exit 1
fi

echo "$1" > "$BUILD_FILE"
echo "Build number reset to $1"

