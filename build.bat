@echo off
rem Builds game.exe with GCC. Run "build.bat run" to build and launch.
setlocal

rem Use the compiler that ships with the raylib installer if gcc is not on PATH
where gcc >nul 2>nul
if errorlevel 1 set "PATH=C:\raylib\w64devkit\bin;%PATH%"

gcc main.c -o game.exe -std=c99 -Wall -Wextra -O2 -Iraylib/include -Lraylib/lib -lraylib -lopengl32 -lgdi32 -lwinmm
if errorlevel 1 (
    echo Build failed.
    exit /b 1
)
echo Built game.exe

if /i "%~1"=="run" game.exe
