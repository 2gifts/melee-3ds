@echo off
rem Melee for New 3DS: build the game and its CIA files from your own disc image.
rem Double-click this file, or drag your Melee .iso onto it.
title Melee for New 3DS - CIA builder
if not exist "%~dp0builder\tools\easy_build\start.ps1" (
    echo The builder's files are missing. Extract the whole zip first:
    echo right-click the zip file, choose "Extract All...", then open the
    echo extracted folder and double-click "Build Melee CIA.bat" there.
    echo.
    pause
    exit /b 1
)
reg query "HKCU\Software\Wine" >nul 2>&1
if not errorlevel 1 (
    echo This builder needs real Windows 10 or 11. It does not work in Wine, Proton or
    echo other Windows emulators on Linux, macOS or Steam Deck.
    echo.
    pause
    exit /b 1
)
set "PS=%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe"
if not exist "%PS%" (
    echo Windows PowerShell was not found on this PC. The builder needs Windows 10 or 11
    echo with PowerShell, which Windows normally includes.
    echo.
    pause
    exit /b 1
)
"%PS%" -NoProfile -ExecutionPolicy Bypass -File "%~dp0builder\tools\easy_build\start.ps1" "%~1"
echo.
echo You can close this window now.
pause
