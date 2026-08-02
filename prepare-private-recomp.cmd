@echo off
setlocal
set "ROOT=%~dp0"
set "ROM=%~1"
if "%ROM%"=="" set "ROM=%USERPROFILE%\Downloads\Kirby Super Star (USA)\Kirby Super Star (USA).sfc"
if not exist "%ROM%" (
  echo ROM not found: "%ROM%"
  echo Usage: prepare-private-recomp.cmd "C:\path\to\Kirby Super Star ^(USA^).sfc"
  exit /b 3
)
set "PYTHONPATH=%ROOT%tools"
set "PYTHON=%ROOT%.venv\Scripts\python.exe"
if not exist "%PYTHON%" set "PYTHON=python"
"%PYTHON%" -m recompiler.kssrecomp.private_build --rom "%ROM%" --out "%ROOT%.private\generated\first-frame"
if errorlevel 1 exit /b %ERRORLEVEL%
echo Private static-recompiler outputs are current under .private\generated\first-frame
