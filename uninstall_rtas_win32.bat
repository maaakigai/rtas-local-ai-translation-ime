@echo off
setlocal
set "DLL_PATH=%~dp0Debug\Ime3.dll"
set "CLSID={64AD179E-ADBC-4BEC-B3E8-260997333DE7}"

if not exist "%DLL_PATH%" (
    echo RTAS uninstall failed. DLL not found: "%DLL_PATH%"
    exit /b 1
)

powershell -NoProfile -ExecutionPolicy Bypass -Command ^
  "$dll='%DLL_PATH%'; " ^
  "$p = Start-Process -FilePath \"$env:SystemRoot\\SysWOW64\\regsvr32.exe\" -ArgumentList '/u','/s',$dll -Verb RunAs -PassThru -Wait; exit $p.ExitCode"
if %errorlevel% NEQ 0 (
    echo RTAS uninstall failed. regsvr32 code=%errorlevel%
    exit /b %errorlevel%
)

reg query "HKCR\CLSID\%CLSID%\InprocServer32" /ve | findstr /I /C:"%DLL_PATH%" >nul
if %errorlevel% EQU 0 (
    echo RTAS uninstall failed. DLL registration still exists.
    exit /b 1
)

echo RTAS uninstall completed.
exit /b 0

