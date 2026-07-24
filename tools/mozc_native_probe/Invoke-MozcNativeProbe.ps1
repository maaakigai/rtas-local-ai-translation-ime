param(
    [string]$Manifest = "tests/samples/provider_comparison/mozc_native_artifact_manifest.example.json",

    [string]$Corpus = "tests/samples/provider_comparison/phase0_cases.tsv",

    [string]$Output = "tmp_provider_comparison/native_artifact_smoke.jsonl",

    [int]$TopN = 8,

    [switch]$ServerStartSmoke,

    [int]$ServerStartTimeoutMs = 2000
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Resolve-RepoPath {
    param([Parameter(Mandatory=$true)][string]$Path)

    if ([System.IO.Path]::IsPathRooted($Path)) {
        return $Path
    }
    return (Join-Path (Get-Location) $Path)
}

function Test-PropertyExists {
    param(
        [Parameter(Mandatory=$true)]$Object,
        [Parameter(Mandatory=$true)][string]$Name
    )

    return ($null -ne $Object.PSObject.Properties[$Name])
}

function Get-PropertyValue {
    param(
        [Parameter(Mandatory=$true)]$Object,
        [Parameter(Mandatory=$true)][string]$Name
    )

    if ($Object -is [System.Collections.IDictionary]) {
        if ($Object.Contains($Name)) {
            return $Object[$Name]
        }
        return $null
    }

    $prop = $Object.PSObject.Properties[$Name]
    if ($null -eq $prop) {
        return $null
    }
    return $prop.Value
}

function Read-JsonFile {
    param([Parameter(Mandatory=$true)][string]$Path)

    $resolved = Resolve-RepoPath $Path
    if (-not (Test-Path -LiteralPath $resolved)) {
        throw "JSON file not found: $resolved"
    }
    return (Get-Content -LiteralPath $resolved -Encoding UTF8 -Raw | ConvertFrom-Json)
}

function Read-Corpus {
    param([Parameter(Mandatory=$true)][string]$Path)

    $resolved = Resolve-RepoPath $Path
    if (-not (Test-Path -LiteralPath $resolved)) {
        throw "Corpus not found: $resolved"
    }

    $rows = @(Import-Csv -LiteralPath $resolved -Delimiter "`t" -Encoding UTF8)
    if ($rows.Count -eq 0) {
        throw "Corpus is empty: $resolved"
    }

    $required = @("id", "category", "reading", "committed_text", "hint", "notes")
    foreach ($column in $required) {
        if (-not (Test-PropertyExists -Object $rows[0] -Name $column)) {
            throw "Corpus is missing required column '$column': $resolved"
        }
    }

    return $rows
}

function ConvertTo-ExternalProbePath {
    param([Parameter(Mandatory=$true)][string]$Location)

    if ($Location.StartsWith("external:", [System.StringComparison]::OrdinalIgnoreCase)) {
        return $Location.Substring("external:".Length)
    }
    return $Location
}

function Resolve-ProbeLocation {
    param([Parameter(Mandatory=$true)][string]$Location)

    $probePath = ConvertTo-ExternalProbePath $Location
    $resolved = $probePath
    if (-not [System.IO.Path]::IsPathRooted($probePath)) {
        $resolved = Resolve-RepoPath $probePath
    }

    return [pscustomobject]@{
        location = $Location
        probe_path = $probePath
        resolved_path = $resolved
    }
}

function Get-FileSha256 {
    param([Parameter(Mandatory=$true)][string]$Path)

    return (Get-FileHash -Algorithm SHA256 -LiteralPath $Path).Hash.ToLowerInvariant()
}

function Test-ArtifactPath {
    param([Parameter(Mandatory=$true)][string]$Location)

    $locationInfo = Resolve-ProbeLocation $Location
    $exists = Test-Path -LiteralPath $locationInfo.resolved_path
    $result = [ordered]@{
        location = $Location
        probe_path = $locationInfo.probe_path
        exists = $exists
    }

    if ($exists -and (Test-Path -LiteralPath $locationInfo.resolved_path -PathType Leaf)) {
        $item = Get-Item -LiteralPath $locationInfo.resolved_path
        $result["length"] = $item.Length
        $result["sha256"] = Get-FileSha256 -Path $locationInfo.resolved_path
    }

    return $result
}

function Join-ManifestLocation {
    param(
        [Parameter(Mandatory=$true)][string]$Root,
        [Parameter(Mandatory=$true)][string]$Child
    )

    if ($Root.EndsWith("/", [System.StringComparison]::Ordinal) -or
        $Root.EndsWith("\", [System.StringComparison]::Ordinal)) {
        return "$Root$Child"
    }
    return "$Root/$Child"
}

function Get-MozcServerLocation {
    param([Parameter(Mandatory=$true)]$ManifestObject)

    $artifact = Get-PropertyValue -Object $ManifestObject -Name "artifact"
    if ($null -eq $artifact) {
        return ""
    }

    $extract = Get-PropertyValue -Object $artifact -Name "administrative_extract"
    if ($null -eq $extract) {
        return ""
    }

    $root = [string](Get-PropertyValue -Object $extract -Name "root")
    if ([string]::IsNullOrWhiteSpace($root)) {
        return ""
    }

    $serverName = [string](Get-PropertyValue -Object $extract -Name "mozc_server")
    if ([string]::IsNullOrWhiteSpace($serverName)) {
        $serverName = "mozc_server.exe"
    }

    return (Join-ManifestLocation -Root $root -Child $serverName)
}

function Test-MozcServerPath {
    param([string]$Location)

    if ([string]::IsNullOrWhiteSpace($Location)) {
        return [ordered]@{
            location = ""
            probe_path = ""
            exists = $false
        }
    }

    $locationInfo = Resolve-ProbeLocation $Location
    $exists = Test-Path -LiteralPath $locationInfo.resolved_path -PathType Leaf
    $result = [ordered]@{
        location = $Location
        probe_path = $locationInfo.probe_path
        exists = $exists
    }

    if ($exists) {
        $item = Get-Item -LiteralPath $locationInfo.resolved_path
        $result["length"] = $item.Length
        $result["sha256"] = Get-FileSha256 -Path $locationInfo.resolved_path
    }

    return $result
}

function Invoke-MozcServerStartSmoke {
    param(
        [string]$Location,
        [Parameter(Mandatory=$true)][int]$TimeoutMs,
        [Parameter(Mandatory=$true)][bool]$Requested
    )

    if (-not $Requested) {
        return [ordered]@{
            requested = $false
            status = "not_requested"
        }
    }

    if ([string]::IsNullOrWhiteSpace($Location)) {
        return [ordered]@{
            requested = $true
            status = "unavailable"
            process_started = $false
            error = "manifest administrative_extract root is missing"
        }
    }

    $locationInfo = Resolve-ProbeLocation $Location
    if (-not (Test-Path -LiteralPath $locationInfo.resolved_path -PathType Leaf)) {
        return [ordered]@{
            requested = $true
            status = "unavailable"
            process_started = $false
            error = "mozc_server.exe was not found at the manifest path"
        }
    }

    $process = $null
    try {
        $workingDirectory = Split-Path -Parent $locationInfo.resolved_path
        $process = Start-Process `
            -FilePath $locationInfo.resolved_path `
            -WorkingDirectory $workingDirectory `
            -WindowStyle Hidden `
            -PassThru

        Start-Sleep -Milliseconds $TimeoutMs

        if ($process.HasExited) {
            return [ordered]@{
                requested = $true
                status = "exited_immediately"
                process_started = $true
                process_observed_running = $false
                exit_code = $process.ExitCode
                cleanup = "not_needed"
            }
        }

        Stop-Process -Id $process.Id -Force
        try {
            Wait-Process -Id $process.Id -Timeout 5 | Out-Null
        } catch {
            # The forced stop already requests cleanup; keep the smoke result explicit.
        }

        return [ordered]@{
            requested = $true
            status = "ok"
            process_started = $true
            process_observed_running = $true
            exit_code = $null
            cleanup = "forced_stop_after_start_smoke"
        }
    } catch {
        if ($null -ne $process -and -not $process.HasExited) {
            Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
        }
        return [ordered]@{
            requested = $true
            status = "error"
            process_started = ($null -ne $process)
            error = $_.Exception.Message
        }
    }
}

function Get-CodePage {
    try {
        return ((& chcp) -join " ").Trim()
    } catch {
        return ""
    }
}

function Get-EnvironmentSnapshot {
    return [ordered]@{
        windows_version = [System.Environment]::OSVersion.VersionString
        architecture = [System.Environment]::GetEnvironmentVariable("PROCESSOR_ARCHITECTURE")
        user_ui_culture = [System.Globalization.CultureInfo]::CurrentUICulture.Name
        current_culture = [System.Globalization.CultureInfo]::CurrentCulture.Name
        code_page = Get-CodePage
        pythonutf8 = [System.Environment]::GetEnvironmentVariable("PYTHONUTF8")
        pythonioencoding = [System.Environment]::GetEnvironmentVariable("PYTHONIOENCODING")
    }
}

function Join-ProtocolSource {
    param([Parameter(Mandatory=$true)]$ManifestObject)

    $sources = @(Get-PropertyValue -Object $ManifestObject -Name "protocol_sources")
    if ($sources.Count -eq 0) {
        return ""
    }
    return ($sources -join ";")
}

function Get-HumanConfirmationList {
    param([Parameter(Mandatory=$true)]$ManifestObject)

    $items = @(Get-PropertyValue -Object $ManifestObject -Name "human_confirmation_required_before")
    if ($items.Count -eq 0) {
        return @()
    }
    return $items
}

function New-SmokeRecord {
    param(
        [Parameter(Mandatory=$true)]$Row,
        [Parameter(Mandatory=$true)]$ManifestObject,
        [Parameter(Mandatory=$true)]$ArtifactProbe,
        [Parameter(Mandatory=$true)]$ServerProbe,
        [Parameter(Mandatory=$true)]$ServerStartResult,
        [Parameter(Mandatory=$true)][string]$ProtocolSource,
        [Parameter(Mandatory=$true)][int]$CandidateLimit
    )

    $artifact = Get-PropertyValue -Object $ManifestObject -Name "artifact"
    $artifactExists = [bool]$ArtifactProbe.exists
    $serverExists = [bool]$ServerProbe.exists
    $serverStartStatus = [string](Get-PropertyValue -Object $ServerStartResult -Name "status")
    $error = ""
    $sessionStatus = "not_run_artifact_missing"
    $candidateStatus = "not_run_artifact_missing"
    $segmentStatus = "not_run_artifact_missing"
    $serverDiscoveredStatus = "unavailable"

    if (-not $artifactExists) {
        $error = "mozc_server_client artifact unavailable: manifest artifact was not found; no fallback was used."
    } elseif (-not $serverExists) {
        $error = "mozc_server_client artifact was discovered, but mozc_server.exe was not found in the manifest administrative extraction."
        $sessionStatus = "not_run_server_missing"
        $candidateStatus = "not_run_server_missing"
        $segmentStatus = "not_run_server_missing"
    } elseif ($serverStartStatus -eq "ok") {
        $error = "mozc_server_client artifact and mozc_server.exe start smoke succeeded; typed client/session probe is still required before conversion."
        $serverDiscoveredStatus = "ok"
        $sessionStatus = "not_run_client_wrapper_missing"
        $candidateStatus = "not_run_client_wrapper_missing"
        $segmentStatus = "not_run_client_wrapper_missing"
    } elseif ($serverStartStatus -eq "not_requested") {
        $error = "mozc_server_client artifact and mozc_server.exe were discovered; server start smoke was not requested."
        $serverDiscoveredStatus = "ok"
        $sessionStatus = "not_run_server_start_not_requested"
        $candidateStatus = "not_run_server_start_not_requested"
        $segmentStatus = "not_run_server_start_not_requested"
    } else {
        $error = "mozc_server_client mozc_server.exe start smoke failed: $serverStartStatus"
        $serverDiscoveredStatus = "ok"
        $sessionStatus = "not_run_server_start_failed"
        $candidateStatus = "not_run_server_start_failed"
        $segmentStatus = "not_run_server_start_failed"
    }

    return [ordered]@{
        schema_version = 1
        corpus_id = $Row.id
        category = $Row.category
        reading = $Row.reading
        committed_text = $Row.committed_text
        hint = $Row.hint
        backend = "native"
        kana_mode = "mozc"
        transport = "native"
        effective_transport = "native"
        native_backend = [string](Get-PropertyValue -Object $ManifestObject -Name "native_backend")
        top_n = $CandidateLimit
        top_candidates = @()
        entries = @()
        segments = @()
        segment_source = "unavailable"
        error = $error
        pending = $false
        request_id = $null
        fallback_used = $false
        fallback_source = ""
        cold_latency_ms = $null
        warm_latency_ms = $null
        layer2_impact = [ordered]@{
            checked = $false
            result = ""
            notes = "not checked by artifact smoke"
        }
        translation_impact = [ordered]@{
            checked = $false
            result = ""
            notes = "not checked by artifact smoke"
        }
        source_provenance = "mozc_native_probe_manifest"
        protocol_source = $ProtocolSource
        mozc_commit = [string](Get-PropertyValue -Object $ManifestObject -Name "mozc_commit")
        mozc_build_artifact = [string](Get-PropertyValue -Object $artifact -Name "location")
        input_scope = "repo_corpus"
        artifact_probe = $ArtifactProbe
        server_probe = $ServerProbe
        server_start_smoke = $ServerStartResult
        build_environment = Get-EnvironmentSnapshot
        session_lifecycle = @(
            [ordered]@{ step = "manifest_loaded"; status = "ok" },
            [ordered]@{ step = "artifact_discovered"; status = $(if ($artifactExists) { "ok" } else { "unavailable" }) },
            [ordered]@{ step = "server_executable_discovered"; status = $serverDiscoveredStatus },
            [ordered]@{ step = "server_start"; status = $serverStartStatus },
            [ordered]@{ step = "session_create"; status = $sessionStatus },
            [ordered]@{ step = "ime_on"; status = $sessionStatus },
            [ordered]@{ step = "kana_input"; status = $sessionStatus },
            [ordered]@{ step = "convert_trigger"; status = $sessionStatus },
            [ordered]@{ step = "session_delete"; status = $sessionStatus }
        )
        candidate_extraction = [ordered]@{
            status = $candidateStatus
            expected_fields = @("Output.candidate_window", "CandidateWindow.candidates", "Output.all_candidate_words")
        }
        segment_extraction = [ordered]@{
            status = $segmentStatus
            segment_source = "unavailable"
            expected_fields = @("Output.preedit.Segment")
        }
        human_confirmation_required_before = Get-HumanConfirmationList -ManifestObject $ManifestObject
        notes = $Row.notes
    }
}

function Write-JsonLines {
    param(
        [Parameter(Mandatory=$true)]$Records,
        [Parameter(Mandatory=$true)][string]$Path
    )

    $resolved = Resolve-RepoPath $Path
    $parent = Split-Path -Parent $resolved
    if (-not [string]::IsNullOrWhiteSpace($parent)) {
        New-Item -ItemType Directory -Force -Path $parent | Out-Null
    }

    $utf8NoBom = New-Object System.Text.UTF8Encoding($false)
    $lines = New-Object System.Collections.Generic.List[string]
    foreach ($record in $Records) {
        $lines.Add(($record | ConvertTo-Json -Depth 12 -Compress))
    }
    [System.IO.File]::WriteAllLines($resolved, $lines, $utf8NoBom)
    Write-Host ("Wrote {0} native smoke records: {1}" -f $lines.Count, $resolved)
}

$manifestObject = Read-JsonFile $Manifest
$artifact = Get-PropertyValue -Object $manifestObject -Name "artifact"
if ($null -eq $artifact) {
    throw "Manifest is missing artifact object: $Manifest"
}

$artifactLocation = [string](Get-PropertyValue -Object $artifact -Name "location")
if ([string]::IsNullOrWhiteSpace($artifactLocation)) {
    throw "Manifest artifact.location must not be empty: $Manifest"
}

$protocolSource = Join-ProtocolSource -ManifestObject $manifestObject
if ([string]::IsNullOrWhiteSpace($protocolSource)) {
    throw "Manifest protocol_sources must not be empty: $Manifest"
}

$artifactProbe = Test-ArtifactPath -Location $artifactLocation
$serverLocation = Get-MozcServerLocation -ManifestObject $manifestObject
$serverProbe = Test-MozcServerPath -Location $serverLocation
$serverStartResult = Invoke-MozcServerStartSmoke `
    -Location $serverLocation `
    -TimeoutMs $ServerStartTimeoutMs `
    -Requested ([bool]$ServerStartSmoke)
$rows = @(Read-Corpus $Corpus)
$records = @()
foreach ($row in $rows) {
    $records += New-SmokeRecord `
        -Row $row `
        -ManifestObject $manifestObject `
        -ArtifactProbe $artifactProbe `
        -ServerProbe $serverProbe `
        -ServerStartResult $serverStartResult `
        -ProtocolSource $protocolSource `
        -CandidateLimit $TopN
}

Write-JsonLines -Records $records -Path $Output

if (-not [bool]$artifactProbe.exists) {
    Write-Host "Artifact unavailable. No download, build, install, or system-wide IME change was attempted."
} elseif ([string](Get-PropertyValue -Object $serverStartResult -Name "status") -eq "ok") {
    Write-Host "Mozc server start smoke succeeded. No install or system-wide IME change was attempted."
}
