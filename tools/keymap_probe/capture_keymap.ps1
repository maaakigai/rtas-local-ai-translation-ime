param(
    [string]$OutputPath = "",
    [switch]$IncludeCtrlAlt
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Read-KeyEvent {
    while ($true) {
        $k = $Host.UI.RawUI.ReadKey("NoEcho,IncludeKeyDown")
        if (-not $k.KeyDown) { continue }
        # Ignore modifier-only presses so Shift/Ctrl/Alt/Win do not get captured as a target key.
        $vk = [int]$k.VirtualKeyCode
        if ($vk -in @(16, 17, 18, 91, 92)) { continue }
        return $k
    }
}

function Escape-Char {
    param([char]$c)
    if ([int][char]$c -eq 0) { return "" }
    switch ($c) {
        "`n" { return "\n" }
        "`r" { return "\r" }
        "`t" { return "\t" }
        " "  { return "SP" }
        default { return [string]$c }
    }
}

if ([string]::IsNullOrWhiteSpace($OutputPath)) {
    $stamp = Get-Date -Format "yyyyMMdd_HHmmss"
    $OutputPath = Join-Path (Get-Location) "keymap_capture_$stamp.txt"
}

$header = @()
$header += "# Key Mapping Capture"
$header += "# Created: $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')"
$header += "# Host: omitted (portable capture)"
$header += "# Note: Use the exact keyboard layout and IME state you want to inspect."
$header += "#"
$header += "# Columns:"
$header += "# Step | Label | VK(dec) | VK(hex) | Char | CharCode | ControlKeyState"
$rows = New-Object System.Collections.Generic.List[string]

$targets = @(
    @{ Label = "A";          Hint = "Press A key" },
    @{ Label = "Z";          Hint = "Press Z key" },
    @{ Label = "One";        Hint = "Press 1 key" },
    @{ Label = "Zero";       Hint = "Press 0 key" },
    @{ Label = "Minus";      Hint = "Press minus key" },
    @{ Label = "Caret";      Hint = "Press caret key" },
    @{ Label = "At";         Hint = "Press at key" },
    @{ Label = "LBracket";   Hint = "Press left bracket key" },
    @{ Label = "Semicolon";  Hint = "Press semicolon key" },
    @{ Label = "Colon";      Hint = "Press colon key" },
    @{ Label = "RBracket";   Hint = "Press right bracket key" },
    @{ Label = "Comma";      Hint = "Press comma key" },
    @{ Label = "Period";     Hint = "Press period key" },
    @{ Label = "Slash";      Hint = "Press slash key" },
    @{ Label = "Backslash";  Hint = "Press backslash key" },
    @{ Label = "Underscore"; Hint = "Press underscore key (if exists)" },
    @{ Label = "Yen";        Hint = "Press yen key (if exists)" },
    @{ Label = "Space";      Hint = "Press space key" }
)

Write-Host ""
Write-Host "=== Key Mapping Capture ==="
Write-Host "Press ESC any time to finish."
Write-Host "This tool records VK code and produced character."
Write-Host ""

$step = 1
foreach ($t in $targets) {
    Write-Host ("[{0}] {1}" -f $step, $t.Hint)
    $k = Read-KeyEvent
    if ($k.VirtualKeyCode -eq 27) { break }

    $vk = [int]$k.VirtualKeyCode
    $ch = [char]$k.Character
    $charCode = [int][char]$ch
    $line = "{0}`t{1}`t{2}`t0x{3}`t{4}`t{5}`t{6}" -f `
        $step, $t.Label, $vk, $vk.ToString("X2"), (Escape-Char $ch), $charCode, $k.ControlKeyState
    $rows.Add($line)
    $step++
}

if ($IncludeCtrlAlt) {
    Write-Host ""
    Write-Host "Entering extra capture mode for Ctrl/Alt combos. ESC to finish."
    while ($true) {
        Write-Host ("[{0}] Press any key combo" -f $step)
        $k = Read-KeyEvent
        if ($k.VirtualKeyCode -eq 27) { break }
        $vk = [int]$k.VirtualKeyCode
        $ch = [char]$k.Character
        $charCode = [int][char]$ch
        $line = "{0}`t{1}`t{2}`t0x{3}`t{4}`t{5}`t{6}" -f `
            $step, "Extra", $vk, $vk.ToString("X2"), (Escape-Char $ch), $charCode, $k.ControlKeyState
        $rows.Add($line)
        $step++
    }
}

$content = @()
$content += $header
$content += ""
$content += "Step`tLabel`tVK(dec)`tVK(hex)`tChar`tCharCode`tControlKeyState"
$content += $rows

$dir = Split-Path -Parent $OutputPath
if (-not [string]::IsNullOrWhiteSpace($dir) -and -not (Test-Path $dir)) {
    New-Item -ItemType Directory -Path $dir | Out-Null
}
$content | Out-File -FilePath $OutputPath -Encoding UTF8

Write-Host ""
Write-Host "Saved: $OutputPath"
Write-Host "Use this file to add missing key mappings."
