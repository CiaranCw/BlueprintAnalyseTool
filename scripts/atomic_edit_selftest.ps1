<#
  atomic_edit_selftest.ps1 — proves the BPATEdit atomic-edit capability end to end.

  Each case runs against an ISOLATED copy (-WorkOnCopy) of a stable source blueprint,
  so the real test suite assets are never modified. Asserts the resulting
  edit_result.json status (and a couple of diff facts) against expectations.

  Usage:
    .\atomic_edit_selftest.ps1 -UERoot "<UE>" -ProjectUProject "<...>.uproject" [-Source "/Game/..."] [-OutputDir "<dir>"]

  Exit codes: 0 = all cases passed, 20 = at least one failed, 30 = setup error.
  Output: <OutputDir>\selftest_summary.json
#>
[CmdletBinding()]
param(
  [string] $UERoot = $env:UE_ROOT,
  [Parameter(Mandatory=$true)] [string] $ProjectUProject,
  [string] $Source = "/Game/BPParserTest/BP_01_PrimitivePins_Basic",
  [string] $OutputDir = ""
)
$ErrorActionPreference = 'Stop'
$ProjectDir = Split-Path $ProjectUProject -Parent
$editScript = Join-Path $PSScriptRoot 'edit_blueprint.ps1'
if (-not (Test-Path $editScript)) { Write-Error "edit_blueprint.ps1 not found"; exit 30 }
if ((Get-Process -Name UnrealEditor -ErrorAction SilentlyContinue)) { Write-Error "Close the UE editor first."; exit 30 }

if ([string]::IsNullOrWhiteSpace($OutputDir)) { $OutputDir = Join-Path $ProjectDir 'Saved\BPParserAgentReports\atomic_edit_selftest' }
New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null
$reqDir = Join-Path $OutputDir 'requests'
New-Item -ItemType Directory -Force -Path $reqDir | Out-Null
$reportRoot = Join-Path $ProjectDir 'Saved\BPParserAgentReports'

# Selectors reused across cases (BP_01 is stable: BeginPlay -> Set TestInt -> Set TestTransform -> Print).
# UE adds disabled ghost Event stubs, so the BeginPlay selector also requires its exec-out to be
# connected (selects the real, wired event rather than the placeholder).
$selBegin = @{ node_class='K2Node_Event';       node_title_contains='BeginPlay'; exec_out_connected=$true }
$selSetInt= @{ node_class='K2Node_VariableSet';  node_title_contains='TestInt' }
$selAdd   = @{ node_class='K2Node_CallFunction'; function_name='Add_IntInt' }
$selPrint = @{ node_class='K2Node_CallFunction'; function_name='PrintString' }

$cases = @(
  @{ name='insert_node_between'; mode='apply-and-verify'; allow=$true; expect='success';
     ops=@(@{ op_id='op1'; operation='insert_node_between'; graph='EventGraph';
              from_node=$selBegin; to_node=$selSetInt;
              new_node=@{ node_class='K2Node_ExecutionSequence'; num_outputs=2; position=@{x=120;y=-80} } }) }

  @{ name='set_pin_default_value'; mode='apply-and-verify'; allow=$false; expect='success';
     ops=@(@{ op_id='op1'; operation='set_pin_default_value'; graph='EventGraph';
              node=$selAdd; pin='B'; value='99' }) }

  @{ name='add_variable'; mode='apply-and-verify'; allow=$false; expect='success';
     ops=@(@{ op_id='op1'; operation='add_variable';
              var_name='SelfTestVar'; var_type=@{ category='int' }; default_value='7'; category='SelfTest' }) }

  @{ name='remove_node_preserve_exec'; mode='apply-and-verify'; allow=$true; expect='success';
     ops=@(@{ op_id='op1'; operation='remove_node'; graph='EventGraph'; node=$selPrint; preserve_exec=$true }) }

  @{ name='add_reroute_on_edge'; mode='apply-and-verify'; allow=$true; expect='success';
     ops=@(@{ op_id='op1'; operation='add_reroute_on_edge'; graph='EventGraph';
              from_node=$selBegin; from_pin='then'; to_node=$selSetInt; to_pin='execute' }) }

  @{ name='destructive_refusal'; mode='apply-and-verify'; allow=$false; expect='partial';
     ops=@(@{ op_id='op1'; operation='insert_node_between'; graph='EventGraph';
              from_node=$selBegin; to_node=$selSetInt;
              new_node=@{ node_class='K2Node_ExecutionSequence'; num_outputs=2 } }) }

  @{ name='plan_only_preview_destructive'; mode='plan-only'; allow=$false; expect='success';
     ops=@(@{ op_id='op1'; operation='remove_node'; graph='EventGraph'; node=$selPrint; preserve_exec=$true }) }

  @{ name='rollback_on_failure'; mode='apply-and-verify'; allow=$true; expect='rolled_back';
     ops=@(@{ op_id='op1'; operation='remove_node'; graph='EventGraph';
              node=@{ node_class='K2Node_CallFunction'; function_name='ThisFunctionDoesNotExist' }; preserve_exec=$true }) }
)

