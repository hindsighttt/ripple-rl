@echo off
REM Launch Ripple RL. Double-click this file, or run it from a terminal.
setlocal
cd /d "%~dp0"

REM Python 3.12 is the interpreter with PySide6 + mitmproxy + pycryptodome installed.
set "PYEXE=C:\Python312\python.exe"
if not exist "%PYEXE%" (
    REM Fall back to the py launcher if 3.12 lives somewhere else.
    where py >nul 2>&1 && (set "PYEXE=py -3.12") || (
        echo Could not find Python 3.12.
        echo Install it, or edit PYEXE in this script to point at the right python.exe.
        pause
        exit /b 1
    )
)

%PYEXE% main.py
if errorlevel 1 (
    echo.
    echo Ripple RL exited with an error - see the message above.
    pause
)
