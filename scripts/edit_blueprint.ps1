<#
  edit_blueprint.ps1 — agent-callable atomic Blueprint editing.

  Runs the BPATEdit commandlet to apply a structured edit request to a Blueprint,
  with backup + plan + verify + diff + rollback. Non-destructive by default.

  Usage:
    .\edit_blueprint.ps1 -UERoot "<UE>" -ProjectUProject "<...>.uproject" `
        -AssetPath "/Game/BPParserTest/BP_04_ExecFlow_Control" `
        -EditRequestJson ".\edit_requests\request_001.json" `
        [-OutputDir "<dir>"] [-Mode plan-only|dry-run|apply|apply-and-verify] `
        [-CreateBackup] [-AllowDestructiveEdit] [-Strict] [-WorkOnCopy "/Game/Scratch/Copy"]

  Exit codes (from commandlet): 0 success, 10 partial, 20 failed, 30 bad input, 40 rolled_back.
  Primary output: <OutputDir>\<sanitized asset>\edits\<timestamp>\edit_result.json
#>
[CmdletBinding()]
param(
  [string] $UERoot = $env:UE_ROOT,
  [Parameter(Mandatory=$true)] [string] $ProjectUProject,
  [string] $AssetPath = "",
  [Parameter(Mandatory=$true)] [string] $EditRequestJson,
  [string] $OutputDir = "",
  [ValidateSet('plan-only','dry-run','apply','apply-and-verify')]
  [string] $Mode = 'plan-only',
  [switch] $CreateBackup,
  [switch] $AllowDestructiveEdit,
  [switch] $Strict,
  [string] $WorkOnCopy = ""
)
$ErrorActionPreference = 'Stop'
function Fail($m,$c){ Write-Error $m; exit $c }

if (-not (Test-Path $ProjectUProject)) { Fail "Project not found: $ProjectUProject" 30 }
if (-not (Test-Path $EditRequestJson)) { Fail "Edit request not found: $EditRequestJson" 30 }
$ProjectDir = Split-Path $ProjectUProject -Parent

# Validate the request JSON early (fail fast before launching UE). Read as UTF-8: PS 5.1 Get-Content uses
# the ANSI codepage and would corrupt UTF-8 (e.g. Chinese node titles/comments in operations) => false reject.
try { [System.IO.File]::ReadAllText($EditRequestJson, (New-Object System.Text.UTF8Encoding($false))) | ConvertFrom-Json | Out-Null }
catch { Fail "Edit request is not valid JSON/UTF-8: $_" 30 }

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

# Editor must be closed for apply modes (it locks assets / clobbers saves).
$applies = ($Mode -eq 'apply' -or $Mode -eq 'apply-and-verify')
if ($applies -and (Get-Process -Name UnrealEditor -ErrorAction SilentlyContinue)) {
  Fail "Unreal Editor is running. Close it before apply/apply-and-verify." 20
}

if ([string]::IsNullOrWhiteSpace($OutputDir)) { $OutputDir = Join-Path $ProjectDir 'Saved\BPParserAgentReports' }
New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null

$cb = if ($CreateBackup) { 1 } else { 0 }
$ad = if ($AllowDestructiveEdit) { 1 } else { 0 }
$st = if ($Strict) { 1 } else { 0 }

$args = @(
  "`"$ProjectUProject`"", "-run=BPATEdit",
  "-EditRequestJson=`"$EditRequestJson`"",
  "-OutputDir=`"$OutputDir`"",
  "-Mode=$Mode", "-CreateBackup=$cb", "-AllowDestructiveEdit=$ad", "-Strict=$st",
  "-unattended", "-nopause", "-nop4", "-stdout"
)
if (-not [string]::IsNullOrWhiteSpace($AssetPath)) { $args += "-AssetPath=$AssetPath" }
if (-not [string]::IsNullOrWhiteSpace($WorkOnCopy)) { $args += "-WorkOnCopy=$WorkOnCopy" }

Write-Host "BPATEdit ($Mode) $AssetPath ..." -ForegroundColor Cyan
& $cmd @args | Out-Null
$rc = $LASTEXITCODE

$statusName = switch ($rc) { 0 {'success'} 10 {'partial'} 20 {'failed'} 30 {'bad_input'} 40 {'rolled_back'} default {"exit_$rc"} }
$color = if ($rc -eq 0) { 'Green' } else { 'Yellow' }
Write-Host "BPATEdit -> $statusName (exit $rc). Reports under: $OutputDir" -ForegroundColor $color
exit $rc
