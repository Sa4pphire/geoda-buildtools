@echo off
setlocal

REM Resolve the repository root from BuildTools\windows\Release.
set "SCRIPT_DIR=%~dp0"
for %%I in ("%SCRIPT_DIR%..\..\..") do set "ROOT_DIR=%%~fI"

set "GDAL_DATA=%ROOT_DIR%\data\gdal"
set "PROJ_LIB=%ROOT_DIR%\data\proj"
set "PROJ_DATA=%ROOT_DIR%\data\proj"

if not exist "%GDAL_DATA%" (
  echo [ERROR] GDAL data directory not found: %GDAL_DATA%
  exit /b 1
)

if not exist "%PROJ_DATA%\proj.db" (
  echo [ERROR] PROJ database not found: %PROJ_DATA%\proj.db
  exit /b 1
)

if not exist "%SCRIPT_DIR%GeoDa.exe" (
  echo [ERROR] GeoDa.exe not found. Build Release x64 first.
  echo         Expected: %SCRIPT_DIR%GeoDa.exe
  exit /b 1
)

echo GDAL_DATA=%GDAL_DATA%
echo PROJ_LIB=%PROJ_LIB%
echo PROJ_DATA=%PROJ_DATA%

start "" /D "%SCRIPT_DIR%" "%SCRIPT_DIR%GeoDa.exe"
