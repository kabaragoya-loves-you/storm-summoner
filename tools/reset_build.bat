@echo off
REM Reset build number for Storm Summoner firmware
REM NOTE: Build number auto-increments on every build now.
REM       Use this script only to reset to a specific value.

setlocal enabledelayedexpansion

set BUILD_FILE=%~dp0..\build_number.txt

if "%1"=="" (
    echo Usage: reset_build.bat [number]
    echo Example: reset_build.bat 100
    echo.
    echo Current build number:
    if exist "%BUILD_FILE%" (
        type "%BUILD_FILE%"
    ) else (
        echo 0
    )
    exit /b 1
)

echo %1>"%BUILD_FILE%"
echo Build number reset to %1

endlocal

