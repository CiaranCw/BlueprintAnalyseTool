<#
  blueprint_agent.ps1 - single, agent-callable entry point for the Blueprint Agent.

  Other AIs provide ONE request.json (task_type = status|warmup|analyze|edit|create|validate) OR pass
  -Task/-AssetPaths directly, and read one dispatch manifest. Routes to the specialized tools; nothing
  is hardcoded to a project/engine.

  Execution modes (execution.mode / -Mode): auto | editor_live | native_full | python_partial | offline_asset_scan
    - editor_live : reuse an ALREADY-OPEN UE editor via the in-editor BPAgentLiveService (file queue).
                    No new UnrealEditor-Cmd is launched.
    - native_full : launch UnrealEditor-Cmd and run a commandlet (cold start; CI/batch/editor-closed).
    - auto        : probe editor_live first; if unavailable, fall back native_full -> python_partial -> offline.

  Usage:
    .\blueprint_agent.ps1 -RequestJson ".\request.json" [-Mode auto] [-PreferEditorLive] [-TimeoutSeconds 60]
    .\blueprint_agent.ps1 -Task status  -Mode editor_live -ProjectUProject "<...>.uproject" [-TimeoutSeconds 20]
    .\blueprint_agent.ps1 -Task analyze -Mode auto -PreferEditorLive -ProjectUProject "<...>.uproject" -AssetPaths "/Game/UI/WBP_X"

  Exit codes: 0 success, 10 partial, 20 failed, 24 editor_live unavailable (explicit mode), 30 bad input,
              40 rolled_back, 41 exists_refused.
  Output: <output_dir>/<task_type>/dispatch_manifest.json  (+ specialized artifacts under sub_output_dir)
#>
[CmdletBinding()]
param(
  [string] $RequestJson = "",
  [string] $OutputDir = "",
  [string] $UERoot = "",
  [string] $ProjectUProject = "",
  [ValidateSet('','status','warmup','analyze','edit','create','validate')] [string] $Task = "",
  [ValidateSet('','auto','editor_live','native_full','python_partial','offline_asset_scan')] [string] $Mode = "",
  [string[]] $AssetPaths = @(),
  [switch] $PreferEditorLive,
  [int] $TimeoutSeconds = 30
)
$ErrorActionPreference='Stop'
function Fail($m,$c){ Write-Error $m; exit $c }
function San([string]$s){ return ($s -replace '[/\\.:]','_').Trim('_') }

# ---- resolve request (from file OR synthesized from -Task/-AssetPaths) ------------------
if ($RequestJson) {
  if (-not (Test-Path $RequestJson)) { Fail "Request not found: $RequestJson" 30 }
  try { $req = Get-Content $RequestJson -Raw | ConvertFrom-Json } catch { Fail "request.json is not valid JSON: $_" 30 }
} else {
  if (-not $Task) { Fail "Provide -RequestJson OR -Task." 30 }
  if (-not $ProjectUProject) { Fail "Provide -ProjectUProject when using -Task without -RequestJson." 30 }
  $req = [pscustomobject]@{
    schema_version='1.0'; task_type=$Task
    project=[pscustomobject]@{ uproject=$ProjectUProject; ue_root=$UERoot; output_dir=$OutputDir; engine_policy=[pscustomobject]@{} }
    execution=[pscustomobject]@{ mode=$(if($Mode){$Mode}else{'auto'}) }
    request=[pscustomobject]@{ asset_paths=@($AssetPaths) }
  }
}

$taskType = if ($Task) { $Task.ToLower() } else { "$($req.task_type)".ToLower() }
if ($taskType -notin @('status','warmup','analyze','edit','create','validate')) { Fail "task_type must be status|warmup|analyze|edit|create|validate (got '$taskType')" 30 }

$proj = if ($ProjectUProject) { $ProjectUProject } else { "$($req.project.uproject)" }
$ue   = if ($UERoot) { $UERoot } else { "$($req.project.ue_root)" }
$outBase = if ($OutputDir) { $OutputDir } elseif ($req.project.output_dir) { "$($req.project.output_dir)" } else { Join-Path (Split-Path $proj -Parent) 'Saved\BPParserAgentReports' }
if (-not $proj) { Fail "project.uproject missing" 30 }
$mode = if ($Mode) { $Mode } elseif ($req.execution.mode) { "$($req.execution.mode)" } else { 'auto' }
$pol  = $req.project.engine_policy
$exec = $req.execution

Write-Host "== blueprint_agent ==" -ForegroundColor Cyan
Write-Host "task_type=$taskType  mode=$mode  prefer_live=$([bool]$PreferEditorLive)  project=$proj"

$scripts = $PSScriptRoot
$rc = 0; $subOut = ""; $reqId = ""; $usedMode = $mode
$fallbackFrom = $null; $fallbackTo = $null; $liveAvailable = $null

# ---- editor_live front-door -------------------------------------------------------------
# Applies to status/analyze/edit/create. status probes live only when explicitly requested.
$liveEligibleTask = ($taskType -in @('analyze','edit','create')) -or ($taskType -eq 'status')
$tryLive = $false
if ($liveEligibleTask) {
  if ($taskType -eq 'status') { $tryLive = ($mode -eq 'editor_live') -or $PreferEditorLive }
  else { $tryLive = ($mode -in @('editor_live','auto')) -or $PreferEditorLive }
}

