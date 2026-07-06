<#
  create_blueprint.ps1 - spec-driven Blueprint creation (Create task).
  Runs the read/write BPCreate commandlet in the target project's UE.

  Usage:
    .\create_blueprint.ps1 -ProjectUProject "<...>.uproject" -SpecFile ".\create_spec.json" `
        [-UERoot "<engine>"] [-OutputDir "<dir>"]

  The editor MUST be closed (creation writes/saves an asset).
  Exit codes (from commandlet): 0 success, 10 partial, 20 failed, 30 bad input, 41 exists(refused).
#>
[CmdletBinding()]
param(
  [Parameter(Mandatory=$true)] [string] $ProjectUProject,
  [Parameter(Mandatory=$true)] [string] $SpecFile,
  [string] $UERoot = "",
  [string] $OutputDir = ""
)
$ErrorActionPreference='Stop'
function Fail($m,$c){ Write-Error $m; exit $c }
if (-not (Test-Path $ProjectUProject)) { Fail "Project not found: $ProjectUProject" 30 }
if (-not (Test-Path $SpecFile)) { Fail "Spec not found: $SpecFile" 30 }
# Read as UTF-8 (PS 5.1 Get-Content uses the ANSI codepage and corrupts UTF-8, e.g. Chinese in a create spec).
try { [System.IO.File]::ReadAllText($SpecFile, (New-Object System.Text.UTF8Encoding($false))) | ConvertFrom-Json | Out-Null } catch { Fail "Spec is not valid JSON/UTF-8: $_" 30 }
$ProjectDir = Split-Path $ProjectUProject -Parent

if ([string]::IsNullOrWhiteSpace($UERoot)) {
  $assoc = (Get-Content $ProjectUProject -Raw | ConvertFrom-Json).EngineAssociation
  if ($assoc -match '^\d+\.\d+$') {
    foreach ($d in 'C','D','E','F') { foreach ($b in @("$($d):\Program Files\Epic Games\UE_$assoc","$($d):\UE\UE_$assoc","$($d):\software\UE\UE_$assoc")) { if (Test-Path (Join-Path $b 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe')) { $UERoot=$b; break } }; if($UERoot){break} }
  } else {
    foreach ($rp in @("HKCU:\SOFTWARE\Epic Games\Unreal Engine\Builds","HKLM:\SOFTWARE\Epic Games\Unreal Engine\Builds")) { try { $v=(Get-ItemProperty $rp -EA Stop).$assoc; if($v -and (Test-Path $v)){ $UERoot=$v; break } } catch {} }
  }
}
$cmd = Join-Path $UERoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
if (-not (Test-Path $cmd)) { Fail "UnrealEditor-Cmd not found (pass -UERoot)." 30 }
if (Get-Process -Name UnrealEditor -EA SilentlyContinue) { Fail "Close the UE editor first (creation writes an asset)." 20 }
if ([string]::IsNullOrWhiteSpace($OutputDir)) { $OutputDir = Join-Path $ProjectDir 'Saved\BPParserAgentReports\create' }
New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null

Write-Host "BPCreate: $SpecFile ..." -ForegroundColor Cyan
& $cmd "$ProjectUProject" -run=BPCreate -SpecFile="$SpecFile" -OutputDir="$OutputDir" -unattended -nopause -nop4 -stdout | Out-Null
$rc=$LASTEXITCODE
$name = switch ($rc) { 0 {'success'} 10 {'partial'} 20 {'failed'} 30 {'bad_input'} 41 {'exists_refused'} default {"exit_$rc"} }
$col = if ($rc -eq 0) {'Green'} else {'Yellow'}
Write-Host "BPCreate -> $name (exit $rc). Reports under: $OutputDir" -ForegroundColor $col
exit $rc
