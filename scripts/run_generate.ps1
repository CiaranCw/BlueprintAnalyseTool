<#
  run_generate.ps1 — run the BPParserTestGen generator (headless commandlet).
  Produces /Game/BPParserTest assets and Saved/BPParserTestReports/generation_log.json.

  Alternatively, inside the editor: open it and run console command  BPParserTest.Generate
  (or menu Tools -> BP Parser Test -> Generate BP Parser Test Suite).

  Usage:
    .\run_generate.ps1 -UE_ROOT "C:\Program Files\Epic Games\UE_5.4" `
                       -PROJECT_UPROJECT "E:\BPTestProject\BPTest\BPTest.uproject"
#>
[CmdletBinding()]
param(
  [Alias('UE_ROOT')]          [string]$UERoot = $env:UE_ROOT,
  [Alias('PROJECT_UPROJECT')] [string]$ProjectUProject = $env:PROJECT_UPROJECT
)
$ErrorActionPreference = 'Stop'
function Fail($msg){ Write-Error $msg; exit 1 }

if ([string]::IsNullOrWhiteSpace($UERoot))         { Fail "UERoot not set." }
if ([string]::IsNullOrWhiteSpace($ProjectUProject)) { Fail "ProjectUProject not set." }
if (-not (Test-Path $ProjectUProject))             { Fail "Project not found: $ProjectUProject" }

$Cmd = Join-Path $UERoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
if (-not (Test-Path $Cmd)) { Fail "UnrealEditor-Cmd.exe not found: $Cmd" }

Write-Host "Running BPParserTestGen commandlet ..." -ForegroundColor Cyan
& $Cmd "$ProjectUProject" -run=BPParserTestGen -stdout -unattended -nopause
$code = $LASTEXITCODE

$Report = Join-Path (Split-Path $ProjectUProject -Parent) 'Saved\BPParserTestReports\generation_log.json'
if (Test-Path $Report) {
  Write-Host "generation_log.json -> $Report" -ForegroundColor Green
  Get-Content $Report -Raw | Write-Host
} else {
  Write-Warning "generation_log.json NOT found at $Report (generation may have failed)."
}

if ($code -ne 0) { Fail "Commandlet exit code $code (one or more assets failed to compile)." }
Write-Host "Generation finished (exit 0)." -ForegroundColor Green
exit 0