function Invoke-EditorLive {
  param([string]$LiveTask,[string]$PayloadJsonPath="")
  $a = @{ ProjectUProject=$proj; Task=$LiveTask; OutputDir=$outBase; TimeoutSeconds=$TimeoutSeconds }
  if ($PayloadJsonPath) { $a.RequestJson=$PayloadJsonPath }
  elseif ($LiveTask -eq 'analyze') { $a.AssetPaths=@($req.request.asset_paths) }
  return (& (Join-Path $scripts 'editor_live_client.ps1') @a)
}

if ($tryLive) {
  $payload = ""
  switch ($taskType) {
    'edit' {
      $er = $req.request
      $p = [ordered]@{ schema_version='1.0'; task_type='edit'; mode='editor_live'; asset_path="$($er.asset_path)"; edit=$er
        execution=[ordered]@{ read_only=$false; allow_edit=$true
          create_backup=($exec.create_backup -ne $false)
          allow_destructive_edit=[bool]($er.allow_destructive_edit -or $exec.allow_destructive_edit)
          strict=[bool]$exec.strict
          require_user_ack=[bool]$exec.require_user_ack
          allow_edit_during_pie=[bool]$exec.allow_edit_during_pie }
        output_dir=($outBase -replace '\\','/') }
      $payload = Join-Path $env:TEMP ("bpagent_live_edit_" + (San "$($er.asset_path)") + ".json")
      ($p | ConvertTo-Json -Depth 40) | Set-Content -Encoding UTF8 $payload
    }
    'create' {
      $cr = $req.request
      $p = [ordered]@{ schema_version='1.0'; task_type='create'; mode='editor_live'; create=$cr
        execution=[ordered]@{ allow_create=$true }
        output_dir=($outBase -replace '\\','/') }
      $payload = Join-Path $env:TEMP ("bpagent_live_create_" + (San "$($cr.asset.asset_path)") + ".json")
      ($p | ConvertTo-Json -Depth 40) | Set-Content -Encoding UTF8 $payload
    }
  }
  $live = Invoke-EditorLive -LiveTask $taskType -PayloadJsonPath $payload
  $liveAvailable = [bool]$live.available
  if ($live.available) {
    $usedMode='editor_live'; $rc=[int]$live.exit_code; $subOut=$live.report_dir; $reqId=$live.request_id
  }
  elseif ($mode -eq 'editor_live') {
    # explicit editor_live but no service answered: DO NOT launch UnrealEditor-Cmd. Report unavailable.
    $usedMode='editor_live'; $rc=24; $subOut=(Join-Path (Join-Path $outBase 'editor_live') (San $live.request_id))
    Write-Host "[editor_live] unavailable and mode is explicit; not falling back to native_full." -ForegroundColor Yellow
  }
  else {
    $fallbackFrom='editor_live'; Write-Host "[auto] editor_live unavailable -> falling back to native_full/python/offline." -ForegroundColor Yellow
  }
}

