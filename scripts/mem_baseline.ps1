# Memory audit baseline capture (Phase 1 + Phase 4 verification)
# Run from repo root after a successful idf.py build.
# Requires ESP-IDF environment (idf.py on PATH).

$ErrorActionPreference = "Stop"
$OutDir = "build\mem_audit"
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

Write-Host "=== Static size (internal RAM footprint) ==="
idf.py size | Tee-Object -FilePath "$OutDir\size.txt"
idf.py size-components | Tee-Object -FilePath "$OutDir\size-components.txt"
idf.py size-files | Tee-Object -FilePath "$OutDir\size-files.txt"

if (Test-Path "build\storm-summoner.map") {
  Copy-Item "build\storm-summoner.map" "$OutDir\storm-summoner.map"
}

Write-Host ""
Write-Host "=== Runtime snapshots (serial monitor / CDC) ==="
Write-Host "1. Boot: look for 'Heap after display_init' and 'Heap at end of app_main' in serial log."
Write-Host "2. Idle: esp_console 'mem' or CDC command MEM (JSON per-region heap)."
Write-Host "3. Load: enter ASSETS mode (93965-byte manifest), send MEM again during transfer."
Write-Host "4. Trace: console 'mem trace' or CDC 'MEM TRACE' (requires CONFIG_HEAP_TRACING_STANDALONE)."
Write-Host ""
Write-Host "=== Phase 4 pass criteria ==="
Write-Host "- No ST7789V3: SPI transmit failed lines during boot or manifest send."
Write-Host "- DMA min_ever comfortably above 64 bytes (target: largest >= 512)."
Write-Host ""
Write-Host "Output written to $OutDir"
