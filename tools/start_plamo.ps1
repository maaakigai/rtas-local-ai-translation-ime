param(
    [ValidateRange(1, 65535)]
    [int]$Port = 8080,
    [ValidateSet('0.8b', '2b')]
    [string]$Size = '0.8b',
    [ValidateSet('cpu', 'gpu')]
    [string]$Device,
    [string]$ConfigPath,
    [switch]$Restart,
    [switch]$ApplySavedDevice,
    [string]$ResultPath
)

$ErrorActionPreference = 'Stop'
function Write-LaunchResult([bool]$success, [string]$message) {
    if ($ResultPath) {
        $json = @{ success = $success; message = $message } | ConvertTo-Json
        [IO.File]::WriteAllText($ResultPath, $json, [Text.UTF8Encoding]::new($false))
    }
}
trap {
    Write-LaunchResult $false $_.Exception.Message
    Write-Error $_ -ErrorAction Continue
    exit 1
}
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$runtime = Join-Path $repoRoot 'build/llama_cpp/b12110/llama-server.exe'
$model = Join-Path $repoRoot "build/models/plamo-3-translate-$Size-Q4_K_M.gguf"
$logDirectory = Join-Path $repoRoot 'build/runtime'
$alias = 'plamo-3-translate'
$deviceRequested = $PSBoundParameters.ContainsKey('Device')
if (!$ConfigPath) {
    foreach ($registryRoot in @('HKCU:', 'HKLM:')) {
        $key = "$registryRoot\Software\Classes\CLSID\{64AD179E-ADBC-4BEC-B3E8-260997333DE7}\InprocServer32"
        if (Test-Path $key) {
            $registered = (Get-Item $key).GetValue('')
            $candidate = Join-Path (Split-Path $registered -Parent) 'config/ime_settings.json'
            if (Test-Path -LiteralPath $candidate) { $ConfigPath = $candidate; break }
        }
    }
    if (!$ConfigPath) { $ConfigPath = Join-Path $repoRoot 'config/ime_settings.json' }
}
if (!$Device -and (Test-Path -LiteralPath $ConfigPath)) {
    $settings = Get-Content -LiteralPath $ConfigPath -Raw -Encoding UTF8 | ConvertFrom-Json
    if ($settings.provider.translation.llm.device) {
        $Device = $settings.provider.translation.llm.device
        $deviceRequested = $true
    }
}
if (!$Device) { $Device = 'gpu' }
if ($Device -notin @('cpu', 'gpu')) { throw '推論先はcpuまたはgpuで指定してください。' }
$statePath = Join-Path $logDirectory "plamo-$Port.state.json"

if (!(Test-Path -LiteralPath $runtime)) {
    throw 'llama.cppとPLaMoモデルが必要です。docs/operations/llama_cpp.mdを確認してください。'
}

Add-Type -AssemblyName System.Net.Http
$handler = [System.Net.Http.HttpClientHandler]::new()
$handler.UseProxy = $false
$client = [System.Net.Http.HttpClient]::new($handler)
$client.Timeout = [TimeSpan]::FromSeconds(2)
$baseUrl = "http://127.0.0.1:$Port"
$startupMutex = [Threading.Mutex]::new($false, "Local\RTASPlamoServer-$Port")
$ownsStartupLock = $false
$settingsMutex = $null
$ownsSettingsLock = $false

function Test-ServerReady {
    try {
        $health = $client.GetStringAsync("$baseUrl/health").GetAwaiter().GetResult() | ConvertFrom-Json
        return $health.status -eq 'ok'
    } catch {
        return $false
    }
}

function Confirm-ServerModel {
    $models = $client.GetStringAsync("$baseUrl/v1/models").GetAwaiter().GetResult() | ConvertFrom-Json
    $properties = $client.GetStringAsync("$baseUrl/props").GetAwaiter().GetResult() | ConvertFrom-Json
    if ($models.data.id -notcontains $alias -or !$properties.model_path -or
        [IO.Path]::GetFullPath($properties.model_path) -ne [IO.Path]::GetFullPath($model)) {
        throw 'このポートでは指定と異なるモデルが動いています。既存サーバーと指定サイズを確認してください。'
    }
}

function Get-ManagedServer {
    try {
        if (!(Test-Path -LiteralPath $statePath)) { return $null }
        $state = Get-Content -LiteralPath $statePath -Raw -Encoding UTF8 | ConvertFrom-Json
        $running = Get-Process -Id $state.process_id -ErrorAction Stop
        $listeners = Get-NetTCPConnection -LocalPort $Port -State Listen -ErrorAction Stop
        if ($running.Path -ne $runtime -or
            $running.StartTime.ToUniversalTime().Ticks.ToString() -ne $state.start_ticks -or
            $listeners.OwningProcess -notcontains $running.Id -or
            [IO.Path]::GetFullPath($state.model) -ne [IO.Path]::GetFullPath($model) -or
            $state.device -notin @('cpu', 'gpu')) { return $null }
        return [pscustomobject]@{ State = $state; Process = $running }
    } catch { return $null }
}

