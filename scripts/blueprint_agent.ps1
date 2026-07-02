<#
  blueprint_agent.ps1 - single, agent-callable entry point for the Blueprint Agent.
  Other AIs provide ONE request.json (task_type = analyze|edit|create|validate) and read one
  dispatch manifest. Routes to the specialized tools; nothing hardcoded to a project/engine.

  Usage:
    .\blueprint_agent.ps1 -RequestJson ".\request.json" [-OutputDir "<override>"] [-UERoot "<override>"] [-ProjectUProject "<override>"]

  request.json (see docs/request_schemas.md):
    { schema_version, task_type, project:{ue_root,uproject,output_dir,engine_policy:{...}},
      execution:{mode,strict,read_only,create_backup,allow_destructive_edit,render_preview}, request:{...} }

  Exit codes: 0 success, 10 partial, 20 failed, 30 bad input, 40 rolled_back, 41 exists_refused.
  Output: <output_dir>/<task_type>/<Sanitized>/manifest.json  (+ specialized artifacts)
#>
[CmdletBinding()]
param(
  [Parameter(Mandatory=$true)] [string] $RequestJson,
  [string] $OutputDir = "",
  [string] $UERoot = "",
  [string] $ProjectUProject = ""
)
$ErrorActionPreference='Stop'
function Fail($m,$c){ Write-Error $m; exit $c }
if (-not (Test-Path $RequestJson)) { Fail "Request not found: $RequestJson" 30 }
try { $req = Get-Content $RequestJson -Raw | ConvertFrom-Json } catch { Fail "request.json is not valid JSON: $_" 30 }

$taskType = "$($req.task_type)".ToLower()
if ($taskType -notin @('status','warmup','analyze','edit','create','validate')) { Fail "task_type must be status|warmup|analyze|edit|create|validate (got '$taskType')" 30 }

# resolve project/exec (param overrides win)
$proj = if ($ProjectUProject) { $ProjectUProject } else { "$($req.project.uproject)" }
$ue   = if ($UERoot) { $UERoot } else { "$($req.project.ue_root)" }
$outBase = if ($OutputDir) { $OutputDir } elseif ($req.project.output_dir) { "$($req.project.output_dir)" } else { Join-Path (Split-Path $proj -Parent) 'Saved\BPParserAgentReports' }
if (-not $proj) { Fail "project.uproject missing" 30 }
$mode = if ($req.execution.mode) { "$($req.execution.mode)" } else { 'auto' }
$pol  = $req.project.engine_policy
$exec = $req.execution
function San([string]$s){ return ($s -replace '[/\\.:]','_').Trim('_') }

Write-Host "== blueprint_agent ==" -ForegroundColor Cyan
Write-Host "task_type=$taskType  mode=$mode  project=$proj"

$scripts = $PSScriptRoot
$rc = 0; $subOut = ""; $reqId = ""

switch ($taskType) {
  'status' {
    # read-only capability probe: which stage is the project at, what can we do now.
    $taskOut = Join-Path $outBase 'status'
    $args = @{ ProjectUProject=$proj; OutputDir=$taskOut }
    if ($ue) { $args.UERoot=$ue }
    & (Join-Path $scripts 'agent_status.ps1') @args
    $rc=$LASTEXITCODE; $subOut=$taskOut
  }
  'warmup' {
    # one-time per-project setup (install read-only plugin + incremental build) -> enables native_full.
    if (-not ($pol.allow_incremental_compile)) { Fail "warmup requires project.engine_policy.allow_incremental_compile=true (adds a plugin + builds the editor). Denied." 30 }
    $args = @{ ProjectUProject=$proj }
    if ($ue) { $args.UERoot=$ue }
    if ($req.project.plugin_source) { $args.PluginSource="$($req.project.plugin_source)" }
    if ($req.request.smoke_asset_path) { $args.SmokeAssetPath="$($req.request.smoke_asset_path)" }
    & (Join-Path $scripts 'warmup_project.ps1') @args
    $rc=$LASTEXITCODE; $subOut=(Join-Path (Split-Path $proj -Parent) 'Saved\BPParserAgentReports\warmup')
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
    if ($assets.Count -gt 1) { Write-Host "[note] batch: only first asset dispatched in this PoC; loop remaining as needed." -ForegroundColor Yellow }
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
    $rc=$LASTEXITCODE; $subOut=$taskOut
  }
  'create' {
    $cr = $req.request; if (-not $cr.asset.asset_path) { Fail "create: request.asset.asset_path missing" 30 }
    $tmp = Join-Path $env:TEMP ("bpagent_create_" + (San "$($cr.asset.asset_path)") + ".json")
    (@{ request=$cr } | ConvertTo-Json -Depth 40) | Set-Content -Encoding UTF8 $tmp
    $taskOut = Join-Path (Join-Path $outBase 'create') (San "$($cr.asset.asset_path)")
    $args = @{ ProjectUProject=$proj; SpecFile=$tmp; OutputDir=$taskOut }
    if ($ue) { $args.UERoot=$ue }
    & (Join-Path $scripts 'create_blueprint.ps1') @args
    $rc=$LASTEXITCODE; $subOut=$taskOut
  }
  'validate' {
    $taskOut = Join-Path $outBase 'validate'
    New-Item -ItemType Directory -Force -Path $taskOut | Out-Null
    & (Join-Path $scripts 'validate_outputs.ps1') -OutputDir $taskOut
    $rc=$LASTEXITCODE; $subOut=$taskOut
  }
}

$status = switch ($rc) { 0 {'success'} 10 {'partial'} 20 {'failed'} 30 {'bad_input'} 40 {'rolled_back'} 41 {'exists_refused'} default {"exit_$rc"} }
$dispatch = [ordered]@{
  schema_version='1.0'; status=$status; task_type=$taskType; mode=$mode
  project_uproject=$proj; ue_root=$ue; request_json=(Resolve-Path $RequestJson).Path
  sub_output_dir=$subOut; sub_manifest=(Join-Path $subOut 'manifest.json'); exit_code=$rc
  generated_at=(Get-Date).ToUniversalTime().ToString('o')
  note='Read sub_manifest for full details (analyze/create: manifest.json; edit: <asset>/edits/<ts>/edit_result.json).'
}
$dispDir = Join-Path $outBase $taskType
New-Item -ItemType Directory -Force -Path $dispDir | Out-Null
($dispatch | ConvertTo-Json -Depth 10) | Set-Content -Encoding UTF8 (Join-Path $dispDir 'dispatch_manifest.json')
$col = if ($rc -eq 0) {'Green'} else {'Yellow'}
Write-Host "blueprint_agent: $taskType -> $status (exit $rc). sub_output=$subOut" -ForegroundColor $col
Write-Host "dispatch manifest: $(Join-Path $dispDir 'dispatch_manifest.json')"
exit $rc
