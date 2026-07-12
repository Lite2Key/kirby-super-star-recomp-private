@echo off
setlocal
set "PYTHON=%~dp0.venv\Scripts\python.exe"
if not exist "%PYTHON%" set "PYTHON=python"
"%PYTHON%" "%~dp0tools\progress\build.py" --evidence "%~dp0progress\progress.json" --out "%~dp0progress\site" --markdown "%~dp0PROGRESS.md"
if errorlevel 1 exit /b %errorlevel%
start "KSS Recomp Progress" "%~dp0progress\site\index.html"
