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
echo    1. llama.cpp server binary - Windows x64, CUDA 12
echo    2. CUDA 12.4 runtime DLLs
echo    3. Llama-3.2-3B-Instruct  - Validator + QA Auditor  ~2.0 GB
echo    4. Llama-3.1-8B-Instruct  - Planner/Worker          ~4.9 GB
echo.
echo  Total download: ~7.3 GB
echo  Destination: %SCRIPT_DIR%
echo.
echo ============================================================================
echo.

REM ---------------------------------------------------------------------------
REM  Check for curl
REM ---------------------------------------------------------------------------
where curl >nul 2>&1
if not "%ERRORLEVEL%"=="0" goto :no_curl
goto :curl_ok

:no_curl
echo [ERROR] curl not found. Windows 10+ includes curl by default.
echo         If you are on an older OS, install curl and add it to PATH.
goto :error_exit

:curl_ok

REM ---------------------------------------------------------------------------
REM  Configuration
REM ---------------------------------------------------------------------------
set "LLAMA_CPP_TAG=b8354"
set "BIN_DIR=%SCRIPT_DIR%bin"

REM ---------------------------------------------------------------------------
REM  Step 1: Download llama.cpp server binary
REM ---------------------------------------------------------------------------
if exist "%BIN_DIR%\llama-server.exe" goto :skip_bin
echo [1/4] Downloading llama.cpp %LLAMA_CPP_TAG% - Windows CUDA 12...
curl -L --progress-bar -o "llama-bin.zip" "https://github.com/ggml-org/llama.cpp/releases/download/%LLAMA_CPP_TAG%/llama-%LLAMA_CPP_TAG%-bin-win-cuda-12.4-x64.zip"
if not "%ERRORLEVEL%"=="0" goto :err_bin_dl

echo [1/4] Extracting llama.cpp binary...
if not exist "%BIN_DIR%" mkdir "%BIN_DIR%"
powershell -NoProfile -Command "Expand-Archive -Path 'llama-bin.zip' -DestinationPath 'llama-bin-temp' -Force"
if not "%ERRORLEVEL%"=="0" goto :err_bin_extract

REM The zip extracts to a subfolder -- move contents up
for /d %%D in (llama-bin-temp\*) do (
    xcopy /E /Y /Q "%%D\*" "%BIN_DIR%\" >nul 2>&1
)
REM If files are directly in llama-bin-temp
xcopy /Y /Q "llama-bin-temp\*.exe" "%BIN_DIR%\" >nul 2>&1
xcopy /Y /Q "llama-bin-temp\*.dll" "%BIN_DIR%\" >nul 2>&1

rmdir /S /Q "llama-bin-temp" 2>nul
del "llama-bin.zip" 2>nul

if not exist "%BIN_DIR%\llama-server.exe" goto :err_bin_missing
echo [1/4] Done.
goto :step2

:skip_bin
echo [SKIP] llama.cpp server already exists at %BIN_DIR%\llama-server.exe
goto :step2

:err_bin_dl
echo [ERROR] Failed to download llama.cpp binary. Check your internet connection.
goto :error_exit

:err_bin_extract
echo [ERROR] Failed to extract llama-bin.zip
goto :error_exit

:err_bin_missing
echo [ERROR] llama-server.exe not found after extraction.
echo         The zip structure may have changed. Check %BIN_DIR% manually.
goto :error_exit

REM ---------------------------------------------------------------------------
REM  Step 2: Download CUDA runtime DLLs
REM ---------------------------------------------------------------------------
:step2
if exist "%BIN_DIR%\cudart64_12.dll" goto :skip_cuda
echo [2/4] Downloading CUDA 12.4 runtime DLLs...
curl -L --progress-bar -o "cuda-dlls.zip" "https://github.com/ggml-org/llama.cpp/releases/download/%LLAMA_CPP_TAG%/cudart-llama-bin-win-cuda-12.4-x64.zip"
if not "%ERRORLEVEL%"=="0" goto :err_cuda_dl

echo [2/4] Extracting CUDA DLLs...
powershell -NoProfile -Command "Expand-Archive -Path 'cuda-dlls.zip' -DestinationPath 'cuda-temp' -Force"
if not "%ERRORLEVEL%"=="0" goto :err_cuda_extract

