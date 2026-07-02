<#
  analyze_blueprint.ps1 - unified, cross-version, agent-callable Blueprint analyzer.

  Three layered modes (see docs/fallback_modes.md):
    offline        - no UE launch: project/version/asset-path/uasset-header scan.
    python-partial - target UE + PythonScriptPlugin, read-only reflection (no build, no plugin).
    native-full    - target project + our C++ read-only dumper commandlet -> full Graph/Node/Pin/Edge IR.
    auto           - offline -> (native-full if feasible/allowed) -> python-partial -> keep best; never silent-fail.

  ALWAYS writes manifest.json. Strictly read-only w.r.t. the user's blueprint asset.
  Nothing is hardcoded to a specific project or engine.

  Usage:
    .\analyze_blueprint.ps1 -ProjectUProject "<...>.uproject" -AssetPath "/Game/UI/WBP_X" `
        [-UERoot "<engine>"] [-OutputDir "<dir>"] [-Mode auto|offline|python-partial|native-full] `
        [-AllowPluginInstall] [-AllowBuild] [-Strict]

  Exit codes: 0 success, 10 partial, 20 failed, 30 bad input.
#>
[CmdletBinding()]
param(
  [Parameter(Mandatory=$true)] [string] $ProjectUProject,
  [Parameter(Mandatory=$true)] [string] $AssetPath,
  [string] $UERoot = "",
  [string] $OutputDir = "",
  [ValidateSet('auto','offline','python-partial','native-full')]
  [string] $Mode = 'auto',
  [switch] $AllowPluginInstall,   # gates native-full's invasive plugin copy into a foreign project
  [switch] $AllowBuild,           # gates native-full's incremental compile of the target project
  [switch] $Strict
)
$ErrorActionPreference = 'Continue'
$RepoRoot = Split-Path $PSScriptRoot -Parent

function Warn($m){ Write-Host "[warn] $m" -ForegroundColor Yellow }
function Info($m){ Write-Host "[info] $m" -ForegroundColor Cyan }

