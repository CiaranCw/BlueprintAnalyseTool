<#
  dump_ir_sample.ps1 — export one blueprint's IR via the read-only BlueprintAgentTools
  (BPATDump) commandlet, for a quick regression sanity check.

  Prereq: BlueprintAgentTools plugin present + enabled in the project, and the
  /Game/BPParserTest assets already generated.

  Usage:
    .\dump_ir_sample.ps1 -UE_ROOT "C:\Program Files\Epic Games\UE_5.4" `
                         -PROJECT_UPROJECT "E:\BPTestProject\BPTest\BPTest.uproject" `
                         -OUTPUT_DIR "E:\bp_out\BPTest" `
                         [-ASSET "/Game/BPParserTest/BP_01_PrimitivePins_Basic"]
#>
[CmdletBinding()]
param(
  [string]$UE_ROOT = $env:UE_ROOT,
  [string]$PROJECT_UPROJECT = $env:PROJECT_UPROJECT,
  [string]$OUTPUT_DIR = $env:OUTPUT_DIR,
  [string]$ASSET = "/Game/BPParserTest/BP_01_PrimitivePins_Basic"
)
$ErrorActionPreference = 'Stop'
function Fail($msg){ Write-Error $msg; exit 1 }

if ([string]::IsNullOrWhiteSpace($UE_ROOT))          { Fail "UE_ROOT not set." }
if ([string]::IsNullOrWhiteSpace($PROJECT_UPROJECT)) { Fail "PROJECT_UPROJECT not set." }
if ([string]::IsNullOrWhiteSpace($OUTPUT_DIR))       { Fail "OUTPUT_DIR not set (must be OUTSIDE the project)." }
if (-not (Test-Path $PROJECT_UPROJECT))              { Fail "Project not found: $PROJECT_UPROJECT" }

$ProjDir = Split-Path $PROJECT_UPROJECT -Parent
if ($OUTPUT_DIR -like "$ProjDir*") { Fail "OUTPUT_DIR must be OUTSIDE the project directory (read-only safety)." }

$Cmd = Join-Path $UE_ROOT 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
if (-not (Test-Path $Cmd)) { Fail "UnrealEditor-Cmd.exe not found: $Cmd" }

New-Item -ItemType Directory -Force -Path $OUTPUT_DIR | Out-Null
Write-Host "Dumping IR for $ASSET ..." -ForegroundColor Cyan
& $Cmd "$PROJECT_UPROJECT" -run=BPATDump -AssetPath="$ASSET" -OutputDir="$OUTPUT_DIR" -StrictReadOnly=1 -stdout -unattended -nopause
$code = $LASTEXITCODE
if ($code -ne 0 -and $code -ne 10) { Fail "BPATDump exit $code (see docs/architecture.md exit-code table)." }

Write-Host "Look for manifest.json + graphs/ under: $OUTPUT_DIR" -ForegroundColor Green
exit 0
