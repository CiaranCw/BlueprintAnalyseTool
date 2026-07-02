<#
  warmup_project.ps1 - ONE-TIME per-project setup that enables native_full (full understanding + edit
  + create). Installs the read-only analysis plugin (source form) into the target project and
  incrementally builds the project's Editor target against its engine, then reports capability state.

  This is the "warmup" step: after it succeeds, the project's AI can call analyze/edit/create in
  native_full repeatedly (no re-warmup unless the engine or plugin version changes).

  Invasive (adds a plugin + triggers a build) -> run only with user consent. Blueprint assets are
  never modified. Editor must be closed.

  Usage:
    .\warmup_project.ps1 -ProjectUProject "<...>.uproject" [-UERoot "<engine>"] [-PluginSource "<repo>\...\BPParserTestGen"] [-SmokeAssetPath "/Game/..."]

  Exit codes: 0 success (native_full ready), 20 failed (install/build), 30 bad input.
#>
[CmdletBinding()]
param(
  [Parameter(Mandatory=$true)] [string] $ProjectUProject,
  [string] $UERoot = "",
  [string] $PluginSource = "",
  [string] $SmokeAssetPath = ""
)
$ErrorActionPreference='Continue'
$scripts = $PSScriptRoot
$RepoRoot = Split-Path $scripts -Parent
if (-not (Test-Path $ProjectUProject)) { Write-Error "Project not found: $ProjectUProject"; exit 30 }
# NOTE: do NOT force a default PluginSource here (the dev-repo path is wrong in the distributed
# Tools\BlueprintAgent layout). Leave it empty and let install_project_plugin.ps1 resolve the correct
# source for whichever layout we're in (single source of truth).
$ProjectDir = Split-Path $ProjectUProject -Parent
$OutDir = Join-Path $ProjectDir 'Saved\BPParserAgentReports\warmup'
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

# resolve UERoot (reuse agent_status's logic by calling it after; here do a compact resolve for build)
if ([string]::IsNullOrWhiteSpace($UERoot)) {
  $assoc = (Get-Content $ProjectUProject -Raw | ConvertFrom-Json).EngineAssociation
  if ($assoc -match '^\d+\.\d+$') { foreach ($d in 'C','D','E','F') { foreach ($b in @("$($d):\Program Files\Epic Games\UE_$assoc","$($d):\UE\UE_$assoc","$($d):\software\UE\UE_$assoc")) { if (Test-Path (Join-Path $b 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe')) { $UERoot=$b; break } } ; if($UERoot){break} } }
  else { foreach ($rp in @("HKCU:\SOFTWARE\Epic Games\Unreal Engine\Builds","HKLM:\SOFTWARE\Epic Games\Unreal Engine\Builds")) { try { $v=(Get-ItemProperty $rp -EA Stop).$assoc; if($v -and (Test-Path $v)){$UERoot=$v;break} } catch {} } }
}
if (-not $UERoot -or -not (Test-Path (Join-Path $UERoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'))) { Write-Error "UERoot not resolved/invalid (pass -UERoot)."; exit 30 }
if (Get-Process -Name UnrealEditor -EA SilentlyContinue) { Write-Error "Close the UE editor first (warmup builds the editor target)."; exit 20 }

$steps = New-Object System.Collections.ArrayList
function Step($n,$ok,$detail){ [void]$steps.Add([ordered]@{ step=$n; status=$(if($ok){'success'}else{'failed'}); detail=$detail }) }

Write-Host "== warmup_project == $ProjectUProject (engine: $UERoot)" -ForegroundColor Cyan

# 1) install (source) + enable in .uproject. Pass -PluginSource only if the caller set it.
$instArgs = @{ ProjectUProject=$ProjectUProject }; if (-not [string]::IsNullOrWhiteSpace($PluginSource)) { $instArgs.PluginSource=$PluginSource }
& (Join-Path $scripts 'install_project_plugin.ps1') @instArgs *>&1 | Out-File -Encoding utf8 (Join-Path $OutDir 'install_log.txt')
$instOk = ($LASTEXITCODE -eq 0); Step 'install_plugin' $instOk "see install_log.txt"
if (-not $instOk) { Write-Error "install failed"; & (Join-Path $scripts 'agent_status.ps1') -ProjectUProject $ProjectUProject -UERoot $UERoot -OutputDir $OutDir | Out-Null; exit 20 }

# 2) build editor target (incremental) + assert the plugin module DLL exists (no false-OK).
& (Join-Path $scripts 'build_project_plugin.ps1') -UERoot $UERoot -ProjectUProject $ProjectUProject -PluginName 'BPParserTestGen' *>&1 | Out-File -Encoding utf8 (Join-Path $OutDir 'build_log.txt')
$buildOk = ($LASTEXITCODE -eq 0); Step 'build_editor' $buildOk "see build_log.txt"
if (-not $buildOk) { Write-Error "build failed (see build_log.txt)"; & (Join-Path $scripts 'agent_status.ps1') -ProjectUProject $ProjectUProject -UERoot $UERoot -OutputDir $OutDir | Out-Null; exit 20 }

# 3) optional smoke: real native dump of a known asset
$smoke = 'skipped'
if ($SmokeAssetPath) {
  & (Join-Path $scripts 'analyze_blueprint.ps1') -UERoot $UERoot -ProjectUProject $ProjectUProject -AssetPath $SmokeAssetPath -OutputDir (Join-Path $OutDir 'smoke') -Mode native-full *>&1 | Out-File -Encoding utf8 (Join-Path $OutDir 'smoke_log.txt')
  $smoke = if ($LASTEXITCODE -eq 0) { 'success' } else { 'failed' }
  Step 'smoke_native_dump' ($smoke -eq 'success') "asset=$SmokeAssetPath; see smoke_log.txt"
}

# 4) capability state after warmup
& (Join-Path $scripts 'agent_status.ps1') -ProjectUProject $ProjectUProject -UERoot $UERoot -OutputDir $OutDir | Out-Null
$state = $null; try { $state = Get-Content (Join-Path $OutDir 'capability_state.json') -Raw | ConvertFrom-Json } catch {}
$nativeReady = [bool]($state -and $state.capabilities.understand_full)

$warm = [ordered]@{
  schema_version='1.0'; status=$(if($nativeReady){'success'}else{'partial'})
  project_uproject=$ProjectUProject; ue_root=$UERoot
  steps=@($steps); smoke=$smoke; native_full_ready=$nativeReady
  capability_state='capability_state.json'
  generated_at=(Get-Date).ToUniversalTime().ToString('o')
}
[IO.File]::WriteAllText((Join-Path $OutDir 'warmup_state.json'), ($warm | ConvertTo-Json -Depth 8), (New-Object System.Text.UTF8Encoding($false)))  # no-BOM UTF-8
$col = if ($nativeReady) {'Green'} else {'Yellow'}
Write-Host "warmup: native_full_ready=$nativeReady -> $(Join-Path $OutDir 'warmup_state.json')" -ForegroundColor $col
if ($nativeReady) { exit 0 } else { exit 20 }
