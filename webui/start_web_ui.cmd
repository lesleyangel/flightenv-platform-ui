@echo off
setlocal
set "FLIGHTENV_WEBUI_PORT=8787"
cd /d "%~dp0"

where python >nul 2>nul
if errorlevel 1 (
  echo [ERROR] Python was not found in PATH.
  pause
  exit /b 1
)

echo [FlightEnv WebUI] Starting local server...
echo [FlightEnv WebUI] URL: http://127.0.0.1:%FLIGHTENV_WEBUI_PORT%/?page=workspace
start "" "http://127.0.0.1:%FLIGHTENV_WEBUI_PORT%/?page=workspace"

python "%~dp0server.py"
echo.
echo [FlightEnv WebUI] Server exited. If there is an error above, send it to Codex.
pause
