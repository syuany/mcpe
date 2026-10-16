@echo off
setlocal enabledelayedexpansion
title MCPE KCP Accelerator Client

set "REMOTE_HOST=azure-dev"
set "REMOTE_PORT=29900"
set "LOCAL_LISTEN=127.0.0.1:25566"
set "BIN_NAME=client_windows_amd64.exe"

echo ====================================================================
echo        MCPE KCP High-Speed Cross-Border Acceleration Client
echo ====================================================================
echo.

if not "%1"=="" set "REMOTE_HOST=%1"

echo [*] Target Remote Host  : %REMOTE_HOST%:%REMOTE_PORT%
echo [*] Local Listen Port   : %LOCAL_LISTEN%
echo.

if not exist "%~dp0%BIN_NAME%" if not exist "%~dp0kcptun_client.exe" (
    echo [!] KCP client binary not found in current directory!
    echo [*] Attempting to download official kcptun client binary...
    powershell -NoProfile -ExecutionPolicy Bypass -Command ^
        "$url = 'https://github.com/xtaci/kcptun/releases/download/v20230214/kcptun-windows-amd64-20230214.tar.gz';" ^
        "$zipPath = Join-Path $env:TEMP 'kcptun.tar.gz';" ^
        "Write-Host '[*] Downloading from' $url '...';" ^
        "Invoke-WebRequest -Uri $url -OutFile $zipPath;" ^
        "tar -xzf $zipPath -C '%~dp0' client_windows_amd64.exe;" ^
        "Remove-Item $zipPath -Force;"
)

set "EXE_PATH=%~dp0%BIN_NAME%"
if not exist "%EXE_PATH%" set "EXE_PATH=%~dp0kcptun_client.exe"

if not exist "%EXE_PATH%" (
    echo [ERROR] Failed to locate client binary.
    echo Please manually download kcptun from:
    echo   https://github.com/xtaci/kcptun/releases
    echo and place client_windows_amd64.exe in the scripts\ folder.
    pause
    exit /b 1
)

echo [+] Starting kcptun client tunnel...
echo [*] Connect your Minecraft 1.12.2 client to: 127.0.0.1:25566
echo.
"%EXE_PATH%" ^
  -r "%REMOTE_HOST%:%REMOTE_PORT%" ^
  -l "%LOCAL_LISTEN%" ^
  -mode fast3 ^
  -crypt none ^
  -nocomp ^
  -datashard 10 ^
  -parityshard 3 ^
  -mtu 1350 ^
  -sndwnd 1024 ^
  -rcvwnd 1024

pause
