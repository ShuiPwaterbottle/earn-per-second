@echo off
rem ===========================================================================
rem  Build script (MinGW-w64)
rem  Usage: build.bat
rem  Requires g++ (MinGW-w64, e.g. winlibs or MSYS2) on PATH.
rem ===========================================================================
setlocal
set OUT=build
if not exist %OUT% mkdir %OUT%

g++ -std=c++17 -O2 -municode -mwindows -static -static-libgcc -static-libstdc++ ^
    src\main.cpp -o %OUT%\EarnPerSecond.exe -luser32 -lgdi32 -lshell32

if %errorlevel%==0 (
    echo Build OK: %OUT%\EarnPerSecond.exe
) else (
    echo Build FAILED - make sure g++ ^(MinGW-w64^) is on PATH.
    exit /b 1
)
