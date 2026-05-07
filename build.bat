@echo off
REM ============================================================
REM  Build script for FEA Pre-Processor (MSVC + CMake + Ninja)
REM  Usage: build.bat [configure|build|clean|run]
REM ============================================================

set "VS_PATH=D:\program_files_delta\Visual Studio"
set "CMAKE=%VS_PATH%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"

REM Load MSVC environment
call "%VS_PATH%\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1

if "%1"=="configure" goto :configure
if "%1"=="clean" goto :clean
if "%1"=="run" goto :run

:build
echo [*] Building...
if not exist build\CMakeCache.txt (
    echo [*] No CMake cache found, auto-configuring first...
    "%CMAKE%" -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_C_COMPILER=cl -DCMAKE_CXX_COMPILER=cl
    if errorlevel 1 exit /b 1
)
"%CMAKE%" --build build --config Debug
exit /b %errorlevel%

:configure
echo [*] Configuring CMake...
"%CMAKE%" -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_C_COMPILER=cl -DCMAKE_CXX_COMPILER=cl
exit /b %errorlevel%

:clean
echo [*] Clean rebuilding...
"%CMAKE%" --build build --config Debug --clean-first
exit /b %errorlevel%

:run
echo [*] Building first...
call :build
if errorlevel 1 (
    echo [!] Build failed, not running.
    exit /b 1
)
echo [*] Running FEA Pre-Processor...
pushd build
FEAPreProcessor.exe
popd
exit /b %errorlevel%
