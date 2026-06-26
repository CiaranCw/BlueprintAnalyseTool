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
  [Alias('UE_ROOT')]          [string]$UERoot = $env:UE_ROOT,
  [Alias('PROJECT_UPROJECT')] [string]$ProjectUProject = $env:PROJECT_UPROJECT
)
$ErrorActionPreference = 'Stop'

function Fail($msg){ Write-Error $msg; exit 1 }

if ([string]::IsNullOrWhiteSpace($UERoot))         { Fail "UERoot not set. Pass -UERoot or set `$env:UE_ROOT (e.g. C:\Program Files\Epic Games\UE_5.4)." }
if ([string]::IsNullOrWhiteSpace($ProjectUProject)) { Fail "ProjectUProject not set. Pass -ProjectUProject (full path to .uproject)." }
if (-not (Test-Path $ProjectUProject))             { Fail "Project not found: $ProjectUProject" }

$BuildBat = Join-Path $UERoot 'Engine\Build\BatchFiles\Build.bat'
if (-not (Test-Path $BuildBat)) { Fail "Build.bat not found under UERoot: $BuildBat" }

$ProjName = [IO.Path]::GetFileNameWithoutExtension($ProjectUProject)
$Target   = "${ProjName}Editor"
$LogDir   = Join-Path (Split-Path $ProjectUProject -Parent) 'Saved\Logs'
New-Item -ItemType Directory -Force -Path $LogDir | Out-Null

Write-Host "Building $Target (Win64 Development) ..." -ForegroundColor Cyan
& $BuildBat $Target Win64 Development -Project="$ProjectUProject" -WaitMutex -FromMsBuild
$code = $LASTEXITCODE
if ($code -ne 0) { Fail "Build FAILED (exit $code). See logs in: $LogDir" }

Write-Host "Build OK. Logs: $LogDir" -ForegroundColor Green
exit 0
