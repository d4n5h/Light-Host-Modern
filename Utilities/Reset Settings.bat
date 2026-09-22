@echo off
setlocal

set "SETTINGS_DIR=%APPDATA%\LightHostModern"
set "SETTINGS_FILE=%SETTINGS_DIR%\LightHostModern.settings"
set "CRASHED_PLUGINS_FILE=%SETTINGS_DIR%\RecentlyCrashedPluginsList"

echo Reset settings for LightHostModern?
choice /C YN /M "Delete saved settings"
if errorlevel 2 (
    echo Settings not altered.
    exit /b 0
)

if exist "%SETTINGS_FILE%" del /F /Q "%SETTINGS_FILE%"
if exist "%CRASHED_PLUGINS_FILE%" del /F /Q "%CRASHED_PLUGINS_FILE%"

echo Settings reset.
