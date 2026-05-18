@echo off
setlocal EnableDelayedExpansion
REM ============================================================
REM  Build script for FEA Pre-Processor (MSVC + CMake + Ninja)
REM  Usage: build.bat [configure|build|clean|run]
REM
REM  Prerequisites:
REM    - Visual Studio 2019 or 2022 (with C++ Desktop workload)
REM    - CMake 3.20+ (bundled with VS, or standalone)
REM    - CUDA Toolkit (optional - CPU fallback works without it)
REM
REM  NOTE: EnableDelayedExpansion is required because path variables
REM  expand to strings containing "(x86)" -- without delayed
REM  expansion, cmd.exe's early-expansion inside if/for parenthesis
REM  blocks miscounts parens and emits
REM      "\Microsoft was unexpected at this time."
REM ============================================================

REM --- Auto-detect Visual Studio via vswhere -----------------
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "!VSWHERE!" (
    echo [!] vswhere.exe not found. Is Visual Studio installed?
    echo     Expected location: !VSWHERE!
    exit /b 1
)

for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -property installationPath`) do set "VS_PATH=%%i"
if "%VS_PATH%"=="" (
    echo [!] No Visual Studio installation found by vswhere.
    exit /b 1
)

set "CMAKE=%VS_PATH%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
if not exist "%CMAKE%" (
    echo [!] CMake not found inside VS at: %CMAKE%
    echo     Install the 'C++ CMake tools for Windows' component in the VS Installer.
    exit /b 1
)

REM --- Load MSVC environment ---------------------------------
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
