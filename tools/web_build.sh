#!/usr/bin/env bash
set -euo pipefail

rm -rf web/schemas
cp -R schemas web/schemas
cp midi-devices/DEVICE_AUTHORING.md web/DEVICE_AUTHORING.md