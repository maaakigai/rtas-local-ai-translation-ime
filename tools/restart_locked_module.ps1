param(
    [string]$Module = "Ime3.dll",
    [switch]$ListOnly,
    [switch]$ForceRestart,
    [switch]$NoRestart,
    [string[]]$IncludeImageName = @(),
    [string[]]$ExcludeImageName = @("System", "System Idle Process", "csrss.exe", "wininit.exe", "services.exe", "lsass.exe", "smss.exe", "dwm.exe"),
    [switch]$IncludeSystemProcesses
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Get-LockingProcesses {
    param([string]$TargetModule)

    $rows = @()
    $csv = & tasklist /m $TargetModule /fo csv /nh 2>$null
    if (-not $csv) {
        return @()
    }

    foreach ($line in $csv) {
        if ([string]::IsNullOrWhiteSpace($line)) { continue }
        if ($line -like "INFO:*") { continue }

        $item = $line | ConvertFrom-Csv -Header "ImageName","PID","SessionName","SessionNum","MemUsage","Modules"
        if (-not $item) { continue }

        $procId = 0
        [void][int]::TryParse($item.PID, [ref]$procId)
        if ($procId -le 0) { continue }

        $cim = Get-CimInstance Win32_Process -Filter ("ProcessId={0}" -f $procId) -ErrorAction SilentlyContinue
        $rows += [pscustomobject]@{
            ImageName      = $item.ImageName
            PID            = $procId
            ExecutablePath = $cim.ExecutablePath
            CommandLine    = $cim.CommandLine
        }
    }

    return $rows | Sort-Object PID -Unique
}

function Match-Name {
    param(
        [Parameter(Mandatory=$true)][string]$Name,
        [string[]]$Patterns = @()
    )
    if (-not $Patterns -or $Patterns.Count -eq 0) {
        return $false
    }
    foreach ($p in $Patterns) {
        if ([string]::IsNullOrWhiteSpace($p)) { continue }
        if ($Name -like $p) { return $true }
    }
    return $false
}

function Filter-LockingProcesses {
    param(
        [Parameter(Mandatory=$true)]$Processes,
        [string[]]$Include = @(),
        [string[]]$Exclude = @(),
        [switch]$AllowSystem
    )
    $out = @()
    foreach ($proc in $Processes) {
        $name = ""
        if ($null -ne $proc.ImageName) { $name = [string]$proc.ImageName }
        if ([string]::IsNullOrWhiteSpace($name)) { continue }
        if ((-not $AllowSystem) -and (Match-Name -Name $name -Patterns $Exclude)) {
            continue
        }
        if ($Include -and $Include.Count -gt 0) {
            if (-not (Match-Name -Name $name -Patterns $Include)) { continue }
        }
        $out += $proc
    }
    return $out | Sort-Object PID -Unique
}

function Stop-And-Restart {
    param(
        [Parameter(Mandatory=$true)]$Proc,
        [switch]$Restart
    )

    Write-Host ("[stop] {0} (PID={1})" -f $Proc.ImageName, $Proc.PID)
    try {
        Stop-Process -Id $Proc.PID -Force -ErrorAction Stop
    } catch {
        Write-Warning ("Failed to stop PID={0}: {1}" -f $Proc.PID, $_.Exception.Message)
        return
    }

    if (-not $Restart) { return }

    $imageName = ""
    if ($null -ne $Proc.ImageName) {
        $imageName = [string]$Proc.ImageName
    }
    $name = $imageName.ToLowerInvariant()
    if ($name -eq "explorer.exe") {
        Write-Host "[start] explorer.exe"
        Start-Process explorer.exe | Out-Null
        return
    }

    if ([string]::IsNullOrWhiteSpace($Proc.ExecutablePath)) {
        Write-Warning ("Skip restart PID={0}: executable path not available." -f $Proc.PID)
        return
    }

    Write-Host ("[start] {0}" -f $Proc.ExecutablePath)
    try {
        Start-Process -FilePath $Proc.ExecutablePath | Out-Null
    } catch {
        Write-Warning ("Failed to restart {0}: {1}" -f $Proc.ExecutablePath, $_.Exception.Message)
    }
}

$locks = @(Get-LockingProcesses -TargetModule $Module)

if (-not $locks -or $locks.Count -eq 0) {
    Write-Host ("No process is locking module: {0}" -f $Module)
    exit 0
}

$targets = Filter-LockingProcesses `
    -Processes $locks `
    -Include $IncludeImageName `
    -Exclude $ExcludeImageName `
    -AllowSystem:$IncludeSystemProcesses

Write-Host ("Locking processes for {0}:" -f $Module)
$locks | Format-Table ImageName, PID, ExecutablePath -AutoSize

if (-not $targets -or $targets.Count -eq 0) {
    Write-Host ""
    Write-Host "No target process remains after include/exclude filtering."
    Write-Host "Tip: use -IncludeImageName \"explorer.exe\",\"notepad.exe\" (wildcards allowed)"
    exit 0
}

Write-Host ""
Write-Host "Target processes:"
$targets | Format-Table ImageName, PID, ExecutablePath -AutoSize

if ($ListOnly -or (-not $ForceRestart)) {
    Write-Host ""
    Write-Host "Dry-run mode. Use -ForceRestart to stop processes."
    Write-Host "Example:"
    Write-Host ("  powershell -ExecutionPolicy Bypass -File `"{0}`" -Module {1} -IncludeImageName explorer.exe,notepad.exe -ForceRestart" -f $PSCommandPath, $Module)
    exit 0
}

$confirmation = Read-Host "Proceed stopping target processes? Type YES to continue"
if ($confirmation -ne "YES") {
    Write-Host "Canceled."
    exit 1
}

$restart = -not $NoRestart
foreach ($proc in $targets) {
    Stop-And-Restart -Proc $proc -Restart:$restart
}

Write-Host "Done."
