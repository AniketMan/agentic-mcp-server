@echo off
REM ============================================================================
REM  AgenticMCP Local Inference Setup
REM  Downloads llama.cpp server (CUDA 12) and 3 Llama GGUF models.
REM  Run once. Everything stays in this folder.
REM ============================================================================
setlocal enabledelayedexpansion

set "SCRIPT_DIR=%~dp0"
cd /d "%SCRIPT_DIR%"

echo ============================================================================
echo  AgenticMCP Local Inference Setup
echo ============================================================================
echo.
echo  This script downloads:
echo    1. llama.cpp server binary (Windows x64, CUDA 12)
echo    2. CUDA 12.4 runtime DLLs
echo    3. Llama-3.2-3B-Instruct  (Validator + QA Auditor)  ~2.0 GB
echo    4. Llama-3.1-8B-Instruct  (Planner/Worker)          ~4.9 GB
echo    5. Cosmos Reason2 2B       (Spatial Reasoner)        ~4.0 GB (via pip/HuggingFace)
echo.
echo  Total download: ~11.3 GB
echo  Destination: %SCRIPT_DIR%
echo.
echo ============================================================================
echo.

REM ---------------------------------------------------------------------------
REM  Check for curl (ships with Windows 10+)
REM ---------------------------------------------------------------------------
where curl >nul 2>&1
if !ERRORLEVEL! neq 0 (
    echo [ERROR] curl not found. Windows 10+ includes curl by default.
    echo         If you are on an older OS, install curl and add it to PATH.
    goto :error_exit
)

REM ---------------------------------------------------------------------------
REM  Configuration
REM ---------------------------------------------------------------------------
set "LLAMA_CPP_TAG=b8354"
set "LLAMA_BIN_URL=https://github.com/ggml-org/llama.cpp/releases/download/!LLAMA_CPP_TAG!/llama-!LLAMA_CPP_TAG!-bin-win-cuda-12.4-x64.zip"
set "CUDA_DLL_URL=https://github.com/ggml-org/llama.cpp/releases/download/!LLAMA_CPP_TAG!/cudart-llama-bin-win-cuda-12.4-x64.zip"

set "MODEL_3B_URL=https://huggingface.co/bartowski/Llama-3.2-3B-Instruct-GGUF/resolve/main/Llama-3.2-3B-Instruct-Q4_K_M.gguf"
set "MODEL_3B_FILE=Llama-3.2-3B-Instruct-Q4_K_M.gguf"

set "MODEL_8B_URL=https://huggingface.co/bartowski/Meta-Llama-3.1-8B-Instruct-GGUF/resolve/main/Meta-Llama-3.1-8B-Instruct-Q4_K_M.gguf"
set "MODEL_8B_FILE=Meta-Llama-3.1-8B-Instruct-Q4_K_M.gguf"

set "BIN_DIR=%SCRIPT_DIR%bin"

REM ---------------------------------------------------------------------------
REM  Step 1: Download llama.cpp server binary
REM ---------------------------------------------------------------------------
if exist "%BIN_DIR%\llama-server.exe" (
    echo [SKIP] llama.cpp server already exists at %BIN_DIR%\llama-server.exe
    goto :step2
)

echo [1/5] Downloading llama.cpp !LLAMA_CPP_TAG! - Windows CUDA 12...
curl -L --progress-bar -o "llama-bin.zip" "!LLAMA_BIN_URL!"
if !ERRORLEVEL! neq 0 (
    echo [ERROR] Failed to download llama.cpp binary.
    echo         URL: !LLAMA_BIN_URL!
    echo         Check your internet connection and try again.
    goto :error_exit
)

echo [1/5] Extracting llama.cpp binary...
if not exist "%BIN_DIR%" mkdir "%BIN_DIR%"
powershell -NoProfile -Command "Expand-Archive -Path 'llama-bin.zip' -DestinationPath 'llama-bin-temp' -Force"
if !ERRORLEVEL! neq 0 (
    echo [ERROR] Failed to extract llama-bin.zip
    goto :error_exit
)

