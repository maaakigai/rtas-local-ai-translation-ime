param(
    [ValidateSet("template", "validate")]
    [string]$Mode = "template",

    [ValidateSet("bridge", "server", "imm32", "dictionary", "native")]
    [string]$Backend = "bridge",

    [string]$Corpus = "tests/samples/provider_comparison/phase0_cases.tsv",

    [string]$Output = "",

    [string]$Result = "",

    [int]$TopN = 8
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$AllowedBackends = @("bridge", "server", "imm32", "dictionary", "native")
$AllowedNativeBackends = @("", "mozc_server_client", "linked_converter")
$AllowedFallbackSources = @("", "bridge", "imm32", "dictionary")
$ForbiddenProtocolSourceFragments = @(
    "googlejapaneseinput",
    "google japanese input",
    "private pipe",
    "named pipe",
    "hand-coded",
    "hand coded",
    "field number",
    "manual protobuf"
)

function Resolve-RepoPath {
    param([Parameter(Mandatory=$true)][string]$Path)

    if ([System.IO.Path]::IsPathRooted($Path)) {
        return $Path
    }
    return (Join-Path (Get-Location) $Path)
}

function Get-PropertyValue {
    param(
        [Parameter(Mandatory=$true)]$Object,
        [Parameter(Mandatory=$true)][string]$Name
    )

    $prop = $Object.PSObject.Properties[$Name]
    if ($null -eq $prop) {
        return $null
    }
    return $prop.Value
}

function Test-PropertyExists {
    param(
        [Parameter(Mandatory=$true)]$Object,
        [Parameter(Mandatory=$true)][string]$Name
    )

    return ($null -ne $Object.PSObject.Properties[$Name])
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

    $ids = @{}
    foreach ($row in $rows) {
        if ([string]::IsNullOrWhiteSpace($row.id)) {
            throw "Corpus contains a row with an empty id."
        }
        if ($ids.ContainsKey($row.id)) {
            throw "Corpus contains duplicate id '$($row.id)'."
        }
        $ids[$row.id] = $row
    }

    return $rows
}

function Convert-BackendToTransport {
    param([Parameter(Mandatory=$true)][string]$BackendName)

    switch ($BackendName) {
        "bridge" { return "bridge" }
        "server" { return "server" }
        "imm32" { return "imm32" }
        "native" { return "native" }
        default { return "" }
    }
}

function Convert-BackendToEffectiveTransport {
    param([Parameter(Mandatory=$true)][string]$BackendName)

    switch ($BackendName) {
        "server" { return "bridge" }
        "dictionary" { return "" }
        default { return $BackendName }
    }
}

function Convert-BackendToKanaMode {
    param([Parameter(Mandatory=$true)][string]$BackendName)

    if ($BackendName -eq "dictionary") {
        return "dictionary"
    }
    return "mozc"
}

function New-TemplateRecord {
    param(
        [Parameter(Mandatory=$true)]$Row,
        [Parameter(Mandatory=$true)][string]$BackendName,
        [Parameter(Mandatory=$true)][int]$CandidateLimit
    )

    $nativeBackend = ""
    if ($BackendName -eq "native") {
        $nativeBackend = "mozc_server_client"
    }

    return [ordered]@{
        schema_version = 1
        corpus_id = $Row.id
        category = $Row.category
        reading = $Row.reading
        committed_text = $Row.committed_text
        hint = $Row.hint
        backend = $BackendName
        kana_mode = Convert-BackendToKanaMode $BackendName
        transport = Convert-BackendToTransport $BackendName
        effective_transport = Convert-BackendToEffectiveTransport $BackendName
        native_backend = $nativeBackend
        top_n = $CandidateLimit
        top_candidates = @()
        entries = @()
        segments = @()
        segment_source = ""
        error = ""
        pending = $false
        request_id = $null
        fallback_used = $false
        fallback_source = ""
        cold_latency_ms = $null
        warm_latency_ms = $null
        layer2_impact = [ordered]@{
            checked = $false
            result = ""
            notes = ""
        }
        translation_impact = [ordered]@{
            checked = $false
            result = ""
            notes = ""
        }
        source_provenance = "manual_provider_boundary"
        protocol_source = if ($BackendName -eq "native") { "generated_mozc_session_proto_or_client_boundary_required" } else { "" }
        mozc_commit = if ($BackendName -eq "native") { "unresolved_phase2_spike" } else { "" }
        mozc_build_artifact = if ($BackendName -eq "native") { "unresolved_phase2_spike" } else { "" }
        native_runtime = if ($BackendName -eq "native") { "app_local_mozc_server_client" } else { "" }
        native_wrapper_exe = if ($BackendName -eq "native") { "unresolved_phase3_app_local_runtime" } else { "" }
        native_server_exe = if ($BackendName -eq "native") { "unresolved_phase3_app_local_runtime" } else { "" }
        input_scope = "repo_corpus"
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
        $lines.Add(($record | ConvertTo-Json -Depth 8 -Compress))
    }
    [System.IO.File]::WriteAllLines($resolved, $lines, $utf8NoBom)
    Write-Host ("Wrote {0} records: {1}" -f $lines.Count, $resolved)
}

function Validate-Record {
    param(
        [Parameter(Mandatory=$true)]$Record,
        [Parameter(Mandatory=$true)]$CorpusById,
        [Parameter(Mandatory=$true)][int]$LineNumber
    )

    $errors = New-Object System.Collections.Generic.List[string]
    $required = @(
        "schema_version",
        "corpus_id",
        "reading",
        "backend",
        "top_candidates",
        "segments",
        "error",
        "fallback_used",
        "fallback_source",
        "cold_latency_ms",
        "warm_latency_ms",
        "layer2_impact",
        "translation_impact",
        "input_scope"
    )

    foreach ($name in $required) {
        if (-not (Test-PropertyExists -Object $Record -Name $name)) {
            $errors.Add("line ${LineNumber}: missing field '$name'")
        }
    }

    if ($errors.Count -gt 0) {
        return $errors
    }

    $schemaVersion = Get-PropertyValue -Object $Record -Name "schema_version"
    if ($schemaVersion -ne 1) {
        $errors.Add("line ${LineNumber}: schema_version must be 1")
    }

    $corpusId = [string](Get-PropertyValue -Object $Record -Name "corpus_id")
    if (-not $CorpusById.ContainsKey($corpusId)) {
        $errors.Add("line ${LineNumber}: corpus_id '$corpusId' is not in the corpus")
    } else {
        $row = $CorpusById[$corpusId]
        $reading = [string](Get-PropertyValue -Object $Record -Name "reading")
        if ($reading -ne $row.reading) {
            $errors.Add("line ${LineNumber}: reading for '$corpusId' does not match the corpus")
        }
        if (Test-PropertyExists -Object $Record -Name "committed_text") {
            $committed = [string](Get-PropertyValue -Object $Record -Name "committed_text")
            if ($committed -ne $row.committed_text) {
                $errors.Add("line ${LineNumber}: committed_text for '$corpusId' does not match the corpus")
            }
        }
    }

    $backend = [string](Get-PropertyValue -Object $Record -Name "backend")
    if ($AllowedBackends -notcontains $backend) {
        $errors.Add("line ${LineNumber}: backend '$backend' is not allowed")
    }

    $inputScope = [string](Get-PropertyValue -Object $Record -Name "input_scope")
    if ($inputScope -ne "repo_corpus") {
        $errors.Add("line ${LineNumber}: input_scope must be 'repo_corpus'")
    }

    $topCandidates = Get-PropertyValue -Object $Record -Name "top_candidates"
    if ($null -ne $topCandidates -and $topCandidates.GetType().Name -ne "Object[]") {
        $errors.Add("line ${LineNumber}: top_candidates must be an array")
    }

    $segments = Get-PropertyValue -Object $Record -Name "segments"
    if ($null -ne $segments -and $segments.GetType().Name -ne "Object[]") {
        $errors.Add("line ${LineNumber}: segments must be an array")
    }

    $fallbackUsed = [bool](Get-PropertyValue -Object $Record -Name "fallback_used")
    $fallbackSource = [string](Get-PropertyValue -Object $Record -Name "fallback_source")
    if ($fallbackUsed -and [string]::IsNullOrWhiteSpace($fallbackSource)) {
        $errors.Add("line ${LineNumber}: fallback_source is required when fallback_used is true")
    }
    if ($AllowedFallbackSources -notcontains $fallbackSource) {
        $errors.Add("line ${LineNumber}: fallback_source '$fallbackSource' is not allowed")
    }

    if (Test-PropertyExists -Object $Record -Name "native_backend") {
        $nativeBackend = [string](Get-PropertyValue -Object $Record -Name "native_backend")
        if ($AllowedNativeBackends -notcontains $nativeBackend) {
            $errors.Add("line ${LineNumber}: native_backend '$nativeBackend' is not allowed")
        }
    }

    if ($backend -eq "native") {
        $nativeRequired = @("native_backend", "protocol_source", "mozc_commit", "mozc_build_artifact")
        foreach ($name in $nativeRequired) {
            if (-not (Test-PropertyExists -Object $Record -Name $name)) {
                $errors.Add("line ${LineNumber}: native record missing field '$name'")
            }
        }

        $nativeBackend = [string](Get-PropertyValue -Object $Record -Name "native_backend")
        if ($nativeBackend -ne "mozc_server_client") {
            $errors.Add("line ${LineNumber}: Phase 2 native records must use native_backend='mozc_server_client'")
        }

        foreach ($name in @("protocol_source", "mozc_commit", "mozc_build_artifact")) {
            $value = [string](Get-PropertyValue -Object $Record -Name $name)
            if ([string]::IsNullOrWhiteSpace($value)) {
                $errors.Add("line ${LineNumber}: native field '$name' must not be empty")
            }
        }

        $protocolSource = ([string](Get-PropertyValue -Object $Record -Name "protocol_source")).ToLowerInvariant()
        foreach ($fragment in $ForbiddenProtocolSourceFragments) {
            if ($protocolSource.Contains($fragment)) {
                $errors.Add("line ${LineNumber}: protocol_source must not refer to private pipes or hand-coded protobuf fields")
                break
            }
        }

        if ($fallbackUsed) {
            $errors.Add("line ${LineNumber}: native spike records must keep fallback_used=false unless an explicit fallback test is added separately")
        }
        if (-not [string]::IsNullOrWhiteSpace($fallbackSource)) {
            $errors.Add("line ${LineNumber}: native spike records must keep fallback_source empty when fallback_used=false")
        }
    }

    return $errors
}

function Invoke-TemplateMode {
    $rows = @(Read-Corpus $Corpus)
    $records = @()
    foreach ($row in $rows) {
        $records += New-TemplateRecord -Row $row -BackendName $Backend -CandidateLimit $TopN
    }

    $target = $Output
    if ([string]::IsNullOrWhiteSpace($target)) {
        $target = Join-Path "tmp_provider_comparison" ("{0}.jsonl" -f $Backend)
    }

    Write-JsonLines -Records $records -Path $target
}

function Invoke-ValidateMode {
    if ([string]::IsNullOrWhiteSpace($Result)) {
        throw "-Result is required when -Mode validate is used."
    }

    $rows = @(Read-Corpus $Corpus)
    $corpusById = @{}
    foreach ($row in $rows) {
        $corpusById[$row.id] = $row
    }

    $resolvedInput = Resolve-RepoPath $Result
    if (-not (Test-Path -LiteralPath $resolvedInput)) {
        throw "Input JSONL not found: $resolvedInput"
    }

    $allErrors = New-Object System.Collections.Generic.List[string]
    $lineNumber = 0
    foreach ($line in [System.IO.File]::ReadLines($resolvedInput, [System.Text.Encoding]::UTF8)) {
        $lineNumber += 1
        if ([string]::IsNullOrWhiteSpace($line)) {
            continue
        }

        try {
            $record = $line | ConvertFrom-Json
        } catch {
            $allErrors.Add("line ${lineNumber}: invalid JSON: $($_.Exception.Message)")
            continue
        }

        $recordErrors = Validate-Record -Record $record -CorpusById $corpusById -LineNumber $lineNumber
        foreach ($err in $recordErrors) {
            $allErrors.Add($err)
        }
    }

    if ($lineNumber -eq 0) {
        $allErrors.Add("input JSONL is empty: $resolvedInput")
    }

    if ($allErrors.Count -gt 0) {
        foreach ($err in $allErrors) {
            Write-Error $err
        }
        exit 1
    }

    Write-Host ("Validated provider comparison JSONL: {0}" -f $resolvedInput)
}

switch ($Mode) {
    "template" { Invoke-TemplateMode }
    "validate" { Invoke-ValidateMode }
}
