@echo off
rem ===========================================================================
rem  Build script (MinGW-w64)
rem  Usage: build.bat
rem  Requires g++ and windres (MinGW-w64, e.g. winlibs or MSYS2) on PATH.
rem ===========================================================================
setlocal
set OUT=build
if not exist %OUT% mkdir %OUT%

windres -O coff src\app.rc -o %OUT%\app_res.o
if %errorlevel% neq 0 (
    echo windres FAILED.
    exit /b 1
)

g++ -std=c++17 -O2 -municode -mwindows -static -static-libgcc -static-libstdc++ ^
    src\main.cpp %OUT%\app_res.o -o %OUT%\EarnPerSecond.exe -luser32 -lgdi32 -lshell32

if %errorlevel%==0 (
    echo Build OK: %OUT%\EarnPerSecond.exe
) else (
    echo Build FAILED - make sure g++ ^(MinGW-w64^) is on PATH.
    exit /b 1
)