REM The zip extracts to a subfolder -- move contents up
for /d %%D in (llama-bin-temp\*) do (
    xcopy /E /Y /Q "%%D\*" "%BIN_DIR%\" >nul 2>&1
)
REM If files are directly in llama-bin-temp (no subfolder)
xcopy /Y /Q "llama-bin-temp\*.exe" "%BIN_DIR%\" >nul 2>&1
xcopy /Y /Q "llama-bin-temp\*.dll" "%BIN_DIR%\" >nul 2>&1

rmdir /S /Q "llama-bin-temp" 2>nul
del "llama-bin.zip" 2>nul

if not exist "%BIN_DIR%\llama-server.exe" (
    echo [ERROR] llama-server.exe not found after extraction.
    echo         The zip structure may have changed. Check %BIN_DIR% manually.
    goto :error_exit
)
echo [1/5] Done.

:step2
REM ---------------------------------------------------------------------------
REM  Step 2: Download CUDA runtime DLLs
REM ---------------------------------------------------------------------------
if exist "%BIN_DIR%\cudart64_12.dll" (
    echo [SKIP] CUDA DLLs already present.
    goto :step3
)

echo [2/5] Downloading CUDA 12.4 runtime DLLs...
curl -L --progress-bar -o "cuda-dlls.zip" "!CUDA_DLL_URL!"
if !ERRORLEVEL! neq 0 (
    echo [ERROR] Failed to download CUDA DLLs.
    echo         URL: !CUDA_DLL_URL!
    goto :error_exit
)

echo [2/5] Extracting CUDA DLLs...
powershell -NoProfile -Command "Expand-Archive -Path 'cuda-dlls.zip' -DestinationPath 'cuda-temp' -Force"
if !ERRORLEVEL! neq 0 (
    echo [ERROR] Failed to extract cuda-dlls.zip
    goto :error_exit
)

REM Copy all DLLs into bin
for /r "cuda-temp" %%F in (*.dll) do (
    copy /Y "%%F" "%BIN_DIR%\" >nul 2>&1
)

rmdir /S /Q "cuda-temp" 2>nul
del "cuda-dlls.zip" 2>nul
echo [2/5] Done.

:step3
REM ---------------------------------------------------------------------------
REM  Step 3: Download Llama 3.2 3B (Validator + QA Auditor)
REM ---------------------------------------------------------------------------
if exist "%SCRIPT_DIR%!MODEL_3B_FILE!" (
    echo [SKIP] !MODEL_3B_FILE! already exists.
    goto :step4
)

echo [3/5] Downloading Llama 3.2 3B Instruct Q4_K_M - approx 2.0 GB...
echo        This may take several minutes depending on your connection.
curl -L --progress-bar -o "!MODEL_3B_FILE!" "!MODEL_3B_URL!"
if !ERRORLEVEL! neq 0 (
    echo [ERROR] Failed to download !MODEL_3B_FILE!.
    echo         URL: !MODEL_3B_URL!
    goto :error_exit
)
echo [3/5] Done.

:step4
REM ---------------------------------------------------------------------------
REM  Step 4: Download Llama 3.1 8B (Planner/Worker)
REM ---------------------------------------------------------------------------
if exist "%SCRIPT_DIR%!MODEL_8B_FILE!" (
    echo [SKIP] !MODEL_8B_FILE! already exists.
    goto :verify
)

echo [4/5] Downloading Llama 3.1 8B Instruct Q4_K_M - approx 4.9 GB...
echo        This may take several minutes depending on your connection.
curl -L --progress-bar -o "!MODEL_8B_FILE!" "!MODEL_8B_URL!"
if !ERRORLEVEL! neq 0 (
    echo [ERROR] Failed to download !MODEL_8B_FILE!.
    echo         URL: !MODEL_8B_URL!
    goto :error_exit
)
echo [4/5] Done.

