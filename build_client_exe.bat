@echo off
setlocal enableextensions
set "ROOT=%~dp0"
cd /d "%ROOT%client_python"
python -m PyInstaller --version >nul 2>&1
if errorlevel 1 (
    echo PyInstaller not found. Installing...
    python -m pip install pyinstaller
)
python -m PyInstaller --onefile client_gui.py
echo Build complete. Executable is located in %cd%\dist\client_gui.exe
pause
