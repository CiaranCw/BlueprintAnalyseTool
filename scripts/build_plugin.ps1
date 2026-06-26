<#
  build_plugin.ps1 — generate project files + build the Editor target so the
  BPParserTestGen (and BlueprintAgentTools) plugins compile.

  Prereqs: copy bpparser_testgen/Plugins/BPParserTestGen (and optionally
  ue_plugin/BlueprintAgentTools) into <YourProject>/Plugins/ first.

  Usage:
    .\build_plugin.ps1 -UE_ROOT "C:\Program Files\Epic Games\UE_5.4" `
                       -PROJECT_UPROJECT "E:\BPTestProject\BPTest\BPTest.uproject"
#>
[CmdletBinding()]
param(
  [string]$UE_ROOT = $env:UE_ROOT,
  [string]$PROJECT_UPROJECT = $env:PROJECT_UPROJECT
)
$ErrorActionPreference = 'Stop'

function Fail($msg){ Write-Error $msg; exit 1 }

if ([string]::IsNullOrWhiteSpace($UE_ROOT))         { Fail "UE_ROOT not set. Pass -UE_ROOT or set `$env:UE_ROOT (e.g. C:\Program Files\Epic Games\UE_5.4)." }
if ([string]::IsNullOrWhiteSpace($PROJECT_UPROJECT)) { Fail "PROJECT_UPROJECT not set. Pass -PROJECT_UPROJECT (full path to .uproject)." }
if (-not (Test-Path $PROJECT_UPROJECT))             { Fail "Project not found: $PROJECT_UPROJECT" }

$BuildBat = Join-Path $UE_ROOT 'Engine\Build\BatchFiles\Build.bat'
if (-not (Test-Path $BuildBat)) { Fail "Build.bat not found under UE_ROOT: $BuildBat" }

$ProjName = [IO.Path]::GetFileNameWithoutExtension($PROJECT_UPROJECT)
$Target   = "${ProjName}Editor"
$LogDir   = Join-Path (Split-Path $PROJECT_UPROJECT -Parent) 'Saved\Logs'
New-Item -ItemType Directory -Force -Path $LogDir | Out-Null

Write-Host "Building $Target (Win64 Development) ..." -ForegroundColor Cyan
& $BuildBat $Target Win64 Development -Project="$PROJECT_UPROJECT" -WaitMutex -FromMsBuild
$code = $LASTEXITCODE
if ($code -ne 0) { Fail "Build FAILED (exit $code). See logs in: $LogDir" }

Write-Host "Build OK. Logs: $LogDir" -ForegroundColor Green
exit 0