:step5
REM ---------------------------------------------------------------------------
REM  Step 5: Install vLLM and pre-download Cosmos Reason2 2B
REM ---------------------------------------------------------------------------
echo.
echo [5/5] Setting up Cosmos Reason2 (Spatial Reasoner)...
echo.

REM Check Python
where python >nul 2>&1
if !ERRORLEVEL! neq 0 (
    echo [WARN] Python not found. Cosmos Reason2 requires Python 3.10+.
    echo        Install from https://www.python.org/downloads/
    echo        Skipping Cosmos setup. You can run start-spatial.bat later
    echo        and it will install dependencies automatically.
    goto :verify
)

REM Install vLLM
python -c "import vllm" >nul 2>&1
if !ERRORLEVEL! neq 0 (
    echo [5/5] Installing vLLM... (this may take several minutes)
    pip install "vllm>=0.11.0" --quiet
    if !ERRORLEVEL! neq 0 (
        echo [WARN] vLLM install failed. You can retry manually: pip install "vllm>=0.11.0"
        echo        Skipping Cosmos pre-download.
        goto :verify
    )
    echo [5/5] vLLM installed.
) else (
    echo [SKIP] vLLM already installed.
)

REM Install transformers
pip install "transformers>=4.57.0" --quiet >nul 2>&1

REM Pre-download the model so first start is fast
echo [5/5] Pre-downloading Cosmos Reason2 2B from HuggingFace (~4 GB)...
python -c "from huggingface_hub import snapshot_download; snapshot_download('nvidia/Cosmos-Reason2-2B')" 2>nul
if !ERRORLEVEL! neq 0 (
    echo [WARN] Model pre-download failed. It will download on first start-spatial.bat run.
) else (
    echo [5/5] Cosmos Reason2 2B cached successfully.
)

echo [5/5] Done.

:verify
REM ---------------------------------------------------------------------------
REM  Verify
REM ---------------------------------------------------------------------------
echo.
echo ============================================================================
echo  Verification
echo ============================================================================

set "ALL_GOOD=1"

if exist "%BIN_DIR%\llama-server.exe" (
    echo  [OK] llama-server.exe
) else (
    echo  [MISSING] llama-server.exe
    set "ALL_GOOD=0"
)

if exist "%SCRIPT_DIR%!MODEL_3B_FILE!" (
    echo  [OK] !MODEL_3B_FILE!
) else (
    echo  [MISSING] !MODEL_3B_FILE!
    set "ALL_GOOD=0"
)

if exist "%SCRIPT_DIR%!MODEL_8B_FILE!" (
    echo  [OK] !MODEL_8B_FILE!
) else (
    echo  [MISSING] !MODEL_8B_FILE!
    set "ALL_GOOD=0"
)

echo.
if "!ALL_GOOD!"=="1" (
    echo ============================================================================
    echo  Setup complete. All files downloaded successfully.
    echo ============================================================================
    echo.
    echo  To start the inference server, use one of the launcher scripts:
    echo.
    echo    start-validator.bat    Llama 3.2 3B on port 8080  - Validator
    echo    start-planner.bat     Llama 3.1 8B on port 8081  - Planner/Worker
    echo    start-qa.bat          Llama 3.2 3B on port 8082  - QA Auditor
    echo    start-spatial.bat     Cosmos Reason2 on port 8083 - Spatial Reasoner
    echo    start-all.bat         All servers (llama.cpp only, start spatial separately)
    echo.
    echo  The gatekeeper - Tools/gatekeeper/ - connects to these automatically.
    echo ============================================================================
) else (
    echo ============================================================================
    echo  [WARNING] Some files are missing. Re-run this script to retry.
    echo ============================================================================
)

echo.
echo Press any key to close...
pause >nul
exit /b 0

:error_exit
echo.
echo ============================================================================
echo  Setup failed. See error above. Press any key to close.
echo ============================================================================
pause >nul
exit /b 1