try {
    $lockDeadline = (Get-Date).AddMinutes(3)
    while (!$ownsStartupLock -and (Get-Date) -lt $lockDeadline) {
        try { $ownsStartupLock = $startupMutex.WaitOne(1000) }
        catch [Threading.AbandonedMutexException] { $ownsStartupLock = $true }
    }
    if (!$ownsStartupLock) { throw '別のPLaMo起動処理を待機中にタイムアウトしました。' }
    if ($ApplySavedDevice) {
        $settingsMutex = [Threading.Mutex]::new($false, 'Local\RTASSettingsEditorSave')
        try { $ownsSettingsLock = $settingsMutex.WaitOne(10000) }
        catch [Threading.AbandonedMutexException] { $ownsSettingsLock = $true }
        if (!$ownsSettingsLock) { throw '別の設定処理が進行中です。時間をおいて保存してください。' }
        $latest = Get-Content -LiteralPath $ConfigPath -Raw -Encoding UTF8 | ConvertFrom-Json
        $latestLlm = $latest.provider.translation.llm
        if ($latestLlm.device -ne $Device -or $latestLlm.backend -ne 'llama_cpp' -or
            $latestLlm.host -notin @('127.0.0.1', 'localhost') -or $latestLlm.port -ne $Port -or
            $latestLlm.model -ne $alias -or $latestLlm.use_tls) {
            throw '別の画面で推論先の設定が変更されています。再読み込みしてください。'
        }
    }
    if (!$PSBoundParameters.ContainsKey('Size') -and (Test-Path -LiteralPath $statePath)) {
        $previous = Get-Content -LiteralPath $statePath -Raw -Encoding UTF8 | ConvertFrom-Json
        if ($previous.size -in @('0.8b', '2b')) { $Size = $previous.size }
    }
    $model = Join-Path $repoRoot "build/models/plamo-3-translate-$Size-Q4_K_M.gguf"
    if (!(Test-Path -LiteralPath $model)) { throw "指定モデルが見つかりません。$model" }
    if (Test-ServerReady) {
        Confirm-ServerModel
        $managed = Get-ManagedServer
        if (!$deviceRequested -or ($managed -and $managed.State.device -eq $Device)) {
            Write-Output "PLaMoサーバーは起動済みです。$baseUrl"
            Write-LaunchResult $true '推論先の設定を反映しました。'
            return
        }
        if (!$managed) { throw 'このサーバーはRTASの管理対象として確認できないため、推論先を変更できません。' }
        if (!$Restart) { throw '推論先を変更するには-Restartを指定してください。' }
        Stop-Process -Id $managed.Process.Id
        $managed.Process.WaitForExit()
    }

    New-Item -ItemType Directory -Force -Path $logDirectory | Out-Null
    $stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
    $standardOutput = Join-Path $logDirectory "plamo-$stamp.stdout.log"
    $standardError = Join-Path $logDirectory "plamo-$stamp.stderr.log"
    $process = $null
    $socket = [Net.Sockets.TcpClient]::new()
    try {
        try { $socket.Connect('127.0.0.1', $Port); $listening = $true }
        catch { $listening = $false }
    } finally { $socket.Dispose() }
    if (!$listening) {
        $deviceArguments = if ($Device -eq 'cpu') { @('--device', 'none', '-ngl', '0') }
                           else { @('--device', 'Vulkan0', '-ngl', '99') }
        $arguments = @('-m', ('"' + $model + '"'), '--alias', $alias,
            '--host', '127.0.0.1', '--port', $Port, '-c', '4096', '--parallel', '1') + $deviceArguments
        $process = Start-Process -FilePath $runtime -ArgumentList $arguments -WindowStyle Hidden -PassThru `
            -RedirectStandardOutput $standardOutput -RedirectStandardError $standardError
        $state = @{ process_id = $process.Id; start_ticks = $process.StartTime.ToUniversalTime().Ticks.ToString();
                    model = $model; device = $Device; port = $Port; size = $Size }
        $state | ConvertTo-Json | Set-Content -LiteralPath $statePath -Encoding UTF8
    }
    $deadline = (Get-Date).AddMinutes(3)
    $nextUpdate = Get-Date
    while ((Get-Date) -lt $deadline) {
        if ($process -and $process.HasExited) {
            throw "PLaMoサーバーが終了しました。ログを確認してください。$standardError"
        }
        if (Test-ServerReady) {
            Confirm-ServerModel
            if ($deviceRequested) {
                $managed = Get-ManagedServer
                if (!$managed -or $managed.State.device -ne $Device) {
                    throw '起動したサーバーの推論先を確認できません。'
                }
            }
            Write-Output "PLaMoサーバーが利用可能です。$baseUrl"
            if ($process) { Write-Output "PID=$($process.Id) ログ $standardError" }
            Write-LaunchResult $true '推論先の設定を反映しました。'
            return
        }
        if ((Get-Date) -ge $nextUpdate) {
            Write-Output 'PLaMoモデルを読み込んでいます…'
            $nextUpdate = (Get-Date).AddSeconds(15)
        }
        Start-Sleep -Seconds 1
    }
    if ($process -and !$process.HasExited) { Stop-Process -Id $process.Id }
    throw "PLaMoサーバーの起動がタイムアウトしました。ログ $standardError"
} finally {
    if ($ownsSettingsLock) { $settingsMutex.ReleaseMutex() }
    if ($settingsMutex) { $settingsMutex.Dispose() }
    if ($ownsStartupLock) { $startupMutex.ReleaseMutex() }
    $startupMutex.Dispose()
    $client.Dispose()
}
