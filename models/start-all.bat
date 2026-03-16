@echo off
REM ============================================================================
REM  Start inference servers for AgenticMCP v3.0 (fully local)
REM
REM  GPU Memory Budget:
REM    - Single 16GB GPU: Run Worker (8B) only. All roles auto-fallback to it.
REM    - 24GB+ GPU: Uncomment the Validator and QA lines below.
REM    - Spatial Reasoner (Cosmos Reason2): Disabled. Requires vLLM + Linux.
REM      Uncomment the spatial line when vLLM Windows support is available.
REM
REM  The gatekeeper auto-detects which servers are alive and routes accordingly.
REM  A single Worker on port 8080 is sufficient for all operations.
REM
REM  Ports (when all enabled):
REM    8080 - Validator        (Llama 3.2 3B via llama.cpp)
REM    8081 - Worker           (Llama 3.1 8B via llama.cpp)
REM    8082 - QA Auditor       (Llama 3.2 3B via llama.cpp)
REM    8083 - Spatial Reasoner (Cosmos Reason2 2B via vLLM) [DISABLED]
REM ============================================================================
setlocal

set "SCRIPT_DIR=%~dp0"
cd /d "%SCRIPT_DIR%"

echo ============================================================================
echo  Starting AgenticMCP inference servers...
echo ============================================================================
echo.

REM --- Worker (8B) on port 8081 - REQUIRED ---
REM This is the primary inference server. The gatekeeper routes all roles here
REM if no other servers are detected.
start "AgenticMCP - Worker (port 8081)" cmd /c "start-worker.bat"
timeout /t 5 /nobreak >nul

REM --- Validator (3B) on port 8080 - OPTIONAL (needs ~2GB extra VRAM) ---
REM Uncomment the next two lines if you have 24GB+ VRAM:
REM start "AgenticMCP - Validator (port 8080)" cmd /c "start-validator.bat"
REM timeout /t 3 /nobreak >nul

REM --- QA Auditor (3B) on port 8082 - OPTIONAL (shares weights with Validator) ---
REM Uncomment the next two lines if you have 24GB+ VRAM:
REM start "AgenticMCP - QA Auditor (port 8082)" cmd /c "start-qa.bat"
REM timeout /t 3 /nobreak >nul

REM --- Spatial Reasoner on port 8083 - DISABLED ---
REM Requires vLLM which needs uvloop (Linux-only). Not available on Windows.
REM Uncomment when vLLM adds Windows support:
REM start "AgenticMCP - Spatial Reasoner (port 8083)" cmd /c "start-spatial.bat"

echo.
echo  Server launched:
echo    Worker (8B):  http://localhost:8081  (llama.cpp)
echo.
echo  The gatekeeper auto-detects this server and routes all roles to it.
echo  No additional configuration needed.
echo.
echo  To enable more servers (24GB+ GPU), edit this file and uncomment lines.
echo.
echo  Close this window when done. Server windows stay open independently.
echo.
pause
