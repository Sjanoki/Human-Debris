@echo off
cd /d "%~dp0"

echo Configuring Debug build...
cmake -S . -B build

echo Building Debug...
cmake --build build --config Debug

echo.
echo Debug build finished. Press any key to continue . . .
pause