# ---------------------------------------------------------------- helpers ----
function Resolve-UERootFromAssociation([string]$assoc) {
  if ($assoc -match '^\d+\.\d+$') {
    foreach ($rp in @("HKLM:\SOFTWARE\EpicGames\Unreal Engine\$assoc","HKLM:\SOFTWARE\WOW6432Node\EpicGames\Unreal Engine\$assoc")) {
      try { $p=(Get-ItemProperty -Path $rp -EA Stop).InstalledDirectory; if ($p -and (Test-Path $p)) { return $p } } catch {}
    }
    foreach ($d in 'C','D','E','F') {
      foreach ($base in @("$($d):\Program Files\Epic Games\UE_$assoc","$($d):\Epic Games\UE_$assoc","$($d):\UE\UE_$assoc","$($d):\software\UE\UE_$assoc")) {
        if (Test-Path (Join-Path $base 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe')) { return $base }
      }
    }
  } else {
    # GUID association -> source/custom build registered under Builds
    foreach ($rp in @("HKCU:\SOFTWARE\Epic Games\Unreal Engine\Builds","HKLM:\SOFTWARE\Epic Games\Unreal Engine\Builds")) {
      try { $b=Get-ItemProperty -Path $rp -EA Stop; $v=$b.$assoc; if ($v -and (Test-Path $v)) { return $v } } catch {}
    }
  }
  return ""
}

function Get-EngineInfo([string]$ueRoot,[string]$assoc) {
  $ver=""; $custom=$false
  $bv = Join-Path $ueRoot 'Engine\Build\Build.version'
  if (Test-Path $bv) {
    try { $j=Get-Content $bv -Raw | ConvertFrom-Json; $ver="$($j.MajorVersion).$($j.MinorVersion).$($j.PatchVersion)" } catch {}
  }
  # source/custom heuristic: GUID association, or no InstalledBuild.txt marker
  if ($assoc -notmatch '^\d+\.\d+$') { $custom=$true }
  if (-not (Test-Path (Join-Path $ueRoot 'Engine\Build\InstalledBuild.txt'))) { $custom=$true }
  return @{ version=$ver; is_custom=$custom }
}

function To-PackagePath([string]$assetPath,[string]$projectDir) {
  # accept "/Game/..." or a local .uasset path under <project>/Content
  if ($assetPath -like '/Game/*' -or $assetPath -like '/Engine/*') { return ($assetPath -replace '\.uasset$','') }
  $full = try { (Resolve-Path $assetPath -EA Stop).Path } catch { $assetPath }
  $content = Join-Path $projectDir 'Content'
  if ($full.ToLower().StartsWith($content.ToLower())) {
    $rel = $full.Substring($content.Length).TrimStart('\','/') -replace '\\','/' -replace '\.uasset$',''
    return "/Game/$rel"
  }
  return ($assetPath -replace '\.uasset$','')
}

function Read-UAssetVersions([string]$file) {
  $o=@{ package_tag=""; file_version_ue4=$null; file_version_ue5=$null }
  try {
    $fs=[IO.File]::OpenRead($file); $br=New-Object IO.BinaryReader($fs)
    $tag=$br.ReadUInt32(); $legacy=$br.ReadInt32(); $ue3=$br.ReadInt32(); $ue4=$br.ReadInt32(); $ue5=$br.ReadInt32()
    $o.package_tag=("0x{0:X8}" -f $tag); $o.file_version_ue4=$ue4; $o.file_version_ue5=$ue5
    $br.Close(); $fs.Close()
  } catch {}
  return $o
}

function New-Sanitized([string]$assetPath) { return ($assetPath -replace '[/\\.:]', '_').Trim('_') }

# --------------------------------------------------- output (common layer) ----
# Builds the unified deliverables (manifest/summary/score/viz/logs) from whatever IR we have.
function Write-Outputs {
  param($OutDir,$Mode,$Status,$Ir,$Meta,$Warnings,$Errors,$Manual,$Fallbacks)
  New-Item -ItemType Directory -Force -Path $OutDir,(Join-Path $OutDir 'logs'),(Join-Path $OutDir 'viz'),(Join-Path $OutDir 'graphs') | Out-Null

  # counts (tolerant of partial IR)
  $graphs = @(); if ($Ir -and $Ir.graphs) { $graphs = @($Ir.graphs) }
  $nodeCount=0;$pinCount=0;$edgeCount=0
  foreach($g in $graphs){ $nodeCount += @($g.nodes).Count; $edgeCount += @($g.edges).Count; foreach($n in @($g.nodes)){ $pinCount += @($n.pins).Count } }
  $bp = if($Ir){ $Ir.blueprint } else { $null }
  $counts = [ordered]@{
    graphs=$graphs.Count; nodes=$nodeCount; pins=$pinCount; edges=$edgeCount
    variables=@($bp.variables).Count; functions=@($bp.functions).Count; macros=@($bp.macros).Count
    dispatchers=@($bp.event_dispatchers).Count; components=@($bp.components).Count
    interfaces=@($Ir.asset.implemented_interfaces).Count; dependencies=@($Ir.asset.dependencies).Count
  }

  # write the IR under the canonical name
  $irName = if ($Status -eq 'success' -and $Mode -eq 'native_full') { 'blueprint_ir.json' } else { 'partial_ir.json' }
  if ($Ir) { ($Ir | ConvertTo-Json -Depth 30) | Set-Content -Encoding UTF8 (Join-Path $OutDir $irName) }

  # per-graph json
  foreach($g in $graphs){ $gn = New-Sanitized ($g.graph_name); if($gn){ ($g | ConvertTo-Json -Depth 30) | Set-Content -Encoding UTF8 (Join-Path $OutDir "graphs\$gn.json") } }

  # viz (dot + mermaid); minimal but always present
  $dot = "digraph BP {`n  rankdir=LR;`n  label=""$($Meta.asset_name) [$Mode/$Status]"";`n  node[shape=box,style=rounded];`n"
  $mmd = "%% $($Meta.asset_name) [$Mode/$Status]`nflowchart LR`n"
  if ($graphs.Count -gt 0) {
    foreach($g in $graphs){
      foreach($n in @($g.nodes)){
        $id = ($n.node_id) -replace '[^A-Za-z0-9]',''
        $lbl = ("$($n.node_title)" -replace '"',"'") -replace '\n',' '
        $dot += "  n$id [label=""$lbl""];`n"; $mmd += "  n$id[""$lbl""]`n"
      }
      foreach($e in @($g.edges)){
        $a=($e.from_node) -replace '[^A-Za-z0-9]',''; $b=($e.to_node) -replace '[^A-Za-z0-9]',''
        $style = if($e.edge_type -eq 'exec'){'solid'}else{'dashed'}
        $dot += "  n$a -> n$b [style=$style];`n"; $mmd += "  n$a --> n$b`n"
      }
    }
  } else {
    $dot += "  note [label=""No graph IR in $Mode mode.\nRun native_full for full Graph/Node/Pin/Edge."",shape=note];`n"
    $mmd += "  note[""No graph IR in $Mode mode - run native_full for full graph""]`n"
  }
  $dot += "}`n"
  Set-Content -Encoding UTF8 (Join-Path $OutDir 'viz\blueprint.dot') $dot
  Set-Content -Encoding UTF8 (Join-Path $OutDir 'viz\blueprint.mmd') $mmd

  # summary.md
  $sum = @"
# Blueprint Understanding Summary

## 1. Asset Overview
- asset_path: ``$($Meta.asset_path)``
- asset_name: $($Meta.asset_name)
- asset_type: $($Ir.asset.asset_type)
- parent_class: $($Ir.asset.parent_class)
- generated_class: $($Ir.asset.generated_class)
- implemented_interfaces: $(@($Ir.asset.implemented_interfaces) -join ', ')
- project: ``$($Meta.project_uproject)``  | engine: $($Meta.engine_version) $(if($Meta.is_custom_engine){'(custom/source)'})
- analysis mode: **$Mode**  | status: **$Status**

## 2. Counts
graphs=$($counts.graphs) nodes=$($counts.nodes) pins=$($counts.pins) edges=$($counts.edges) | variables=$($counts.variables) functions=$($counts.functions) components=$($counts.components) dispatchers=$($counts.dispatchers) interfaces=$($counts.interfaces) dependencies=$($counts.dependencies)

## 3. Dependencies (first 30)
$((@($Ir.asset.dependencies) | Select-Object -First 30 | ForEach-Object { "- $_" }) -join "`n")

## 4. Graphs
$((@($graphs) | ForEach-Object { "- $($_.graph_name) [$($_.graph_type)] nodes=$(@($_.nodes).Count) edges=$(@($_.edges).Count)" }) -join "`n")

## 5. Confidence & Limitations
- mode=$Mode. $(if($Mode -ne 'native_full'){'Graph/Node/Pin/Edge NOT fully available in this mode; run native_full for the complete IR.'}else{'Full EdGraph IR extracted by native dumper.'})
- warnings: $(@($Warnings).Count) ; errors: $(@($Errors).Count) ; fallbacks_used: $(@($Fallbacks) -join ', ')

## 6. Manual Check Required
$((@($Manual) | ForEach-Object { "- $_" }) -join "`n")
"@
  Set-Content -Encoding UTF8 (Join-Path $OutDir 'summary.md') $sum

  # understanding_score.json
  $sc = { param($ok) if($ok){'complete'}else{'failed'} }
  $hasGraphs = $graphs.Count -gt 0
  $score = [ordered]@{
    schema_version='1.0'; asset_path=$Meta.asset_path; status=$Status
    score=[ordered]@{
      asset_load = if($Ir.asset.asset_type){'complete'}elseif($Status -eq 'failed'){'failed'}else{'partial'}
      graph_discovery = if($hasGraphs){'complete'}else{'partial'}
      node_discovery = if($nodeCount -gt 0){'complete'}else{'partial'}
      pin_discovery = if($pinCount -gt 0){'complete'}else{'partial'}
      edge_discovery = if($edgeCount -gt 0){'complete'}else{'partial'}
      variable_discovery = if(@($bp.variables).Count -gt 0){'complete'}else{'partial'}
      function_discovery = if(@($bp.functions).Count -gt 0){'complete'}else{'partial'}
      dispatcher_discovery = if(@($bp.event_dispatchers).Count -gt 0){'complete'}else{'partial'}
      component_discovery = if(@($bp.components).Count -gt 0){'complete'}else{'partial'}
      dependency_discovery = if(@($Ir.asset.dependencies).Count -gt 0){'complete'}else{'partial'}
      visualization = if($hasGraphs){'complete'}else{'partial'}
      agent_callable = 'complete'
    }
    confidence = if($Mode -eq 'native_full' -and $hasGraphs){0.9}elseif($Mode -eq 'python_partial'){0.4}else{0.2}
    limitations = @(if($Mode -ne 'native_full'){'No full EdGraph (node/pin/edge) in this mode.'})
    manual_check_required = @($Manual)
    next_actions = @(if($Mode -ne 'native_full'){'Run -Mode native-full (needs the read-only plugin built into the target project) for full IR.'})
  }
  ($score | ConvertTo-Json -Depth 8) | Set-Content -Encoding UTF8 (Join-Path $OutDir 'understanding_score.json')

  # logs
  (@($Warnings) | ConvertTo-Json -Depth 5) | Set-Content -Encoding UTF8 (Join-Path $OutDir 'logs\warnings.json')
  (@($Errors)   | ConvertTo-Json -Depth 5) | Set-Content -Encoding UTF8 (Join-Path $OutDir 'logs\errors.json')

  # manifest.json (primary entry for other agents)
  $manifest = [ordered]@{
    schema_version='1.0'; status=$Status; mode=$Mode
    asset_path=$Meta.asset_path; asset_name=$Meta.asset_name
    asset_type=$Ir.asset.asset_type; parent_class=$Ir.asset.parent_class
    project_uproject=$Meta.project_uproject; ue_root=$Meta.ue_root
    engine_version=$Meta.engine_version; is_custom_engine=[bool]$Meta.is_custom_engine
    plugin_installed=[bool]$Meta.plugin_installed; plugin_built=[bool]$Meta.plugin_built
    read_only=$true; generated_at=(Get-Date).ToUniversalTime().ToString('o')
    fallbacks_used=@($Fallbacks)
    outputs=[ordered]@{ ir=$irName; summary='summary.md'; understanding_score='understanding_score.json'; dot='viz/blueprint.dot'; mermaid='viz/blueprint.mmd'; png=''; svg=''; logs='logs/' }
    counts=$counts
    warnings=@($Warnings); errors=@($Errors); manual_check_required=@($Manual)
  }
  ($manifest | ConvertTo-Json -Depth 30) | Set-Content -Encoding UTF8 (Join-Path $OutDir 'manifest.json')
  return $manifest
}

# ======================================================================= main
if (-not (Test-Path $ProjectUProject)) { Write-Error "Project not found: $ProjectUProject"; exit 30 }
$ProjectDir = Split-Path $ProjectUProject -Parent
$assoc = try { (Get-Content $ProjectUProject -Raw | ConvertFrom-Json).EngineAssociation } catch { "" }
if ([string]::IsNullOrWhiteSpace($UERoot)) { $UERoot = Resolve-UERootFromAssociation $assoc }
$cmdExe = if ($UERoot) { Join-Path $UERoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe' } else { "" }
$eng = if ($UERoot) { Get-EngineInfo $UERoot $assoc } else { @{version="";is_custom=$false} }
$pkgPath = To-PackagePath $AssetPath $ProjectDir
if ([string]::IsNullOrWhiteSpace($OutputDir)) { $OutputDir = Join-Path $ProjectDir 'Saved\BPParserAgentReports' }
$OutDir = Join-Path $OutputDir (New-Sanitized $pkgPath)

$meta = @{
  asset_path=$pkgPath; asset_name=($pkgPath -replace '.*/',''); project_uproject=$ProjectUProject
  ue_root=$UERoot; engine_version=$eng.version; is_custom_engine=$eng.is_custom
  plugin_installed=$false; plugin_built=$false
}
$warn=New-Object Collections.ArrayList; $err=New-Object Collections.ArrayList
$manual=New-Object Collections.ArrayList; $fallbacks=New-Object Collections.ArrayList

Info "project=$ProjectUProject"; Info "assoc=$assoc  UERoot=$(if($UERoot){$UERoot}else{'<not found>'})  engine=$($eng.version) custom=$($eng.is_custom)"
Info "asset(pkg)=$pkgPath  mode=$Mode  out=$OutDir"
$ueOk = ($cmdExe -and (Test-Path $cmdExe))
if (-not $ueOk) { [void]$warn.Add("UnrealEditor-Cmd not found (UERoot='$UERoot'); native/python modes unavailable.") }

# ---- mode runners ---------------------------------------------------------
function Run-Offline {
  $target = if ($AssetPath -like '/Game/*') { $null } else { $AssetPath }
  $local = if ($target -and (Test-Path $target)) { $target } else {
    $c = Join-Path $ProjectDir ("Content\" + ($pkgPath -replace '^/Game/','') + ".uasset"); if (Test-Path $c) { $c } else { $null }
  }
  $ver = if ($local) { Read-UAssetVersions $local } else { $null }
  if (-not $local) { [void]$warn.Add("offline: .uasset file not located on disk for $pkgPath") }
  [void]$manual.Add("offline mode cannot extract Graph/Node/Pin/Edge; use python-partial or native-full.")
  $ir = [ordered]@{
    schema_version='1.0'; mode='offline_asset_scan'; partial=$true
    asset=[ordered]@{ asset_path=$pkgPath; asset_name=$meta.asset_name; asset_type='unknown(offline)';
      blueprint_class=''; generated_class=''; parent_class=''; implemented_interfaces=@(); dependencies=@();
      uasset_file=$local; package_version=$ver }
    blueprint=[ordered]@{ variables=@(); functions=@(); macros=@(); event_dispatchers=@(); components=@(); timelines=@(); graphs=@() }
    graphs=@(); analysis=[ordered]@{ manual_check_required=@($manual) }
  }
  $status = if ($local) { 'partial' } else { 'failed' }
  return @{ ir=$ir; status=$status }
}

function Run-Python {
  if (-not $ueOk) { [void]$err.Add("python-partial: UnrealEditor-Cmd unavailable"); return $null }
  $py = Join-Path $PSScriptRoot 'bp_analyze.py'
  $raw = Join-Path $OutDir 'python_partial_ir.json'
  New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
  $env:BPAT_ASSET=$pkgPath; $env:BPAT_OUT=$raw; $env:BPAT_LOG=(Join-Path $OutDir 'logs\python_log.txt')
  New-Item -ItemType Directory -Force -Path (Join-Path $OutDir 'logs') | Out-Null
  Info "python-partial: launching $cmdExe (this loads the target project; may take a while)..."
  & $cmdExe "$ProjectUProject" -run=pythonscript -script="$py" -unattended -nopause -nosplash -nullrhi -stdout *>&1 | Out-File -Encoding utf8 (Join-Path $OutDir 'logs\python_run.txt')
  if (-not (Test-Path $raw)) { [void]$err.Add("python-partial: no output produced (see logs/python_run.txt)"); return $null }
  $ir = Get-Content $raw -Raw | ConvertFrom-Json
  foreach($w in @($ir.warnings)){ [void]$warn.Add("python: $w") }
  foreach($e in @($ir.errors)){ [void]$err.Add("python: $e") }
  return @{ ir=$ir; status='partial' }
}

function Run-Native {
  if (-not $ueOk) { [void]$err.Add("native-full: UnrealEditor-Cmd unavailable"); return $null }
  $pluginDir = Join-Path $ProjectDir 'Plugins\BPParserTestGen'
  $meta.plugin_installed = (Test-Path $pluginDir)
  if (-not $meta.plugin_installed) {
    if (-not $AllowPluginInstall) { [void]$err.Add("native-full: plugin not installed in target project and -AllowPluginInstall not set."); return $null }
    & (Join-Path $PSScriptRoot 'install_project_plugin.ps1') -ProjectUProject $ProjectUProject -PluginSource (Join-Path $RepoRoot 'bpparser_testgen\Plugins\BPParserTestGen') | Out-Null
    $meta.plugin_installed = (Test-Path $pluginDir)
  }
  # ensure built (heuristic: editor DLL for the plugin present) — else build if allowed
  if ($AllowBuild) {
    & (Join-Path $PSScriptRoot 'build_project_plugin.ps1') -UERoot $UERoot -ProjectUProject $ProjectUProject -PluginName 'BPParserTestGen' *>&1 | Out-File -Encoding utf8 (Join-Path $OutDir 'logs\native_build.txt')
    if ($LASTEXITCODE -ne 0) { [void]$err.Add("native-full: build failed (see logs/native_build.txt)"); return $null }
  }
  $meta.plugin_built = $true
  $rawDir = Join-Path $OutDir 'native_raw'
  New-Item -ItemType Directory -Force -Path $rawDir,(Join-Path $OutDir 'logs') | Out-Null
  Info "native-full: running BPParserTestDump commandlet..."
  & $cmdExe "$ProjectUProject" -run=BPParserTestDump -AssetPath="$pkgPath" -OutputDir="$rawDir" -unattended -nopause -stdout *>&1 | Out-File -Encoding utf8 (Join-Path $OutDir 'logs\native_run.txt')
  $short = ($pkgPath -replace '.*/','')
  $irFile = Join-Path $rawDir "$short.ir.json"
  if (-not (Test-Path $irFile)) { [void]$err.Add("native-full: dumper produced no IR (see logs/native_run.txt)"); return $null }
  $dump = Get-Content $irFile -Raw | ConvertFrom-Json
  # adapt bpat-ir-dump -> unified IR shape
  $ir = [ordered]@{
    schema_version='1.0'; mode='native_full'; partial=$false
    asset=[ordered]@{ asset_path=$pkgPath; asset_name=$meta.asset_name; asset_type=$dump.blueprint_class;
      blueprint_class=$dump.blueprint_class; generated_class=''; parent_class=$dump.parent_class;
      implemented_interfaces=@($dump.interfaces); dependencies=@() }
    blueprint=[ordered]@{ variables=@($dump.variables); functions=@($dump.functions); macros=@($dump.macros);
      event_dispatchers=@($dump.event_dispatchers); components=@(); timelines=@(); graphs=@($dump.graphs | ForEach-Object { $_.graph_name }) }
    graphs=@($dump.graphs); analysis=[ordered]@{ manual_check_required=@() }
  }
  return @{ ir=$ir; status='success' }
}

# ---- dispatch with fallback ----------------------------------------------
$result=$null; $usedMode=''
function Try-Mode($name,$fn){
  $r = & $fn
  if ($r -and $r.ir) { $script:result=$r; $script:usedMode=$name; return $true }
  return $false
}

switch ($Mode) {
  'offline'        { [void](Try-Mode 'offline_asset_scan' { Run-Offline }) }
  'python-partial' { if (-not (Try-Mode 'python_partial' { Run-Python })) { [void]$fallbacks.Add('offline_asset_scan'); [void](Try-Mode 'offline_asset_scan' { Run-Offline }) } }
  'native-full'    { if (-not (Try-Mode 'native_full' { Run-Native })) { [void]$fallbacks.Add('python_partial'); if (-not (Try-Mode 'python_partial' { Run-Python })) { [void]$fallbacks.Add('offline_asset_scan'); [void](Try-Mode 'offline_asset_scan' { Run-Offline }) } } }
  'auto' {
    # always have an offline baseline
    $offline = Run-Offline
    $nativeFeasible = $ueOk -and ((Test-Path (Join-Path $ProjectDir 'Plugins\BPParserTestGen')) -or $AllowPluginInstall)
    if ($nativeFeasible -and (Try-Mode 'native_full' { Run-Native })) { }
    elseif ($ueOk) { [void]$fallbacks.Add('native_full->python_partial'); if (-not (Try-Mode 'python_partial' { Run-Python })) { [void]$fallbacks.Add('python_partial->offline'); $script:result=$offline; $script:usedMode='offline_asset_scan' } }
    else { [void]$fallbacks.Add('offline_only'); $script:result=$offline; $script:usedMode='offline_asset_scan' }
  }
}

if (-not $result) { $result = @{ ir=$null; status='failed' } ; $usedMode = if($usedMode){$usedMode}else{'offline_asset_scan'} ; [void]$err.Add("all modes failed to produce IR") }

# pull asset-level fields into meta for manifest
if ($result.ir) { $meta.asset_type=$result.ir.asset.asset_type; $meta.parent_class=$result.ir.asset.parent_class }
$manifest = Write-Outputs -OutDir $OutDir -Mode $usedMode -Status $result.status -Ir $result.ir -Meta $meta -Warnings $warn -Errors $err -Manual $manual -Fallbacks $fallbacks

Write-Host ""
Write-Host ("analyze_blueprint: status=$($result.status) mode=$usedMode -> $OutDir\manifest.json") -ForegroundColor $(if($result.status -eq 'success'){'Green'}elseif($result.status -eq 'failed'){'Red'}else{'Yellow'})
switch ($result.status) { 'success' { exit 0 } 'partial' { if($Strict){exit 10}else{exit 0} } default { exit 20 } }