REM Copy all DLLs into bin
for /r "cuda-temp" %%F in (*.dll) do (
    copy /Y "%%F" "%BIN_DIR%\" >nul 2>&1
)

rmdir /S /Q "cuda-temp" 2>nul
del "cuda-dlls.zip" 2>nul
echo [2/4] Done.
goto :step3

:skip_cuda
echo [SKIP] CUDA DLLs already present.
goto :step3

:err_cuda_dl
echo [ERROR] Failed to download CUDA DLLs. Check your internet connection.
goto :error_exit

:err_cuda_extract
echo [ERROR] Failed to extract cuda-dlls.zip
goto :error_exit

REM ---------------------------------------------------------------------------
REM  Step 3: Download Llama 3.2 3B
REM ---------------------------------------------------------------------------
:step3
set "MODEL_3B_FILE=Llama-3.2-3B-Instruct-Q4_K_M.gguf"
if exist "%SCRIPT_DIR%%MODEL_3B_FILE%" goto :skip_3b
echo [3/4] Downloading Llama 3.2 3B Instruct Q4_K_M - ~2.0 GB...
echo        This may take several minutes depending on your connection.
curl -L --progress-bar -o "%MODEL_3B_FILE%" "https://huggingface.co/bartowski/Llama-3.2-3B-Instruct-GGUF/resolve/main/Llama-3.2-3B-Instruct-Q4_K_M.gguf"
if not "%ERRORLEVEL%"=="0" goto :err_3b
echo [3/4] Done.
goto :step4

:skip_3b
echo [SKIP] %MODEL_3B_FILE% already exists.
goto :step4

:err_3b
echo [ERROR] Failed to download %MODEL_3B_FILE%.
goto :error_exit

REM ---------------------------------------------------------------------------
REM  Step 4: Download Llama 3.1 8B
REM ---------------------------------------------------------------------------
:step4
set "MODEL_8B_FILE=Meta-Llama-3.1-8B-Instruct-Q4_K_M.gguf"
if exist "%SCRIPT_DIR%%MODEL_8B_FILE%" goto :skip_8b
echo [4/4] Downloading Llama 3.1 8B Instruct Q4_K_M - ~4.9 GB...
echo        This may take several minutes depending on your connection.
curl -L --progress-bar -o "%MODEL_8B_FILE%" "https://huggingface.co/bartowski/Meta-Llama-3.1-8B-Instruct-GGUF/resolve/main/Meta-Llama-3.1-8B-Instruct-Q4_K_M.gguf"
if not "%ERRORLEVEL%"=="0" goto :err_8b
echo [4/4] Done.
goto :verify

:skip_8b
echo [SKIP] %MODEL_8B_FILE% already exists.
goto :verify

:err_8b
echo [ERROR] Failed to download %MODEL_8B_FILE%.
goto :error_exit

REM ---------------------------------------------------------------------------
REM  Verify
REM ---------------------------------------------------------------------------
:verify
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

if exist "%SCRIPT_DIR%%MODEL_3B_FILE%" (
    echo  [OK] %MODEL_3B_FILE%
) else (
    echo  [MISSING] %MODEL_3B_FILE%
    set "ALL_GOOD=0"
)

if exist "%SCRIPT_DIR%%MODEL_8B_FILE%" (
    echo  [OK] %MODEL_8B_FILE%
) else (
    echo  [MISSING] %MODEL_8B_FILE%
    set "ALL_GOOD=0"
)

echo.
if "!ALL_GOOD!"=="1" goto :success
goto :partial_fail

:success
echo ============================================================================
echo  Setup complete. All files downloaded successfully.
echo ============================================================================
echo.
echo  To start the inference servers:
echo.
echo    start-validator.bat    Llama 3.2 3B on port 8080
echo    start-planner.bat      Llama 3.1 8B on port 8081
echo    start-qa.bat           Llama 3.2 3B on port 8082
echo    start-all.bat          All three servers
echo.
echo  The gatekeeper connects to these automatically.
echo ============================================================================
goto :done

:partial_fail
echo ============================================================================
echo  [WARNING] Some files are missing. Re-run this script to retry.
echo ============================================================================
goto :done

:error_exit
echo.
echo ============================================================================
echo  Setup failed. See error above.
echo ============================================================================
echo.
echo Press any key to close...
pause >nul
exit /b 1

:done
echo.
echo Press any key to close...
pause >nul
exit /b 0