function Newest-ResultDir {
  $sani = ($Source -replace '[/\\.:]', '_')
  $base = Join-Path $reportRoot (Join-Path $sani 'edits')
  if (-not (Test-Path $base)) { return $null }
  return (Get-ChildItem $base -Directory | Sort-Object LastWriteTime -Desc | Select-Object -First 1)
}

$results = @()
$idx = 0
$runId = Get-Date -Format 'yyyyMMddHHmmss'
foreach ($c in $cases) {
  $idx++
  $copy = "/Game/BPParserScratch/run${runId}_t$idx"
  $req = [ordered]@{
    schema_version='1.0'; asset_path=$Source; intent=$c.name; mode=$c.mode
    allow_destructive_edit=[bool]$c.allow; create_backup=$true; operations=$c.ops
  }
  $reqFile = Join-Path $reqDir ("req_{0}_{1}.json" -f $idx, $c.name)
  ($req | ConvertTo-Json -Depth 12) | Set-Content -Encoding UTF8 $reqFile

  $splat = @{
    UERoot          = $UERoot
    ProjectUProject = $ProjectUProject
    AssetPath       = $Source
    EditRequestJson = $reqFile
    Mode            = $c.mode
    WorkOnCopy      = $copy
    CreateBackup    = $true
  }
  if ($c.allow) { $splat.AllowDestructiveEdit = $true }

  Write-Host ("[{0}] {1} ({2}) ..." -f $idx, $c.name, $c.mode) -ForegroundColor Cyan
  & $editScript @splat | Out-Null
  $rc = $LASTEXITCODE
  $actual = switch ($rc) { 0 {'success'} 10 {'partial'} 20 {'failed'} 30 {'bad_input'} 40 {'rolled_back'} default {"exit_$rc"} }

  $resDir = Newest-ResultDir
  $diffFacts = ""
  if ($resDir) {
    $resFile = Join-Path $resDir.FullName 'diff_report.json'
    if (Test-Path $resFile) {
      $d = [System.IO.File]::ReadAllText($resFile, (New-Object System.Text.UTF8Encoding($false))) | ConvertFrom-Json
      $diffFacts = "added=$(@($d.added_nodes).Count) removed=$(@($d.removed_nodes).Count) +edges=$(@($d.added_edges).Count) -edges=$(@($d.removed_edges).Count)"
    }
  }
  $pass = ($actual -eq $c.expect)
  $results += [ordered]@{ idx=$idx; name=$c.name; mode=$c.mode; expected=$c.expect; actual=$actual; exit=$rc; pass=$pass; diff=$diffFacts; report_dir=($resDir.FullName) }
  $col = if ($pass) { 'Green' } else { 'Red' }
  Write-Host ("    -> {0} (expected {1})  {2}" -f $actual, $c.expect, $diffFacts) -ForegroundColor $col
}

$passCount = (@($results | Where-Object { $_.pass })).Count
$summary = [ordered]@{
  schema_version='1.0'; source=$Source; total=$results.Count; passed=$passCount; failed=($results.Count - $passCount); cases=$results
}
$summaryFile = Join-Path $OutputDir 'selftest_summary.json'
($summary | ConvertTo-Json -Depth 8) | Set-Content -Encoding UTF8 $summaryFile

Write-Host ""
$allColor = if ($passCount -eq $results.Count) { 'Green' } else { 'Red' }
Write-Host ("Atomic edit self-test: {0}/{1} passed. Summary: {2}" -f $passCount, $results.Count, $summaryFile) -ForegroundColor $allColor
if ($passCount -eq $results.Count) { exit 0 } else { exit 20 }
