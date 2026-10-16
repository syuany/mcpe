@echo off
setlocal enabledelayedexpansion
title MCPE KCP Accelerator Client

set "REMOTE_HOST=20.239.194.249"
set "REMOTE_PORT=29900"
set "LOCAL_LISTEN=127.0.0.1:25566"
set "BIN_NAME=gost.exe"

echo ====================================================================
echo        MCPE KCP High-Speed Cross-Border Acceleration Client
echo ====================================================================
echo.

if not "%1"=="" set "REMOTE_HOST=%1"

echo [*] Target Remote Host  : %REMOTE_HOST%:%REMOTE_PORT%
echo [*] Local Listen Port   : %LOCAL_LISTEN%
echo.

if not exist "%~dp0%BIN_NAME%" (
    echo [!] gost.exe not found in scripts directory!
    echo [*] Downloading official gost Windows amd64 binary...
    powershell -NoProfile -ExecutionPolicy Bypass -Command ^
        "$ProgressPreference = 'SilentlyContinue';" ^
        "$url = 'https://github.com/ginuerzh/gost/releases/download/v2.11.5/gost-windows-amd64-2.11.5.zip';" ^
        "$zipPath = Join-Path $env:TEMP 'gost.zip';" ^
        "Write-Host '[*] Downloading from' $url '...';" ^
        "Invoke-WebRequest -Uri $url -OutFile $zipPath;" ^
        "Expand-Archive -Path $zipPath -DestinationPath '%~dp0' -Force;" ^
        "Remove-Item $zipPath -Force;"
)

if not exist "%~dp0%BIN_NAME%" (
    echo [ERROR] Failed to download or extract %BIN_NAME%.
    echo Please manually download gost Windows AMD64 from:
    echo   https://github.com/ginuerzh/gost/releases/tag/v2.11.5
    echo and place gost.exe into the scripts\ directory.
    pause
    exit /b 1
)

echo [+] Starting KCP client acceleration tunnel...
echo [*] Connect your Minecraft 1.12.2 client to: 127.0.0.1:25566
echo.
"%~dp0%BIN_NAME%" -L="tcp://:25566/127.0.0.1:25566" -F="kcp://%REMOTE_HOST%:%REMOTE_PORT%?mode=fast3&nodelay=1"

pause
