@echo off
cd /d "%~dp0"
if not exist "R3nzSkin.dll" (
  echo Missing R3nzSkin.dll. Extract the complete package first.
  pause
  exit /b 1
)
if not exist "R3nzSkin_Injector.exe" (
  echo Missing R3nzSkin_Injector.exe. Extract the complete package first.
  pause
  exit /b 1
)
start "" "R3nzSkin_Injector.exe"
