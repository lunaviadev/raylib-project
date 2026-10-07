@echo off
rem Builds the browser version into web/ with Emscripten, then zips it for itch.io.
setlocal

if not defined EMSDK call C:\raylib\emsdk\emsdk_env.bat >nul 2>nul
if not exist web mkdir web

call emcc main.c -o web/index.html -std=gnu99 -Os -Wall -IC:/raylib/raylib/src C:/raylib/raylib/src/libraylib.web.a -DPLATFORM_WEB -sUSE_GLFW=3 -sFETCH -sASYNCIFY -sTOTAL_MEMORY=67108864 --shell-file C:/raylib/raylib/src/minshell.html
if errorlevel 1 (
    echo Web build failed.
    exit /b 1
)

powershell -NoProfile -Command "Compress-Archive -Path web\index.html,web\index.js,web\index.wasm -DestinationPath dungeon-of-time-web.zip -Force"
echo Built web/ and dungeon-of-time-web.zip
