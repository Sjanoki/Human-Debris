@echo off
setlocal enableextensions
cd /d "%~dp0"

rem Optional first argument: build type (Debug or Release)
set "BUILD_TYPE=%~1"

if "%BUILD_TYPE%"=="" (
    set /p BUILD_TYPE=Build type [Debug/Release] (default Debug): 
)

if "%BUILD_TYPE%"=="" set "BUILD_TYPE=Debug"

echo Configuring build (%BUILD_TYPE%)...
cmake -S . -B build

echo Building...
cmake --build build --config %BUILD_TYPE%

echo.
echo Build finished. Press any key to continue . . .
pause
