@echo off
setlocal enableextensions
cd /d "%~dp0"
set BUILD_TYPE=%1
if "%BUILD_TYPE%"=="" (
    set /p BUILD_TYPE=Build type [Debug/Release] (default Debug): 
)
if "%BUILD_TYPE%"=="" set BUILD_TYPE=Debug
if /I not "%BUILD_TYPE%"=="Debug" if /I not "%BUILD_TYPE%"=="Release" (
    echo Invalid build type: %BUILD_TYPE%
    pause
    exit /b 1
)

echo Configuring build (%BUILD_TYPE%)...
cmake -S . -B build -DCMAKE_BUILD_TYPE=%BUILD_TYPE%

echo Building...
cmake --build build --config %BUILD_TYPE%

pause
