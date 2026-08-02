@echo off
setlocal
set "ROOT=%~dp0"
set "ROM=%~1"
if "%ROM%"=="" set "ROM=%USERPROFILE%\Downloads\Kirby Super Star (USA)\Kirby Super Star (USA).sfc"
set "EXE=%ROOT%build\windows-ninja\kss-native.exe"
if not exist "%EXE%" (
  echo The Windows runtime has not been built yet.
  echo Build preset: cmake --preset windows-msvc ^&^& cmake --build --preset windows-msvc-debug
  exit /b 2
)
if not exist "%ROM%" (
  echo ROM not found: "%ROM%"
  echo Usage: run-port.cmd "C:\path\to\Kirby Super Star ^(USA^).sfc"
  exit /b 3
)
"%EXE%" --rom "%ROM%"
exit /b %ERRORLEVEL%
