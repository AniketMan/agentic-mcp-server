@echo off
echo ============================================
echo Audio Analysis Service Setup
echo ============================================

REM Check for Python
python --version >nul 2>&1
if errorlevel 1 (
    echo ERROR: Python not found. Please install Python 3.11+
    exit /b 1
)

echo Installing dependencies...
pip install -r requirements.txt

echo.
echo ============================================
echo Setup complete!
echo.
echo Usage:
echo   Start service:  python transcribe.py --serve
echo   Transcribe file: python transcribe.py audio.wav --output srt
echo.
echo Service will run on http://localhost:9848
echo ============================================
