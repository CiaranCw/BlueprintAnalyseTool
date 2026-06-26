<#
  export_ir.ps1 — export one blueprint's IR to JSON via the BPParserTestDump
  commandlet (real parser implemented in the BPParserTestGen plugin).

  Usage:
    .\export_ir.ps1 -UERoot "<UE>" -ProjectUProject "<...>.uproject" `
                    -AssetPath "/Game/BPParserTest/BP_01_PrimitivePins_Basic" `
                    [-OutputDir "<dir>"]

  Output: <OutputDir>\<AssetName>.ir.json   (default OutputDir: <Project>\Saved\BPParserTestReports\ir_dumps)
  Exit codes: 0 ok, 30 bad args, 50 dump failed.
#>
[CmdletBinding()]
param(
  [string] $UERoot = $env:UE_ROOT,
  [Parameter(Mandatory=$true)] [string] $ProjectUProject,
  [Parameter(Mandatory=$true)] [string] $AssetPath,
  [string] $OutputDir = ""
)
$ErrorActionPreference = 'Stop'
function Fail($m,$c){ Write-Error $m; exit $c }

if (-not (Test-Path $ProjectUProject)) { Fail "Project not found: $ProjectUProject" 30 }
$ProjectDir = Split-Path $ProjectUProject -Parent

if ([string]::IsNullOrWhiteSpace($UERoot)) {
  $ver = (Get-Content $ProjectUProject -Raw | ConvertFrom-Json).EngineAssociation
  foreach ($d in 'C','D','E','F') {
    foreach ($base in @("$($d):\Program Files\Epic Games\UE_$ver","$($d):\UE\UE_$ver","$($d):\software\UE\UE_$ver")) {
      if (Test-Path (Join-Path $base 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe')) { $UERoot = $base; break }
    }
    if ($UERoot) { break }
  }
}
$cmd = Join-Path $UERoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
if (-not (Test-Path $cmd)) { Fail "UnrealEditor-Cmd.exe not found (pass -UERoot)." 30 }

if ([string]::IsNullOrWhiteSpace($OutputDir)) { $OutputDir = Join-Path $ProjectDir 'Saved\BPParserTestReports\ir_dumps' }
New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null

Write-Host "Exporting IR for $AssetPath ..." -ForegroundColor Cyan
& $cmd "$ProjectUProject" -run=BPParserTestDump -AssetPath="$AssetPath" -OutputDir="$OutputDir" -unattended -nopause -stdout | Out-Null
$rc = $LASTEXITCODE

$short = ($AssetPath -replace '.*/','')
$outFile = Join-Path $OutputDir "$short.ir.json"
if ((Test-Path $outFile)) {
  Write-Host "IR written: $outFile" -ForegroundColor Green
  exit 0
}
Fail "IR dump not found at $outFile (commandlet exit $rc)." 50
