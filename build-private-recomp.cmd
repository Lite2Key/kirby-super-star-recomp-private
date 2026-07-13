@echo off
setlocal
set "ROOT=%~dp0"
call "%ROOT%prepare-private-recomp.cmd" %*
if errorlevel 1 exit /b %ERRORLEVEL%
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul
if errorlevel 1 exit /b %ERRORLEVEL%
cmake --preset windows-msvc-private
if errorlevel 1 exit /b %ERRORLEVEL%
cmake --build --preset windows-msvc-private-debug
if errorlevel 1 exit /b %ERRORLEVEL%
ctest --preset windows-msvc-private-debug
exit /b %ERRORLEVEL%
