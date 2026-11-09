$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$installation = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $installation) { throw 'Visual Studio C++ build tools are required.' }
$vcvars = Join-Path $installation 'VC/Auxiliary/Build/vcvars64.bat'

Push-Location $repoRoot
try {
    New-Item -ItemType Directory -Force build/llm-tests | Out-Null
    $compile = 'cl /nologo /std:c++20 /EHsc /utf-8 /Gy /DUNICODE /D_UNICODE /DNOMINMAX /DWIN32_LEAN_AND_MEAN /Fe:build\llm-tests\llm_backend_probe.exe /Fo:build\llm-tests\ tests\integration\llm_backend_probe.cpp Ime3\rtas_translation.cpp Ime3\rtas_utils.cpp src\config\provider_settings.cpp src\llm\model_catalog.cpp src\llm\local_server.cpp /link /OPT:REF winhttp.lib user32.lib ole32.lib uuid.lib'
    & $env:ComSpec /d /c "`"$vcvars`" >nul && $compile"
    if ($LASTEXITCODE -ne 0) { throw 'Translation probe compilation failed.' }
    python tests/integration/test_llm_backends.py build/llm-tests/llm_backend_probe.exe
    if ($LASTEXITCODE -ne 0) { throw 'LLM backend integration tests failed.' }
} finally {
    Pop-Location
}
