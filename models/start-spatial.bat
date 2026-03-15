@echo off
setlocal enabledelayedexpansion

echo ============================================================================
echo  Starting Spatial Reasoner (Cosmos Reason2 2B INT8) on port 8083
echo  Context: 4096  Mode: Screenshots only
echo ============================================================================
echo.

REM Check Python is available
where python >nul 2>&1
if not "!ERRORLEVEL!"=="0" (
    echo [ERROR] Python not found in PATH.
    echo Please install Python 3.10+ from https://www.python.org/downloads/
    echo Make sure to check "Add Python to PATH" during installation.
    pause
    exit /b 1
)

REM Check Python version
for /f "tokens=2 delims= " %%v in ('python --version 2^>^&1') do set PYVER=%%v
echo [INFO] Python version: !PYVER!

REM Check if vllm is installed
python -c "import vllm" >nul 2>&1
if not "!ERRORLEVEL!"=="0" (
    echo [WARN] vLLM not installed. Installing now...
    echo This may take several minutes on first run.
    pip install "vllm>=0.11.0" --quiet
    if not "!ERRORLEVEL!"=="0" (
        echo [ERROR] Failed to install vLLM.
        echo Try manually: pip install "vllm>=0.11.0"
        pause
        exit /b 1
    )
    echo [INFO] vLLM installed successfully.
)

REM Check if transformers is recent enough
python -c "import transformers; assert tuple(int(x) for x in transformers.__version__.split('.')[:2]) >= (4, 57)" >nul 2>&1
if not "!ERRORLEVEL!"=="0" (
    echo [WARN] transformers version too old. Upgrading...
    pip install "transformers>=4.57.0" --quiet
)

echo.
echo [INFO] Starting vLLM server for Cosmos Reason2 2B...
echo [INFO] Model will be downloaded from Hugging Face on first run (~4 GB).
echo [INFO] Subsequent starts use the cached model.
echo [INFO] Server will be available at http://localhost:8083
echo.

python -m vllm.entrypoints.openai.api_server ^
    --model nvidia/Cosmos-Reason2-2B ^
    --port 8083 ^
    --max-model-len 4096 ^
    --dtype float16 ^
    --quantization int8 ^
    --gpu-memory-utilization 0.35 ^
    --reasoning-parser qwen3 ^
    --trust-remote-code

if not "!ERRORLEVEL!"=="0" (
    echo.
    echo [ERROR] vLLM server exited with error code !ERRORLEVEL!
    echo.
    echo Common fixes:
    echo   - Ensure CUDA is installed and your GPU is detected
    echo   - Try reducing --gpu-memory-utilization to 0.25
    echo   - Check that port 8083 is not already in use
    echo   - Run: nvidia-smi to verify GPU status
    pause
    exit /b 1
)

pause
