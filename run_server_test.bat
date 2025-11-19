@echo off
setlocal enableextensions
cd /d "%~dp0"
set "SERVER_EXE=build\server\Debug\orbital_server.exe"
set "SERVER_BIN=build\server\orbital_server"
if exist "%SERVER_EXE%" (
    echo Running %SERVER_EXE%
    "%SERVER_EXE%"
) else if exist "%SERVER_BIN%" (
    echo Running %SERVER_BIN%
    "%SERVER_BIN%"
) else (
    echo Server binary not found. Please build first.
)
pause
