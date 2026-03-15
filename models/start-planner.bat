@echo off
REM ============================================================================
REM  Start Planner/Worker inference server (Llama 3.1 8B) on port 8081
REM  System prompt injected from instructions/worker.md
REM ============================================================================
setlocal enabledelayedexpansion

set "SCRIPT_DIR=%~dp0"
cd /d "%SCRIPT_DIR%"

set "MODEL=Meta-Llama-3.1-8B-Instruct-Q4_K_M.gguf"
set "PORT=8081"
set "GPU_LAYERS=99"
set "CTX_SIZE=8192"
set "THREADS=4"

if not exist "bin\llama-server.exe" (
    echo [ERROR] llama-server.exe not found. Run setup-models.bat first.
    pause
    exit /b 1
)

if not exist "%MODEL%" (
    echo [ERROR] %MODEL% not found. Run setup-models.bat first.
    pause
    exit /b 1
)

REM Load system prompt from instruction MD
set "SYSTEM_PROMPT_FILE=instructions\worker.md"
if not exist "%SYSTEM_PROMPT_FILE%" (
    echo [WARNING] %SYSTEM_PROMPT_FILE% not found. Starting without system prompt.
    set "SYSTEM_PROMPT_ARG="
) else (
    set "SYSTEM_PROMPT_ARG=--system-prompt-file "%SYSTEM_PROMPT_FILE%""
)

echo ============================================================================
echo  Starting Planner/Worker (Llama 3.1 8B) on port %PORT%
echo  GPU layers: %GPU_LAYERS%  Context: %CTX_SIZE%  Threads: %THREADS%
echo ============================================================================
echo.

bin\llama-server.exe ^
    --model "%MODEL%" ^
    --port %PORT% ^
    --n-gpu-layers %GPU_LAYERS% ^
    --ctx-size %CTX_SIZE% ^
    --threads %THREADS% ^
    %SYSTEM_PROMPT_ARG% ^
    --log-disable

echo.
echo [INFO] Planner/Worker server stopped. Press any key to close.
pause >nul
