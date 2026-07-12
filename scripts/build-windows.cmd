@echo off
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\Common7\Tools\VsDevCmd.bat" -arch=x64
if errorlevel 1 exit /b %errorlevel%

set "CMAKE=C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
set "CTEST=C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe"

"%CMAKE%" --preset windows-msvc
if errorlevel 1 exit /b %errorlevel%
"%CMAKE%" --build --preset windows-msvc-debug
if errorlevel 1 exit /b %errorlevel%
"%CTEST%" --preset windows-msvc-debug
exit /b %errorlevel%