# ---- native routing (skipped if editor_live already handled the task, or explicit-live-unavailable) --
$handledByLive = ($usedMode -eq 'editor_live')
if (-not $handledByLive) {
  switch ($taskType) {
    'status' {
      $taskOut = Join-Path $outBase 'status'
      $args = @{ ProjectUProject=$proj; OutputDir=$taskOut }
      if ($ue) { $args.UERoot=$ue }
      & (Join-Path $scripts 'agent_status.ps1') @args
      $rc=$LASTEXITCODE; $subOut=$taskOut; $usedMode='offline_asset_scan'
    }
    'warmup' {
      if (-not ($pol.allow_incremental_compile)) { Fail "warmup requires project.engine_policy.allow_incremental_compile=true (adds a plugin + builds the editor). Denied." 30 }
      $args = @{ ProjectUProject=$proj }
      if ($ue) { $args.UERoot=$ue }
      if ($req.project.plugin_source) { $args.PluginSource="$($req.project.plugin_source)" }
      if ($req.request.smoke_asset_path) { $args.SmokeAssetPath="$($req.request.smoke_asset_path)" }
      & (Join-Path $scripts 'warmup_project.ps1') @args
      $rc=$LASTEXITCODE; $subOut=(Join-Path (Split-Path $proj -Parent) 'Saved\BPParserAgentReports\warmup'); $usedMode='native_full'
    }
    'analyze' {
      $assets = @($req.request.asset_paths); if (-not $assets -or $assets.Count -eq 0) { Fail "analyze: request.asset_paths is empty" 30 }
      $amode = switch ($mode) { 'native_full'{'native-full'} 'python_partial'{'python-partial'} 'offline_asset_scan'{'offline'} default {'auto'} }
      $taskOut = Join-Path $outBase 'analyze'
      $args = @{ ProjectUProject=$proj; AssetPath=$assets[0]; OutputDir=$taskOut; Mode=$amode }
      if ($ue) { $args.UERoot=$ue }
      if ($pol.allow_project_plugin_install) { $args.AllowPluginInstall=$true }
      if ($pol.allow_incremental_compile) { $args.AllowBuild=$true }
      if ($exec.strict) { $args.Strict=$true }
      & (Join-Path $scripts 'analyze_blueprint.ps1') @args
      $rc=$LASTEXITCODE; $reqId=San($assets[0]); $subOut=Join-Path $taskOut $reqId
      if ($usedMode -eq 'editor_live') {} else { $usedMode='native_full'; if($fallbackFrom){$fallbackTo='native_full'} }
      if ($assets.Count -gt 1) { Write-Host "[note] batch: only first asset dispatched in native path; loop remaining as needed." -ForegroundColor Yellow }
    }
    'edit' {
      $er = $req.request; if (-not $er.asset_path) { Fail "edit: request.asset_path missing" 30 }
      $tmp = Join-Path $env:TEMP ("bpagent_edit_" + (San "$($er.asset_path)") + ".json")
      ($er | ConvertTo-Json -Depth 30) | Set-Content -Encoding UTF8 $tmp
      $emode = if ($er.mode) { "$($er.mode)" } else { 'apply-and-verify' }
      $taskOut = Join-Path $outBase 'edit'
      $args = @{ ProjectUProject=$proj; AssetPath="$($er.asset_path)"; EditRequestJson=$tmp; OutputDir=$taskOut; Mode=$emode }
      if ($ue) { $args.UERoot=$ue }
      if ($exec.create_backup -ne $false) { $args.CreateBackup=$true }
      if ($er.allow_destructive_edit -or $exec.allow_destructive_edit) { $args.AllowDestructiveEdit=$true }
      if ($exec.strict) { $args.Strict=$true }
      & (Join-Path $scripts 'edit_blueprint.ps1') @args
      $rc=$LASTEXITCODE; $subOut=$taskOut; $usedMode='native_full'; if($fallbackFrom){$fallbackTo='native_full'}
    }
    'create' {
      $cr = $req.request; if (-not $cr.asset.asset_path) { Fail "create: request.asset.asset_path missing" 30 }
      $tmp = Join-Path $env:TEMP ("bpagent_create_" + (San "$($cr.asset.asset_path)") + ".json")
      (@{ request=$cr } | ConvertTo-Json -Depth 40) | Set-Content -Encoding UTF8 $tmp
      $taskOut = Join-Path (Join-Path $outBase 'create') (San "$($cr.asset.asset_path)")
      $args = @{ ProjectUProject=$proj; SpecFile=$tmp; OutputDir=$taskOut }
      if ($ue) { $args.UERoot=$ue }
      & (Join-Path $scripts 'create_blueprint.ps1') @args
      $rc=$LASTEXITCODE; $subOut=$taskOut; $usedMode='native_full'; if($fallbackFrom){$fallbackTo='native_full'}
    }
    'validate' {
      $taskOut = Join-Path $outBase 'validate'
      New-Item -ItemType Directory -Force -Path $taskOut | Out-Null
      & (Join-Path $scripts 'validate_outputs.ps1') -OutputDir $taskOut
      $rc=$LASTEXITCODE; $subOut=$taskOut; $usedMode='offline_asset_scan'
    }
  }
}

$status = switch ($rc) { 0 {'success'} 10 {'partial'} 20 {'failed'} 24 {'editor_live_unavailable'} 30 {'bad_input'} 40 {'rolled_back'} 41 {'exists_refused'} default {"exit_$rc"} }
$dispatch = [ordered]@{
  schema_version='1.0'; status=$status; task_type=$taskType; mode=$usedMode; requested_mode=$mode
  editor_live=[ordered]@{ attempted=[bool]$tryLive; available=$liveAvailable; fallback_from=$fallbackFrom; fallback_to=$fallbackTo }
  project_uproject=$proj; ue_root=$ue; request_json=$(if($RequestJson){(Resolve-Path $RequestJson).Path}else{''})
  sub_output_dir=$subOut; sub_manifest=$(if($subOut){Join-Path $subOut 'manifest.json'}else{''}); exit_code=$rc
  generated_at=(Get-Date).ToUniversalTime().ToString('o')
  note='Read sub_manifest for full details. editor_live results live under <output_dir>/editor_live/<request_id>/.'
}
$dispDir = Join-Path $outBase $taskType
New-Item -ItemType Directory -Force -Path $dispDir | Out-Null
[IO.File]::WriteAllText((Join-Path $dispDir 'dispatch_manifest.json'), ($dispatch | ConvertTo-Json -Depth 12), (New-Object System.Text.UTF8Encoding($false)))  # no-BOM UTF-8
$col = if ($rc -eq 0) {'Green'} elseif ($rc -eq 24) {'Yellow'} else {'Yellow'}
Write-Host "blueprint_agent: $taskType -> $status (exit $rc) mode=$usedMode. sub_output=$subOut" -ForegroundColor $col
Write-Host "dispatch manifest: $(Join-Path $dispDir 'dispatch_manifest.json')"
exit $rc
