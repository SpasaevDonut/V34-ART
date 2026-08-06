@echo off
setlocal

set "ROOT=%~dp0"
set "POWERSHELL=%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe"
set "PACKAGE=%ROOT%dist\v34-art-v1.0.zip"

if not exist "%POWERSHELL%" (
  echo [ERROR] Windows PowerShell was not found.
  exit /b 1
)

"%POWERSHELL%" -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%ROOT%scripts\build.ps1" %*
if errorlevel 1 (
  echo.
  echo [ERROR] ART V1.0 build failed.
  exit /b 1
)

if not exist "%PACKAGE%" (
  echo.
  echo [ERROR] Build completed, but the release ZIP was not created.
  echo [ERROR] Expected: "%PACKAGE%"
  exit /b 1
)

echo.
echo ART V1.0 build completed.
echo Release ZIP: "%PACKAGE%"
exit /b 0
