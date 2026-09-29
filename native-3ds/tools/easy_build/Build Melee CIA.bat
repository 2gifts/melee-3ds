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
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0builder\tools\easy_build\start.ps1" "%~1"
echo.
pause
