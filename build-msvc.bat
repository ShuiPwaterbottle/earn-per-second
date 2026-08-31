@echo off
rem ===========================================================================
rem  Build script (MSVC)
rem  Usage: build-msvc.bat   (run inside a "x64 Native Tools" prompt)
rem  The DPI manifest is embedded automatically via /MANIFEST:EMBED below.
rem ===========================================================================
setlocal
if not exist build mkdir build

cl /nologo /std:c++17 /EHsc /O2 /utf-8 /DUNICODE /D_UNICODE ^
   src\main.cpp /Fe:build\EarnPerSecond.exe ^
   /link /SUBSYSTEM:WINDOWS user32.lib gdi32.lib shell32.lib ^
   /MANIFEST:EMBED /MANIFESTINPUT:src\app.manifest

if %errorlevel%==0 (
    echo Build OK: build\EarnPerSecond.exe
) else (
    echo Build FAILED.
    exit /b 1
)
