$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$installation = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $installation) { throw 'Visual Studio C++ build tools are required.' }
$vcvars = Join-Path $installation 'VC/Auxiliary/Build/vcvars64.bat'
$testDirectory = Join-Path ([System.IO.Path]::GetTempPath()) ('rtas-settings-tests-' + [Guid]::NewGuid())
New-Item -ItemType Directory -Path $testDirectory | Out-Null

Push-Location $repoRoot
try {
    New-Item -ItemType Directory -Force build/llm-tests | Out-Null
    $compile = 'cl /nologo /std:c++20 /EHsc /utf-8 /DUNICODE /D_UNICODE /DNOMINMAX /DWIN32_LEAN_AND_MEAN /Fe:build\llm-tests\settings_editor_tests.exe /Fo:build\llm-tests\ tests\unit\settings_editor_tests.cpp src\config\settings_editor.cpp src\config\provider_settings.cpp'
    & $env:ComSpec /d /c "`"$vcvars`" >nul && $compile"
    if ($LASTEXITCODE -ne 0) { throw 'Settings tests compilation failed.' }
    & ./build/llm-tests/settings_editor_tests.exe $testDirectory
    if ($LASTEXITCODE -ne 0) { throw 'Settings editor tests failed.' }
} finally {
    Pop-Location
    # The test removes its file. Remove only the empty directory we created.
    if ((Get-ChildItem -LiteralPath $testDirectory).Count -eq 0) {
        Remove-Item -LiteralPath $testDirectory
    }
}
