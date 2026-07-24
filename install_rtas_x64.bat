@echo off
setlocal
set "DLL_PATH=%~dp0x64\Debug\Ime3.dll"
set "SRC_CONFIG=%~dp0config\ime_settings.json"
set "DST_CONFIG_DIR=%~dp0x64\Debug\config"
set "DST_CONFIG=%DST_CONFIG_DIR%\ime_settings.json"
set "CLSID={64AD179E-ADBC-4BEC-B3E8-260997333DE7}"

if not exist "%DLL_PATH%" (
    echo RTAS install failed. DLL not found: "%DLL_PATH%"
    exit /b 1
)
if not exist "%SRC_CONFIG%" (
    echo RTAS install failed. Config not found: "%SRC_CONFIG%"
    exit /b 1
)

powershell -NoProfile -ExecutionPolicy Bypass -Command ^
  "$dll='%DLL_PATH%'; $src='%SRC_CONFIG%'; $dst='%DST_CONFIG%'; $dir=Split-Path -Parent $dst; " ^
  "if(!(Test-Path $dir)){ New-Item -ItemType Directory -Path $dir -Force | Out-Null }; " ^
  "Copy-Item -Force $src $dst; " ^
  "$p = Start-Process -FilePath \"$env:SystemRoot\\System32\\regsvr32.exe\" -ArgumentList '/s', $dll -Verb RunAs -PassThru -Wait; exit $p.ExitCode"
if %errorlevel% NEQ 0 (
    echo RTAS install failed. regsvr32 code=%errorlevel%
    exit /b %errorlevel%
)

reg query "HKCR\CLSID\%CLSID%\InprocServer32" /ve | findstr /I /C:"%DLL_PATH%" >nul
if %errorlevel% EQU 0 (
    echo RTAS install completed.
) else (
    echo RTAS install failed. DLL registration was not detected.
    exit /b 1
)
exit /b 0

