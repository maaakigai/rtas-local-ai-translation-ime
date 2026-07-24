@echo off
setlocal EnableExtensions

rem Self-elevate if not already running as administrator.
net session >nul 2>&1
if %errorlevel% NEQ 0 (
    echo Administrator privileges are required.
    echo Please run this script from an elevated command prompt.
    exit /b 1
)

set "SCRIPT_DIR=%~dp0"
set "ACTION="
set "CONFIG=Debug"
set "PLATFORM=Win32"

:parse_args
if "%~1"=="" goto args_done

if not defined ACTION (
    set "ACTION=%~1"
    shift
    goto parse_args
)

if /I "%~1"=="--config" (
    if "%~2"=="" (
        echo Missing value for --config option.
        exit /b 1
    )
    set "CONFIG=%~2"
    shift
    shift
    goto parse_args
)

if /I "%~1"=="--platform" (
    if "%~2"=="" (
        echo Missing value for --platform option.
        exit /b 1
    )
    set "PLATFORM=%~2"
    shift
    shift
    goto parse_args
)

echo Unknown argument: %~1
exit /b 1

:args_done

if not defined ACTION (
    goto :usage
)

set "OUTPUT_DIR=%SCRIPT_DIR%"
if /I "%PLATFORM%"=="Win32" (
    set "OUTPUT_DIR=%OUTPUT_DIR%%CONFIG%"
) else (
    set "OUTPUT_DIR=%OUTPUT_DIR%%PLATFORM%\%CONFIG%"
)

set "DLL_PATH=%OUTPUT_DIR%\Ime3.dll"

if not exist "%DLL_PATH%" (
    echo Target DLL not found: "%DLL_PATH%"
    echo Please build the project or adjust --config / --platform settings.
    exit /b 1
)

if /I "%PLATFORM%"=="Win32" (
    set "REGSVR32=%SystemRoot%\SysWOW64\regsvr32.exe"
) else (
    set "REGSVR32=%SystemRoot%\System32\regsvr32.exe"
)

if /I "%ACTION%"=="install" (
    set "SRC_CONFIG=%SCRIPT_DIR%config\ime_settings.json"
    set "DST_CONFIG_DIR=%OUTPUT_DIR%\config"
    set "DST_CONFIG=%DST_CONFIG_DIR%\ime_settings.json"
    if exist "%SRC_CONFIG%" (
        if not exist "%DST_CONFIG_DIR%" (
            mkdir "%DST_CONFIG_DIR%" >nul 2>&1
        )
        copy /Y "%SRC_CONFIG%" "%DST_CONFIG%" >nul
        if %errorlevel% EQU 0 (
            echo Copied config to "%DST_CONFIG%"
        ) else (
            echo Failed to copy config to "%DST_CONFIG%"
            exit /b 1
        )
    ) else (
        echo Source config not found: "%SRC_CONFIG%"
        exit /b 1
    )
    echo Running %REGSVR32% /s "%DLL_PATH%"
    "%REGSVR32%" /s "%DLL_PATH%"
    exit /b %errorlevel%
)

if /I "%ACTION%"=="uninstall" (
    echo Running %REGSVR32% /u /s "%DLL_PATH%"
    "%REGSVR32%" /u /s "%DLL_PATH%"
    exit /b %errorlevel%
)

echo Unknown action: %ACTION%
echo Supported actions are install and uninstall.
exit /b 1

:usage
echo Registers or unregisters the RTAS DLL via regsvr32.
echo.
echo Usage:
echo    register_rtas.bat install   [--config Debug] [--platform Win32^|x64]
echo    register_rtas.bat uninstall [--config Debug] [--platform Win32^|x64]
echo.
echo Defaults: --config Debug, --platform Win32.
exit /b 1

