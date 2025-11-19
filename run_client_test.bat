@echo off
setlocal enableextensions
set "ROOT=%~dp0"
cd /d "%ROOT%client_python"
python -c "import pygame" >nul 2>&1
if errorlevel 1 (
    echo pygame not found. Installing...
    python -m pip install pygame
)
python client_gui.py
pause
