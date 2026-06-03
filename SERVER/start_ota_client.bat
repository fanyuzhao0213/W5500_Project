@echo off
echo ========================================
echo   W5500 OTA Client
echo ========================================
echo.

cd /d "%~dp0"

if not exist "ota_server.py" (
    echo [ERROR] ota_server.py not found!
    echo [ERROR] Current directory: %CD%
    pause
    exit /b 1
)

echo [INFO] Starting OTA Client...
echo.

start python ota_server.py

echo [INFO] Client started in new window.
timeout /t 2 /nobreak >nul
exit /b 0
