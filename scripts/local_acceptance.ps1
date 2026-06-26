<#
  local_acceptance.ps1 — one-stop, Agent-callable local acceptance for the
  BPParserTest blueprint test suite. Discovers UE, builds the plugin, runs the
  generator, validates outputs, and writes a machine-readable manifest.

  Usage:
    .\local_acceptance.ps1 -ProjectUProject "E:\BPTestProject\BPTest\BPTest.uproject"
    .\local_acceptance.ps1 -UERoot "D:\software\UE\UE_5.4" -ProjectUProject "...\BPTest.uproject" -OutputDir "..."

  Exit codes: 0 = success, 10 = partial (automation OK, manual UE check remains), 20 = failed.
  Primary output: <OutputDir>\acceptance_manifest.json
#>
[CmdletBinding()]
param(
  [string] $UERoot = "",
  [Parameter(Mandatory=$true)] [string] $ProjectUProject,
  [string] $OutputDir = "",
  [switch] $SkipBuild,
  [switch] $SkipGenerate,
  [switch] $SkipViz,
  [switch] $Strict
)
$ErrorActionPreference = 'Continue'

function Read-EngineAssociation([string]$uproject) {
  try { return (Get-Content $uproject -Raw | ConvertFrom-Json).EngineAssociation } catch { return "" }
}
function Find-UERoot([string]$Version) {
  if ($Version -match '^\d+\.\d+$') {
    foreach ($rp in @("HKLM:\SOFTWARE\EpicGames\Unreal Engine\$Version","HKLM:\SOFTWARE\WOW6432Node\EpicGames\Unreal Engine\$Version")) {
      try { $p = (Get-ItemProperty -Path $rp -ErrorAction Stop).InstalledDirectory; if ($p -and (Test-Path $p)) { return $p } } catch {}
    }
    foreach ($d in 'C','D','E','F') {
      foreach ($base in @("$($d):\Program Files\Epic Games\UE_$Version","$($d):\Epic Games\UE_$Version","$($d):\UE\UE_$Version","$($d):\software\UE\UE_$Version")) {
        if (Test-Path (Join-Path $base 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe')) { return $base }
      }
    }
  } else {
    # GUID association (source/custom build) -> Builds registry
    foreach ($rp in @("HKLM:\SOFTWARE\Epic Games\Unreal Engine\Builds","HKCU:\SOFTWARE\Epic Games\Unreal Engine\Builds")) {
      try { $b = Get-ItemProperty -Path $rp -ErrorAction Stop; $v = $b.$Version; if ($v -and (Test-Path $v)) { return $v } } catch {}
    }
  }
  return ""
}

if (-not (Test-Path $ProjectUProject)) { Write-Error "Project not found: $ProjectUProject"; exit 20 }
$ProjectDir = Split-Path $ProjectUProject -Parent
$ProjName = [IO.Path]::GetFileNameWithoutExtension($ProjectUProject)
$engineAssoc = Read-EngineAssociation $ProjectUProject

if ([string]::IsNullOrWhiteSpace($UERoot)) { $UERoot = Find-UERoot $engineAssoc }
$cmdExe = if ($UERoot) { Join-Path $UERoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe' } else { "" }
if ([string]::IsNullOrWhiteSpace($OutputDir)) { $OutputDir = Join-Path $ProjectDir 'Saved\BPParserTestReports' }
New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null

$steps = New-Object System.Collections.ArrayList
function Add-Step($name,$status,$log,$errors,$warnings) {
  [void]$steps.Add([ordered]@{ name=$name; status=$status; log=$log; errors=@($errors); warnings=@($warnings) })
}

Write-Host "== local_acceptance ==" -ForegroundColor Cyan
Write-Host "Project       : $ProjectUProject"
Write-Host "EngineAssoc   : $engineAssoc"
Write-Host "UE_ROOT       : $(if($UERoot){$UERoot}else{'<not found>'})"
Write-Host "OutputDir     : $OutputDir"

$ueOk = ($cmdExe -and (Test-Path $cmdExe))
if (-not $ueOk) { Add-Step 'discover_ue' 'failed' '' @("UnrealEditor-Cmd.exe not found. Pass -UERoot explicitly (EngineAssociation='$engineAssoc').") @() }
else { Add-Step 'discover_ue' 'success' '' @() @() }

# --- ensure code project (Blueprint-only projects need a minimal C++ module to compile plugins) ---
if (-not $SkipBuild -and $ueOk) {
  $ecpLog = Join-Path $OutputDir 'ensure_code_project_log.txt'
  & (Join-Path $PSScriptRoot 'ensure_code_project.ps1') -ProjectUProject $ProjectUProject -PluginNames 'BPParserTestGen' *> $ecpLog
  Add-Step 'ensure_code_project' ($(if($LASTEXITCODE -eq 0){'success'}else{'failed'})) $ecpLog @() @()
}

# --- build ---
$buildExit = 0
$buildLog = Join-Path $OutputDir 'build_log.txt'
if ($SkipBuild) { Add-Step 'build' 'skipped' $buildLog @() @() }
elseif (-not $ueOk) { Add-Step 'build' 'failed' $buildLog @('UE not found') @(); $buildExit = 1 }
else {
  & (Join-Path $PSScriptRoot 'build_plugin.ps1') -UERoot $UERoot -ProjectUProject $ProjectUProject *> $buildLog
  $buildExit = $LASTEXITCODE
  Add-Step 'build' ($(if($buildExit -eq 0){'success'}else{'failed'})) $buildLog @($(if($buildExit -ne 0){"build exit $buildExit; see build_log.txt"})) @()
}

# --- generate ---
$genLog = Join-Path $OutputDir 'generation_log.json'
$genRunLog = Join-Path $OutputDir 'generation_run_log.txt'
if ($SkipGenerate) { Add-Step 'generate' 'skipped' $genRunLog @() @() }
elseif ($buildExit -ne 0) { Add-Step 'generate' 'skipped' $genRunLog @('skipped because build failed') @() }
elseif (-not $ueOk) { Add-Step 'generate' 'failed' $genRunLog @('UE not found') @() }
else {
  & (Join-Path $PSScriptRoot 'run_generate.ps1') -UERoot $UERoot -ProjectUProject $ProjectUProject *> $genRunLog
  $gOk = Test-Path $genLog
  Add-Step 'generate' ($(if($gOk){'success'}else{'failed'})) $genRunLog @($(if(-not $gOk){'generation_log.json not produced'})) @()
}

# --- validate ---
& (Join-Path $PSScriptRoot 'validate_outputs.ps1') -OutputDir $OutputDir -GenerationLog $genLog *> (Join-Path $OutputDir 'validate_log.txt')
$valExit = $LASTEXITCODE
Add-Step 'validate' ($(if($valExit -eq 0){'success'}else{'failed'})) (Join-Path $OutputDir 'validate_log.txt') @($(if($valExit -ne 0){'hard validation failures; see failed_items.json'})) @()

# --- assemble assets[] from generation_log ---
$assets = @(); $genFailed = 0; $genTotal = 0
if (Test-Path $genLog) {
  try {
    $gl = Get-Content $genLog -Raw | ConvertFrom-Json
    $genTotal = $gl.total_assets
    foreach ($a in $gl.assets) {
      if ($a.compile_status -eq 'error') { $genFailed++ }
      $assets += [ordered]@{
        name=($a.asset_path -replace '.*/',''); path=$a.asset_path; type=$a.asset_type
        generated=$a.created; compiled=($a.compile_status -in 'up_to_date','warnings','n/a')
        compile_status=$a.compile_status; warnings=@(); errors=@($(if($a.compile_status -eq 'error'){'compile error'})); manual_check_required=@()
      }
    }
  } catch {}
}

$manualCount = 0
$mcrPath = Join-Path $OutputDir 'manual_check_required.json'
if (Test-Path $mcrPath) { try { $manualCount = (Get-Content $mcrPath -Raw | ConvertFrom-Json).count } catch {} }

# --- overall status ---
$status = 'success'
if (($buildExit -ne 0 -and -not $SkipBuild) -or (-not $ueOk) -or (-not (Test-Path $genLog) -and -not $SkipGenerate) -or ($genFailed -gt 0) -or ($valExit -ne 0)) {
  $status = 'failed'
} elseif ($manualCount -gt 0 -or $SkipBuild -or $SkipGenerate) {
  $status = 'partial'
}

$manifest = [ordered]@{
  schema_version='1.0'; status=$status; timestamp=(Get-Date).ToUniversalTime().ToString('o')
  ue_version=$engineAssoc; ue_root=$UERoot; project_uproject=$ProjectUProject; project=$ProjName
  plugin_loaded=($ueOk -and $buildExit -eq 0)
  build_success=($buildExit -eq 0 -or [bool]$SkipBuild)
  generation_success=((Test-Path $genLog) -and $genFailed -eq 0)
  generated_assets=@($assets | Where-Object { $_.generated } | ForEach-Object { $_.path })
  failed_assets=@($assets | Where-Object { -not $_.compiled } | ForEach-Object { $_.path })
  total_assets=$genTotal; failed_count=$genFailed; manual_check_count=$manualCount
  steps=$steps; assets=$assets
  artifacts=[ordered]@{
    generation_log=$genLog; build_log=$buildLog
    coverage_summary=(Join-Path $OutputDir 'coverage_summary.json')
    failed_items=(Join-Path $OutputDir 'failed_items.json')
    manual_check_required=$mcrPath
    expected_ir_dir=(Resolve-Path (Join-Path $PSScriptRoot '..\bpparser_testgen\deliverables\expected_ir') -ErrorAction SilentlyContinue).Path
    viz_dir=(Resolve-Path (Join-Path $PSScriptRoot '..\bpparser_testgen\deliverables\viz') -ErrorAction SilentlyContinue).Path
    coverage_matrix=(Resolve-Path (Join-Path $PSScriptRoot '..\bpparser_testgen\deliverables\coverage_matrix.md') -ErrorAction SilentlyContinue).Path
  }
}
$manifestPath = Join-Path $OutputDir 'acceptance_manifest.json'
($manifest | ConvertTo-Json -Depth 8) | Out-File $manifestPath -Encoding utf8

Write-Host ""
Write-Host "STATUS=$status  total=$genTotal failed=$genFailed manual=$manualCount" -ForegroundColor ($(if($status -eq 'failed'){'Red'}elseif($status -eq 'partial'){'Yellow'}else{'Green'}))
Write-Host "Manifest: $manifestPath"

switch ($status) { 'success' { exit 0 } 'partial' { exit 10 } default { exit 20 } }
