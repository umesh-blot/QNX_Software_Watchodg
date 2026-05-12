@echo off
REM QNX Build Script for Windows
REM Sets up QNX environment and builds the project

setlocal enabledelayedexpansion

REM Source the official QNX SDK environment setup
call D:\QNX_800\qnxsdp-env.bat

REM Verify qcc compiler is available
where qcc >nul 2>&1
if %errorlevel% neq 0 (
    echo Error: QCC compiler not found. Check QNX installation.
    exit /b 1
)

echo QNX Build Environment:
echo QNX_HOST=%QNX_HOST%
echo QNX_TARGET=%QNX_TARGET%
echo.

REM Set include paths for rpi-gpio project
set CFLAGS=-I D:\Prj\Weather\common\system\gpio
set CXXFLAGS=-I D:\Prj\Weather\common\system\gpio

REM Run make with QNX environment
REM -e ensures environment variables (CFLAGS/CXXFLAGS) are used by make
make -e %*

if %errorlevel% equ 0 (
    echo Build successful. Deploying to target...
    scp -o MACs=hmac-sha2-512 build\aarch64le-debug\SW_Producer build\aarch64le-debug\SW_WDG root@169.254.149.71:/data/home/root
) else (
    echo Build failed. Skipping deployment.
)

endlocal
