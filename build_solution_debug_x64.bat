@echo off
setlocal

set "ROOT_DIR=%~dp0"
pushd "%ROOT_DIR%" >nul

set "TARGET=Build"
if /I "%~1"=="rebuild" set "TARGET=Rebuild"

set "MSBUILD_EXE="

if exist "%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" (
  for /f "usebackq delims=" %%I in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.Component.MSBuild -find MSBuild\**\Bin\amd64\MSBuild.exe`) do (
    set "MSBUILD_EXE=%%I"
    goto :msbuild_found
  )
)

if exist "%ProgramFiles(x86)%\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\amd64\MSBuild.exe" (
  set "MSBUILD_EXE=%ProgramFiles(x86)%\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\amd64\MSBuild.exe"
  goto :msbuild_found
)

if exist "%ProgramFiles%\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\amd64\MSBuild.exe" (
  set "MSBUILD_EXE=%ProgramFiles%\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\amd64\MSBuild.exe"
  goto :msbuild_found
)

echo [ERROR] MSBuild.exe could not be found.
echo         Install Visual Studio Build Tools 2022 or adjust this script path.
popd >nul
pause
exit /b 1

:msbuild_found
echo [INFO] Using MSBuild: "%MSBUILD_EXE%"
echo [INFO] Target: %TARGET%
echo [INFO] Building Ime3.sln ^(Debug x64^)^...

"%MSBUILD_EXE%" "Ime3.sln" /t:%TARGET% /p:Configuration=Debug /p:Platform=x64 /p:CLToolAdditionalOptions="/FS"
set "BUILD_RC=%ERRORLEVEL%"

if not "%BUILD_RC%"=="0" (
  echo.
  echo [FAIL] Build failed. Exit code: %BUILD_RC%
  popd >nul
  pause
  exit /b %BUILD_RC%
)

echo.
echo [OK] Build succeeded.
echo      Output: x64\Debug\
popd >nul
pause
exit /b 0
