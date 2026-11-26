@echo off
REM Launch Ripple RL with its built-in native backend.
setlocal
cd /d "%~dp0"
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0run-ui.ps1"
if errorlevel 1 pause
