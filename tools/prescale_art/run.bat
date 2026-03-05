@echo off
title Pseudo Vinyl - Album Art Converter
echo.
echo  ========================================
echo   Pseudo Vinyl - Album Art Converter
echo  ========================================
echo.

:: Check for Python
python --version >nul 2>&1
if errorlevel 1 (
    echo  ERROR: Python is not installed or not in PATH.
    echo  Please install Python 3.8+ from https://www.python.org/downloads/
    echo.
    pause
    exit /b 1
)

:: Install dependencies
echo  Installing dependencies...
pip install -r requirements.txt -q >nul 2>&1
echo  Done.
echo.

:: Launch GUI
echo  Launching converter...
echo.
python prescale_art_gui.py

if errorlevel 1 (
    echo.
    echo  The application encountered an error.
    pause
)
