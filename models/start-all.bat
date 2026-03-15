@echo off
REM ============================================================================
REM  Start all 4 inference servers
REM
REM  NOTE: The 3B model is shared between Validator and QA Auditor.
REM  llama.cpp memory-maps the GGUF file, so two instances of the same model
REM  share the weights in RAM/VRAM. No double memory cost.
REM
REM  Ports:
REM    8080 - Validator        (Llama 3.2 3B via llama.cpp)
REM    8081 - Worker           (Llama 3.1 8B via llama.cpp)
REM    8082 - QA Auditor       (Llama 3.2 3B via llama.cpp)
REM    8083 - Spatial Reasoner (Cosmos Reason2 2B via vLLM)
REM ============================================================================
setlocal

set "SCRIPT_DIR=%~dp0"
cd /d "%SCRIPT_DIR%"

echo ============================================================================
echo  Starting all AgenticMCP inference servers...
echo ============================================================================
echo.

REM Start each in its own window so you can see logs independently
start "AgenticMCP - Validator (port 8080)" cmd /c "start-validator.bat"
timeout /t 3 /nobreak >nul

start "AgenticMCP - Worker (port 8081)" cmd /c "start-planner.bat"
timeout /t 3 /nobreak >nul

start "AgenticMCP - QA Auditor (port 8082)" cmd /c "start-qa.bat"
timeout /t 3 /nobreak >nul

start "AgenticMCP - Spatial Reasoner (port 8083)" cmd /c "start-spatial.bat"

echo.
echo  All servers launching. Check individual windows for status.
echo.
echo  Validator:         http://localhost:8080  (llama.cpp)
echo  Worker:            http://localhost:8081  (llama.cpp)
echo  QA Auditor:        http://localhost:8082  (llama.cpp)
echo  Spatial Reasoner:  http://localhost:8083  (vLLM)
echo.
echo  The gatekeeper connects to these automatically.
echo  Close this window when done. Server windows stay open independently.
echo.
pause
